/// @file tim.c
/// @brief TIM parsing in C17 (see tim.h for the verified layout and the
/// authentic-file measurements it was checked against).

#include "psx/tim.h"

#include "reader.h"

#include <stddef.h>
#include <stdint.h>

/// @brief One parsed section (CLUT or image): rect, borrowed payload, and the
/// raw size field (needed to locate the next section).
typedef struct tim_section
{
  psx_tim_rect_t rect;
  psx_bytes_t data;
  uint32_t size_field;
} tim_section_t;

/// @brief Read a signed i16 rectangle (4 x i16) at offset.
/// @param reader Reader over the whole TIM.
/// @param at     Offset of the first i16.
/// @param out    Receives the rect; unmodified on failure.
/// @return PSX_OK or PSX_ERROR_TRUNCATED (the caller maps that to
///         PSX_ERROR_TIM_NO_IMAGES, which is what a truncated section is).
static psx_error_t tim_read_rect(psx_reader_t reader, size_t at, psx_tim_rect_t* out)
{
  if (!psx_reader_fits(reader, at, 8u))
    return PSX_ERROR_TRUNCATED;

  int16_t values[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; ++i)
  {
    const psx_error_t error = psx_reader_i16(reader, at + (size_t)i * 2u, &values[i]);
    if (error != PSX_OK)
      return error;
  }

  out->x = values[0];
  out->y = values[1];
  out->w = values[2];
  out->h = values[3];
  return PSX_OK;
}

void psx_tim_image_init(psx_tim_image_t* image)
{
  if (image == NULL)
    return;
  image->flags = 0u;
  image->color_depth = PSX_TIM_BPP_4;
  image->has_clut = 0;
  image->clut_rect.x = 0;
  image->clut_rect.y = 0;
  image->clut_rect.w = 0;
  image->clut_rect.h = 0;
  image->clut.data = NULL;
  image->clut.size = 0u;
  image->image_rect.x = 0;
  image->image_rect.y = 0;
  image->image_rect.w = 0;
  image->image_rect.h = 0;
  image->image_data.data = NULL;
  image->image_data.size = 0u;
  image->image_section_offset = 0u;
  image->trailing_bytes = 0u;
}

int psx_tim_is_tim(psx_bytes_t src)
{
  const psx_reader_t reader = psx_reader_from(src);
  if (!psx_reader_fits(reader, 0u, 8u))
    return 0;
  uint32_t id = 0u;
  if (psx_reader_u32(reader, 0u, &id) != PSX_OK)
    return 0;
  return id == PSX_TIM_MAGIC;
}

size_t psx_tim_clut_entry_count(psx_tim_rect_t rect)
{
  if (rect.w <= 0 || rect.h <= 0)
    return 0u;
  /* Width is widened through uint16_t to keep the multiplication unsigned; a rect
     that is positive in i16 cannot exceed that range. */
  return (size_t)(uint16_t)rect.w * (size_t)(uint16_t)rect.h;
}

/// @brief Bytes a rect's texel grid needs at the given depth, or 0 when the depth
/// has no verified rule (reported through out_known).
///
/// 4/8 bpp store indices in 16-bit texels and 15 bpp stores one colour per 16-bit
/// texel, so all three need `w * 2 * h` bytes. 24 bpp has no verified reference
/// rule here, so it reports "unknown" and is left unchecked.
/// @param rect       Rect to size.
/// @param depth      Colour depth code.
/// @param out_known  Receives non-zero when a rule applies.
/// @return Required byte count, or 0 when unknown / the rect is not positive.
static size_t tim_required_image_bytes(psx_tim_rect_t rect, psx_tim_bpp_t depth, int* out_known)
{
  *out_known = depth != PSX_TIM_BPP_24;
  if (!*out_known || rect.w <= 0 || rect.h <= 0)
    return 0u;

  const size_t w = (size_t)(uint16_t)rect.w;
  const size_t h = (size_t)(uint16_t)rect.h;
  /* Overflow guard: TIM rects are 16-bit, so w*h*2 cannot realistically overflow,
     but the check keeps the reasoning explicit and holds on a 32-bit size_t too. */
  if (h != 0u && w > (0xFFFFFFFFu / 2u) / h)
    return 0xFFFFFFFFu;
  return w * 2u * h;
}

