#pragma once
/// @file bytes.h
/// @brief `psx_bytes_t` — the non-owning byte buffer passed to and returned by
/// every `psx` function (docs/architecture/psx-c-api.md §3).
///
/// Ownership rule for the whole library, stated once here:
///
///  - A `psx_bytes_t` **never owns storage**. The caller allocates, the library
///    reads or writes, and the caller frees.
///  - Anything the library hands back inside a `psx_bytes_t` (TIM sections, MRG
///    entry slices, DEFF subfiles) **borrows** the caller's input buffer and is
///    valid only while that buffer is alive and unmodified.
///  - The only `psx` allocation is behind an opaque table handle
///    (`psx_mrg_table_t` / `psx_deff_table_t`), which the matching `_destroy`
///    function frees.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief A borrowed, contiguous byte range.
///
/// @note Alignment: byte-granular and **unaligned-safe** — the library reads
///       multi-byte fields byte by byte (little-endian), so `data` may have any
///       alignment and no field of any input format is accessed in place.
/// @note Lifetime: `data` is not copied and not retained beyond the call, except
///       where a function's documentation says the result borrows it.
typedef struct psx_bytes
{
  const uint8_t* data; ///< First byte; may be NULL only when size is 0.
  size_t size;         ///< Byte count.
} psx_bytes_t;

/// @brief A writable output range supplied by the caller.
///
/// @note Ownership: storage stays the caller's during and after the call.
///       Functions report how many bytes they wrote through a separate
///       `outWritten` parameter; bytes past that count are untouched.
typedef struct psx_writable_bytes
{
  uint8_t* data;   ///< First byte; may be NULL only when capacity is 0.
  size_t capacity; ///< Bytes addressable from `data`.
} psx_writable_bytes_t;

/// @brief Well-formedness test used by every entry point: a null pointer with a
/// non-zero size is an invalid argument, not an empty buffer.
///
/// @param Bytes Buffer descriptor to inspect.
/// @return Non-zero when the descriptor is usable (either null with size 0, or a
///         real pointer with any size).
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_bytes_is_valid(psx_bytes_t bytes)
{
  return bytes.data != NULL || bytes.size == 0u;
}

/// @brief Same test for a writable descriptor.
/// @param Bytes Output-buffer descriptor to inspect.
/// @return Non-zero when the descriptor is usable.
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_writable_bytes_is_valid(psx_writable_bytes_t bytes)
{
  return bytes.data != NULL || bytes.capacity == 0u;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
