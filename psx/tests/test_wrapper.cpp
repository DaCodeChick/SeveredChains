// psx/tests/test_wrapper.cpp
//
// C++23 wrapper contract (testing.md §2.11): delegation, move semantics, RAII
// destruction, error propagation and span lifetime. The wrappers are thin delegates to
// the C core, so these cases target the guarantees a C caller cannot express — the
// formats themselves are covered by the format groups and by the pure-C consumer.

#include "psx/cpp/psx.hpp"
#include "psx_test.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace

{
void PushU32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

void PushU16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

/// @brief MRG container from {offset,size} pairs plus a payload.
std::vector<std::uint8_t> BuildMRG(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& entries,
                                   std::size_t payloadBytes)
{
  std::vector<std::uint8_t> out;
  PushU32(out, PSX_ARCHIVE_MRG_MAGIC);
  PushU32(out, static_cast<std::uint32_t>(entries.size()));
  for (const auto& [offset, size] : entries)
  {
    PushU32(out, offset);
    PushU32(out, size);
  }
  for (std::size_t i = 0; i < payloadBytes; ++i)
    out.push_back(static_cast<std::uint8_t>(0x55u + (i % 13u)));
  return out;
}

std::vector<std::uint8_t> BuildDEFF(const std::vector<std::uint32_t>& starts, std::size_t payloadBytes)
{
  std::vector<std::uint8_t> out;
  PushU32(out, PSX_ARCHIVE_DEFF_MAGIC);
  out.push_back(0);
  out.push_back(0);
  PushU16(out, static_cast<std::uint16_t>(starts.size()));
  for (const std::uint32_t start : starts)
  {
    PushU32(out, 0u);
    PushU32(out, start);
  }
  for (std::size_t i = 0; i < payloadBytes; ++i)
    out.push_back(static_cast<std::uint8_t>(i & 0xffu));
  return out;
}

/// @brief Minimal 4bpp TIM: header, 16x16 CLUT, 16x16 image.
std::vector<std::uint8_t> BuildTIM()
{
  std::vector<std::uint8_t> tim;
  PushU32(tim, PSX_TIM_MAGIC);
  PushU32(tim, 0x8u); // CLUT present, 4bpp
  PushU32(tim, PSX_TIM_SECTION_HEADER_BYTES + 512u);
  PushU16(tim, 0u);
  PushU16(tim, 112u);
  PushU16(tim, 16u);
  PushU16(tim, 16u);
  for (std::uint16_t i = 0; i < 256u; ++i)
    PushU16(tim, i);
  PushU32(tim, PSX_TIM_SECTION_HEADER_BYTES + 512u);
  PushU16(tim, 0u);
  PushU16(tim, 0u);
  PushU16(tim, 16u);
  PushU16(tim, 16u);
  for (std::uint16_t i = 0; i < 256u; ++i)
    PushU16(tim, static_cast<std::uint16_t>(0x1234u + i));
  return tim;
}

/* Compile-time contract: the C++ enum mirrors the C values, and the RAII wrappers
   own resources so they must not be silently copyable. */
static_assert(static_cast<int>(psx::Error::BPEMagic) == PSX_ERROR_BPE_MAGIC);
static_assert(static_cast<int>(psx::Error::TIMRectExtent) == PSX_ERROR_TIM_RECT_EXTENT);
static_assert(static_cast<int>(psx::Error::MRGEntryMissing) == PSX_ERROR_MRG_ENTRY_MISSING);
static_assert(static_cast<int>(psx::Error::DEFFEntryMissing) == PSX_ERROR_DEFF_ENTRY_MISSING);
static_assert(!std::is_copy_constructible_v<psx::archive::MRGTableHandle>);
static_assert(!std::is_copy_assignable_v<psx::archive::MRGTableHandle>);
static_assert(std::is_move_constructible_v<psx::archive::MRGTableHandle>);
static_assert(std::is_move_assignable_v<psx::archive::MRGTableHandle>);
static_assert(!std::is_copy_constructible_v<psx::archive::DEFFTableHandle>);
static_assert(!std::is_copy_assignable_v<psx::archive::DEFFTableHandle>);
/* Index containers are plain value types: copying one is cheap and independent. */
static_assert(std::is_copy_assignable_v<psx::archive::MRGTable>);
static_assert(std::is_copy_assignable_v<psx::archive::DEFFTable>);
} // namespace

