# PSX C17 Migration — Report

**Scope:** migrate the `psx` library from an in-tree C++23 implementation to a standalone,
reusable **C17 shared library** with optional **C++23 header-only wrappers** — a language
and packaging change, not a functional rewrite.

**Status: complete and verified.** The C17 core is the shipped implementation, the engine
consumes it through the C++23 wrappers without a single call-site change, and equivalence
with the pre-migration implementation is enforced by a corpus test rather than claimed.

Reference repository `~/Documents/GitHub/Severed-Chains` was **not consulted or modified**
for this increment: no format question was open, and the byte layouts and provenance were
already recorded in [`research/psx-formats.md`](../research/psx-formats.md). The reference
commit on record is unchanged (`ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb`, `progress.md`).

---

## 1. What was asked, and where each requirement is satisfied

| # | Requirement | Where satisfied | Evidence |
|---|---|---|---|
| 1 | C core compiles with a C compiler, no C++ runtime, zero dependencies | `psx/CMakeLists.txt` (`LANGUAGES C`, `C_STANDARD 17`, `C_EXTENSIONS OFF`, no-C++-source guard), `psx/src/*.c` | `nm -u` on the built library: `malloc calloc free __stack_chk_guard __stack_chk_fail` — nothing else. `otool -L`: `libSystem` only. Enforced by CTest `psx.abi.symbols` |
| 2 | Existing parsing/decoding/validation behaviour preserved exactly | `psx/src/{bpe,tim,archive}.c` | `psx.differential`: frozen pre-migration implementation vs the C core over a deterministic corpus (valid fixtures, truncations, per-byte flips, random noise, boundary probing). Negative control: three injected fixture faults each produced `MISMATCH` failures |
| 3 | Stable versioned C API, opaque handles, explicit ownership, typed error codes | `psx/include/psx/{export,version,error,bytes,bpe,tim,archive,psx}.h` | 39 exported functions + 3 `static inline` predicates; `psx_mrg_table_t`/`psx_deff_table_t` opaque; `psx_error_t` = one `typedef enum` carrying its frozen per-domain values (guarded to stay `int`-sized); `PSX_ABI_VERSION` + `SOVERSION` |
| 4 | C++23 wrappers are thin delegates (RAII, `std::expected`, `std::span`, move-only) | `psx/include/psx/cpp/*.hpp` (`psx::cpp`) | `psx.wrapper` (8 cases): delegation, span aliasing, value-type independence, move-only handles, exact error propagation; `static_assert`s pin `psx::Error` to the C constants and delete handle copies |
| 5 | Error codes with stable numeric values, decoder-specific granularity kept | `psx/include/psx/error.h`, `psx/src/error.c` | Ranges 0 / 1–49 / 50–79 / 80–109 / 110–139 / 140–169; values identical to the pre-migration C++ enum (compile-time asserted in the differential test); names/messages shared by both languages |
| 6 | CMake: shared C library with export/versioning, separate C++23 interface target | `psx/CMakeLists.txt`, `psx/cmake/psx-config.cmake.in` | `psx` (SHARED by default, `SOVERSION` = `PSX_ABI_VERSION`, hidden visibility) + `psx_cpp` INTERFACE (`EXPORT_NAME cpp`); install/export **verified consumable**: an out-of-tree project does `find_package(psx REQUIRED)` and links `psx::psx` from C and `psx::cpp` from C++ |
| 7 | Full test suite preserved + C-linkage and wrapper coverage | `psx/tests/` | 41 C++ cases (2 smoke, 9 BPE, 11 TIM, 10 archive, 8 wrapper, 1 C-linkage) — the 32 pre-migration cases carried over with their assertions intact |
| 8 | A standalone C consumer compiles, links and exercises the library | `psx/tests/c_consumer/main.c` (CTest `psx.c_consumer`) | C17 only, `-Wall -Wextra -Wshadow -Wconversion -Wmissing-prototypes -Wstrict-prototypes -pedantic`, 261 checks: every entry point, every documented error code, NULL-argument rejection, malformed-input contract, aliasing, handle lifetimes |
| 9 | Exported-symbol surface audited | `psx/tests/abi_symbols.cmake` (CTest `psx.abi.symbols`) | Asserts all 39 documented exports present, no undocumented export, no C++ mangled symbol exported **or** referenced; runs for shared **and** static. Method is chosen from the artifact: `nm` for ELF/Mach-O, the **PE export table** (`objdump -p` / `dumpbin /EXPORTS`) for a `.dll` — verified against a MinGW cross-built `libpsx.dll` |
| 10 | Documentation updated | `docs/architecture/psx-c-api.md` (new) + AGENTS.md, psx-library.md, error-handling.md, testing.md, build-system.md, guidelines/cpp.md, README.md, roadmap.md, progress.md | See §4 |
| 11 | Incremental migration; originals kept until verified equivalent | `psx/migration/legacy/` (fixture), `psx/migration/differential/` | C++ originals removed from `psx/src/` only after the differential run + 41 wrapper cases + 261 C checks passed; the originals remain as the fixture (byte-identical but for the namespace/include rename) |
| 12 | Scope discipline | — | No engine subsystem touched beyond the include/`target_link_libraries` swap; no new third-party dependency anywhere; `psx` still has no SDL/Qt/Luau/engine reference |

