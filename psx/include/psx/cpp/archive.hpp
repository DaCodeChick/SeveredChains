#pragma once
/// @file archive.hpp
/// @brief C++23 wrappers over the C MRG/DEFF parsers (psx-c-api.md §7).
///
/// Two flavours, both delegating to the same C core:
///  - `MRGTable` / `DEFFTable`: value types owning their entry array (a
///    std::vector allocated on the C++ side), which is what the engine has always
///    used.
///  - `MRGTableHandle` / `DEFFTableHandle`: move-only RAII owners of the C opaque
///    handles, for consumers that want the C lifetime model in C++.
///
/// @see psx/archive.h for the format layouts and error semantics.

#include "psx/archive.h"
#include "psx/cpp/bytes.hpp"
#include "psx/cpp/error.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

namespace psx::archive
{

/// @brief MRG magic word at offset 0x0.
inline constexpr std::uint32_t MRGMagic = PSX_ARCHIVE_MRG_MAGIC;

/// @brief DEFF magic word at offset 0x0.
inline constexpr std::uint32_t DEFFMagic = PSX_ARCHIVE_DEFF_MAGIC;

/// @brief CD-ROM sector size used by sector-aligned MRG roots (0x800 = 2048).
inline constexpr std::size_t SectorSize = PSX_SECTOR_SIZE;

/// @brief One MRG entry, after virtual-entry resolution.
///
/// Public data is deliberate here (cpp.md §6.2): this is a *result*, not a component.
/// The four fields translate `psx_mrg_entry_t` field-for-field — same widths, same
/// meanings — and nothing must be re-derived from them by hand, which is why `Virtual()`
/// projects `parent` instead of the caller testing `parent >= 0`. It comes back by value
/// from At(), so there is no state anyone could mutate after the parse: the storage that
/// holds the entries is private to MRGTable.
struct MRGEntry
{
  std::size_t offset = 0;      ///< Byte offset into the container (already sector-scaled).
  std::size_t size = 0;        ///< Byte size (0 for a virtual entry with no parent).
  std::size_t virtualSize = 0; ///< Size field as stored (0 marks a virtual entry).
  int parent = -1;             ///< Index of the parent entry, or -1 for a real entry.

  /// @brief Whether this entry aliases another entry instead of owning a range.
  [[nodiscard]] bool Virtual() const noexcept { return parent >= 0; }
};

/// @brief One DEFF subfile range.
///
/// Same reasoning as MRGEntry: a two-field projection of `psx_deff_entry_t`, delivered by
/// value, with no invariant between the fields and no mutator anywhere in the wrapper.
struct DEFFEntry
{
  std::size_t offset = 0; ///< Start offset within the container.
  std::size_t size = 0;   ///< Byte size (next start minus this start).
};

namespace detail
{

/* Inside a nested `detail`, unqualified lookup would stop here and never reach the
   shared span helpers, so they are re-exported rather than repeated. */
using ::psx::detail::ToBytes;
using ::psx::detail::ToSpan;
using ::psx::detail::ToWritable;

/// @brief Convert a C MRG entry to the wrapper type.
/// @param Source C entry.
/// @return The equivalent MRGEntry.
[[nodiscard]] constexpr MRGEntry FromC(const psx_mrg_entry_t& source) noexcept
{
  return MRGEntry{source.offset, source.size, source.virtual_size, source.parent};
}

/// @brief Convert a C DEFF entry to the wrapper type.
/// @param Source C entry.
/// @return The equivalent DEFFEntry.
[[nodiscard]] constexpr DEFFEntry FromC(const psx_deff_entry_t& source) noexcept
{
  return DEFFEntry{source.offset, source.size};
}

} // namespace detail

/// @brief Parsed MRG entry table (owns its entry array, no asset bytes).
///
/// @note Copyable and movable: the payload it indexes is never owned, so a copy is
///       just the table (a few hundred bytes for a real DRGN archive).
/// @note Thread: value type; safe to copy per thread. Concurrent readers of one
///       table are fine, mutation is not part of the API.
class MRGTable
{
public:
  /// @brief Empty table that indexes nothing.
  MRGTable() = default;

