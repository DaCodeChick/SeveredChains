#pragma once
/// @file psx.h
/// @brief Public entry point of the reusable PSX library (psx-library.md).
///
/// The core is **C17**: this umbrella compiles as C17 and as C++23 (every public
/// header carries an `extern "C"` guard), and the shared library depends on
/// nothing but the C standard library. Namespace `psx`/`psx::` only — prefix-free
/// by contract (no Sc/SC/sc/LoD/LOD/lod).
///
/// Implemented modules (C API here, optional C++23 wrappers in `psx/cpp/`):
///  - psx_bpe_*   — adaptive byte-pair codec used by the disc (bpe.h)
///  - psx_tim_*   — TIM texture images (tim.h)
///  - psx_archive_* / psx_mrg_* / psx_deff_* — MRG / DEFF containers (archive.h)
///
/// Documented but not yet implemented (psx-library.md §2): gte, gpu, spu, xa,
/// sssq, cdrom, tmd, anm, tag. Nothing here guesses at their layouts; each lands
/// with the evidence and golden tests that pin it down.
///
/// @see docs/architecture/psx-c-api.md for the API conventions, ABI versioning,
///      ownership rules, error-code table and C/C++ integration examples.

#include "psx/archive.h"
#include "psx/bpe.h"
#include "psx/bytes.h"
#include "psx/error.h"
#include "psx/export.h"
#include "psx/tim.h"
#include "psx/version.h"
