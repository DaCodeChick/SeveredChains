#pragma once
/// @file cdrom.h
/// @brief CD-ROM XA sector geometry and raw-sector header decoding — C17 API
/// (docs/architecture/psx-library.md §2, `psx::cdrom`).
///
/// This is the **sector layer**: it turns a raw 2352-byte sector into
/// decodable facts (which kind of data it carries, where the payload starts,
/// how big it is) so that a decoder such as `psx::xa` can be fed from real
/// disc data instead of hand-built buffers. It deliberately contains no file
/// I/O: every function works over a borrowed `psx_bytes_t`, so the caller
/// decides whether that span came from a memory-mapped image, a loaded
/// container, or a test fixture (docs/architecture/psx-c-api.md §3).
///
/// What is *not* here, and why:
///
///  - **ISO 9660 name/directory parsing.** The reference reads a volume
///    descriptor at sector 16 (`Unpacker.PVD_SECTOR`) and walks directory
///    records, all of which land on the same 24-byte data boundary this file
///    uses (see the note on `psx_sector_payload`). What is missing is the
///    parser itself and goldens to verify it against: a hand-built volume
///    descriptor would only prove self-consistency, so that work lands with a
///    real disc image rather than being guessed now.
///  - **The FMV mux** (`VideoSector`: 2016-byte chunks at sector offset 56)
///    and **XA interleave groups** (`XaTranscoder.interleaveMode`, 4 or 16,
///    chosen from the mod's filename). Neither is a PlayStation sector format:
///    the first is a container the mod's own tooling writes for the PC
///    remaster, the second is how the mod packs audio and video sectors. Both
///    therefore belong to the engine/game layer under AGENTS.md's rule keeping
///    reusable PSX technology separate from Legend of Dragoon-specific code,
///    and both were ruled out of this module explicitly on 2026-10-29 rather
///    than left ambiguous. The measured layout is recorded in
///    docs/research/psx-formats.md §8.3 so the video path starts from
///    measurements; no placeholder API for them exists here.
///
/// Provenance of every offset and bit in this header is listed in
/// docs/research/psx-formats.md §8, traced to `SectorHeader.java`,
/// `IsoReader.java`, `Unpacker.java`, `VideoSector.java`, `XaTranscoder.java`,
/// and `XaAdpcm.java` in the reference repository at commit 3433e6f.

#include "psx/bytes.h"
#include "psx/error.h"
#include "psx/export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* --- Sector geometry ------------------------------------------------------ */

/// @brief Raw sector size, byte 0..2351 (IsoReader.SECTOR_SIZE = 0x930).
#define PSX_CDROM_SECTOR_SIZE 2352u

/// @brief Sync pattern length (IsoReader.SYNC_PATTER_SIZE).
///
/// @note Not a payload offset. `IsoReader.seekSector` stops here, and every
///       caller then skips eight more bytes (`Unpacker` advances
///       `SYNC_PATTER_SIZE` again before reading a volume descriptor or a
///       directory record), so the reference's data boundary is still byte 24.
///       Only `psx_sector_run_extent` and `psx_sector_payload` offer payload
///       positions, and both use 24.
#define PSX_CDROM_SYNC_SIZE 12u

/// @brief Bytes before user data (SectorHeader.HEADER_SIZE, and the offset
/// `IsoReader.readSectors` copies from).
///
/// @note The *sum* is what the reference pins. The usual CD-ROM description of
///       those 24 bytes is 12 sync + 4 header + 8 error-check/reserved, but
///       nothing in the reference or in a real image is consulted here, so do
///       not read that split as verified — only 24 is.
#define PSX_CDROM_HEADER_SIZE 24u

/// @brief User-data size for a Form 1 sector.
///
/// This is also the size the reference uses for *every* non-raw file read,
/// regardless of the sector's actual form (IsoReader.readSectors). A Form 2
/// sector therefore loses 280 bytes per sector through that path; the
/// form-aware psx_sector_payload() does not.
#define PSX_CDROM_FORM1_DATA_SIZE 2048u

/// @brief User-data size for a Form 2 sector (2352 - 24).
#define PSX_CDROM_FORM2_DATA_SIZE 2328u

/* --- Raw-sector header byte offsets --------------------------------------- */

