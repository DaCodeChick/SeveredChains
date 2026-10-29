# PSX C API — C17 Core, C++23 Wrappers

**Decision: the reusable `psx` library is a C17 shared library (`psx`, exported as
`psx::psx`) with an optional header-only C++23 wrapper layer (`psx_cpp`, exported as
`psx::cpp`).** The C core is the ABI; the wrappers are a convenience face over it, not a
second implementation. This document is authoritative for the C surface, the ABI rules,
and the wrapper contract; `psx-library.md` remains authoritative for *what PS1
technology lives here* and for the module roadmap.

The migration was a language and packaging change, not a rewrite: the parsing,
validation and decoding rules are those recorded in
[`research/psx-formats.md`](../research/psx-formats.md), and equivalence with the
pre-migration C++ implementation is enforced by a corpus test (§10), not asserted.

## 1. Shape of the library

| Layer | Artifact | Language | Depends on |
|---|---|---|---|
| Core | `psx` / `psx::psx` (shared by default, `PSX_SHARED=OFF` for static) | C17 | libc allocator only |
| Wrappers | `psx_cpp` / `psx::cpp` (INTERFACE, header-only) | C++23 | the core, `<expected>`, `<span>` |

Those two names are what a consumer writes **both** in-tree and after
`find_package(psx)`: the wrapper target's `EXPORT_NAME` is `cpp`, so the installed package
publishes `psx::psx` and `psx::cpp` rather than `psx::psx_cpp` (verified by installing to a
prefix and building an out-of-tree C and C++ consumer — psx-c-api.md §8).
| Consumers | `scengine` (and therefore `lodsc`, engine tests) | C++23 | `psx::cpp` |

```
psx/include/psx/            C ABI, include-able from C17 or C++
  psx.h                     umbrella: everything below
  export.h                  PSX_API / PSX_NO_EXPORT (MSVC declspec, GCC visibility)
  version.h                 PSX_VERSION_*, PSX_ABI_VERSION, PSX_ABI_IS_COMPATIBLE
  error.h                   psx_error_t + grouped code ranges + name/message/grouping
  bytes.h                   psx_bytes_t, psx_writable_bytes_t (+ inline predicates)
  bpe.h                     BPE segment probe, declared size, decompress, compress
  tim.h                     TIM parse/validate, CLUT entry count
  archive.h                 MRG + DEFF parse, entry access, checked slicing, handles
psx/include/psx/cpp/        C++23 wrappers (thin delegates)
  psx.hpp  bytes.hpp  error.hpp  bpe.hpp  tim.hpp  archive.hpp
psx/src/                    C17 implementation
  bpe.c  tim.c  archive.c  error.c  version.c
  reader.h                internal bounds-checked little-endian reader
psx/migration/legacy/       frozen pre-migration C++ implementation (fixture, §10)
psx/migration/differential/ corpus test comparing fixture vs core (§10)
```

Everything a consumer needs is reachable from one include:

```c
#include "psx/psx.h"        /* C17  */
```

```cpp
#include "psx/cpp/psx.hpp"  /* C++23 */
```

## 2. Linkage, naming and symbol export

- Every declaration in the C headers is inside `extern "C"` when compiled as C++, so the
  headers work from either language without the caller adding anything.
  `psx/tests/test_c_linkage.cpp` additionally includes the umbrella *inside an explicit*
  `extern "C"` block, which is how an embedder with an older toolchain would do it.
- Public functions are declared `PSX_API`. Visibility is set to hidden for the target,
  so **only** `PSX_API`-marked symbols leave the library; the exported set is audited
  (§9). Nothing else may be relied upon.
- The build defines `PSX_BUILDING_LIBRARY` while compiling the core and never defines
  `PSX_STATIC` for a shared build; consumers of the static form define `PSX_STATIC`
  themselves (the macro switches `PSX_API` to nothing, which is what a static link needs).
