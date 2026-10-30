#pragma once
/// @file cdrom.hpp
/// @brief C++23 wrapper over the CD-ROM XA sector layer (psx-c-api.md §7).
///
/// Thin by contract: span/error translation only. The geometry rules, the
/// form-derived payload size, and the bounds checks live once, in the C core
/// (psx/src/cdrom.c) — which is what lets the C17 consumer test the same
/// behaviour without C++.
///
/// The submode/coding helpers in psx/cdrom.h (`psx_sector_header_is_eof`,
/// `psx_sector_header_form`, …) are `static inline` and take the header by
/// value, so C++ calls them directly. They are deliberately not re-declared
/// here: a second spelling of the same bit test is a second thing to get wrong.
///
/// @see psx/cdrom.h for the provenance of every offset and for the two
/// measurements this layer is still waiting on before the ISO 9660 half exists.

#include "psx/cdrom.h"
#include "psx/cpp/bytes.hpp"
#include "psx/cpp/error.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace psx::cdrom
{

/// @brief Raw sector size in bytes (2352).
inline constexpr std::size_t SectorSize = PSX_CDROM_SECTOR_SIZE;

/// @brief Bytes of sync + header + subheader before user data (24).
inline constexpr std::size_t HeaderSize = PSX_CDROM_HEADER_SIZE;

/// @brief Payload size of a Form 1 sector (2048).
inline constexpr std::size_t Form1DataSize = PSX_CDROM_FORM1_DATA_SIZE;

/// @brief Payload size of a Form 2 sector (2328).
inline constexpr std::size_t Form2DataSize = PSX_CDROM_FORM2_DATA_SIZE;

/// @brief What a sector's header says, and the raw bytes it came from.
using Header = psx_sector_header_t;

/// @brief VIDEO / AUDIO / DATA, resolved from submode bits 1..3.
using Kind = psx_sector_kind_t;

/// @brief A byte range inside a larger span, as positions rather than pointers.
///
/// @note Lifetime: describes a range; owns nothing. Pair it with the span it
///       was computed against.
struct Extent
{
  std::size_t Offset; ///< First byte of the range.
  std::size_t Length; ///< Bytes in the range.
};

/// @brief Read a sector's header.
///
/// @param sector Raw sector bytes (borrowed); at least HeaderSize needed.
/// @return The header, or Error::InvalidArgument / Error::Truncated.
/// @note Thread: pure function — thread-safe.
[[nodiscard]] inline std::expected<Header, Error> ParseHeader(std::span<const std::uint8_t> sector)
{
  Header header{};
  if (const auto status = psx_sector_header_parse(detail::ToBytes(sector), &header); status != PSX_OK)
    return std::unexpected(static_cast<Error>(status));
  return header;
}

/// @brief Resolve what a sector carries.
///
/// @param header A header from ParseHeader().
/// @return The kind, or Error::InvalidArgument / Error::CdromBadSectorType when
///         submode bits 1..3 are none of VIDEO, AUDIO, or DATA.
/// @note Thread: pure function — thread-safe.
[[nodiscard]] inline std::expected<Kind, Error> Classify(const Header& header)
{
  Kind kind{};
  if (const auto status = psx_sector_classify(header, &kind); status != PSX_OK)
    return std::unexpected(static_cast<Error>(status));
  return kind;
}

/// @brief Borrow one sector's payload, sized by the sector's own form bit.
///
/// @param sector Exactly one raw sector (borrowed). A span longer than one
///               sector is rejected rather than read from the front.
/// @return A span of 2048 (Form 1) or 2328 (Form 2) bytes borrowing `sector`,
///         or Error::InvalidArgument / Error::Truncated.
/// @note Thread: pure function — thread-safe.
[[nodiscard]] inline std::expected<std::span<const std::uint8_t>, Error> Payload(std::span<const std::uint8_t> sector)
{
  psx_bytes_t payload{};
  if (const auto status = psx_sector_payload(detail::ToBytes(sector), &payload); status != PSX_OK)
    return std::unexpected(static_cast<Error>(status));
  return detail::ToSpan(payload);
}

/// @brief Describe a run of consecutive sectors inside a larger span.
///
/// @param image       The span to index into (borrowed).
/// @param firstSector Index of the first sector.
/// @param count       Number of sectors, must be > 0.
/// @return The covering range, or Error::InvalidArgument (zero count, wrapping
///         arithmetic) or Error::Truncated (the run does not fit).
/// @note Thread: pure function — thread-safe.
[[nodiscard]] inline std::expected<Extent, Error> RunExtent(std::span<const std::uint8_t> image,
                                                            std::size_t firstSector, std::size_t count)
{
  psx_sector_extent_t extent{};
  if (const auto status = psx_sector_run_extent(detail::ToBytes(image), firstSector, count, &extent); status != PSX_OK)
    return std::unexpected(static_cast<Error>(status));
  return Extent{extent.offset, extent.length};
}

/// @brief Sector count for a payload length: ceil(length / 2048).
///
/// @note This is the reference's rule, including the part where it is applied to
///       raw 2352-byte transfers. See psx/cdrom.h.
/// @note Thread: pure function — thread-safe.
[[nodiscard]] constexpr std::size_t SectorCount(std::size_t length) noexcept
{
  return psx_sector_count_for_bytes(length);
}

} // namespace psx::cdrom
