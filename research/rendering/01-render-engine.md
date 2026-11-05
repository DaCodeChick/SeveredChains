# M5-01 — Render Engine Architecture

Status: Complete (M5, 2026-11-05). Reference: Severed-Chains @ `0c08c679b`, read-only. This document covers the modern render engine (`legend/core/renderer/*`), its frame structure, sorting, passes, resources, and presentation geometry, and carries forward the M4 GPU-model compatibility rows (G1–G15) where the modern pipeline intersects them. Companion documents: M5-02 (TMD pipeline and GLSL geometry shaders), M5-03 (UI/text), M5-04 (post-processing, widescreen, FMV).

Evidence vocabulary: **source-verified** (established from pinned reference source), **experimentally verified**, **format-corroborated**, **asset-validated** (*none exists — no such claim is made*), **Inferred**, **Unresolved**. Layers per `research/psx/01-compatibility-boundaries.md`: (1) PSX platform, (3) LoD-specific, (4) reference-specific. The render engine is **layer 4** (modern re-hosting of the frame — R1 boundary pass §2.1); where it *reproduces* PSX behavior (H-division, translucency pass structure, TPage/CLUT decoding, VAM texture path) that reproduction is documented as a compatibility mechanism, not as original hardware. No public documentation was consulted (network-restricted).

## 1. Backends (RenderApi abstraction)

`RenderApi` (`legend/core/renderer/RenderApi.java`, 43 lines) is the backend interface: resource factories (`makeMesh`, `makeTexture`, `makeFrameBuffer`, `makeShader` with optional geometry stage, `makeUniformBuffer`) plus state operations (`clear`, `viewport`, `initBatch`, `backfaceCulling`, `enableDepthTest(DepthComparator)`, `scissor(QueuedModel, …)`, `translucency(Translucency)`, `wireframe`, `unbindFramebuffer/Texture`). Three implementations:

- **`GlApi`** (`opengl/GlApi.java`, 448 lines) — LWJGL desktop OpenGL; a debug context (GL 4.3 `GL_DEBUG_OUTPUT` or KHR_debug) with an object-label-aware debug callback that surfaces GL errors as log + `GameOverlay` notifications; `GL_LINE_SMOOTH` enabled at init; `glLineWidth(renderHeight / 480)` on resize (line thickness scales with render resolution; skipped on macOS where `glLineWidth` is removed). (Source-verified.)
- **`GlesApi`** (`opengles/GlesApi.java`, 436 lines) — LWJGL `org.lwjgl.opengles` GLES 2.0 target with the same structure and the same translucency decomposition (same blend-function choices per mode). Selected when the window falls back to a GLES context. (Source-verified.)
- **`NoopApi`** (`noop/NoopApi.java`, 153 lines) — full stub ("No-op renderer version 1.0"): all resources are no-op objects; used via `NoopWindow` for headless runs. (Source-verified.)

**Backend selection** (source-verified, `SdlWindow.java:124-131`): the `SdlWindow` constructor attempts an OpenGL window/context → `GlApi`; on failure it creates a GLES window → `GlesApi`. There is no runtime config to choose the backend; `NoopWindow`/`NoopApi` are the platform layer's headless alternative. The shader pipeline is compiled from the `gfx/shaders/*.vsh|gsh|fsh` GLSL sources via **shaderc to SPIR-V** (`ShaderManager`, `shaderc_compile_into_spv`, `shaderc_env_version_opengl_4_5`) at `RENDERER.init()`, with a dev-only reload action (`INPUT_ACTION_DEBUG_RELOAD_SHADERS`).

## 2. Frame structure

### 2.1 Boot-time callback progression (source-verified)

`GameEngine` boot: `RENDERER.setRenderCallback(GameEngine::loadGfx)` (GameEngine.java:198) → `RENDERER.init()` (:259) → intro phase `RENDERER.setRenderCallback(GameEngine::renderIntro)` (:469/473) → once the ROM context is bound, `Scus94491BpeSegment.bindRendererEvents()` (called from GameEngine.java:444) sets **`RENDERER.setRenderCallback(Scus94491BpeSegment::gameLoop)`** (Scus94491BpeSegment.java:215). So the render callback progresses loadGfx → renderIntro → gameLoop; the entire decompiled game frame then executes inside the window's onDraw (R1 execution model §1-2). `RENDERER.run()` (RenderEngine.java:1229) shows the window and drives `PLATFORM.run()` (the software-timer Action loop; swap → onDraw → clearPressed; no vsync).

### 2.2 The onDraw handler (RenderEngine.java:586-795, full read)

