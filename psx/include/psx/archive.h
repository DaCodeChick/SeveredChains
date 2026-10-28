#pragma once
/// @file archive.h
/// @brief MRG and DEFF container tables — C17 API (psx-library.md §2
/// `psx::archive`, docs/research/psx-formats.md §2 and §3).
///
/// Both formats are index-only containers over a single buffer: the entries
/// below carry **offsets, never borrowed spans**, so a table cannot dangle when
/// its buffer moves or is released. Slicing is explicit — pass the same buffer
/// you parsed and get a checked range back.
///
/// MRG:
///
/// ```text
/// 0x0  u32   magic 0x1a47524d ("MRG\x1a")
/// 0x4  u32   entry count
/// 0x8  u32   entry[i].offset   (at 0x8 + i * 8)
///      u32   entry[i].size     (at 0x8 + i * 8 + 4)
/// ```
///
/// An entry with size == 0 is *virtual*: its offset field points at a real
/// entry's offset, and it resolves to that entry. Two real entries sharing an
/// offset collapse onto the first one. For root DRGN*.BIN archives the offsets
/// are sector numbers and get multiplied by 0x800; that is a property of the
/// container's path, which this library does not know, so it is the caller's
/// flag (see psx_archive_mrg_parse).
///
/// DEFF:
///
/// ```text
/// 0x0  u32   magic 0x46464544 ("DEFF")
/// 0x6  u16   subfile count
/// 0x8  u32   subfile[i].start  (at 8 + i * 8 + 4)
/// ```
///
/// Subfiles are contiguous: subfile i spans [start(i), start(i+1)), and the end
/// of the last one is the container size.
///
/// Two usage styles, same parser, same results:
///
///  - **Caller-owned storage (no allocation).** psx_archive_mrg_entry_count()
///    then psx_archive_mrg_parse() into an array you own; the resulting
///    psx_mrg_view_t is a borrowed view of that array.
///  - **Opaque handle (allocation).** psx_mrg_table_parse() allocates, the
///    accessors read, psx_mrg_table_destroy() frees. Use it when the table has to
///    outlive a stack buffer; do not use it when a view is enough.

#include "psx/bytes.h"
#include "psx/error.h"
#include "psx/export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief MRG magic word at offset 0x0.
#define PSX_ARCHIVE_MRG_MAGIC 0x1a47524du

/// @brief DEFF magic word at offset 0x0.
#define PSX_ARCHIVE_DEFF_MAGIC 0x46464544u

/// @brief CD-ROM sector size used by sector-aligned MRG roots (0x800 = 2048).
#define PSX_SECTOR_SIZE 0x800u

/// @brief One MRG entry, after virtual-entry resolution.
///
/// @note ABI: plain data; layout frozen for PSX_ABI_VERSION 1.
typedef struct psx_mrg_entry
{
  size_t offset;       ///< Byte offset into the container (already sector-scaled).
  size_t size;         ///< Byte size (0 for a virtual entry with no parent).
  size_t virtual_size; ///< Size field as stored (0 marks a virtual entry).
  int parent;          ///< Index of the parent entry, or -1 for a real entry.
} psx_mrg_entry_t;

/// @brief Whether an MRG entry aliases another entry instead of owning a range.
/// @param entry Entry to test (must not be NULL).
/// @return Non-zero for a virtual (aliasing) entry.
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_mrg_entry_is_virtual(const psx_mrg_entry_t* entry)
{
  return entry != NULL && entry->parent >= 0;
}

/// @brief Borrowed view of a caller-owned MRG entry array.
///
/// @note Lifetime: `entries` belongs to the caller (the array passed to
///       psx_archive_mrg_parse()); the view must not outlive it, and the array
///       must not be modified or freed while the view is used.
typedef struct psx_mrg_view
{
  const psx_mrg_entry_t* entries; ///< Entry array (borrowed); NULL when count is 0.
  size_t count;                   ///< Number of entries.
  size_t virtual_count;           ///< How many entries alias another entry.
} psx_mrg_view_t;

