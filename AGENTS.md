# Severed Chains Native — Agent Guide

## Project Goals

Cross-platform native C++23 reimplementation of *The Legend of Dragoon: Severed Chains*,
preserving gameplay, narrative, combat, audiovisual presentation, and timing, plus
Severed Chains' enhancements and modding capabilities.

- **Not** a PS1 emulator; no cycle-accuracy or PS1 hardware limits unless they serve
  genuine gameplay/compatibility purposes.
- Reusable engine technology must be kept separate from Legend of Dragoon-specific code.
  The engine should be potentially reusable for other PS1 games (e.g. *Legend of Legaia*).
- Original game data must be legally obtained by the player; we never distribute game data.
- Use measurements, not assumptions, for performance work. Do not assume C++ is faster.

## Authoritative Reference Repository

- **Path:** `~/Documents/GitHub/Severed-Chains` (Java, Gradle, LWJGL/SDL3/OpenGL, JavaFX)
- **Treat as READ-ONLY.** Never modify its source, config, build files, or Git history.
- Record its commit hash in `progress.md` before starting research (done: see progress.md).
- Java source references in docs use paths relative to that repository root.
- Do not confuse it with the native C++ project (this repository).

## Approved Technology Stack

| Technology | Purpose |
|---|---|
| C++23 | Core engine and game |
| C17 | Reusable `psx` library core (stable C ABI; C++23 wrappers on top) |
| SDL3 | Windowing, input, controllers, audio, platform integration |
| SDL3 GPU | Rendering (Vulkan / Metal / D3D12 backends) |
| GLM | Mathematics |
| EnTT | Entity-component management and events (where it pays off) |
| Luau | Gameplay scripting and portable modding |
| Qt 6 | Launcher, settings, desktop UI, development tools |
| CMake + Ninja | Build system |

- SDL_shadercross and Dear ImGui are **not** part of this project.
- Extra third-party dependencies require a documented justification
  (purpose, alternatives, license, portability, maintenance) and explicit approval.
- Player-facing game UI (menus, HUDs, dialogue) renders via SDL3 GPU, **not** Qt widgets.
- Keep the core engine Qt-independent where practical.

## C++23 Coding Standards

- **MANDATORY READING: `docs/guidelines/cpp.md`** — every C++ implementation
  task follows it before and during work. Modern, type-safe C++23 is the
  default idiom: `std::format`/`std::print`-based logging with literal format
  strings (`std::format_string`), `std::expected<T, Status>` error
  propagation, RAII/value semantics, `std::span`/`std::string_view`/
  `std::filesystem`/ranges, `enum class`/`constexpr`/`[[nodiscard]]`. The
  printf family, `std::va_list`, C strings in C++ APIs, raw owning pointers,
  and unnecessary macros are **prohibited**; the documented exceptions are
  C third-party interop (SDL3) and PSX binary-format work (guidelines §9).
- RAII, clear ownership/lifetimes, value semantics, explicit error/result types
  (`std::expected` / error enums), minimal global mutable state, minimal inheritance.
- Clear interfaces between subsystems; standard containers and algorithms.

### MANDATORY: Exception Policy — Permitted but Discouraged

- C++ exceptions are **permitted but discouraged** in project-owned C++ code
  (engine, game, tools, Qt integration, Luau bindings, plugins, tests).
  `std::expected<T, Status>` / explicit error enums remain the primary error
  path for recoverable failures. Any deliberate use of exceptions must be
  justified by a concrete advantage over `std::expected` (guidelines §1/§4).
- Never use exceptions for normal control flow, routine validation, resource
  lookups, or expected failure conditions. Catch them at subsystem or
  application boundaries, preserve the diagnostic information, and never
  swallow failures with a blanket `catch (...)`. Destructors must not throw.
- Exceptions must never propagate across module, plugin, or public API
  boundaries, and never across a C ABI. Luau runtime errors are handled via
  Luau's own error-reporting facilities, never by letting exceptions cross the
  scripting boundary.
- **Exception-free by contract** regardless of build configuration: the `psx`
  library and the native plugin API/SDK. The `psx` **core is C17** — it has no exception
  mechanism at all — and its C++23 wrapper/test/fixture targets always compile with
  `-fno-exceptions` / `-fno-rtti` (`/EHs-c-`, `/GR-`), as does the plugin SDK
  (build-system.md §2, psx-c-api.md §7, psx-library.md §5, native-api.md §1.6).
