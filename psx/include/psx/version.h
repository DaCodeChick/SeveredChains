#pragma once
/// @file version.h
/// @brief Library and ABI versioning for `psx` (docs/architecture/psx-c-api.md §6).
///
/// Two version numbers, deliberately separate because they answer different
/// questions:
///
///  - **Library version** (`PSX_VERSION_MAJOR/MINOR/PATCH`) — what the project
///    released.
///  - **ABI version** (`PSX_ABI_VERSION`) — what a *linked binary* depends on:
///    exported symbol names, struct layouts, struct sizes, enum values, and
///    documented semantics of return values.
///
/// Rules (checked by the `psx.c_api` test, which asserts the runtime values
/// match these macros):
///  - Adding a function, or a new error code with a fresh numeric value, is a
///    MINOR library change and leaves `PSX_ABI_VERSION` alone: existing binaries
///    keep working.
///  - Changing any exported function signature, any public struct layout or size,
///    any existing error-code value, or the documented behavior of an existing
///    function is an ABI change: bump `PSX_ABI_VERSION` **and** the library major
///    version together.
///  - Patch releases are binary-compatible by construction: same ABI version,
///    same SONAME.
///
/// The shared library carries `SOVERSION = PSX_ABI_VERSION`, so an old binary
/// refuses to load an incompatible library instead of misreading a struct.

#include "psx/export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief Library major version (breaking changes, including ABI changes).
#define PSX_VERSION_MAJOR 1
/// @brief Library minor version (additive, ABI-compatible).
#define PSX_VERSION_MINOR 0
/// @brief Library patch version (fixes only).
#define PSX_VERSION_PATCH 0

/// @brief ABI version: exported symbols, struct layouts, error-code values and
/// documented semantics. Independent of the minor/patch library version.
#define PSX_ABI_VERSION 1u

/// @brief Version string of the build that produced this header.
/// @note The runtime string comes from psx_version_string(); comparing the two
///       detects a header/library mismatch (header from one release, dylib from
///       another).
#define PSX_VERSION_STRING "1.0.0"

/// @brief Compile-time check that a linked library matches the expected ABI.
/// @param ABI Expected ABI version, normally `PSX_ABI_VERSION`.
/// @return Non-zero when the ABI is acceptable (same major ABI version).
/// @note Thread: pure function, no state — thread-safe.
#define PSX_ABI_IS_COMPATIBLE(ABI) ((ABI) == PSX_ABI_VERSION)

/// @brief Library major version of the loaded library.
/// @note Thread: pure function, no state — thread-safe.
PSX_API uint32_t psx_version_major(void);

/// @brief Library minor version of the loaded library.
/// @note Thread: pure function, no state — thread-safe.
PSX_API uint32_t psx_version_minor(void);

/// @brief Library patch version of the loaded library.
/// @note Thread: pure function, no state — thread-safe.
PSX_API uint32_t psx_version_patch(void);

/// @brief ABI version of the loaded library (see PSX_ABI_VERSION).
/// @note Thread: pure function, no state — thread-safe.
PSX_API uint32_t psx_abi_version(void);

/// @brief Version string of the loaded library ("major.minor.patch").
/// @return Pointer to a static, null-terminated string; never NULL, valid for
///         the whole process lifetime, must not be freed.
/// @note Thread: pure function returning static storage — thread-safe.
PSX_API const char* psx_version_string(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
