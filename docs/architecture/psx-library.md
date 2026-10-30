# PSX Library — Reusable PlayStation 1 Technology

**Decision: an independent C17 library (CMake target `psx`, exported `psx::psx`, C symbol
prefix `psx_`) with an optional header-only C++23 wrapper layer (`psx_cpp`, exported
`psx::cpp`) is part of the native architecture.** It is the home for
PS1-specific technology that is reusable across PlayStation game projects
(this project, and potentially e.g. *Legend of Legaia*), kept strictly
separate from Legend of Dragoon game logic, Qt, and SDL3 GPU rendering.
It is **not** a PS1 emulator: it provides *semantics* (math, decoding,
state machines) and one software reference rasterizer — not CPU, timing,
or full-system emulation.

The C core is the ABI and the C++ wrappers are a thin delegate layer over it:
language, ABI, symbol-export and wrapper rules are in **psx-c-api.md** (that document is
authoritative for the API surface; this one is authoritative for what belongs here).

**Identity (owner decision):** the library is **prefix-free — no Sc/SC/sc, no LoD/LOD/lod
in any of its names** (CMake target `psx`, C++ namespace `psx::`, C prefix `psx_`, source
dir `psx/` at the repository root). It is a separate project identity from
Severed Chains today and may be extracted to its own repository in the
future; the root-level placement keeps that extraction mechanical.
**Build form:** shared by default (`PSX_SHARED=ON`, the migration brief pins the core as a
shared C library); `PSX_SHARED=OFF` produces the static form for embedders. Both are
audited identically (psx-c-api.md §9).

## 1. Boundary

