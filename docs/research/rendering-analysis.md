# Rendering Analysis — Severed Chains (Java) → Native

Reference commit: `ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb`
All paths relative to `~/Documents/GitHub/Severed-Chains`.

## 1. Renderer Architecture (verified)

The renderer is split into three layers:

| Layer | Classes | Role |
|---|---|---|
| Engine | `legend.core.renderer.RenderEngine` (1,484 lines) | Main loop frame, batches, pools, uniform uploads, post-processing, free-cam/debug |
| Queue | `RenderBatch`, `QueuePool`, `QueuedModel*`, `Obj`/`MeshObj`, `Texture` | Per-frame model/texture submission & deferred resource cleanup |
| API | `RenderApi` + `opengl.GlApi` / `opengles.GlesApi` | Thin GL 3.3 / GLES 3.2 bindings (shaders, VBOs, UBOs, FBOs, state) |

`RenderApi` (`src/main/java/legend/core/renderer/RenderApi.java`) is the only
GL-facing seam: `makeShader`, `makeUniformBuffer`, `makeMesh`, `bindFramebuffer`,
`clear`, `translucency(...)`, `scissor(...)`, `enableDepthTest`, `wireframe`,
`viewport`, `resize`, `initBatch`, `useTexture`, etc. This maps directly onto an
SDL3 GPU device + command-buffer design (see `docs/architecture/rendering-sdl3-gpu.md`).

### Submission model

- Every drawable is an `Obj` (geometry + vertex attributes) or `MeshObj` (TMD-derived).
- `RenderEngine.queueModel(obj, mv, type)` / `queueOrthoModel(...)` create a
  `QueuedModel*` in the batch's `QueuePool` (perspective `modelPool` or 2D
  `orthoPool`). Queued models are generic over a `type` (class token) selecting the
  per-model uniforms/behavior: `QueuedModelStandard`, `QueuedModelTmd`
  (+ geometry shader pipeline), `QueuedModelBattleTmd`.
- `Translucency` enum (`Translucency.java`) mirrors PS1 GS blending: `NONE`,
  `HALF_B_PLUS_HALF_F` (half alpha), `B_PLUS_F`, `B_MINUS_F`, `B_PLUS_QUARTER_F`.
  `FOR_RENDERING` = the non-opaque modes.
- At frame time, pools are sorted (perspective: view-space Z from cached
  modelView `m32` + screenspaceOffset, then insertion sequence; ortho: Z then
  sequence) and drawn in 4 passes per batch:
  1. opaque 3D (backface culling on),
  2. opaque 2D ortho,
  3. translucent 3D back-to-front (culling off),
  4. translucent 2D back-to-front.
- **Two-pass translucent quirk (PS1 fidelity):** within a translucent primitive's
  first pass, opaque pixels (translucency bit clear in 4/8bpp texture) are drawn
  *without* blending, and only then the truly-translucent pixels are drawn in pass
  3 (see `RenderEngine.renderPool` "First pass of translucency rendering" comment
  and `standard.fsh` `discardTranslucency` uniform 0/1/2). This must be preserved
  for texture-driven translucency to look identical.
- **Deferred deletion:** `Obj.deleteObjects()` / `Texture.deleteTextures()` at end of
  frame — resources marked for deletion are freed after rendering. A native design
  with GPU-fence-tracked lifetimes (SDL3 GPU handles) is a natural fit.
- Multiple `RenderBatch`es supported (`addBatch()`); each has its own projection,
  widescreen config; depth cleared between batches. Used e.g. for overlays.

### Frame flow (verified, `RenderEngine.init()` `onDraw`)

```
1. run queued tasks (addTask)
2. deferred renderbuffer resize
3. pre(): viewport (legacy 4:3 letterbox or full), set 3D projection mode, clear color+depth
4. [if not paused] renderFrame():
     - run game render callback (game update + model queueing + SFX queuing)
     - upload CLUT animation UBO
5. bind renderBuffers[renderBufferIndex] (FBO: RGBA8 color + shared float depth)
6. clear depth; for each batch: sort; initBatch; clear depth;
   renderPool (3D opaque, 2D opaque, 3D translucent, 2D translucent)
7. disable depth, unbind FBO, clear backbuffer
8. draw fullscreen post quad with `screen` shader (CRT/post effects)
   - turn-order mod bounds uniform when a turn-order mod is visible
9. unbind textures; advance renderBufferIndex (frame-skip aware)
10. FPS stats / title update; reload shaders if requested; free-cam movement
```

