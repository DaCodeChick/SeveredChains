# Build System — CMake + Ninja

Requirements: CMake + Ninja; reproducible builds on Windows / Linux / macOS
(Apple Silicon); explicit switchable exception policy (exceptions permitted but discouraged, `SC_ENABLE_EXCEPTIONS` default ON; `psx` and the plugin SDK exception-free by contract); stable
pinned dependencies; no unnecessary submodules; documented clean-clone
procedure + platform prerequisites; shader compilation integrated; engine
library + thin host; Debug/Release configurations; packaging.

## 1. Top-Level Layout

```
CMakeLists.txt                # root: project(), C++23, options, subdirs
psx/                          # reusable PS1 library — C17 core `psx` (psx::psx) +
                              #   header-only C++23 wrappers `psx_cpp` (psx::cpp).
                              #   PREFIX-FREE (no Sc/SC/sc, no LoD/LOD/lod), deps = libc only,
                              #   compiles standalone; a self-contained subproject that may
                              #   be extracted into its own repo. psx-library.md, psx-c-api.md
cmake/
  ScToolchain.cmake           # compiler flags, exception/RTTI policy, sanitizers,
                              #   sc_add_library (build form follows
                              #   CMake's BUILD_SHARED_LIBS — user's choice)
  ScDeps.cmake                # dependency resolution (FetchContent/vcpkg/Qt6)
  ScShader.cmake              # shader build rules (glslang/spirv-cross/dxc)
  ScPackaging.cmake           # install rules, bundle layout
  versions.cmake              # pinned versions (single source of truth;
                              #   arrives with the first fetched dependency, M2)
src/engine/CMakeLists.txt     # scengine (build form: CMake BUILD_SHARED_LIBS)
src/game/CMakeLists.txt       # scgame (build form: CMake BUILD_SHARED_LIBS; the
                              #   "lod" content lives here)
src/libs/dabas/CMakeLists.txt # scdabas (build form: CMake BUILD_SHARED_LIBS;
                              #   std-only — the Mogul Dabas minigame)
src/sdk/CMakeLists.txt        # sc-sdk (plugin SDK: frozen headers + toolchain file)
src/tools/qt/CMakeLists.txt   # launcher, settings, modmanager, debugger, inspector
src/tools/fate/CMakeLists.txt # fate CLI (pure C++, no Qt; links psx only)
src/plugins/examples/         # sample plugin (explicitly SHARED — must be
                              #   loadable; C++23 SDK consumer)
tests/                        # minimal in-repo test runner (no new dep) — see testing.md
psx/tests/                    # psx standalone tests (sc_tests_psx, sc_psx_c_consumer,
                              #   psx.abi.symbols) — link psx only; psx/tests/abi_symbols.cmake
psx/migration/                 # frozen pre-migration C++ implementation + differential corpus
                              #   test (opt-in: -DPSX_ENABLE_DIFFERENTIAL_TEST=ON)
```

**Build form (owner decision): the user's choice, via CMake's built-in
`BUILD_SHARED_LIBS`.** All project-owned libraries (`psx`, `scengine`,
`scgame`, `scdabas`) follow that switch: `scengine`/`scgame`/`scdabas` are declared
**without a library type** (`add_library(<name> <sources>)`), and `psx` defaults
`PSX_SHARED` to the value of `BUILD_SHARED_LIBS` when the user sets it (shared when the
variable is absent, which is what the C-ABI migration brief pins). `PSX_SHARED` is the
explicit override for the psx core alone. The architecture prescribes no build form — that
choice belongs to the user; both psx forms get the same symbol/ABI audit (psx-c-api.md §9).
A `sc_add_library(<name> ...)` helper in `ScToolchain.cmake` wraps the
typeless declaration plus `sc::exceptions_policy`/`sc_nortti` and
visibility flags.
One explicit exception: the **sample plugin is always SHARED** (it must be
loadable at runtime); the host is always an executable.