| Belongs in `psx/` (reusable PS1 tech) | Does NOT belong in `psx/` |
|---|---|
| GTE fixed-point math (geometry transform engine) | LoD game logic (characters, combat, additions, story, registries content) |
| PS1 GPU/GS *semantics*: primitive set, ordering table, top-left rule, depth test, blending modes (incl. B+1/4F), PS1 projection (H division), texture modes (fixed/CLUT/chroma key), per-primitive constant depth | The software GS's *render-engine integration* (batching, render targets, SDL3 GPU) — that's `scengine::render` (rendering-sdl3-gpu.md) |
| PS1 GPU *software reference rasterizer* (port of the reference `Gpu`/`Graphics`) — the parity oracle and the "legacy"/"VRAM" render modes | The fictional **Dabas handheld** VM (not PS1 hardware — it's a game mechanic inside the game; lives in `libs/dabas`, dig-dabas.md §5) |
| PS1 SPU voice-model semantics (voice registers, FM/noise/reverb, key on/off, 4-bit PCM, pitch/ADSR) — the model, not a thread | The reference's 120 Hz music *synth* scheduling and its audio-thread design (`scengine::audio` owns threading) |
| XA ADPCM decoding (PS1 CD audio format: 4-bit ADPCM, stereo/mono, block alignment, pre-skip) | Game-specific audio assets/sequences content |
| SShd / SSSq (PS1 SPU sound/sequence headers — generic PS1 audio formats) | The LoD-specific sequencer data layout details beyond the format |
| ISO 9660 (Level 1, PS1) reading: PVD/VDL, path + LBN/extent mapping, directory walk, sector I/O | LoD's 4-disc merge policy and `SCUS94491` region handling (`scengine::assets` / `game`) |
| TMD/CTMD (GTE mesh), TIM/DABA (VRAM texture images), ANM (animation), MRG/DEFF (archives), BPE (byte-pair compression) — the *generic* PS1-era file formats, all with the same lineage across PS1 titles | LoD-only containers: CContainer (CLUT anims), MCQ (menu data), CMB (battle stages) — those decoders live in `game/` and emit `psx::` IR types |
| Tag serialization (the PS1-era save tag format LoD saves use) | The *save schema* (what a LoD save contains, V8→V10 migration content) — `game/` |
| CD-Rom sector I/O abstractions (read at LBN, multi-sector) | — |

**Independence rules (mandatory):**
- The core has **zero third-party dependencies** — C17 and the libc allocator only
  (`malloc`/`calloc`/`free`, verified as the whole undefined-symbol surface of the built
  library; psx-c-api.md §9). It defines its own fixed-point geometry types (MV/VC/SS/DV
  structs mirroring the GTE register semantics) rather than using GLM,
  so the library stays usable in any context (tools, tests, embedders).
- **No C++ runtime in the core.** The wrappers are header-only C++ on top; a C or C++
  embedder links the same `libpsx`.
- No SDL, no Qt, no Luau, no engine headers. `psx` compiles standalone
  (own target, own tests) — a hard build-system rule (build-system.md).
- No LoD strings, no `lod:` registry ids, no save-schema knowledge.
- Threading: `psx` is thread-*agnostic* — decoders are pure functions on
  input buffers; the rasterizer is single-context. Callers own
  synchronization (engine does).

## 2. Modules (verified against reference source)

Reference sources (paths relative to `~/Documents/GitHub/Severed-Chains`). Each row names
the future C module (`psx/<module>.h`, C functions `psx_<module>_*`) and its C++ wrapper
namespace (`psx::<module>`):

| `psx::` module | Reference source | Contents (verified) |
|---|---|---|
| `psx::gte` | `legend/core/gte/*` (`Gte.java`, `MV.java`, `VC.java`, `Ss.java`, `Dv.java`, `Mv.java`) | All GTE ops used by the game: `RTPS/RTD/RTT/RT1`, `NCST/NCSF`, `NCLIP` (near/far clip planes, `H`), `NCDT/NCSF`, `OP` (light), `NV`/`NS` (normal), `AVSZ/AVPT/AVOT/AVOT2` (average), `DTPT/DTLN`, `MAC3/MACH` (matrix), `NORM`, `C2F/C2D` (float conversion), `GPCI`; fixed-point conventions (S.x = 32.0 fixed in 24 bits? — port the exact shifts from `Gte.java`), flag accumulation (`flg`), the **`Gte` API used for camera matrices** (`worldToScreenMatrix` derivation, submap/battle camera) |
| `psx::gpu` | `legend/core/gpu/*` (`Gpu.java`, `Graphics.java`, `OrderingTable`, `Texture*`, `Bpp.java`) | PS1 GPU semantics + the **software reference rasterizer**: primitive set (3d flat/gouraud, 3d textured flat/gouraud, 2d lines/quads, sprites), **ordering table** (per-pixel/16x16-cell draw order, the reason the Java GS is pixel-exact), **top-left rule**, depth compare (per-primitive `z`, ZTEST on/off), **all PS1 blend modes** (B+F, B-F, B+1/4F, B+3/4F, B+F/2 (quarter variants), B+1/16F (5%? port exactly), no blend) — including the exact fixed-point color math (5-bit channels, 24-bit RGBA00 internal), chroma keying, CLUT fetch, **per-primitive constant depth** (`usePs1Depth` path), perspective-correct interpolation |
| `psx::spu` | `legend/core/audio/*` (`Spu.java`, `Voice.java`, `FmVoice`, `NoiseTable`, reverb) | PS1 SPU voice model: 24 voices, voice register layout (volume/ADSR, pitch, reverb/FM/noise flags, repeat), key on/off edge semantics, 4-bit PCM + FM + noise + reverb, sample-rate/shift handling — as a *deterministic voice processor* (input: control writes + sample data; output: PCM). The engine's audio thread (M5) drives it; the reference's 120 Hz music-synth *scheduling* (sequence playback) stays engine-side (it's LoD's music system, not PS1 hardware — though the SSSq/SShd *decoding* is `psx::sssq`) |
| `psx::xa` | `legend/core/spu/XaAdpcm.java` (the decoder), `legend/core/audio/xa/XaTranscoder.java` (offline Opus transcoder, not hardware) | PS1 XA 4-bit ADPCM **sector decoder**: mono/stereo, the 4-entry coefficient filter, truncating prediction divide, clamp rails, per-sector block alignment. **Implemented 2026-10-29 and sample-exact against the Java decoder** — 17 synthetic sectors in `psx/testdata/xa_adpcm.bin`, regenerated by `psx/tools/xa_oracle.py`. Gain (`Fmv.java:425`, `>>= 1` after decode) and `XaTranscoder.PRE_SKIP = 312` are **not** decoder behaviour and are out of scope: pre-skip is 312 leading zeros written into the Opus header for the Opus encoder, and `resampleTo44100Hz` is never called. The earlier wording here asked for "pre-skip and gain behavior" *inside* the decoder while also demanding a byte-exact match to the Java decoder, which the reference cannot support — it does neither inside `XaAdpcm` |
| `psx::sssq` | `legend/core/audio/sequencer/*` + `SShd*`/`SSSq*` in unpacker | SShd (sound header: sample table) + SSSq (sequence: note events) *parsing* into IR structs; the *playback interpretation* (the music synth) is engine-side |
| `psx::cdrom` | `legend/game/unpacker/IsoReader.java` | ISO 9660 Level-1: PVD validation, volume/directory path table parse, LBN/extent resolution, sector-aligned reads, file tree build (the 4-disc *merge policy* is engine-side; the *reader* is generic) |
| `psx::tmd` | `legend/core/renderer/Tmd*`/`Ctmd*` + unpacker `CtmdTransformer` | TMD mesh format (vertex blocks, primitive blocks, lit/unlit, textured/untextured, gouraud/flat, tri/quad, material+texture pointers) + CTMD (scaled vertex/transform container, the `ctmdFlags` incl. `usePs1Depth`) → `psx::tmd::Mesh` IR (position/normal/uv/indices + per-primitive depth flag + lighting) |
| `psx::tim` | `legend/core/gpu/Bpp.java`, `Texture*`, DABA handling | TIM (tile image: tiles, CLUT) + DABA (VRAM image with absolute coords, stitching across multiple DABA files) → `psx::tim::Texture` IR (tiles/CLUT/coords + bpp) — the *decode*; the *GPU upload layout* (R32UI tex15 etc.) is engine-side (rendering-sdl3-gpu.md) |
| `psx::anm` | `legend/core/anim/*` (ANM) | ANM animation sequences (sprite sequences, per-frame CLUT offsets, timing) → IR |
| `psx::archive` | `legend/game/unpacker/MrgArchive.java`, `DeffArchive.java` | MRG (multi-record global archive: TOC, **virtual entries**, sector alignment) + DEFF (per-file container) → in-memory file map |
| `psx::bpe` | `legend/game/unpacker/Unpacker.decompress` (the `SCUS94491BPE` codec) | The adaptive byte-pair compression used by the disc (fully specified in asset-pipeline.md) — a generic codec: `compress` (for tooling/repair) + `decompress` (vectorized) |
| `psx::tag` | save system (Tag serializer in reference `saves` package) | The Tag binary tree format: node types (byte/short/int/float/string/double/…), length prefixes, string encoding, the serializer/parser — **byte-exact with the reference** (saves depend on it) |

### 2.1 Implemented (Milestone 3, migrated to C17 in Milestone 3b)

Four formats are implemented, tested and wired into the engine, each as a C17 entry
point set plus a C++23 wrapper. Byte-level
layouts, per-field provenance and every deviation from the original readers are
recorded in **research/psx-formats.md** (code comments state behaviour, not Java
citations — guidelines/cpp.md §2).

| Module | C surface (`psx/…h`) | C++ wrapper (`psx/cpp/…hpp`) | Status |
|---|---|---|---|
| BPE | `psx_bpe_is_bpe`, `psx_bpe_declared_size`, `psx_bpe_decompress`, `psx_bpe_compressed_size`, `psx_bpe_compress` | `IsBPE`, `DeclaredSize`, `DecompressBPE(data, maxOutputBytes)`, `CompressBPE(raw)` (tooling) | Implemented; bounds-checked, allocation-bounded, size-mismatch is an error rather than silent zero padding |
| Archive (MRG) | `psx_archive_is_mrg`, `psx_archive_mrg_entry_count`, `psx_archive_mrg_parse`, `psx_mrg_view_at`, `psx_mrg_view_slice`, `psx_mrg_table_*` | `IsMRG`, `MRGEntry`/`MRGTable`, `ParseMRG(data, sectorAligned)`, `MRGEntryData`, `MRGTableHandle` | Implemented; index-only tables (no borrowed spans), virtual/duplicate resolution, checked slicing |
| Archive (DEFF) | `psx_archive_is_deff`, `psx_archive_deff_subfile_count`, `psx_archive_deff_parse`, `psx_deff_view_at`, `psx_deff_view_slice`, `psx_deff_table_*` | `IsDEFF`, `DEFFEntry`/`DEFFTable`, `ParseDEFF`, `DEFFEntryData`, `DEFFTableHandle` | Implemented |
| TIM | `psx_tim_is_tim`, `psx_tim_parse`, `psx_tim_clut_entry_count`, `psx_tim_image_init` | `Magic`, `SectionHeaderBytes`, `BPP`, `Rect`, `Image`, `IsTIM`, `ParseTIM`, `CLUTEntryCount` | Implemented for parsing/validation. **Not** implemented: DABA stitching, GPU upload layout, pixel decoding to RGBA (engine-side, rendering-sdl3-gpu.md) |
| Error/version | `psx_error_name`, `psx_error_message`, `psx_error_is_*`, `psx_version_*`, `psx_abi_version` | `psx::Error`, `psx::ErrorName`, `psx::ErrorMessage`, `psx::Checked` | Implemented; single name/message table shared by both languages |
| Internal reader | — | — | `psx/src/reader.h`: explicit little-endian, every access bounds-checked (not an exported API) |

Conventions these modules establish for the rest of `psx`:

- **Errors:** `psx_error_t` (one `typedef enum` that carries its own values, kept
  `int`-sized by an ABI guard, so out-of-range values stay legal across the
  ABI) with grouped numeric ranges: 1–49 generic, 50s BPE, 80s TIM, 110s MRG, 140s DEFF;
  `psx_error_name`/`psx_error_message` for logs; mirrored as `psx::Error` with
  `psx::Result<T> = std::expected<T, Error>` in C++ (psx-c-api.md §4). Recoverable format
  failure is *never* a status code from `lod::Status`; the engine maps `psx::Error` →
  `Status::Decode*` at its own boundary and logs the specific name alongside it.
- **No exceptions, no RTTI:** the core is C, so neither exists; the C++ wrappers and their
  test runner are built with `-fno-exceptions` / `-fno-rtti` (`/EHs-c-`, `/GR-`) under
  *every* project exception policy. That is a portability/ABI property of the library, not
  a style preference (§5).
- **Borrowed vs owned:** the core copies nothing and caches nothing. Results that refer to
  input bytes alias the caller's buffer; the only heap objects are the explicit opaque
  table handles, and the allocation-free parse path (caller-supplied entry array) is the
  one the engine uses (psx-c-api.md §3, §5.3).
- **Untrusted input is the norm:** declared sizes, section lengths, entry counts
  and rect extents are validated against the actual buffer before use, and
  allocation is bounded by a caller-supplied ceiling.

Still unimplemented (unchanged from the table above): `psx::gte`, `psx::gpu`,
`psx::spu`, `psx::sshd`, `psx::sssq`, `psx::cdrom`, `psx::tmd`, `psx::anm`,
`psx::tag`, DABA.

`psx::xa` left this list on 2026-10-29: `psx/include/psx/xa.h`,
`psx/src/xa.c`, wrapper `psx/include/psx/cpp/xa.hpp`, tests
`psx/tests/test_xa.cpp`. It is the first module with a **committed**
differential fixture, which is a different class of test from the optional
authentic-asset case above: the input is synthetic and the expected PCM came
from the reference, so it is always available and its absence is a failure, not
a skip.

IR types (`psx::tmd::Mesh`, `psx::tim::Texture`, ...) are plain
C++23 value types (POD-ish, `std::vector` of vertex/index/param structs) —
they are the contract between `psx/` decoders and the engine's GPU upload
paths. They do **not** cross the plugin ABI (native-api.md) and are not
seen by Luau (luau-api.md).

## 3. Why a separate library (vs. putting it in scengine)

1. **Reuse requirement** (project goal: "Reusable engine technology must
   be kept separate... potentially reusable for other PS1 games"). A
   project with zero engine/SDL/Qt dependencies can be dropped into the
   next PS1 port unchanged — decoders, GTE math, and the GS reference
   rasterizer are exactly the pieces a *Legaia* port would share.
2. **Testing independence**: `psx` tests run with no windowing, no
   device, no game data directory — pure buffer-in/buffer-out golden
   tests (the fastest, most reliable test target in the repo).
3. **Layering**: `scengine` (SDL3 GPU, audio threads, assets) and `game`
   (LoD) both *consume* `psx` — a shared dependency cannot live in either
   consumer.
4. **Exception-free discipline**: a small, dependency-free surface is the
   easiest codebase to keep 100% exception-free and 100% Doxygen-documented
   (it's the reference implementation for the rest of the project). Writing the
   core in C makes that property structural rather than enforced: there is no
   exception mechanism to misuse, and the portability promise (any C17 compiler,
   any embedder, no C++ runtime) comes with it.
5. **Two-language reachability**: the engine, the Qt tools and a future
   third-party embedder all consume the same binary; the C++ wrappers keep the
   engine's call sites idiomatic C++23 without a second implementation
   (psx-c-api.md §7).

## 4. Integration points

| Consumer | Use |
|---|---|
| `scengine::render` | legacy/VRAM render modes run `psx::gpu`'s rasterizer (rendering-sdl3-gpu.md §3); GTE-derived camera matrices (`psx::gte`) feed the modern path's view matrices (M7); the rasterizer doubles as the **golden oracle** for the modern pipeline (testing.md) |
| `scengine::audio` | the music synth + SFX engine drive `psx::spu` voice processors; `psx::xa` decodes the CD audio cache (M5/M6) |
| `scengine::assets` | `psx::cdrom` (ISO read), `psx::bpe`/`psx::archive` (unpack), `psx::tmd`/`tim`/`anm` (decoders feeding the asset manager's IR) (M6) |
| `game/` | `psx::tag` (saves), `psx::sssq` (music data), LoD-only containers decoded into `psx::` IR (M6+), `psx::gte` (battle/camera math) |
| `tools/` (Qt) | asset inspector decodes via `psx::` (M12) |
| `libs/dabas/` | **none** (the Dabas handheld is not PS1 hardware — deliberate) |
| plugins / Luau | **none directly** — engine services expose *results*, never `psx::` types (ABI boundary stays clean) |

## 5. Conventions applied to psx

Full rules in **psx-c-api.md**; the summary a contributor needs daily:

- **Core:** C17, no compiler extensions (`C_EXTENSIONS OFF`), warning-clean under the
  project warning set plus the C-specific strictness (`-Wshadow -Wconversion
  -Wmissing-prototypes -Wstrict-prototypes -pedantic`), warnings-as-errors under
  `SC_WERROR`. Allocation only inside explicit `_table_parse` entry points, matched by the
  paired `_destroy`.
- **Wrappers:** C++23 header-only, `std::expected<T, psx::Error>` results for all fallible
  operations (error-handling.md taxonomy; each decoder defines specific error codes —
  malformed-class granularity per testing.md §2.11), built `-fno-exceptions` / `-fno-rtti`
  independently of the project-wide `SC_ENABLE_EXCEPTIONS` default (AGENTS.md §4,
  build-system.md §2).
- **Doxygen on every public symbol** (AGENTS.md): params with in/out and ownership,
  returns, `@errors`, ownership (borrowed spans vs owned tables vs caller-allocated
  destinations), thread safety ("pure function, no state — thread-safe"; the rasterizer:
  "single-context, caller synchronizes").
- Naming — **two grammars, one library**:
  - C: `psx_` + snake_case functions (`psx_bpe_decompress`, `psx_tim_clut_entry_count`),
    `psx_…_t` types, `PSX_UPPER_SNAKE` macros/enumerators.
  - C++ wrappers: namespace `psx` (flatcase abbreviation) with sub-namespaces
    (`psx::tim`, `psx::archive`), types PascalCase (`psx::tim::Image`), free functions
    PascalCase with fully capitalised acronyms in every position (`DecompressBPE()`,
    `CLUTEntryCount()`, `Error::BPEMagic`), variables/parameters camelCase with the
    leading-acronym exception, private members `camelCase_`, constants PascalCase.
    `tools/check_naming.py` audits both grammars (CTest `repo.naming`).
- **Formatting**: `tools/check_format.py` (CTest `repo.format`) runs `clang-format`
  over every project source — including the C core and the C-only consumer fixture — so
  `.clang-format` is enforced rather than advisory. Without a suitable `clang-format` (≥ 17)
  on the host it reports the style rules as **not verified** and exits 77, which CTest
  shows as *skipped*; it no longer approximates the brace rule with a structural scan, and
  it discovers version-suffixed tool names (`clang-format-18`) because CI images ship the
  tool under those names only.
- Golden-test-first: every module ships byte/sample-exact goldens vs the Java reference
  before the consuming milestone starts (testing.md §2.11, §2.14), and each new format also
  gets pure-C coverage in `psx/tests/c_consumer/main.c` plus an entry in the
  exported-symbol audit (psx-c-api.md §8).

## 6. Migration mapping (from scengine's original plan)

Phase 0's engine design assumed these pieces lived in `scengine/ps1/`;
they move to `psx`:
- `scengine/ps1/gte` → `psx` (gte)
- `scengine/ps1/gpu` (software GS) → `psx` (gpu)
- `scengine/ps1/decoders` (BPE/MRG/DEFF/TMD/TIM/ANM/XA/SShd/SSSq/ISO9660/
  Tag) → `psx` (per module above)
- `scengine/ps1/spu` (voice model) → `psx` (spu); audio *scheduling*
  stays `scengine::audio`
- **Stays in scengine:** the R32UI/RGBA8 upload layouts, the 120 Hz music
  synth scheduler, the SFX voice *allocation* policy (LoD's Spu), the
  4-disc merge policy, the versioned `files/` cache, the low-memory mode.
- **Stays in game/:** CContainer/MCQ/CMB decoders, save schema +
  migrations, the Dabas integration.