- **C naming** (AGENTS.md naming table applied to C): functions are `psx_` +
  `lower_snake_case` with acronyms lower-case like the rest of the identifier
  (`psx_bpe_declared_size`, `psx_tim_clut_entry_count`); types are
  `psx_…_t`; macros and enumerators are `PSX_UPPER_SNAKE`
  (`PSX_ERROR_BPE_MAGIC`, `PSX_TIM_SECTION_HEADER_BYTES`). Acronym policy in its
  PascalCase form applies to the *C++ wrappers*, where the same names reappear as
  `DecompressBPE`, `CLUTEntryCount`, `Error::BPEMagic`.
- Third-party and reference names stay verbatim; `psx` is prefix-free by owner decision
  (`psx-library.md`), and the `psx_` symbol prefix is the namespace, not a project tag.

## 3. Byte views and ownership

The core never allocates a buffer it hands back as decoded data, and never keeps a
pointer after a call returns. Two descriptor types express the whole contract:

```c
typedef struct psx_bytes_t           { const uint8_t* data; size_t size; } psx_bytes_t;
typedef struct psx_writable_bytes_t  { uint8_t* data;   size_t capacity; } psx_writable_bytes_t;
```

- `psx_bytes_t` is a **borrowed, read-only** view. Passing `{NULL, 0}` is valid and means
  "empty"; `{NULL, n}` with `n > 0` is a caller bug and is rejected with
  `PSX_ERROR_INVALID_ARGUMENT` (`psx_bytes_is_valid`, inline in `psx/bytes.h`).
- `psx_writable_bytes_t` is a **caller-owned destination**. `psx_bpe_decompress` writes
  into it and reports the byte count through `out_written`; the core allocates nothing.
- Parsed results that reference input bytes (`psx_tim_image_t.clut`,
  `psx_tim_image_t.image_data`, the spans returned by `psx_mrg_view_slice` /
  `psx_deff_view_slice`) **alias the caller's buffer**. The buffer must outlive the
  result; nothing copies.
- Containers (`psx_mrg_table_t`, `psx_deff_table_t`) are the only heap objects the core
  creates, and only through the explicit `_table_parse` entry points (§5.3).
- Offsets inside MRG/DEFF containers are **absolute inside the container** — as they are
  in a real `DRGN*.BIN` — not payload-relative. The parser slices them verbatim after
  bounds-checking.

## 4. Error model

```c
typedef enum
{
  PSX_OK = 0,
  PSX_ERROR_INVALID_ARGUMENT = 1,
  /* … every code in one enumerator list, grouped by domain with comments … */
  PSX_ERROR_DEFF_ENTRY_MISSING = 143
} psx_error_t;
```

One `typedef enum` that carries its own values — not `typedef int` plus a separate list of
integer constants. The type and the set of valid values are declared in one place, so a
debugger, a binding generator or an IDE sees the vocabulary instead of a bare `int`, and
the constant set cannot drift away from the parameter type. Two properties are held on
purpose and both are checked in `psx/include/psx/error.h`:

- **Width.** C leaves an `enum`'s compatible type implementation-defined and this type
  crosses a shared-library ABI, so a `_Static_assert`/`static_assert` guard requires
  `sizeof(psx_error_t) == sizeof(int)`. A toolchain that would pack the enumerators
  tighter (`-fshort-enums`) fails to compile instead of silently shipping a different ABI.
  Verified to fire that way with `clang -std=c17 -fshort-enums`.
- **Out-of-range values.** Enumeration *parameters* are not strongly typed in C, so any
  `int`-shaped value may still arrive and stays reportable (`psx_error_name` answers
  `"Unknown"`). All valid codes are 0..169, so whatever signedness a toolchain picks for
  the enumerator set is not observable across the boundary. Callers that *pass* a value
  outside the enumerator set need a cast (C++ requires it, C needs it for negatives); the
  test suite does exactly that to pin the behaviour.