## 2. Verification

| Configuration | Result |
|---|---|
| In-tree default (RelWithDebInfo) | **27/27 CTest**, 67 engine + 41 psx cases, 0 warnings |
| In-tree `-DSC_WERROR=ON -DPSX_ENABLE_DIFFERENTIAL_TEST=ON` | **28/28** |
| In-tree `-DSC_ENABLE_EXCEPTIONS=OFF` / `=ON` | **27/27** both |
| In-tree ASan + UBSan (+ differential) | **28/28** (differential corpus ≈112 s instrumented) |
| `psx` standalone, shared | **8/8** |
| `psx` standalone, `-DPSX_SHARED=OFF` (static) | **8/8** |
| `psx` standalone, `-DBUILD_SHARED_LIBS=OFF` | static `libpsx.a`, **8/8** |
| `psx.differential` (opt-in) | 4 cases: error-code/name preservation, BPE, TIM, archive corpora |
| `cmake --install` + out-of-tree `find_package(psx)` consumer (one C17 exe, one C++23 exe) | both build and run against the installed package: C sees `abi=1 version=1.0.0`, `psx_tim_parse` of an empty view → `Truncated`, `psx_tim_clut_entry_count(16x16)` = 256; C++ round-trips BPE through `psx::cpp` and prints `BPEMagic` |
| `psx` cross-built for **Windows** (MinGW-w64 GCC 16.2, `x86_64-w64-mingw32`, toolchain file) | `libpsx.dll` + import lib + `sc_tests_psx.exe` + `sc_psx_c_consumer.exe` build warning-free; PE export table = exactly the 39 documented symbols, 0 C++ symbols in exports or imports; the audit's PE path passes on that DLL and fails on an export-less control exe; the `dumpbin` row shape is checked against MSVC-shaped `/EXPORTS` output (39/39 parsed, missing-export negative control fires). Binaries are **not executed** (no Wine/emulator here) |
| Linux **target** compile check (host clang, `--target=x86_64-unknown-linux-gnu`) | All 5 core TUs compile to **ELF** objects warning-free under the project's strict set (`-std=c17 -Wall -Wextra -Wshadow -Wconversion -Wmissing-prototypes -Wstrict-prototypes -pedantic`); the objects define exactly 39 `psx_*` globals with 0 stray globals and reference nothing but `malloc calloc free`. Caveat: `<stdlib.h>` came from the CLT SDK (no glibc headers here), and there is no link/run — this is a target/ABI compile check, not a Linux build. Toolchain file and commands are in §3.13 |
| Naming lint (`repo.naming`) | clean (C and C++ grammars both audited) |
| Formatting gate (`repo.format`, whole tree incl. fixtures) | clean **at the time of that report** — `clang-format --dry-run --Werror` over the then 61 project files (84 as of 2026-10-29; numbers age, re-measure). **Correction 2026-10-29:** the second half of this row was false. The degradable brace scan did **not** "report the offenders it claims to" — it reported 41 lines that `clang-format` 23.1 calls clean, including braced two-statement bodies where braces are mandatory (`psx/src/bpe.c`, `src/engine/src/fs.cpp`). It had never been run against CI, which is where the error was visible: the Formatting gate step failed in 4 s on every leg, because the runner names its formatter `clang-format-18` and the script probed only the bare name, then fell into the fallback and failed correct code. Both defects are now fixed (skip with exit 77 instead of a fallback; discover version-suffixed names; `#pragma once` remains a hard gate on every host) and verified against a Linux-shaped PATH under Python 3.9.6 |