  /// @brief Number of entries in the table.
  [[nodiscard]] std::size_t Count() const noexcept { return storage_.size(); }

  /// @brief Entries in the C representation — for debugging, C interop, and for
  ///        proving that a View() aliases this table's own storage.
  /// @note Lifetime: borrowed; valid while this table is alive and unmodified.
  [[nodiscard]] std::span<const psx_mrg_entry_t> Entries() const noexcept { return storage_; }

  /// @brief Entry at index.
  /// @param index Entry index; must be < Count().
  /// @return The entry, or Error::MRGEntryMissing when index is out of range.
  [[nodiscard]] Result<MRGEntry> At(std::size_t index) const noexcept
  {
    if (index >= storage_.size())
      return std::unexpected(Error::MRGEntryMissing);
    return detail::FromC(storage_[index]);
  }

  /// @brief How many entries alias another entry.
  [[nodiscard]] std::size_t VirtualCount() const noexcept { return virtualEntries_; }

  /// @brief Borrow the table as the C view type (no copy).
  /// @return A psx_mrg_view_t aliasing this table's storage.
  /// @note Lifetime: valid while this table is alive and unmodified.
  [[nodiscard]] psx_mrg_view_t View() const noexcept
  {
    return psx_mrg_view_t{storage_.data(), storage_.size(), virtualEntries_};
  }

private:
  /* Entries are stored in the C representation so the wrapper can hand the core a
     pointer without copying; the accessors above are the C++ face.

     Both fields are private because they form one invariant: the virtual count is a
     property of this exact entry array. Public fields let a caller resize or reorder
     the array while the count stays behind, and View() then hands the core a
     virtual_count that does not describe the entries beside it. ParseMRG below is the
     only writer, so the pair can only ever come from one parse. */
  friend Result<MRGTable> ParseMRG(std::span<const std::uint8_t> data, bool sectorAligned) noexcept;

  std::vector<psx_mrg_entry_t> storage_;
  std::size_t virtualEntries_ = 0;
};

/// @brief Parsed DEFF subfile table (owns its entry array, no asset bytes).
class DEFFTable
{
public:
  /// @brief Empty table that indexes nothing.
  DEFFTable() = default;

  /// @brief Number of subfiles.
  [[nodiscard]] std::size_t Count() const noexcept { return storage_.size(); }

  /// @brief Subfiles in the C representation (debugging, C interop).
  /// @note Lifetime: borrowed; valid while this table is alive and unmodified.
  [[nodiscard]] std::span<const psx_deff_entry_t> Entries() const noexcept { return storage_; }

  /// @brief Subfile at index.
  /// @param index Subfile index; must be < Count().
  /// @return The entry, or Error::DEFFEntryMissing when index is out of range.
  [[nodiscard]] Result<DEFFEntry> At(std::size_t index) const noexcept
  {
    if (index >= storage_.size())
      return std::unexpected(Error::DEFFEntryMissing);
    return detail::FromC(storage_[index]);
  }

  /// @brief Borrow the table as the C view type (no copy).
  /// @return A psx_deff_view_t aliasing this table's storage.
  /// @note Lifetime: valid while this table is alive and unmodified.
  [[nodiscard]] psx_deff_view_t View() const noexcept { return psx_deff_view_t{storage_.data(), storage_.size()}; }

private:
  /* Subfile ranges live in the C representation, in container order. Private for the
     same reason as MRGTable's array: the array is what View() publishes, so letting a
     caller swap it out from under a published view would be a lifetime bug waiting to
     happen. ParseDEFF below is the only writer. */
  friend Result<DEFFTable> ParseDEFF(std::span<const std::uint8_t> data) noexcept;

