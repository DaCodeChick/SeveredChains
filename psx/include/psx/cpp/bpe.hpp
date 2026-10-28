#pragma once
/// @file bpe.hpp
/// @brief C++23 wrapper over the C BPE codec (psx-c-api.md §7).
///
/// Thin by contract: every function converts its arguments, calls the C core, and
/// translates the result. No parsing or decoding logic lives here — a second
/// implementation would be a second set of bugs.
///
/// @see psx/bpe.h for the format layout, error semantics and deviations.

#include "psx/bpe.h"
#include "psx/cpp/bytes.hpp"
#include "psx/cpp/error.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace psx::bpe
{

/// @brief Segment magic, stored as a u32 at offset 0x4.
inline constexpr std::uint32_t Magic = PSX_BPE_MAGIC;

/// @brief Largest number of decompressed bytes one block may declare; a larger
/// block size is rejected as malformed.
inline constexpr std::size_t MaxBlockSize = PSX_BPE_MAX_BLOCK_SIZE;

/// @brief Default ceiling on the declared output size. The header's size field is
/// untrusted, so allocation is bounded before anything is reserved; the engine
/// passes its own resource cap.
inline constexpr std::size_t DefaultMaxOutputBytes = PSX_BPE_DEFAULT_MAX_OUTPUT_BYTES;

/// @brief Cheap content sniff: at least 8 bytes and the magic at 0x4 matches.
/// @param data Candidate segment.
/// @return Whether this looks like a BPE segment.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline bool IsBPE(std::span<const std::uint8_t> data) noexcept
{
  return psx_bpe_is_bpe(detail::ToBytes(data)) != 0;
}

/// @brief Read the declared decompressed size (u32 at 0x0).
/// @param data Candidate segment.
/// @return The declared byte count, or Error::Truncated / Error::BPEMagic.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline Result<std::size_t> DeclaredSize(std::span<const std::uint8_t> data) noexcept
{
  std::size_t declared = 0;
  if (const psx_error_t code = psx_bpe_declared_size(detail::ToBytes(data), &declared); code != PSX_OK)
    return std::unexpected(static_cast<Error>(code));
  return declared;
}

/// @brief Decompress a BPE segment.
///
/// The ceiling is enforced before the destination is allocated (as before), so a
/// hostile header cannot cause a large allocation; the C core then sees a
/// destination that is exactly the size it expects.
///
/// @param data           Compressed segment (not owned).
/// @param maxOutputBytes Ceiling for the declared output size.
/// @return The decompressed bytes, or Error::Truncated, Error::BPEMagic,
///         Error::BPEOutputTooLarge, Error::BPEBlockSize,
///         Error::BPEDictionaryOverflow, Error::BPEExpansionOverflow,
///         Error::BPESizeMismatch (see psx/bpe.h for which condition yields which).
/// @note Throws nothing; a failing std::vector allocation is not a recoverable
///       path (project policy: no exceptions for control flow).
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline Result<std::vector<std::uint8_t>>
DecompressBPE(std::span<const std::uint8_t> data, std::size_t maxOutputBytes = DefaultMaxOutputBytes) noexcept
{
  auto declared = DeclaredSize(data);
  if (!declared)
    return std::unexpected(declared.error());
  if (*declared > maxOutputBytes)
    return std::unexpected(Error::BPEOutputTooLarge);

  std::vector<std::uint8_t> out(*declared);
  std::size_t written = 0;
  if (const psx_error_t code =
        psx_bpe_decompress(detail::ToBytes(data), detail::ToWritable(std::span<std::uint8_t>(out)), &written);
      code != PSX_OK)
  {
    return std::unexpected(static_cast<Error>(code));
  }
  out.resize(written);
  return out;
}

/// @brief Emit a valid BPE segment for raw bytes using an identity dictionary.
/// @param raw Bytes to wrap.
/// @return A well-formed BPE segment, or Error::BPEOutputTooLarge when raw is
///         larger than the u32 header can describe.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline Result<std::vector<std::uint8_t>> CompressBPE(std::span<const std::uint8_t> raw) noexcept
{
  std::size_t required = 0;
  if (const psx_error_t code = psx_bpe_compressed_size(raw.size(), &required); code != PSX_OK)
    return std::unexpected(static_cast<Error>(code));

  std::vector<std::uint8_t> out(required);
  std::size_t written = 0;
  if (const psx_error_t code =
        psx_bpe_compress(detail::ToBytes(raw), detail::ToWritable(std::span<std::uint8_t>(out)), &written);
      code != PSX_OK)
  {
    return std::unexpected(static_cast<Error>(code));
  }
  out.resize(written);
  return out;
}

} // namespace psx::bpe