- **The `psx` core is C17, not C++** (psx-c-api.md): no C++ runtime, no third-party
  dependency, libc allocator only; stable `psx_error_t` codes instead of `std::expected`;
  `PSX_API` + hidden visibility define the exported set. C++23 lives in the header-only
  `psx::cpp` wrappers, which delegate to the C core and never reimplement it. The C++
  guidelines below apply to project-owned C++ (engine, game, tools, wrappers, tests); C
  sources follow the C rules in psx-c-api.md §2-§5.
- All other project targets compile with exceptions **enabled** by default.
  `SC_ENABLE_EXCEPTIONS=OFF` disables them for every project-owned target
  (Clang/GCC `-fno-exceptions`, MSVC `/EHs-c-`) and a configure-time probe
  verifies the effective flags; the codebase must build under both policies.
- Precompiled third-party libraries (e.g. Qt 6) may have their own exception behavior;
  document limitations, do not assume our flags change them.
- **No RTTI across public API boundaries:** the native plugin API and engine public
  headers compile with RTTI disabled (Clang/GCC `-fno-rtti`, MSVC `/GR-`) and never
  use `dynamic_cast` (native-api.md §1.6). Engine-internal code may use RTTI only
  where it does not appear in a public header.

### MANDATORY: Doxygen-Commented Public APIs

- **All public project-owned C++ APIs carry Doxygen-compatible comments**: engine
  interfaces, the PSX library APIs (psx-library.md), and the native plugin interface
  (native-api.md) first; internal helpers where clarity benefits.
- Document: purpose, `@param` for each parameter (in/out/ownership), `@return`
  / `std::expected` semantics, `@errors` (which `Status` codes and when) for
  fallible APIs,
  **ownership/lifetime** of any handle or pointer, and **thread safety** (which
  thread may call; default is main-thread unless documented).
- Use standard Doxygen tags (`/** ... */`, `@brief`, `@param`, `@return`, `@errors`,
  `@note`, `@see`). Thread-safety notes use the standard tag form
  `@note Thread: ...` — this Doxygen build cannot parse custom command
  aliases, so non-standard commands (e.g. a custom `@thread`) are avoided.
  Generated docs land in `docs/api/` (build target, see build-system.md).
- Third-party API names are preserved verbatim (no re-naming to fit our conventions).

### Naming Conventions (mandatory for project-owned code)

| Element | Convention | Example |
|---|---|---|
| Namespaces | **libraries only** (no per-module/per-class namespaces) | `lod` (engine), `game`, `psx`, `lod::test` |
| Classes, structs, functions, methods, constants, enum values | PascalCase | `RenderEngine`, `DecompressBPE`, `EntitySpawn`, `OnLoad`, `MaxVoices`, `Status::Ok` |
| Variables and parameters | camelCase; acronyms per the acronym policy below | `simFrame`, `gpuDevice`, `deviceGPU` |
| Private fields | camelCase with trailing underscore | `simFrame_`, `windowGPU_` |
| Macros | UPPER_SNAKE_CASE | `LOD_API`, `LOD_NOEXCEPT_TARGETS` |
| C-linkage discovery symbols (plugin entry only) | PascalCase with `LoD` prefix | `LoDPluginCreateV1` |
| Files | snake_case | `render_engine.cpp`, `render_engine.hpp` |

