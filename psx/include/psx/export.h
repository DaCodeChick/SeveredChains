#pragma once
/// @file export.h
/// @brief Public-symbol export mechanism for the `psx` shared library.
///
/// Every symbol in the public C API is marked `PSX_API`; everything else is
/// hidden where the toolchain supports visibility control (CMake sets
/// `C_VISIBILITY_PRESET hidden`, and MSVC only exports what is marked). The
/// library's ABI is therefore exactly the `PSX_API` set — see
/// docs/architecture/psx-c-api.md §2.
///
/// The only compiler-specific extensions in the library live in this header,
/// each with a fallback so an unknown toolchain still builds (it just exports
/// more than it needs to).
///
/// Build-time definitions (set by CMake, documented for hand-rolled builds):
///  - `PSX_BUILDING_SHARED` — compiling the library itself as a shared object.
///  - `PSX_STATIC`          — the library is a static archive; no import/export.
///  - neither               — consuming a `psx` shared library (dllimport on
///                            MSVC, default visibility elsewhere).

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(PSX_STATIC)
#    define PSX_API
#  elif defined(PSX_BUILDING_SHARED)
#    define PSX_API __declspec(dllexport)
#  else
#    define PSX_API __declspec(dllimport)
#  endif
#  define PSX_NO_EXPORT
#else
#  if defined(PSX_STATIC)
#    define PSX_API
#  elif (defined(__GNUC__) && __GNUC__ >= 4) || defined(__clang__)
#    define PSX_API __attribute__((visibility("default")))
#  else
#    define PSX_API
#  endif
#  if (defined(__GNUC__) && __GNUC__ >= 4) || defined(__clang__)
#    define PSX_NO_EXPORT __attribute__((visibility("hidden")))
#  else
#    define PSX_NO_EXPORT
#  endif
#endif

/// @brief Not part of the ABI: internal helpers shared between translation
/// units of the library. Never a substitute for a documented internal header.
#define PSX_INTERNAL PSX_NO_EXPORT