| Range | Domain | Codes |
|---|---|---|
| 0 | success | `PSX_OK` |
| 1–49 | generic | `PSX_ERROR_INVALID_ARGUMENT = 1`, `PSX_ERROR_TRUNCATED = 2` |
| 50–79 | BPE | magic, block size, output ceiling, size mismatch, dictionary overflow, expansion overflow |
| 80–109 | TIM | magic, bpp, no images, section size, rect extent, section out of range |
| 110–139 | MRG | magic, table truncated, entry out of range, entry missing |
| 140–169 | DEFF | magic, table truncated, offsets bad, entry missing |

Rules:

- **Numeric values are frozen.** They appear in logs, crash context and test
  expectations, and they are the values the pre-migration C++ enum already used — the
  migration preserved them exactly (proved by compile-time assertions in
  `psx/migration/differential/test_differential.cpp`, and by `psx/include/psx/cpp/error.hpp`
  building `psx::Error` directly from these constants). A new decoder adds codes inside
  its own range; existing numbers never move. Because the values are named *inside* the
  enum, an appended code is a one-line edit with no second list to update.
- Every fallible function returns `psx_error_t` and writes results through out-parameters
  marked with a trailing `_`. `PSX_OK` means the out-parameter is set; any other value
  means it is untouched.
- NULL out-parameters are `PSX_ERROR_INVALID_ARGUMENT`, checked before any parsing work.
- `psx_error_name()` returns the stable identifier (`"TIMRectExtent"`),
  `psx_error_message()` a one-line explanation for operator output, and
  `psx_error_is_bpe/_tim/_mrg/_deff()` classify a code so a caller can tell "the asset is
  malformed" from "my buffer is wrong". Unrecognised values yield `"Unknown"` and are
  never out-of-bounds reads.
- Across the engine boundary the precise code becomes a coarse `lod::Status::Decode*` and
  the precise name goes into the log line (error-handling.md §4.1). The C++ wrapper keeps
  the precise code (`psx::Error`), so nothing is lost before that mapping.

## 5. API surface

39 exported functions plus 3 `static inline` header predicates
(`psx_bytes_is_valid`, `psx_writable_bytes_is_valid`, `psx_mrg_entry_is_virtual`), which
are deliberately not part of the ABI.

### 5.1 Version and error

| Function | Contract |
|---|---|
| `psx_version_major/minor/patch` `psx_version_string` | Library release of the *loaded* library |
| `psx_abi_version` | ABI number; compare against `PSX_ABI_VERSION` from the header |
| `psx_error_name` `psx_error_message` | Name / one-line message for any code |
| `psx_error_is_bpe` `psx_error_is_tim` `psx_error_is_mrg` `psx_error_is_deff` | Range predicates |

### 5.2 Format entry points

| Function | Notes |
|---|---|
| `psx_bpe_is_bpe` `psx_bpe_declared_size` | Cheap sniff; header-level size |
| `psx_bpe_decompress(src, dst, out_written)` | Caller-supplied destination; `dst.capacity` is the ceiling |
| `psx_bpe_compressed_size` `psx_bpe_compress` | Tooling encoder (identity dictionary); exists for round-trip tests and fixture generation — the reference format has no packer |
| `psx_tim_is_tim` `psx_tim_parse` `psx_tim_clut_entry_count` `psx_tim_image_init` | `psx_tim_image_t` is caller-allocated and plain (§5.4) |
| `psx_archive_is_mrg` `psx_archive_is_deff` | Magic sniff, no allocation |
| `psx_archive_mrg_entry_count(data, sector_aligned, out_count)` | Two-pass reading needs the count first; `sector_aligned` is `int` 0/1 |
| `psx_archive_mrg_parse(data, sector_aligned, entries, entry_capacity, out_view)` | **No allocation**: the caller's `entries` array receives the table |
| `psx_mrg_view_at` `psx_mrg_view_slice` `psx_mrg_entry_is_virtual` | Entry access and bounds-checked slicing from a view |
| `psx_archive_deff_subfile_count` `psx_archive_deff_parse` `psx_deff_view_at` `psx_deff_view_slice` | Same pattern for DEFF |

