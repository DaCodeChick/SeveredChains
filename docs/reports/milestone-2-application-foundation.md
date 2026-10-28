# Milestone Report — Application Foundation (config, platform, events, timing, host)

Scope label: the user's "Milestone 2" continuation brief. In `docs/architecture/roadmap.md`
terms this is the remaining work of **M1 — Application Foundation**
(lifecycle, window, config, timing, diagnostics). **M2 — SDL3 GPU Foundation was
not started** (no `SDL_CreateGPUDevice`, swapchain, pipeline or `psx` format
modules were added).

Reference repository: `~/Documents/GitHub/Severed-Chains` at
`ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb` (observed; no Java-side research was
required for this increment — the work is engine-side foundation).

---

## 1. Verified starting state (the previous agent's report, checked against the tree)

| Reported | Actual |
|---|---|
| `ScDeps.cmake` SDL3 integration | Present and working (`find_package(SDL3 3.2.0 CONFIG REQUIRED)`; SDL 3.4.18 via Homebrew) |
| Engine status I/O error codes | Present (`Status::IOOpen/IORead/IOWrite` = 100..102) |
| `platform.h` / `platform.cpp` | Present but **never compiled** — the tree could not configure (`src/config.cpp` was listed in `src/engine/CMakeLists.txt` but did not exist), and `platform.cpp` used `ctx.Log` / `ctx.Diag` through `subsystem.h`'s forward declarations (incomplete-type errors) |
| `fs.h` / `fs.cpp` | Present, uncompiled; `std::getenv` without `<cstdlib>`; whole-file read via `getline(..., '\0')`, which truncates at a NUL byte |
| `config.h` | Present; no implementation |
| Engine CMakeLists updated | True (`config.cpp` listed) |

No prior build artifacts were reused: the existing `build/` cache was generated
from a now-deleted source path (`/Users/schala/.mlx-serve/workspace/...`) and
could not be reconfigured, so it was moved to `/tmp/lodsc-stale-build-20261025`
(not deleted) and the tree was rebuilt from a fresh configure.

## 2. Changes

**New**

| File | Content |
|---|---|
| `src/engine/include/lod/frame.h`, `src/engine/src/frame.cpp` | `FrameEvents` (relocated from `platform.h`) + `MergeFrameEvents` with documented merge rules |
| `src/engine/src/config.cpp` | Full `key value` config reader/writer/validator |
| `tests/engine/test_config.cpp` | 10 cases: defaults, all-keys parse, comments/CRLF/case, per-key recovery + warnings, clamping, NaN handling, save/load round trip, multiline-title rejection, `IOOpen` on unreadable file |
| `tests/engine/test_fs.cpp` | 11 cases: env-override precedence, bundle-relative resource dir, flat/user-dir/no-home fallbacks, `NotFound` (absent + directory), parent-dir creation + truncating rewrite, binary-safe round trip, empty file, `IOWrite` (EEXIST-as-file parent, ENOTDIR parent), `IOOpen` on unreadable file |
| `tests/engine/test_platform.cpp` | 7 cases: idempotent init/shutdown + reacquire, destructor releases window *and* video subsystem, absurd size clamped, idle poll, QUIT/CLOSE_REQUESTED → quit, resize/iconify/focus translation, quit event terminates the App loop |
| `tests/engine/test_app_frame.cpp` | 8 cases: quit event stops the loop, `RequestQuit` semantics, delta clamp, spiral cap retains backlog, exact tick budget, invalid-option rejection, polling respects init/rollback/shutdown state, frame-event merge rules |

**Modified**

- `src/engine/include/lod/subsystem.h` — `Subsystem::PollFrame()` (default: no
  events) added to the lifecycle contract; `Initialize`/`PollFrame` and the
  `AppContext` fields documented; fallible virtuals marked `[[nodiscard]]`.
- `src/engine/include/lod/platform.h` + `src/engine/src/platform.cpp` —
  `Poll()` → `PollFrame()` override; `SDL_Window` now owned by
  `std::unique_ptr<SDL_Window, SDLWindowDeleter>` with a `Handle()` escape hatch
  (cpp.md §5/§9.1 required this; the raw pointer + manual `SDL_DestroyWindow`
  did not comply); window size clamped to SDL's minimum; init failures log and
  record the SDL detail **plus the available video-driver list**;
  `SDL_EVENT_WINDOW_MINIMIZED`/`RESTORED` explicitly ignored (SDL reports 0×0
  there, which would otherwise look like a usable resize).