PSX_TEST(wrapper, error_strings_delegate_to_the_c_table)
{
  /* One table for both languages, so a log line reads the same whichever side
     produced it. */
  PSX_CHECK(psx::ErrorName(psx::Error::BPEMagic) == std::string_view{psx_error_name(PSX_ERROR_BPE_MAGIC)});
  PSX_CHECK(psx::ErrorMessage(psx::Error::MRGEntryMissing) ==
            std::string_view{psx_error_message(PSX_ERROR_MRG_ENTRY_MISSING)});
  PSX_CHECK(psx::ErrorName(psx::Error::DEFFOffsetsBad) == "DEFFOffsetsBad");

  /* A value outside the C range reports Unknown rather than reading past a table. */
  PSX_CHECK(psx::ErrorName(static_cast<psx::Error>(9999)) == "Unknown");
  PSX_CHECK(psx::ErrorMessage(static_cast<psx::Error>(-42)) ==
            std::string_view{psx_error_message(static_cast<psx_error_t>(-42))});

  /* Checked() turns a bare C code into an expected<void> failure. */
  PSX_CHECK(static_cast<bool>(psx::Checked(PSX_OK)));
  const auto failed = psx::Checked(PSX_ERROR_TIM_BPP);
  PSX_CHECK(!failed.has_value());
  PSX_CHECK(failed.error() == psx::Error::TIMBPP);
}

PSX_TEST(wrapper, span_conversion_aliases_and_round_trips)
{
  std::vector<std::uint8_t> storage{1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};

  const psx_bytes_t view = psx::detail::ToBytes(std::span<const std::uint8_t>(storage));
  PSX_CHECK(psx_bytes_is_valid(view));
  PSX_CHECK(view.data == storage.data());
  PSX_CHECK(view.size == storage.size());

  /* Conversion back must describe the same memory — no copy on the way out. */
  const auto back = psx::detail::ToSpan(view);
  PSX_CHECK(back.data() == storage.data());
  PSX_CHECK(back.size() == storage.size());

  /* Mutating the source is visible through the view: it borrows, it does not own. */
  storage[3] = 0xaau;
  PSX_CHECK(back[3] == 0xaau);

  /* An empty span is a valid descriptor; a null base with a size is a caller error
     and must never become a span. */
  const psx_bytes_t empty = psx::detail::ToBytes(std::span<const std::uint8_t>{});
  PSX_CHECK(psx_bytes_is_valid(empty));
  PSX_CHECK(psx::detail::ToSpan(empty).empty());

  psx_bytes_t broken;
  broken.data = nullptr;
  broken.size = 16u;
  PSX_CHECK(!psx_bytes_is_valid(broken));
  PSX_CHECK(psx::detail::ToSpan(broken).empty());

  /* Writable descriptors describe the caller's storage exactly. */
  const psx_writable_bytes_t writable = psx::detail::ToWritable(std::span<std::uint8_t>(storage));
  PSX_CHECK(psx_writable_bytes_is_valid(writable));
  PSX_CHECK(writable.data == storage.data());
  PSX_CHECK(writable.capacity == storage.size());

  /* And the core really writes through what the wrapper handed it. */
  const auto packed = psx::bpe::CompressBPE(std::span<const std::uint8_t>(storage));
  PSX_CHECK(packed.has_value());
  std::vector<std::uint8_t> destination(storage.size(), 0u);
  std::size_t written = 0u;
  PSX_CHECK(psx_bpe_decompress(psx::detail::ToBytes(std::span<const std::uint8_t>(*packed)),
                               psx::detail::ToWritable(std::span<std::uint8_t>(destination)), &written) == PSX_OK);
  PSX_CHECK(written == storage.size());
  PSX_CHECK(destination == storage);
}

