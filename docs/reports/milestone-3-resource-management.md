# Milestone Report — Resource Management, Asset Discovery and PSX Data Integration

> **Addendum (later the same day).** The `psx` library described throughout this report
> was subsequently migrated from C++23 to a **C17 core with header-only C++23 wrappers**.
> The C++ surface names cited here (`psx::bpe::DecompressBPE`, `psx::archive::ParseMRG`,
> `psx::Error`, `psx::Result<T>`) remain the names engine code calls — they now live in
> `psx/include/psx/cpp/`; the byte-level findings, error semantics and test expectations
> are unchanged and are now enforced by a differential corpus against the implementation
> described in this report. See `docs/reports/psx-c17-migration.md` and
> `docs/architecture/psx-c-api.md`. Paths like `psx/src/byte_reader.h` and the `.cpp`
> sources listed below are the pre-migration layout; the equivalent today is
> `psx/src/reader.h` and `psx/src/*.c`, with the originals preserved as the fixture under
> `psx/migration/legacy/`.

Scope label: the user's "Milestone 3" brief. In `docs/architecture/roadmap.md`
terms this is the **front part of M6 — Asset Pipeline** (resource discovery,
ownership, `psx::bpe` / `psx::archive` / `psx::tim`), plus the first runtime
surface for it. **No renderer, audio device or gameplay work was started**
(no `SDL_CreateGPUDevice`, no SPU/XA, no scene code).

Reference repository: `~/Documents/GitHub/Severed-Chains` at
`ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb` (observed read-only; working tree
clean, nothing in it was modified).

---

## 1. Verified starting state

The M2 report's claims were checked against the tree before building on them.

| Claim | Actual |
|---|---|
| `scengine` / `scgame` / `lodsc` / `sc_tests_engine` / `psx` build | Confirmed (fresh configure + build) |
| 46 engine cases / 18–21 CTest entries green | Confirmed (46 cases, 21 CTest entries at start) |
| `psx` was a stub (single `psx.cpp`, no formats) | Confirmed — `psx/` contained a placeholder target and a `PSX_TEST` harness with one smoke case |
| `lod::fs` had text read/write only | Confirmed (`ReadFileText` / `WriteFileText`); no binary path, no size ceiling |
| `Status` had no decode-domain codes | Confirmed (comment reserved the 200s) |
| `psx` linked the *switchable* exception interface | Confirmed as already fixed in M2: `psx` links `sc::exceptions_off` (verified again below) |

## 2. What was implemented

### 2.1 `psx` format modules (new, ~1 000 lines of implementation)

| File | Content |
|---|---|
| `psx/include/psx/error.h` | `psx::Error` (grouped codes: 1–2 generic, 50s BPE, 80s TIM, 110s MRG, 140s DEFF), `psx::Result<T>`, `psx::ErrorName()` |
| `psx/src/byte_reader.h` | Internal `psx::detail::ByteReader`: bounds-checked, explicitly little-endian `U8/U16/I16/U32/Slice/Fits` |
| `psx/include/psx/bpe.h`, `psx/src/bpe.cpp` | `IsBPE`, `DeclaredSize`, `DecompressBPE(data, maxOutputBytes)`, `CompressBPE` (tooling); adaptive per-block dictionary, LIFO expansion, word-aligned block cursor; allocation bounded by the caller's ceiling; block/dictionary/expansion/size-mismatch failures are explicit errors |
| `psx/include/psx/tim.h`, `psx/src/tim.cpp` | `IsTIM`, `ParseTIM`, `CLUTEntryCount`, `BPP`, `Rect`, `Image` (borrowed spans); validates depth code, section sizes, section extents, rect positivity and rect-vs-section extent; reports trailing bytes |
| `psx/include/psx/archive.h`, `psx/src/archive.cpp` | `IsMRG/ParseMRG/MRGEntryData`, `IsDEFF/ParseDEFF/DEFFEntryData`, `MRGTable/DEFFTable` (index-only, no borrowed spans); virtual-entry resolution, duplicate-offset collapse, optional sector scaling, monotonicity/range checks |
| `psx/include/psx/psx.h` | Umbrella header; states which modules exist and which do not |
| `psx/CMakeLists.txt` | Compiles the three modules + tests; replaced the stub |