  std::vector<psx_deff_entry_t> storage_;
};

/// @brief Cheap content sniff for MRG.
/// @param data Candidate container.
/// @return Whether this looks like an MRG.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline bool IsMRG(std::span<const std::uint8_t> data) noexcept
{
  return psx_archive_is_mrg(detail::ToBytes(data)) != 0;
}

/// @brief Cheap content sniff for DEFF.
/// @param data Candidate container.
/// @return Whether this looks like a DEFF.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline bool IsDEFF(std::span<const std::uint8_t> data) noexcept
{
  return psx_archive_is_deff(detail::ToBytes(data)) != 0;
}

/// @brief Parse an MRG entry table, resolving virtual entries and duplicates.
/// @param data          Container bytes (not owned, not retained).
/// @param sectorAligned True for DRGN*.BIN roots, where offsets are sector numbers
///                      scaled by SectorSize.
/// @return The table, or Error::MRGMagic, Error::Truncated, or
///         Error::MRGTableTruncated.
/// @note Entry ranges are **not** validated here — MRGEntryData() is the checked
///       slice, which is also what makes a stale table harmless.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline Result<MRGTable> ParseMRG(std::span<const std::uint8_t> data, bool sectorAligned) noexcept
{
  const int sector = sectorAligned ? 1 : 0;
  std::size_t count = 0;
  if (const psx_error_t code = psx_archive_mrg_entry_count(detail::ToBytes(data), sector, &count); code != PSX_OK)
    return std::unexpected(static_cast<Error>(code));

  MRGTable table;
  table.storage_.resize(count);
  psx_mrg_view_t view{};
  if (const psx_error_t code =
        psx_archive_mrg_parse(detail::ToBytes(data), sector, table.storage_.data(), table.storage_.size(), &view);
      code != PSX_OK)
  {
    return std::unexpected(static_cast<Error>(code));
  }
  table.virtualEntries_ = view.virtual_count;
  return table;
}

/// @brief Slice one MRG entry out of its container.
/// @param table   Table produced by ParseMRG.
/// @param container The **same** bytes that were parsed.
/// @param index   Entry index.
/// @return The entry's bytes (borrowed from container), or Error::MRGEntryMissing /
///         Error::MRGEntryOutOfRange. A virtual entry yields its parent's range; an
///         entry with no bytes yields an empty span.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline Result<std::span<const std::uint8_t>>
MRGEntryData(const MRGTable& table, std::span<const std::uint8_t> container, std::size_t index) noexcept
{
  psx_bytes_t out{};
  if (const psx_error_t code = psx_mrg_view_slice(table.View(), detail::ToBytes(container), index, &out);
      code != PSX_OK)
  {
    return std::unexpected(static_cast<Error>(code));
  }
  return detail::ToSpan(out);
}

/// @brief Parse a DEFF subfile table.
/// @param data Container bytes (not owned, not retained).
/// @return The table, or Error::DEFFMagic, Error::Truncated,
///         Error::DEFFTableTruncated, or Error::DEFFOffsetsBad.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline Result<DEFFTable> ParseDEFF(std::span<const std::uint8_t> data) noexcept
{
  std::size_t count = 0;
  if (const psx_error_t code = psx_archive_deff_subfile_count(detail::ToBytes(data), &count); code != PSX_OK)
    return std::unexpected(static_cast<Error>(code));

  DEFFTable table;
  table.storage_.resize(count);
  psx_deff_view_t view{};
  if (const psx_error_t code =
        psx_archive_deff_parse(detail::ToBytes(data), table.storage_.data(), table.storage_.size(), &view);
      code != PSX_OK)
  {
    return std::unexpected(static_cast<Error>(code));
  }
  return table;
}

/// @brief Slice one DEFF subfile out of its container.
/// @param table     Table produced by ParseDEFF.
/// @param container The **same** bytes that were parsed.
/// @param index     Subfile index.
/// @return The subfile bytes (borrowed from container), or Error::DEFFEntryMissing.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline Result<std::span<const std::uint8_t>>
DEFFEntryData(const DEFFTable& table, std::span<const std::uint8_t> container, std::size_t index) noexcept
{
  psx_bytes_t out{};
  if (const psx_error_t code = psx_deff_view_slice(table.View(), detail::ToBytes(container), index, &out);
      code != PSX_OK)
  {
    return std::unexpected(static_cast<Error>(code));
  }
  return detail::ToSpan(out);
}

/// @brief Move-only RAII owner of a C `psx_mrg_table_t` handle.
///
/// @note Ownership: exactly one owner; destruction frees the entry array. Copying
///       is deleted because two owners would double-free.
/// @note Thread: single-owner; read-only accessors may be shared across threads
///       while no thread destroys the handle.
class MRGTableHandle
{
public:
  /// @brief Empty handle (owns nothing).
  MRGTableHandle() = default;