`sector_aligned` stays a caller-supplied flag: sector scaling is a property of the *path*
a container was read from (`DRGN*.BIN`), which `psx` does not know. The engine decides it
(`src/engine/src/resources.cpp`).

### 5.3 Opaque handles (the allocating path)

`psx_mrg_table_t` / `psx_deff_table_t` exist for callers that do not want to size an
entry array themselves:

```c
psx_mrg_table_t* table = NULL;
psx_error_t err = psx_mrg_table_parse(container, /*sector_aligned*/ 0, &table);
if (err == PSX_OK) {
  psx_mrg_view_t view;
  psx_mrg_table_view(table, &view);          /* borrows the table's entries */
  psx_bytes_t entry;
  err = psx_mrg_view_slice(view, container, index, &entry);
  psx_mrg_table_destroy(table);              /* NULL is legal */
}
```

- The out-parameter is left untouched on failure: a failed parse never leaves a
  half-built handle behind.
- `count`/`virtual_count`/`view` on a NULL handle answer 0 / empty / `PSX_OK` rather than
  crashing, so a moved-from or never-created handle is safe to query.
- `psx_mrg_table_view` copies a view out; the view is valid only while the table is alive
  and unmodified.
- Both usage styles are first-class: the engine uses the allocation-free path (it keeps
  tables in `std::vector`), embedders that prefer C ownership use the handles.

### 5.4 Structs in headers

`psx_tim_image_t`, `psx_tim_rect_t`, `psx_mrg_entry_t`, `psx_mrg_view_t`,
`psx_deff_entry_t`, `psx_deff_view_t`, `psx_bytes_t`, `psx_writable_bytes_t` are plain
structs of fixed-width types and `size_t`. They are caller-allocated and copyable;
changing any of them is an ABI break (§6).

## 6. Versioning and ABI

Two independent axes, both in `psx/version.h`:

| Axis | Macro | Bumped when |
|---|---|---|
| Library release | `PSX_VERSION_MAJOR/MINOR/PATCH`, `PSX_VERSION_STRING` | Any shipped change (behaviour, new format, fix) |
| ABI | `PSX_ABI_VERSION` | Any change to a struct layout, a function signature, or an error code's numeric value |

- CMake parses the header (it does not duplicate the numbers) and sets
  `VERSION = MAJOR.MINOR.PATCH`, `SOVERSION = PSX_ABI_VERSION`.
- `PSX_ABI_IS_COMPATIBLE(runtime_abi)` lets a loader detect a header/library mismatch at
  startup instead of misreading a struct.
- Adding a function inside a domain range is minor; changing/removing one, or moving a
  number, is an ABI bump.

## 7. C++23 wrappers

`psx::cpp` exists so engine and tool code keeps writing C++23 idiom while the shipped
library is C. The rules are deliberately narrow:

- **Delegate, never reimplement.** Every wrapper function is one C call plus a
  conversion. No wrapper adds validation, defaults, or caching of its own.
- **Types.** `std::span<const uint8_t>` in, `std::expected<T, psx::Error>` out
  (`psx::Result<T>`). `psx::Error` is an `enum class` whose enumerators are
  `static_assert`-checked against the C constants, so the mirror cannot drift silently.
- **Names.** Wrapper names follow the C++ conventions the engine already used
  (`IsBPE`, `DeclaredSize`, `DecompressBPE`, `ParseMRG`, `MRGEntryData`, `CLUTEntryCount`,
  `Error::BPEMagic`), which is why `src/engine/src/resources.cpp` compiled unchanged
  after the migration: the wrappers preserve the pre-migration C++ signatures.
