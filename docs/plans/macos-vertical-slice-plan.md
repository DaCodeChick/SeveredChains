# macOS Vertical Slice — Execution Plan (Autonomous Mode)

Started 2026-10-29. Mode: autonomous; no mid-task assistant questions. Deliverable
evidence over narrative. No push without authorization.

## Phase status

| Phase | State | Note |
|---|---|---|
| 0 Verify starting state | **done** | see findings F1–F7 |
| 1 macOS-only CI | in progress | |
| 2 Roadmap reconciliation | pending | |
| 3 Metal vertical slice | pending | |
| 4 Architectural boundaries | continuous | |
| 5 Verification | pending | |
| 6 Commits + handoff | pending | |

## Findings (measured 2026-10-29, this machine, Apple M5 Max)

- **F1 — Dependency reality.** Homebrew `sdl3` **3.4.18** is installed; its
  `libSDL3.0.dylib` links `Metal.framework` and exports 103 GPU symbols
  (`SDL_CreateGPUDevice`, `SDL_AcquireGPUSwapchainTexture`, …). `SDL_gpu.h` is in
  the SDK include path. The project's pinned minimum is 3.2, and
  `SC_SDL3_VERSION` (FetchContent fallback) is already 3.4.18.
- **F2 — The Metal GPU backend exists and works here.**
  `SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, …)` returns a device with
  `SDL_GetGPUDeviceDriver() == "metal"`. Earlier notes that "no Metal renderer is
  available" were about *the repository's own code* (correct: none existed) and
  must not be read as "the backend is unavailable".
- **F3 — MSL source is accepted directly.** `SDL_GPU_SHADERFORMAT_MSL` (1<<4) and
  `SDL_GPU_SHADERFORMAT_METALLIB` (1<<5) are both offered by the Metal backend. A
  hand-written MSL vertex+fragment pair compiled and drew on the first attempt,
  with **no shader toolchain installed** (no `metal`, no `glslang`, no
  `spirv-cross`, no `dxc`; only Command Line Tools, no `xcrun metal`). Runtime
  compilation happens inside SDL's Metal backend.
- **F4 — `SDL_VIDEO_DRIVER=dummy` has no GPU backend at all.** Measured all four
  combinations: `dummy` + no window → `No supported SDL_GPU backend found!`;
  `dummy` + window → same; `cocoa` + no window → **device OK**; `cocoa` + window →
  device OK. Consequence: the current CI's `SDL_VIDEO_DRIVER: dummy` makes any GPU
  check vacuous, and a CI GPU lane must not force the dummy driver.
- **F5 — Vulkan is not reachable here.** `SDL_GPU_SHADERFORMAT_SPIRV` reports no
  support; no MoltenVK, no vulkan-loader installed. Metal-only on this host, which
  the 2026-10-29 M2 ruling already accepts.
- **F6 — Readback works and is exact enough to assert on.** A 16×16
  `R8G8B8A8_UNORM` colour target with `SDL_GPU_TEXTUREUSAGE_COLOR_TARGET` alone can
  be downloaded with `SDL_DownloadFromGPUTexture` +
  `SDL_GPUTextureTransferInfo{pixels_per_row, rows_per_layer}`; the clear colour was
  read back correctly, which proves the harness rather than the draw.
- **F7 — Two API traps found by measurement, not by reading.**
  (a) A vertex buffer needs an explicit `SDL_BeginGPUCopyPass` →
  `SDL_UploadToGPUBuffer` → `SDL_EndGPUCopyPass` before it can be drawn; without it
  the readback shows the clear colour only, i.e. a silently empty frame.
  (b) SDL 3.4.18 has **no** `SDL_GetGPUDeviceNames`, **no**
  `SDL_GPU_FILLMODE_SOLID` (it is `SDL_GPU_FILLMODE_FILL`), **no**
  `SDL_GPU_TEXTUREUSAGE_COPY_SRC`, and **no** `SDL_GetGPUSwapchainTextureFormat`
  without a claimed window. Any code or doc written against the 1.x/2.x-style
  sketch in `rendering-sdl3-gpu.md` §1 is wrong for this SDK.