- **Acronym policy (mandatory for every non-trivial identifier).** Acronyms are
  written in **upper case** in type names, functions and methods, constants and
  enum values, and in variables wherever the acronym is *not* the first element
  of the name. Two-letter acronyms count (`IO`, `FP`, `ID`).

  | Element | Correct | Wrong |
  |---|---|---|
  | Type | `CLIOptions`, `XADecoder`, `GTEView`, `SDLWindowPtr` | `CliOptions`, `XaDecoder`, `GteView`, `SdlWindowPtr` |
  | Function / method | `DecompressBPE()`, `ToFPMicros()` | `DecompressBpe()`, `ToFpMicros()` |
  | Constant / enum value | `Status::IOOpen`, `MaxHTTPRetries` | `Status::IoOpen`, `MaxHttpRetries` | <!-- naming-allow: the wrong-spelling column of the naming table -->
  | Variable, acronym later in the name | `deviceGPU`, `tableID`, `rawXABlock` | `deviceGpu`, `tableId`, `rawXaBlock` |
  | Variable, acronym **first** (the sole exception) | `gpuDevice`, `idTable`, `cliOptions` | `GPUDevice`, `IDTable`, `CliOptions` |
  | Data member (public) | `TickHz`, `MinAPIVersion`, `ResourceDir` | `tickHz`, `minAPIVersion`, `resourceDir` | <!-- naming-allow: the wrong-spelling column of the naming table -->

  The exception exists only because variables are camelCase: a name that
  *begins* with the acronym starts in lower case (`gpuDevice`), while the same
  acronym anywhere else in the same name is upper case (`deviceGPU`, `tableID`).
  Types, functions, methods, constants and enum values have **no** exception —
  they are PascalCase from the first character (`CLIOptions`, `IOOpen`,
  `ToFPMicros`, `T::ID`).
- **Truncations are not acronyms.** `Ptr`, `Buf`, `Msg`, `Env`, `Ctx`, `Tmp`,
  `Args` are clipped words, not initialisms, and stay mixed-case
  (`SDLWindowPtr`, `MsgLevel`). Prefer the full word in public APIs anyway
  (`Pointer`, `Message`, `Context`); this policy neither forbids nor encourages
  abbreviations.
- **The audit is mechanical, not a memory exercise.**
  `python3 tools/check_naming.py` (CTest entry `repo.naming`) checks every
  identifier in project code *and* every identifier the documentation prescribes
  (`Status::DecodeUnsupportedBPP`, `Error::MRGEntryOutOfRange`, …) — a mis-cased
  name in a doc becomes mis-cased code. It must be clean before work is reported
  complete. Third-party and reference names are exempt because they are preserved
  verbatim (SDL_*, luau_*, Qt; Java class names cited in research docs, e.g.
  `IoHelper`, `XaAdpcm`); a deliberate quotation or rule counter-example carries a
  `<!-- naming-allow: reason -->` marker on its line. Function-scope *named
  constants* are constants, not variables: `MaxSector`, never `maxSector`).
- **Data members are not variables.** The camelCase rule above covers *scoped*
  variables and parameters only. A data member takes the data form instead:
  **public data members are PascalCase** (`TickHz`, `WindowTitle`, `ResourceDir`,
  `MinAPIVersion`), **private and protected members are camelCase with a trailing
  underscore** (`simFrame_`, `crashDir_`, `tableID_`). The underscore marks a name
  as member state rather than a scoped variable at the use site; PascalCase marks a
  public field as data sitting beside PascalCase methods. A `constexpr` /
  `static constexpr` member is a *constant*, not member state, so it keeps the
  PascalCase constant position (`NoSource`, `RepeatIntervalNs`) and never takes the
  underscore. A callable member (a function pointer invoked as `tc.fn(ok)`) follows
  the function convention — camelCase, no underscore — because the use site is a
  call. Third-party struct members are preserved verbatim (`event.key`,
  `event.gaxis.axis`), like any other third-party name. All engine value types
  (`AppContext`, `RunOptions`, `LoopStats`, `Config`, `fs::Paths`, `FrameEvents`)
  now follow this, and the frozen-layout `PluginInfo` example in native-api.md §2 is
  written to it as well.
- Free functions and struct/class methods alike are PascalCase
  (e.g. `DecompressBPE(bytes)`, `EngineAPI::QueryTable(...)`,
  `Plugin::OnLoad(...)`); camelCase is reserved for scoped
  variables/parameters and private member state.
- String *ids* and table id constants (e.g. `"lod.gameplay"`, registry ids,
  event ids) are lowercase per the id grammar (mod-loader.md §4) — they are
  identifiers in a string namespace, not C++ identifiers.
- Third-party API naming is preserved (e.g. SDL_*, luau_*, Qt names stay
  as-is; Java reference class names are cited verbatim in docs, e.g.
  `GlApi`, `XaAdpcm`).