- **Ownership.** `psx::bpe::DecompressBPE` allocates the `std::vector` the C core fills
  and enforces the ceiling *before* allocating (a hostile header cannot cause a large
  allocation). `psx::archive::MRGTable`/`DEFFTable` own their entry arrays as
  `std::vector<psx_mrg_entry_t>` — the C layout, so `View()` is a pointer handoff with no
  copy. Payload spans still borrow the caller's buffer.
- **RAII.** `psx::archive::MRGTableHandle`/`DEFFTableHandle` are move-only owners of the
  opaque C handles; move-assignment destroys what it replaced, destruction calls
  `psx_*_table_destroy`, and copy is deleted (compile-time asserted in the tests).
- **Exceptions.** The wrappers throw nothing; they are built with
  `-fno-exceptions`/`-fno-rtti` like the core (`psx/tests/CMakeLists.txt`), so the whole
  `psx` surface stays exception-free by contract under either project-wide policy.
- **Escape hatch.** `Handle()` on the RAII wrappers exposes the raw pointer while the
  wrapper keeps ownership, so a caller can use a C entry point the wrappers have not
  wrapped.

## 8. Consumers and tests

| Target | Language | Proves |
|---|---|---|
| `sc_tests_psx` (groups `psx`, `bpe`, `tim`, `xa`, `archive`, `wrapper`, `clinkage`) | C++23 | Format behaviour (52 cases incl. the authentic `chester.tim` lane and the 17-sector `xa_adpcm.bin` differential), wrapper delegation/move/RAII/error propagation, C-header usability |
| `sc_psx_c_consumer` (`psx/tests/c_consumer/main.c`) | **C17 only** | The C API stands alone: compiled by the C compiler with `-Wall -Wextra -Wshadow -Wconversion -Wmissing-prototypes -Wstrict-prototypes -Wpointer-arith -Wwrite-strings -Wredundant-decls -pedantic`, 294 checks covering every entry point, every documented error code, NULL-argument rejection, and the malformed-input contract. It includes only `<stdio.h>`, `<string.h>` and `psx/psx.h`, so anything the umbrella omits cannot be exercised from C at all — `psx/tests/test_c_linkage.cpp` is the only other target that compiles the umbrella header. Its XA section covers all four XA entry points, both XA error codes, NULL rejection, the 2327/2328 truncation boundary, an undersized destination and stream independence |
| `psx.abi.symbols` (`psx/tests/abi_symbols.cmake`) | CMake + `nm` (ELF/Mach-O) or the PE export table via `objdump -p`/`dumpbin /EXPORTS` | Exactly the documented symbols are exported, no undocumented export, no C++ symbol in either direction (runs for shared and static; the method is chosen from the artifact, so a cross-built `.dll` audits too) |
| `psx.differential` (`-DPSX_ENABLE_DIFFERENTIAL_TEST=ON`) | C++23 | Old-vs-new equivalence over a deterministic corpus (§10) |

The pure-C consumer is the completion criterion for "this is a C library": if it builds
and passes, no C++ assumption has leaked into the headers or the linkage.

`psx` also still builds and tests standalone as its own project
(`cmake -S psx -B build-psx && cmake --build build-psx && ctest --test-dir build-psx`),
which is the extraction path `psx-library.md` requires — and the install/export rules are
consumable rather than aspirational: `cmake --install` plus
`find_package(psx REQUIRED)` yields working `psx::psx` and `psx::cpp` targets for an
unrelated project (verified with a C17 and a C++23 consumer; the packaging defect this
check exposed is recorded in `docs/reports/psx-c17-migration.md` §3).

## 9. Zero-dependency guarantees

The core's entire external dependency surface, verified by `psx.abi.symbols` plus
`nm -u` / `otool -L` on the built library:

```
malloc  calloc  free  __stack_chk_guard  __stack_chk_fail
```

i.e. libc only. No C++ runtime, no third-party library, no engine or SDL/Qt symbols; the
library links against `libSystem` alone on macOS. This holds because:

- the sources are C — there is no C++ runtime to link;
- allocation is limited to the two handle types (`malloc`/`calloc`, matched by
  `free` in the destroy functions);
- symbol visibility is hidden, so nothing unintended is published.

The audit is a test rather than a review note, because "no dependencies" is the property
that makes this library reusable and it is the property most easily lost by one
convenient `#include`. Windows runs the equivalent check through the linker's own
export flow; the CMake audit skips with a visible message there rather than passing
silently.

## 10. Migration record and differential verification

- `psx/migration/legacy/` holds the last C++-only revision (namespace `legacy_impl`,
  byte-identical to what it replaced apart from the include-path/namespace rename). It is
  a fixture: never installed, never exported, never referenced by the engine, and built
  only when `PSX_ENABLE_DIFFERENTIAL_TEST=ON`.
- `psx/migration/differential/test_differential.cpp` runs both implementations over the
  same deterministic corpus (fixed-seed LCG, no `<random>`) and compares booleans, error
  codes, decoded bytes, entry tables, slice offsets and every parsed TIM field. The corpus
  combines valid fixtures, truncated prefixes, per-byte flips, random noise, and
  **systematic boundary probing** — every 2- and 4-byte window of each fixture loaded with
  values sitting exactly on the thresholds the formats test. Boundary probing is not
  decoration: random and flip mutations alone were measured to miss an off-by-one in a
  `blockSize > 0x800` check and one in a section-size comparison, both of which the
  boundary pass catches.
- The harness was validated by injecting faults into the fixture (a virtual entry
  inheriting the wrong size, a shifted block-size limit, a shifted section-size limit):
  each injection produced `MISMATCH` failures, and reverting restored a clean run.
- Cost: sub-second in a normal build, ~2 minutes under ASan+UBSan for the whole suite —
  which is why the differential target is opt-in rather than always-on.
- The C++ originals were removed from `psx/src/` only after the differential run, the
  41-case wrapper suite and the C consumer all passed.

## 11. Adding a decoder to this library

1. Write the C header first: `psx/include/psx/<format>.h`, `PSX_API` on every public
   function, Doxygen on every declaration (purpose, `@param` with in/out and ownership,
   `@return`/`@errors`, lifetime of anything borrowed, `@note Thread:`).
2. Reserve an error range in `psx/error.h`, add the name/message rows, and add a range
   predicate if the domain is new.
3. Implement in `psx/src/<format>.c` on top of `psx/src/reader.h`; every read is
   bounds-checked, every declared length is validated against the real buffer before use,
   and nothing allocates except an explicit `_table_parse`.
4. Add the C++ wrapper header under `psx/include/psx/cpp/` as a pure delegate, and assert
   its error values against the C constants.
5. Add cases to the C++ group, add C checks to `psx/tests/c_consumer/main.c`, add the
   symbol to `_psx_required` in `psx/tests/abi_symbols.cmake`, and add the fixture builder
   to the differential corpus so old-vs-new coverage extends to the new format.
6. Bump `PSX_ABI_VERSION` if any struct, signature or code value changed (§6).

## 12. Open items

- Windows/Linux **execution** and a native-toolchain run: the Windows build and its export
  table are audited through a MinGW-w64 cross-build, every core TU compiles for a Linux ELF
  target, and both PE row shapes parse (`psx-c17-migration.md` §2, findings 12–13). Still
  unverified here: running a non-macOS binary, a Linux link against real glibc headers, and
  MSVC's own output. Needs a three-platform CI matrix; the audit skips with a visible message
  when neither `objdump` nor `dumpbin` is available, so an unconfigured runner cannot pass
  silently.
- `psx::cpp` covers the four implemented formats only. As GTE/GPU/SPU/CD-ROM modules land
  (psx-library.md §2), decide per module whether a C++ wrapper is worth having at all —
  the wrappers exist for the engine's convenience, not for symmetry.
- Large-container streaming is unaffected by the language change and remains open
  (resource-management.md §6).