PSX_TEST(wrapper, bpe_allocates_exactly_the_declared_output)
{
  const std::vector<std::uint8_t> raw(300u, 0x41u);
  const auto packed = psx::bpe::CompressBPE(std::span<const std::uint8_t>(raw));
  PSX_CHECK(packed.has_value());

  const auto decoded = psx::bpe::DecompressBPE(std::span<const std::uint8_t>(*packed));
  PSX_CHECK(decoded.has_value());
  PSX_CHECK(decoded->size() == raw.size());
  PSX_CHECK(decoded->front() == 0x41u && decoded->back() == 0x41u);

  /* The wrapper hands back owning bytes, so the segment can be released first. */
  std::vector<std::uint8_t> segmentCopy = *packed;
  const auto again = psx::bpe::DecompressBPE(std::span<const std::uint8_t>(segmentCopy));
  PSX_CHECK(again.has_value() && again->size() == raw.size());

  /* The ceiling is enforced before allocation, and is expressible per call. */
  const auto capped = psx::bpe::DecompressBPE(std::span<const std::uint8_t>(*packed), 64u);
  PSX_CHECK(!capped.has_value());
  PSX_CHECK(capped.error() == psx::Error::BPEOutputTooLarge);
}

PSX_TEST(wrapper, propagates_the_exact_decoder_code)
{
  /* Malformed input surfaces the C code unchanged — no coarse "decode failed". */
  const std::vector<std::uint8_t> notBPE{1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
  const auto declared = psx::bpe::DeclaredSize(std::span<const std::uint8_t>(notBPE));
  PSX_CHECK(!declared.has_value());
  PSX_CHECK(declared.error() == psx::Error::BPEMagic);

  const std::vector<std::uint8_t> notTIM{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
  const auto image = psx::tim::ParseTIM(std::span<const std::uint8_t>(notTIM));
  PSX_CHECK(!image.has_value());
  PSX_CHECK(image.error() == psx::Error::TIMMagic);

  const std::vector<std::uint8_t> notMRG{9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u};
  PSX_CHECK(psx::archive::ParseMRG(std::span<const std::uint8_t>(notMRG), false).error() == psx::Error::MRGMagic);
  PSX_CHECK(psx::archive::ParseDEFF(std::span<const std::uint8_t>(notMRG)).error() == psx::Error::DEFFMagic);
  PSX_CHECK(psx::archive::MRGTableHandle::Parse(std::span<const std::uint8_t>(notMRG), false).error() ==
            psx::Error::MRGMagic);
  PSX_CHECK(psx::archive::DEFFTableHandle::Parse(std::span<const std::uint8_t>(notMRG)).error() ==
            psx::Error::DEFFMagic);

  /* Truncation stays distinguishable from corruption. */
  const std::vector<std::uint8_t> shortTIM{0x10u, 0u, 0u, 0u};
  PSX_CHECK(psx::tim::ParseTIM(std::span<const std::uint8_t>(shortTIM)).error() == psx::Error::Truncated);
}

/* Encapsulation is part of the wrapper contract, not an implementation detail: the
   entry arrays are private state, so the accessors are the only way to read a table
   and nothing outside the parse functions can move an entry array and its virtual
   count apart. Pinned here because a re-added public field would compile silently.
   The concept is what makes the probe well-formed: with a template parameter the
   missing (or inaccessible) member is a substitution failure instead of a hard error. */
namespace
{
template <class Table>
concept HasPublicEntryArray = requires(Table table) {
  table.storage;
  table.virtualEntries;
};
} // namespace

static_assert(!HasPublicEntryArray<psx::archive::MRGTable>);
static_assert(!HasPublicEntryArray<psx::archive::DEFFTable>);

PSX_TEST(wrapper, mrg_table_is_an_independent_value_type)
{
  const std::vector<std::uint8_t> container = BuildMRG({{0u, 4u}, {4u, 8u}, {4u, 0u}}, 16u);
  const auto bytes = std::span<const std::uint8_t>(container);

  const auto table = psx::archive::ParseMRG(bytes, false);
  PSX_CHECK(table.has_value());
  PSX_CHECK(table->Count() == 3u);
  PSX_CHECK(table->VirtualCount() == 1u);

  /* Copy: independent storage, same logical content. MRGTable has no mutating API, so
     independence is shown from the other side — emptying the original must leave the
     copy's entries and their offsets alone. */
  psx::archive::MRGTable original = *table;
  psx::archive::MRGTable copy = original;
  PSX_CHECK(copy.Count() == original.Count());
  const std::size_t copiedEntries = copy.Count();
  const std::size_t firstOffset = copy.At(0)->offset;
  original = psx::archive::MRGTable{};
  PSX_CHECK(copy.Count() == copiedEntries);
  PSX_CHECK(copy.At(0)->offset == firstOffset);
  PSX_CHECK(copy.Entries().data() != original.Entries().data());

  /* Move: the source is left empty, so a moved-from table cannot hand out entries.
     Fresh sources, because the assignment above emptied `original`. */
  psx::archive::MRGTable moved = copy;
  psx::archive::MRGTable source = copy;
  moved = std::move(source);
  PSX_CHECK(moved.Count() == 3u);
  PSX_CHECK(source.Count() == 0u);
  const auto miss = moved.At(9u);
  PSX_CHECK(!miss.has_value());
  PSX_CHECK(miss.error() == psx::Error::MRGEntryMissing);

  const auto entry = moved.At(2u);
  PSX_CHECK(entry.has_value());
  PSX_CHECK(entry->parent == 1);
  PSX_CHECK(entry->Virtual());
  PSX_CHECK(entry->size == 8u);

  /* The view borrows the table's own vector: zero cost, and it must not outlive it. */
  const psx_mrg_view_t view = moved.View();
  PSX_CHECK(view.count == moved.Count());
  PSX_CHECK(view.entries == moved.Entries().data());
  PSX_CHECK(view.virtual_count == moved.VirtualCount());

  const auto data = psx::archive::MRGEntryData(moved, bytes, 0u);
  PSX_CHECK(data.has_value());
  PSX_CHECK(data->size() == 4u);
  /* Entry offsets are absolute inside the container. */
  PSX_CHECK(data->data() == container.data());

  /* Slicing is bounds-checked against the container actually supplied. */
  const auto clipped = psx::archive::MRGEntryData(moved, std::span<const std::uint8_t>(container.data(), 8u), 1u);
  PSX_CHECK(!clipped.has_value());
  PSX_CHECK(clipped.error() == psx::Error::MRGEntryOutOfRange);
}

PSX_TEST(wrapper, mrg_handle_releases_on_move_and_at_scope_exit)
{
  const std::vector<std::uint8_t> container = BuildMRG({{0u, 4u}, {4u, 8u}}, 16u);
  const auto bytes = std::span<const std::uint8_t>(container);

  auto first = psx::archive::MRGTableHandle::Parse(bytes, false);
  PSX_CHECK(first.has_value());
  PSX_CHECK(static_cast<bool>(*first));
  PSX_CHECK(first->Count() == 2u);

  auto second = psx::archive::MRGTableHandle::Parse(bytes, true);
  PSX_CHECK(second.has_value());

  /* Move-assignment must release the destination's handle — leaking or double
     freeing shows up in the sanitiser builds of this target. */
  second = std::move(first);
  PSX_CHECK(second->Count() == 2u);
  PSX_CHECK(second->Handle() != nullptr);
  PSX_CHECK(!static_cast<bool>(*first)); // the moved-from handle owns nothing
  PSX_CHECK(first->Count() == 0u);       // an emptied handle answers zero, never crashes
  PSX_CHECK(first->Handle() == nullptr);

  const psx_mrg_view_t view = second->View();
  PSX_CHECK(view.count == 2u);
  const auto slice = second->Slice(bytes, 1u);
  PSX_CHECK(slice.has_value());
  PSX_CHECK(slice->size() == 8u);

  /* A scope is enough to release the heap state the handle owns. */
  {
    auto scoped = psx::archive::MRGTableHandle::Parse(bytes, false);
    PSX_CHECK(scoped.has_value());
    PSX_CHECK(scoped->VirtualCount() == 0u);
  }

  /* Failure leaves no handle behind. */
  const std::vector<std::uint8_t> junk{1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
  const auto failed = psx::archive::MRGTableHandle::Parse(std::span<const std::uint8_t>(junk), false);
  PSX_CHECK(!failed.has_value());
  PSX_CHECK(failed.error() == psx::Error::MRGMagic);

  /* A default-constructed handle owns nothing and is safe to destroy. */
  psx::archive::MRGTableHandle empty{};
  PSX_CHECK(!static_cast<bool>(empty));
  PSX_CHECK(empty.View().count == 0u);
}

PSX_TEST(wrapper, deff_table_and_handle_agree_with_the_c_layer)
{
  const std::vector<std::uint8_t> container = BuildDEFF({32u, 40u, 60u}, 32u);
  const auto bytes = std::span<const std::uint8_t>(container);

  const auto table = psx::archive::ParseDEFF(bytes);
  PSX_CHECK(table.has_value());
  PSX_CHECK(table->Count() == 3u);
  const auto entry = table->At(1u);
  PSX_CHECK(entry.has_value());
  PSX_CHECK(entry->offset == 40u);
  PSX_CHECK(entry->size == 20u);
  PSX_CHECK(!table->At(3u).has_value());

  const auto handle = psx::archive::DEFFTableHandle::Parse(bytes);
  PSX_CHECK(handle.has_value());
  PSX_CHECK(handle->Count() == table->Count());
  PSX_CHECK(handle->View().count == table->View().count);
  PSX_CHECK(handle->Handle() != nullptr);

  psx_deff_entry_t fromHandle{};
  PSX_CHECK(psx_deff_view_at(handle->View(), 2u, &fromHandle) == PSX_OK);
  PSX_CHECK(fromHandle.offset == 60u);

  const auto data = handle->Slice(bytes, 0u);
  PSX_CHECK(data.has_value());
  PSX_CHECK(data->size() == 8u);
  PSX_CHECK(data->data() == container.data() + 32u);

  /* The wrapper's span borrows the caller's container, including for the last
     subfile, whose size is derived from the container length. */
  const auto last = psx::archive::DEFFEntryData(*table, bytes, 2u);
  PSX_CHECK(last.has_value());
  PSX_CHECK(last->size() == container.size() - 60u);
  PSX_CHECK(last->data() == container.data() + 60u);

  /* Moving a DEFF handle releases what it replaced. */
  auto movedTo = psx::archive::DEFFTableHandle::Parse(bytes);
  auto movedFrom = psx::archive::DEFFTableHandle::Parse(bytes);
  const psx_deff_table_t* replaced = movedTo->Handle();
  movedTo = std::move(movedFrom);
  PSX_CHECK(movedTo->Handle() != replaced);
  PSX_CHECK(movedFrom->Handle() == nullptr); // released, not leaked
  PSX_CHECK(movedTo->Count() == 3u);
}

PSX_TEST(wrapper, tim_wrapper_exposes_borrowed_payload)
{
  const std::vector<std::uint8_t> tim = BuildTIM();

  const auto image = psx::tim::ParseTIM(std::span<const std::uint8_t>(tim));
  PSX_CHECK(image.has_value());
  PSX_CHECK(image->flags == 0x8u);
  PSX_CHECK(image->colorDepth == psx::tim::BPP::Four);
  PSX_CHECK(image->hasCLUT);
  PSX_CHECK(image->clutRect.w == 16);
  PSX_CHECK(image->clutRect.y == 112);
  PSX_CHECK(psx::tim::CLUTEntryCount(image->clutRect) == 256u);
  PSX_CHECK(image->clut.size() == 512u);
  PSX_CHECK(image->imageRect.w == 16);
  PSX_CHECK(image->imageData.size() == 512u);
  PSX_CHECK(image->trailingBytes == 0u);

  /* Borrowed, and still aliasing after the parse: writing through the source is
     visible in the payload the wrapper returned. */
  const std::size_t payloadStart = image->imageSectionOffset + PSX_TIM_SECTION_HEADER_BYTES;
  const auto* alias = image->imageData.data();
  PSX_CHECK(alias == tim.data() + payloadStart);

  std::vector<std::uint8_t> mutableCopy = tim;
  const auto borrowed = psx::tim::ParseTIM(std::span<const std::uint8_t>(mutableCopy));
  PSX_CHECK(borrowed.has_value());
  mutableCopy[payloadStart] = 0x00u;
  mutableCopy[payloadStart + 1u] = 0x00u;
  PSX_CHECK(borrowed->imageData[0] == 0x00u && borrowed->imageData[1] == 0x00u);

  /* The wrapper struct is a value: copying copies descriptors, never pixels. */
  const psx::tim::Image copy = *borrowed;
  PSX_CHECK(copy.imageData.data() == borrowed->imageData.data());
  PSX_CHECK(copy.flags == borrowed->flags);

  /* Rect conversion round-trips, so the C helper sees the same rect. */
  const psx_tim_rect_t asC = psx::tim::detail::ToC(image->clutRect);
  PSX_CHECK(psx::tim::detail::FromC(asC).h == image->clutRect.h);
  PSX_CHECK(psx_tim_clut_entry_count(asC) == psx::tim::CLUTEntryCount(image->clutRect));
}
