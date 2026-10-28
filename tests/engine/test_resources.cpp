/// @file test_resources.cpp
/// @brief Resource discovery, ownership, path confinement and validation
/// (resources.h, asset-pipeline.md §1-§4). Fixtures are synthetic; the only
/// authentic asset path is opt-in and lives in the psx suite.
#include "lod/fs.h"
#include "lod/log.h"
#include "lod/resources.h"
#include "lod/status.h"
#include "psx/cpp/psx.hpp"
#include "sc_test.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{

using Bytes = std::vector<std::uint8_t>;

void PushU32(Bytes& out, std::uint32_t value)
{
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void PushI16(Bytes& out, std::int16_t value)
{
  PushU32(out, static_cast<std::uint32_t>(static_cast<std::uint16_t>(value)));
  out.pop_back();
  out.pop_back();
}

void PushU16(Bytes& out, std::uint16_t value)
{
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

/// @brief A minimal valid 4bpp TIM with a 4x1 CLUT and a 4x2 image.
Bytes MakeTIM(std::string_view trailing = {})
{
  Bytes out;
  PushU32(out, 0x10);
  PushU32(out, 8);    // depth 0 (4bpp) + CLUT present
  PushU32(out, 0x14); // CLUT section size: 12 + 8 bytes
  PushI16(out, 0);
  PushI16(out, 0);
  PushI16(out, 4);
  PushI16(out, 1);
  for (std::uint8_t i = 0; i < 8; ++i)
    out.push_back(i);
  PushU32(out, 28); // image section: 12 + 16 bytes
  PushI16(out, 0);
  PushI16(out, 0);
  PushI16(out, 4);
  PushI16(out, 2);
  out.insert(out.end(), 16, 0x5a);
  out.insert(out.end(), trailing.begin(), trailing.end());
  return out;
}

/// @brief An MRG whose payload starts after the entry table.
Bytes MakeMRG(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& entries, const Bytes& payload)
{
  Bytes out;
  PushU32(out, 0x1a47524du);
  PushU32(out, static_cast<std::uint32_t>(entries.size()));
  for (const auto& [offset, size] : entries)
  {
    PushU32(out, offset);
    PushU32(out, size);
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

/// @brief A DEFF: magic, u16 count at 0x6, then {pad, start} slots.
Bytes MakeDEFF(const std::vector<std::uint32_t>& starts, const Bytes& payload)
{
  Bytes out;
  PushU32(out, 0x46464544u);
  PushU16(out, 0);
  PushU16(out, static_cast<std::uint16_t>(starts.size()));
  for (const std::uint32_t start : starts)
  {
    PushU32(out, 0);
    PushU32(out, start);
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

/// @brief A BPE segment built by the library's own packer (literal dictionary),
/// so the fixture and the decoder cannot drift apart.
Bytes MakeBPE(std::string_view text)
{
  auto packed = psx::bpe::CompressBPE(
    std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
  return packed.value_or(Bytes{});
}

std::filesystem::path TempRoot(const std::string& tag)
{
  const auto dir = std::filesystem::temp_directory_path() / ("lodsc-res-test-" + tag);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

void WriteFile(const std::filesystem::path& root, const std::string& relative, const Bytes& bytes)
{
  const std::filesystem::path full = root / relative;
  std::error_code ec;
  std::filesystem::create_directories(full.parent_path(), ec);
  std::ofstream out(full, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!out)
    std::print("fixture write failed for {}\n", relative);
}

/// @brief A tree that passes resource-root recognition.
std::filesystem::path MakeGameRoot(const std::string& tag)
{
  const auto root = TempRoot(tag);
  WriteFile(root, "version", {'5', '\n'});
  WriteFile(root, "SECT/DRGN0.BIN/5546/1", {'n', 'o', ' ', 'h', 'e', 'a', 'd', 'e', 'r'});
  std::error_code ec;
  std::filesystem::create_directories(root / "SECT/DRGN1.BIN", ec);
  std::filesystem::create_directories(root / "SECT/NOTADIRN.BIN", ec);
  std::filesystem::create_directories(root / "SUBMAP", ec);
  std::filesystem::create_directories(root / "OVL", ec);
  std::filesystem::create_directories(root / "XA", ec);
  return root;
}

bool Contains(std::string_view haystack, std::string_view needle)
{
  return haystack.find(needle) != std::string_view::npos;
}

} // namespace

LOD_TEST(res, detect_kind_and_kind_names)
{
  const Bytes tim = MakeTIM();
  const Bytes mrg = MakeMRG({{16, 2}}, {'a', 'b'});
  const Bytes deff = MakeDEFF({24}, Bytes(8, 0));
  const Bytes bpe = MakeBPE("kind");

  LOD_CHECK(lod::res::DetectKind(tim) == lod::res::ResourceKind::TIM);
  LOD_CHECK(lod::res::DetectKind(mrg) == lod::res::ResourceKind::MRG);
  LOD_CHECK(lod::res::DetectKind(deff) == lod::res::ResourceKind::DEFF);
  LOD_CHECK(lod::res::DetectKind(bpe) == lod::res::ResourceKind::BPE);
  const Bytes none;
  const Bytes twoBytes = {'x', 'y'};
  LOD_CHECK(lod::res::DetectKind(none) == lod::res::ResourceKind::Raw);
  LOD_CHECK(lod::res::DetectKind(twoBytes) == lod::res::ResourceKind::Raw);

  LOD_CHECK(lod::res::KindName(lod::res::ResourceKind::TIM) == "tim");
  LOD_CHECK(lod::res::KindName(lod::res::ResourceKind::BPE) == "bpe");
  LOD_CHECK(lod::res::KindName(lod::res::ResourceKind::Raw) == "raw");
}

LOD_TEST(res, validate_root_reports_absent_paths_without_failing)
{
  // Absence is an answer, not an I/O failure: the caller needs to say *what* was
  // missing, which requires the populated info rather than a bare error code.
  const auto missing = TempRoot("absent") / "nope";
  auto info = lod::res::ValidateRoot(missing);
  LOD_REQUIRE(info.has_value());
  LOD_CHECK(!info->Exists);
  LOD_CHECK(!info->LooksLikeGameRoot());
  LOD_CHECK(Contains(info->Notes.front(), "does not exist"));

  // A file is not a directory.
  const auto root = TempRoot("afile");
  WriteFile(root, "afile", {'x'});
  auto notDir = lod::res::ValidateRoot(root / "afile");
  LOD_REQUIRE(notDir.has_value());
  LOD_CHECK(!notDir->Exists);
  LOD_CHECK(Contains(notDir->Notes.front(), "not a directory"));
}

LOD_TEST(res, validate_root_reads_markers_and_counts_drgn_archives)
{
  const auto root = MakeGameRoot("markers");
  auto info = lod::res::ValidateRoot(root);
  LOD_REQUIRE(info.has_value());
  LOD_CHECK(info->Exists);
  LOD_CHECK(info->HasSectDir);
  LOD_CHECK(info->LooksLikeGameRoot());
  LOD_CHECK(info->HasVersionFile);
  LOD_CHECK(info->Version == "5"); // trailing newline trimmed
  LOD_CHECK(info->DrgnBins == 2);  // DRGN0.BIN + DRGN1.BIN, not NOTADIRN.BIN
  LOD_CHECK(info->HasSubmapDir);
  LOD_CHECK(info->HasOvlDir);
  LOD_CHECK(info->HasXADir);
  LOD_CHECK(info->Notes.size() >= 3);
}

LOD_TEST(res, open_requires_a_sect_root)
{
  std::vector<std::string> lines;
  lod::Logger log;
  log.SetSink([&lines](std::string_view line) { lines.emplace_back(line); });
  lod::res::ResourceManager::Options options;
  options.Log = &log;

  // Version file but no SECT/: recognised as a directory, rejected as a root.
  const auto partial = TempRoot("partial");
  WriteFile(partial, "version", {'5'});
  auto openPartial = lod::res::ResourceManager::Open(partial, options);
  LOD_CHECK(!openPartial.has_value());
  LOD_CHECK(!openPartial ? openPartial.error() == lod::Status::NotFound : false);
  LOD_CHECK(lines.size() >= 2);
  LOD_CHECK(Contains(lines.front(), "SECT/ missing"));

  auto openAbsent = lod::res::ResourceManager::Open(partial / "absent", options);
  LOD_CHECK(!openAbsent.has_value());
  LOD_CHECK(!openAbsent ? openAbsent.error() == lod::Status::NotFound : false);

  // The positive control: same helper, real root, no diagnostics lost.
  lines.clear();
  auto openGood = lod::res::ResourceManager::Open(MakeGameRoot("open"), options);
  LOD_REQUIRE(openGood.has_value());
  LOD_CHECK(!lines.empty());
  LOD_CHECK(Contains(lines.front(), "SECT/ present"));
}

LOD_TEST(res, load_returns_owned_bytes_and_sniffs_kind)
{
  const auto root = MakeGameRoot("load");
  WriteFile(root, "SUBMAP/x.tim", MakeTIM("abc"));
  lod::res::ResourceManager manager(root);

  auto blob = manager.Load("SUBMAP/x.tim");
  LOD_REQUIRE(blob.has_value());
  LOD_CHECK(blob->Size() == MakeTIM("abc").size());
  LOD_CHECK(blob->Bytes().size() == blob->Size());
  LOD_CHECK(blob->Kind() == lod::res::ResourceKind::TIM);
  LOD_CHECK(blob->RelPath() == "SUBMAP/x.tim");
  LOD_CHECK(!blob->Empty());

  // Empty files load as empty resources, not as failures.
  WriteFile(root, "SUBMAP/empty.bin", {});
  auto empty = manager.Load("SUBMAP/empty.bin");
  LOD_REQUIRE(empty.has_value());
  LOD_CHECK(empty->Empty());
  LOD_CHECK(empty->Kind() == lod::res::ResourceKind::Raw);
}

LOD_TEST(res, blob_is_move_only_and_release_is_deterministic)
{
  static_assert(!std::is_copy_constructible_v<lod::res::ResourceBlob>);
  static_assert(!std::is_copy_assignable_v<lod::res::ResourceBlob>);
  static_assert(std::is_move_constructible_v<lod::res::ResourceBlob>);

  const auto root = MakeGameRoot("move");
  WriteFile(root, "SECT/DRGN0.BIN/1/2", MakeTIM());
  lod::res::ResourceManager manager(root);

  auto original = manager.Load("SECT/DRGN0.BIN/1/2");
  LOD_REQUIRE(original.has_value());
  const std::size_t size = original->Size();
  const std::uint8_t* data = original->Bytes().data();

  lod::res::ResourceBlob moved(std::move(*original));
  LOD_CHECK(moved.Size() == size);
  LOD_CHECK(moved.Bytes().data() == data); // ownership transferred, not copied
  LOD_CHECK(original->Empty());            // NOLINT: moved-from object is valid and empty

  moved.Release();
  LOD_CHECK(moved.Empty());
  LOD_CHECK(moved.Size() == 0);
  moved.Release(); // idempotent
  LOD_CHECK(moved.Empty());
}

LOD_TEST(res, load_rejects_absolute_and_traversing_paths)
{
  const auto root = MakeGameRoot("confine");
  WriteFile(root, "SECT/DRGN0.BIN/1/2", {'a'});
  lod::res::ResourceManager manager(root);

  LOD_CHECK(lod::res::ResourceManager::IsSafeRelativePath("SECT/DRGN0.BIN/1/2"));
  LOD_CHECK(lod::res::ResourceManager::IsSafeRelativePath("version"));
  LOD_CHECK(!lod::res::ResourceManager::IsSafeRelativePath(""));
  LOD_CHECK(!lod::res::ResourceManager::IsSafeRelativePath("/etc/passwd"));
  LOD_CHECK(!lod::res::ResourceManager::IsSafeRelativePath("../escape"));
  LOD_CHECK(!lod::res::ResourceManager::IsSafeRelativePath("SECT/../../escape"));
  LOD_CHECK(!lod::res::ResourceManager::IsSafeRelativePath("SECT/../version"));

  for (std::string_view bad : {"/etc/passwd", "../escape", "SECT/../../escape"})
  {
    auto loaded = manager.Load(bad);
    LOD_CHECK(!loaded ? loaded.error() == lod::Status::InvalidArgument : false);
  }
}

LOD_TEST(res, load_separates_missing_directory_and_oversized)
{
  const auto root = MakeGameRoot("taxonomy");
  WriteFile(root, "SUBMAP/big.bin", Bytes(4096, 0x11));
  std::error_code ec;
  std::filesystem::create_directories(root / "SUBMAP/adir", ec);

  lod::res::ResourceManager manager(root);
  auto missing = manager.Load("SUBMAP/nope.bin");
  LOD_CHECK(!missing ? missing.error() == lod::Status::NotFound : false);

  auto directory = manager.Load("SUBMAP/adir");
  LOD_CHECK(!directory ? directory.error() == lod::Status::NotFound : false);

  // The same file under a ceiling it does not fit: a distinct, specific code.
  lod::res::ResourceManager::Options capped;
  capped.MaxFileBytes = 1024;
  lod::res::ResourceManager small(root, capped);
  auto oversized = small.Load("SUBMAP/big.bin");
  LOD_CHECK(!oversized ? oversized.error() == lod::Status::IOTooLarge : false);

  // And within the ceiling it loads normally.
  auto fits = manager.Load("SUBMAP/big.bin");
  LOD_CHECK(fits.has_value());
}

LOD_TEST(res, scan_lists_sorted_entries_with_sizes_and_kinds)
{
  const auto root = MakeGameRoot("scan");
  WriteFile(root, "SUBMAP/b.tim", MakeTIM());
  WriteFile(root, "SUBMAP/a.raw", {'1', '2', '3'});
  WriteFile(root, "OVL/container.mrg", MakeMRG({{16, 2}}, {'q', 'r'}));
  lod::res::ResourceManager manager(root);

  auto scan = manager.Scan();
  LOD_REQUIRE(scan.has_value());
  LOD_CHECK(!scan->Truncated);
  LOD_CHECK(scan->Entries.size() == 5); // version, DRGN file, b.tim, a.raw, container.mrg
  LOD_CHECK(scan->Directories >= 6);

  std::uintmax_t expectedBytes = 0;
  for (const lod::res::ResourceEntry& entry : scan->Entries)
    expectedBytes += entry.Size;
  LOD_CHECK(scan->TotalBytes == expectedBytes);

  bool sorted = true;
  for (std::size_t i = 1; i < scan->Entries.size(); ++i)
    sorted = sorted && scan->Entries[i - 1].RelPath < scan->Entries[i].RelPath;
  LOD_CHECK(sorted);

  const auto byPath = [&](std::string_view wanted) -> const lod::res::ResourceEntry*
  {
    for (const lod::res::ResourceEntry& entry : scan->Entries)
      if (entry.RelPath == wanted)
        return &entry;
    return nullptr;
  };
  const auto* tim = byPath("SUBMAP/b.tim");
  const auto* raw = byPath("SUBMAP/a.raw");
  const auto* mrg = byPath("OVL/container.mrg");
  LOD_REQUIRE(tim != nullptr && raw != nullptr && mrg != nullptr);
  LOD_CHECK(tim->Kind == lod::res::ResourceKind::TIM);
  LOD_CHECK(raw->Kind == lod::res::ResourceKind::Raw);
  LOD_CHECK(mrg->Kind == lod::res::ResourceKind::MRG);
  LOD_CHECK(raw->Size == 3);
}

LOD_TEST(res, scan_stops_at_the_entry_limit)
{
  const auto root = MakeGameRoot("limit");
  for (int i = 0; i < 5; ++i)
    WriteFile(root, std::format("SUBMAP/f{}.bin", i), Bytes(4, static_cast<std::uint8_t>(i)));

  lod::res::ResourceManager::Options options;
  options.ScanLimit = 3;
  lod::res::ResourceManager manager(root, options);

  auto scan = manager.Scan();
  LOD_REQUIRE(scan.has_value());
  LOD_CHECK(scan->Truncated);
  LOD_CHECK(scan->Entries.size() == 3);
}

LOD_TEST(res, inspect_describes_every_supported_container)
{
  const auto root = MakeGameRoot("inspect");
  WriteFile(root, "SUBMAP/tim.tim", MakeTIM("zz"));
  WriteFile(root, "OVL/plain.mrg", MakeMRG({{16, 4}}, {'a', 'b', 'c', 'd'}));
  WriteFile(root, "OVL/virtual.mrg", MakeMRG({{16, 4}, {16, 0}}, {'a', 'b', 'c', 'd'}));
  Bytes deffPayload = Bytes(8, 1);
  deffPayload.insert(deffPayload.end(), 6, 2);
  WriteFile(root, "OVL/subs.deff", MakeDEFF({24, 32}, deffPayload));
  WriteFile(root, "SECT/DRGN0.BIN/9/1", MakeBPE("inflate me"));
  WriteFile(root, "SECT/DRGN0.BIN/9/2", {'n', 'o', 'p', 'e'});
  lod::res::ResourceManager manager(root);

  auto tim = manager.Inspect("SUBMAP/tim.tim");
  LOD_REQUIRE(tim.has_value());
  LOD_CHECK(tim->Kind == lod::res::ResourceKind::TIM);
  LOD_CHECK(Contains(tim->Summary, "4bpp"));
  LOD_CHECK(Contains(tim->Summary, "4x2"));
  LOD_CHECK(Contains(tim->Summary, "4 entries"));
  LOD_CHECK(Contains(tim->Summary, "2 B trailing"));

  auto mrg = manager.Inspect("OVL/plain.mrg");
  LOD_REQUIRE(mrg.has_value());
  LOD_CHECK(Contains(mrg->Summary, "1 entries"));
  LOD_CHECK(Contains(mrg->Summary, "4 B payload"));
  LOD_CHECK(!Contains(mrg->Summary, "sector-aligned")); // not a DRGN root path

  auto virtualMRG = manager.Inspect("OVL/virtual.mrg");
  LOD_REQUIRE(virtualMRG.has_value());
  LOD_CHECK(Contains(virtualMRG->Summary, "1 virtual"));

  auto deff = manager.Inspect("OVL/subs.deff");
  LOD_REQUIRE(deff.has_value());
  LOD_CHECK(Contains(deff->Summary, "2 subfiles"));
  LOD_CHECK(Contains(deff->Summary, "2 readable"));

  auto bpe = manager.Inspect("SECT/DRGN0.BIN/9/1");
  LOD_REQUIRE(bpe.has_value());
  LOD_CHECK(Contains(bpe->Summary, "BPE"));
  LOD_CHECK(Contains(bpe->Summary, "-> 10 B"));

  // Headerless data inspects honestly: size only, no invented structure.
  auto raw = manager.Inspect("SECT/DRGN0.BIN/9/2");
  LOD_REQUIRE(raw.has_value());
  LOD_CHECK(raw->Kind == lod::res::ResourceKind::Raw);
  LOD_CHECK(Contains(raw->Summary, "raw 4 B"));
}

LOD_TEST(res, inspect_maps_decoder_errors_to_decode_statuses)
{
  const auto root = MakeGameRoot("decode");
  // TIM with a valid id but an unknown depth code.
  Bytes badDepth = MakeTIM();
  badDepth[4] = 6; // flags & 7 == 6: not one of the four depths
  WriteFile(root, "SUBMAP/depth.tim", badDepth);
  // TIM whose image section claims more bytes than exist.
  Bytes shortImage = MakeTIM();
  const auto imageSection = static_cast<std::size_t>(shortImage[8]) + 8;
  shortImage[imageSection] = 0x40;
  shortImage[imageSection + 1] = 0x27; // 0x2740 = 10048 B
  WriteFile(root, "SUBMAP/short.tim", shortImage);
  // MRG entry pointing past the container.
  WriteFile(root, "OVL/bad.mrg", MakeMRG({{16, 0x4000}}, {'a'}));
  // DEFF with non-monotonic starts.
  WriteFile(root, "OVL/bad.deff", MakeDEFF({40, 24}, Bytes(48, 0)));
  // BPE whose declared size cannot match what the blocks produce.
  Bytes brokenBPE = MakeBPE("payload");
  brokenBPE[0] = 40;
  brokenBPE[1] = 0;
  brokenBPE[2] = 0;
  brokenBPE[3] = 0;
  WriteFile(root, "SECT/DRGN0.BIN/7/1", brokenBPE);

  lod::res::ResourceManager manager(root);

  auto depth = manager.Inspect("SUBMAP/depth.tim");
  LOD_CHECK(!depth ? depth.error() == lod::Status::DecodeUnsupported : false);

  auto shortTIM = manager.Inspect("SUBMAP/short.tim");
  LOD_CHECK(!shortTIM ? shortTIM.error() == lod::Status::DecodeTruncated : false);

  auto badMRG = manager.Inspect("OVL/bad.mrg");
  LOD_CHECK(!badMRG ? badMRG.error() == lod::Status::DecodeCorrupt : false);

  auto badDEFF = manager.Inspect("OVL/bad.deff");
  LOD_CHECK(!badDEFF ? badDEFF.error() == lod::Status::DecodeCorrupt : false);

  auto badBPE = manager.Inspect("SECT/DRGN0.BIN/7/1");
  LOD_CHECK(!badBPE ? badBPE.error() == lod::Status::DecodeSizeMismatch : false);

  // Load failures keep their own taxonomy — never disguised as decode errors.
  auto absent = manager.Inspect("SUBMAP/absent.tim");
  LOD_CHECK(!absent ? absent.error() == lod::Status::NotFound : false);
}

LOD_TEST(res, inspect_skips_bpe_inflation_above_the_limit)
{
  // A header that declares a large output must be reported without allocating
  // for it: inspection is a read-only pass, not a decompression stress test.
  Bytes big;
  PushU32(big, 64u * 1024u * 1024u);
  PushU32(big, 0x1a455042u);
  PushU32(big, 0); // no blocks at all
  const auto root = MakeGameRoot("bpelimit");
  WriteFile(root, "SECT/DRGN0.BIN/3/1", big);

  lod::res::ResourceManager::Options options;
  options.InspectDecompressLimit = 4096;
  lod::res::ResourceManager manager(root, options);

  auto inspected = manager.Inspect("SECT/DRGN0.BIN/3/1");
  LOD_REQUIRE(inspected.has_value());
  LOD_CHECK(Contains(inspected->Summary, "not decompressed"));
  LOD_CHECK(Contains(inspected->Summary, "4096"));
}

LOD_TEST(res, sector_alignment_follows_the_drgn_root_path)
{
  // Identical bytes, two paths: only a root DRGN*.BIN interprets offsets as
  // sector numbers, so the same table resolves to different ranges.
  const Bytes container = MakeMRG({{1, 4}}, Bytes(0x800 + 4, 0x41));
  const auto root = MakeGameRoot("sectors");
  WriteFile(root, "DRGN0.BIN", container);
  WriteFile(root, "OVL/DRGN0.BIN", container); // nested: first component is OVL/
  lod::res::ResourceManager manager(root);

  auto rootLevel = manager.Inspect("DRGN0.BIN");
  LOD_REQUIRE(rootLevel.has_value());
  LOD_CHECK(Contains(rootLevel->Summary, "sector-aligned"));
  LOD_CHECK(Contains(rootLevel->Summary, "4 B payload"));

  auto nested = manager.Inspect("OVL/DRGN0.BIN");
  LOD_REQUIRE(nested.has_value());
  LOD_CHECK(!Contains(nested->Summary, "sector-aligned"));
  LOD_CHECK(Contains(nested->Summary, "4 B payload")); // [1,5) inside the table

  // Case-insensitive match, because the tree lives on case-insensitive volumes.
  WriteFile(root, "drgn1.bin", container);
  auto lower = manager.Inspect("drgn1.bin");
  LOD_REQUIRE(lower.has_value());
  LOD_CHECK(Contains(lower->Summary, "sector-aligned"));
}

LOD_TEST(res, diagnostics_name_the_decoder_error_and_mapped_status)
{
  const auto root = MakeGameRoot("diag");
  Bytes badDepth = MakeTIM();
  badDepth[4] = 6;
  WriteFile(root, "SUBMAP/depth.tim", badDepth);

  std::vector<std::string> lines;
  lod::Logger log;
  log.SetSink([&lines](std::string_view line) { lines.emplace_back(line); });
  lod::res::ResourceManager::Options options;
  options.Log = &log;
  lod::res::ResourceManager manager(root, options);

  auto failed = manager.Inspect("SUBMAP/depth.tim");
  LOD_CHECK(!failed.has_value());
  LOD_REQUIRE(lines.size() >= 2); // load note + the failure itself
  // The specific decoder cause and the mapped engine status must both be there,
  // plus the path: that is enough for an operator to find the file and the
  // reason without a debugger.
  std::string joined;
  for (const std::string& line : lines)
  {
    joined += line;
    joined += '\n';
  }
  LOD_CHECK(Contains(joined, "SUBMAP/depth.tim"));
  LOD_CHECK(Contains(joined, "TIMBPP"));
  LOD_CHECK(Contains(joined, "DecodeUnsupported"));

  lines.clear();
  auto missing = manager.Inspect("SUBMAP/never-written.tim");
  LOD_CHECK(!missing.has_value());
  LOD_CHECK(lines.size() == 1);
  LOD_CHECK(Contains(lines.front(), "SUBMAP/never-written.tim"));
}
