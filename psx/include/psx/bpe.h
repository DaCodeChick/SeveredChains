#pragma once
/// @file bpe.h
/// @brief The disc's adaptive byte-pair codec — C17 API (psx-library.md §2
/// `psx::bpe`, asset-pipeline.md §2, docs/research/psx-formats.md §1).
///
/// Layout (all little-endian). Provenance, per-field evidence and the deviations
/// of this port are recorded in docs/research/psx-formats.md §1.
///
/// ```text
/// 0x0  u32  decompressed size (total bytes the blocks must produce)
/// 0x4  u32  magic 0x1a455042 ("BPE\x1a")
/// 0x8  u32  block 0 decompressed size (0 = end of stream)
///      u32[256]-ish adaptive dictionary, see below
///      block data (codes, expanded through the dictionary)
///      padding to a 4-byte boundary
///      u32  block 1 decompressed size ... repeat
/// ```
///
/// Each block carries its own dictionary: 256 keys, each holding a left byte and
/// (for non-identity keys) a right byte. A code resolves to its left byte when
/// `left[code] == code`, otherwise it expands in place to `left[code]` followed
/// by `right[code]` and resolution repeats — which is why a code can stand for an
/// arbitrarily long run of bytes.
///
/// Memory contract: every function here is allocation-free. The caller supplies
/// the destination, so a hostile declared size can never cause an allocation —
/// it fails with PSX_ERROR_BPE_OUTPUT_TOO_LARGE against the caller's capacity.

#include "psx/bytes.h"
#include "psx/error.h"
#include "psx/export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief Segment magic, stored as a u32 at offset 0x4.
#define PSX_BPE_MAGIC 0x1a455042u

/// @brief Largest number of decompressed bytes one block may declare; a larger
/// block size is rejected as malformed.
#define PSX_BPE_MAX_BLOCK_SIZE 0x800u

/// @brief Default ceiling on the declared output size (64 MiB).
/// @note In C the ceiling is simply the capacity you pass to
///       psx_bpe_decompress(); this constant exists so the C++ wrapper keeps the
///       default it has always had, and so callers that size a buffer by
///       convention use the same bound.
#define PSX_BPE_DEFAULT_MAX_OUTPUT_BYTES (64u * 1024u * 1024u)

/// @brief Cheap content sniff: at least 8 bytes and the magic at 0x4 matches —
/// the same test the extraction pipeline uses to select its BPE transformer.
/// @param src Candidate segment (borrowed).
/// @return 1 when this looks like a BPE segment, 0 otherwise. Never reports an
///         error: this is a classifier, not a parser.
/// @note Thread: pure function, no state — thread-safe.
PSX_API int psx_bpe_is_bpe(psx_bytes_t src);

/// @brief Read the declared decompressed size (u32 at 0x0).
/// @param src      Candidate segment (borrowed).
/// @param out_size Receives the declared byte count. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_size),
///         PSX_ERROR_TRUNCATED (fewer than 8 bytes), or PSX_ERROR_BPE_MAGIC.
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_bpe_declared_size(psx_bytes_t src, size_t* out_size);

/// @brief Decompress a BPE segment into a caller-owned buffer.
///
/// Every read is bounds-checked and malformed input is rejected rather than
/// replayed: block sizes over 0x800, dictionaries that run past key 0xff, and
/// self-referential dictionaries whose expansion cannot fit the block all return
/// an error instead of looping or overrunning.
///
/// @param src         Compressed segment (borrowed; not retained).
/// @param out         Destination. Its `capacity` is the ceiling on the declared
///                    size; `data` may be NULL only when the segment declares 0
///                    bytes.
/// @param out_written Receives the number of bytes written (== declared size on
///                    success). Unmodified on failure. Required.
/// @return PSX_OK, or:
///         PSX_ERROR_INVALID_ARGUMENT (null out_written, null out.data with
///         non-zero capacity, or null out.data for a non-empty result),
///         PSX_ERROR_TRUNCATED (input ended mid-structure),
///         PSX_ERROR_BPE_MAGIC (bad magic),
///         PSX_ERROR_BPE_OUTPUT_TOO_LARGE (declared size over out.capacity),
///         PSX_ERROR_BPE_BLOCK_SIZE (block over 0x800),
///         PSX_ERROR_BPE_DICTIONARY_OVERFLOW (dictionary wrote past key 0xff),
///         PSX_ERROR_BPE_EXPANSION_OVERFLOW (expansion exceeds what the block can
///         emit),
///         PSX_ERROR_BPE_SIZE_MISMATCH (blocks did not produce exactly the
///         declared size, or the destination is too small).
/// @note On failure the destination is partially written and must be treated as
///       garbage: out_written is only defined for PSX_OK.
/// @note Deviation from the reference decoder, which zero-pads the destination
///       when the blocks produce fewer bytes than the header declares and returns
///       it anyway: here that is PSX_ERROR_BPE_SIZE_MISMATCH, so a truncated
///       segment cannot masquerade as valid data (docs/research/psx-formats.md §1).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_bpe_decompress(psx_bytes_t src, psx_writable_bytes_t out, size_t* out_written);

/// @brief Exact output size of psx_bpe_compress() for a given input size.
///
/// Lets a caller allocate once instead of probing. The framing is deterministic:
/// 8-byte header, then one block per PSX_BPE_MAX_BLOCK_SIZE chunk (4-byte size
/// field + 2 * (1 + 128) dictionary bytes + the chunk, padded up to a 4-byte
/// boundary), then a 4-byte end-of-stream marker.
///
/// @param raw_size  Number of bytes to be wrapped.
/// @param out_size  Receives the required capacity. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_size), or
///         PSX_ERROR_BPE_OUTPUT_TOO_LARGE (raw_size beyond what the u32 header can
///         describe).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_bpe_compressed_size(size_t raw_size, size_t* out_size);

/// @brief Emit a valid BPE segment for raw bytes using an **identity dictionary**
/// (literal pass-through, blocks of at most 0x800 bytes).
///
/// This is tooling, not a size-optimizing compressor: the reference has no
/// packer. The output is accepted by the reference decoder, which makes it useful
/// for fixtures, repair utilities and round-trip tests of the real
/// block/dictionary framing.
///
/// @param raw         Bytes to wrap (borrowed).
/// @param out         Destination; must provide at least the capacity reported by
///                    psx_bpe_compressed_size().
/// @param out_written Receives the segment length. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_written), or
///         PSX_ERROR_BPE_OUTPUT_TOO_LARGE (out.capacity too small, or raw larger
///         than a u32 can describe).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_bpe_compress(psx_bytes_t raw, psx_writable_bytes_t out, size_t* out_written);

#ifdef __cplusplus
} /* extern "C" */
#endif