- Enum values are PascalCase; `enum class` type names are PascalCase plural or
  singular by clarity (`EntryKind`, `Priority`).

### Formatting: .clang-format (mandatory)

- A root-level `.clang-format` (LLVM baseline, 2-space indent, **Allman
  braces**, **no braces around single-statement bodies**, 120-column limit,
  grouped/sorted includes, Doxygen comments left
  intact) is the
  single source of formatting truth. Run `clang-format` on project-owned files you
  touch; do **not** mass-reformat untouched files.
- **Single-statement `if`/`else`/`for`/`while` bodies are written without braces**
  (`RemoveBracesLLVM: true`, docs/guidelines/cpp.md §6.1). Braces stay where removal
  would change meaning: a body that is itself a braced-less `if`, a body that declares
  a variable, multi-statement bodies, function/lambda bodies.
- The formatting gate is **`python3 tools/check_format.py`** (CTest entry `repo.format`):
  it runs `clang-format --dry-run --Werror` over every project source. On a host with no
  `clang-format` at least version 17 it prints that the style rules were **not** verified
  and exits 77, which CTest reports as *skipped* — never as passed, and never as a failure
  on code `clang-format` itself considers clean. The script also searches the
  version-suffixed names (`clang-format-18`, …) because that is how the Linux runner
  images name the tool; probing only the bare name made a fully-tooled runner look
  untooled. An earlier structural brace scan filled this slot and has been **removed**: it
  failed 41 lines that `clang-format` 23.1 reports clean, including braced bodies where
  braces are mandatory. Like `repo.naming`, this is a host-tool check: skipped with a
  visible message when the tool is missing, never silently passed.
- Doxygen comment blocks are preserved (no reflow that breaks `@param`/`@return`
  alignment); clang-format's `ReflowComments` stays off.
- Where hand layout aids readability (e.g. aligned struct fields), prefer
  clang-format output; document intentional deviations with a `// NOLINT` + reason.

## Architectural Boundaries

- **PSX library (reusable PlayStation 1 tech, independent):** GTE math, PS1 GPU/GS
  semantics + software reference rasterizer, SPU voice model, XA/SShd/SSSq decoding,
  ISO 9660 (PS1) reading, TMD/TIM/ANM/MRG/DEFF/BPE/Tag formats — a **C17 core** (target
  `psx`, C symbols `psx_*`, deps = libc only) with **header-only C++23 wrappers** (target
  `psx_cpp`, namespace `psx::`), no SDL/Qt/game logic (psx-library.md, psx-c-api.md).
  **Prefix-free identity** (no Sc/SC/sc, no LoD/LOD/lod; it may be extracted to its own
  repo), root-level subproject; build form follows CMake `BUILD_SHARED_LIBS` (the user's
  choice), overridable with `PSX_SHARED`. Consumed by engine, game, and tools.
- **Engine (reusable):** platform/lifecycle, SDL3 GPU rendering, input, audio, assets,
  scene/ECS, animation, save serialization, scripting host, native plugin host, timing.
- **Game (LoD-specific):** characters, combat, Additions, dialogue/events, submaps,
  world map, saves content, retail registries — the equivalent of the Java `lod` mod.
- **Mogul Dabas minigame:** separate C++23 library (`src/libs/dabas`, target
  `scdabas`) for the original in-game handheld minigame; first-party content,
  not a plugin, not PSX
  hardware (dig-dabas.md §5).
- **Desktop tools (Qt 6):** launcher, mod manager, asset inspection, debuggers.
- **Native plugin API:** **C++23 interface (not a C ABI)** — C-linkage discovery
  symbol only; frozen-layout C++ interfaces, toolchain-parity rule, RAII ownership,
  no EnTT/psx exposure, versioned interfaces (native-api.md). Qt's plugin system is
  **not** used for the engine's plugin architecture.
- **Luau:** primary portable scripting; independent of native plugins; both share
  controlled engine services. Luau APIs must not be required by native extensions.

## Research Methodology

- Work in bounded subsystems; persist findings into `docs/` as you go.
- Every significant finding must cite concrete Java source files/classes/symbols
  (paths relative to `~/Documents/GitHub/Severed-Chains`).