/// @brief Opaque, heap-owned MRG table (psx_mrg_table_parse / _destroy).
typedef struct psx_mrg_table psx_mrg_table_t;

/// @brief One DEFF subfile range.
typedef struct psx_deff_entry
{
  size_t offset; ///< Start offset within the container.
  size_t size;   ///< Byte size (next start minus this start; last one runs to the end).
} psx_deff_entry_t;

/// @brief Borrowed view of a caller-owned DEFF subfile array.
typedef struct psx_deff_view
{
  const psx_deff_entry_t* entries; ///< Subfile array (borrowed); NULL when count is 0.
  size_t count;                    ///< Number of subfiles.
} psx_deff_view_t;

/// @brief Opaque, heap-owned DEFF table (psx_deff_table_parse / _destroy).
typedef struct psx_deff_table psx_deff_table_t;

/* --- content sniffing ---------------------------------------------------- */

/// @brief Cheap content sniff for MRG: at least 8 bytes and the magic matches —
/// the same test the extraction pipeline uses to select its MRG transformer.
/// @param src Candidate container (borrowed).
/// @return 1 when this looks like an MRG, 0 otherwise. Never reports an error.
/// @note Thread: pure function, no state — thread-safe.
PSX_API int psx_archive_is_mrg(psx_bytes_t src);

/// @brief Cheap content sniff for DEFF: at least 8 bytes and the magic matches —
/// the same test the extraction pipeline uses to select its DEFF transformer.
/// @param src Candidate container (borrowed).
/// @return 1 when this looks like a DEFF, 0 otherwise. Never reports an error.
/// @note Thread: pure function, no state — thread-safe.
PSX_API int psx_archive_is_deff(psx_bytes_t src);

/* --- MRG: caller-owned storage ------------------------------------------- */

/// @brief Number of MRG entries a container declares.
/// @param src         Container bytes (borrowed).
/// @param sector_aligned  0: offsets are byte offsets. 1: DRGN*.BIN root, where
///                    the stored offsets are sector numbers scaled by
///                    PSX_SECTOR_SIZE. Any non-zero value means "aligned" (C has
///                    no bool in this ABI; see psx-c-api.md §3).
/// @param out_count  Receives the entry count. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_count),
///         PSX_ERROR_TRUNCATED, PSX_ERROR_MRG_MAGIC, or
///         PSX_ERROR_MRG_TABLE_TRUNCATED (count does not fit the buffer).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_archive_mrg_entry_count(psx_bytes_t src, int sector_aligned, size_t* out_count);

/// @brief Parse an MRG entry table into caller-owned storage, resolving virtual
/// entries and duplicate offsets.
///
/// @param src            Container bytes (borrowed, not retained).
/// @param sector_aligned  1 (any non-zero) for DRGN*.BIN roots (sector-scaled
///                    offsets); 0 for byte offsets.
/// @param entries        Destination array; may be NULL only when the container
///                       declares 0 entries.
/// @param entry_capacity Elements addressable in `entries`.
/// @param out_view       Receives a view of `entries`. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_view, or entry_capacity
///         below the declared count), PSX_ERROR_TRUNCATED, PSX_ERROR_MRG_MAGIC,
///         or PSX_ERROR_MRG_TABLE_TRUNCATED.
/// @note Entry **ranges** are not validated here — psx_mrg_view_slice() is what
///       checks them, which is also what makes a stale table harmless.
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_archive_mrg_parse(psx_bytes_t src, int sector_aligned, psx_mrg_entry_t* entries,
                                          size_t entry_capacity, psx_mrg_view_t* out_view);