Phase order per due render tick (source-verified; line numbers into `RenderEngine.init`'s `onDraw` lambda):

1. **Frame-start task flush** — `addTask` deque (synchronized) — documented "run at the start of the next frame".
2. **Deferred resize** — if `resizeRenderBuffers` was flagged by `onResize`, the render FBOs/textures/depth are rebuilt (deferred so it happens on the render thread).
3. **`pre()`** (only when `frameSkipIndex == 0`) — viewport (full, or centered 4:3 when `renderMode == LEGACY` + `LEGACY_WIDESCREEN_MODE_CONFIG == FORCED_4_3`), `setProjectionMode(3D)` (updates the `transforms` UBO and `projectionInfo`), then `clear(colour + depth)` on the **default framebuffer** (no FBO is bound at this point — the color clear here is largely overwritten later by the full-window post quad; in `legacyMode != 0` it is the visible clear).
4. `EVENTS.clearStaleRefs()` (Mod-Listener bindings purge).
5. **Pause toggle** (debugger action): on resume — `resetBatches()`, delete marked `Obj`s/`Texture`s, scissor reset; on pause — one `renderFrame()` (frozen re-render).
6. **Frame advance** (debugger action, while paused): delete marked objects, scissor reset, advance `renderBufferIndex`, `resetBatches()`, `renderFrame()`.
7. **Normal frame** (when not paused):
   - `ignoreQueues` on **every pool of every batch + main batch**: `false` when `frameSkipIndex == 0`, else `true` (models queued during a skipped turbo frame are acquired from the object pools but **not enqueued** — dropped, Q-36).
   - **`renderFrame()`** (:850): reset the CLUT-animation uniform index → `renderCallback.run()` (**the entire game frame — `gameLoop` phases 0-20** — executes here) → upload the CLUT-animation uniform (terminator `−1` appended).
   - `GameOverlay.drawNotifications()` / `drawFps()` (reference HUD).
8. **Main render pass** (`legacyMode == 0` only, the release default; Tab-cycles 0/1/2 in dev builds, :771-775):
   - **Battle TMD pre-bind** (:665-669, source comment "Gross hack bro" — R1 audit F-18): if the current state is a `Battle` with `battle._800c6930 != null`, `battleTmdShader.use()` + set the `battleColour` uniform **before** the main pass, so subsequent `battle_tmd` model draws inherit the current battle-tint state.
   - Bind `renderBuffers[renderBufferIndex]` (the frame's color FBO); when `frameSkipIndex == 0`: `clear(colour)` (depth is cleared **per batch** by `renderBatch`), then `renderBatch` for each extra batch in insertion order, then the main batch.
   - After the batches (:686-791): wireframe off, depth test off, translucency off (restores depth mask, §5.3), **unbind FBO**, clear window depth (:692-693), bind the screen (post) shader and configure all CRT/post uniforms from config (:696-780 — full option set: enableCrt, time, scanlines, grille, pixelate + resolution, roll (speed/size/variation/intensity), noise (opacity/speed), static, aberration, brightness, discolour, edge warp, vignette (intensity/opacity), bloom (intensity/threshold/radius)), compute the **turn-order UI bounds** uniform (a `TurnOrderMod`, when loaded and visible in an active battle, reports its current HUD width/height in native 320×240 coordinates; converted to normalized screen bounds with the `widescreenOrthoOffsetX` compensation, :752-780), set the viewport to the window (:782), then draw the full-window post quad sampling `renderTextures[renderBufferIndex]` (the buffer just drawn into, :783-784) at window resolution, and unbind the texture (:788, in-source comment: "If we don't unbind the framebuffer textures, window resizing will crash since it has to resize the framebuffer").
9. **Post-frame bookkeeping** (when not paused): when `frameSkipIndex == 0` — `resetBatches()`, delete marked objects/textures, scissor reset; then advance `frameSkipIndex` (mod speed multiplier) and advance `renderBufferIndex` **once per `speedMultiplier` frames** when the frame-skip option is on (else every frame); `vsyncCount += 60 × multiplier / fpsLimit` (the virtual 60 Hz counter used by the CRT shader time and the `simpleRand` re-seed — M4-05 §2.3); rolling FPS (window title in dev builds).
10. Shader reload (dev) and **`handleMovement()`** (freecam — the debug camera writes into `worldToScreenMatrix_800c3548`, a **game-visible ROM global**, so freecam moves the actual game camera; reference-only, dev builds).

### 2.3 Frame-skip / turbo (Q-36 cross-reference)

Mechanism (source-verified): at speed multiplier *m*, `frameSkipIndex` cycles 0..m−1; only index 0 runs `pre()`, the FBO clear, the batch renders, `resetBatches()`, object deletion, and scissor reset; `ignoreQueues` discards queued models of skipped frames; `renderBufferIndex` (and hence which buffer the post pass re-presents) advances only every *m* frames; the post pass and `vsyncCount` run **every** presented frame. *Intent* (logic every frame, one rendered frame per *m*, stale-but-queued rendering on skipped frames) remains `Inferred` (register Q-36, unchanged by this document).

### 2.4 Presentation latency

The window presents the backbuffer at the **start** of each tick (`SDL_GL_SwapWindow` before `onDraw`, R1 §1); the post quad fills the backbuffer during onDraw; so the frame the user sees at time *t* was composed during onDraw of iteration *t−1* — one frame of presentation latency, plus the GPU model's own one-frame display-buffer model (M4-01 §4.4) for legacy/VRAM modes. (Source-verified ordering; consequence `Inferred`.)

## 3. Framebuffers and presentation targets

- **Double render FBOs** (`RenderEngine.java:255-267`, `RENDER_BUFFER_COUNT = 2`): `renderBuffers[2]`, each a `FrameBuffer` with a color attachment (`renderTextures[i]`, `RGBA_8` at `renderWidth × renderHeight`) and a **shared** `depthTexture` (`DEPTH_COMPONENT`, float data). `renderBufferIndex` flips per presented frame (or per *m* frames under frame-skip). FBOs are recreated on resize (deferred to the render thread, §2.2.2).
- **Render resolution** (`onResize`, :1255-1282, source-verified): from `RESOLUTION_CONFIG` — `NATIVE` → `renderWidth = windowWidth × nativeWidth/320`, `renderHeight = windowHeight`; fixed presets (`Resolution`: ORIGINAL 240, 480, 720, 1080, 1440, 4K 2160, 8K 4320) → `renderHeight = preset`, width from aspect. **Then `renderHeight` is forced to the next multiple of 240** ("to fix lines in the UI" — in-source comment) and width recomputed from the aspect. This multiple-of-240 rule is a load-bearing reference detail for any UI-line geometry (M5-03) and for the post shader's 240-based CRT math.
- **The three presentation modes** (`legacyMode`, static, dev-cycled; R1 §7, execution model):
  - **0 — OpenGL rendering (release default):** §2.2.8 — model pools + post chain into the FBOs. The PSX GPU model's ordering-table commands **still tick every frame** inside `gameLoop` phase 19 (`GPU.endFrame()`, M4-01 §4.4) and are rendered from the z-queues into the GPU model's own draw buffers — but in mode 0 the GPU model's draw buffers are **not the presentation path**; its visible effects in mode 0 are the objects whose game code explicitly routes to it (per-object `legacyMode` branches — Q-37; the `Battle.applyTextureAnimation` VRAM copies, M4-01 §7.2, execute inside the GPU model regardless of mode and are visible in mode 0 through the VRAM textures that mode-0 models sample, §9).
  - **1 — legacy rendering:** the whole §2.2.8 block is skipped; presentation is the GPU model's software-rasterized display buffer uploaded to a texture and drawn as a quad by `Gpu.tick` (M4-01 §4.4/§3.3). Model-pool content is **never drawn** in modes 1/2 (the batch-render block is mode-0-only); game code branches on `legacyMode` per object to route to the GPU command stream instead (Q-37 branch sites: `tmd/Renderer.java:46`, `DeffTmdRenderer14.java:85`, `SEffe.java:325-336`, `TmdParticle.java:116`, `Graphics.java:145`).
  - **2 — VRAM rendering:** `Gpu.drawVram()` presents the full 1024×512 VRAM texture (M4-01 §3.3; this is the only consumer of `vramTexture24`, Gpu.java:448-449).
- **`getLastFrame()`** returns `renderTextures[floorMod(renderBufferIndex − 1, 2)]` — the previous frame's color buffer (used by reference-side consumers, e.g. screen-capture effects; M4-02 §1.2 census).

## 4. Batches and model pools

- **Batches** (`RenderBatch`): a main batch plus any number of extra batches (`RENDERER.addBatch()`). Extra batches **copy the main batch's configuration at creation** (native size, render mode, projections, widescreen offset) and are rendered in insertion order; **the depth buffer is cleared between batches, so each batch renders on top of the previous one** (in-source javadoc, source-verified). Each batch owns: `modelPool` (perspective/3D) and `orthoPool` (2D), both `QueuePool<QueuedModel<?,?>>`.
- **`QueuePool`** (`legend/core/QueuePool.java`): a per-type **object pool** (`addType(cls, supplier)` — every batch's pool registers exactly three model types: `QueuedModelStandard`, `QueuedModelTmd`, `QueuedModelBattleTmd`, RenderBatch.java:66-69) plus the frame's queue list. `acquire(cls)` reuses a pooled entry (or constructs one) and enqueues it **unless `ignoreQueues`** (turbo, §2.3). `reset()` clears the queue and returns all entries to the type pools (per-frame, §2.2.9). Sorting uses an allocation-free merge-insert sort into a pre-grown work array (`Sort.sort`), re-writing the queue in place.
- **`QueuedModel`** — per-model render record (acquire-time defaults source-verified, :165-180): `transforms` (mat4), `modelView` (cached at sort, §6.1), `screenspaceOffset` (vec3; `depthOffset(z)` sets the z), `colour` (vec3, default white), `clutOverride`/`tpageOverride`/`uvOffset` (vec2), **`worldScissor` = `scissorStack.top()` captured at acquire time**, `modelScissor` (rect, top-left origin, 0×0 = none), `startVertex`/`vertexCount` (a vertex range within the Obj's mesh — allows one mesh to be drawn in parts), `textures[32]` (per texture unit; none → the VRAM 15-bit texture, §9.3), depth comparators (**opaque = `LESS`, translucent = `LESS_THAN_OR_EQUAL`** defaults). `sequence` = the batch's monotonic queue counter (reset per frame).
- **Lifetime:** `Obj`/`Texture` keep static registries; `delete()` only marks; `deleteObjects()`/`deleteTextures()` (per frame, §2.2.9) perform the real deletion; `clearObjList(clearPersistent)` (at shutdown via `RENDERER.delete()`) force-deletes and **logs every leaked non-persistent object** (warn + dev-build overlay notification). `persistent` objects (the built-in quads: `plainQuads` per translucency mode, `opaqueQuad`, `centredQuadOpaque/BPlusF/BMinusF`, `lineBox`/`lineBoxBPlusF`, `renderBufferQuad`, the post quad) survive state transitions and are only cleared with `clearPersistent = true`.

## 5. Passes and PSX translucency decomposition

### 5.1 Pass order within a batch (`renderBatch`, :862-887)

1. If `needsSorting` (any `queueModel` this frame): sort both pools (§6), clear the flag.
2. `api.initBatch(batch)` (sets the batch's widescreen/scaling state, disables culling) → **clear depth**.
3. **3D opaque pass:** `setProjectionMode(3D)` (culling on), `renderPool(modelPool, backFaceCulling = true)`.
4. **2D opaque pass:** `setProjectionMode(2D)` (culling off), `renderPool(orthoPool, false)`.
5. **3D translucent pass:** `setProjectionMode(3D)`, `renderPoolTranslucent(modelPool)`.
6. **2D translucent pass:** `setProjectionMode(2D)`, `renderPoolTranslucent(orthoPool)`.

### 5.2 The opaque pass's two render roles (`renderPool`, :889-940)

Per model (in sorted order), in 128-model chunks: the chunk's model transforms (mat4 + screenspaceOffset, 20 floats each) and lighting (32 floats each when used) are uploaded to the shared `transforms2`/`lighting` uniform buffers; `useShader(modelIndex, 1)` (discard mode 1); `enableDepthTest(opaqueDepthComparator)`; scissor applied (§7); then **per layer** (layer = mesh index, `MeshObj`):
- **Role A — opaque pixels:** `shouldRender(null, layer)` (mesh is opaque) → `render(null, layer)` with per-obj backface culling.
- **Role B — first pass of translucent primitives:** for each of the four `Translucency` modes, `shouldRender(mode, layer)` (mesh is a translucent mesh of that mode) → `render(mode, layer)` with culling off. The in-source comment: "renders opaque pixels with translucency bit not set for translucent primitives (note: does not apply to 24bpp textures)".

The pixel-level split happens in the fragment shader (§5.4): role A draws the **non-translucent** pixels of translucent primitives; the translucent pixels are deferred to the translucent pass. This is the modern-pipeline implementation of the PSX's per-pixel translucent-bit two-pass behavior (M4-01 G12 carry-over, §10).

### 5.3 The translucent pass (`renderPoolTranslucent`, :942-998)

Culling off; per model with `hasTranslucency()`: `useShader(modelIndex, 2)` (discard mode 2), `enableDepthTest(translucentDepthComparator)`, scissor, then per layer the four modes in fixed order:

| Mode rendered | GL blend state set (`GlApi.translucency`) |
|---|---|
| `HALF_B_PLUS_HALF_F` | `FUNC_ADD(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` — the fragment sets `alpha = 0.5` (standard.fsh:117-120; `translucencyMode == 1` is the ordinal+1 encoding of `HALF_B_PLUS_HALF_F`) so the result is (B+F)/2 |
| `B_PLUS_F` | `FUNC_ADD(ONE, ONE)` |
| `B_MINUS_F` | `FUNC_REVERSE_SUBTRACT(ONE, ONE)` (= B − F) |
| `B_PLUS_QUARTER_F` | **`FUNC_ADD(ONE, ONE)` — the `B_PLUS_F` state** (RenderEngine.java:986-989) |

**Resolution of R1 audit F-13 (register Q-37):** the `B_PLUS_F` blend state for the `B_PLUS_QUARTER_F` pass is **not an alias/bug** — it is a decomposition. `QueuedModel.updateColours` (QueuedModel.java:235-240) multiplies the model colour by **0.25** for the `B_PLUS_QUARTER_F` mode, so the pass computes B + 0.25·F with a B+F blend. Corroborating: `GlApi.translucency` has **no case** for `B_PLUS_QUARTER_F` — it falls through to `default -> throw RuntimeException(translucency + " not yet supported")` (GlApi.java:418, method :388-420) — the renderer never calls it; the shader-side colour scaling is the only path. Difference vs the PSX GPU model (M4-01 §5.4, G10): the GPU model truncates F/4 to byte precision *before* adding (B + (F>>2)); the modern path computes B + 0.25·F in float and rounds once — a per-channel difference of at most ~1/255 (Inferred bound, both equations source-verified). The same decomposition pattern applies to `B_MINUS_F` + `useTextureAlpha` (fragment premultiplies F by alpha, standard.fsh:112-116, `translucencyMode` 2/3 = B_PLUS_F/B_MINUS_F — the `translucency` shader uniform is **ordinal + 1** with 0 = opaque, `ShaderOptionsStandard.java:21`).
- **Depth mask:** entering the first translucent draw sets `glDepthMask(false)`; returning to opaque (translucency null) sets it true again (GlApi.java:389-397, in-source comments: "Do not update the depth mask so that we don't prevent things further away than this from rendering" / "Update the depth mask so nothing further away than this will render") — the modern analogue of the PSX "translucent pixels don't set the depth buffer" behavior.

### 5.4 The fragment-shader pass masks (standard.fsh, source-verified)

- **Scissoring in the shader** (:44-47): the model's scissor rect (converted to render coordinates, §7) is enforced by `discard` in the fragment stage. In-source comment (source-verified): *"Older Intel iGPUs are buggy and don't implement scissoring properly, causing the Shirley fight to lock up when she transforms into another character. This is a workaround and reimplements scissoring at the shader level."* — a documented reference workaround with a specific gameplay incident (the Shirley boss fight). No GL scissor rectangle is ever enabled by the render engine.
- **Linearized depth for modern perspective** (:50-54): mode 2 writes `gl_FragDepth = (depth − znear) × zdiffInv + depthOffset` so 2D/ortho models can be placed at arbitrary depths within a perspective frame (`depthOffset` = `screenspace.z × zdiffInv`); modes 0/1 use the raw `gl_FragCoord.z` (in H-division mode the clip z *is* the projection-plane z, so 3D and 2D share one coherent depth order — the "usePs1Depth" family of behavior, M5-02).
- **Translucent-pixel deferral** (standard.fsh:95-103): textured — mode 1 discards translucent pixels (alpha≠0) of translucent primitives and keeps the rest; mode 2 is the complement (the single-condition form at :95-96); zero-alpha (and zero-colour when not in texture-alpha mode) pixels are discarded (transparent texel, the shader-side counterpart of the GPU model's "index 0 = no pixel" rule, M4-01 §5.2; :89-91). Untextured — mode 1 discards all translucent pixels (they have no per-pixel bit and wait for the translucent pass), mode 2 discards all opaque (source-verified comment at :101).

## 6. Sorting and ordering

### 6.1 Perspective (3D) pool (`sortPerspectivePool`, :1163-1170)

- Before sorting, each model's `modelView = camera3d.view × model.transforms` is cached (the sort key needs the post-camera z).
- **Predicate** (`perspectiveTranslucencySorter`, :1148): depth = `modelView.m32 + screenspaceOffset.z`, **descending** (farthest first — back-to-front, the PSX OT direction, `Inferred` layer-1 corroboration), tie-broken by **`sequence` ascending — insertion order (FIFO)** for equal depth.
- Note the sort is over the *same* pool used for both the opaque and translucent passes; the translucent pass re-uses the opaque-pass ordering (no second sort — `needsSorting` is cleared after the first).

### 6.2 Ortho (2D) pool (`sortOrthoPool` + `orthoTranslucencySorter`, :1150-1158)

- Depth = `transforms.m32 + screenspaceOffset.z`, **descending**; equal depth → **`Integer.compare(b.sequence, a.sequence)` — reverse insertion order (LIFO)**.
- **The two pools have opposite equal-depth tie-breaks** (3D FIFO, 2D LIFO) — source-verified, intent `Unresolved` (see register Q-71, §13). Consequence: for 2D content at the same depth (the common case — most UI/text/HUD models queue at z = 0 or a fixed offset), **the last-queued model draws first (is underneath)** in the modern path, mirroring the GPU model's same-z LIFO traversal (M4-01 §4.2, Q-66) but for the modern 2D pool specifically.

### 6.3 Depth-buffer behavior

- One shared depth texture per FBO pair; cleared per batch (§5.1.2) and before the post pass (window depth).
- Per-model comparators: opaque `LESS` / translucent `LESS_THAN_OR_EQUAL` defaults (overridable via `opaqueDepthComparator`/`translucentDepthComparator`); the depth *function* is state-cached by the API (set only on change).
- Translucent passes don't write depth (§5.3); opaque passes do — so a translucent primitive at depth *d* can be occluded by an opaque one at the same *d* only via the `LESS`/`LEQUAL` interplay (opaque drawn first with `LESS`, translucent with `LEQUAL` — equal-depth opaque wins). (Source-verified mechanism; ordering consequence `Inferred`.)

### 6.4 Relationship to the PSX ordering table (Q-66 carry-over)

The modern path has **no ordering table**: back-to-front is achieved by sorting on model-view z once per frame, and within-primitive ordering by the depth buffer. The GPU model's 0x4000-slot OT still runs every frame in all modes (M4-01 §4.4) and remains the execution venue for per-pixel GPU commands (the `Battle.applyTextureAnimation` VRAM copies, §11). The two ordering systems coexist and are **not** cross-ordered: a model-pool primitive and a GPU-command primitive at "the same z" have no defined interleaving (per-object routing is the game code's choice — Q-37).

## 7. Scissor stack and clipping

- **`ScissorStack`** (82 lines): a 100-deep stack of `Rect4i`; `reset()` (per frame, §2.2.9) sets level 0 to the full render resolution; `push()`/`pop()`; `set(x,y,w,h)` intersects an absolute-coord rect into the current top (`subregion`); **`setRescale(x,y,w,h)`** converts **native (320×240) coordinates** to render coordinates — y-flip (`renderHeight − (y+h)`), scale by render/native multipliers, plus widescreen compensation (PERSPECTIVE+ALLOW_WIDESCREEN or LEGACY+EXPANDED: x offset by `widescreenOrthoOffsetX` and width scaled by `expectedWidth/nativeWidth`, i.e. the 368/320 factor for expanded submaps, :52-67).
- **Per-model capture:** `QueuedModel.acquire` snapshots `worldScissor = scissorStack.top()` (the region in force at queue time); `modelScissor` (set via `QueuedModel.scissor`, top-left origin) further clips the model. At draw time `GlApi.scissor` intersects both, applies the same native→render/widescreen conversion, and writes the rect to the **`scissor` uniform** (a UBO, not GL state) — consumed by the fragment-shader `discard` (§5.4). The uniform upload is change-cached (`activeScissorRect` comparison). (All source-verified.)
- **Compatibility note:** the PSX GPU has no per-primitive scissor; clipping was the draw-attribute rectangle (M4-01 G-row). The scissor *stack* is a layer-4 facility for 2D content (UI, text, boxes — M5-03); the draw-area semantics themselves remain on the GPU model for mode 1/2 and for the mode-0 GPU-command subset.

## 8. Cameras and projections

- **Cameras** (source-verified): `camera2d` = `BasicCamera` (a pure translation view — 2D content is drawn in screen coordinates with the camera at the origin); `camera3d` = `QuaternionCamera` (yaw/pitch/position, strafe/jump/look; the freecam writes it — §2.2.10). The view matrix + the batch's projection matrix are packed into the `transforms` UBO (2 × mat4) by `setTransforms` on every `setProjectionMode` call.
- **Projection matrices per render mode** (`RenderBatch.updateProjections`, :114-150, source-verified):
  - `legacyMode != 0`: plain `setPerspectiveLH(π/4, renderAspect, 0.1, 500)` + `setOrtho2D(0, renderW, renderH, 0)` — the GPU model's display buffer is presented without the game's projection structure.
  - **PERSPECTIVE mode** (all states except SUBMAP/TITLE — platform/04 §8, Q-38): (matrix at :138-141) *"LOD uses a left-handed projection with a negated Y axis because reasons"* (in-source comment) — `setPerspectiveLH(fieldOfView, ratio, 0.1, 1e6).negateY()`. The ortho projection: 320×240 at `widescreenOrthoOffsetX = 0`, or (ALLOW_WIDESCREEN) width = `nativeHeight × renderAspect` (scaled by `nativeWidth/expectedWidth`), `widescreenOrthoOffsetX = (w − expectedWidth)/2` — the modern widescreen expansion (M5-04 §widescreen). `fieldOfView = atan((nativeWidth/2)/projectionDepth) × 2 / aspectRatio` with `aspectRatio = 320/nativeHeight` (**the aspect is pinned to 320/height, not nativeWidth/height** — source-verified; the 368 wide contexts keep the 320-derived FOV).
  - **LEGACY mode** (SUBMAP/TITLE): the "perspective" projection is actually a **centered ortho** (`setOrthoLH(−w/2, w/2, h/2, −h/2, 0, 1e6)`, :147-150) and perspective is emulated by **projection-plane division in the vertex shader** (standard.vsh:137-141: `gl_Position.xy *= zfar / z`, `zfar` = the GTE projection plane distance — the PSX H-division pseudo-perspective, M4 boundary pass §2.1; z clamped to [0, 65536] in the shader at :139, matching the GTE model's z clamp, M4-02 §2.1). EXPANDED widescreen (M5-04): `expectedWidth = 368`, `w = Math.round(h × renderAspect) + 1 & ~1` (forced even), `widescreenOrthoOffsetX = (w − nativeWidth)/2` (RenderBatch.java:143-150) — the renderer-side counterpart of the GPU model's 384→368 clamp (two distinct 368 mechanisms, §10 G4).
- **`projectionInfo` UBO** (vec4, `setProjectionMode`, :1100-1130): `[znear = 0, zfar = GTE projection plane, zdiff (1e−6 for high-quality perspective, 1/plane for PS1), mode (0 = ortho, 1 = PS1 perspective, 2 = high-quality perspective)]`. The GTE's projection plane distance feeds the renderer via `Graphics.setProjectionPlaneDistance` → `RENDERER.setProjectionDepth` (Graphics.java:909; default 320 in `GsInitGraph`, M4-02 §1.2/T6).
- **Screen offsets:** `screenspaceOffset.xy` is added to clip coordinates **before** the projection matrix (i.e. in post-projection pixel space) — the modern analogue of the GTE OFX/OFY and the GPU model's center-origin draw offset (M4-01 G3). `depthOffset` (screenspaceOffset.z) is added to z in PS1 mode or linearized into `gl_FragDepth` in mode 2.

## 9. Rendering resources

### 9.1 Uniform buffer layout (source-verified)

| UBO | Content | Stride |
|---|---|---|
| `transforms` | camera view mat4 + projection mat4 | 32 floats |
| `transforms2` | per-model: model mat4 + screenspaceOffset vec4 | 20 floats × 128 models |
| `lighting` | per-model: lightDirection mat4 + lightColour 3×vec4 + backgroundColour vec4 | 32 floats × 128 models (written only for lit models) |
| `projectionInfo` | znear/zfar/zdiff/mode | 4 floats |
| `scissor` | rect (render coords) | 4 floats |
| `clutAnimation` | CLUT-animation remap table | vec4 × 1024 (terminator −1; per-frame index, `renderFrame` :850-859) |

Models index their data by `modelIndex = i & 0x7f` (the position within the 128-chunk); chunks beyond 128 models re-upload the UBO every 128 (source-verified, :921-927). The 128-model chunk size is a hard constant (buffer capacities `(4×4+4)×128` / `(4×4+3×4+4)×128`).

### 9.2 Vertex attribute layout (source-verified, `TmdObjLoader` constants + `standard.vsh`)

16 floats per vertex: `inPos` vec4 (w = vertex index), `inNorm` vec3, `inUv` vec2, `inTpage` float (the **PSX TPage word**), `inClut` float (the **PSX CLUT word**), `inColour` vec4, `inFlags` float (bits: `LIT 0x1`, `TEXTURED 0x2`, `COLOURED 0x4`, `TRANSLUCENT 0x8`; TmdObjLoader.java:37-40). The same layout serves the TMD model pipeline (M5-02) and all built-in builders (QuadBuilder/PolyBuilder/LineBuilder/McqBuilder import the `TmdObjLoader.*_SIZE/*_FLAG` constants — a single shared vertex ABI). **TPage/CLUT decoding happens in the vertex shader** (standard.vsh:96-111: `bpp = intTpage >> 7 & 0x3`, base = `((intTpage & 0xf) × 64, (intTpage & 0x10) ? 256 : 0)`, clut = `((intClut & 0x3f) × 16, intClut >> 6)`) — the exact field arithmetic of the CPU-side `Graphics.GetTPage/GetClut` decode (M4-01 §2.2, G2), with overrides (`tpageOverride`/`clutOverride` non-zero) replacing the decoded values.

### 9.3 The VRAM texture path in mode 0 (G1 carry-over)

- `QueuedModel.useTexture()`: if the model bound explicit textures (units 0-31), use them; **otherwise `GPU.useVramTexture()` binds `vramTexture15` to texture unit 1** (Gpu.java:137-139). The standard shader's `tex15` (unit 1) is the **VRAM 15-bit store as a 1024×512 R32 texture** (M4-01 §1.1) — 4/8-bit texels are fetched from it with the exact PSX field arithmetic (`widthMultiplier/widthMask/indexShift/indexMask` computed from `bpp` in the vertex shader, standard.vsh:125-131; the CLUT pixel is fetched from the *same* tex15 store at `(clutX + p, clutY)`, standard.fsh:60-74), 15-bit pixels are direct, and 24-bit TPages sample `tex24` (unit 0) — which `useVramTexture()` does **not** bind (only mode-2 `drawVram` uses `vramTexture24`, Gpu.java:448-449); 24bpp mode-0 models must therefore bind their own texture to unit 0 (a reference gap/corner, `Inferred` low exposure — LoD TMDs are 4/8/15bpp).
- The CLUT-animation remap (1024-entry linear scan per textured vertex, standard.vsh:113-122; entries = (originalX, originalY, replacementX, replacementY), terminator x = −1, registered per frame via `RENDERER.addClutAnimation`) lets game code animate CLUT origins without VRAM re-uploads (M4-01 §6.3 mechanism).
- **Consequence (carrying M4-01 §1.2 into M5):** the VRAM arrays remain a live data path in mode 0 — models without dedicated textures sample the same 15-bit store the GPU model rasterizes into, so GPU-command writes (e.g. the battle scroll copies) are visible to mode-0 models through this texture (the mechanism by which `Battle.applyTextureAnimation` is observable in the release default mode, §11).

## 10. M4 compatibility obligations (G1–G15 carry-over)

Addressed here where the modern pipeline intersects the GPU-model rows (full row text: `research/psx/05-compatibility-classification.md` §3.1):

| Row | M5-01 disposition |
|---|---|
| G1 (VRAM 1024×512, dual stores) | **Addressed** — mode 0 samples `vramTexture15` (unit 1) with the same 4/8/15/24-bit semantics (§9.3); the 24-bit store's only mode-0 role is per-model explicit binding. |
| G2 (Bpp table, TPage/CLUT encoding) | **Addressed** — decode replicated exactly in the vertex shader (same field arithmetic, §9.2); overrides replace the decoded values. |
| G3 (center-origin offset, double buffer, PSDCNT) | **Addressed (mechanism)** — the modern analogue is `screenspaceOffset.xy` applied post-projection (§8); the GPU model's double buffer/PSDCNT remain as-is (M4-01). |
| G4 (384→368 clamp) | **Cross-referenced + distinction** — two separate 368s: the GPU model's width *clamp* (M4-01 §3.2, G4 Unresolved vs original NTSC width) and the renderer's LEGACY-EXPANDED widescreen *expectedWidth = 368* projection (this document §8, M5-04). They interact only through `nativeWidth` (set to 384 by the game for wide contexts, then clamped to 368 by the GPU model for the GPU path; the renderer's 368 is a projection choice). G4 remains Unresolved. |
| G5 (ordering-table constants, back-to-front) | **Cross-referenced** — the OT still runs in all modes (M4-01 §4.4); the modern path's back-to-front is the sort (§6.1), directionally the same (`Inferred` corroboration). |
| G6 (same-z LIFO vs PSX FIFO) | **Addressed (new instance)** — the GPU model keeps its LIFO traversal (M4-01 §4.2); the modern **2D pool tie-breaks equal depth LIFO** and the 3D pool FIFO (§6.2 — register Q-71). Q-66 remains Unresolved for the GPU model; the modern-path tie-breaks are reference behavior (layer 4) with no PSX counterpart. |
| G7 (z insertion, /4 scale, zMax clamp) | **Addressed (modern counterpart)** — mode 0 has no OT insertion: depth is the sorted model-view z + the depth buffer; the /4-scale question (Q-67b) applies only to the GPU-model TMD path (M4-02 §3.4). |
| G8 (rasterizer, top-left rule, barycentric) | **Cross-referenced** — mode 0 uses the GL rasterizer (no top-left rule — modern fill convention); the GPU-model rasterizer (M4-01 §5.2) remains for modes 1/2 and the mode-0 GPU-command subset. Pixel-exactness vs PSX still unverified (no baseline). |
| G9 (texturing, T×C/128) | **Addressed** — `outColour = clamp(vertColour × texColour, 0, 1)` (standard.fsh:98) — the same texture×color multiply in float (the GPU model does the 8.7 `>>> 7` integer form, M4-01 §5.4). |
| G10 (blend equations) | **Addressed** — all four modes reproduced (§5.3); B+F/4 decomposed as B+F blend + 0.25 colour scale (F-13 resolved, §5.3); per-channel rounding difference vs the GPU model's byte-truncated F/4 documented (~1/255). |
| G11 (TPage rate 00 → B+F/2) | **Addressed (modern path)** — the mode-0 shader **never reads the TPage rate bits** (bits 6:5): the translucent mode comes from the mesh's `translucencyMode` (TMD path: the `tmdTranslucency` value from the TMD header, M5-02) or the per-model override. So G11's rate-00 question applies only to the GPU-model TMD path (M4-01 §5.4) — remains Unresolved there. |
| G12 (mask bit, translucent gating) | **Addressed** — depth-mask on/off across passes (§5.3) + the per-pixel translucent-bit deferral via discard modes (§5.4) reproduce the PSX two-pass rule in the modern pipeline. |
| G13 (one-frame display latency) | **Addressed** — presentation ordering (§2.4): swap-before-onDraw adds one frame on top of the GPU model's own display-buffer model. |
| G14 (upload paths) | **Cross-referenced** — `uploadData15`/dual-store writes (M4-01 §6) are the producer side of the mode-0 VRAM texture (§9.3); unchanged. |
| G15 (VRAM copy, no overlap protection) | **Cross-referenced** — GPU-model path only (M4-01 §5.3); no modern-path counterpart. |

## 11. Worked trace — `Battle.applyTextureAnimation` and why same-z order is observable

(M4-01 §7.2 established the queue; this section positions it within the full frame, per the M5-01 obligation.) The battle texture-scroll animation queues three z=1 `GpuCommandCopyVramToVram` per frame (backing store at (960,256)): (1) bottom `h` rows of the rect → into the store region; (2) the middle shifts up by `h`; (3) top `h` rows of the rect → into the store. Execution order is the GPU model's **LIFO** traversal (M4-01 §4.2): **copy 3 → copy 2 → copy 1**, i.e. the rect's top rows are saved to the backing store *before* the middle shift overwrites the area, and the stored bottom rows are restored *after* — a correct circular scroll. Under a FIFO execution the first pass would read the backing store before copy 3 refreshes it (stale frame after the first) — so the animation's correctness **depends on the LIFO order** (M4-01 §7.2, Q-66). In the release default (mode 0) this is still visible: the copies execute inside `GPU.endFrame()` (gameLoop phase 19, **inside** `renderCallback.run()` — i.e. *before* the mode-0 batch render of the same frame, §2.2.7-8), and their results land in the VRAM stores that mode-0 models sample through `vramTexture15` (§9.3). Whether the *original* PSX (FIFO within slot) produced the same scroll with this queue order, or a different one, remains Unresolved (Q-66 — not re-adjudicated here).

## 12. Verification criteria cross-check (M5 plan §M5-01)

- Render-order diagram: §13 (every pool/batch/stage with inputs, sorting, state, target, lifetime, evidence).
- Baseline rendering resolution vs presentation resolution: baseline = `nativeWidth × nativeHeight` = 320×240 (or 384→368 contexts, G4), set by the ROM's `resizeDisplay` (M4-01 §3.2); render resolution = `RESOLUTION_CONFIG`-scaled (ORIGINAL = window-proportional at 240 native height), forced to a multiple of 240 (§3); presentation = window size, the post quad mapping render→window at 1:1 aspect (§2.2.8). The multiple-of-240 rule and the 320-pinned FOV aspect are the two load-bearing coupling points (source-verified).
- Q-36 (turbo): mechanism re-verified; intent unchanged.
- Q-37 (render-path faithfulness): the mode-0 per-object coexistence is documented at the mechanism level (§3 modes, §6.4); the F-13 translucency-pass question is **resolved** (§5.3); the F-18 battle-colour hack is documented as state-leak-by-design (§2.2.8); the per-object census (which objects use which path in mode 0) is **M5-02/M9 territory** (the TMD/battle-object audit) and is not claimed complete here — Q-37 stays open with this narrower scope.

## 13. Render-order diagram (source-annotated)

Per presented frame (mode 0, release default; all steps source-verified, line refs into `RenderEngine.java` unless noted):

```
SdlWindow.tick (R1 §1):  SDL_GL_SwapWindow (present previous frame's backbuffer)
onDraw (RenderEngine.init, :586):
 1  task flush (:588-592)
 2  deferred FBO resize (:594-597)
 3  pre()  [frameSkipIndex==0] (:599-601)
      viewport (full | centred 4:3 LEGACY-FORCED_4_3) → setProjectionMode(3D)
      → clear colour+depth on DEFAULT framebuffer
 4  EVENTS.clearStaleRefs()
 5/6 pause toggle / frame advance (debugger)
 7  normal frame:
      ignoreQueues = (frameSkipIndex != 0)   [all pools, all batches]
      renderFrame() (:850): clutAnimIndex=0 → renderCallback.run()
         ── gameLoop (Scus94491BpeSegment.java:137-194), phases 0-20 (R1 §3) ──
         │  phase 0  endFrame(): mask-bit GPU commands (z=3, z=size−1) queued to OT
         │  phase 1  GPU.startFrame(): dirty VRAM → vramTexture15 upload
         │  phase 4  state transition (RENDERER.setRenderMode + updateProjections,
         │           EngineStates.java:94-95)
         │  phase 5  renderUi() / phase 8  loadAndRenderMenus() / phase 9  uploadRenderables()
         │           → queueModel/queueOrthoModel calls fill modelPool/orthoPool
         │             (needsSorting = true; sequence++)
         │  phase 10 SCRIPTS.tick() (M3)
         │  phase 19 GPU.endFrame(): z-queues rendered BACK-TO-FRONT, same-z LIFO
         │           (M4-01 §4.2 — Battle.applyTextureAnimation executes here)
         │           → legacy/VRAM mode presentation; drawBufferIndex flip
         ────────────────────────────────────────────────────────────────────
      → clutAnimation uniform upload (terminator −1)
      GameOverlay.drawNotifications/drawFps
 8  main render pass [legacyMode==0] (:664-791; legacyMode cycles 0/1/2 on TAB in dev builds, :1346-1355):
      [battle pre-bind: battleTmdShader.use() + battleColour, "Gross hack bro" (:665-669)]
      bind renderBuffers[renderBufferIndex]
      [frameSkipIndex==0] clear colour
      for each extra batch (insertion order): renderBatch(batch)
      renderBatch(mainBatch)
        renderBatch (:884): [needsSorting] sortPerspectivePool (depth desc, seq asc/FIFO)
                             + sortOrthoPool (depth desc, seq desc/LIFO)
                             → initBatch (widescreen/scaling state) → clear DEPTH
                             → 3D opaque  (cull on,  proj 3D,  renderPool: role A opaque
                                           pixels + role B first-pass translucent pixels,
                                           128-model UBO chunks, per-model depth LESS,
                                           scissor uniform, fragment discard-masks)
                             → 2D opaque  (cull off, proj 2D)
                             → 3D translucent (renderPoolTranslucent: 4 modes in order,
                                           depth LEQUAL, depth-mask off, blend per §5.3;
                                           B+F/4 = B+F blend × 0.25 colour, F-13 resolved)
                             → 2D translucent
      unbind FBO → clear window depth → screen shader (post uniforms from config,
      turn-order bounds) → viewport = window → draw post quad sampling
      renderTextures[renderBufferIndex] → unbind textures
 9  bookkeeping [!paused]: [frameSkipIndex==0] resetBatches + delete marked
      Obj/Texture + scissor reset; frameSkipIndex++; renderBufferIndex += (frameSkip
      ? every m frames : every frame); vsyncCount += 60·m/fpsLimit; FPS roll
10  shader reload (dev); handleMovement() (freecam → worldToScreenMatrix, dev)
next tick: swap presents this frame's backbuffer (one-frame presentation latency)
```

**Inputs / outputs / lifetimes by stage:** queue-time inputs = game state + VRAM (textures) + scissor stack; sort inputs = modelView (camera) + screenspaceOffset; opaque outputs = FBO color (translucent-pixel-deferred) + no depth writes for translucent passes; translucent outputs = FBO color over the opaque result; post output = window backbuffer (CRT/post effects, §M5-04); pool entries live one frame (reset §2.2.9), Obj/Texture live until marked-deleted (persistent ones until shutdown); the CLUT-animation table lives one frame; the turn-order bounds live one frame.

## 14. Register updates

- **Q-36** (turbo/frame-skip): *advanced* — mechanism re-verified against the full onDraw handler (ignoreQueues on all pools of all batches, renderBufferIndex cadence, per-frame post pass); intent still `Inferred`.
- **Q-37** (render-path faithfulness): *advanced, scope narrowed* — the mode-0 coexistence mechanics are documented (§3, §6.4, §11); **F-13 resolved** (B+F/4 decomposition, §5.3 — the "alias or bug" question closes as intentional decomposition with a documented ~1/255 rounding difference vs the GPU model); **F-18 documented** as deliberate state pre-binding (the battle-tint is applied to every subsequent battle_tmd draw of the frame by design of the pre-bind position); remaining scope: the per-object mode-0 census (which TMD/battle/UI objects route to the GPU model vs the pools) — M5-02 (TMD objects) + M9 (battle objects).
- **Q-71** (new): **Modern-path equal-depth ordering asymmetry.** The perspective pool tie-breaks equal model-view z by **insertion order (FIFO)** (`perspectiveTranslucencySorter`, RenderEngine.java:1148); the ortho pool tie-breaks by **reverse insertion order (LIFO)** (`orthoTranslucencySorter`, :1150-1158). The PSX has no 2D "pool" — all primitives shared one ordering table (and its within-slot order is itself Q-66 Unresolved) — so neither tie-break has a direct hardware counterpart; the 2D LIFO mirrors the GPU model's traversal, the 3D FIFO does not. Whether any shipped 2D content (UI/text/HUD drawn at equal depth) exhibits visible order dependence, and whether the asymmetry is intentional, is undecidable from the repo. — `Open` — M5/M9 + (ideally) visual comparison — needs: an equal-depth 2D-content census in M5-03 (UI/text) and battle-HUD (M9); no action before visual evidence (a "fix" to symmetric tie-breaking could change visible 2D layering).