- Distinguish **verified behavior** from **assumptions** and **open questions**.
- Do not guess: if a question can't be answered from available sources, document the gap.
- Write research documentation only into THIS repository.

## Build Requirements

- CMake + Ninja; clean-clone build procedure documented per platform
  (Windows, Linux, macOS/Apple Silicon).
- Exception policy enforced by CMake and switchable: exceptions are enabled by
  default (permitted but discouraged); `SC_ENABLE_EXCEPTIONS=OFF` builds every
  project-owned target with `-fno-exceptions` / `/EHs-c-` and a configure-time
  probe verifies it. `psx` (C17 core — structural, plus its C++ wrapper/test targets) and
  the plugin SDK are always exception-free. Public
  API targets are additionally built with RTTI disabled (see above).
- `psx` compiles standalone (no engine/SDL/Qt/Luau dependencies) — enforced by the
  build system (build-system.md) and by an exported-symbol audit that fails the suite if
  the core ever references a C++ runtime or third-party symbol (psx-c-api.md §9).
  Project-owned C++ libraries are declared without a library type: CMake's built-in
  `BUILD_SHARED_LIBS` selects the build form at configuration time — the architecture
  prescribes no default (the choice is the user's); `psx` defaults to shared and takes
  `BUILD_SHARED_LIBS` as its default when set.
- Pinned, stable dependency versions; straightforward builds; avoid unnecessary
  Git submodules.
- Shader pipeline must produce backend-appropriate artifacts (SPIR-V / Metal / DXIL)
  without SDL_shadercross.

## Licensing Considerations

- Original Severed Chains: AGPL-3.0 (verify against reference repo `LICENSE`).
- This project is an **independent derivative implementation**; document derivative-work
  obligations (attribution, source availability, distribution terms).
- Qt 6 licensing (LGPL v3 / commercial) implications must be documented.
- Proprietary LoD assets are never distributed; players supply their own legal data.
- Do not assume mods share the project's license; document per-mod licensing uncertainty.

## Existing Code Evaluation, Improvement, and Preservation

### 1. Core Philosophy

Existing code is a starting point, not an unquestionable authority.

Code written by previous developers, AI models, or agents may contain architectural weaknesses, implementation mistakes, unnecessary complexity, performance problems, or missed opportunities.

You are explicitly authorized and encouraged to identify and improve these deficiencies.

However, changes must be purposeful, technically justified, and proportionate to their benefits.

**Preserve what is good. Improve what is deficient. Replace what is demonstrably inferior.**

Do not blindly preserve existing implementations, but do not rewrite them merely because you would have approached the problem differently.

### 2. Critical Evaluation

When examining existing code:

- Evaluate correctness, safety, maintainability, performance, and architectural consistency.
- Identify actual defects separately from stylistic preferences.
- Question existing design decisions when there is a sound technical reason.
- Recognize opportunities to simplify code without sacrificing functionality.
- Identify redundant abstractions, unnecessary dependencies, duplicated logic, and avoidable complexity.
- Consider whether modern language features or established libraries provide a demonstrably better solution.
- Respect documented requirements and constraints unless they conflict with correctness or other explicit project goals.

**Previous AI-generated code receives no special deference.** Evaluate it using the same engineering standards as any other implementation.

### 3. Improvement Authorization

You may refactor, restructure, optimize, or replace existing implementations when doing so provides meaningful benefits.

Valid justifications include:

- Fixing bugs, undefined behavior, resource leaks, or concurrency problems.
- Improving memory safety, ownership clarity, and resource management.
- Correcting architectural weaknesses or violations of project requirements.
- Eliminating unnecessary complexity or duplication.
- Improving performance where meaningful or measurable.
- Improving testability, extensibility, and long-term maintainability.
- Replacing fragile workarounds with robust implementations.
- Improving compatibility with the project's language standard, frameworks, and supported platforms.

Do not require explicit approval for every routine improvement within an authorized task.

However, **propose substantial architectural changes before implementing them** when they affect public APIs, subsystem boundaries, persistence formats, external compatibility, or multiple unrelated components.

### 4. Change Scope and Discipline

Exercise engineering judgment rather than enforcing minimal changes at all costs.

- Prefer focused changes when they adequately solve the problem.
- Broaden the scope when a localized fix would preserve a fundamental defect.
- Avoid unrelated refactoring that increases review difficulty or regression risk.
- Preserve working behavior unless changing that behavior is intentional and justified.
- Avoid introducing abstractions without a concrete purpose.
- Do not add speculative infrastructure for hypothetical future requirements.
- Do not sacrifice architectural quality merely to minimize a diff.
- Never silently discard functionality, error handling, or platform support.

**A larger, well-justified improvement is preferable to a smaller change that perpetuates a flawed design.**

### 5. Evidence-Based Engineering

Distinguish verified findings from assumptions.

Before recommending or implementing significant changes:

1. Understand the existing implementation and its dependencies.
2. Identify the specific problem or improvement opportunity.
3. Evaluate alternative solutions and their tradeoffs.
4. Select the solution that best satisfies project requirements.
5. Implement the change with appropriate validation.
6. Check for regressions and unintended consequences.

Where applicable:

- Use compiler diagnostics, static analysis, tests, profiling, and benchmarks.
- Validate claims about performance rather than relying on intuition.
- Consider portability, ABI compatibility, and downstream consumers.
- Never claim a change is tested or verified unless the corresponding validation actually occurred.
- Clearly disclose unavailable tests, unverified assumptions, and remaining risks.

### 6. Architectural Consistency

Existing architecture should be understood before being modified.

- Follow established conventions unless there is a compelling reason to improve them.
- Treat `AGENTS.md`, project documentation, and explicit user requirements as authoritative constraints.
- Distinguish intentional design decisions from accidental implementation patterns.
- Do not perpetuate mistakes simply to maintain consistency with previous mistakes.
- When improving a convention, apply the change consistently within the approved scope.
- Prefer coherent subsystem design over isolated patches that create long-term technical debt.

When existing architecture conflicts with an explicit requirement, prioritize the requirement and explain the conflict.

### 7. Collaboration and Transparency

Operate as an independent, technically critical engineering collaborator rather than an unquestioning code generator.

- Challenge flawed assumptions constructively.
- Explain significant design decisions and their tradeoffs.
- Do not agree with existing implementations merely because they already exist.
- Do not manufacture criticisms to justify making changes.
- Avoid excessive commentary for straightforward modifications.
- Surface important discoveries even when they fall outside the immediate task, but do not automatically modify unrelated code.
- Clearly distinguish required fixes, recommended improvements, and optional enhancements.

For substantial refactoring, communicate:

- **Problem:** What is deficient and why.
- **Solution:** What should change.
- **Benefit:** What the improvement accomplishes.
- **Risk:** What could break or require migration.
- **Validation:** How correctness will be established.

### 8. Regression Prevention

Improvement must not come at the expense of reliability.

- Preserve existing tests and add or update tests when behavior changes.
- Check affected callers, interfaces, and dependencies.
- Do not weaken validation or remove safeguards to make tests pass.
- Do not suppress compiler warnings instead of addressing their causes.
- Do not modify generated files when their source definitions should be changed.
- Review changes for accidental loss of functionality.
- Keep modifications reviewable and compatible with the project's version-control workflow.

Never use destructive Git operations to conceal mistakes or discard unrelated user changes.

### 9. Continuous Codebase Improvement

Treat software quality as an ongoing responsibility, not a one-time cleanup exercise.

While implementing requested functionality, remain attentive to opportunities for meaningful improvements.

Prioritize:

1. Correctness and preservation of required behavior.
2. Safety, reliability, and resource management.
3. Compliance with explicit project requirements.
4. Architectural coherence and maintainability.
5. Performance and resource efficiency.
6. Simplicity, readability, and consistency.

These priorities are guidelines, not absolute rules; apply judgment according to the project's requirements.

Do not initiate large-scale cleanup campaigns without a concrete justification or appropriate authorization.

### 10. Final Engineering Principle

**Your objective is not to preserve the previous model's code, nor to replace it with your own. Your objective is to produce the best technically sound implementation for the project's requirements.**

Evaluate existing work independently, preserve its strengths, correct its weaknesses, and pursue meaningful improvements.

Be ambitious about quality, disciplined about scope, and rigorous about verification.

**Code quality takes precedence over model loyalty, stylistic preference, and unnecessary change avoidance.**