- `src/engine/include/lod/app.h` + `src/engine/src/app.cpp` — main loop
  rewritten (see §3): event polling, quit handling, clamped fixed step,
  `LoopStats`, `PollFrameEvents()`, `RequestQuit()`/`IsQuitRequested()`,
  `SetSessionMode()`, `RunOptions::MaxTicks` → `std::optional`, new
  `MaxFrameDeltaSeconds`/`Verbose`, `TickHz`/`MaxTicksPerFrame`/
  `MaxFrameDeltaSeconds` validation (`Status::InvalidArgument`).
- `src/engine/include/lod/config.h` — kept `LoadConfig`/`ValidateConfig`
  signatures and semantics; added `SaveConfig`, `ConfigFileName`, and named
  bounds constants (`MinTickHz`…`MaxWindowSize`); Allman/Doxygen style.
- `src/engine/include/lod/fs.h` + `src/engine/src/fs.cpp` — `<cstdlib>` added;
  whole-file read rewritten (size + `read` + `gcount` check; NUL-safe, short-read
  → `IORead`); write checks flush **and** close; `UserBaseDir` switched to the
  `error_code` overload of `is_directory` (the throwing one would raise
  `filesystem_error`, which is never control flow here); docs now name every
  Status and the fallible functions are `[[nodiscard]]`.
- `src/engine/src/diagnostics.cpp` — terminate handler's `try/catch` guarded by
  the exception-macro feature test so `SC_ENABLE_EXCEPTIONS=OFF` compiles (it
  previously did not, making the advertised policy switch a broken promise).
- `src/host/src/main.cpp` — composition root: CLI parsing (with
  full-range-checked values and per-flag error messages), path resolution,
  config load → CLI overrides → validate → first-run create, `Platform` wiring,
  session mode, tick budget. Flags: `--ticks`, `--tick-hz`, `--windowed`,
  `--headless`, `--width`, `--height`, `--config`, `--crash-dir`,
  `--no-config-write`, `--help`.
- `cmake/ScToolchain.cmake` — sanitizer flags mapped to compiler spelling
  (`-fsanitize=asan` → `address`, `ubsan` → `undefined`, `tsan` → `thread`;
  MSVC: address only, others rejected), `-fno-sanitize-recover=all` added. The
  previous spelling was rejected by Clang, so every sanitizer build failed.
- `tests/engine/CMakeLists.txt` — new sources; CTest groups
  `fs config platform app.loop app.lifecycle app.frame app.timing app.quit
  frame.merge`; host tests sandboxed via `ENVIRONMENT`
  `SC_CONFIG_DIR/SC_DATA_DIR/SC_RESOURCE_DIR` with a fixture that wipes the
  sandbox first, plus `host.config_create`, `host.windowed_flag`,
  `host.bad_args`.
- `README.md` — run/flag/config/directory documentation; layout and policy
  notes updated. `docs/architecture/engine-design.md` — implemented main-loop
  contract (§2) and the interim config file (§5) recorded.

## 3. Architectural decisions

1. **Frame events are a subsystem capability, not a platform singleton.**
   `Subsystem::PollFrame()` + `App::PollFrameEvents()` (merge) instead of
   `App` holding a `Platform*`: no cast, no dangling observer, and the later
   GPU/audio subsystems get per-frame pumping without touching `App`. Only
   `InitializedCount` subsystems are polled, so an un-initialized or
   rolled-back subsystem is never asked for events.
2. **`FrameEvents` moved to `frame.h`.** It is the return type of a
   `Subsystem` virtual, so it cannot live in `platform.h` (which includes
   `subsystem.h`). Type name and fields are unchanged; `platform.h` includes
   `frame.h`.
3. **Frame summary, not a queue.** `resized`/`focusChanged` mean "at least one
   such event this frame", size/focus are newest-valid. Rationale: a swapchain
   is recreated once per frame, so intermediate drag-resize sizes are noise; and
   an iconify's 0×0 must never reach the renderer as a size request. Merge rules
   are pinned by `frame.merge_rules`.
4. **Quit is sticky and honoured at the frame boundary.** The frame that
   reported quit still runs its sim tick (input carried by that event is not
   dropped). `RequestQuit()` during `Initialize()` is not swallowed, and the
   flag is not cleared by `RunMainLoop` — a quit means "stop this session".
5. **Delta clamp + per-frame cap, backlog retained.**
   `accumulator += min(delta, MaxFrameDeltaSeconds)`; the clamp turns a stall
   (modal dialog, debugger, window drag) into skipped wall time instead of a
   catch-up storm; `MaxTicksPerFrame` bounds one frame and the leftover
   accumulator is *kept*, so no sim time is silently lost (a spiral guard that
   zeroes the accumulator would drop game time — rejected). `MaxTicks` bounds
   the frame loop too: `--ticks N` is exact.
