#pragma once
/// @file psx.hpp
/// @brief Umbrella for the optional C++23 wrappers over the C17 `psx` core
/// (psx-library.md §2.1, psx-c-api.md §7).
///
/// Include this from C++ instead of the C headers when you want the
/// `psx::bpe` / `psx::tim` / `psx::archive` / `psx::xa` surface with `std::span`
/// in and `std::expected<T, psx::Error>` out. The wrappers are header-only and
/// contain no decoding logic: everything routes into the C shared library.
///
/// @code
/// #include "psx/cpp/psx.hpp"
///
/// if (auto image = psx::tim::ParseTIM(bytes))
/// {
///     std::println("TIM {}x{} at {:#x} depth {}", image->imageRect.w, image->imageRect.h,
///                  static_cast<unsigned>(image->flags), static_cast<int>(image->colorDepth));
/// }
/// else
/// {
///     std::println("rejected: {} ({})", psx::ErrorName(image.error()), psx::ErrorMessage(image.error()));
/// }
/// @endcode

#include "psx/cpp/archive.hpp"
#include "psx/cpp/bpe.hpp"
#include "psx/cpp/bytes.hpp"
#include "psx/cpp/error.hpp"
#include "psx/cpp/tim.hpp"
#include "psx/cpp/xa.hpp"
#include "psx/version.h"