Runtime dependency of the shipped library, in full:

```
$ nm -u build/psx/libpsx.1.0.0.dylib
___stack_chk_fail ___stack_chk_guard _calloc _free _malloc
$ otool -L build/bin/lodsc | grep psx
@rpath/libpsx.1.dylib (compatibility version 1.0.0, current version 1.0.0)
```

## 3. Findings and decisions taken during the migration

1. **The pre-migration error values were already the ABI.** The C enum kept every number
   the C++ enum used (1–2 generic, 50s/80s/110s/140s by domain), so codes cross the
   boundary by cast. That is now *asserted* rather than assumed, in
   `psx/migration/differential/test_differential.cpp`.
2. **`ErrorName` stopped being `constexpr`.** It delegates to the C table so there is one
   name/message table for both languages; nothing in the tree needed it in a constant
   expression (recorded in `psx/include/psx/cpp/error.hpp`).
3. **Three documented predicates are header-inline, not exported**
   (`psx_bytes_is_valid`, `psx_writable_bytes_is_valid`, `psx_mrg_entry_is_virtual`).
   The symbol audit caught this mismatch between documentation and the built library and
   was corrected to test them where they are actually used — the C consumer — instead of
   weakening the audit.
4. **Random and flip mutations alone are not enough to prove equivalence.** Boundary
   probing was added after measurement: with a deliberate off-by-one in the BPE block-size
   limit (`> 0x800` → `> 0x801`) and one in the TIM section-size comparison, the
   truncation/flip/noise corpus passed. Loading every 2-/4-byte window of each fixture with
   threshold values catches both. This is recorded in psx-c-api.md §10 so the next person
   does not re-derive it.
5. **MRG/DEFF entry offsets are absolute inside the container.** Two new tests asserted a
   payload-relative reading and failed; the implementations (old and new) agree on
   absolute, which matches how a real `DRGN*.BIN` is read. The expectation was wrong, not
   the code — and the differential test confirms the C core matches the old C++ behaviour
   here.
6. **`sector_aligned` stays a caller-supplied flag** (`int` 0/1 in C). Sector scaling is a
   property of the path a container came from, which `psx` cannot know; `lod::res` decides
   it. Keeping it a parameter is what lets the core stay generic.
7. **Two allocation models, both first-class.** The allocation-free parse path
   (caller-supplied entry array) is what the engine uses; the opaque `_table_parse` handles
   serve C embedders. Handles leave the caller's out-parameter untouched on failure and
   answer 0/empty for NULL, so a moved-from handle is safe to query.
8. **`BUILD_SHARED_LIBS` and the "shared C library" brief were reconciled** rather than
   one overriding the other: `PSX_SHARED` defaults to `BUILD_SHARED_LIBS` when the user sets
   it, and to shared when unset. Both forms are audited identically.