### 2.2 Engine resource layer (`lod::res`, new)

`src/engine/include/lod/resources.h` + `src/engine/src/resources.cpp` (~980 lines):

- `RootInfo` / `ValidateRoot` — root markers (`SECT/`, `version` + its value,
  `DRGN*.BIN` count, `SUBMAP/`, `OVL/`, `XA/`) and human-readable notes.
  `ValidateRoot` is **total**: absence is reported as data, only an
  unlistable directory is an error.
- `ResourceKind` + `DetectKind` + `KindName` — header-only classification
  (BPE by magic at 0x4; MRG/DEFF/TIM by magic at 0x0).
- `ResourceBlob` — the single owner: move-only, `Bytes()` span into its own
  buffer, deterministic `Release()` + destructor.
- `ResourceManager` — `Open` (policy: root must look like a game tree), `Load`
  (path-confined, size-capped), `Scan` (sorted, symlink-safe, limit-truncated),
  `Inspect` / `InspectBytes` (validate through the matching psx parser),
  `IsSafeRelativePath`, `Validate`. `Options` carries the per-file ceiling,
  scan limit, inspect decompress limit and an optional `Logger*`.
- `ToStatus(psx::Error)` — coarse mapping into the new `Status::Decode*` codes,
  with the specific `psx::Error` name logged alongside.

### 2.3 `lod::fs` / `Status` additions

- `fs::ReadFileBytes(path, maxBytes)` — binary read where the ceiling **rejects**
  (`Status::IOTooLarge`), checked against the reported size before allocating and
  again against the bytes actually read.
- `fs::ReadFilePrefix(path, maxBytes)` — binary read where the ceiling
  **truncates** (used by scan header sniffing; a short file is not an error).
- `Status`: `IOTooLarge` (103) plus the reserved decode domain now realized —
  `DecodeHeader`, `DecodeTruncated`, `DecodeSizeMismatch`, `DecodeUnsupported`,
  `DecodeCorrupt` (200–204), with `StatusName()` coverage.

### 2.4 Runtime integration (host)

`config.txt` gained a `resource_dir` key (`Config::resourceDir`, parsed,
validated, round-tripped, omitted from the file while unset), so the resource
root is configurable through all three documented channels:
`--resources` → `config.txt resource_dir` → resolved default (`SC_RESOURCE_DIR`).
The winning source is logged, and asset commands never create or rewrite the
config file.

`src/host/src/main.cpp`: `--resources <dir>`, `--validate-assets`,
`--inspect <path>` (repeatable), plus documented exit codes (0 ok, 4 root
unusable, 5 a resource failed). These commands initialize no window, audio or
game subsystem — they are pure filesystem operations, so they run in CI and on a
machine with no playable data. Report lines go to stdout and are flushed at
section boundaries so they stay ordered against the logger's stderr.

Sample run (synthetic tree, fixtures under `/tmp/sc-assets`):

```text
root: /tmp/sc-assets
  SECT/ present (1 DRGN archive(s))
  version = '5'
  optional dirs: SUBMAP yes OVL yes XA yes
discovered 8 file(s), 2827 B in 6 director(ies)
  bpe: 1
  deff: 1
  mrg: 2
  raw: 2
  tim: 1
validated 8 resource(s), 1 failure(s):
  SUBMAP/broken.tim: DecodeCorrupt

SUBMAP/x.tim: TIM 4bpp, image 4x2 (16 B), 3 B trailing, clut 4 entries (8 B)
DRGN0.BIN: MRG 1 entries (0 virtual, 1 readable, 4 B payload, sector-aligned offsets)
SECT/DRGN0.BIN/5546/2: BPE 540 B -> 9 B
```

## 3. Architectural decisions

