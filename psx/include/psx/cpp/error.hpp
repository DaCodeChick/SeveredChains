#pragma once
/// @file error.hpp
/// @brief C++23 error vocabulary over the C core's error codes
/// (psx-c-api.md §4, error-handling.md §4.1).
///
/// `psx::Error` is the C++ face of `psx_error_t`; the numeric values are shared,
/// and the static assertions below prove the mapping at compile time rather than
/// trusting a hand-maintained table. Translation is a `static_cast` — there is no
/// lookup table to drift and nothing to allocate.
///
/// Exceptions never cross the C ABI, and none are thrown here: recoverable failure
/// travels as `std::expected<T, psx::Error>`.

#include "psx/error.h"

#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>

namespace psx
{

/// @brief Failure codes for every `psx` decoder — mirrors psx_error_t 1:1.
///
/// Grouped by domain so a new decoder adds codes without renumbering existing
/// ones; the numeric values are stable (test expectations, crash logs, and the C
/// ABI all cite them).
enum class Error : int
{
  // --- generic (0..49) -----------------------------------------------------
  InvalidArgument = PSX_ERROR_INVALID_ARGUMENT, ///< Caller violated a precondition.
  Truncated = PSX_ERROR_TRUNCATED,              ///< Input ended before the structure did.

  // --- BPE (50..79) --------------------------------------------------------
  BPEMagic = PSX_ERROR_BPE_MAGIC,                            ///< u32 at 0x4 is not 0x1a455042.
  BPEBlockSize = PSX_ERROR_BPE_BLOCK_SIZE,                   ///< Block over the 0x800-byte limit.
  BPEOutputTooLarge = PSX_ERROR_BPE_OUTPUT_TOO_LARGE,        ///< Declared size over the caller's cap.
  BPESizeMismatch = PSX_ERROR_BPE_SIZE_MISMATCH,             ///< Blocks disagree with the header.
  BPEDictionaryOverflow = PSX_ERROR_BPE_DICTIONARY_OVERFLOW, ///< Dictionary wrote past key 0xff.
  BPEExpansionOverflow = PSX_ERROR_BPE_EXPANSION_OVERFLOW,   ///< Expansion cannot fit the block.

  // --- TIM (80..109) -------------------------------------------------------
  TIMMagic = PSX_ERROR_TIM_MAGIC,                            ///< u32 at 0x0 is not 0x10.
  TIMBPP = PSX_ERROR_TIM_BPP,                                ///< Depth code in flags is not 0..3.
  TIMNoImages = PSX_ERROR_TIM_NO_IMAGES,                     ///< Buffer ends before a required section.
  TIMSectionSize = PSX_ERROR_TIM_SECTION_SIZE,               ///< Section shorter than its header.
  TIMRectExtent = PSX_ERROR_TIM_RECT_EXTENT,                 ///< Empty rect or payload too small.
  TIMSectionOutOfRange = PSX_ERROR_TIM_SECTION_OUT_OF_RANGE, ///< Payload runs past the buffer.

  // --- MRG (110..139) ------------------------------------------------------
  MRGMagic = PSX_ERROR_MRG_MAGIC,                        ///< u32 at 0x0 is not 0x1a47524d.
  MRGTableTruncated = PSX_ERROR_MRG_TABLE_TRUNCATED,     ///< Entry table exceeds the buffer.
  MRGEntryOutOfRange = PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE, ///< Entry lies outside the archive.
  MRGEntryMissing = PSX_ERROR_MRG_ENTRY_MISSING,         ///< Index out of range.

  // --- DEFF (140..169) -----------------------------------------------------
  DEFFMagic = PSX_ERROR_DEFF_MAGIC,                    ///< u32 at 0x0 is not 0x46464544.
  DEFFTableTruncated = PSX_ERROR_DEFF_TABLE_TRUNCATED, ///< Subfile table exceeds the buffer.
  DEFFOffsetsBad = PSX_ERROR_DEFF_OFFSETS_BAD,         ///< Subfile starts not monotonic.
  DEFFEntryMissing = PSX_ERROR_DEFF_ENTRY_MISSING,     ///< Index out of range.

  // --- XA audio (170..199) --------------------------------------------------
  XAUnsupportedCoding = PSX_ERROR_XA_UNSUPPORTED_CODING, ///< Coding byte asks for 8-bit.
  XAOutputTooLarge = PSX_ERROR_XA_OUTPUT_TOO_LARGE,      ///< Output span cannot hold a sector.