9. **The wrappers are what made the migration invisible to the engine.** Because
   `psx::cpp` preserves the pre-migration C++ signatures (`ParseMRG`, `MRGEntryData`,
   `DecompressBPE`, `psx::Error`), `src/engine/src/resources.cpp` needed only its include
   line changed. That is the intended shape of the layering, not luck.
10. **The exported C++ target name was wrong, and only an out-of-tree consumer could
    prove it.** `install(EXPORT ... NAMESPACE psx::)` derives the imported name from the
    *target* name, so the package published `psx::psx_cpp` while the tree uses `psx::cpp` —
    every in-tree configuration passed because it never consults the export set. Fixed with
    `EXPORT_NAME cpp` on `psx_cpp`, verified by installing to a prefix and building a
    throwaway project that does `find_package(psx REQUIRED)` and links `psx::psx` (C) and
    `psx::cpp` (C++). Lesson kept: an install/export surface is untested until something
    outside the build tree consumes it.
11. **Nested `detail` namespaces broke name lookup** in the wrapper headers
    (`detail::ToBytes` resolved to `psx::tim::detail` and never reached the shared helpers).
    Fixed by re-exporting the helpers with `using` declarations rather than duplicating them.
12. **The Windows export surface is now machine-checked — from a macOS host.** A MinGW-w64
    cross-build (`x86_64-w64-mingw32`, GCC 16.2) compiles the C core, the wrapper headers
    and both test executables warning-free, which incidentally puts the `__declspec` branch
    of `export.h`, libstdc++'s C++23 `<expected>`/`<span>` and GCC's strict-C diagnostics
    under test for the first time. It also exposed that the audit's `nm` approach does not
    transfer to PE: `nm --defined-only --extern-only` on `libpsx.dll` reports **216** names
    for a **39**-entry export table (globals from the linked objects plus MinGW's CRT and
    startup symbols), so it invents missing exports *and* stray ones at the same time. The
    audit therefore chooses its method from the artifact — MZ header or `.dll` → export
    table, otherwise `nm` — and the PE path is verified against the real cross-built DLL
    with a negative control. Two gotchas recorded: CMake's regex dialect has no `{4,}`
    repetition (it matched nothing at all, silently, until the row shapes were written out
    explicitly), and PE cdecl export names are undecorated, so the Mach-O `_` prefix must
    not be applied there.

13. **Cross-platform verification without a second OS.** No Linux or Windows host was
    available and the Docker daemon was deliberately left stopped (thermal budget), so the
    builds were verified by cross-compiling instead:

    ```cmake
    # mingw-w64.cmake — Windows cross-build used for §2
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
    set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
    set(CMAKE_FIND_ROOT_PATH /opt/homebrew/opt/mingw-w64)
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
    ```

    `cmake -S psx -B win -G Ninja -DCMAKE_TOOLCHAIN_FILE=mingw-w64.cmake` then produced the
    DLL, the import library and both test executables. The Linux leg used the host compiler
    targeting `x86_64-unknown-linux-gnu` with `-ffreestanding -nostdinc` plus the clang
    resource headers (4 of 5 TUs need nothing from a libc at all — only `archive.c` includes
    `<stdlib.h>`, which is the allocator boundary the design already documents). Neither leg
    *ran* anything; that is what CI is for, and §6.1 says so.

## 4. Documentation changes