/// @brief Entry at index in a view.
/// @param view      View produced by psx_archive_mrg_parse() (or a handle's view).
/// @param index     Entry index.
/// @param out_entry Receives the entry. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_entry), or
///         PSX_ERROR_MRG_ENTRY_MISSING (index out of range).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_mrg_view_at(psx_mrg_view_t view, size_t index, psx_mrg_entry_t* out_entry);

/// @brief Slice one MRG entry out of its container.
/// @param view      View produced from the **same** container.
/// @param container The bytes that were parsed (borrowed; the result borrows them).
/// @param index     Entry index.
/// @param out_data  Receives the entry's bytes. Unmodified on failure.
/// @return PSX_OK (an entry with no bytes yields data NULL / size 0),
///         PSX_ERROR_INVALID_ARGUMENT (null out_data, or container.data NULL with
///         a non-zero size),
///         PSX_ERROR_MRG_ENTRY_MISSING (index out of range), or
///         PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE (the entry claims bytes the container
///         does not have).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_mrg_view_slice(psx_mrg_view_t view, psx_bytes_t container, size_t index, psx_bytes_t* out_data);

/* --- MRG: opaque handle -------------------------------------------------- */

/// @brief Parse an MRG table, allocating the entry array with malloc().
/// @param src            Container bytes (borrowed, not retained).
/// @param sector_aligned  1 (any non-zero) for DRGN*.BIN roots (sector-scaled
///                    offsets); 0 for byte offsets.
/// @param out_table      Receives the handle. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_table),
///         PSX_ERROR_MRG_MAGIC, PSX_ERROR_TRUNCATED,
///         PSX_ERROR_MRG_TABLE_TRUNCATED, or PSX_ERROR_INVALID_ARGUMENT when the
///         allocation fails (the only allocation failure mode in the library;
///         no handle is produced, so nothing leaks).
/// @note Ownership: the caller owns the handle and must call
///       psx_mrg_table_destroy() exactly once.
/// @note Thread: pure function, no shared state — thread-safe. The resulting
///       handle has a single owner; concurrent mutation of one handle is not
///       supported (read-only accessors may be shared).
PSX_API psx_error_t psx_mrg_table_parse(psx_bytes_t src, int sector_aligned, psx_mrg_table_t** out_table);

/// @brief Free an MRG table. Safe to call with NULL and safe to call twice with
/// the same pointer only if the caller has set it to NULL.
/// @param table Handle from psx_mrg_table_parse(), or NULL.
/// @note Thread: single-owner operation, like any free().
PSX_API void psx_mrg_table_destroy(psx_mrg_table_t* table);

/// @brief Number of entries in a handle's table.
/// @param table Handle (may be NULL, which yields 0).
/// @return Entry count.
/// @note Thread: read-only accessor — thread-safe for concurrent readers.
PSX_API size_t psx_mrg_table_count(const psx_mrg_table_t* table);

/// @brief How many entries alias another entry.
/// @param table Handle (may be NULL, which yields 0).
/// @return Virtual-entry count.
/// @note Thread: read-only accessor — thread-safe for concurrent readers.
PSX_API size_t psx_mrg_table_virtual_count(const psx_mrg_table_t* table);

/// @brief Borrow the handle's entry array as a view (no copy, no allocation).
/// @param table    Handle (may be NULL, which yields an empty view).
/// @param out_view Receives the view. Unmodified on failure.
/// @return PSX_OK or PSX_ERROR_INVALID_ARGUMENT (null out_view).
/// @note Lifetime: the view is valid until psx_mrg_table_destroy(table).
/// @note Thread: read-only accessor — thread-safe for concurrent readers.
PSX_API psx_error_t psx_mrg_table_view(const psx_mrg_table_t* table, psx_mrg_view_t* out_view);

/* --- DEFF: caller-owned storage ------------------------------------------ */

/// @brief Number of DEFF subfiles a container declares.
/// @param src        Container bytes (borrowed).
/// @param out_count  Receives the subfile count. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_count),
///         PSX_ERROR_TRUNCATED, PSX_ERROR_DEFF_MAGIC, or
///         PSX_ERROR_DEFF_TABLE_TRUNCATED.
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_archive_deff_subfile_count(psx_bytes_t src, size_t* out_count);