  // --- CD-ROM sector (200..229) ---------------------------------------------
  CdromBadSectorType = PSX_ERROR_CDROM_BAD_SECTOR_TYPE, ///< Submode type bits unrecognised.
};

/* The C++ enumerators must keep the C values: the wrapper translates by cast, and
   logs/tests print these names. A mismatch is a compile error, not a runtime
   surprise. */
static_assert(static_cast<int>(Error::InvalidArgument) == PSX_ERROR_INVALID_ARGUMENT);
static_assert(static_cast<int>(Error::Truncated) == PSX_ERROR_TRUNCATED);
static_assert(static_cast<int>(Error::BPEMagic) == PSX_ERROR_BPE_MAGIC);
static_assert(static_cast<int>(Error::BPEBlockSize) == PSX_ERROR_BPE_BLOCK_SIZE);
static_assert(static_cast<int>(Error::BPEOutputTooLarge) == PSX_ERROR_BPE_OUTPUT_TOO_LARGE);
static_assert(static_cast<int>(Error::BPESizeMismatch) == PSX_ERROR_BPE_SIZE_MISMATCH);
static_assert(static_cast<int>(Error::BPEDictionaryOverflow) == PSX_ERROR_BPE_DICTIONARY_OVERFLOW);
static_assert(static_cast<int>(Error::BPEExpansionOverflow) == PSX_ERROR_BPE_EXPANSION_OVERFLOW);
static_assert(static_cast<int>(Error::TIMMagic) == PSX_ERROR_TIM_MAGIC);
static_assert(static_cast<int>(Error::TIMBPP) == PSX_ERROR_TIM_BPP);
static_assert(static_cast<int>(Error::TIMNoImages) == PSX_ERROR_TIM_NO_IMAGES);
static_assert(static_cast<int>(Error::TIMSectionSize) == PSX_ERROR_TIM_SECTION_SIZE);
static_assert(static_cast<int>(Error::TIMRectExtent) == PSX_ERROR_TIM_RECT_EXTENT);
static_assert(static_cast<int>(Error::TIMSectionOutOfRange) == PSX_ERROR_TIM_SECTION_OUT_OF_RANGE);
static_assert(static_cast<int>(Error::MRGMagic) == PSX_ERROR_MRG_MAGIC);
static_assert(static_cast<int>(Error::MRGTableTruncated) == PSX_ERROR_MRG_TABLE_TRUNCATED);
static_assert(static_cast<int>(Error::MRGEntryOutOfRange) == PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE);
static_assert(static_cast<int>(Error::MRGEntryMissing) == PSX_ERROR_MRG_ENTRY_MISSING);
static_assert(static_cast<int>(Error::DEFFMagic) == PSX_ERROR_DEFF_MAGIC);
static_assert(static_cast<int>(Error::DEFFTableTruncated) == PSX_ERROR_DEFF_TABLE_TRUNCATED);
static_assert(static_cast<int>(Error::DEFFOffsetsBad) == PSX_ERROR_DEFF_OFFSETS_BAD);
static_assert(static_cast<int>(Error::DEFFEntryMissing) == PSX_ERROR_DEFF_ENTRY_MISSING);
static_assert(static_cast<int>(Error::XAUnsupportedCoding) == PSX_ERROR_XA_UNSUPPORTED_CODING);
static_assert(static_cast<int>(Error::XAOutputTooLarge) == PSX_ERROR_XA_OUTPUT_TOO_LARGE);
static_assert(static_cast<int>(Error::CdromBadSectorType) == PSX_ERROR_CDROM_BAD_SECTOR_TYPE);

/// @brief Human-readable name for an Error value (logs, tests, crash context).
/// @param Code Any Error value; a code the library never produced yields
/// "Unknown" (the C core owns that table).
/// @return Static string, e.g. "TIMRectExtent".
/// @note The wrapper's `static_cast<Error>(code)` translations are safe because
///       the C core only ever returns codes declared in psx/error.h — one table,
///       one producer. Do not build an `Error` from an arbitrary integer: outside
///       the declared range that is undefined behaviour in C++ (an enum load),
///       even though the same value is legal at the C boundary. Probing that
///       boundary is deliberately a C-side test (psx/tests/c_consumer/main.c).
/// @note Source-compat note: this used to be a constexpr switch. It now delegates
///       to the C core so there is one table for both languages, which makes it a
///       runtime call. Nothing in the project needed it in a constant expression.
/// @note Thread: pure function returning static storage — thread-safe.
[[nodiscard]] inline std::string_view ErrorName(Error Code) noexcept
{
  return std::string_view(psx_error_name(static_cast<psx_error_t>(Code)));
}

/// @brief One-line human-readable explanation of an error code.
/// @param Code Any Error value.
/// @return Static sentence suitable for an operator-facing message; ErrorName()
///         stays the stable identifier for logs that are parsed.
/// @note Thread: pure function returning static storage — thread-safe.
[[nodiscard]] inline std::string_view ErrorMessage(Error Code) noexcept
{
  return std::string_view(psx_error_message(static_cast<psx_error_t>(Code)));
}

/// @brief Wrap a C result code as an expected<void> failure/success.
/// @param Code psx_error_t returned by a C call.
/// @return An empty success when Code is PSX_OK, otherwise the mapped Error.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] inline std::expected<void, Error> Checked(psx_error_t Code) noexcept
{
  if (Code == PSX_OK)
    return {};
  return std::unexpected(static_cast<Error>(Code));
}

/// @brief Result alias for the library (psx-library.md §5: every fallible
/// operation returns `std::expected<T, psx::Error>`).
template <class T>
using Result = std::expected<T, Error>;

/// @brief Fallible operation with no payload on success.
using StatusResult = std::expected<void, Error>;

} // namespace psx