/* The reference reads these from the same 2352-byte buffer it fills per
 * sector, so the numbers below are absolute offsets inside a raw sector.
 *
 * These are the reference's positions, reproduced rather than re-derived. Two
 * of them are corroborated by a second place in the reference: IsoReader's
 * Form 2 test is `(sectorData[16 + 2] >>> 5) & 1`, which is bit 5 of byte 18,
 * and the XA decoder takes its coding byte from 19 as well
 * (`Fmv.play -> XaAdpcm.decode(data, data[19])`).
 *
 * Whether those positions match the CD-ROM XA white-book layout is **not
 * established** — see psx-formats.md §8. Do not "correct" them from a
 * hardware description without a real image to measure against.
 *
 * The reference *also* defines getMode() at offset 15 and never calls it (one
 * match repo-wide: its own declaration), and this header exposes no `mode`
 * field. That is a decision rather than an oversight. A mode byte only means
 * something if the buffer being parsed is known to be a standard raw CD-ROM
 * sector, and nothing here can establish that — the white-book layout alone is
 * not sufficient evidence. The one path that would have used it (FMV playback)
 * feeds 2352-byte buffers taken from the mod's own mux file, and nothing shows
 * those files were authored with a standard sector header in bytes 0..23 at
 * all. Revisit only when a concrete caller or a verified format requirement
 * needs the field. */

/// @byte Offset of the Mode 2 channel number.
#define PSX_CDROM_CHANNEL_OFFSET 17u

/// @brief Offset of the Mode 2 submode byte.
#define PSX_CDROM_SUBMODE_OFFSET 18u

/// @brief Offset of the Mode 2 coding byte. Equal to
/// PSX_XA_CODING_INFO_OFFSET in psx/xa.h; the equality is pinned by a test so
/// the two headers cannot drift apart.
#define PSX_CDROM_CODING_OFFSET 19u

/* --- Submode bit masks (SectorHeader.Submode) ----------------------------- */

/// @brief Submode bit 7. Marks the last sector of a track/sequence. The
/// reference stops playback when it sees this (Fmv.play -> header.submode.isEof).
#define PSX_CDROM_SUBMODE_EOF 0x80u

/// @brief Submode bit 6. Sector is part of a real-time (streamed) transfer.
#define PSX_CDROM_SUBMODE_REALTIME 0x40u

/// @brief Submode bit 5. Clear = Form 1, set = Form 2. Selects the payload
/// size returned by psx_sector_payload().
#define PSX_CDROM_SUBMODE_FORM 0x20u

/// @brief Submode bit 4. Transfer-trigger bit (heuristic, drive-dependent).
#define PSX_CDROM_SUBMODE_TRIGGER 0x10u

/// @brief Submode bits 1..3: the data type, read with psx_sector_classify().
#define PSX_CDROM_SUBMODE_TYPE 0x0Eu

/// @brief Submode bit 0. End of an audio sequence.
#define PSX_CDROM_SUBMODE_END_AUDIO 0x01u

/* --- Coding bit masks (SectorHeader.Coding) ------------------------------- */

/// @brief Coding bit 4. Set means 8-bit samples, clear means 4-bit ADPCM
/// (psx_xa_decode_sector refuses the 8-bit case).
#define PSX_CDROM_CODING_8BIT 0x10u

/// @brief Coding bit 2. Set means the 18900 Hz rate flag, clear means 37800 Hz.
#define PSX_CDROM_CODING_HALF_RATE 0x04u

/// @brief Coding bit 0. Set means stereo.
/// @note The reference reads this as "stereo" because its own XA decoder
///       consumes nibbles two at a time (mask `1 << (nibble & 7)`, not `& 1`).
///       That is a decoder-level detail, not evidence about the hardware
///       encoding; see docs/research/psx-formats.md §6.
#define PSX_CDROM_CODING_STEREO 0x01u

/* --- Types ---------------------------------------------------------------- */

/// @brief The three header bytes a sector consumer needs.
///
/// Held by value and copied out, deliberately: the reference keeps a reference
/// to the sector buffer and re-reads it, which is correct there but makes the
/// result depend on when you ask. Here the caller parses after filling a
/// buffer, and the parsed value cannot change underneath.
///
/// @note There is no `mode` field. See the note above the offset defines.
typedef struct psx_sector_header
{
  uint8_t channel; ///< Byte 17: Mode 2 channel number.
  uint8_t submode; ///< Byte 18: Mode 2 submode, decoded by the helpers below.
  uint8_t coding;  ///< Byte 19: Mode 2 coding, decoded by the helpers below.
} psx_sector_header_t;

/// @brief Sector data form, taken from submode bit 5.
typedef enum psx_sector_form
{
  PSX_SECTOR_FORM_1 = 0, ///< 2048 payload bytes + error correction.
  PSX_SECTOR_FORM_2 = 1  ///< 2328 payload bytes, no error correction.
} psx_sector_form_t;

/// @brief What a sector carries, from submode bits 1..3.
///
/// The reference throws on any value outside these three
/// (`SectorHeader.Submode.getType`'s default branch). Here an unrecognised
/// pattern is a reported error instead, because a malformed sector must not be
/// able to abort the process (docs/architecture/error-handling.md §4.1).
typedef enum psx_sector_kind
{
  PSX_SECTOR_KIND_VIDEO = 2, ///< Streamed video/mux payloads (routing label only; decoding is engine-side).
  PSX_SECTOR_KIND_AUDIO = 4, ///< XA audio data.
  PSX_SECTOR_KIND_DATA = 8   ///< Raw / Mode 1 data.
} psx_sector_kind_t;