/// @brief Parse a DEFF subfile table into caller-owned storage.
/// @param src            Container bytes (borrowed, not retained).
/// @param entries        Destination array; may be NULL only for 0 subfiles.
/// @param entry_capacity Elements addressable in `entries`.
/// @param out_view       Receives a view of `entries`. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_view, capacity below the
///         declared count), PSX_ERROR_DEFF_MAGIC, PSX_ERROR_TRUNCATED,
///         PSX_ERROR_DEFF_TABLE_TRUNCATED, or PSX_ERROR_DEFF_OFFSETS_BAD
///         (non-monotonic or out-of-range subfile starts).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_archive_deff_parse(psx_bytes_t src, psx_deff_entry_t* entries, size_t entry_capacity,
                                           psx_deff_view_t* out_view);

/// @brief Subfile at index in a view.
/// @param view      View produced by psx_archive_deff_parse().
/// @param index     Subfile index.
/// @param out_entry Receives the entry. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_entry), or
///         PSX_ERROR_DEFF_ENTRY_MISSING (index out of range).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_deff_view_at(psx_deff_view_t view, size_t index, psx_deff_entry_t* out_entry);

/// @brief Slice one DEFF subfile out of its container.
/// @param view      View produced from the **same** container.
/// @param container The bytes that were parsed (borrowed; the result borrows them).
/// @param index     Subfile index.
/// @param out_data  Receives the subfile bytes. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_data, or container.data
///         NULL with a non-zero size), or PSX_ERROR_DEFF_ENTRY_MISSING (index out
///         of range, or — as in the C++
///         API this replaced — a range that does not fit the container).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_deff_view_slice(psx_deff_view_t view, psx_bytes_t container, size_t index,
                                        psx_bytes_t* out_data);

/* --- DEFF: opaque handle ------------------------------------------------- */

/// @brief Parse a DEFF table, allocating the entry array with malloc().
/// @param src       Container bytes (borrowed, not retained).
/// @param out_table Receives the handle. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_table, or allocation
///         failure), PSX_ERROR_DEFF_MAGIC, PSX_ERROR_TRUNCATED,
///         PSX_ERROR_DEFF_TABLE_TRUNCATED, or PSX_ERROR_DEFF_OFFSETS_BAD.
/// @note Ownership: the caller owns the handle and must call
///       psx_deff_table_destroy() exactly once.
/// @note Thread: pure function, no shared state — thread-safe; the handle has a
///       single owner.
PSX_API psx_error_t psx_deff_table_parse(psx_bytes_t src, psx_deff_table_t** out_table);

/// @brief Free a DEFF table.
/// @param table Handle from psx_deff_table_parse(), or NULL.
/// @note Thread: single-owner operation, like any free().
PSX_API void psx_deff_table_destroy(psx_deff_table_t* table);

/// @brief Number of subfiles in a handle's table.
/// @param table Handle (may be NULL, which yields 0).
/// @return Subfile count.
/// @note Thread: read-only accessor — thread-safe for concurrent readers.
PSX_API size_t psx_deff_table_count(const psx_deff_table_t* table);

/// @brief Borrow the handle's subfile array as a view (no copy, no allocation).
/// @param table    Handle (may be NULL, which yields an empty view).
/// @param out_view Receives the view. Unmodified on failure.
/// @return PSX_OK or PSX_ERROR_INVALID_ARGUMENT (null out_view).
/// @note Lifetime: the view is valid until psx_deff_table_destroy(table).
/// @note Thread: read-only accessor — thread-safe for concurrent readers.
PSX_API psx_error_t psx_deff_table_view(const psx_deff_table_t* table, psx_deff_view_t* out_view);

#ifdef __cplusplus
} /* extern "C" */
#endif