**Targets:**
| Target | Type | Links |
|---|---|---|
| `psx` | C17 library (`PSX_SHARED`, default shared; `SOVERSION` = `PSX_ABI_VERSION`); **prefix-free**, **zero third-party deps**; hidden visibility, only `PSX_API` symbols exported; no SDL/Qt/Luau/engine | libc allocator only (`malloc`/`calloc`/`free`) |
| `psx_cpp` | INTERFACE (header-only C++23 wrappers over `psx`) | `psx`, C++23 std |
| `scdabas` | library (typeless); std only; host-interface driven | C++23 std only |
| `scengine` | library (typeless); symbol prefix `sc_`, hidden visibility (shared builds) | **psx**, SDL3, SDL3_image, GLM (hdr), EnTT (hdr), Luau, (libcurl/libopus optional) |
| `scgame` | library (typeless); the "lod" content module | scengine, **scdabas** |
| `lodsc` (host) | EXECUTABLE (thin) — the app (LoD: Severed Chains) | scgame, scengine |
| `severed-chains-launcher` / `-settings` / `-modmanager` / `-debugger` / `-inspector` | EXECUTABLE | scengine + Qt6 |
| `fate` | EXECUTABLE | **psx** (script/fate parts — no engine needed) + in-repo fate compiler |
| `sc-sdk` | INTERFACE (plugin SDK headers + toolchain file; engine import library in shared builds) | — (consumers link scengine) |
| `scplugin_example` | SHARED (explicit — must be loadable at runtime; **C++23 against sc-sdk**) | scengine (via SDK import lib) |
| `sc-tests`, `lod-fuzz` | EXECUTABLE | per testing.md |
| `sc_tests_psx` | EXECUTABLE (standalone subproject) | `psx::cpp` (→ `psx`) |
| `sc_psx_c_consumer` | EXECUTABLE, **compiled as C17 only** | `psx` |
| `psx_legacy` + `sc_tests_psx_differential` | STATIC fixture + EXECUTABLE, opt-in | `psx::cpp`, `psx_legacy` (never installed/exported) |
| `sc-api-docs` | CUSTOM (doxygen over engine + psx + SDK public headers → `docs/api/`) | — |

## 2. Exception-Policy & RTTI Enforcement

Exception policy follows AGENTS.md §4 (revised 2026-10): exceptions are
**permitted but discouraged** — `std::expected` is the primary error path.
The policy is explicit and switchable (`SC_ENABLE_EXCEPTIONS`, default ON);
when OFF, `-fno-exceptions` / `/EHs-c-` applies to every project-owned
target and a configure-time probe checks that the flag actually did something.
The probe is **per-toolchain, not aspirational** — demanding the same evidence
from compilers that cannot produce it would only make the check fake:

| Outcome | When | Meaning |
|---|---|---|
| `enforced` | clang / GCC | `throw_probe.cpp` stops compiling with the flag on. If it still compiles, the flag was dropped → `FATAL_ERROR`. |
| `macro-verified` | MSVC | No flag forbids a throw (`/EH` describes cleanup *after* one), so the probe proves the exception-model macro flips instead: `_CPPUNWIND` / `__cpp_exceptions` undefined with the flag, defined without it. That is the whole observable signal `cl.exe` offers, and roadmap M1 acceptance is written to it. |
| `unverifiable` | anything else | Neither test can decide; named in the configure log — reported, not assumed either way. |

The non-vacuity guard reuses the same probe source, so a compiler that never
defines the exception macro at all is reported rather than silently skipping the
check. The macro path was proven against a `cl`-compatible driver
(`clang --driver-mode=cl`) before any real `cl.exe` was available.

