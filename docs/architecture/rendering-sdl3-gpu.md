# Rendering on SDL3 GPU — Native Design

Based on: `docs/research/rendering-analysis.md` (Java behavior) + verified SDL3
GPU API surface (SDL 3.2+, `SDL_gpu.h`).

## 1. Device & Backends

- `SDL_CreateGPUDevice(usage, shader_formats, debug)`:
  - `usage`: `GPU_USAGE_RENDER | GPU_USAGE_COMPUTE` (compute reserved; not
    needed for MVP).
  - `shader_formats`: `SPIRV | MSL | DXIL` (all three artifacts shipped — see
    shader-migration.md) so SDL selects the best backend per platform.
  - Backend mapping (verified from SDL docs): `vulkan` (Windows/Linux),
    `direct3d12` (Windows 10+, FL 11.0 + RBT2; `ALLOW_FEWER_RESOURCE_SLOTS`
    property for Haswell/Broadwell if we care), `metal` (macOS 10.14+, Apple
    Silicon / Intel Mac2+).
  - `debug: true` in dev/Debug builds.
- Backend selection UI (Qt settings + CLI): list `SDL_GPU` device names via
  `SDL_GetGPUDeviceNames`; persist selection in global config (parity with
  Java's `GPU_PREFERENCE` launcher.conf concept, plus explicit device).
- Coordinate system: SDL3 GPU is left-handed, NDC z ∈ [0,1] (near=0), viewport
  y-down; SDL auto-converts backend differences (Vulkan) — **our shaders
  therefore target the SDL convention** (see §4 projection handling).

## 2. Frame Structure (port of verified Java flow)

```
per display frame:
  cmd = AcquireGPUCommandBuffer
  # transfers: upload per-frame data (transform2/light blocks for the next
  #   128-model window — computed when the model list changes; clutAnimation;
  #   primitiveParams for usePs1Depth models; per-draw uniform buffers)
  transferCmd = BeginTransferCommand(cmd)
    UploadToGPUBuffer(...) / UploadToGPUTexture(...)
  EndTransferCommand
  # scene pass → render target[writeIndex] (RGBA8) + depth (D32_FLOAT or D24)
  pass = BeginRenderPass(cmd, {color: RT[writeIndex], depth: depthTex,
                               loadOp: CLEAR(color=bg, depth=1.0)})
    SetViewport(0,0,renderW,renderH)
    for batch in batches:
      for pool-pass in [3D-opaque, 2D-opaque, 3D-translucent, 2D-translucent]:
        SetPipeline(passPipeline[pool-pass])         # pipeline cache lookup
        for modelWindow in pool (128-blocks):
          BindVertexBuffers(vb slot0)                # per-model vertex buffers
          BindVertexUniforms(set0, transforms2/light/clutAnim/projInfo/primParams)
          for model in window:
            BindVertexUniforms(set0, slot transforms)  # camera+projection (per pass)
            BindFragmentUniforms(set1, per-draw uniforms)  # modelIndex, flags...
            BindFragmentSamplers(set1, tex24, tex15)
            DrawIndexedPrimitives(...)
    # (scissor set per model via SetGPUScissorRect — real scissor + shader
    #   fallback uniform for the Intel iGPU case)
  EndRenderPass
  # post pass → swapchain
  swapTex = WaitAndAcquireGPUSwapchainTexture(device, window)
  pass = BeginRenderPass(cmd, {color: swapTex, loadOp: CLEAR})
    SetPipeline(postPipeline)
    BindFragmentSamplers(set1, screenTex = RT[readIndex])
    BindFragmentUniforms(set1, postUniforms)   # CRT settings, turn_order_bounds
    DrawPrimitives(postQuad, 4)
  EndRenderPass
  SubmitGPUCommandBuffer(cmd)
```

- Ping-pong render targets: 2× RGBA8 (R8G8B8A8_UNORM) + 1 shared depth
  (query D32_FLOAT first, fall back D24_UNORM per the SDL header guidance:
  "always check which of D24/D32 is supported"). `cycle=false`; the read
  target from N-1 is used as the post input (Java `getLastFrame()` behavior,
  used by `ScreenCaptureEffect`).
- Resolution policy (port of `onResize`): renderHeight = window height (or
  configured), renderWidth = renderHeight × (renderAspect from native 320
  scaling), **snapped renderHeight to multiple of 240** (UI line alignment),
  width recomputed; letterbox for LEGACY forced-4:3 (viewport offset).
- Render batches: same model as Java (each batch = independent render pass
  with its own projection/widescreen; depth cleared between passes — note SDL
  guidance "beginning a new render pass is expensive": we have 1–3 passes +
  post, acceptable).
- Depth-write-off translucent passes: per-pipeline `depth_stencil_state
  .write_mask = false` + compare per model via per-model pipeline variant or
  per-pass grouping (compare mode is pipeline-static; the Java
  `translucentDepthComparator` is a small enum — bake into pipeline variants,
  at most a few per pool).

## 3. Pipelines (static state)

Pipeline variants needed (small finite set, created at init):
- `simple` (UI quads): no depth test, no blend.
- `standard`: {depth test on/off} × {cull on/off} × {blend: none/half/bplusf/
  bminusf/bquarter(scale-in-shader)} — used by 2D UI models + ortho.
- `tmd`: same axes + {usePs1Depth on/off} (primitiveParams path).
- `battle_tmd`: tmd + battle lighting uniform (same pipeline, dynamic uniform).
- `post`: no depth, no blend.
- `copy`: no depth, no blend (texture copy for mod assets/save-card textures).
Estimate ≈ 20–40 pipelines total. Pipeline creation is cheap-ish; create at
init and cache (SDL: "don't churn resources").

**Blend mapping (verified SDL enums):**
- none: `enable_blend=false`, depth write on
- HALF_B_PLUS_HALF_F: `ADD`, `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA`
- B_PLUS_F: `ADD`, `ONE`/`ONE`
- B_MINUS_F: `REVERSE_SUBTRACT`, `ONE`/`ONE`
- B_PLUS_QUARTER_F: `ADD`, `ONE`/`ONE` + fragment multiplies F by 0.25 (see
  shader-migration.md §5)
All translucent: `write_mask=false` on depth.

## 4. Shader Interface Conventions

- GLSL 450 source with **explicit `layout(set = N, binding = B)`** on all
  resources (required for SPIRV-Cross to map to MSL/DXIL bindings).
- Entry point `main` (SDL default entrypoint convention).
- Vertex inputs via `layout(location = i)` matching `SDL_GPUVertexAttribute`
  locations:
  - TMD: 0 `vec4 inPos` (w packed: primitiveIndex|vertexInPrimitive — see
    shader-migration §3), 1 `vec3 inNorm`, 2 `vec2 inUv`, 3 `float inTpage`,
    4 `float inClut`, 5 `vec4 inColour`, 6 `float inFlags`.
  - UI quad: 0 `vec2 inPos`, 1 `vec2 inUv`.
- Uniform bindings (per shader-migration.md §5): set0 (vertex): 0 transforms
  (camera+proj, per pass), 1 transforms2[128], 2 lighting[128], 3
  clutAnimation[1024], 4 projectionInfo, 5 primitiveParams (tmd only);
  set1 (fragment): 0 tex24 (sampler), 1 tex15 (sampler, R32_UINT), 2 scissor
  (16B), 3 per-draw uniform struct (all per-model scalars + battleColor +
  post uniforms share this slot pattern per pipeline).
- Small per-draw data: use `SDL_PushGPUVertexUniformData`/
  `SDL_PushGPUFragmentUniformData` **or** the per-draw uniform buffer bound in
  set1 — decision: **per-draw uniform buffer** (uniforms stay in one place,
  no 4-slot limit issues; the push slots are for data that's pushed without a
  buffer — we don't need that).
- **Projection handling with SDL's left-handed [0,1] z**: the Java pipeline
  uses an OpenGL-style projection (NDC z ∈ [-1,1], +z into screen, y-up).
  Native shaders compose the same *world→view* math (GTE-derived view matrix)
  but with a right... no — with a **left-handed-style projection matrix**
  built to match SDL's convention: we keep the PS1 projection *semantics*
  (divide-by-H for PS1 mode; screen offset added after division) but express
  final positions in SDL NDC (z ∈ [0,1] near=0). Concretely: the
  `projection` matrix in the `transforms` UBO becomes the SDL-convention
  projection; PS1 perspective mode keeps its divide-by-z path (done in view
  space before the projection, identical to Java); `gl_FragDepth` linearization
  becomes writing to SDL's [0,1] depth range (`(depth - znear) * zdiffInv +
  depthOffset` maps directly since both use near=0..far=1 normalized depth).
  **This is a pure shader-side change; world/view math and GTE state are
  untouched.** Ortho mode: same (SDL ortho with y-flip — viewport y-down
  means our 2D content, authored in Java's y-down screen space, maps 1:1;
  verify with the title-screen golden test).
- Depth comparators: `SDL_GPUCompareOp` LESS (opaque), LEQUAL/LESS per
  translucent model comparator (port `DepthComparator` enum).

## 5. Texture System

- `tex15` = `SDL_GPU_TEXTUREFORMAT_R32_UINT` texture (SAMPLER usage; **query
  `SDL_GPUTextureSupportsFormat` at init — R32_UINT sampling is broadly
  supported; if a driver refuses, fallback: unpack to RGBA8 at upload for that
  backend, decode moves to CPU — correctness preserved, documented).
  CLUT data packed in the same texture at PS1 VRAM coordinates (Java layout).
- `tex24` = `R8G8B8A8_UNORM` (24bpp content, alpha=1).
- Samplers: nearest (all game textures — PS1 fidelity), clamp/edge for atlas
  quads; `mip_lod_bias` is a no-op on Metal (irrelevant, no mips).
- Upload: TIM/DABA → CPU packed 15-bit rows (4bpp: 2px/byte expanded to R32
  slots; 8bpp: 1px/byte; 15bpp: direct) → transfer buffer → texture. Same
  "absolute VRAM coordinates" stitching model (tpage regions) preserved — the
  native `VramTexture` equivalent. <!-- naming-allow: Java class name cited verbatim -->
- Mod atlas (512×512 packer) + UI PNGs (PNG decode — SDL3 has `SDL_LoadBMP`/
surface APIs; PNG decoding is **not** in SDL3 core → use **SDL3_image**,
the official SDL3 companion, zlib-licensed C — see dependencies.md, G2).
- Render targets: RGBA8; depth as above.
- Save-card textures & DABA extraction: port (GPU VRAM upload path identical).

## 6. Geometry Submission

- TMD meshes: static vertex buffer per TMD obj (port `TmdObjLoader` output),
  index buffer grouped per primitive (for the primitiveParams/indirect path).
- Draw strategy:
  - non-usePs1Depth models: single indexed draw per TMD mesh (all
    primitives), modern depth.
  - usePs1Depth models: per-primitive draws with `primitiveParams` buffer
    (avgDepth + culled flag computed on CPU from the model matrix — see
    shader-migration §3 option B); culled primitives skipped entirely (draw
    list is precomputed per mesh, filtered per frame).
- `transform2[128]` blocks: as in Java — when the model window changes (i%128),
  upload the block's matrices (80B×128) + lighting block (128B×128). The model
  matrices are the same `MV` data the sim computed (GTE RT/TR or camera
  matrices) — CPU-owned, no divergence.
- UI: MCQ quads + text glyph quads → `simple`/`standard` pipelines into the
  ortho pool (2D pass, ortho projection, `widescreenOrthoOffsetX` preserved).

## 7. Frame-Skip / 30Hz Presentation

- Sim at 30Hz (vsyncMode=2) → render presents every display frame; the model
  queue is only rebuilt on sim frames (equivalent to Java's `ignoreQueues` on
  skipped frames: at 30 sim the odd display frames re-issue the identical
  queue — implement by simply re-encoding the previous frame's draw list;
  cost: one render pass re-issue. Simpler alternative: present the same
  swapchain image twice via two `WaitAndAcquire`... no — SDL swapchain
  requires rendering each present. So: re-encode previous draws (cheap: no
  CPU transform recompute, same buffers). Documented as a presentation
  choice; visually identical to Java's behavior.

## 8. Free-Cam & Debug (in-engine)

- Port of RenderEngine free-cam (yaw/pitch/strafe/zoom keys, cursor hide),
  pause, frame-advance, wireframe (pipeline variant), shader reload
  (artifact re-load). All behind a debug config flag, matching Java's
  `!Version.isRelease()` gating.

## 9. Verification Plan (linked to testing.md)

1. Milestone 2: triangle + textured quad on all three backends; pipeline
   creation smoke test for every pipeline variant.
2. Milestone 7: golden screenshots (title, field, battle menu, addition
   overlay, credits) native-vs-Java (same assets, same camera poses, fixed
   RNG), with per-scene tolerance + visual diff review; PS1-depth battle
   overlap check (specific battle: use a battle with heavy overlapping CTMDs);
   translucent pass checks (two-pass quirk scene: Shirley transform fight);
   scissor check (same scene that hit the Intel bug).
3. Performance: frame-time budget vs Java on the same machine (measure, don't
   assume — the sim+render work is similar in magnitude).
