#pragma once
/// @file bytes.hpp
/// @brief span ↔ `psx_bytes_t` conversions shared by the C++23 wrappers
/// (psx-c-api.md §3). Internal helper namespace; not part of the documented API.

#include "psx/bytes.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace psx::detail
{

/// @brief Borrow a std::span as the C view type.
/// @param Bytes Span to describe (not owned; must outlive any result derived from it).
/// @return A psx_bytes_t aliasing the same storage. An empty span keeps a non-null
///         base when the span has one, and a null base when it does not — both are
///         valid inputs to the C API.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] constexpr psx_bytes_t ToBytes(std::span<const std::uint8_t> bytes) noexcept
{
  return psx_bytes_t{bytes.data(), bytes.size()};
}

/// @brief Borrow a writable std::span as the C output type.
/// @param Bytes Destination span (not owned).
/// @return A psx_writable_bytes_t over the same storage.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] constexpr psx_writable_bytes_t ToWritable(std::span<std::uint8_t> bytes) noexcept
{
  return psx_writable_bytes_t{bytes.data(), bytes.size()};
}

/// @brief Borrow the C view back as a std::span.
/// @param Bytes A psx_bytes_t produced by the library (always borrows caller data).
/// @return A span over the same bytes; empty when data is null.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline std::span<const std::uint8_t> ToSpan(psx_bytes_t bytes) noexcept
{
  return bytes.data != nullptr ? std::span<const std::uint8_t>(bytes.data, bytes.size)
                               : std::span<const std::uint8_t>{};
}

} // namespace psx::detail