- Two ping-pong render buffers (`RENDER_BUFFER_COUNT = 2`) + one shared depth
  texture; only the color buffer index advances, so the previous frame is still
  available (`getLastFrame()`, e.g. for screen-capture battle effects).
- `frameSkip` (PS1 30Hz): when the game wants 30fps, the frame renders at 60Hz but
  model queueing only happens when `frameSkipIndex == 0` (`ignoreQueues`) and the
  buffer advances only on the last skipped frame — the 30Hz frame is shown twice.
- `paused` / `frameAdvance` / `wireframeMode` are debugger features
  (`InputAction` bindings in `onInputActionPressed`).
- Resolution handling (`onResize`): render resolution = window height (or configured
  vertical resolution), width keeps aspect of the *game* (320-native scaled),
  forced to even multiples of 240 vertically to keep UI lines aligned
  (`MathHelper.nextMultiple(renderHeight, 240)`).

### Cameras & projections (verified)

- `camera2d` (`BasicCamera`, ortho) and `camera3d` (`QuaternionCamera`) stored on the
  render engine; game sets `camera3d` via GTE-derived matrices.
- `setProjectionMode` uploads: `projectionInfo` UBO = `{ znear=0, zfar=H
  (projection plane distance), zdiffInv, mode }` where mode is 0=ortho,
  1=PS1 perspective (divide-by-Z in shader), 2=modern perspective (with
  `gl_FragDepth` linearization for ortho-at-depth overlaying,
  `depthOffset = screenOffset.z * zdiffInv`).
- `transforms` UBO = camera (view) matrix + projection matrix (shared per pass).
- `transforms2` UBO = **128 × (mat4 model + vec4 screenOffset)** — per-model
  matrices uploaded in 128-blocks as models are drawn; `modelIndex = i & 0x7f`.
- `lighting` UBO = **128 × (mat4 lightDirection + mat3 lightColour + vec4
  backgroundColour)** — PS1 flat lighting parameters replicated per model (same
  values, padded to the block).
- Screen offset (X/Y) is added *after* projection-plane division, before the
  projection matrix (PS1 OFSX/OFSY order — verified in all model vertex shaders).
- Widescreen: `RenderBatch.widescreenOrthoOffsetX` shifts 2D content; submap
  widescreen modes in `SubmapWidescreenMode` (legacy vs forced 4:3).

### PS1 state emulation (verified)

- `legend.game.Graphics` — game-side GS state: ordering table
  (`orderingTableBits_1f8003c0`, `zShift_8004dd34/zMin/zMax`), tpage/clut globals
  (`tmdGp0Tpage_1f8003ec`, `tmdGp0CommandId_1f8003ee`), draw offset/clip,
  display buffers, `vsyncMode_8007a3b8`, `renderMode`, light matrices
  (`lightDirectionMatrix_800c34e8`, `lightColourMatrix_800c3508`),
  `worldToScreenMatrix_800c3548`, `Gs*` helper functions.
- `legend.core.gte.Gte` — software PS1 GTE: `perspectiveTransform` (RTPS),
  `perspectiveTransformTriangle` (RTPT), `normalClipping` (NCLIP winding),
  `normalColour` (NCCS flat lighting: `clamp(dot(N,L)) * lightColour +
  backgroundColour`, 8-bit clamp), `averageZ3/4` (AVSZ), FLAG register with
  PS1 saturation semantics (SXY clamp ±1024, SZ clamp 0..0xffff, Z clamp
  0..65536).
- `legend.core.gpu.Gpu` — **full software PS1 GS**: 1024×512 VRAM as packed
  15-bit pixels, per-frame command queue sorted into an ordering table,
  `rasterizeTriangle` with perspective-correct UV/colour interpolation,
  top-left rule for translucent polys (`isTopLeft`), PS1 blending
  (`handleTranslucence`), `uploadData15`/`downloadData15`, `copyVramToVram`.
  Rendered to a `vramTexture15` (R32UI) texture.
- Three render modes (TAB key cycles, `RenderEngine.legacyMode` static):
  0 = **OpenGL/HQ** (default; modern shaders; GTE used for camera/lighting only),
  1 = **Legacy** (per-primitive CPU GTE + `GPU.queueCommand` → software GS),
  2 = **VRAM** (software GS output presented; most artifact-faithful).
  Legacy path verified in `legend.game.tmd.Renderer.renderTmdPrimitive`
  (only active when `legacyMode == 1`).