  MRGTableHandle(const MRGTableHandle&) = delete;
  MRGTableHandle& operator=(const MRGTableHandle&) = delete;

  /// @brief Steal the handle from another owner.
  /// @param other Owner to empty.
  MRGTableHandle(MRGTableHandle&& other) noexcept : table_(std::exchange(other.table_, nullptr)) {}

  /// @brief Release the current handle and steal the other one.
  /// @param other Owner to empty.
  /// @return This.
  MRGTableHandle& operator=(MRGTableHandle&& other) noexcept
  {
    if (this != &other)
    {
      psx_mrg_table_destroy(table_);
      table_ = std::exchange(other.table_, nullptr);
    }
    return *this;
  }

  /// @brief Destroy the owned table (if any).
  ~MRGTableHandle() { psx_mrg_table_destroy(table_); }

  /// @brief Parse a container into a freshly owned handle.
  /// @param data          Container bytes (not owned, not retained).
  /// @param sectorAligned True for DRGN*.BIN roots.
  /// @return The handle, or the parse error (see ParseMRG).
  [[nodiscard]] static Result<MRGTableHandle> Parse(std::span<const std::uint8_t> data, bool sectorAligned) noexcept
  {
    psx_mrg_table_t* table = nullptr;
    if (const psx_error_t code = psx_mrg_table_parse(detail::ToBytes(data), sectorAligned ? 1 : 0, &table);
        code != PSX_OK)
    {
      return std::unexpected(static_cast<Error>(code));
    }
    return MRGTableHandle{table};
  }

  /// @brief Whether this handle owns a table.
  /// @return True when non-null.
  [[nodiscard]] explicit operator bool() const noexcept { return table_ != nullptr; }

  /// @brief Number of entries.
  [[nodiscard]] std::size_t Count() const noexcept { return psx_mrg_table_count(table_); }

  /// @brief How many entries alias another entry.
  [[nodiscard]] std::size_t VirtualCount() const noexcept { return psx_mrg_table_virtual_count(table_); }

  /// @brief Borrow the owned entries as a C view.
  /// @return The view, or an empty view for a null handle.
  [[nodiscard]] psx_mrg_view_t View() const noexcept
  {
    psx_mrg_view_t view{};
    (void)psx_mrg_table_view(table_, &view);
    return view;
  }

  /// @brief Slice one entry out of the container this table was parsed from.
  /// @param container The same bytes passed to Parse().
  /// @param index     Entry index.
  /// @return The entry's bytes, or the C error mapped into psx::Error.
  [[nodiscard]] Result<std::span<const std::uint8_t>> Slice(std::span<const std::uint8_t> container,
                                                            std::size_t index) const noexcept
  {
    psx_bytes_t out{};
    if (const psx_error_t code = psx_mrg_view_slice(View(), detail::ToBytes(container), index, &out); code != PSX_OK)
      return std::unexpected(static_cast<Error>(code));
    return detail::ToSpan(out);
  }