1. **Index-only containers.** `MRGTable`/`DEFFTable` store offsets, never spans,
   and `MRGEntryData`/`DEFFEntryData` are the checked slice. A table cannot dangle
   when its buffer moves, and "does this entry fit the container" becomes an
   answerable question instead of a latent out-of-range access.
2. **Sector alignment is a path property, so it is the caller's flag.** `psx`
   cannot know a container's path; `ParseMRG(data, sectorAligned)` keeps the
   library generic, and `lod::res` decides it from the first path component
   matching `DRGN(?:0|1|2[1234])?.BIN` (case-insensitively, because the tree also
   lives on case-insensitive volumes). Tested both ways with identical bytes.
3. **`Result<RootInfo>` for absence, policy in `Open`.** Telling an operator
   "the tree has no `SECT/`" needs the populated info, not a bare code. So
   `ValidateRoot` reports and `Open` decides. (This also fixed a real bug —
   see §5.)
4. **No cache, no handles, no refcounts.** `Load` returns an owning blob and the
   manager keeps nothing. Every speculative mechanism listed in
   architecture/resource-management.md §2 has a concrete trigger recorded there
   instead of being built now.
5. **Two binary reads with different ceiling semantics** (`ReadFileBytes`
   rejects, `ReadFilePrefix` truncates). One helper would have been wrong for one
   of the two callers: sniffing must not fail on a 4 MiB asset, and loading must
   not return a truncated file that a decoder then calls "corrupt".
6. **Coarse status across the boundary, precise error in the log.**
   `psx::Error` (24 codes) maps to five `Status::Decode*` codes; failure lines
   carry path, size, `psx::ErrorName` and the mapped status. Decoder granularity
   survives for humans without leaking `psx` codes into engine control flow.
7. **Validation reports honestly or not at all.** Headerless data inspects as
   `raw` with its size only; an oversized declared BPE output is reported, not
   inflated; an entry outside its container is a failure, not a warning.
8. **`CompressBPE` is tooling, not parity.** The reference has no packer. It
   exists so round-trip properties are testable and so fixtures cannot drift from
   the decoder (the engine test builds its BPE fixture with it).

## 4. Build and test results (all executed on this machine)

| Configuration | Result |
|---|---|
| Default (`RelWithDebInfo`, exceptions ON) | Build clean; **67** engine cases, **32** psx cases, 0 failures; **23/23** CTest entries |
| `-DSC_WERROR=ON` | Build clean, 0 diagnostics; 67 + 32 cases pass |
| `-DSC_ENABLE_EXCEPTIONS=OFF` | Build clean, 0 diagnostics; 67 + 32 cases pass (engine compiles with exceptions disabled; Qt-backed tools stay documented as excluded — M2 note) |
| `-DSC_ENABLE_ASAN=ON -DSC_ENABLE_UBSAN=ON` (Debug) | Build clean, 0 diagnostics; 67 + 32 cases pass; **no sanitizer reports** |
| `psx` standalone (`cmake -S psx -B <dir>`) | Builds and its 32 cases pass with no engine/SDL/Qt present (independence rule) |
| `compile_commands.json` flag audit | `psx/{bpe,tim,archive}.cpp`: `-fno-exceptions -fno-rtti`; `src/engine/src/{app,resources}.cpp`: neither (permitted-but-discouraged policy) |

Case counts by group:

| Suite | Groups (cases) |
|---|---|
| `psx` (32) | `bpe` 9, `tim` 11 (incl. the opt-in authentic-asset case), `archive` 10, `psx` 2 |
| engine (67) | `res` 15, `fs` 15 (4 new for the binary reads), `config` 10, `app` 11, `platform` 7, `diagnostics` 2, `log` 2, `status` 2, `frame` 1 |
| CTest (22) | `engine.*` 17, `psx.{psx,bpe,tim,archive}` 4, `host.*` 5 (one setup fixture) |