## 2. Texture Pipeline (verified)

- Source formats: PS1 **TIM** (`legend.game.tim.Tim`: 12-byte header,
  flags = bpp (3 bits) | hasClut (bit 3), CLUT rect + image rect, packed pixel
  data), **DABA** VRAM pages (`legend.core.gpu.VramTextureLoader`), PNG (bundled UI
  art in `gfx/`).
- `Bpp` enum: `BITS_4` (2 px/byte), `BITS_8`, `BITS_15` (A5R5G5B5), `BITS_24`.
- Upload layout (verified in `GlTexture` + shaders):
  - 4/8/15bpp content → one **`tex15` = R32UI** texture: 32-bit value in the `.r`
    channel holds the 4/8-bit palette *index* or the 15-bit color; CLUTs live in
    the same texture at their PS1 VRAM coordinates.
  - 24bpp content → **`tex24` = RGBA8** texture.
- Fragment shader does the decode (`standard.fsh`/`tmd.fsh`): `texelFetch(tex15)`
  for palette index + CLUT fetch (4/8bpp), 15-bit unpack (`>> 15/10/5/0` × /31),
  or `texture(tex24)` (24bpp). No hardware filtering on the 15bpp path
  (texelFetch = nearest, matching PS1).
- **CLUT animation** (`RenderEngine.addClutAnimation`, `CContainerClutAnimations`,
  `ClutAnimation`): per-frame array (≤1024) of `{origClutX, origClutY,
  replClutX, replClutY, -1 terminator}`; vertex shader remaps CLUT coordinates for
  animated palette effects (CContainer sprites).
- `VramTextureSingle`/`VramTextureStitched`: textures stitched at absolute PS1 VRAM
  coordinates (tpage points to 64×256 sub-regions; tpage encoding:
  `(tpage & 0xf) * 64` = x, `tpage & 0x10` → y+256, `tpage >> 7 & 3` = bpp,
  `tpage >> 5 & 3` = translucency for 4/8bpp).
- `TextureAtlas`/`TexturePacker` (mod content): mod-provided images packed into a
  512×552-ish atlas (512×512 in `bootRegistries`), event `RegisterAtlasTexturesEvent`.
- Save-card textures: `SaveCardTextureExtractor` (unpacker) extracts save card face
  textures from the original save format; `DabasManager` reads/writes DABA save
  data embedded in SC saves.

## 3. Geometry (verified)

- TMD → `TmdObjLoader.fromTmd` builds GPU meshes: vertex layout
  `{ pos(4: xyz + vertexIndex), normal(3), uv(2), tpage(1), clut(1), colour(4:
  ABGR normalized), flags(1: 0x1 lit, 0x2 textured, 0x4 coloured, 0x8 translucent,
  0x10 quad) }` = 16 floats/vertex. Primitives decoded from PS1 GP0 command bits
  (gouraud/flat, quad/tri, lit/unlit, textured/untextured, translucent).
- One `Obj` per TMD "obj table" (parts); TMDs split into sub-meshes by translucency
  (`TmdObjLoaderMeshes`: opaque + one per translucency mode).
- CTMD (`legend.game.unpacker.Ctmd`): scaled-vertex TMD variant (battle/cutscenes);
  converted to TMD at unpack time (`CtmdTransformer`); battle CTMDs carry per-hit
  lighting flags (`ctmdFlags` uniform: 0x20 ctmd, 0x10 uniformLit, 0x2 translucent).
- **TMD geometry shader** (`gfx/shaders/tmd.gsh`, `triangles_adjacency` in →
  `triangle_strip` out): (a) culls the whole primitive if any vertex has
  `2*z >= H` (PS1 near-plane clip — faces too close to camera vanish),
  (b) replaces per-vertex depth with the primitive's *average* depth when
  `usePs1Depth` (PS1 per-primitive Z from the ordering table — critical for battle
  draw order). Input is quads-as-adjacency-triangles (4/6 vertices).
- UI geometry: `McqBuilder` (MCQ menu quad files), `QuadBuilder`, `LineBuilder`,
  `PolyBuilder`, `TexturePacker`; text rendered as quad atlases
  (`legend.core.font.Font` + glyph quads, `legend.game.Text` textboxes).
