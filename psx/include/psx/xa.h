#pragma once
/// @file xa.h
/// @brief PS1 XA 4-bit ADPCM sector decoding — C17 API (psx-library.md §2
/// `psx::xa`).
///
/// Scope is *decoding a sector to PCM*. Three things the reference does around
/// decoding are deliberately not in here, because none of them is decoding:
///
/// - gain: `Fmv.java:425` applies `>>= 1` and a volume multiply *after*
///   `XaAdpcm.decode` returns, in the FMV player;
/// - `PRE_SKIP = 312` (`XaTranscoder.java:39`): a run of 312 leading zeros fed
///   to the Opus encoder and recorded in the Opus header (`OpusFile(channels,
///   PRE_SKIP, 37800)`). It is an offline encoding artefact that lets an Opus
///   decoder discard encoder lookahead. Decoded XA PCM is never truncated by
///   it, so there is nothing to reproduce in a decoder;
/// - `resampleTo44100Hz` and the `zigZag` table: dead code, never called.
///
/// Keeping them out is what makes the core testable on synthetic input, and it
/// is why the C++ surface stops at samples. An earlier revision of this comment
/// filed pre-skip and interleaving under "the playback layer", which was
/// vague-to-wrong: pre-skip belongs to a transcoding tool, not to playback.
///
/// Sector layout handled here (the reference's, see the deviations note):
///
/// ```text
/// 0x000  24 bytes   sync + header + subheader (skipped)
/// 0x018  18 x 128-byte portions:
///          4..11    shift/filter byte per (block, channel)
///          16..127  112 bytes of nibble-pair sample data
/// 0x918  remainder of the 2352-byte sector is not read
/// ```
///
/// Coding info comes from the sector byte at offset 19, which is also how the
/// reference reaches it: `Fmv.java:421` calls `XaAdpcm.decode(data, data[19])`
/// on a 2352-byte buffer, and `SectorHeader.Coding` decodes that same byte
/// (bit 0 stereo, bit 2 the 18900 Hz rate flag, bit 4 the 8-bit flag).
///
/// Only bit 0 changes the decode path. The rate flag has no resampler behind it
/// in the reference (`resampleTo44100Hz` is never called) and is not
/// implemented here either. The 8-bit flag is a **behaviour difference**:
/// `XaAdpcm.decode` computes `is8BitPerSample` and then ignores it, decoding
/// 8-bit data as 4-bit nibbles and producing noise, whereas this port reports
/// `PSX_ERROR_XA_UNSUPPORTED_CODING`. Refusing an unsupported format is the
/// safer failure; silently decoding it as a supported one yields plausible
/// looking garbage. No retail sector has been checked for the flag, so whether
/// any real asset needs a true 8-bit path is an open question.
///
/// @note **Deviation from the reference, deliberate.** `XaAdpcm` keeps its
///       predictor history in `static` fields, so every stream on the machine
///       shares one filter state and two concurrent tracks corrupt each other.
///       Here the history lives in a caller-owned `psx_xa_stream_t`, which
///       makes per-stream decoding correct and the function re-entrant. Output
///       for a *single* stream is bit-identical either way.
/// @note **Unverified against hardware.** The reference reads each block's
///       shift/filter bytes at 4..11 and its samples at 16..127, while the
///       documented CD-ROM XA Mode 2 block is usually described as an 8-byte
///       header over 120 data bytes. What is *not* in doubt is the stereo bit:
///       the reference is internally consistent, because `XaAdpcm.decode` and
///       `SectorHeader.Coding.isStereo` both read bit 0 as *set = stereo*. An
///       earlier revision of this note claimed the two disagreed; that claim
///       was wrong and has been removed. The open question is therefore only
///       the block layout. This port reproduces the reference because that is
///       the oracle available to this project (ruling R1); it is not a claim
///       that the reference matches the console. Recorded rather than silently
///       "corrected", because changing the layout would break every golden
///       without evidence for the replacement.
/// @see docs/research/psx-formats.md §6 for the measured findings and §7 for
///      the open questions, and psx/tools/xa_oracle.py for how the differential
///      goldens are made.

#include "psx/bytes.h"
#include "psx/error.h"
#include "psx/export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief Raw sector size the reference hands to the decoder (Mode 2, 2352).
#define PSX_XA_SECTOR_SIZE 2352

/// @brief Bytes skipped at the front of a sector (sync + header + subheader).
#define PSX_XA_HEADER_SIZE 24

/// @brief 128-byte ADPCM portions per sector.
#define PSX_XA_PORTION_COUNT 18

/// @brief Bytes per ADPCM portion.
#define PSX_XA_PORTION_SIZE 128

