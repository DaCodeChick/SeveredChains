# Shader Migration — GLSL 3.30 → SDL3 GPU (SPIR-V / MSL / DXIL)

Reference commit: `ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb`
Shader sources: `gfx/shaders/` in the reference repo (GLSL `#version 330 core`).

## 1. Shader Inventory (verified)

| Name | Files | Lines | Purpose |
|---|---|---|---|
| Simple | `simple.vsh/.fsh` | 20/13 | UI textured quads: `transform` UBO (cam+proj) + `transforms2` (model), `shiftUv`, `recolour` (vec4 multiply) |
| Copy | `copy.vsh/.fsh` | 13/12 | Texture copy (FBO readback / `copyTexture`): plain `projection` uniform, forces `a = 1.0` |
| Post | `post.vsh` + `screen.fsh` | 12/292 | Fullscreen CRT/VHS post pipeline (scanlines, grille, pixelate, roll, noise, aberration, brightness, discolour, warp, vignette, bloom, turn-order bounds). Vertex shader is a passthrough quad (NDC) |
| Standard | `standard.vsh/.fsh` | 155/126 | 2D/ortho + simple 3D models (UI models, text quads, shadow): PS1 lighting in VS, tpage/clut decode, CLUT animation remap, PS1/modern projection modes, in-shader scissor, translucency discard modes |
| TMD | `tmd.vsh` + `tmd.gsh` + `tmd.fsh` | 163/115/119 | 3D game models (TMD/CTMD): same as Standard + geometry shader (PS1 near-plane cull, per-primitive average depth when `usePs1Depth`) |
| Battle TMD | `battle_tmd.vsh` + `tmd.gsh` + `battle_tmd.fsh` | 180/122 | TMD + battle-specific: `ctmdFlags` (0x20 ctmd, 0x10 uniformLit, 0x2 translucent), `battleColour` uniform lighting for unlit textured translucent CTMDs with per-channel `mod(2.0)` PS1 overflow replication |

Shared uniform blocks (std140):
- `transforms`: `mat4 camera; mat4 projection;` (per-pass, 32B)
- `transforms2[128]`: `mat4 model; vec4 screenOffset;` (80B stride, per-model, 128-blocks)
- `lighting[128]`: `mat4 lightDirection; mat3 lightColour; vec4 backgroundColour;` (128B stride)
- `clutAnimation`: `vec4[1024]` (`{origX, origY, replX, replY}`, -1 terminator)
- `projectionInfo`: `float znear, zfar(H), zdiffInv, projectionMode(0/1/2)`
- `scissor`: `float x, y, w, h`

Per-pipeline plain uniforms: `tex24` (sampler2D), `tex15` (usampler2D),
`modelIndex`, `recolour`, `uvOffset`, `clutOverride`, `tpageOverride`,
`discardTranslucency`, `translucency`, `alpha`, `useTextureAlpha`,
`tmdTranslucency`, `usePs1Depth`, `ctmdFlags`, `battleColour`, `shiftUv` (simple),
`projection` (copy), all screen.fsh post uniforms.

Vertex input layout (TMD/Standard): location 0 `vec4 inPos` (w = vertex index),
1 `vec3 inNorm`, 2 `vec2 inUv`, 3 `float inTpage`, 4 `float inClut`,
5 `vec4 inColour`, 6 `float inFlags`.

## 2. GLSL Features in Use (porting checklist)

- `texelFetch` on a `usampler2D`/`R32UI` texture (palette index + CLUT decode,
  15-bit unpack) — supported on all three SDL3 GPU backends (Vulkan yes,
  Metal yes via MSL 2.x, D3D12 yes).
- `gl_FragDepth` write (modern-perspective linearization) — Vulkan yes; Metal:
  `[[depth(any)]]` requires the render pass to declare depth attachment as
  `depth(any)` (SDL3 GPU allows depth formats; verify `SDL_GPU_DEPTH_FORMAT`
  any-write support on Apple); D3D12: SV_Depth requires pipeline state with
  depth semantics — supported. **Verification item for Milestone 2/3.**