## Decisions

- **D1 — macOS-only CI** (user decision 2026-10-29). Linux x86_64/arm64 and
  Windows legs are **withdrawn, not abandoned**; documented so the next reader does
  not read "absent" as "abandoned". Reasons: no development hardware, no way to
  verify a non-Metal backend from one macOS host, no shader cross-compilation
  toolchain, and no demonstrated requirement for cross-platform rendering yet.
- **D2 — The slice is Metal-only and does not build a "multi-backend framework".**
  The API surface stays backend-neutral because it talks to SDL3 GPU (which is the
  cross-platform layer); there are no per-backend placeholder paths, no Vulkan or
  D3D12 code paths, no speculative multi-renderer abstraction.
- **D3 — Hand-written MSL, source format, no shader toolchain.** Shader text lives
  in `shaders/`, is embedded in the renderer for the slice, and is validated by
  parsing (declaration + entry point). Runtime compile is a *measured* SDL3 Metal
  capability (F3), not an assumption.
- **D4 — Two render paths, because one of them is the only CI-runnable one.**
  (a) Scene render → RGBA8 target → readback → compare against a CPU reference:
  needs a window server but no window; this is the CI-runnable proof.
  (b) Swapchain path with a real window: the runtime demonstration (manual, run on
  the M5 Max). Both are executed locally.
- **D5 — Test pattern = 16×16 sprite blocks + one triangle, with a CPU reference.**
  The reference is a real rasteriser for the tested subset (solid block fills with
  the documented rule + one triangle with a documented fill rule), so the
  comparison is a genuine oracle rather than a tautology. Edge pixels are compared
  with a documented tolerance; interior/exterior must match exactly.
- **D6 — Where code is skipped because no display/device is available, it must
  report SKIP, never PASS.** `SKIPPED (no display or no GPU device)` on the first
  line, and the CTest entry marks itself skipped (PASS_REGULAR_EXPRESSION on the
  skip marker) instead of green-by-vacuity.
- **D7 — `SDL_VIDEO_DRIVER=dummy` is no longer forced in CI** (F4). Removing it is
  what makes CI able to select Metal; keeping it would have guaranteed a vacuous
  GPU check.

## Blockers / open questions

- **B1 (open).** Whether a GitHub `macos-15` runner can create a window or a Metal
  device at all is **unverified** — it will be answered by the first CI run, not by
  reasoning. The slice is designed so a runner without a display produces a visible
  SKIP, and the local run is the executed evidence.
- **B2 (open).** Whether the project wants GPU CI to be *mandatory* (fail when no
  GPU) or *reported* (skip when unavailable) is B1-dependent; default implemented:
  reported, with a loud skip line.
- **B3 (open).** Native asset decoding (TIM/BPE → texture) has no consumer until
  this slice exists; the slice deliberately uses a synthetic pattern instead of
  pretending to consume real assets. Real-asset verification stays deferred.

## Next actions

1. Rewrite `.github/workflows/ci.yml`: macOS-only jobs (exceptions ON/OFF),
   toolchain + SDL provider reporting, in-tree ctest, psx standalone + ABI audit,
   GPU verification step, no host-specific paths.
2. Implement `lod::render` slice: `gpu_device`, `frame_targets`, `test_pattern`,
   `renderer` + `shaders/` MSL.
3. Wire the host: a `--verify-rendering` tool path + CTest entries + a manual
   window demo mode.
4. Update docs: roadmap (M1/M2/M3 truth, deferred platforms), rendering-sdl3-gpu.md
   §1 API corrections, testing.md §2.13/2.14, dependencies.md platform statements,
   HANDOFF.md.
5. Run: two configs, in-tree ctest, standalone psx ctest, ABI audit, naming/format
   gates, manual window demo, readback evidence.
6. Commit in logical order; no push.