6. **Floating-point microseconds for the accumulator, ceiling for the sleep.**
   1e6/60 is not an integer number of microseconds; integer truncation per tick
   drifts the sim, and a truncated sub-microsecond sleep advances a virtual
   clock by nothing — which is exactly how the first version of this loop hung
   the test runner. `std::chrono::ceil<microseconds>` guarantees progress and
   bounds the overshoot to 1us/frame.
7. **`std::optional<uint64_t> MaxTicks`** rather than a `0 = forever` sentinel
   (cpp.md: `optional` over sentinels). The host maps `--ticks 0` → `nullopt`,
   and a bare `lodsc` stays a one-tick lifecycle smoke test in headless mode
   while `lodsc --windowed` stays open until the window closes.
8. **`LoadConfig` keeps `Result<Config>`.** Malformed content is recovered
   per-key (defaults + warning, as the header specified), but an unreadable
   existing file reports `Status::IOOpen` and *"callers decide fatality"*:
   `main` logs and continues with defaults. Chosen over an always-succeeding
   `Config` so the environment problem stays visible at the boundary instead of
   being indistinguishable from a first run.
9. **`SaveConfig` added** (first-run creation + round-trip); it refuses a
   multiline/empty title rather than writing an unloadable file, and `main`
   never rewrites a config it could not read (that would destroy content the
   user might repair).
