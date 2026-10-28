#pragma once
/// @file tim.h
/// @brief TIM (PS1 texture image) parsing — C17 API (psx-library.md §2
/// `psx::tim`, docs/research/psx-formats.md §4).
///
/// Layout (all fields little-endian), cross-checked against an authentic TIM
/// asset. Provenance, the measured field-by-field evidence and this port's
/// deviations are recorded in docs/research/psx-formats.md §4.
///
/// ```text
/// 0x0  u32        id, 0x10 for every TIM
/// 0x4  u32        flags: colour depth = flags & 7 (see psx_tim_bpp_t), CLUT present = flags & 8
/// 0x8  u32        CLUT section size   [only when the CLUT flag is set]
/// 0xc  i16[4]     CLUT rect x, y, w, h
/// 0x14 CLUT data  (sectionSize - 12 bytes)
///      ...        image section, at 0x8 + clutSectionSize (or 0x8 without a CLUT)
///         u32     image section size
///         i16[4]  image rect x, y, w, h
///         u8[]    image data (sectionSize - 12 bytes)
/// ```
///
/// Rects are **signed** i16. Bytes after the declared image section are reported
/// as `trailing_bytes` rather than rejected: real assets carry padding there.
///
/// Memory contract: parsing allocates nothing. psx_tim_image_t borrows the
/// caller's buffer, so it must not outlive it.

#include "psx/bytes.h"
#include "psx/error.h"
#include "psx/export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief TIM id/magic word at offset 0x0.
#define PSX_TIM_MAGIC 0x10u

/// @brief Section header length: u32 size + i16[4] rect.
#define PSX_TIM_SECTION_HEADER_BYTES 0xcu

/// @brief Colour depth selector — the low three bits of the TIM flags, selected
/// by `flags & 7`.
///
/// Stored in psx_tim_image_t as `int` (stable width across the ABI); the
/// enumerators are the on-disc codes.
typedef int psx_tim_bpp_t;

/// @brief Colour-depth codes (values are on-disc encodings, not arbitrary).
enum
{
  PSX_TIM_BPP_4 = 0,  ///< 4 bits per pixel (16 colours).
  PSX_TIM_BPP_8 = 1,  ///< 8 bits per pixel (256 colours).
  PSX_TIM_BPP_15 = 2, ///< 15 bits per pixel (direct colour).
  PSX_TIM_BPP_24 = 3  ///< 24 bits per pixel (declared by the reference enum).
};

/// @brief A TIM rectangle: signed 16-bit x, y, w, h.
typedef struct psx_tim_rect
{
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
} psx_tim_rect_t;

/// @brief Parsed view of a TIM image — a plain data result the caller allocates.
///
/// @note Lifetime: `clut` and `image_data` **borrow** the buffer handed to
///       psx_tim_parse(); they do not own pixels (a TIM image section is commonly
///       several kilobytes, and copying it serves no purpose here). The view must
///       not outlive that buffer, and the buffer must not be modified while the
///       view is used.
/// @note ABI: this struct's layout and size are part of PSX_ABI_VERSION 1 —
///       callers allocate it. psx_tim_image_init() is the only supported way to
///       obtain a zeroed instance.
typedef struct psx_tim_image
{
  uint32_t flags;              ///< Raw flag word at 0x4 (extra bits preserved, not interpreted).
  psx_tim_bpp_t color_depth;   ///< Colour depth selected by flags & 7.
  int has_clut;                ///< Non-zero when a CLUT section is present (flags & 8).
  psx_tim_rect_t clut_rect;    ///< CLUT rect (valid when has_clut).
  psx_bytes_t clut;            ///< CLUT bytes (borrowed; valid when has_clut).
  psx_tim_rect_t image_rect;   ///< Image rect.
  psx_bytes_t image_data;      ///< Image section payload bytes (borrowed).
  size_t image_section_offset; ///< Offset of the image section header.
  size_t trailing_bytes;       ///< Bytes after the declared image section.
} psx_tim_image_t;

/// @brief Zero a psx_tim_image_t before passing it to psx_tim_parse().
/// @param image Struct to initialise; no-op for a NULL pointer.
/// @note Thread: pure function, no shared state — thread-safe.
PSX_API void psx_tim_image_init(psx_tim_image_t* image);

/// @brief Cheap content sniff: at least 8 bytes and the id at 0x0 is 0x10.
/// @param src Candidate image (borrowed).
/// @return 1 when this looks like a TIM, 0 otherwise. Never reports an error.
/// @note Thread: pure function, no state — thread-safe.
PSX_API int psx_tim_is_tim(psx_bytes_t src);

/// @brief Parse and validate a TIM image.
///
/// Validation goes beyond the reference reader (which trusts every field): the
/// depth code must be one of the four known values, each section header and its
/// data must lie inside the buffer, section sizes must cover their own header,
/// rects must be positive, and the declared section must be large enough for the
/// texels its rect describes (4/8/15 bpp: `w * 2 * h` bytes, since a 16-bit texel
/// is 2 bytes at every indexed depth; a CLUT must cover `w * h` 16-bit entries).
/// The 24 bpp extent rule is not verified in the reference, so it is not enforced
/// here either.
///
/// @param src        TIM bytes (borrowed; the result borrows from them).
/// @param out_image  Receives the parsed view. Zeroed and left untouched on
///                   failure; must not be NULL.
/// @return PSX_OK, or:
///         PSX_ERROR_INVALID_ARGUMENT (out_image is NULL, or src.data is NULL
///         with a non-zero size),
///         PSX_ERROR_TRUNCATED (buffer too small for the fixed header),
///         PSX_ERROR_TIM_MAGIC (id at 0x0 is not 0x10),
///         PSX_ERROR_TIM_BPP (depth code out of range),
///         PSX_ERROR_TIM_NO_IMAGES (the buffer ends before a required section
///         header/rect exists),
///         PSX_ERROR_TIM_SECTION_SIZE (section size smaller than its own header),
///         PSX_ERROR_TIM_SECTION_OUT_OF_RANGE (the section header fits but its
///         declared payload runs past the buffer end),
///         PSX_ERROR_TIM_RECT_EXTENT (non-positive rect, an empty CLUT, a CLUT
///         payload smaller than its rect, or a rect needing more bytes than the
///         section declares).
/// @note Deviation from the original reader, which takes the CLUT section size as
///       a u16 when locating the image section and thereby truncates a section
///       above 0xffff: the full u32 is used here
///       (docs/research/psx-formats.md §4).
/// @note Thread: pure function, no state — thread-safe.
PSX_API psx_error_t psx_tim_parse(psx_bytes_t src, psx_tim_image_t* out_image);

/// @brief Number of palette entries a CLUT rect describes (w * h).
/// @param rect CLUT rect.
/// @return Entry count, or 0 when the rect is not positive in both axes.
/// @note Thread: pure function, no state — thread-safe.
PSX_API size_t psx_tim_clut_entry_count(psx_tim_rect_t rect);

#ifdef __cplusplus
} /* extern "C" */
#endif