/// @brief A byte range inside a larger span, expressed as position + length.
///
/// @note Lifetime: describes a range inside the span passed to the call. The
///       extent itself owns nothing.
typedef struct psx_sector_extent
{
  size_t offset; ///< First byte of the range, measured from the span start.
  size_t length; ///< Bytes in the range.
} psx_sector_extent_t;

/* --- Header decoding ------------------------------------------------------ */

/// @brief Read a sector's header.
///
/// @param sector     Raw sector (borrowed). Must cover at least the 24-byte
///                   header region; a full 2352-byte sector is normal.
/// @param out_header Receives channel/submode/coding. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null `out_header`, or a null
///         `sector.data` with non-zero size), or PSX_ERROR_TRUNCATED (fewer
///         than PSX_CDROM_HEADER_SIZE bytes).
/// @note Thread: no state — thread-safe.
PSX_API psx_error_t psx_sector_header_parse(psx_bytes_t sector, psx_sector_header_t* out_header);

/// @brief Resolve what a parsed header says the sector carries.
///
/// @param header   A header from psx_sector_header_parse().
/// @param out_kind Receives VIDEO, AUDIO, or DATA. Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null `out_kind`), or
///         PSX_ERROR_CDROM_BAD_SECTOR_TYPE when bits 1..3 are not one of the
///         three defined types (patterns 0, 6, 10, 12, and 14).
/// @note Thread: no state — thread-safe.
PSX_API psx_error_t psx_sector_classify(psx_sector_header_t header, psx_sector_kind_t* out_kind);

/// @brief Locate one complete sector's payload, using the sector's own form.
///
/// Reads submode bit 5 of `sector`: Form 1 yields PSX_CDROM_FORM1_DATA_SIZE
/// (2048) bytes, Form 2 yields PSX_CDROM_FORM2_DATA_SIZE (2328), both starting
/// at byte 24.
///
/// @note Where user data starts, and why 24 rather than 12. Three independent
///       places in the reference put the data boundary at byte 24:
///         - `IsoReader.readSectors(...)` copies payload from index 24 of the
///           2352-byte buffer, 2048 bytes for Form 1 and 2328 for Form 2.
///         - `XaAdpcm` walks 18 x 128-byte portions inside 0x914 = 2324 bytes
///           starting at 24, which is that Form 2 region minus its 4-byte EDC.
///         - `Unpacker` seeks to `sector * 2352 + 12` and then advances 12
///           more bytes before reading a volume descriptor, and expects
///           descriptor type 1 as the first byte it reads. That is byte 24.
///       `seekSector`'s lone 12 is a sync-skip, not a competing data offset:
///       its callers move past the remaining header themselves. There is no
///      "12 versus 24" discrepancy to reconcile, and an implementation that
///       started payloads at 12 would read twelve header bytes as data.
///
/// @param sector      One raw sector (borrowed). Must be a whole
///                    PSX_CDROM_SECTOR_SIZE bytes: a short span is reported as
///                    truncation rather than answered with a short payload, so
///                    a truncated image cannot be mistaken for a valid sector.
/// @param out_payload Receives the payload view, borrowing `sector`.
///                    Unmodified on failure.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null `out_payload`, null
///         `sector.data` with non-zero size, or a `sector.size` larger than a
///         whole sector that still leaves no room for a full payload), or
///         PSX_ERROR_TRUNCATED (span shorter than one sector, or too short for
///         the payload its own form promises).
/// @note Thread: no state — thread-safe.
PSX_API psx_error_t psx_sector_payload(psx_bytes_t sector, psx_bytes_t* out_payload);

/// @brief Describe a run of consecutive sectors inside a larger span.
///
/// The reference walks FMV frames and extracts files by stepping over whole
/// 2352-byte sectors; this is the bounds-checked form of that arithmetic.
///
/// @param image        The span to index into (whole image, or one container's
///                     concatenated sectors).
/// @param first_sector Index of the first sector; 0 is the span's first sector.
/// @param count      Number of consecutive sectors wanted. Must be > 0.
/// @param out_extent Receives the covering range: offset
///                   `first_sector * PSX_CDROM_SECTOR_SIZE`, length
///                   `count * PSX_CDROM_SECTOR_SIZE`. Use psx_sector_payload()
///                   per sector to get form-correct payload out of it.
/// @return PSX_OK, PSX_ERROR_INVALID_ARGUMENT (null `image.data` with non-zero
///         size, null `out_extent`, `count` == 0, or arithmetic that overflows
///         the range computation), or PSX_ERROR_TRUNCATED (the run does not fit
///         inside `image`, including a `first_sector` already past the end).
/// @note Thread: no state — thread-safe.
PSX_API psx_error_t psx_sector_run_extent(psx_bytes_t image, size_t first_sector, size_t count,
                                          psx_sector_extent_t* out_extent);