| File | Change |
|---|---|
| `docs/architecture/psx-c-api.md` | **New.** Authoritative for the C surface: layout, linkage/naming/export, byte views and ownership, error model, full API inventory (incl. handle lifecycle and struct/ABI rules), the two version axes, wrapper contract, consumer/test matrix, zero-dependency guarantees, migration record, "adding a decoder" checklist, open items |
| `docs/architecture/psx-library.md` | Decision statement now C17 core + C++23 wrappers; independence rules ("no C++ runtime"); §2.1 rewritten as a C-surface/wrapper table; conventions updated (error model, borrowed-vs-owned, two naming grammars); why-a-separate-library gains the two-language-reachability point |
| `docs/architecture/error-handling.md` | §1 records that psx's exception-free-ness is now structural; **new §4.1** "Format decoders (`psx`) across the language boundary" — the precise-code / coarse-status contract, table of the three vocabularies, and the rules that keep it honest |
| `docs/architecture/testing.md` | §2.11 rewritten as the four-target psx matrix with what each guarantees; malformed-input corpus described; error-code expectations stated in both languages |
| `docs/architecture/build-system.md` | Layout/target table for `psx` + `psx_cpp` (libc-only, hidden visibility, `SOVERSION` = `PSX_ABI_VERSION`); build-form paragraph reconciled with `PSX_SHARED`; exception-policy wording corrected (core needs no flags; C++ psx targets link `sc::exceptions_off`); CI test list updated; `psx-tests` → current target names |
| `docs/guidelines/cpp.md` | §1 gains the C17 baseline for the psx core; §7 records the two naming grammars; §9.2 rewritten for the C core + wrapper-delegate rules (and the stale "parity: Gte.java:line" comment example removed, per §2's no-Java-citation rule) |
| `AGENTS.md` | Approved stack gains C17 (psx core); boundary text for the psx library; exception-free-by-contract and standalone/dependency text updated to the C-core + wrappers reality |
| `README.md` | Summary, standalone build instructions (incl. the differential option), tree listing, psx build options, policy notes (exceptions, RTTI, ABI audit) |
| `docs/architecture/roadmap.md` | M6 note now names the standalone psx suite and the wrapper layer |
| `progress.md` | Row for this increment |
| Code comments | Cite `psx-c-api.md` sections rather than restating the rules; the three pre-existing citations that pointed at the wrong testing.md/psx-c-api.md sections were corrected |

## 5. Scope check

- **No new third-party dependency** anywhere; `psx` links libc only.
- **No engine/game behaviour change**: `lod::res` keeps its validation, path confinement,
  RAII blob ownership and status mapping; the only edits were the include line and
  `psx::psx` → `psx::cpp`.
- **No rendering, audio or gameplay work**, no ISO/unpacking work.
- **`psx/migration/` is a fixture, not shipped code**: never installed, never exported, not
  built unless `PSX_ENABLE_DIFFERENTIAL_TEST=ON`, and unreachable from the engine.

## 6. Open items

1. **Windows/Linux execution and a native-toolchain run** — the Windows *build* and its
   export table are machine-checked through a MinGW-w64 cross-build (§2, finding 12), the
   `objdump -p` and `dumpbin /EXPORTS` row shapes both parse, and every core TU compiles for
   a Linux ELF target (§3.13), but nothing here *ran* a non-macOS binary and no Linux library
   was linked (no glibc, no ELF linker, Docker left stopped to avoid load). Closing this needs
   a three-platform CI matrix — MSVC build + run + `dumpbin` audit, Linux build + run + `nm`
   audit, macOS as today. `psx/tests/abi_symbols.cmake` skips with a visible message when
   neither `objdump` nor `dumpbin` is on PATH, so an unconfigured runner cannot pass silently.
2. **Wrapper coverage for future modules** — GTE/GPU/SPU/CD-ROM should each decide whether a
   C++ wrapper earns its keep, rather than wrapping for symmetry.
3. **Large-container streaming** is unchanged by this migration and remains open
   (resource-management.md §6).
4. **Packaged-consumer CI lane** — out-of-tree `find_package(psx)` consumption is verified
   manually (§2) and the export-name defect it found is fixed, but nothing in the suite
   installs and re-consumes the package automatically. A CI job that installs to a scratch
   prefix and builds a two-target consumer would make that a gate rather than a checklist
   item; it needs a small in-repo fixture project to hang off.