10. **Host stays a composition root.** CLI parsing + wiring only; the engine
    owns the config format, path policy, window and loop. `--windowed` is
    expressed as `Config::headless` (the config file's own key), so file and
    CLI are the same knob with CLI precedence.
11. **`LoopStats` as the measurement surface** (`frames`, `ticks`,
    `clampedFrames`, `cappedFrames`, accepted + raw max delta, `simHz`), logged
    on loop exit and asserted through injected clocks — AGENTS.md: measure, do
    not assume.

## 4. Build and test results (all executed)

| Configuration | Command | Result |
|---|---|---|
| Default (this tree) | `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --timeout 20` | **18/18 CTest tests passed; 46 cases, 0 failures** |
| Clean clone | fresh `/tmp/sc-clean`, `RelWithDebInfo` | configure + build clean, **18/18 passed** |
| Warnings as errors | `/tmp/sc-werror -DSC_WERROR=ON`, full rebuild | **zero warnings/errors**, 18/18 passed |
| Exceptions off | `/tmp/sc-noexc -DSC_ENABLE_EXCEPTIONS=OFF -DSC_WERROR=ON` | throw-probe correctly fails at configure; build clean; **18/18 passed** |
| Sanitizers | `/tmp/sc-san -DSC_ENABLE_ASAN=ON -DSC_ENABLE_UBSAN=ON` | build clean; **18/18 passed** (no ASan/UBSan report) |
| `psx` standalone | `cmake -S psx -B /tmp/build-psx` | builds, **1/1 passed** |
| Formatting | `clang-format --dry-run -Werror` over engine/host/test sources | no diagnostics |

Every one of the 46 registered cases is matched by at least one CTest entry
(verified by enumerating runner case names against the group filters).

Manual/behavioural checks (macOS, Apple Silicon, SDL 3.4.18):

- `lodsc --ticks 5` (headless, dummy driver): config created, window created,
  clean exit 0, `main loop completed 5 sim tick(s) in 6 frame(s)`.
- Sustained-rate measurement (the loop's actual accuracy, not an assumption):
  `--ticks 300 --tick-hz 60` → `sim 59.95Hz` over 5.02s (300 ticks / 301
  frames); `--ticks 1000 --tick-hz 240` → `sim 239.92Hz` over 4.17s. Short runs
  read low (e.g. 57.65Hz for 5 ticks) because per-sleep scheduler overshoot is
  amortized over few frames; the loop logs what it measured either way.
- `--tick-hz 0.5` → `tick_hz 0.5 out of range [10, 1000] — using default 60`
  (CLI overrides pass through the same validation as the file).
- `lodsc --windowed --ticks 3 --width 800 --height 600`: real `cocoa` driver,
  800×600 window, **exactly 3 ticks** after the budget fix, exit 0.
- Config round trip observed on disk (`tick_hz 60`, `headless true`, …) and
  reloaded.
- `--help`, missing-value and unknown-flag paths exit 2 with usage.

## 5. Deviations from the specification / prior interface

1. **Exception policy conflict in the docs — found during this milestone,
   since resolved by the owner.** `AGENTS.md` ("MANDATORY: No C++ Exceptions",
   `-fno-exceptions` for all targets) contradicted `docs/guidelines/cpp.md` §1
   and `docs/architecture/error-handling.md` §0, which cite "AGENTS.md §4,
   revised 2026-10". Per the brief I followed the **revised** policy:
   `SC_ENABLE_EXCEPTIONS=ON` default, `std::expected` everywhere, no exceptions
   for control flow in project code; the one `try/catch` (terminate handler,
   pre-existing) is feature-guarded so the OFF policy also builds. The owner
   then confirmed "permitted but discouraged" and `AGENTS.md` + every stale doc
   were reconciled — see §6.
2. **`SaveConfig` added** to the config interface (the header declared only
   load/validate). Justification: first-run file creation and round-trip
   verification are documented acceptance items; `LoadConfig`/
   `ValidateConfig` signatures and documented semantics are unchanged.
3. **`FrameEvents` relocated** (`platform.h` → `frame.h`) and
   **`Platform::Poll()` renamed** to the `PollFrame()` override. No callers
   existed (the TU had never compiled), so nothing else changed.
4. **`RunOptions::MaxTicks` is now `std::optional`** — an intentional public
   API change required by "run until quit" (existing `opts.MaxTicks = N`
   assignments still compile).
5. **Config format is `key value` text in `config.txt`**, not the Tag-based
   `config.dcnf` named in roadmap M1. The roadmap also defers the full Tag
   serializer to M6; using the `.dcnf` name for a text file would invite a
   future Tag reader to parse the wrong format, so the interim name is
   deliberate. Recorded in `engine-design.md` §5.
6. **Build-system fix outside the brief's list:** sanitizer flag spelling in
   `ScToolchain.cmake` (required to run the new code under ASan/UBSan at all).
7. **`Platform::Handle()` added** although the GPU layer (its consumer) is not
   in this milestone: cpp.md §9.1 prescribes exactly this named escape hatch for
   the C handle, and the RAII change without it would have hidden the accessor
   behind a cast.

## 6. Post-review follow-ups (naming + policy documentation)

- **Naming** (AGENTS.md / cpp.md §2: "acronyms fully capitalized in every
  identifier position"): `CliOptions` → **`CLIOptions`**
  (`src/host/src/main.cpp`), and the loop's private helpers
  `FpMicros`/`ToFpMicros` → **`FPMicros`/`ToFPMicros`**
  (`src/engine/src/app.cpp`). A whole-tree identifier scan over
  `src/`, `tests/`, `psx/` found no other violations of the acronym rule. The
  owner then required the two-letter I/O codes to follow it as well, so
  **`Status::IoOpen`/`IoRead`/`IoWrite` → `Status::IOOpen`/`IORead`/`IOWrite`** <!-- naming-allow: historical record of a rename: the old spelling is quoted on purpose -->
  across `status.h` (enum values *and* their `StatusName()` strings), `fs.h/.cpp`,
  `config.h/.cpp`, both test files, and every document that cited them. Numeric
  values are unchanged (`IOOpen = 100`, then 101/102) and the codes are not
  persisted anywhere yet, so no migration is involved. `N32`/`N64` stay as they
  are (unit suffixes, not acronyms); `IoHelper` stays in the research docs
  because that is a Java class name cited verbatim.
- **Policy documentation.** "Exceptions permitted but discouraged" is now the
  single stated rule, in `AGENTS.md` §4 and mirrored in `README.md`,
  `cpp.md` §1, `error-handling.md` §1, `build-system.md` §2, `psx-library.md`
  §5, `native-api.md` §1.6, `qt-integration.md`, `roadmap.md` (M1 acceptance),
  `testing.md` §1 and `dependencies.md`. Two things were separated that had
  been conflated: the **switchable project default**
  (`SC_ENABLE_EXCEPTIONS`) versus the **always-exception-free boundaries**
  (`psx`, plugin SDK) — the latter now link `sc::exceptions_off` and are
  checked by their own configure-time probe, so `psx` can no longer gain
  exception support in a default build while its document claims otherwise.
  Qt-backed tools are documented as excluded from an
  `SC_ENABLE_EXCEPTIONS=OFF` build rather than described as building both ways.
- **Observation:** `tests/framework/ScTestMain.cpp` appeared in the initial
  source scan of this session but is absent from the tree now. Nothing
  references it (the test framework target is header-only; the engine runner
  supplies its own `tests/engine/test_main.cpp`) and the preserved stale build
  cache contains no object file for it, so it was never compiled and nothing
  broke. It is noted here because the disappearance is unexplained, and because
  a PascalCase `.cpp` name would have violated the `snake_case` file rule
  anyway.

- **Acronym policy made explicit** in the convention documents (AGENTS.md
  naming section + docs/guidelines/cpp.md §2 + README policy notes): acronyms
  are upper case in types, functions/methods, constants and enum values and
  inside variable names, with the single camelCase exception for a variable that
  *leads* with the acronym (`gpuDevice` vs `deviceGPU`); two-letter acronyms are
  covered (`IO`, `FP`, `ID`); truncations (`Ptr`, `Msg`, `Env`, `Ctx`) are stated
  not to be acronyms; and new identifiers must be audited against the table
  before work is declared complete. A re-scan of `src/`, `tests/`, `psx/` for
  mixed-case acronym fragments found **no remaining violations**.
- **Flagged, not changed:** the docs are inconsistent about struct data members.
  native-api.md §2 writes them camelCase (`minAPIVersion`), which the naming
  table implies ("variables and parameters: camelCase"), while the Milestone 1/2
  engine value types (`AppContext`, `RunOptions`, `LoopStats`, `Config`,
  `fs::Paths`, `FrameEvents`) use PascalCase members (`TickHz`, `WindowTitle`).
  The rule is now written down (camelCase, acronyms per policy) with this
  deviation recorded; the rename itself is mechanical and unpersisted but touches
  many call sites, so it awaits approval.

## 7. Unresolved issues / open items

- **Roadmap M1 acceptance items not in this brief** remain open: text input
  (`SDL_EVENT_TEXT_INPUT` → typed-character model), fullscreen + per-monitor
  selection (incl. the Windows borderless +1px behavior), vsync-mode-driven sim
  rate, gamepad DB loading, crash-log field parity with the reference
  (version/commit/loaded-mods sections — project versioning is deliberately
  deferred per the README policy note), and the 3-platform build matrix. Only
  macOS/Apple Silicon was compiled and run here.
- **Input plumbing (the other half of M1) is implemented after this report was
  first written** and is described in engine-design.md §4.7: `lod::InputManager`
  (SDL-free) plus the SDL translation in `platform.cpp`, covering keyboard,
  gamepad (open/close, axes, buttons), pointer classification, deadzones, the
  Steam Input guard, and the focus/background gate. It is pinned by 8 SDL-free
  state-machine cases and 8 SDL-boundary cases that drive SDL's virtual
  joystick. Pointer events are *classified only*: there is no UI consumer yet,
  so delivery would be speculative infrastructure (AGENTS.md §4).
- **Controller testing is virtual-only.** No retail controller was used: the
  tests attach SDL virtual gamepads (full button/axis mapping) and one injected
  event naming a pad id that does not exist. Whether SDL maps the Steam Deck's
  built-in controls or the Switch Online adapters is unverified here. See
  roadmap Cross-Milestone Notes item 6.
- **`Status::IORead` has no test**: reaching it needs a short-read/fault seam
  (testing.md §6 `FaultInjector` for the IO layer) — adding production seams for
  a hypothetical was out of scope.
- **Leak checking is not actually performed on macOS** (ASan leak detection is
  unsupported there). The sanitizer lane is green for memory-error/UB detection
  only; leak verification needs the Linux CI lane.
- **Permission-based tests are POSIX-only** (`chmod 0` → `IOOpen`): skipped on
  Windows and they would fail when the suite runs as root.
- **`SDL_HINT_VIDEO_DRIVER` is process-global**: a headless `Platform` sets the
  dummy-driver hint for the whole process, so mixed headless/windowed
  `Platform`s in one process are unsupported (tests are headless-only; a real
  session is one or the other).
- **Config gaps (deliberate, milestone-scoped):** no `vsyncMode`, fullscreen,
  monitor, or persisted keybinding entries. Input *bindings* exist at runtime
  (`InputManager::SetBindings`); nothing loads them from disk yet, which is what
  the reference's `InputBindings` data does and what M6's keybinding tables will
  need. No `key=value` separator spelling; a value cannot contain a newline
  (rejected by design).
- **`host.windowed_flag` asserts the applied mode, not window contents** — on a
  display-less runner SDL may fail to create a window, and CTest's
  `PASS_REGULAR_EXPRESSION` ignores the exit code, so the test verifies the
  flag's effect rather than rendering (rendering is M2).
- **Tree hygiene:** this working tree is **not a Git repository** (no commit was
  or could be made), and `AGENTS.md` refers to a `progress.md` that does not
  exist in it. The unusable stale build cache was preserved at
  `/tmp/lodsc-stale-build-20261025` rather than deleted.
