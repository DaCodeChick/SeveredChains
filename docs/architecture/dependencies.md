# Dependencies — Approved & Requiring Approval

Policy (requirement §2): prefer the approved stack + C++23 standard library;
extra dependencies need documented purpose/alternatives/license/portability/
maintenance and explicit approval.

## 1. Approved Stack (per project brief)

| Dependency | Version policy | License | Role | Notes |
|---|---|---|---|---|
| C++23 standard library | toolchain-provided | per-toolchain (MIT-ish) | everything | works under both exception policies (`std::expected` needs no exceptions) |
| SDL3 (incl. SDL3 GPU) | pin to current stable ≥ 3.2.0 (GPU API introduced 3.2.0) | zlib | windowing, input, gamepad, audio, GPU, platform | verified API surface: `SDL_gpu.h` (device/swapchain/pipelines/blending/shader formats/backends). GPU backends: Vulkan (Win/Linux), D3D12 (Win 10+), Metal (macOS 10.14+) |
| GLM | pin (latest 1.0.x) | MIT | math (mat4/quat/vec, GLM-GTC where needed) | header-only, no-exceptions (`GLM_FORCE_NO_CTOR` available; default fine) |
| EnTT | pin (latest 3.x) | MIT | ECS (scenes/entities/events) | header-only, no-exceptions; **kept out of plugin ABI** |
| Luau | pin exact release (lua-lang/Luau) | MIT | gameplay scripting/modding VM | C++11-compatible core; builds under both exception policies (Luau avoids exceptions; verify pinned version in CI). Prebuilt VM lib; tools: luau-analyze/luau-compile bundled for mod tooling |
| Qt 6 | pin current LTS (Core/Gui/Widgets [+Network for launcher]) | **LGPL v3** (or commercial) | launcher, settings, mod manager, debugger, asset inspector | **Licensing: dynamic linking of Qt is required for LGPL compliance** (tools link Qt dynamically — that is a license obligation, not a build preference). No static Qt. See licensing.md |
| CMake ≥ 3.28 + Ninja | any recent | BSD/GPL (build-time) | build | build-time only |

**In-repo reusable libraries (not third-party — no approval needed):**