Two interfaces, because two different things are being expressed:
`sc::exceptions_policy` carries the *project-wide* switch (no flags when ON),
while `sc::exceptions_off` always applies the flags and is linked by the
targets whose exception-free-ness is a contract rather than a preference
(the psx C++ surface — wrappers, test runner and the migration fixture, which are built
`-fno-exceptions`/`-fno-rtti` exactly as the C core's contract requires; plugin SDK —
native-api.md §1.6/§8). A second
configure-time probe compiles `throw_probe.cpp` with `sc::exceptions_off`
flags whatever the project-wide policy says.
The psx **core** needs no such flags: it is C17, which has no exception mechanism, and its
dependency-free/exception-free contract is instead enforced structurally plus by the
symbol audit (psx-c-api.md §9).

`ScToolchain.cmake`:
```cmake
# Exception-policy interface linked into EVERY project-owned target.
add_library(sc_exceptions_policy INTERFACE)
if(NOT SC_ENABLE_EXCEPTIONS)
  if(MSVC)
    target_compile_options(sc_exceptions_policy INTERFACE /EHs-c-)
  else()
    target_compile_options(sc_exceptions_policy INTERFACE -fno-exceptions)
  endif()
endif()
target_compile_features(sc_exceptions_policy INTERFACE cxx_std_23)

# Public API targets (psx C++ surface, scengine public, sc-sdk) additionally:
add_library(sc_nortti INTERFACE)
target_link_libraries(sc_nortti INTERFACE sc_noexcept)
if(MSVC)
  target_compile_options(sc_nortti INTERFACE /GR-)
else()
  target_compile_options(sc_nortti INTERFACE -fno-rtti)
endif()
```
- Every target: `target_link_libraries(<t> PRIVATE sc::exceptions_policy)`;
  the **public API targets** (`psx`, `sc-sdk`) additionally link `sc_nortti`
  (AGENTS.md: no RTTI across public API boundaries — the plugin API and
  engine public headers never `dynamic_cast`).
- **Qt bridge:** `qtbridge` sources (the narrow C-shim TUs,
  qt-integration.md §2) are exactly where the "permitted" half of the policy
  applies — Qt 6 throws, so interop code may catch. Under
  `SC_ENABLE_EXCEPTIONS=OFF` the Qt-backed tools are excluded from the build
  (documented limitation, not a silent downgrade).
- **Plugin SDK toolchain file** (`sc_toolchain.cmake`, native-api.md §8):
  plugin builds import it — C++23, `-fno-exceptions -fno-rtti`, the same
  warning/visibility flags, and it records the **toolchain hash** that
  `PluginInfo::toolchain` must carry (the loader enforces the match).
- **Verification (Milestone 1 acceptance — implemented; runs at configure
  time):** the probes measure whether a policy flag changed what the compiler
  accepts, not merely that the flag was passed. Severity is set by what the
  toolchain can actually prove: a *dropped* flag (no effect where it must have
  one) fails configuration; a toolchain that offers no observable signal at all
  is reported in the configure log and not counted as a pass.
  1. Exception-policy probe: when `SC_ENABLE_EXCEPTIONS=OFF`, `throw` must fail
     to compile under the effective flags. With the default ON policy this probe
     is skipped (AGENTS.md §4 revision, 2026-10).
  2. Exception-boundary probe: the same checks run against `sc::exceptions_off`
     (the `psx` / plugin-SDK contract) whatever `SC_ENABLE_EXCEPTIONS` says,
     because that contract does not depend on the project-wide policy.
  3. RTTI probe: `cmake/probes/rtti_probe.cpp` must compile under the
     `sc_nortti` flags *and* fail without them.
  4. Toolchain-specific signal, not assumed: GCC/Clang reject `throw` under
     `-fno-exceptions`, so rejection is the signal there. cl.exe does **not**
     reject it — `/EH` describes how to clean up after a throw rather than
     whether one is allowed, and `/EHs-c-` still compiles `try`/`throw`/`catch`
     (clang-cl differs: it maps `/EHs-c-` onto `-fno-exceptions` and does
     reject). The MSVC signal is therefore the compiler's own exception-model /
     RTTI macro going down (`_CPPUNWIND`, `_CPPRTTI`), which is why
     `cmake/probes/eh_macro_probe.cpp` exists and why `throw_probe.cpp`
     deliberately carries **no** macro guard: one TU containing both halves
     cannot separate "the flag works" from "this compiler never defines the
     macro", which is the distinction the whole scheme exists to make.
  5. Non-vacuity, always paired: the macro probes are compiled twice (with and
     without the flags) and `throw_probe.cpp` is compiled without flags as a
     baseline. A probe that fires in both configurations, or in neither, reports
     that it proved nothing instead of reporting success.
  6. Build with `-Werror` on the project sources (`SC_WERROR`).

  Evidence for item 4, measured here rather than quoted: with
  `clang --driver-mode=cl` (a cl-compatible driver, not cl.exe) the macro guards
  fire under `/EHsc /GR` and do not fire under `/EHs-c- /GR-`, and `_CPPRTTI` is
  predefined in msvc mode. Native `cl.exe` was not available on the development
  machine, so the Windows behaviour is *written to be correct*: if cl.exe behaves
  differently the configure log says so, and the first Windows CI run decides it
  (build-system.md §8). The same probes were sabotaged locally — substituting
  `-O1` for `-fno-exceptions` / `-fno-rtti` — and each probe failed the
  configure, which is what distinguishes these from the vacuous kind.
