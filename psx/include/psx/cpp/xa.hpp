#pragma once
/// @file xa.hpp
/// @brief C++23 wrapper over the C XA-ADPCM decoder (psx-c-api.md §7).
///
/// Thin by contract: span/error translation only — the decode arithmetic and its
/// validation live once, in the C core.
///
/// @see psx/xa.h for the sector layout, the reference-fidelity notes, and the
/// open question about how the reference's block layout differs from the
/// documented CD-ROM XA block.

#include "psx/cpp/bytes.hpp"
#include "psx/cpp/error.hpp"
#include "psx/xa.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace psx::xa
{

/// @brief Interleaved 16-bit sample ceiling for one sector (mono).
inline constexpr std::size_t MaxSamples = PSX_XA_MAX_SAMPLES;

/// @brief Byte offset of the coding-info byte inside a raw sector.
inline constexpr std::size_t CodingInfoOffset = PSX_XA_CODING_INFO_OFFSET;

/// @brief Predictor history for one audio stream.
///
/// Copy-constructible and copy-assignable by design: copying a Stream forks the
/// filter state, which is what a scratch decode (probe, transcoder, golden
/// regeneration) needs without disturbing the live stream.
using Stream = psx_xa_stream_t;

/// @brief Read the coding byte of a raw sector.
///
/// @param sector Raw sector bytes (borrowed).
/// @return The coding byte, or Error::Truncated when the sector is too short to
///         carry one.
/// @note Thread: pure function — thread-safe.
[[nodiscard]] inline std::expected<std::uint8_t, Error> CodingInfo(std::span<const std::uint8_t> sector)
{
  std::uint8_t info = 0u;
  if (const psx_error_t code = psx_xa_coding_info(detail::ToBytes(sector), &info); code != PSX_OK)
    return std::unexpected(static_cast<Error>(code));
  return info;
}

/// @brief Interleaved sample count a coding byte implies (4032 stereo, 8064 mono).
///
/// @param codingInfo The sector's coding byte.
/// @return The count, or Error::XAUnsupportedCoding for 8-bit coding, which has
///         no decoder here.
/// @note Thread: pure function — thread-safe.
[[nodiscard]] inline std::expected<std::size_t, Error> DecodedSamples(std::uint8_t codingInfo)
{
  std::size_t samples = 0u;
  if (const psx_error_t code = psx_xa_decoded_samples(codingInfo, &samples); code != PSX_OK)
    return std::unexpected(static_cast<Error>(code));
  return samples;
}

/// @brief Decode one raw XA sector into interleaved 16-bit stereo PCM.
///
/// @param stream In/out predictor state; pass the same Stream across the sectors
///               of one track so the filter chain stays continuous.
/// @param sector Raw sector (borrowed, not retained).
/// @param codingInfo The sector's coding byte.
/// @param out Destination for interleaved samples; must hold
///            DecodedSamples(codingInfo) * 2 bytes.
/// @return The number of samples written, or a psx::Error (see psx/xa.h).
/// @note Thread: mutates `stream`, so one Stream is one thread at a time.
[[nodiscard]] inline std::expected<std::size_t, Error>
DecodeSector(Stream& stream, std::span<const std::uint8_t> sector, std::uint8_t codingInfo, std::span<std::uint8_t> out)
{
  std::size_t samples = 0u;
  const psx_error_t code =
    psx_xa_decode_sector(&stream, detail::ToBytes(sector), codingInfo, detail::ToWritable(out), &samples);
  if (code != PSX_OK)
    return std::unexpected(static_cast<Error>(code));
  return samples;
}

} // namespace psx::xa