/// @brief Parse one section (CLUT or image): size field, rect, payload bounds.
/// @param reader           Reader over the whole TIM.
/// @param at               Offset of the section's size field.
/// @param depth            Colour depth (image sections only; a CLUT is always 16-bit).
/// @param check_rect_extent Non-zero to require the payload to cover the rect.
/// @param out              Receives the section; unmodified on failure.
/// @return PSX_OK or one of the TIM error codes.
static psx_error_t tim_read_section(psx_reader_t reader, size_t at, psx_tim_bpp_t depth, int check_rect_extent,
                                    tim_section_t* out)
{
  uint32_t size_field = 0u;
  if (psx_reader_u32(reader, at, &size_field) != PSX_OK)
  {
    /* Nothing to read at all: the buffer ends before the section header. */
    return PSX_ERROR_TIM_NO_IMAGES;
  }
  if (size_field < PSX_TIM_SECTION_HEADER_BYTES)
    return PSX_ERROR_TIM_SECTION_SIZE;

  psx_tim_rect_t rect;
  if (tim_read_rect(reader, at + 4u, &rect) != PSX_OK)
    return PSX_ERROR_TIM_NO_IMAGES;
  if (rect.w <= 0 || rect.h <= 0)
    return PSX_ERROR_TIM_RECT_EXTENT;

  const size_t payload_length = (size_t)size_field - PSX_TIM_SECTION_HEADER_BYTES;
  const size_t payload_offset = at + PSX_TIM_SECTION_HEADER_BYTES;
  if (!psx_reader_fits(reader, payload_offset, payload_length))
    return PSX_ERROR_TIM_SECTION_OUT_OF_RANGE;

  if (check_rect_extent)
  {
    int known = 0;
    const size_t required = tim_required_image_bytes(rect, depth, &known);
    if (known && required > payload_length)
      return PSX_ERROR_TIM_RECT_EXTENT;
  }

  out->rect = rect;
  out->data.data = reader.data + payload_offset;
  out->data.size = payload_length;
  out->size_field = size_field;
  return PSX_OK;
}

psx_error_t psx_tim_parse(psx_bytes_t src, psx_tim_image_t* out_image)
{
  if (out_image == NULL || !psx_bytes_is_valid(src))
    return PSX_ERROR_INVALID_ARGUMENT;
  psx_tim_image_init(out_image);

  const psx_reader_t reader = psx_reader_from(src);
  if (!psx_reader_fits(reader, 0u, 8u))
    return PSX_ERROR_TRUNCATED;

  uint32_t id = 0u;
  psx_error_t error = psx_reader_u32(reader, 0u, &id);
  if (error != PSX_OK)
    return error;
  if (id != PSX_TIM_MAGIC)
    return PSX_ERROR_TIM_MAGIC;

  uint32_t flags = 0u;
  error = psx_reader_u32(reader, 4u, &flags);
  if (error != PSX_OK)
    return error;

  psx_tim_image_t image;
  psx_tim_image_init(&image);
  image.flags = flags;

  const uint32_t depth_code = flags & 0x7u;
  switch (depth_code)
  {
    case 0u:
      image.color_depth = PSX_TIM_BPP_4;
      break;
    case 1u:
      image.color_depth = PSX_TIM_BPP_8;
      break;
    case 2u:
      image.color_depth = PSX_TIM_BPP_15;
      break;
    case 3u:
      image.color_depth = PSX_TIM_BPP_24;
      break;
    default:
      return PSX_ERROR_TIM_BPP;
  }

  image.has_clut = (flags & 0x8u) != 0u;
  size_t image_offset = 8u;

  if (image.has_clut)
  {
    tim_section_t clut;
    error = tim_read_section(reader, 8u, PSX_TIM_BPP_15, 0, &clut);
    if (error != PSX_OK)
      return error;
    /* A CLUT is a grid of 16-bit entries: the declared payload must cover it. */
    const size_t entries = psx_tim_clut_entry_count(clut.rect);
    if (entries == 0u || entries * 2u > clut.data.size)
      return PSX_ERROR_TIM_RECT_EXTENT;
    image.clut_rect = clut.rect;
    image.clut = clut.data;
    image_offset = 8u + (size_t)clut.size_field;
  }

  tim_section_t section;
  error = tim_read_section(reader, image_offset, image.color_depth, 1, &section);
  if (error != PSX_OK)
    return error;
  image.image_rect = section.rect;
  image.image_data = section.data;
  image.image_section_offset = image_offset;

  const size_t consumed = image_offset + (size_t)section.size_field;
  image.trailing_bytes = reader.size > consumed ? reader.size - consumed : 0u;

  *out_image = image;
  return PSX_OK;
}