- Third-party: SDL3/SDL3_image/EnTT/GLM/Luau compile under either policy
  (dependencies.md §3); Qt 6 is the documented throwing dependency.

## 3. Dependency Resolution

- **Strategy:** `FetchContent` with pinned tags + SHA (no submodules, per
  requirement) for: GLM, EnTT (header-only), Luau (source build — its
  `prebuilt/VM` via its CMake), SDL3_image. This makes the
  clean-clone build self-contained given network access; CI mirrors
  (cache) for reproducibility.
- **psx / scdabas:** no dependencies at all — nothing to fetch; they build
  from their own sources only (psx-library.md §1 is a hard build rule:
  a CMake audit target fails the build if psx/scdabas link anything but
  the C++ standard library). `psx` lives at the repo root (`psx/`) and is
  prefix-free by design.
- **SDL3:** platform package manager preferred (avoids building SDL from
  source): vcpkg (Windows) with pinned port version; system/brew (Linux/macOS)
  via `find_package(SDL3)`. Fallback: FetchContent SDL3 source build (slower,
  for CI hermeticity). **Decision:** `find_package` first, FetchContent
  fallback — documented.
- **Qt6:** `find_package(Qt6 COMPONENTS Core Gui Widgets [Network])`;
  Windows via vcpkg or the Qt installer (vcpkg `qtbase`); macOS via brew
  `qt@6`; Linux via system (`libqt6*`) or vcpkg. Pin the major.minor in
  `versions.cmake` (e.g. `Qt6 6.8`).
- **Shader tools (glslang/SPIRV-Cross/DXC):** downloaded as pinned prebuilt
  release artifacts (or built from source in CI) into the build dir;
  recorded hashes in `versions.cmake`. They are *executables*, not linked
  libs — no runtime footprint.

## 4. Shader Build Integration (`ScShader.cmake`)

- Custom commands per stage (shader-migration.md §4):
  ```
  .vsh/.fsh (GLSL) --glslang--> .spv (Vulkan; -V --target-env vulkan1.2
      + explicit set/binding layouts in source)
  .spv --spirv-cross--> .metal (MSL 2.4, --msl-ios / msl-version as needed)
  .spv --spirv-cross--> .hlsl --dxc--> .dxil (SM 6.0+; verify min SM for used
      features at M4)
  ```
- Artifacts land in `${CMAKE_BINARY_DIR}/shader-cache/<hash>/` and are
  **installed** next to the binary (`<bindir>/../resources/shaders/...`);
  the runtime loads them (or from `--shaders-dir` override for dev reload).
- Diagnostics: tool stderr is captured; on failure CMake prints
  file:line:col (glslang/dxc native) and the failing command.
- Optional `SC_SHADER_VERBOSE` prints the tool invocations.