- In-shader scissor via `discard` (Intel iGPU workaround) — trivially portable.
- Loops with dynamic bounds over the CLUT animation array (≤1024, break on -1) —
  fine in all backends (SPIR-V loop; D3D12 level 11.1+ unbounded; 1024 iterations
  is well within limits).
- No derivatives, no image stores, no atomics, no multisampling, no tessellation.
- No geometry shader in: Simple/Copy/Post/Standard; **only TMD/Battle TMD use GS**
  (`triangles_adjacency` → `triangle_strip`).

## 3. Geometry Shader Replacement (decision)

The TMD GS does exactly two things (verified in `tmd.gsh`):
1. **PS1 near-plane cull**: discard the whole primitive if any of its real
   vertices has `2 * viewspaceZ >= zfar(H)` (PS1 clips faces closer than H/2;
   visible artifact in the original when the camera is too close).
2. **Per-primitive depth**: when `usePs1Depth` (data-driven per model,
   `Model124.usePs1Depth`; used by battle entities, submap models, sobj effects),
   all emitted vertices get the *average* clip-space Z of the input vertices
   (PS1 ordering-table per-primitive depth). Quads (flag 0x10) include the 4th
   vertex in the average and cull test.

SDL3 GPU has no geometry-shader support on D3D12/Metal, so the GS is replaced.
Options considered:

| Option | Pros | Cons |
|---|---|---|
| A. CPU cull + per-primitive draw (push constant avgDepth) | Simple, exact, no new shader stages | Draw-call per primitive for usePs1Depth models |
| B. CPU cull + per-vertex primitive-data buffer (VS lookup) | One draw per mesh; exact; scales | Per-frame CPU fill of primitive buffer (only for usePs1Depth meshes); needs primitiveIndex attribute |
| C. Two-pass (VS → compute → VS) | GPU-resident | Complexity, extra sync, overkill |
| D. Ignore (modern depth + no near cull) | Simplest | Wrong battle draw order for PS1-depth models; visible near-clip differences — **fails fidelity requirement** |

**Recommendation: B** (fallback to A for the first iteration of Milestone 7).
Details:
- Mesh build: add `u32 primitiveIndex` attribute (or encode in `inPos.w` — the
  current `w = vertexIndex` becomes `{primitiveIndex, vertexInPrimitive}`;
  layout change is confined to TmdObjLoader-equivalent).
- Per frame, per usePs1Depth model: CPU already has the model matrix
  (`transforms2` data is CPU-authored). For each primitive, compute view-space Z
  of 3–4 vertices (3–4 mat-vec multiplies), the `2z >= H` cull test (using the
  current camera + H — both known on CPU), and average Z. Write
  `{avgDepth, culled}` into a per-model `PrimitiveParams` buffer (uploaded with
  the other per-frame data).
- Vertex shader: look up `primitiveParams[inPos.w_primitive]`; if culled, emit a
  degenerate (off-screen) position; `gl_Position.z = avgDepth` when usePs1Depth
  (for non-PS1-depth models the existing per-vertex path is kept by a
  pipeline-level or per-model uniform).
- Cost bound: usePs1Depth is a per-model flag; non-battle fields rarely set it.
  Even at ~10k primitives/model × few models, the CPU cost is microseconds with
  SIMD-friendly loops.
- The cull test needs the *view* matrix (camera UBO) — the same data the shader
  consumes; no numerical divergence because it's the same float math
  (JOML float32 today; GLM float32 in native — keep 32-bit floats for parity).

## 4. Target Representations & Toolchain

SDL3 GPU backend artifact requirements:
- **Vulkan (Windows/Linux):** SPIR-V (GLSL 450 core, `SPV_1_3`+).
- **Metal (macOS/Apple Silicon):** MSL ≥ 2.4.
- **Direct3D 12 (Windows):** DXIL.

