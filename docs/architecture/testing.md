# Testing & Verification Strategy

Requirement §17: cover engine init/shutdown, exception-policy enforcement,
SDL3 GPU backends, shaders, resource lifetimes, asset decoding, gameplay
calculations, addition timing, save/load, Luau, native ABI, plugin lifecycle,
mod dependencies, cross-platform consistency, error handling. Use
measurements; propose tools only when necessity is established.

## 1. Test Framework

- **Decision: use a minimal dependency-free in-repo test runner**
  (`tests/framework/`): a ~200-line catch-like harness (test case
  registration via static initializers, `CHECK`/`REQUIRE` macros, section
  support, filter by tag, exit codes; the harness itself is throw-free *by
  design* — checks are bool-based and failures abort the test case via
  longjmp-free control flow (early returns), never `throw`. That is a harness
  design choice, not a statement of the project exception policy
  (AGENTS.md §4). Rationale: the approved stack has no test
  framework; adding Catch2/GTest is a new dependency requiring approval for
  zero gameplay-specific benefit; our needs (unit + golden + fuzz harnesses)
  fit a small in-repo harness. *If the team disagrees, the fallback is
  Catch2 (BSD) — flagged for approval in dependencies.md review.*
- One test binary per area (`sc_tests_engine`, `sc_tests_ps1`,
  `sc_tests_game`, `sc_tests_abi`, ...) so areas build/run independently;
  a `ctest`-style driver (CMake `add_test`) runs all.
- **No network, no game data in CI by default:** unit tests use synthetic
  fixtures (hand-built byte buffers for every format) + tiny generated
  assets. Data-dependent integration tests (real ISO) run locally / in a
  separate CI lane with a secret-provided sample (opt-in).

## 2. Per-Area Strategy

### 2.1 Engine init/shutdown & error handling
- Unit: each subsystem `init/deinit` round-trips; deinit-idempotency;
  partial-init failure (injected) → clean teardown + correct Status;
  **every Status code reachable** (error-path coverage table in the report).
- Exception-policy probe (build-system.md §2): when `SC_ENABLE_EXCEPTIONS=OFF`,
  a `throw` TU must not compile; with the default ON policy the probe is
  skipped.
- Crash-path test: terminate handler writes crash log (temp dir).

### 2.2 SDL3 GPU device & backend selection
- Headless-ish smoke on CI where possible (Linux: Vulkan via CI GPU or
  `vulkan-1` softpipe? — softpipe supports Vulkan; run pipeline-creation
  test); macOS: Metal in CI runner (hardware available); Windows: D3D12 in
  CI runner.
- Tests: `SDL_CreateGPUDevice` succeeds with our shader-format mask on each
  CI platform; device name enumeration; **pipeline-creation matrix**: create
  every pipeline variant of every backend (shader-migration §6) — this is
  the "Vulkan, Metal, D3D12 rendering" verification for Milestone 3.
- Backend fallback: if one backend init fails, error reporting is correct
  (no silent fallback).

### 2.3 Shader compilation & loading
- Build-time: all shaders compile on all three targets (glslang/spirv-cross/
  dxc) — CI gates on zero warnings-as-errors (configurable).
- Cache: content-hash invalidation test (touch source → rebuild; unchanged
  → no rebuild).
- Runtime load: artifact load + pipeline create per backend; shader reload
  (dev mode) swaps pipelines without crash.

### 2.4 Resource lifetimes
- Unit: RAII wrapper null-state safety; deferred-delete ordering (resource
  used in frame N is destroyed ≥ frame N+2 — test with fence callbacks);
  cycling transfer buffer correctness (write while bound → cycles, no
  corruption — golden data check); stress: 10k create/destroy cycles.
- TSAN run of the audio thread + async loader (dedicated CI job).

### 2.5 Asset decoding (psx/ formats — `psx` library, standalone)
- Runs in the standalone psx suite (`sc_tests_psx`, links `psx` only — the full
  suite, incl. GTE/GS goldens, is §2.11). **Round-trip tests per decoder**
  with synthetic + reference fixtures:
  BPE (compress→decompress identity on known data; the decompressor on a
  captured real BPE segment from a user-provided disc, opt-in lane), MRG
  (incl. virtual entries + sector-aligned), DEFF, TIM (all BPPs, CLUT/no
  CLUT), TMD (all primitive types: lit/unlit, textured/untextured,
  gouraud/flat, tri/quad, translucent), CTMD (scale application), ANM
  (sequence/sprite metrics), CContainer (frames + CLUT animations),
  MCQ, SShd/SSSq (subfile extraction), XA (decode a captured block →
  compare PCM against the Java `XaTranscoder`/`XaAdpcm` output — **golden <!-- naming-allow: Java class names cited verbatim -->
  PCM fixtures generated from the Java reference**, committed), Tag
  (serialize/deserialize identity + a real V10 save round-trip, opt-in
  lane), ISO9660 tree build (synthetic disc image fixture).
- Malformed input: truncation/corruption at every offset class → specific
  Status code (fuzz — see §3).

### 2.6 Gameplay calculations & combat
- **Golden-value tests**: port the damage/SP formulas (`combat/formula`
  package) as unit tests with the reference values (computed from the Java
  code paths on fixed inputs — generated during Phase 1 by running the
  Java reference in a harness; values committed as fixtures).
- **Addition timing** (the critical one): the timing-square evaluator is a
  pure state machine — property tests: (a) press at frame
  [lower..upper] → success; (b) before lower → fail-early; (c) after upper
  → fail-late; (d) wrong action → fail-wrong; (e) timing-window multiplier
  config scales bounds exactly (parity with Java `getHitProperty`);
  (f) auto modes (tutorial/auto) skip input. **Frame-exact**: run the
  battle state machine with a recorded input script and compare full
  sim-state snapshots (entity positions, turn order, HP, effects timeline)
  against Java reference snapshots (opt-in data lane; a small battle
  scenario suffices for the first cut).
- RNG: LCG sequence identity (port test: N steps from a seed == Java
  output fixture).
- Turn order, stat growth, level-up, encounter tables: unit tests from
  reference tables.

### 2.7 Save/load
- Tag V10 byte-exact: serialize(Java-deserialized data) == original bytes
  (fixtures: minimal save + a real save, opt-in lane).
- Migration chain V8→V9→V10 on fixtures; corrupt save → InvalidSaveResult;
  memcard import (synthetic DABA fixture + real `.mcr`, opt-in);
  crash-recovery save generation (injected mid-game crash → save loads);
  mod `modData` slot isolation (two mods' data survive a save/load; a mod
  removal leaves orphan data, not corruption).

### 2.8 Luau
- VM: per-mod isolation (one VM's error doesn't affect another);
  protected-call error paths (runtime error → Result with message, no
  exception); memory cap trigger; instruction budget trigger; stdlib
  reduction (no `os`/`io`/`debug` — negative test).
- API: each registered capability (character template, addition, item,
  event, config) round-trips through the registry and is visible to the
  game (integration tests with a script mod fixture); combat evaluator
  seam (a Luau button-sequence evaluator drives a mock battle hit — the
  Playable-Lloyd class scenario).
- Bytecode cache: version-keyed (Luau version bump → cache miss → recompile).
- Performance: per-frame hook overhead budget test (<50µs typical, p99).

### 2.9 Native plugin API & plugin lifecycle (C++23)
- **ABI stability tests** (the "native ABI compatibility" requirement,
  native-api.md §9.4): sample plugin compiled against SDK v1 (same
  toolchain) loads against engine vN and passes functional tests; interface
  versioning (`api.Table<T>()` — unknown id/version → nullopt, known →
  works); **frozen-layout probe** — `static_assert`s on size/offset of every
  frozen SDK type, compiled against the SDK *and* the previous engine's SDK
  headers (CI catches drift); string lifetime rules (engine doesn't hold
  plugin strings past the call — ASAN detectable).
- **Toolchain-parity tests**: a plugin built with a *different* compiler
  family than the engine (toolchain hash mismatch) is **refused at load**
  with a clear diagnostic (negative test, per platform matrix); the SDK
  compile probes (throw / dynamic_cast TUs must fail to compile) run on the
  SDK headers with every CI compiler.
- **Lifecycle tests**: discover (entry symbol lookup V-max..V1) → validate
  (bad manifest, missing dep, version mismatch, toolchain mismatch, cycle)
  → load → init (`OnLoad` success + failure injection) → ready → shutdown
  (handle-leak check) → unload; plugin crash isolation is *documented not
  testable in-process* (covered by the contract tests + the out-of-process
  future work).
- **Fuzzing** (dedicated `lod-fuzz` binary, libFuzzer): every plugin API
  surface exercised through a fuzz-host plugin with random args; every
  decoder with random/truncated buffers; Tag parser; config parser; unified-
  diff applier (fuzzed patch text). Run in CI (time-boxed) + ASAN/UBSAN.

### 2.10 Mod dependency resolution
- Unit: semver range matching (port Java's `^x.y.z` semantics — golden-test
  against semver4j NPM-mode fixtures: `^`, `~`, comparators, prerelease
  edge cases; mod-loader.md §2), dependency closure, cycles, missing deps
  (→ skip + report), duplicate ids, load order determinism (alphabetical +
  dependency order, golden test).

### 2.11 PSX library (`psx`) — standalone golden suite

The `psx` suite links **`psx` only** (no engine/SDL/Qt) — the fastest test surface in the
repo, and the parity backbone (psx-library.md, psx-c-api.md §8). Four targets, because the
C17 core has four distinct consumers:

| Target / CTest entries | What it guarantees |
|---|---|
| `sc_tests_psx` — `psx.psx`, `psx.bpe`, `psx.tim`, `psx.xa`, `psx.archive`, `psx.wrapper`, `psx.clinkage` | Format behaviour through the C++23 wrappers (52 cases: psx 2, bpe 9, tim 11, xa 11, archive 10, wrapper 8, clinkage 1), wrapper delegation/move-semantics/RAII/error-propagation/span-borrowing, and that the C header is usable from C++ under `extern "C"` |
| `psx.c_consumer` (`sc_psx_c_consumer`) | A **pure C17** program (C compiler only, `-Wall -Wextra -Wshadow -Wconversion -Wmissing-prototypes -Wstrict-prototypes -Wpointer-arith -Wwrite-strings -Wredundant-decls -pedantic`) exercises every entry point, every documented error code, NULL-argument rejection and the malformed-input contract: 294 checks, of which 33 are the XA lane added 2026-10-29. This is the target that answers "is the API really usable without C++?" — which is why the XA work is not finished at the C++ wrapper: the wrapper test proves the wrapper delegates, not that a C program can use the API. It also catches umbrella gaps, because it only includes `psx/psx.h` |
| `psx.abi.symbols` | Export-surface audit: exactly the documented symbols are exported, none undocumented, no C++ mangled symbol exported *or* referenced. Reads the symbol table with `nm` on ELF/Mach-O and the **PE export table** (`objdump -p`/`dumpbin /EXPORTS`) on a `.dll` — the method follows the artifact, so a cross-built Windows DLL is auditable from any host. Runs for both shared and static builds |
| `psx.differential` (`-DPSX_ENABLE_DIFFERENTIAL_TEST=ON`) | Old-vs-new equivalence against the frozen pre-migration C++ implementation over a deterministic corpus incl. boundary probing (psx-c-api.md §10) |

Malformed input is a first-class corpus, not an afterthought: truncation at every length,
per-byte flips, random noise, and every 2-/4-byte window loaded with threshold values,
compared between the two implementations.

One ABI property is checked in C only, deliberately: an out-of-range error code
(`psx_error_name(12345)`, `(psx_error_t)-7`) is representable at the C boundary and
undefined behaviour in C++ (an enum load outside its declared range). So the C++
linkage test pins the compile-time shape (`is_enum`, `sizeof == sizeof(int)`) and the
pure-C consumer pins the runtime behaviour.

The whole suite also runs under AddressSanitizer + UndefinedBehaviorSanitizer —
`-DSC_ENABLE_ASAN=ON -DSC_ENABLE_UBSAN=ON` locally, which is what the sanitizer legs
of `ci.yml` pass (three of the seventeen). `ScToolchain` adds
`-fno-sanitize-recover=all` and the CI test step sets `halt_on_error=1`, so the first
violation fails the binary instead of logging it and passing. Leak detection runs only
on the Linux legs (macOS ASan aborts on `detect_leaks=1`). That lane is what proved
the C++ version of the enum probe was UB rather than a test: the case printed `OK`
while sanitizer reported the bad loads, which is how a passing-but-invalid test gets
caught.

Decoding expectations:
- **GTE math**: every op (RTPS/NCLIP/NCCS/AVSZ/MAC3/... ) against Java
  `Gte` outputs for a fixed input matrix (exact 32-bit integer equality —
  GTE is integer-fixed-point; no float tolerance needed); flag accumulation
  sequences; the camera-matrix derivations used by submap/battle cameras.
- **Software GS (psx::gpu)**: render a fixture scene → **byte-identical**
  to the Java `Gpu`/`Graphics` output (the parity oracle, rendering-
  analysis.md): ordering-table draws, top-left rule, every blend mode
  (incl. B+1/4F), per-primitive constant depth, CLUT/chroma-key paths.
- **Decoders**: BPE (compress→decompress identity + captured real segment),
  MRG (virtual entries, sector alignment), DEFF, TIM/DABA (all BPPs),
  TMD/CTMD (all primitive classes + `ctmdFlags`), ANM, XA (sample-exact PCM
  delivered 2026-10-29 — the next two bullets say what that did and did not
  cover), SShd/SSSq parse, ISO9660 tree (synthetic disc fixture), Tag
  (serialize/deserialize identity + real save bytes, opt-in lane).
- **XA is exact against the Java decoder, and its scope was corrected on the way.**
  The expectation here used to read "sample-exact PCM vs the Java
  `XaAdpcm`/`XaTranscoder` fixtures, incl. the 312 pre-skip" <!-- naming-allow: reference class names cited verbatim -->
  — those two clauses contradict each other. Pre-skip is not a decoding step:
  `PRE_SKIP = 312` is a transcoder-only constant that writes 312 leading zeros
  into an Opus header, and the `>>= 1` gain happens in the FMV player after
  `decode` returns. Reproducing either inside the decoder would have broken the
  byte-exact clause it sat next to. Delivered scope and the deliberate 8-bit
  divergence are written up in psx-formats.md §6; fixture is
  `psx/testdata/xa_adpcm.bin`, 88 704 samples.
- **Two classes of differential fixture, and the difference matters.** The
  authentic-asset lanes (`SC_PSX_ASSET_DIR`, retail `DRGN0.BIN`) are optional and
  **skip with a visible message** when the data is absent, because absence there
  is expected. The XA fixture is synthetic, committed, and always available, so
  `test_xa.cpp` **fails** when it cannot be read — a skip there would report a
  pass that was not earned, and would also hide a broken checkout. Do not
  generalise the §2.2 skip rule to committed synthetic fixtures.
- **A differential harness must be shown able to fail.** The XA run is accompanied
  by seven mutations (filter coefficient, per-call state reset, mono duplication,
  clamp→wraparound, dropped `+32`, arithmetic-vs-truncating divide, L/R interleave
  swap), each of which produces mismatches against a 19/19 baseline, plus three
  on the C-consumer side (carried stream ignored, stereo/mono sample counts
  swapped, 8-bit coding accepted as nibbles). A future
  decoder lane is not done until it has the same demonstration.
- **Mutation experiments run in a scratch copy, never in the live tree.** Copy
  the tree (or use a `git worktree`), mutate there, and confirm afterwards with
  `git status --short` that the working tree is clean. This is not ceremony: a
  mutation left in place can be committed. `fd7d8a6` did exactly that — the
  "per-call state reset" mutation above was committed as if it were the
  implementation, which silently destroyed per-stream XA history (a real
  behaviour change: audio decoded by the C API lost its filter taps at every
  sector boundary) while the code still *looked* like the reference.
- **A test run only counts if the build that produced it succeeded.** The
  residue above was caught the second time by checking the build exit code
  before the test exit code. With `-DSC_WERROR=ON` (the CI default) the
  mutated file fails to compile because the mutation made a parameter unused,
  and `ctest` then re-runs the **stale binaries from the previous build** and
  reports every test green. Read the build result first; `100% tests passed`
  after a failed build means nothing.
- Malformed input: truncation/corruption at every offset class → the
  specific `psx_error_t` / `psx::Error` code (fuzzed — §3), asserted identically from
  C and C++.

### 2.12 Script tooling (psx::script + fate)
- **Round-trip**: disassemble→assemble == identity (byte-exact) on the full
  retail script set (opt-in data lane) + a synthetic-script corpus in-repo
  covering every op/param type, pointer-table overruns, LodStrings, fork/jmp
  tables.
- **Translator/Tokenizer**: text format round-trip (bytes → text → bytes),
  canonical-form stability (the patch contract — a re-run of translate on
  already-patched output is a no-op).
- **Unified diff component**: generate/apply/undo on fixture text (3 context
  lines, fuzz tolerance = none, failure → specific error); `call`-normalization
  (index↔name via meta) golden tests; the full reference patch corpus
  (`patches/scripts.csv` + all `.diff`/`.txt`) applies to the unpatched
  scripts and produces **byte-identical output to the Java unpacker's
  patched tree** (opt-in data lane — the strongest fate-pipeline proof).
- **Fate compiler** (hand-rolled, per Q-SCR-1 recommendation): differential test against
  **generated** goldens produced by running the Java `FateCompiler` (see
  `docs/research/fate-toolchain-phase1.md` §8 and §12 — the oracle harness must exist before the
  compiler does; it now does, as `tools/fate_oracle.py`). The reference `patches/**/*.txt` files
  are **assembly text, not Fate source** and cannot be fed to `compile`: none of them contains a
  `def`, and `player_combat_script.txt` begins with `entrypoint :ENTRYPOINT_0_MAIN`. They are
  inputs for `assemble`/`decompile` and semantic cross-checks only. The real Fate corpus is two
  files (`char_utils.fate`, `queen_fury.fate`), which together cover only a fraction of the
  grammar.
- **Fate corpus status (F2, 2026-10-28).** `fate/testdata/` holds 19 fixtures — the two real
  files plus 17 synthesized probes — each with a byte-exact `.expected` produced by the reference
  and a `.diag` recording its exit code and diagnostics. `python3 tools/fate_oracle.py check`
  re-runs the oracle and fails on drift; it exits **77 (skip)** with a visible message when the
  oracle cannot run, so a machine without a JDK never reports a pass it did not earn. Comparison
  is byte-exact by design: leading blank line, trailing space on zero-parameter ops, and per-file
  scratch/label counters are all observable. Still missing from the corpus: unterminated string
  and opcode-name collision. The missing `-L` directory case is covered, but by the
  **`cli_contract`** table rather than by a golden: the reference aborts there (exit 1, stack
  trace), so there is nothing to match byte-for-byte, and the native 0/1/2/3 contract is not
  overridden by ruling R1. A missing *include file* is now decided the same way (owner ruling
  2026-10-28): fatal, exit 2, no output, pinned by `c4`–`c8`. The reference agrees that the
  compile aborts — `Include.resolve` throws `IncludeFailedException`, which escapes the
  `catch (IOException)` in `FateCompiler.compile` that would otherwise log and continue — but
  it prints a stack trace and then exits 0 with no output, which is the pairing the contract
  forbids ("a failure that also produces a deliverable"). Design doc §9.2 and §14 item 6.
- **Fate differential harness.** `fate/testdata/run_golden.py` is the acceptance test — one CTest
  entry, `fate.compile.golden`, registered by `fate/CMakeLists.txt` and **on by default**
  (`-DFATE_ENABLE_GOLDEN_TEST=OFF` opts out; a default build that registers zero fate tests is
  not acceptable coverage). It runs the compiled `fate` binary per fixture and compares **three
  axes** — emitted bytes, emitted diagnostics (stderr against the message lines of `<id>.diag`,
  order-insensitive but multiset-strict, with the reference's own crash lines filtered out), and
  exit status against the manifest's `expect` class; it
  never runs Java, never touches the network, and skips with exit 77 when the binary, the goldens
  or the meta bundle are absent. The test sets `SKIP_RETURN_CODE 77` — without it CTest reads any
  non-zero exit as a failure, and a machine with no meta bundle would report a broken compiler
  instead of an absent fixture. Because the meta CSVs are not committed, a runner without them
  compiles the tool and compares nothing; the CI step prints `NOT VERIFIED` in that case rather
  than reporting coverage. It also fingerprints every fixture/golden before the sweep and fails
  if a run mutates one, because two probes in this project's history truncated fixtures while the
  suite stayed green. A second, smaller table — `cli_contract` — pins the **native** CLI contract
  in nine entries: two for `-L` handling (missing directory, file-as-directory), one control that
  a valid `-L` still compiles, three for the unresolvable-include abort (with `-L`, without `-L`,
  and one where the include sits between two valid `def`s so the failure happens only after real
  statements have been parsed), and three controls around it (the trailing-space include gate, a
  stale output file being discarded, and `-o` naming the input being refused). Those entries have no golden, are reported on their own
  line as *not derived from the oracle*, and put `-o` inside a private temporary directory so they
  cannot write into the tree — `c8`, the input-as-output case, runs on a **copy** of a fixture for
  the same reason.
  Current local state (re-measured 2026-10-28 after the include-abort work, fresh configure at the
  repository root, 30/30 CTest): 18/19 fixtures byte-match, 1 pinned deviation; 17 diagnostics
  exact, 2 reported NOT VERIFIED (`p10` crash filter, `p12` unpinned recovery by-product); 19/19
  exit statuses; 9/9 native CLI-contract checks.
- **Test gates were proven non-vacuous, twice.** The second time, on 2026-10-28, against **two**
  baselines rather than one impression: a binary built from the pre-`-L` working tree, where every
  entry of the then-eight-entry table failed (20 assertion failures); and, re-run the same day
  against the current nine-entry table, a binary built from `dd58586`, which already has the `-L`
  guard — there exactly the four pre-existing checks pass (`c1`, `c2`, `c3`, `c6`) and exactly the
  five new ones fail, 18 assertion failures, including `c8`: *"the input file was modified"*, which
  is the data-loss path the two new guards close. The `dd58586` run is the informative shape: it
  separates the new checks from the old, so it shows the new checks can fail **and** that the old
  ones did not silently regress. Its `c9` output is also the clearest picture of what the abort
  replaced — 170 bytes of assembly emitted beside `unknown function helper` and `unknown function
  other`, a deliverable whose call targets have no definitions.
  Separately, with `p12` flipped back to `parity: match` in a **copy** of `testdata`, the
  harness fails on bytes and diagnostics, and a truncated valid fixture fails too.
  A check that cannot fail is not evidence — new checks need a deliberately-wrong compiler or a
  deliberately-wrong expectation, not just a green run.
- **How to get a baseline binary.** `git archive HEAD fate | tar -x -C <dir>` then configure that
  directory standalone (`cmake -S <dir>/fate -B <build>`). Build **in-tree** is not reusable here:
  the local `build/` tree (gitignored — repo content is unaffected) was configured while the tree
  lived at a different path, so its cached source dir
  (`/Users/schala/Documents/severedchains`) resolves to a directory with no `CMakeLists.txt` and
  every `cmake --build` there fails. That failure reads like a project defect when it is only a
  stale cache, so check `CMAKE_HOME_DIRECTORY` in `CMakeCache.txt` before believing it. The
  whole-project 30/30 figure quoted above came from a fresh configure at `/tmp/scb`, not from
  `build/`.
- **Fate CI lane (written, never executed).** `.github/workflows/ci.yml` has a dedicated
  `fate-toolchain` job (macOS arm64, Linux x86-64 GCC, Linux x86-64 Clang) while the engine
  matrix passes `-DSC_ENABLE_FATE=OFF`. Compiler selection is by measured feature support: the
  `auto x = 1;` probe must compile under `-std=c23`/`-std=c2x` **and** fail under
  `-std=c17 -Werror -pedantic-errors`, so a toolchain that ignores the standard flag cannot
  qualify; if no candidate qualifies the job fails with instructions rather than downgrading.
  **No leg of this workflow has been observed running** — that remains the M1 gap tracked
  separately, not a claim of cross-platform verification. It is no longer blocked by the absence of
  a repository: the tree has been under git since 2026-10-28 18:19 with a remote configured, so a
  push would trigger a run; whether that has ever happened is unverified.

### 2.13 Mogul Dabas minigame (scdabas)
- **Console semantics**: timer/RTC/interrupt/power/DAC behavior unit tests
  (fixed-step host-driven ticks per dig-dabas.md Q-DAB-2 decision);
  32×32 1bpp framebuffer inversion parity with the Java host adapter.
- **RNG**: LCG+mix32 sequence identity (N draws from seed 1 == Java
  `dabas.core.Random` fixture — generated from the reference; distinct from
  the main-game RNG test).
- **Save byte-exactness**: `Save60` + tile grid (0x260 blob, grid layout per
  Q-DAB-4) serialize/parse identity + a Java-produced save round-trip.
- **Game logic**: a scripted input tape (5 buttons over N ticks) drives a
  fresh dig → rewards (gold/items/special) compared to a Java-session
  fixture (opt-in data); crash-isolation: an injected VM fault returns an
  error result and the host continues (parity with the Java
  try/catch-shutdown — the native error-path analog).

### 2.14 Cross-platform consistency
- **Golden output tests**: (a) BPE/MRG/TIM/TMD decode → byte-identical
  across OS/ABI (fixtures in repo); (b) GTE math → identical floats
  (32-bit arithmetic, no FMA contraction — compile with `-ffp-contract=off`
  where the reference math is GTE-quantized, documented); (c) Tag
  serialization bytes; (d) RNG sequence; (e) shader compile → identical
  SPIR-V hash across OS (tool pinned).
- CI matrix runs the golden suite on Win/Linux/macOS (arm64).
- **Memory safety**: three of the seventeen legs build the whole tree under ASan +
  UBSan (macOS arm64, Linux x86_64 GCC and Clang). The Linux pair also enables Leak
  Sanitizer, which has never run anywhere yet — see build-system.md §8.
- **Current state (pre-Milestone 2)**: `.github/workflows/ci.yml` covers four
  runner families across three CPU architectures — macOS arm64 (Apple Silicon,
  the priority platform), Linux x86_64, Linux arm64 on native ARM runners, and
  Windows x86_64/MSVC. The golden-decode suite of item (a) is only partly
  present: `psx.differential` compares the C17 decoders against the frozen
  pre-migration implementation over a deterministic corpus, and it runs on three
  of those legs (macOS arm64, Linux x86_64, Linux arm64), so a decode that
  differed by architecture or by OS would fail somewhere rather than only on the
  machine that produced it. What is still missing for (a) is committed golden
  fixture bytes, and TMD/GTE/RNG coverage, which have no implementation yet; when
  they land they join these legs rather than needing new ones.
- **Local parity checking** (no runner has been observed executing the workflow — see §2.12 for
  why that is now a push question rather than a missing-repository question): the four
  Apple-Silicon legs were executed locally
  with the exact argument strings the workflow assembles (28/28, 28/28, 29/29 and
  29/29 CTest entries, each with the standalone `psx` project at 8/8, `arm64` binaries
  confirmed). The macOS dependency step was run against current SDL3, an
  deliberately too-old SDL3, and no SDL3 at all. The PE export audit was tested
  against a real cross-built `libpsx.dll` using both GNU and LLVM objdump, plus a
  deliberately-bad DLL to confirm it reports stray/C++ exports instead of passing
  vacuously. Unexecuted: the eight Linux x86_64 legs, the three Linux arm64 legs and
  the two Windows legs — see build-system.md §8 for what each would be the first
  to check.

### 2.15 Rendering fidelity (measurement, not assumption)
- **Golden screenshots** (Milestone 7): fixed camera poses + fixed RNG +
  fixed clock → render N frames → compare against Java reference captures
  (per-scene: title, field (legacy + HQ projection), battle menu, addition
  overlay active, battle with PS1-depth CTMDs, translucent-heavy scene,
  CRT post on/off, 4:3 letterbox, widescreen submap). Metric: per-pixel
  diff with a tolerance (allow ±1/31 in 5-bit channel quantization, 0
  tolerance on geometry edges — reviewed visually when exceeded).
- **Timing tests**: sim tick rate measurement (vsyncMode 1 & 2, speed
  multiplier 1/2/4/16) against expected tick counts over a wall-clock
  window (±2%); addition window frame counts (deterministic sim → exact).
- **Audio measurement** (Milestone 5+): synth output PCM vs Java reference
  PCM for a fixed sequence/seed (spectral + sample diff), SFX voice
  timing (key-on latency), XA stream alignment (pre-skip 312 parity).

### 2.16 Input (state machine + SDL boundary)

Two tiers, because `lod::InputManager` is deliberately SDL-free (design:
engine-design.md §4.7) and the translation layer is the only place SDL input
types appear (cpp.md §9.1):

- **Tier A — SDL-free state machine** (`tests/engine/test_input.cpp`, CTest
  group `input`): deadzone curve including the degenerate-region quirks, binding
  dispatch, key-repeat cadence, focus/background gating, the Steam Input guard,
  device removal, axis max-combine. Cases drive `BeginFrame`/`EndFrame` with an
  injected clock, so repeat timing is exact rather than wall-clock dependent.
- **Tier B — SDL boundary** (`tests/engine/test_input_sdl.cpp`, same group):
  real `SDL_Event`s pushed through `Platform::PollFrame()`, plus SDL's *virtual
  joystick* (`SDL_AttachVirtualJoystick` + `SDL_SetJoystickVirtualButton/Axis`)
  for controller open, hold and unplug. Injected virtual input is published by
  SDL's own polling thread, so a case sleeps 60 ms after writing it (a poll that
  happens immediately may legitimately see nothing); cases skip rather than fail
  when the gamepad subsystem is unavailable, and a fixture asserts its platform
  initialised instead of running against a half-built one.

Headless rule: a headless `Platform` sets the dummy video-driver hint before
`SDL_Init`, so the suite needs no display server; a gamepad-subsystem failure
degrades to keyboard-only (logged) rather than failing init. A dummy-driver host
reports "has keyboard focus", otherwise every headless run would silently disable
input. Verified headless on macOS with `SDL_VIDEODRIVER=dummy` and no `DISPLAY`.

Not covered by design: real hardware mappings (roadmap Cross-Milestone Notes
item 6) and pointer *delivery* (no UI consumer exists yet).

## 3. Fuzzing & Profiling Tools (proposed, necessity-established)

| Tool | Necessity |
|---|---|
| libFuzzer (Clang) / AFL++ (GCC) | established: 6+ external-format parsers + plugin API surface + unified-diff applier |
| ASAN/UBSAN/TSAN (compiler) | established: audio thread + async loader + plugin boundary |
| RenderDoc capture (Windows/Linux) + Xcode Metal capture (macOS) + D3D12 debug layer + Vulkan validation layers | established: SDL3 GPU debugging (per SDL docs; no new dependency — platform SDK facilities) |
| `perf`/Instruments/WTProfiler (platform profilers) | established: measurement-based performance work |
| **No** additional GUI/test profilers proposed in v1 (nothing else has demonstrated necessity yet) |

## 4. Verification Gating (milestone acceptance)

- Every milestone (roadmap.md) lists its concrete test gates; a milestone
  is not done until its gates pass on all three platforms (or the
  platform-specific gap is documented + user-acknowledged).
- Nightly: full unit + golden + fuzz (time-boxed) on the matrix.

### 4.1 CI is the authoritative gate — and as of 2026-10-29 it does NOT yet gate the psx suite

**Correction of an earlier claim in this file.** A previous revision of this
section stated that CI had never run and, separately, that the existing workflow
already covered `psx.xa` / `psx.c_consumer`. Both were wrong, and the second was
built on the first. What follows is measured.

**CI has run.** Nine pushes, nine workflow runs, **all nine concluded
`failure`** — including run #9 on `e091dc1`, the commit that fixed the XA stream
history. Job- and step-level conclusions were read from the Actions API (the log
endpoint needs an authenticated request and returns 403 anonymously, so this is
evidence about *where* things failed, not the failure text).

**No run has ever executed the psx suite.** In run #9 all 21 jobs failed, and
the engine legs died before the test steps, which GitHub then reports as
`skipped` (a later step does not run once an earlier one fails):

| Legs | First failing step | Outcome for `psx.xa` / `psx.c_consumer` |
|---|---|---|
| 4 × macOS arm64 | `Prerequisites (macOS)` (~10 s per job) | test steps **skipped** — never executed |
| 8 × Linux (x64 + arm64, incl. sanitizers) | `Configure` (~48 s) | test steps **skipped** — never executed |
| 2 × Windows | `Build` | test steps **skipped** — never executed |
| 3 × fate-toolchain | `Tests (fate compile, golden differential)` | reached a test step, and failed there |

So the requirement that these two tests "run in CI against the actual revision,
with a nonzero exit on failure, with no silent skips" is **not met**. The suite
being registered in the CTest files is not the same thing as CI executing it, and
reading the workflow's *steps* as evidence that they *ran* is exactly the mistake
this section exists to prevent.

**What local reproduction does establish** (unchanged, and still the reason the
test half is sound): under CI's own flags the two tests are registered in both
configurations (in-tree: 30 tests, both present; standalone `psx`: 9 tests, both
present); and against the defective `xa.c`, built cleanly with `SC_WERROR=OFF`
so the mutation could not hide behind a compile error, 2 of 30 fail and `ctest`
exits **8**. That proves the tests are non-vacuous. It does not prove CI will
run them, because CI currently never gets that far.

**Two of the three failure classes are now root-caused and fixed; the third is
still open, and stays open on purpose.**

*Class 1 — `Prerequisites (macOS)`, 4 legs, ~1–3 s.* Confirmed, reproduced, and it
was never the `sort -V` logic or a missing `brew`. GitHub Actions runs `shell: bash`
with **errexit on** (`bash -e {0}`), so the script's own `set -uo pipefail` does not
turn error handling off. `brew list --versions <formula>` exits **1** for a formula
that is simply not installed, and that pipeline sat inside `ver="$( … )"`: the
assignment inherits the pipeline status, pipefail propagates it, errexit aborts the
step. The step died before writing `DEP_MODE`, so Configure had no dependency mode
either. Differential proof, run against the block extracted from `ci.yml` rather than
a retyped copy: HEAD's version exits **1** with brew present-but-sdl3-absent and
exits **0** with brew reporting sdl3 3.4.18 — which is exactly why it passed on a
development machine that has SDL3 installed and failed on a runner that does not.
The repaired block returns 0 in all four brew states (absent / present-but-absent
formula / 3.4.18 / 3.1.0) and still selects `source` for the too-old case, so the
probe was made safe without being made blind.

*Class 2 — `Formatting gate`, the repo-gates job, 4 s.* Confirmed and reproduced, two
defects stacked. The runner image ships clang-format 16.0.6 / 17.0.6 / 18.1.3 as
`clang-format-16`…`-18`, and the gate probed only the bare name, so a fully-tooled
runner looked untooled; the degraded path then ran a structural brace scan that flagged
**41 lines clang-format 23.1 reports clean**, including braced bodies holding two
statements where braces are mandatory. Both halves are fixed (discover suffixed names;
skip with exit 77 instead of approximating; `SKIP_RETURN_CODE 77` so CTest says
*Skipped*). Re-proven non-vacuous after the change: real offenders still exit 1 with a
formatter present, and a header missing `#pragma once` still exits 1 **without** one.

*Class 3 — Linux `Configure` (8 legs, 24–35 s) and Windows `Build` (2 legs) — still
open.* Ubuntu 24.04 has no `libsdl3-dev` in its own archive, so those legs select
`dep_mode=source` and configure through the FetchContent SDL3 path; that same path
configured cleanly on macOS in ~26 s, so the failure is platform-specific and **not
reproduced** here. `brew`-equivalent reasoning does not transfer: the macOS cause was
an exit-status bug in a probe, whereas this one sits inside CMake on a host this
machine does not have. Resolving it needs a Linux host (Docker is installed here, the
daemon is stopped) or one authenticated look at a `Configure` log.

What this does **not** establish: that any leg now goes green. Class 1 and 2 were
confirmed blockers on the path to the tests, and removing them is not evidence that
nothing else blocks it — after the macOS prerequisites pass, that job still has to
configure and build, and the macOS source-fallback branch has never run on a runner.
Because every leg is already red, only the log text (or a Linux host) distinguishes a
real fix from a second defect, so the remaining step is still *evidence*, not a patch.

**One structural note, independent of the causes:** a leg that dies in a
prerequisite step skips its tests, so "CI is red" is currently the only signal,
and it does not say whether code or environment is at fault. That is worth
fixing once the causes are known, not before.

**Also still open, and untouched on purpose:** enforcement depends on a human
running the suite before committing. Nothing catches a *committed* mutation
automatically today — `fd7d8a6` is the proof — and adding a pre-commit or
mutation gate is not authorised here.

---

## 5. Gaps / Open

- **CI has never executed the test suites it declares** (§4.1). Nine runs, nine
  failures, every engine leg dying at a prerequisite/configure/build step, so
  `psx.xa` and `psx.c_consumer` have run zero times in CI. The gates are declared
  and locally proven non-vacuous; they are not yet *executed* by CI.
  Two causes are now fixed with local differential evidence: the macOS prerequisite
  step's errexit-on-probe bug, and the formatting gate's false positives on a runner
  that names its formatter `clang-format-18`. Still unreproduced and unfixed: Linux
  `Configure` (8 legs) and Windows `Build` (2 legs). Diagnosing those needs a Linux
  host (start the Docker daemon) or authenticated log access — **not** a speculative
  rewrite of `ci.yml`.
- Java reference test harness for golden value generation: the reference
  repo's tests are excluded from its build (`test { exclude '**/*' }`) —
  generating goldens from Java requires running it (with a user's disc) in
  a dev harness; documented as a Phase 1 task (no CI dependency).
- D3D12 headless CI: limited; rely on the Windows runner's hardware.
- Audio golden fixtures: generate from the Java reference during Phase 1
  (same harness as gameplay goldens).