## 5. Configurations & Optimizations

- `CMAKE_BUILD_TYPE`: `Debug` (O0, asserts on, SDL debug on, GPU debug flag
  on, Luau JIT off) / `RelWithDebInfo` (O2, asserts off, GPU debug off —
  the "dev build" of Java's non-release channel) / `Release` (O3,
  LTO off by default, strips symbols; "isRelease" parity gates: free-cam
  off, FPS title off — port `Version.isRelease()` semantics to the build
  variable `SC_CHANNEL` → C++ macro `LOD_CHANNEL`).
- LTO: opt-in (`-flto`/`/GL`+`/LTCG`) behind `SC_ENABLE_LTO=ON` (measure
  first — no assumption).
- Sanitizers: `SC_ENABLE_ASAN/UBSAN/TSAN` options — dev-only and also the CI
  sanitizer lane (three of the seventeen legs, §8). They apply through
  `add_compile_options`/`add_link_options` in the root scope, so every target in
  the parent build is instrumented; a standalone `cmake -S psx` build does not see
  the options and takes raw `-DCMAKE_C_FLAGS=-fsanitize=…` instead. The flag
  `-fno-sanitize-recover=all` is added so the first violation aborts the test
  binary; leak detection is Linux-only (macOS ASan aborts on `detect_leaks=1`). TSAN to
  validate the audio-thread design (no thread code exists yet).
- Apple Silicon: default arch `arm64` (native) — no x86_64 slice required
  for v1 (documented; universal2 possible later via `CMAKE_OSX_ARCHITECTURES`).
- Reproducibility: `-gno-record-gcc-switches`-style flags where supported,
  deterministic timestamps off (`__DATE__` banned in code — version info
  comes from CMake defines: commit hash, timestamp at configure time),
  sorted link inputs where practical. (Full bit-reproducible builds are a
  *goal*, documented as best-effort in v1.)

## 6. Clean-Clone Build Procedure (per platform)

**Common:**
```
git clone <this repo> && cd SeveredChainsNative
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
# shaders compile during the build (tool auto-download per versions.cmake)
./build/bin/lodsc                   # game (the app) (needs isos/ per asset-pipeline.md)
```

**Prerequisites:**
| Platform | Prereqs |
|---|---|
| Windows | Visual Studio 2022 (MSVC v143+, C++23) or clang-cl; Ninja; vcpkg (SDL3, Qt6) or system SDL3; DirectX SDK headers (with VS); shader tools auto-downloaded |
| Linux | GCC 13+ (or Clang 16+ with libc++ for full C++23), Ninja, `libsdl3-dev` (≥3.2), `qt6-base-dev`, libvulkan (driver) for Vulkan; SDL3 GPU note: Metal not applicable; D3D12 not applicable |
| macOS (Apple Silicon) | Xcode 15+ (Clang, Metal SDK), Ninja, `brew install sdl3 qt@6` (or vcpkg); Metal for GPU; Vulkan via MoltenVK is *not* required (Metal is native) but optional if the user prefers Vulkan |

## 7. Packaging & Installation

- **Windows:** per-tool `windeployqt`-style bundling of Qt DLLs; game dir =
  `lodsc.exe` + `scengine.dll` + SDL3 DLL + resources + `mods/` +
  `isos/` + `files/` (created at first run); NSIS/MSI installer optional in
  v1 (zip-first, like the Java distribution model).
- **macOS:** `.app` bundle (game + engine framework); notarization notes
  documented at packaging time; Qt tools as separate `.app`s.
- **Linux:** tarball or distro packaging later; v1 = tarball with system
  deps documented.
- **Install layout (prefix):**
  ```
  bin/               # host + tools
  lib/               # scengine, psx, scgame, scdabas (layout follows
                     #   BUILD_SHARED_LIBS); Qt per system installation
  resources/         # shaders (compiled), fonts, UI art (non-copyrighted)
  share/             # gamecontrollerdb.txt (SDL game controller db — shipped
                     #   like the Java project), default configs
  ```