Proposed build pipeline (single GLSL source of truth, mirroring the reference's
own shaderc+SPIRV-Cross usage):

```
gfx/shaders/*.vsh/.fsh/.gsh (GLSL 330, repo-owned)
   │  build-time (CMake custom commands)
   ├─ glslangValidator → SPIR-V (.spv)          [Vulkan, and input for the rest]
   │      └─ spirv-cross → MSL (.metal)         [Metal]
   │      └─ spirv-cross → HLSL → dxc → DXIL    [D3D12]
   └─ (debug: keep GLSL sources for editor/log correlation)
```

**Dependencies requiring approval:**
| Dependency | License | Purpose | Alternative |
|---|---|---|---|
| **glslang** (Khronos) | MIT | GLSL → SPIR-V build-time compiler; error diagnostics with line/col | shaderc (BSD, what Java uses — heavier, more deps) |
| **SPIRV-Cross** | MIT | SPIR-V → MSL and SPIR-V → HLSL translation | hand-write MSL/HLSL per shader (maintenance trap); MoltenVK/D3D12-over-Vulkan (rejected: we want native backends) |
| **DXC** (Microsoft DirectX Shader Compiler) | MIT | HLSL/SPIR-V → DXIL for D3D12; `dxc -spirv` can also do SPIR-V→DXIL directly (skips HLSL step) | fxc (dead, no DXIL); ANGLE (rejected) |

All three are stable, MIT-licensed, header/amalgamation-friendly or single-binary
tools; used at **build time only** (no runtime cost). DXC additionally provides
great compile-time diagnostics for the DXIL path.

Fallback if DXC on Linux/macOS proves awkward: pre-generate DXIL on Windows at
publish time and commit DXIL artifacts (documented, versioned), or restrict D3D12
artifact generation to Windows CI. This must not block Vulkan/Metal.

## 5. Binding Layout Design for SDL3 GPU

SDL3 GPU uses explicit `SDL_GPUShader` + `SDL_GPUVertexInput`/`SDL_GPUResourceLayout`
via `SDL_GPUComputePipeline`/`SDL_GPUGraphicsPipeline` (`SDL_GPUPipelineLayout`).

Conventions:
- **Set 0 (vertex):** `bindings`:
  - 0: `transforms` (32B, per-pass → **push constants** are an option; SDL3 GPU
    has no push constants, so use a small dynamic uniform buffer)
  - 1: `transforms2` (128×80B, per-128-model-window dynamic buffer)
  - 2: `lighting` (128×128B, dynamic)
  - 3: `clutAnimation` (1024×16B, per-frame dynamic)
  - 4: `projectionInfo` (16B)
  - 5: `primitiveParams` (TMD only, per-model dynamic)
- **Set 1 (fragment):** `tex24` (sampler-bound texture), `tex15`
  (sampler-bound R32UINT texture), `scissor` (16B dynamic).
- Plain per-draw uniforms (`modelIndex`, `recolour`, translucency flags,
  `battleColour`, `usePs1Depth`, post uniforms...) → one **per-draw uniform
  buffer** bound in the fragment set (value-sized, updated per draw call),
  replacing GL's individual uniform locations. This is the standard SDL3 GPU
  pattern and keeps pipelines static (no per-state pipeline variants).