  /// @brief Escape hatch: the raw C handle (still owned by this wrapper).
  /// @return The handle pointer, or null.
  /// @note Lifetime: valid until this wrapper is destroyed or reassigned.
  [[nodiscard]] psx_mrg_table_t* Handle() const noexcept { return table_; }

private:
  /// @brief Adopt an already-created handle (only Parse() may do this).
  /// @param table Handle from psx_mrg_table_parse().
  explicit MRGTableHandle(psx_mrg_table_t* table) noexcept : table_(table) {}

  psx_mrg_table_t* table_ = nullptr;
};

/// @brief Move-only RAII owner of a C `psx_deff_table_t` handle.
/// @see MRGTableHandle for the ownership and threading contract.
class DEFFTableHandle
{
public:
  DEFFTableHandle() = default;
  DEFFTableHandle(const DEFFTableHandle&) = delete;
  DEFFTableHandle& operator=(const DEFFTableHandle&) = delete;

  /// @brief Steal the handle from another owner.
  /// @param other Owner to empty.
  DEFFTableHandle(DEFFTableHandle&& other) noexcept : table_(std::exchange(other.table_, nullptr)) {}

  /// @brief Release the current handle and steal the other one.
  /// @param other Owner to empty.
  /// @return This.
  DEFFTableHandle& operator=(DEFFTableHandle&& other) noexcept
  {
    if (this != &other)
    {
      psx_deff_table_destroy(table_);
      table_ = std::exchange(other.table_, nullptr);
    }
    return *this;
  }

  /// @brief Destroy the owned table (if any).
  ~DEFFTableHandle() { psx_deff_table_destroy(table_); }

  /// @brief Parse a container into a freshly owned handle.
  /// @param data Container bytes (not owned, not retained).
  /// @return The handle, or the parse error (see ParseDEFF).
  [[nodiscard]] static Result<DEFFTableHandle> Parse(std::span<const std::uint8_t> data) noexcept
  {
    psx_deff_table_t* table = nullptr;
    if (const psx_error_t code = psx_deff_table_parse(detail::ToBytes(data), &table); code != PSX_OK)
      return std::unexpected(static_cast<Error>(code));
    return DEFFTableHandle{table};
  }

  /// @brief Whether this handle owns a table.
  /// @return True when non-null.
  [[nodiscard]] explicit operator bool() const noexcept { return table_ != nullptr; }

  /// @brief Number of subfiles.
  [[nodiscard]] std::size_t Count() const noexcept { return psx_deff_table_count(table_); }

  /// @brief Borrow the owned subfiles as a C view.
  /// @return The view, or an empty view for a null handle.
  [[nodiscard]] psx_deff_view_t View() const noexcept
  {
    psx_deff_view_t view{};
    (void)psx_deff_table_view(table_, &view);
    return view;
  }

  /// @brief Slice one subfile out of the container this table was parsed from.
  /// @param container The same bytes passed to Parse().
  /// @param index     Subfile index.
  /// @return The subfile bytes, or the mapped error.
  [[nodiscard]] Result<std::span<const std::uint8_t>> Slice(std::span<const std::uint8_t> container,
                                                            std::size_t index) const noexcept
  {
    psx_bytes_t out{};
    if (const psx_error_t code = psx_deff_view_slice(View(), detail::ToBytes(container), index, &out); code != PSX_OK)
      return std::unexpected(static_cast<Error>(code));
    return detail::ToSpan(out);
  }

  /// @brief Escape hatch: the raw C handle (still owned by this wrapper).
  /// @return The handle pointer, or null.
  /// @note Lifetime: valid until this wrapper is destroyed or reassigned.
  [[nodiscard]] psx_deff_table_t* Handle() const noexcept { return table_; }

private:
  /// @brief Adopt an already-created handle (only Parse() may do this).
  /// @param table Handle from psx_deff_table_parse().
  explicit DEFFTableHandle(psx_deff_table_t* table) noexcept : table_(table) {}

  psx_deff_table_t* table_ = nullptr;
};

} // namespace psx::archive