Authentic-data verification: `psx/tests/test_tim.cpp` asserts the full measured
layout of `patches/replacements/chester.tim` (4640 B, 4bpp, CLUT rect
(0,112,16,16), 512 B CLUT, image section at 0x214, image rect (0,0,16,112),
0xff4 B image, 12 B trailing) when `SC_PSX_ASSET_DIR` points at it; without the
variable the case reports `SKIP` and passes (no game data ships here). Run with
the variable set: **11/11 TIM cases pass against the real file.**

```text
$ SC_PSX_ASSET_DIR=~/Documents/GitHub/Severed-Chains/patches/replacements ./build/psx/tests/sc_tests_psx tim
11 case(s) ran, 0 failed
```

## 5. Defects found and fixed while implementing

- **Dereferencing an `unexpected` value.** `ResourceManager::Open` logged
  `info->notes` inside the `if (!info)` branch. Discovered by running the new CLI
  against a nonexistent root: it aborted with `std::bad_alloc` (UB). Fixed by
  making `ValidateRoot` total (§3.3) so notes exist on the success path, and by
  having `Open` log from the value it actually holds. Regression-covered by
  `res.validate_root_reports_absent_paths_without_failing` and
  `res.open_requires_a_sect_root`, and by the CLI path itself.
- **Scan sniffed nothing.** Header detection used `ReadFileBytes(full, 16)`,
  which *rejects* files larger than 16 bytes, so every real asset classified as
  `raw`. Fixed by adding `fs::ReadFilePrefix` (the truncating contract) and
  asserting detection in `res.scan_lists_sorted_entries_with_sizes_and_kinds`.
- **Underflowing suffix read** in the DRGN-name matcher for names shorter than 8
  characters (`size() - 4` on `size_t`). Guarded and commented.
- **Out-of-range container entries were a warning.** `Inspect` now fails with
  `DecodeCorrupt`, because a container whose entries point outside itself is
  malformed, not quirky.

## 6. Naming and policy work in this increment

- **Java citations moved out of code** (guidelines/cpp.md §2 forbids `.java`
  references in code comments). 38 citations were rewritten into direct behaviour
  statements and relocated, with per-field evidence and deviation rationale, to
  **research/psx-formats.md**. `grep -rn "\.java" src psx tests` is now 0.
- **Struct data members → camelCase** (the documented rule; cpp.md §2):
  `RootInfo`, `ResourceEntry`, `ScanResult`, `Inspection`, `Options`,
  `MRGEntry`/`MRGTable`/`DEFFEntry`/`DEFFTable`, `Rect`, `Image` all use
  camelCase members (`drgnBins`, `maxFileBytes`, `imageRect`, `virtualSize`), and
  private fields use the trailing-underscore form (`root_`, `opts_`, `path_`,
  `contents_`, `contentKind_`, `buffer_`).
- **Acronym policy applied to the new API**: `BPE`, `MRG`, `DEFF`, `TIM`, `CLUT`,
  `BPP` are uppercase in PascalCase positions (`DecompressBPE`, `MRGEntryData`,
  `TIMBPP`, `CLUTEntryCount`, `ResourceKind::MRG`), camelCase only when leading a
  variable (`clutRect`, `drgnBins`). Truncations stay mixed-case (`SDLWindowPtr`,
  `MsgLevel`), per the documented exception.
- **Doxygen on every new public API** (purpose, `@param`, `@return` with the
  exact failure codes, ownership/lifetime for borrowed spans, `@note Thread:`).