- **Pipeline variants** (static states baked into pipelines):
  - `standard` (no GS), `tmd` (no GS, uses primitiveParams path), `battle_tmd`.
  - Per-pipeline static: backface cull mode, depth test/compare, blend mode.
    Verified SDL3 GPU blending (SDL 3.2, `SDL_GPUBlendFactor`/`SDL_GPUBlendOp`):
    - HALF_B_PLUS_HALF_F: `BLENDOP_ADD`, `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA`
    - B_PLUS_F: `BLENDOP_ADD`, `ONE` / `ONE`
    - B_MINUS_F: `BLENDOP_REVERSE_SUBTRACT`, `ONE` / `ONE`
    - **No QUARTER blend factor exists in SDL3 GPU.** Note the Java reference
      itself does not implement true B+1/4F on the modern GL path:
      `GlApi.translucency` throws for `B_PLUS_QUARTER_F`, and
      `RenderEngine.renderPoolTranslucent` renders B+1/4F models using the
      `B_PLUS_F` blend state (a known simplification). The software GS path
      (`Gpu.handleTranslucence`) does implement true B+1/4F.
      **Native decision:** implement true B+1/4F as `BLENDOP_ADD`, `ONE`/`ONE`
      with the fragment shader scaling the foreground color by 0.25 when the
      dynamic `translucency` uniform equals B_PLUS_QUARTER_F — exact result,
      no driver support needed. (Matching the Java modern path's B+F fallback
      is also acceptable for the default mode; the fidelity GS mode has true
      B+1/4F either way.)
    - Translucent passes: depth test on with per-model compare, **depth write
      off** (Java sets `glDepthMask(false)` for all translucent passes).
  - `discardTranslucency` 0/1/2 stays a dynamic uniform (it's data-driven per
    model, not state-driven).
- Vertex input: TMD layout above (16 floats + 1 u32 primitive index, or re-packed
  `inPos.w`); Simple/Copy/Post quads: `{vec2 pos, vec2 uv}`.

## 6. Shader Build, Caching, Diagnostics

- **Build step (Milestone 4):** CMake `add_custom_command` per shader stage:
  glslang → `.spv` (Vulkan target `spirv1.3`), spirv-cross → `.metal`
  (`--msl-version 2_4`, iDeviceFamily for Apple Silicon), dxc → `.dxil`
  (`-spirv` or from HLSL, `--target dxil_1_2`... D3D12 Shader Model 6.x — verify
  minimum SM for the features used; none are SM6-exclusive, SM 5.1/6.0 both fine).
- **Caching:** content-addressed (SHA-256 of GLSL source + tool versions +
  target) → `build/shader-cache/<hash>.<spv|metal|dxil>`; invalidated by source or
  tool change. Runtime loads the compiled artifact next to the binary
  (`resources/shaders/<name>_<hash>.<ext>`).
- **Diagnostics:** glslang/dxc stderr captured into CMake build failure output
  (file:line preserved); a `--shaders-verbose` runtime option logs which artifact
  (hash, backend) loaded per pipeline.
- **Runtime hot-reload** (Java has `ShaderManager.reload` bound to a debug key):
  supported by re-loading artifacts from disk when mtime/hash changes — cheap
  since pipelines are cheap to recreate.
- **Validation:** run a headless pipeline-creation test (Milestone 2/3) that
  creates every pipeline on each available backend and asserts success; plus a
  golden-image test (Milestone 7) comparing native vs Java output per shader.

## 7. Fidelity Notes (from shader sources)

1. Colour range: lit textured vertices clamp to 2.0 (PS1 0..255 → 0.0..2.0),
   unlit to 1.0 — replicated in `standard.vsh`/`tmd.vsh`; keep.
2. `battle_tmd` per-channel `mod(c, 2.0)` overflow — keep (visual effect in
   specific battle CTMDs).
3. Tpage/CLUT math: `tpage & 0xf → x*64`, `0x10 → y+256`, `>>7&3 → bpp`,
   `>>5&3 → translucency`; `clut & 0x3f → x*16`, `>>6 → y`. Keep exactly.
4. 15-bit unpack `/31.0` — keep (PS1 5-bit channels).
5. `screen.fsh` `turn_order_bounds` interaction with a turn-order mod — document
   the mod interface; native must expose the same uniform.
6. Integer `gl_FragCoord` scissor comparison — keep identical comparison to avoid
   edge-pixel differences.