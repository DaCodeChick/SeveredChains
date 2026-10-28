/// @file test_archive.cpp
/// @brief MRG / DEFF container tests. Fixtures follow the layouts documented in
/// archive.h and docs/research/psx-formats.md §2-§3.

#include "psx/cpp/psx.hpp"
#include "psx_test.h"

#include <cstdint>
#include <vector>

namespace
{

using Bytes = std::vector<std::uint8_t>;

void PushU32(Bytes& out, std::uint32_t V)
{
  out.push_back(static_cast<std::uint8_t>(V & 0xffu));
  out.push_back(static_cast<std::uint8_t>((V >> 8u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((V >> 16u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((V >> 24u) & 0xffu));
}

void PushU16(Bytes& out, std::uint16_t V)
{
  out.push_back(static_cast<std::uint8_t>(V & 0xffu));
  out.push_back(static_cast<std::uint8_t>((V >> 8u) & 0xffu));
}

/// @brief MRG header + entry table; payload is appended by the caller.
Bytes MakeMRGHeader(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& entries)
{
  Bytes out;
  PushU32(out, psx::archive::MRGMagic);
  PushU32(out, static_cast<std::uint32_t>(entries.size()));
  for (const auto& [offset, size] : entries)
  {
    PushU32(out, offset);
    PushU32(out, size);
  }
  return out;
}

/// @brief DEFF header + subfile start table (starts must be ascending).
Bytes MakeDEFFHeader(std::uint16_t Count, const std::vector<std::uint32_t>& Starts)
{
  Bytes out;
  PushU32(out, psx::archive::DEFFMagic);
  PushU16(out, 0); // 0x4: unused by the reference reader
  PushU16(out, Count);
  for (std::uint32_t start : Starts)
  {
    PushU32(out, 0); // entry field at +0 (unused)
    PushU32(out, start);
  }
  return out;
}

} // namespace

PSX_TEST(archive, mrg_resolves_plain_entries)
{
  // Payload follows the entry table: 8-byte header + 2 entries x 8 bytes.
  constexpr std::size_t payloadStart = 8 + 2 * 8;
  Bytes data = MakeMRGHeader({{payloadStart, 4}, {payloadStart + 4, 8}});
  const Bytes payload = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  data.insert(data.end(), payload.begin(), payload.end());

  PSX_CHECK(psx::archive::IsMRG(data));

  auto table = psx::archive::ParseMRG(data, /*sectorAligned=*/false);
  PSX_CHECK(table.has_value());
  if (!table)
    return;
  PSX_CHECK(table->Count() == 2);
  PSX_CHECK(table->VirtualCount() == 0);

  auto first = psx::archive::MRGEntryData(*table, data, 0);
  auto second = psx::archive::MRGEntryData(*table, data, 1);
  PSX_CHECK(first.has_value() && first->size() == 4);
  PSX_CHECK(second.has_value() && second->size() == 8);
  if (first && second)
  {
    PSX_CHECK((*first)[0] == 1 && (*second)[0] == 5);
    PSX_CHECK(first->data() == data.data() + payloadStart);
  }

  PSX_CHECK(!table->At(2) && table->At(2).error() == psx::Error::MRGEntryMissing);
}

PSX_TEST(archive, mrg_resolves_virtual_entries_to_their_parent)
{
  // Entry 1 has size 0 and repeats entry 0's offset -> it aliases entry 0
  // (virtual entries alias their parent).
  constexpr std::size_t payloadStart = 8 + 2 * 8;
  Bytes data = MakeMRGHeader({{payloadStart, 4}, {payloadStart, 0}});
  const Bytes payload = {0xaa, 0xbb, 0xcc, 0xdd};
  data.insert(data.end(), payload.begin(), payload.end());

  auto table = psx::archive::ParseMRG(data, false);
  PSX_CHECK(table.has_value());
  if (!table)
    return;
  PSX_CHECK(table->Count() == 2);
  PSX_CHECK(table->VirtualCount() == 1);

  auto entry = table->At(1);
  PSX_CHECK(entry.has_value());
  PSX_CHECK(entry && entry->Virtual());
  PSX_CHECK(entry && entry->parent == 0);
  PSX_CHECK(entry && entry->virtualSize == 0);
  PSX_CHECK(entry && entry->size == 4);

  auto slice = psx::archive::MRGEntryData(*table, data, 1);
  PSX_CHECK(slice.has_value() && slice->size() == 4);
}

PSX_TEST(archive, mrg_without_a_parent_keeps_an_empty_entry)
{
  Bytes data = MakeMRGHeader({{999, 0}}); // size 0, no real entry at offset 999
  auto table = psx::archive::ParseMRG(data, false);
  PSX_CHECK(table.has_value());
  if (!table)
    return;
  PSX_CHECK(table->VirtualCount() == 0);
  auto entry = table->At(0);
  PSX_CHECK(entry && !entry->Virtual() && entry->size == 0);

  auto slice = psx::archive::MRGEntryData(*table, data, 0);
  PSX_CHECK(slice.has_value() && slice->empty());
}

PSX_TEST(archive, mrg_duplicates_collapse_onto_the_first_entry)
{
  constexpr std::size_t payloadStart = 8 + 2 * 8;
  Bytes data = MakeMRGHeader({{payloadStart, 4}, {payloadStart, 4}});
  const Bytes payload = {1, 2, 3, 4};
  data.insert(data.end(), payload.begin(), payload.end());

  auto table = psx::archive::ParseMRG(data, false);
  PSX_CHECK(table.has_value());
  if (!table)
    return;
  // The reference marks the second one virtual against the first
  // (duplicate offsets collapse); its own size is kept in virtualSize.
  PSX_CHECK(table->VirtualCount() == 1);
  auto second = table->At(1);
  PSX_CHECK(second && second->Virtual() && second->parent == 0);
  PSX_CHECK(second && second->size == 4 && second->virtualSize == 4);
}

PSX_TEST(archive, mrg_applies_sector_scaling_only_when_asked)
{
  Bytes data = MakeMRGHeader({{2, 4}});
  data.insert(data.end(), 2 * psx::archive::SectorSize + 8, 0x5a);

  auto aligned = psx::archive::ParseMRG(data, /*sectorAligned=*/true);
  PSX_CHECK(aligned.has_value());
  if (aligned)
  {
    auto entry = aligned->At(0);
    PSX_CHECK(entry && entry->offset == 2 * psx::archive::SectorSize);
    auto slice = psx::archive::MRGEntryData(*aligned, data, 0);
    PSX_CHECK(slice.has_value() && slice->size() == 4);
    PSX_CHECK(slice && (*slice)[0] == 0x5a);
  }

  // Without the DRGN*.BIN sector rule the same table means byte offsets.
  auto raw = psx::archive::ParseMRG(data, false);
  PSX_CHECK(raw.has_value());
  if (raw)
  {
    auto entry = raw->At(0);
    PSX_CHECK(entry && entry->offset == 2);
  }
}

PSX_TEST(archive, mrg_rejects_bad_magic_and_impossible_tables)
{
  Bytes nope = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  PSX_CHECK(!psx::archive::IsMRG(nope));
  auto badMagic = psx::archive::ParseMRG(nope, false);
  PSX_CHECK(!badMagic && badMagic.error() == psx::Error::MRGMagic);

  const Bytes threeBytes = {0x4d, 0x52, 0x47};
  auto truncated = psx::archive::ParseMRG(threeBytes, false);
  PSX_CHECK(!truncated && truncated.error() == psx::Error::Truncated);

  Bytes huge;
  PushU32(huge, psx::archive::MRGMagic);
  PushU32(huge, 0xFFFFFFFFu); // claims 4 Gi entries in a 8-byte buffer
  auto badTable = psx::archive::ParseMRG(huge, false);
  PSX_CHECK(!badTable && badTable.error() == psx::Error::MRGTableTruncated);
}

PSX_TEST(archive, mrg_reports_entries_outside_the_container)
{
  constexpr std::size_t payloadStart = 8 + 1 * 8;
  Bytes data = MakeMRGHeader({{payloadStart, 0x1000}}); // claims 4 Ki of payload
  data.insert(data.end(), 8, 0);                        // ...delivers 8 bytes

  auto table = psx::archive::ParseMRG(data, false);
  PSX_CHECK(table.has_value());
  if (!table)
    return;
  auto slice = psx::archive::MRGEntryData(*table, data, 0);
  PSX_CHECK(!slice && slice.error() == psx::Error::MRGEntryOutOfRange);
  PSX_CHECK(!psx::archive::MRGEntryData(*table, data, 99));
}

PSX_TEST(archive, deff_slices_contiguous_subfiles)
{
  // Subfile starts are absolute; the table is 8 + 3 * 8 bytes long.
  constexpr std::size_t payloadStart = 8 + 3 * 8;
  Bytes data = MakeDEFFHeader(3, {payloadStart, payloadStart + 8, payloadStart + 24});
  data.insert(data.end(), 8, 0x11);  // subfile 0: [16, 24)
  data.insert(data.end(), 16, 0x22); // subfile 1: [24, 40)
  data.insert(data.end(), 6, 0x33);  // subfile 2: [40, end)

  PSX_CHECK(psx::archive::IsDEFF(data));

  auto table = psx::archive::ParseDEFF(data);
  PSX_CHECK(table.has_value());
  if (!table)
    return;
  PSX_CHECK(table->Count() == 3);

  const std::size_t expectedSizes[3] = {8, 16, 6};
  for (std::size_t i = 0; i < 3; ++i)
  {
    auto entry = table->At(i);
    PSX_CHECK(entry.has_value());
    auto slice = psx::archive::DEFFEntryData(*table, data, i);
    PSX_CHECK(slice.has_value() && slice->size() == expectedSizes[i]);
  }

  // The last subfile runs to the end of the container.
  auto last = table->At(2);
  PSX_CHECK(last && last->offset + last->size == data.size());
  PSX_CHECK(!table->At(3) && table->At(3).error() == psx::Error::DEFFEntryMissing);
}

PSX_TEST(archive, deff_rejects_bad_magic_and_non_monotonic_tables)
{
  Bytes nope = {0, 1, 2, 3, 4, 5, 6, 7};
  PSX_CHECK(!psx::archive::IsDEFF(nope));
  PSX_CHECK(!psx::archive::ParseDEFF(nope) && psx::archive::ParseDEFF(nope).error() == psx::Error::DEFFMagic);

  Bytes backwards = MakeDEFFHeader(2, {40, 16});
  auto badOrder = psx::archive::ParseDEFF(backwards);
  PSX_CHECK(!badOrder && badOrder.error() == psx::Error::DEFFOffsetsBad);

  Bytes pastEnd = MakeDEFFHeader(1, {1000});
  pastEnd.insert(pastEnd.end(), 16, 0);
  auto badRange = psx::archive::ParseDEFF(pastEnd);
  PSX_CHECK(!badRange && badRange.error() == psx::Error::DEFFOffsetsBad);

  Bytes shortTable = MakeDEFFHeader(4, {}); // 4 subfiles, no table entries
  auto truncated = psx::archive::ParseDEFF(shortTable);
  PSX_CHECK(!truncated && truncated.error() == psx::Error::DEFFTableTruncated);
}

PSX_TEST(archive, empty_containers_parse_as_empty)
{
  Bytes emptyMRG = MakeMRGHeader({});
  auto mrg = psx::archive::ParseMRG(emptyMRG, false);
  PSX_CHECK(mrg.has_value() && mrg->Count() == 0);

  Bytes emptyDEFF = MakeDEFFHeader(0, {});
  auto deff = psx::archive::ParseDEFF(emptyDEFF);
  PSX_CHECK(deff.has_value() && deff->Count() == 0);
}

// The runner's main() lives in test_smoke.cpp (one runner per psx suite).