| Library | Deps | Role | Notes |
|---|---|---|---|
| `psx` (psx-library.md) | **none** (C++23 std only) | GTE, PS1 GPU semantics + software reference rasterizer, SPU model, XA/SSSq, ISO9660, TMD/TIM/ANM/MRG/DEFF/BPE/Tag, script dis/asm tooling | zero-dep is a hard build rule; prefix-free target (independent identity); build form follows CMake `BUILD_SHARED_LIBS` (user's choice); consumed by engine, game, tools, fate CLI |
| `scdabas` (dig-dabas.md §5) | **none** (C++23 std only) | Mogul Dabas minigame (handheld console + game logic port) | host-interface driven; linked into scgame |

## 2. Requiring Approval (identified by research)

| Dependency | License | Purpose | Alternatives considered | Portability/maintenance | Verdict asked |
|---|---|---|---|---|---|
| **glslang** (Khronos) | MIT | Build-time GLSL→SPIR-V compiler for the shader pipeline (shader-migration.md §4); diagnostics with file:line | shaderc (what the Java project used via LWJGL; heavier dep tree); hand-rolled (no) | Mature (Khronos, used by Android NDK); single library; build-time only; no runtime presence | **Approve** (build-time tool) |
| **SPIRV-Cross** | MIT | Build-time SPIR-V→MSL (Metal) and SPIR-V→HLSL (intermediate for DXC) translation | hand-maintained MSL/HLSL twins per shader (maintenance trap, drift risk) | Mature (Han-Solo/ Khronos ecosystem); build-time only; small | **Approve** (build-time tool) |
| **DXC** (Microsoft DirectX Shader Compiler) | MIT | Build-time DXIL generation for D3D12 (from HLSL or directly `dxc -spirv`) | fxc (EOL, no DXIL); pre-generated DXIL committed (stale-artifact risk; Windows-only CI) | Mature (Microsoft); cross-compiles from Linux/macOS (used by game engines for exactly this); build-time only | **Approve** (build-time tool) — with fallback plan: commit DXIL artifacts generated on Windows CI if DXC-on-Linux/macOS proves flaky |
| **SDL3_image** | zlib (same license family as SDL3) | PNG decoding for bundled UI art + mod-provided images at load time (SDL3 core has no PNG decoder — BMP only) | stb_image / SDL_stb_image (public domain / zlib — separate codebase; the project standardizes on the SDL3 family, so the official companion is used instead); libpng (zlib, separate project); writing our own (no) | Official SDL3 companion library (same maintainers, versioning, and release cadence as SDL3); also covers additional formats (JPEG, etc.) if ever needed | **Approve** (owner decision: we are using SDL3, so we use SDL3_image) |
| **libopus** *(only if XA→Opus parity is kept)* | BSD | streaming CD audio in the low-latency/small-size form the Java pipeline produces at unpack | **(recommended alternative)** transcode XA→PCM/WAV at unpack (no new dep; ~2-3× file size; SDL3 audio streams it fine); or decode XA at runtime (we port XaAdpcm; resampling CPU cost) | Mature (IETF, used everywhere); but adds a runtime codec | **Decision needed from user**: keep Opus parity (approve libopus) or switch the unpack cache to PCM/WAV (no new dep). Research recommendation: **PCM/WAV for v1** (simplest, zero new deps, audio fidelity identical — it's the same PCM the Java player would mix; size is acceptable: ~100MB of CD audio at 48kHz/16bit/2ch for the full game — verify at implementation; the Java pipeline's Opus choice was for download size in their distribution model, which we don't have) |
| **libcurl or engine HTTP** *(for the launcher updater)* | MIT (libcurl) / OpenSSL | GitHub release update check (Java `Updater` parity) | SDL3 has no HTTP; a minimal HTTPS client needs TLS → libcurl is the standard answer; alternatively drop auto-update in v1 (users get updates via package manager / manual download) | Mature; native SSL backends per platform | **Decision needed**: auto-update in v1? If yes → approve libcurl (+ platform TLS). Recommendation: **yes for the launcher** (it's a flagship Java feature users rely on — the title-screen "Update Available" button) |
| **ffmpeg/libav** *(only if disc FMVs must play)* | LGPL/GPL | video decoding for original FMV cutscenes (`Fmv`/`VideoPlayer` parity; Java uses javacv/ffmpeg) | the Java game *also* ships a bundled intro MP4 (human-made) — v1 could ship the intro as a texture sequence or a bundled video via an SDL_video-like approach... SDL3 has no video decode. **Scope note:** FMVs are *disc* assets; the intro.mp4 is bundled. If FMV support is out of v1 scope (story cutscenes without video — the game is fully playable; FMVs are a few boss/transition clips), no dependency needed for v1 | Mature but heavy + GPL considerations (LGPL build possible) | **Decision needed**: FMV support in v1? Recommendation: **out of v1** (document the gap; the intro screen can use a bundled MP4 decoded by... — see below). If even the intro MP4 is desired: prefer re-authoring the intro as an animated texture sequence (no dep) or accept libav (LGPL) — user decision |
| **ANTLR4 C++ runtime** *(only if the fate compiler reuses the Java grammar mechanically — Q-SCR-1)* | LGPL-2.1+ (C++ runtime) | compile the reference's `FateLexer.g4`/`FateParser.g4` unmodified for the fate compiler (script-recompiler.md §6) | **(recommended alternative)** hand-rolled recursive-descent parser for the small, known fate grammar — no new dependency; validated by golden tests against Java-compiled outputs. Note the ANTLR C++ runtime is an **exception-using C++ library** that would be *runtime*-linked (the fate compiler runs in-engine at unpack time, not just build-time) — it would become another exception-behavior register entry in addition to Qt. This is a strong argument for the hand-rolled parser | Hand-rolled parser: ~1–2k LOC for this grammar (bounded terminal set), maintained in-repo, trivially reviewable | **Decision needed**: approve ANTLR4-C++ (LGPL, runtime-linked, exceptions-internal) or confirm the **recommended hand-rolled fate parser (no new dependency)** |

### Summary: approvals requested
1. glslang (build-time) — needed for the mandated shader pipeline.
2. SPIRV-Cross (build-time) — needed for Metal/D3D12 shader artifacts.
3. DXC (build-time) — needed for D3D12 DXIL.
4. SDL3_image (runtime, trivial) — PNG decoding (official SDL3 companion).
5. (decision) libopus vs PCM/WAV unpack cache.
6. (decision) libcurl for launcher auto-update.
7. (decision) ffmpeg/libav for FMVs (recommend: out of v1).
8. (decision, **new in Phase 0 follow-up**) fate compiler: ANTLR4-C++
   (LGPL-2.1+, runtime, exceptions-internal) vs **recommended hand-rolled
   parser (no new dependency)** — script-recompiler.md Q-SCR-1.

No new dependencies are introduced by the Phase 0 follow-up architecture
changes (PSX library, C++23 plugin API, Dabas minigame) — all of it is
in-repo code with the C++23 standard library only.

## 3. Third-Party Exception-Behavior Register (per requirement §3)

| Lib | Uses C++ exceptions? | Our handling |
|---|---|---|
| SDL3 | No (C API) | none needed |
| GLM | No (header-only; optional CTORS off by default) | none needed |
| EnTT | No | none needed |
| Luau | No (designed exceptions-free; verify pinned build flags) | none needed |
| glslang/SPIRV-Cross/DXC | C++ libs **with** exceptions internally | **build-time only** — never linked into project binaries; irrelevant to runtime |
| SDL3_image | No (C) | none needed |
| libopus | No (C) | none needed (if approved) |
| libcurl | C; no C++ exceptions in the C API | none needed (if approved) |
| ffmpeg (LGPL) | C API | none needed (if approved) |
| **Qt 6** | **Yes — Qt 6 is built with exceptions and its C++ API throws in defined situations** (invalid args, container misuse, `QException` paths) | Documented limitation + mitigations: (a) Qt linked dynamically (LGPL); (b) Qt-interop code may catch, per the permitted-but-discouraged policy, preserving the diagnostic; (c) the narrow `qtbridge` C-shim TU converts those failures into status codes at the boundary (Qt itself is an approved exception-using dependency — our flags don't change Qt's own objects); (d) `std::set_terminate` diagnostic handler in the Qt host processes; (e) the Qt-backed tools are excluded from an `SC_ENABLE_EXCEPTIONS=OFF` build (Qt throws would reach `-fno-exceptions` TUs = UB). If the shim boundary proves unstable in testing → move UI to QML (no C++ exceptions in our TUs). See qt-integration.md §2 |

## 4. Version Pinning & Supply Chain

- All versions pinned in a `versions.cmake` / toolchain manifest (single
  file): SDL3, GLM, EnTT, Luau (exact tag), Qt LTS, toolchain deps,
  shader tool binaries (glslang/SPIRV-Cross/DXC release tags).
- Checksums for fetched artifacts; no network at build time beyond the
  documented fetch step (or a local package mirror in CI).
- No Git submodules (requirement): dependencies fetched by CMake's
  `FetchContent` with pinned tag+hash (or vcpkg portfiles on Windows — see
  build-system.md).

## 5. Maintenance Burden (annual review)

- SDL3: fast-moving project — track releases quarterly; GPU API is stable
  post-3.2 but new features land regularly (e.g. compute storage, swapchain
  depth APIs).
- Luau: stable; occasional VM feature updates; we pin and bump deliberately
  (mod bytecode compatibility — luau-api.md §8).
- Qt LTS: 2-year support windows; track LTS bumps.
- glslang/SPIRV-Cross/DXC: build-time only; bump with SDL/toolchain changes.