- No game data in any package (requirement: legal user data only).

## 8. CI (GitHub Actions — mirrors the reference's workflow shape)

**Implemented** (`.github/workflows/ci.yml`). Seventeen build-and-test legs plus
a static-gate job. Each leg runs the same three things a developer can run
locally, so a green run means the same gates a contributor would hit:

1. `cmake --build` with `-DSC_WERROR=ON` (warnings are errors in CI).
2. `ctest` on the in-tree suite (engine + host + `repo.naming` + `repo.format`).
3. `psx` configured as its **own project** (`cmake -S psx`) and tested there,
   which is what keeps the "extractable subproject" claim checked rather than
   aspirational.

**Legs, listed in priority order rather than alphabetically.** The development
machine is one Apple Silicon Mac, so the matrix is ordered by how much each lane
extends verification beyond what a developer can check by hand, and the
Apple-Silicon lanes come first:

| Legs | Target | Why it is there |
|---|---|---|
| 4 × `macos-15` (arm64) | Metal / Apple Silicon | Developer parity, and the priority platform: both exception policies, the differential corpus, and a sanitizer lane. `CMAKE_OSX_ARCHITECTURES=arm64` is passed so the `arm64` in the leg name constrains the build instead of describing the runner image. `macos-15` is pinned because `macos-latest` is a moving label. |
| 8 × `ubuntu-24.04` (x86_64) | Vulkan / SPIR-V | GCC and Clang, both policies, plus SDL3-from-source, the differential corpus, and a sanitizer lane (with leak detection, which macOS cannot do). |
| 3 × `ubuntu-24.04-arm` (arm64) | Vulkan on the development machine's CPU family | Native arm64 runners (not emulation): alignment, NEON-assumption and implicit-cast problems an x86_64 leg cannot surface, checked on both policies and with the differential corpus. |
| 2 × `windows-latest` (x86_64, MSVC) | D3D12 | The only lane that will ever compile the D3D12 backend, and the only one that audits a PE export table. |

Both exception policies are *built*, not just configured. With OFF, the
configure-time probes (ScToolchain §1 items 1–5) must show that `-fno-exceptions`
removes exception handling; where a toolchain cannot show that, the configure log
says so instead of the run claiming a pass.

**The sanitizer legs are the memory-safety lane** and they use the existing build
options rather than a hand-built flag string (`-DSC_ENABLE_ASAN=ON
-DSC_ENABLE_UBSAN=ON`, which is also why the options exist: raw
`-DCMAKE_C_FLAGS=…` strings break in a step that word-splits its argument list,
and a leg cannot test what it never passed). `ScToolchain` adds
`-fno-sanitize-recover=all`, so the first violation aborts the test binary, and
`UBSAN_OPTIONS`/`ASAN_OPTIONS` repeat that at runtime. Leak detection runs only on
Linux: it is unsupported on macOS, where `detect_leaks=1` makes ASan abort the
process before any test runs — measured, which is why `ASAN_OPTIONS` is
platform-conditional instead of one string for all legs. The first run of this
configuration found undefined behaviour inside a test that *passed* (out-of-range
`psx_error_t` loads, testing.md §2.11), which is the class of defect only this lane
sees.

Toolchain selection on Linux is a measurement, not a hard-coded version: Ubuntu
24.04's default compiler is GCC 13, whose libstdc++ has no `<print>`, and the
project hard-fails configure without `<expected>` + `<print>` (probe item 1), so
"use the default `cc`" would red every Linux leg for a packaging reason. The
picker probes the image's `g++-*` / `clang++-*` candidates with the same source
the configure probe uses, newest first, and requires a *paired* C compiler — the
C17 core is never built by a C++ compiler. A `clang` leg that finds no working
clang fails rather than quietly compiling with GCC, because a matrix label has to
be a test: the chosen compilers are exported as `CMAKE_C_{C,CXX}_COMPILER`, and a
leg whose label does not match the compiler actually used cannot happen.