- **Naming audit before completion** (the M2 feedback item: audit every new or
  changed identifier instead of shipping violations). The sweep found and fixed,
  in this increment's own code: `Literal`/`Trailing` parameters, the `B0..B3`
  locals in the byte reader, PascalCase lambda variables (`EqualIgnoreCase`,
  `IsSpace` → `equalIgnoreCase`, `isSpace` — camelCase is both the documented rule
  and the M2 code's practice), the cryptic `Rect r` / `Section::r` (→ `rect`), and
  `Image::image`, which repeated its enclosing type name (→ `imageData`).
  `StartTime`-style false positives ("Tim" inside "Time") were checked and left.
- **Second audit round — the sweep was too narrow, so it is now a tool.** The
  sweep above looked at types and functions in code. It missed **enum values and
  constants**, and it did not look at the names *documentation prescribes*, so
  those shipped mis-cased. Owner-flagged, then fixed:

  | Where | Was | Now |
  |---|---|---|
  | `psx/src/archive.cpp` (function-scope constant) | `maxSector` | `MaxSector` |
  | `src/engine/src/resources.cpp` (constant) | `XaDir` | `XADir` |
  | `lod::res::RootInfo` member (+ uses) | `hasXaDir` | `hasXADir` |
  | `src/engine/{include/lod/platform.h,src/platform.cpp}` | `SdlUp`, `SdlText`, `SdlMinWindowSize` | `SDLUp`, `SDLText`, `SDLMinWindowSize` |
  | `docs/research/psx-formats.md` (6 quoted codes) | `Error::BpeSizeMismatch`, `Error::BpeOutputTooLarge`, `Error::BpeExpansionOverflow`, `Error::MrgEntryOutOfRange`, `Error::DeffOffsetsBad`, `Error::DeffTableTruncated` | `BPE…`/`MRG…`/`DEFF…` (matching the code) | <!-- naming-allow: the 'Was' column quotes the old spellings on purpose -->
  | `docs/architecture/error-handling.md` (prescribed code) | `DecodeUnsupportedBpp` | `DecodeUnsupportedBPP` |
  | `docs/guidelines/cpp.md` (stale type name) | `SdlWindowDeleter` | `SDLWindowDeleter` (the real alias in platform.h) |

  To make the audit non-optional, **`tools/check_naming.py`** now enforces the
  policy mechanically and runs as CTest entry **`repo.naming`**: it splits
  identifiers into camel/Pascal humps and compares each against an acronym
  dictionary, so `Time`/`Timer` never trip the `TIM` rule, snake_case names (file
  names, test groups, string ids) are out of scope, a leading lowercase acronym is
  accepted only in variable position, and Java/third-party names are exempt. Docs
  are checked too — `::`-qualified names everywhere (Java is dotted, so a
  `::`-qualified name is always ours) and every backticked identifier in
  `docs/architecture/**`, `docs/guidelines/**` and `README.md`, because a
  mis-cased name written in a doc becomes mis-cased code. Deliberate quotations
  and rule counter-examples carry a `<!-- naming-allow: reason -->` marker
  (AGENTS.md's "Wrong" column, the Java classes roadmap.md/testing.md cite, the
  M2 report's record of the `IoOpen`→`IOOpen` rename). Python 3 is an optional
  development host tool here: CMake skips the test with a status message when it
  is absent, and no project target depends on it.
- **Formatting**: `clang-format --dry-run -Werror` clean across the **whole tree**
  (`find . -name '*.cpp' -o -name '*.h' | grep -v ./build | xargs clang-format
  --dry-run -Werror` returns nothing), including two pre-existing files that were
  not formatted before this increment (`cmake/probes/rtti_probe.cpp`,
  `psx/tests/psx_test.h`).

## 7. Deviations from the original readers (deliberate, documented)

Recorded in code as behaviour, in research/psx-formats.md as provenance:

1. **BPE size mismatch is an error.** The original allocates its output from the
   declared size and never compares it with what the blocks produced: a short
   stream yields silent zero padding, an over-long one writes out of bounds. The
   port returns `BPESizeMismatch`.
2. **TIM CLUT section size is read as a full u32.** The original reads it as a
   u16 when locating the image section, truncating any section above 0xffff.
3. **TIM/MRG/DEFF validate what the originals trust** (depth codes, section
   extents, rect extents, table fit, monotonic subfile starts, entry ranges).
   The originals fail later, in a loader, or render garbage.
4. **Bounds checking everywhere.** The originals index aggressively once the
   magic matched; every read here goes through a checked cursor, so malformed
   input produces an `Error` and nothing else.
5. **`trailingBytes` is reported, not rejected** (matches original behaviour on
   the authentic asset).
6. **24 bpp rect extent is not enforced** — the original does not define the
   pitch, so it is documented as unverified rather than guessed.

## 8. Unresolved issues / open items

1. **No retail game data available.** BPE/MRG/DEFF are verified against the
   reference readers and synthetic fixtures; the TIM path is additionally
   verified against one authentic non-retail asset. A pass over a real
   `DRGN0.BIN` / `SUBMAP` tree is still owed. Consequence: some *structural*
   rules (e.g. how far a real MRG entry may legitimately reach) are as strict as
   the reference-verified data supports, not as strict as retail data might allow.
2. **Unimplemented `psx` modules** (unchanged): `gte`, `gpu`, `spu`, `xa`,
   `sshd`, `sssq`, `cdrom`, `tmd`, `anm`, `tag`, DABA. `psx/include/psx/psx.h`
   and psx-library.md §2.1 say so explicitly.
3. **TIM is parse-only.** No pixel decode to RGBA, no DABA stitching, no GPU
   upload layout — those belong to the rendering milestone.
4. **Extraction is not implemented.** `lod::res` reads a tree that something else
   must produce; today that "something else" is the reference Java unpacker or a
   hand-built tree. ISO reading, unpacking, SMAP/OVL splitting and the per-file
   patcher set remain M6 work.
5. **`version` mismatch policy.** The reader reports the value; deciding whether
   the runtime refuses, warns or triggers re-extraction belongs to the extraction
   milestone.
6. **Large-container streaming.** `MRGEntryData` slices a resident buffer; reading
   a sub-range without materializing the container is the likely first change to
   the ownership model once real multi-megabyte containers matter.
7. **Carried over from M2, still open:** struct data members in the M1/M2 value
   types (`AppContext`, `RunOptions`, `LoopStats`, `Config`, `fs::Paths`,
   `FrameEvents`) and PascalCase *parameters* throughout M1/M2 code still deviate
   from the documented camelCase rule. New code in this increment follows the
   documented rule; the mechanical sweep across M1/M2 awaits an owner decision
   (renaming `Config`/`FrameEvents` members touches config parsing and every
   engine test).
8. **Platform coverage.** Everything above was executed on macOS/Apple Silicon
   (AppleClang 21, SDL 3.4.18). Linux/Windows lanes and the 3-platform CI matrix
   remain outstanding; `ReadFileBytes`'s short-read path and the POSIX-only
   permission tests are the parts most likely to behave differently elsewhere.
9. **`progress.md` was missing** although AGENTS.md refers to it ("Record its
   commit hash in `progress.md` before starting research"). Created now as a
   forward-looking log — it records this increment and later ones; it does not
   backdate history that was never recorded.

## 9. Files

**New:** `psx/include/psx/{error,bpe,tim,archive}.h`, `psx/src/{byte_reader.h,bpe.cpp,tim.cpp,archive.cpp}`,
`psx/tests/{test_bpe,test_tim,test_archive}.cpp`,
`src/engine/include/lod/resources.h`, `src/engine/src/resources.cpp`,
`tests/engine/test_resources.cpp`, `docs/research/psx-formats.md`,
`docs/architecture/resource-management.md`, this report.

**Modified:** `psx/include/psx/psx.h`, `psx/CMakeLists.txt`, `psx/tests/CMakeLists.txt`,
`psx/tests/psx_test.h` (case-name filter), `psx/tests/test_smoke.cpp` (real assertions
+ suite `main`), `src/engine/include/lod/{fs,status,config}.h`,
`src/engine/src/{fs,resources,config}.cpp`, `src/engine/CMakeLists.txt`,
`src/host/src/main.cpp`, `tests/{CMakeLists.txt,engine/CMakeLists.txt,repo/CMakeLists.txt}`,
`tests/engine/{test_fs.cpp,test_config.cpp}`, `tools/check_naming.py`,
`docs/architecture/{psx-library,roadmap,engine-design}.md`,
`docs/research/asset-pipeline.md`, `README.md`, `progress.md` (created — AGENTS.md
references it and it did not exist), `cmake/probes/rtti_probe.cpp` (formatting only).
