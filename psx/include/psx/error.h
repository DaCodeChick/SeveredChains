#pragma once
/// @file error.h
/// @brief `psx_error_t` — the failure vocabulary of the C core
/// (docs/architecture/psx-c-api.md §4, error-handling.md §4.1).
///
/// The numeric values are **frozen**: they equal the C++ `psx::Error` values
/// that preceded this migration, they appear in test expectations and crash
/// diagnostics, and the C++ wrapper maps them with `static_cast` (proved by
/// compile-time assertions in psx/cpp/error.hpp). Never renumber an existing
/// code; append a new value in the owning group instead.
///
/// Grouping (the ranges exist so a new decoder adds codes without renumbering):
///
/// ```text
///   0        PSX_OK
///   1..49    generic
///  50..79    BPE
///  80..109   TIM
/// 110..139   MRG
/// 140..169   DEFF
/// ```

#include "psx/export.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief Error type of every fallible `psx` function, together with the complete
///        set of codes it can carry.
///
/// One `typedef enum` rather than `typedef int` plus loose integer constants: the
/// type and its valid values are declared in one place, so a debugger, a binding
/// generator or an IDE sees the vocabulary instead of a bare `int`, and there is no
/// way for the constant set and the parameter type to drift apart.
///
/// Two properties this type must keep, both enforced or documented below:
///  - **Size.** C leaves the compatible type of an `enum` implementation-defined, and
///    this type crosses a shared-library ABI, so an ABI guard asserts it stays
///    `int`-sized for every build of the library (§ below). A toolchain that would
///    pack the enumerators tighter (`-fshort-enums`) fails to compile rather than
///    silently changing the ABI.
///  - **Out-of-range values.** C enumeration parameters are not strongly typed: any
///    `int`-shaped value may be passed, and unknown codes stay reportable
///    (psx_error_name() yields "Unknown"). That is deliberate — a caller reading a
///    code from an older/newer library must still get a diagnostic, not a crash.
///    All valid codes are 0..169, so the signedness a given toolchain picks for the
///    enumerator set is not observable across the boundary.
typedef enum
{
  /// @brief Success.
  PSX_OK = 0,

  /* --- generic (1..49) -------------------------------------------------- */

  /// @brief Caller violated a documented precondition (NULL out-param, null
  ///        buffer with non-zero size, capacity smaller than a required header).
  PSX_ERROR_INVALID_ARGUMENT = 1,
  /// @brief Input ended before the structure being read did.
  PSX_ERROR_TRUNCATED = 2,

  /* --- BPE (50..79) ----------------------------------------------------- */

  /// @brief u32 at 0x4 is not 0x1a455042 — not a BPE segment.
  PSX_ERROR_BPE_MAGIC = 50,
  /// @brief Block header exceeds the 0x800-byte block limit.
  PSX_ERROR_BPE_BLOCK_SIZE = 51,
  /// @brief Declared output size exceeds the caller's capacity/ceiling.
  PSX_ERROR_BPE_OUTPUT_TOO_LARGE = 52,
  /// @brief The blocks produce a byte count different from the header's.
  PSX_ERROR_BPE_SIZE_MISMATCH = 53,
  /// @brief Dictionary build ran past key 0xff.
  PSX_ERROR_BPE_DICTIONARY_OVERFLOW = 54,
  /// @brief Expansion holds more pending bytes than the block can emit
  ///        (self-referential dictionary).
  PSX_ERROR_BPE_EXPANSION_OVERFLOW = 55,

  /* --- TIM (80..109) ---------------------------------------------------- */

  /// @brief u32 at 0x0 is not 0x10 — not a TIM.
  PSX_ERROR_TIM_MAGIC = 80,
  /// @brief Colour-depth code in flags is not 0..3.
  PSX_ERROR_TIM_BPP = 81,
  /// @brief Buffer ends before a required section header/rect exists.
  PSX_ERROR_TIM_NO_IMAGES = 82,
  /// @brief Section length field is smaller than its own 12-byte header.
  PSX_ERROR_TIM_SECTION_SIZE = 83,
  /// @brief Rect is empty/negative, or promises more bytes than the section
  ///        declares.
  PSX_ERROR_TIM_RECT_EXTENT = 84,
  /// @brief A section's declared payload runs past the buffer.
  PSX_ERROR_TIM_SECTION_OUT_OF_RANGE = 85,

  /* --- MRG (110..139) --------------------------------------------------- */

  /// @brief u32 at 0x0 is not 0x1a47524d — not an MRG.
  PSX_ERROR_MRG_MAGIC = 110,
  /// @brief Entry count is plausible but the entry table exceeds the buffer.
  PSX_ERROR_MRG_TABLE_TRUNCATED = 111,
  /// @brief An entry's offset/size lies outside the container.
  PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE = 112,
  /// @brief Entry index is out of range.
  PSX_ERROR_MRG_ENTRY_MISSING = 113,

  /* --- DEFF (140..169) -------------------------------------------------- */

  /// @brief u32 at 0x0 is not 0x46464544 — not a DEFF.
  PSX_ERROR_DEFF_MAGIC = 140,
  /// @brief Subfile table exceeds the buffer.
  PSX_ERROR_DEFF_TABLE_TRUNCATED = 141,
  /// @brief Subfile starts are not monotonically increasing inside the
  ///        container.
  PSX_ERROR_DEFF_OFFSETS_BAD = 142,
  /// @brief Subfile index is out of range.
  PSX_ERROR_DEFF_ENTRY_MISSING = 143
} psx_error_t;

