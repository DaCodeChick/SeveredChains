#pragma once
/// @file tim.hpp
/// @brief C++23 wrapper over the C TIM parser (psx-c-api.md §7).
///
/// Thin by contract: conversions and error translation only — the validation rules
/// live once, in the C core.
///
/// @see psx/tim.h for the format layout, validation rules and deviations.

#include "psx/cpp/bytes.hpp"
#include "psx/cpp/error.hpp"
#include "psx/tim.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psx::tim
{

/// @brief TIM id/magic word at offset 0x0.
inline constexpr std::uint32_t Magic = PSX_TIM_MAGIC;

/// @brief Section header length: u32 size + i16[4] rect.
inline constexpr std::size_t SectionHeaderBytes = PSX_TIM_SECTION_HEADER_BYTES;

/// @brief Colour depth selector, the low three bits of the TIM flags: the four
/// depths in declaration order, selected by `flags & 7`.
enum class BPP : std::uint8_t
{
  Four = 0,       ///< 4 bits per pixel (16 colours).
  Eight = 1,      ///< 8 bits per pixel (256 colours).
  Fifteen = 2,    ///< 15 bits per pixel (direct colour).
  TwentyFour = 3, ///< 24 bits per pixel (declared by the reference enum).
};

/// @brief A TIM rectangle: signed 16-bit x, y, w, h.
///
/// Public data is deliberate (cpp.md §6.2): a field-for-field translation of
/// `psx_tim_rect_t`, and the validation that decides whether these four numbers make
/// sense already happened in the C core before a Rect was ever produced.
struct Rect
{
  std::int16_t x = 0;
  std::int16_t y = 0;
  std::int16_t w = 0;
  std::int16_t h = 0;
};

/// @brief Parsed view of a TIM image.
///
/// @note Lifetime: `clut` and `imageData` **borrow** the buffer handed to
///       ParseTIM(); they do not own pixels (a TIM image section is commonly
///       several kilobytes, and copying it serves no purpose here). The view must
///       not outlive that buffer.
///
/// Public data is deliberate (cpp.md §6.2): this is `psx_tim_image_t` expressed in C++
/// types, produced once by ParseTIM after the core has validated every extent in it.
/// There is no mutator to route around and nothing to reconstruct — an encapsulated
/// version would be nine accessors over nine facts about a buffer the caller already
/// holds, and would stop matching the C struct it mirrors.
struct Image
{
  std::uint32_t flags = 0;                 ///< Raw flag word at 0x4 (extra bits preserved, not interpreted).
  BPP colorDepth = BPP::Four;              ///< Colour depth selected by flags & 7.
  bool hasCLUT = false;                    ///< Whether a CLUT section is present (flags & 8).
  Rect clutRect{};                         ///< CLUT rect (valid when hasCLUT).
  std::span<const std::uint8_t> clut;      ///< CLUT bytes (borrowed; valid when hasCLUT).
  Rect imageRect{};                        ///< Image rect.
  std::span<const std::uint8_t> imageData; ///< Image section payload bytes (borrowed).
  std::size_t imageSectionOffset = 0;      ///< Offset of the image section header.
  std::size_t trailingBytes = 0;           ///< Bytes after the declared image section.
};

namespace detail
{

/* Inside a nested `detail`, unqualified lookup would stop here and never reach the
   shared span helpers, so they are re-exported rather than repeated. */
using ::psx::detail::ToBytes;
using ::psx::detail::ToSpan;
using ::psx::detail::ToWritable;

/// @brief Convert a C rect to the wrapper type.
/// @param Source C rect.
/// @return The equivalent Rect.
[[nodiscard]] constexpr Rect FromC(psx_tim_rect_t source) noexcept
{
  return Rect{source.x, source.y, source.w, source.h};
}

/// @brief Convert the wrapper rect to the C type.
/// @param Source Rect to convert.
/// @return The equivalent psx_tim_rect_t.
[[nodiscard]] constexpr psx_tim_rect_t ToC(Rect source) noexcept
{
  return psx_tim_rect_t{source.x, source.y, source.w, source.h};
}

} // namespace detail

/// @brief Cheap content sniff: at least 12 bytes and the id at 0x0 is 0x10.
/// @param data Candidate image.
/// @return Whether this looks like a TIM.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline bool IsTIM(std::span<const std::uint8_t> data) noexcept
{
  return psx_tim_is_tim(detail::ToBytes(data)) != 0;
}

/// @brief Parse and validate a TIM image.
/// @param data TIM bytes (not owned; the result borrows from it).
/// @return The parsed image, or Error::Truncated, Error::TIMMagic, Error::TIMBPP,
///         Error::TIMNoImages, Error::TIMSectionSize,
///         Error::TIMSectionOutOfRange, Error::TIMRectExtent (psx/tim.h states the
///         exact condition behind each).
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline Result<Image> ParseTIM(std::span<const std::uint8_t> data) noexcept
{
  psx_tim_image_t parsed;
  if (const psx_error_t code = psx_tim_parse(detail::ToBytes(data), &parsed); code != PSX_OK)
    return std::unexpected(static_cast<Error>(code));

  Image image;
  image.flags = parsed.flags;
  image.colorDepth = static_cast<BPP>(parsed.color_depth);
  image.hasCLUT = parsed.has_clut != 0;
  image.clutRect = detail::FromC(parsed.clut_rect);
  image.clut = detail::ToSpan(parsed.clut);
  image.imageRect = detail::FromC(parsed.image_rect);
  image.imageData = detail::ToSpan(parsed.image_data);
  image.imageSectionOffset = parsed.image_section_offset;
  image.trailingBytes = parsed.trailing_bytes;
  return image;
}

/// @brief Number of palette entries a CLUT rect describes (w * h).
/// @param rect CLUT rect.
/// @return Entry count, or 0 when the rect is not positive in both axes.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline std::size_t CLUTEntryCount(Rect rect) noexcept
{
  return psx_tim_clut_entry_count(detail::ToC(rect));
}

} // namespace psx::tim