- Particles/effects (`legend.game.combat.effects`, `particles`): TMD particles
  (`TmdParticle`), quad particles, screen-space effects (lens flare, distortion,
  screen capture via `getLastFrame()`), SEffe special-effect engine
  (`SEffe.java`, 4,592 lines).

## 4. Postprocessing (verified)

- `screen.fsh` (292 lines): optional CRT/VHS pipeline — manual bilinear +
  pixelation at chosen resolution (240/480/720/N÷2/N÷3/N÷4), scanlines, aperture
  grille, roll distortion (Perlin noise driven), chromatic aberration, brightness,
  VHS discolour, edge warp, vignette, bloom (high-pass + radius), and a
  `turn_order_bounds` uniform that "cheats" the pixelation scale in the turn-order
  HUD region for legibility.
- When `enableCrt == false` the shader is a straight copy of the renderbuffer.

## 5. Input/UI rendering notes

- All player UI is SDL/OpenGL-rendered quads (no JavaFX in-game); JavaFX is only
  the auto-updater and the in-game debugger.
- Menus: `Menus.java`, `McqBuilder`, `Renderable58`/`RenderableMetrics14`,
  `MenuEntries`; battle HUD: `BattleHud.java` (2,186 lines) with `BattleUiParts`
  (preloaded UI TMDs).
- Text: `Font`/`FontManager` (JSON+PNG fonts in `gfx/fonts`), `Text.java`
  (textbox geometry, `textZ_800bdf00`), I18n via `lang/` overrides.

## 6. Migration Assessment → SDL3 GPU

| Area | Action |
|---|---|
| RenderApi/GlApi/GlesApi | **Replace** with SDL3 GPU (device, command buffers, pipelines). No parallel RHI. |
| RenderEngine frame structure | **Port** (batches, pools, 4-pass order, ping-pong targets, post quad). |
| QueuedModel/Obj/Texture | **Redesign** with RAII + GPU-fence lifetimes; keep the queue API shape. |
| UBO layout (transforms2/lighting 128-blocks, clutAnimation) | **Port** as `std140`-layout GPU buffers (SDL3 GPU supports std140-compatible layouts). |
| PS1 GS (Gpu.java) software rasterizer | **Port as a fidelity mode** (optional build/runtime flag). Genuinely reusable PS1 tech. |
| GTE (Gte.java) | **Port** (small, well-defined; also reusable for Legaia). |
| Gs* helpers in Graphics.java | **Port** into a `ps1::gs_state` module shared by both render paths. |
| tex15 R32UI texture layout | **Preserve** — SDL3 GPU supports R32_UINT textures; keeps CLUT decode in the shader. |
| Geometry shader (near-plane cull + avg depth) | **Replace** — see `shader-migration.md` §3. |
| Post shader | **Port** as-is (no GS, pure FS work; manual bilinear is fine). |
| Frame-skip / 30Hz pacing | **Port** into the timing system (sim ticks decoupled from render Hz). |
| Free-cam/debugger (JavaFX) | Reimplement free-cam in engine; debugger moves to Qt 6 tooling. |

### Risks

1. **Per-model UBO blocks of 128** (80B + 128B stride) must be re-uploaded per
   128-model window — fine on all SDL3 GPU backends, but watch dynamic buffer
   update limits (D3D12 mapped subresources are cheap; Vulkan buffer copies).
2. **Two-pass translucency + shader scissoring** are subtle; regressions show up
   as specific scenes (Shirley transformation fight is the known scissor case,
   commented in `standard.fsh`). Keep an A/B screenshot test suite.
3. The `usePs1Depth` average-depth behavior affects battle draw order correctness;
   must be bit-exact or battles render with wrong overlaps.
4. Intel iGPU scissor workaround is in-shader — portable, keep it.
5. R32UI texture filtering restrictions: none needed (nearest), so all backends OK.

## Open Questions

- Exact behavior difference between `legacyMode` 1 and 2 (both appear to route
  through the software GS; 2 may additionally skip the modern pipeline entirely —
  needs targeted verification if we ship fidelity modes).
- `EngineState.RenderMode` (LEGACY/PERSPECTIVE/VRAM) per engine state — which
  states pick which (submap PERSPECTIVE vs LEGACY verified indirectly via
  `renderMode = currentEngineState.getRenderMode()`; full table TBD).
- Whether any content relies on the `Gpu` display-buffer ping-pong
  (double-buffered VRAM presentation) for specific effects.