/// @brief Interleaved 16-bit sample count ceiling for one sector (mono = 8064,
/// stereo = 4032). Mono is the larger because each decoded sample is written
/// twice; use psx_xa_decoded_samples() for the exact figure.
#define PSX_XA_MAX_SAMPLES 8064

/// @brief Offset of the coding-info byte inside a raw sector.
#define PSX_XA_CODING_INFO_OFFSET 19

/// @brief Coding-info bit 0. Set selects the stereo decode path (see the file
/// note about how this differs from the hardware encoding).
#define PSX_XA_FLAG_STEREO 0x01u

/// @brief Coding-info bit 2. Set marks the 18900 Hz rate flag. Nothing
/// consumes it in this build; it is reported so the caller can resample.
#define PSX_XA_FLAG_HALF_RATE 0x04u

/// @brief Coding-info bit 4. Set marks 8-bit samples, which are not
/// implemented — decoding such a sector returns `PSX_ERROR_XA_UNSUPPORTED_CODING`.
#define PSX_XA_FLAG_8BIT 0x10u

/// @brief Per-stream ADPCM filter history.
///
/// Two predictor taps per channel. `prev`/`prev2` index 0 is the left (or, for
/// a mono sector, the only) channel and 1 the right. The reference carries the
/// same values in process-wide statics; carrying them here is what makes two
/// streams independent.
typedef struct psx_xa_stream
{
  int16_t prev[2];  ///< `old` tap: the most recent decoded sample per channel.
  int16_t prev2[2]; ///< `older` tap: the sample before that.
} psx_xa_stream_t;

/// @brief Reset a stream to the start-of-track state (both taps zero).
///
/// @param stream Target. NULL is ignored.
/// @note Thread: touches only its argument — safe per-stream.
PSX_API void psx_xa_stream_init(psx_xa_stream_t* stream);

/// @brief Read the coding-info byte of a raw sector.
///
/// @param sector   Raw sector (borrowed); must cover offset 19.
/// @param out_info Receives the byte at PSX_XA_CODING_INFO_OFFSET. Unmodified
///                 on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_info), or
///         PSX_ERROR_TRUNCATED (sector shorter than the offset).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_xa_coding_info(psx_bytes_t sector, uint8_t* out_info);

/// @brief Interleaved 16-bit sample count psx_xa_decode_sector() would write
/// for this coding byte: 4032 stereo, 8064 mono (each mono sample duplicated).
///
/// Lets a caller size its destination from the header instead of the worst
/// case. No state, no allocation.
///
/// @param coding_info   The coding byte (only bit 0 is read).
/// @param out_samples   Receives the count. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null out_samples), or
///         PSX_ERROR_XA_UNSUPPORTED_CODING (bit 4 set: 8-bit data).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_xa_decoded_samples(uint8_t coding_info, size_t* out_samples);

/// @brief Decode one raw XA sector into interleaved stereo PCM.
///
/// Reads bytes 24..0x918 of `sector`. Mono input is widened by writing each
/// decoded sample twice so the caller always gets a stereo pair stream, matching
/// the reference's output contract. The stream's filter history is read and then
/// updated, which is what makes consecutive sectors of one track continuous.
///
/// @param stream      In/out predictor state; carry it across sectors of one
///                    track and reset it between tracks. NULL decodes from the
///                    zero state without writing back (one-shot decode).
/// @param sector      Raw sector (borrowed, not retained). Fewer than 24 +
///                    18*128 bytes yields PSX_ERROR_TRUNCATED.
/// @param coding_info The sector's coding byte (usually sector.data[19]).
/// @param out         Destination for interleaved int16 samples. Capacity is
///                    measured in **bytes**; needs at least
///                    psx_xa_decoded_samples() * 2.
/// @param out_samples Receives the interleaved sample count written. Unmodified
///                    on failure.
/// @return PSX_OK, or:
///         PSX_ERROR_INVALID_ARGUMENT (null out_samples),
///         PSX_ERROR_TRUNCATED (sector shorter than 24 + 18*128 bytes),
///         PSX_ERROR_XA_UNSUPPORTED_CODING (8-bit coding flag set),
///         PSX_ERROR_XA_OUTPUT_TOO_LARGE (capacity below the required bytes).
/// @note Thread: mutates `*stream`, so one stream must be driven by one thread
///       at a time. Independent streams are independent — which is the point of
///       moving the history out of process-wide statics.
PSX_API psx_error_t psx_xa_decode_sector(psx_xa_stream_t* stream, psx_bytes_t sector, uint8_t coding_info,
                                         psx_writable_bytes_t out, size_t* out_samples);

#ifdef __cplusplus
} /* extern "C" */
#endif