Dependency acquisition: no leg assumes a developer machine. Linux/macOS take
SDL3 from the platform package (`libsdl3-dev` / `brew sdl3`) **only if it is
>= 3.2** — the version is probed, and an older package switches that leg to the
pinned source build (`SC_DEPS_FETCHCONTENT=ON`, SDL3 3.4.18, static). Windows
has no system SDL3, so both Windows legs use the source build; static avoids
hunting for `SDL3.dll` at test time.

The `psx.abi.symbols` audit additionally runs as a **Windows-only step that
refuses to skip**: it locates a PE export reader (`objdump` / `llvm-objdump` /
`dumpbin`), runs `psx/tests/abi_symbols.cmake` against the built `libpsx.dll`
with that tool, and fails the job if no reader exists or if the audit did not
actually read the export table. A Windows leg cannot therefore pass by not
looking. Row shapes are matched per tool flavour (GNU and LLVM objdump print
different tables and `objdump` on macOS *is* llvm-objdump), which is why the
audit is driven by output shape rather than by which tool it found.

**Not yet implemented** (no targets exist for them): clang-cl leg, macOS
x86_64 leg, SDK compile probes, oldest-plugin matrix, shader golden compile,
headless GPU smoke (needs the Milestone 2 GPU layer), doxygen, artifact
packaging, dependency caching. The Linux legs run without a display: `xvfb-run`
when the image provides it, otherwise `SDL_VIDEO_DRIVER=dummy`, so no Linux run
exercises a real windowing backend yet.

**Verification status of the workflow itself:** as of writing the repository is
still unverified as to execution. The repository has been under git since 2026-10-28 18:19 with a
remote configured, so a runner *could* execute this file — a push would trigger it — but no run has
been observed, so no claim below rests on CI having run. What
*has* been executed is the content of the four Apple-Silicon legs, run locally on
an M5 Max with the exact argument strings the workflow builds: exceptions ON,
exceptions OFF, the differential leg, and the sanitizer leg — 28/28, 28/28, 29/29
and 29/29 CTest entries respectively, each with the standalone `psx` subproject at
8/8, `arm64` binaries confirmed, and no test skipping itself into a pass (the
differential case is reported `Passed`, not `Skipped`). The macOS dependency step was additionally run
against three faked Homebrew states (current SDL3, SDL3 3.0.10, no SDL3 formula)
to confirm it picks the package, falls back to source, and survives a failed
install. Caveats: the local runs used the machine's AppleClang 21 + Homebrew SDL3
3.4.18, whereas a `macos-15` runner brings its own Xcode and brew versions, so
this is evidence about the *commands*, not a substitute for the runner; and the
two Windows legs, the eight x86_64 Linux legs and the three arm64 Linux legs remain
unexecuted. The two Linux sanitizer legs are the least predictable of them: they are
the first legs to enable leak detection, so a first run may legitimately report
leaks — inside project code, or inside SDL3, which is instrumented too when a leg
builds SDL from source — and that would be a finding to act on rather than a harness
fault. The PE export parser was separately tested against a real cross-built
`libpsx.dll` with both objdump flavours plus a deliberately-bad DLL, to confirm it
still reports stray/C++ exports instead of passing vacuously.

## 9. Risks

1. SDL3 3.2 GPU maturity on D3D12 (youngest backend) — mitigation:
   feature-query at runtime (`SDL_GPUTextureSupportsFormat`, pipeline
   creation tests per backend in CI headless mode where possible — D3D12
   headless is hard; covered by the Windows CI runner).
2. MSVC C++23 completeness (`std::expected` needs MSVC 19.34+ / VS 17.10+
   — pin the minimum VS version; `std::expected` available; `std::print`
   not used; ranges fine).
3. Shader tool cross-platform builds — prebuilt artifacts mitigate; DXC on
   Linux from MS release assets (verify availability at M4).
4. Qt via vcpkg on macOS can be slow/flaky — brew fallback documented.