/// @brief ABI guard: `psx_error_t` crosses the shared-library boundary as a function
///        return and parameter type, so its width must not depend on what the
///        toolchain finds convenient. Every supported compiler (Clang, GCC without
///        `-fshort-enums`, MSVC) gives this enumerator set `int` width; if one ever
///        does not, the build stops here instead of shipping a mismatched ABI.
/// @see psx-c-api.md §4 (error model) and §6 (ABI rules).
#ifdef __cplusplus
static_assert(sizeof(psx_error_t) == sizeof(int), "psx_error_t must stay int-sized: it is part of the C ABI");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(psx_error_t) == sizeof(int), "psx_error_t must stay int-sized: it is part of the C ABI");
#endif

/// @brief Identifier-style name for an error code ("TIMRectExtent", "PSX_OK").
///
/// These strings are part of the diagnostic contract: they appear in engine logs
/// and test output, so renaming one is a breaking change.
///
/// @param Error Any psx_error_t value, including unknown ones.
/// @return Static, null-terminated string; never NULL. `PSX_OK` yields "Ok";
///         any value not listed above yields "Unknown" (the same spelling the
///         C++ `psx::ErrorName` has always used for out-of-range input).
/// @note Thread: pure function returning static storage — thread-safe.
PSX_API const char* psx_error_name(psx_error_t error);

/// @brief Human-readable one-line explanation for an error code.
/// @param Error Any psx_error_t value, including unknown ones.
/// @return Static, null-terminated string; never NULL. Intended for operator
///         messages; psx_error_name() stays the stable identifier.
/// @note Thread: pure function returning static storage — thread-safe.
PSX_API const char* psx_error_message(psx_error_t error);

/// @brief Whether a code belongs to the BPE group (50..79).
/// @param Error Code to classify.
/// @return Non-zero when the code is a BPE failure.
/// @note Thread: pure function, no state — thread-safe.
PSX_API int psx_error_is_bpe(psx_error_t error);

/// @brief Whether a code belongs to the TIM group (80..109).
/// @param Error Code to classify.
/// @return Non-zero when the code is a TIM failure.
/// @note Thread: pure function, no state — thread-safe.
PSX_API int psx_error_is_tim(psx_error_t error);

/// @brief Whether a code belongs to the MRG group (110..139).
/// @param Error Code to classify.
/// @return Non-zero when the code is an MRG failure.
/// @note Thread: pure function, no state — thread-safe.
PSX_API int psx_error_is_mrg(psx_error_t error);

/// @brief Whether a code belongs to the DEFF group (140..169).
/// @param Error Code to classify.
/// @return Non-zero when the code is a DEFF failure.
/// @note Thread: pure function, no state — thread-safe.
PSX_API int psx_error_is_deff(psx_error_t error);

#ifdef __cplusplus
} /* extern "C" */
#endif