/// @brief Sector count the reference would compute for a file of `length`
///        payload bytes: `ceil(length / 2048)`.
///
/// @note Quirk reproduced deliberately. The reference applies this 2048-byte
///       divisor even when it then reads 2352 bytes per sector, so a raw
///       transfer of a `length`-byte file reads `ceil(length/2048) * 2352`
///       bytes (IsoReader.readSectors: `sectorCount` sized from 0x7ff,
///       `dataRead += sectorSize`). Keeping the function honest about that is
///       what lets a caller tell "matches the reference" from "matches the
///       hardware".
///
/// @param length Payload bytes to cover.
/// @return Number of sectors (0 for length 0).
/// @note Thread: pure function, no state — thread-safe.
static inline size_t psx_sector_count_for_bytes(size_t length)
{
  return (length + (PSX_CDROM_FORM1_DATA_SIZE - 1u)) / PSX_CDROM_FORM1_DATA_SIZE;
}

/* --- Submode / coding field accessors ------------------------------------- */
/*
 * These take the header **by value**: they are bit tests with no failure mode,
 * so there is no pointer to null-check and nothing to leak. Each one is the
 * direct translation of the reference accessor named in its comment.
 */

/// @brief Submode bit 7 — end of sequence (SectorHeader.Submode.isEof).
/// @param header Parsed header.
/// @return Non-zero when the sector is flagged EOF.
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_sector_header_is_eof(psx_sector_header_t header)
{
  return (header.submode & PSX_CDROM_SUBMODE_EOF) != 0u;
}

/// @brief Submode bit 6 — real-time stream (Submode.isRealtime).
/// @param header Parsed header.
/// @return Non-zero when the sector belongs to a real-time transfer.
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_sector_header_is_realtime(psx_sector_header_t header)
{
  return (header.submode & PSX_CDROM_SUBMODE_REALTIME) != 0u;
}

/// @brief Submode bit 5 — sector form (Submode.getForm, which indexes an enum
///        with the masked bit).
/// @param header Parsed header.
/// @return PSX_SECTOR_FORM_1 or PSX_SECTOR_FORM_2. Never any other value,
///         because the bit is masked rather than shifted.
/// @note Thread: pure function, no state — thread-safe.
static inline psx_sector_form_t psx_sector_header_form(psx_sector_header_t header)
{
  return ((header.submode >> 5) & 1u) != 0u ? PSX_SECTOR_FORM_2 : PSX_SECTOR_FORM_1;
}

/// @brief Submode bit 4 — transfer trigger (Submode.isTrigger).
/// @param header Parsed header.
/// @return Non-zero when the trigger bit is set.
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_sector_header_is_trigger(psx_sector_header_t header)
{
  return (header.submode & PSX_CDROM_SUBMODE_TRIGGER) != 0u;
}

/// @brief Submode bit 0 — end of audio (Submode.isEndAudio).
/// @param header Parsed header.
/// @return Non-zero when the audio sequence ends with this sector.
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_sector_header_is_end_audio(psx_sector_header_t header)
{
  return (header.submode & PSX_CDROM_SUBMODE_END_AUDIO) != 0u;
}

/// @brief Coding bits 4 — sample width (SectorHeader.Coding.getBitsPerSample).
/// @param header Parsed header.
/// @return 8 or 4.
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_sector_header_coding_bits(psx_sector_header_t header)
{
  return (header.coding & PSX_CDROM_CODING_8BIT) != 0u ? 8 : 4;
}

/// @brief Coding bit 2 — sample rate flag (Coding.getSampleRate).
/// @param header Parsed header.
/// @return 18900 or 37800.
/// @note Informational only in this build: there is no resampler, so a 18900
///       flag is reported, not corrected (see psx/xa.h and
///       docs/research/psx-formats.md §6).
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_sector_header_coding_rate(psx_sector_header_t header)
{
  return (header.coding & PSX_CDROM_CODING_HALF_RATE) != 0u ? 18900 : 37800;
}

/// @brief Coding bit 0 — stereo (Coding.isStereo).
/// @param header Parsed header.
/// @return Non-zero when the sector is flagged stereo.
/// @note Thread: pure function, no state — thread-safe.
static inline int psx_sector_header_coding_is_stereo(psx_sector_header_t header)
{
  return (header.coding & PSX_CDROM_CODING_STEREO) != 0u;
}

#ifdef __cplusplus
} /* extern "C" */
#endif