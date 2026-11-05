# M4-01 — GPU, VRAM, and Ordering Tables

Status: Complete (M4, 2026-11-04). Reference: Severed-Chains @ `0c08c679b` (primary), read-only. This document refines the R1 boundary pass (`research/psx/01-compatibility-boundaries.md` §2.1) at the constant and mechanism level. Companion documents: M4-02 (GTE — the screen-coordinate producer), M4-05 (classification).

Evidence vocabulary: **source-verified** (established from pinned reference source), **format-corroborated** (independent spec/implementation/test), **experimentally verified** (reproduced through a real test), **asset-validated** (confirmed against real game bytes — *none exists; no such claim is made*), **Inferred** (reasoned, not conclusively established), **Unresolved** (insufficient or conflicting evidence). Layers per `research/psx/01-compatibility-boundaries.md`: (1) PSX platform, (3) LoD-specific, (4) reference-specific.

Scope note: "GPU" here means `legend.core.gpu` (the PSX command-stream model) **plus** the libgs layer decompiled from ROM in `legend/game/Graphics.java`. The modern GL pipeline (`legend.core.renderer`) is documented only where it is load-bearing for the GPU model's behavior (Q-37: per-object routing between the two is M5).

## 1. VRAM model

### 1.1 Dimensions and representation (source-verified)

- VRAM is **1024 × 512** pixels — `Gpu.STANDARD_VRAM_WIDTH/HEIGHT = 1024/512` (`core/gpu/Gpu.java:36-39`), exposed as `vramWidth`/`vramHeight`. This is the libgpu/libgs texture-space convention used throughout the reference; the correspondence to the physical PSX VRAM size is an external cross-check item (Q-65c; no public documentation was consulted in this audit).
- **Two parallel pixel stores** (`Gpu.java:46-47`):
  - `vram24` — `int[]` of 1024×512, one packed 24-bit (0x00RRGGBB layout; bit 24 = mask/alpha bit) pixel per texel. This is the store the rasterizer reads/writes (`getPixel`, `setVramPixel`), and the source of CLUT lookups.
  - `vram15` — `int[]` of the same size, one **packed 15-bit** pixel per texel. This is the store uploaded to CPU memory by `uploadData15`, read back by `downloadData15`, and uploaded to the GL texture (`vramTexture15`) for the modern renderer and the "VRAM display" legacy mode 2.
  - `setVramPixel(x, y, pixel24, pixel15)` writes both atomically under `vramLock`; there is no path that updates one without the other (source-verified — every write site passes both). The 15-bit value is the canonical 15.0 color plus, for copied/drawn pixels, the mask bit in bit 15.
- GL exposure (`Gpu.init/initVram`, `Gpu.java:85-114`): `vramTexture15` is a persistent 1024×512 texture in `R_32_UINT`/`RED_INT` (R32 packed 15-bit), uploaded from `vram15` **only when dirty** (`updateVramTexture`, `Gpu.java:116-123`); `vramTexture24` is a persistent 1024×512 `RGBA_8` texture, re-uploaded **every frame** in legacy mode 2 (`drawVram`, `Gpu.java:438-449`).
- All pixel operations are **per-pixel Java loops under `synchronized(vramLock)`** — there is no hardware BLT acceleration, no row-word operations, and no overlap handling beyond the obvious. Layer-4 implementation of a layer-1 data structure.

### 1.2 VRAM as the texture path in all render modes (source-verified)

Textures never bypass VRAM:

- Game code uploads images via `Gpu.uploadData15` (see §7); the canonical helper is `Tim.uploadToGpu()` (`game/tim/Tim.java:72-78`): upload image rect, then CLUT rect if the TIM has a CLUT.
- In legacy mode 1 (GPU-rasterized) the rasterizer fetches texels directly from `vram15`/`vram24` via `Gpu.getTexel` (§4.3).
- In mode 0 (modern renderer, the release default) objects that do not carry a dedicated renderer texture bind `GPU.useVramTexture()` (unit 1, `core/renderer/QueuedModel.java:190-200`) — the shader samples the 15-bit R32 VRAM texture; CLUT selection is expressed through shader uniforms (`tpageOverride`/`clutOverride`/`clutAnimation`, `RenderEngine.java:159-193`). **The in-shader pixel→CLUT fetch details are M5 (Q-37)**; what is established here is that *the VRAM arrays are a live data path in mode 0 as well*, not a legacy-mode artifact.
- Consequence for reimplementation: any faithful port must keep a shared VRAM store with 15-bit canonical pixels + CLUTs, regardless of which rasterizer is used (see M4-05).

### 1.3 Observed VRAM layout conventions (source-verified, layer-3)

LoD treats specific VRAM regions as named resources (coordinates are VRAM pixel coordinates, origin top-left):

| Region | Use | Evidence |
|---|---|---|
| (0, 0)–(1024, 512) general | scene textures, model pages | TMD `GetTPage` bases seen at x ∈ {0..1024 step 64}, y ∈ {0, 256} |
| (832, 422) 64×2 | system font CLUT row | `Scus94491BpeSegment.loadSystemFont` (`@Method(0x800e6524)`): CLUT rect (832, 422, 32, 1), image rect (832, 424, 64, 56) |
| (832, 256) 64×240, (832, 480) 16×4 | battle UI / shadow tiles | `rectArray28_80010770` table (`Scus94491BpeSegment.java:32-38`) |
| (896, 256) 16×46, (832, 484) 16×1, (912, 256) 28×14, (1008, 484) 16×1 | battle HUD extras | same table |
| (960, 256) | frame-store backing store for battle texture-scroll animations | `Battle.applyTextureAnimation` (see §8.2) |
| (0..256, 0..512), (64..256, 496) rows | menu/2D tile strips | same table (first 16 entries) |

The `rectArray28_80010770` table alternates image rect and CLUT rect pairs — it is LoD's own VRAM allocation map (a decompiled constant, so the coordinates are **source-verified facts about the reference's interpretation of the original layout**, layer-3).

## 2. Bpp encodings

### 2.1 Parameter table (source-verified, `core/gpu/Bpp.java`)

| Enum | PSX bpp | `widthDivisor` | `widthMask` | `indexShift` | `indexMask` |
|---|---|---|---|---|---|
| `BITS_4` | 4 | 4 | 0x3 | 2 | 0xf |
| `BITS_8` | 8 | 2 | 0x1 | 3 | 0xff |
| `BITS_15` | 15 | 1 | 0 | 0 | 0 |
| `BITS_24` | 24 | 1 | 0 | 0 | 0 |

`Bpp.of(int)` indexes by ordinal (0..3 = 4/8/15/24-bit) — the ordinal *is* the PSX TPage bpp field. Texture-fetch index computation (`Gpu.getTexel`, `Gpu.java:625-628`):

```
texelIndex = vram15[y / widthDivisor + textureBaseY][x + textureBaseX]
            >> ((x & widthMask) << indexShift) & indexMask
color      = vram24[clutX + texelIndex, clutY]
```

i.e. for 4-bit: one 16-bit VRAM word holds 4 texels (2 per byte), texel *x mod 4* selects a 4-bit field; for 8-bit: one word holds 2 texels (1 per byte). **Note:** the reference decodes 4/8-bit texels from the **15-bit store** (`getPixel15`) — a 4-bit texel packed as a 15-bit value. This is faithful to the PSX hardware's actual organization (4/8-bit VRAM is physically the low bits of the 16-bit words) and is the reason `uploadData15` must convert every pixel to 24-bit for the rasterizer's color store (source-verified; layer-1 `Inferred` — the physical bit-packing matches documented PSX VRAM organization, external cross-check pending §9).

### 2.2 TPage / CLUT encoding (source-verified, ROM-anchored libgs)

`Graphics.GetTPage(bpp, trans, x, y)` (`@Method(0x8003b3f0)`, `Graphics.java:192-198`):

```
TPage = (bpp.ordinal() << 7 | trans.ordinal() << 5
        | (y & 0x200) << 2 | (y & 0x100) >> 4 | (x & 0x3ff) >> 6) & 0xffff
```

| Bits | Field | Meaning |
|---|---|---|
| 9:7 | bpp | 0=4-bit, 1=8-bit, 2=15-bit, 3=24-bit (Bpp ordinal) |
| 6:5 | translucency rate | `Translucency` ordinal, 1:1 from the TPage field: 0 = `HALF_B_PLUS_HALF_F`, 1 = `B_PLUS_F`, 2 = `B_MINUS_F`, 3 = `B_PLUS_QUARTER_F` (see §5.4 caveat on rate 0) |
| 11 | (y & 0x200) | page row above row 0/256 (bit 9 of y) |
| 4 | (y & 0x100) | page row: 0 → y=0, 1 → y=256 (bit 8 of y) |
| 3:0 | (x & 0x3ff) >> 6 | page column x/64, 4 bits used in practice (1024/64 = 16 pages) |

`Graphics.GetClut(x, y)` (`@Method(0x8003b430)`, `Graphics.java:212-214`): `CLUT = (y << 6 | (x >> 4) & 0x3f) & 0xffff` — x a multiple of 16 (6-bit column of 16-wide CLUT pages), y a multiple of 1 (10-bit row). CLUTs live **in VRAM** and are sampled at `(clutX + index, clutY)` from the 24-bit store.

The decode side of the TMD legacy path (`tmd/Renderer.java:142-150`) reads the same fields back: `bpp = Bpp.of(tpage >>> 7 & 0b11)`, `trans = Translucency.of(tpage >>> 5 & 0b11)`, texture base = `((tpage & 0b1111) * 64, (tpage & 0b10000) != 0 ? 256 : 0)`, CLUT = `GetClut(clut & 0b111111, clut >>> 6)` → `(clutX * 16, clutY)`. The encode/decode pair is exact (source-verified); the TMD path uses only the low bits that 1024×512 VRAM actually needs (x/64 needs 4 bits; y needs 1 bit — the higher page bits in `GetTPage` exist to mirror the full PSX TPage register, `Inferred`).

## 3. Draw area, screen offsets, display mode

### 3.1 State (source-verified)

- `Gpu.drawingArea` / `scaledDrawingArea` (`Rect4i`, set identically by `Gpu.drawingArea(l, t, w, h)`); `offsetX`/`offsetY` (`short`).
- `Gpu.status` mirrors PSX draw-attribute state: `setMaskBit` ("bit 11 — set mask bit when drawing pixels"), `drawPixels` (`ALWAYS` / `NOT_TO_MASKED_AREAS`, "bit 12 — draw pixels"), plus `horizontalResolution`/`verticalResolution` (default 320×240).

### 3.2 The libgs interface (source-verified, ROM-anchored)

- `Graphics.setDrawOffset()` (`@Method(0x8003c5e0)`): `centreScreenX/Y = displayWidth/2, displayHeight/2` then `GsSetDrawBuffOffset()` → `GPU.drawingOffset(centreScreenX, centreScreenY)`. This is the **center-origin convention**: ROM code supplies screen coordinates relative to the screen center; the GPU model adds the offset at render time (§4.1).
- `Graphics.GsSetDrawBuffClip()` (`@Method(0x8003c2d0)`) → `GPU.drawingArea(displayRect_800c34c8 …)`. `displayRect_800c34c8` is set to (0, 0, displayWidth, displayHeight) by `initDisplay` (`@Method(0x8003be28)`) and by game code for partial clips.
- `Graphics.GsInitGraph(w, h)` (`@Method(0x8003bc30)`) = `GsInitGraph2` (`ResetGraph` + `drawingOffset(0,0)` + `displayMode`) + `GsInit3D` (GTE screen offset 0,0) + `initDisplay` + clip + offset. `syncFrame_reinit` (`@Method(0x80012f24)`) is the one-frame reinitializer used when the resolution changes: it re-derives the ordering-table state (§5.1), resets clear color to 0, and restores the normal `syncFrame`/`swapDisplayBuffer` callbacks.
- **Resolution census (call sites, source-verified):** 320×240 (battle `Battle.java:2038`, preload, `WMap.java:634`, `PostBattleScreen.java:103`), 384×240 (title `Ttle.java:277`, menus `Menus.java:70`, `SMap.java:4092`, `PostBattleScreen.java:501`), 640×240 (game-over `GameOver.java:99`). `preload()` (`@Method(0x800e6184)`) sets 320×240 + `vsyncMode = 2`.
- **The 384 → 368 clamp (source-verified):** both `Gpu.displayMode` and `Gpu.drawingArea` rewrite `width == 384` to `368` before storing. Effect: all "wide" contexts (title, menus, submap widescreen, post-battle) actually render 368 px wide. This is a reference-side correction of a PSX-era width quirk — **layer-4 behavior with layer-1 intent (`Inferred`: the PSX GPU accepts 384 but the effective visible width on NTSC 240-line is ~368 usable pixels; the reference pins this explicitly). Whether the original LoD code relied on the same clamp is Unresolved** (register Q-65).

### 3.3 Display mode and buffer model (source-verified)

- `Gpu.displayMode(w, h)` (GP1(08h) "Display Mode" per the in-source comment) sets `RENDERER.setProjectionSize`, the status resolution, and (on the GL thread) recreates the two `VramTextureSingle` **draw/display buffers** sized `w×h` and the window `displayTexture`. Off-GL-thread calls defer via `displayChanged` (applied at the next `tick`).
- **Double buffer:** `renderBuffers[2]` (24-bit, `Bpp.BITS_24`), `drawBufferIndex` flips at the end of every `Gpu.tick()`; `getDisplayBuffer() = renderBuffers[drawBufferIndex ^ 1]`, `getDrawBuffer() = renderBuffers[drawBufferIndex]`. The flip is unconditional; `Graphics.GsSwapDispBuff()` (`@Method(0x8003c350)`) only bumps the frame counter `PSDCNT_800c34d0` (wrapping to 1) — the *libgs* side of the swap. The `PSDCNT` frame counter is the invalidation token for the `GsCOORDINATE2` matrix cache (`GsGetLw/GsGetLs/GsGetLws` compare `flg == PSDCNT`, `Graphics.java:493-708`).
- `Graphics.displaySize()` (the buffer-creation helper) also feeds `updateDisplayTexture` (4:3 letterboxed window mesh).

## 4. Ordering table

### 4.1 Constants — plan values verified (source-verified)

`Scus94491BpeSegment.gameInit()` (`Scus94491BpeSegment.java:605-628`, called from `Scus94491BpeSegment.java:600`):

```
ResetGraph();
orderingTableBits_1f8003c0 = 14;
zShift_1f8003c4           = 0;
orderingTableSize_1f8003c8 = 0x4000;     // 16384
zMax_1f8003cc              = 0x3ffe;     // 16382
GPU.updateOrderingTableSize(orderingTableSize_1f8003c8);
…
InitGeom();                 // projection plane 1000
setProjectionPlaneDistance(640);   // LoD default projection plane
vsyncMode_8007a3b8 = 2;
```

The plan's expected constants — **VRAM 1024×512, ordering table size 0x4000, zMax 0x3ffe, bits 14, zShift 0** — are all confirmed exactly (source-verified). The same values are re-derived on every graphics re-init by `Graphics.syncFrame_reinit()` (`@Method(0x80012f24)`) from `reinitOrderingTableBits_8004dd38` (default 14): `zShift = 14 − bits`, `size = 1 << bits`, `zMax = (1 << bits) − 2` — i.e. `zMax` is *always* two less than the size for the standard bits=14 configuration, and `setDepthResolution(bits)` (`@Method(0x8001324c)`) re-initializes the table whenever the requested depth resolution changes.

Note the off-by-two: **`zMax_1f8003cc = 0x3ffe` is not the top queue index in use.** `GsSortClear` and `Graphics.endFrame` queue at `orderingTableSize − 1 = 0x3fff` (the last slot of the 0x4000-slot array); `zMax = 0x3ffe` is used as the **clamp for transformed polygon z** (`tmd/Renderer.java:169`: `z = min((screenZ + zOffset) >> zShift, zMax)`). Both the PSX OT's `zMax` register and LoD's usage agree that 0x3ffe is the max *depth value*, while slot 0x3fff is the front-of-table clear/mask slot (source-verified; the split between "max depth" and "top slot" is `Inferred` layer-1 attribution).

### 4.2 Allocation, queueing, traversal (source-verified)

- `Gpu.updateOrderingTableSize(size)` allocates `zQueues = new ArrayList[size]` (one list per depth slot). Reallocation happens lazily in `Gpu.startFrame()` when `zMax` (the field, confusingly the size here) differs from the configured `orderingTableSize_1f8003c8`.
- `Gpu.queueCommand(z, cmd)` / `queueCommand(float z, cmd)` — the float overload **rounds** z (`Math.round`) before indexing. No bounds check: an out-of-range z is a `ArrayIndexOutOfBoundsException` in the reference (source-verified; the game clamps via `zMax`/`zMin` before queueing — §8.3).
- **Traversal (`Gpu.tick()`, runs at `GPU.endFrame()`, i.e. the end of the game loop):**

```
for (z = zQueues.length − 1; z >= 0; z--)
  for (i = queue[z].size() − 1; i >= 0; i--)
    queue[z].get(i).render(gpu);
  queue[z].clear();
drawBufferIndex ^= 1;
```

  - **Back-to-front across slots** (highest depth slot first) — matches the PSX OT scan direction (the GPU draws from the back of the OT toward the front; `Inferred` layer-1 attribution).
  - **Reverse insertion order within a slot (LIFO).** The PSX GPU executes primitives within one OT slot in **FIFO** order (queue order; `Inferred` layer-1 — no external documentation was consulted in this audit, network access is restricted). The reference therefore draws same-depth primitives in the **opposite** order of the original hardware: an earlier-queued same-z primitive is drawn last (on top) in the reference, earlier (underneath) on PSX. Whether any LoD scene exhibits visible same-z overlap is **Unresolved** (Q-66). This is the single clearest GPU-model divergence found in M4-01.
  - All queues are cleared after each frame; `Graphics.ResetGraph()` (`@Method(0x80038190)`, "GP1(01h) Reset Command Buffer") clears them mid-frame.

### 4.3 z insertion from the GTE (source-verified; GTE internals are M4-02)

The TMD legacy path (`tmd/Renderer.java:166-172`) computes per-primitive depth:

```java
screenZ = quad ? GTE.averageZ4() : GTE.averageZ3();
z       = Math.min((screenZ + zOffset_1f8003e8) / (1 << zShift_1f8003c4), zMax_1f8003cc);
if (z < zMin) skip;
```

`zOffset_1f8003e8` (default 0) and `zMin` (default 0) are layered-drawing knobs the game manipulates temporarily — e.g. `SEffe.java:351-358` saves/sets `zMin = 0xb` and restores it, and `TmdParticle.java:133,148` zero `zOffset_1f8003e8` and fence `zMin` (the classic libgs layered-background trick). `Graphics.RotTransPers4` (`@Method(0x8003f9c0)`) returns `GTE.getScreenZ(3) / 4.0f` — the libgs convention of returning a quarter of the screen Z for OT insertion (see `RotTransPers4` javadoc; also `Graphics.perspectiveTransform`).

### 4.4 Frame flow — where the ordering table executes (source-verified)

`Scus94491BpeSegment.gameLoop()` (`@Method(0x80011e1c)`, `Scus94491BpeSegment.java:141-197`) phase order:

1. `endFrame()` (libgs: queue two mask-bit commands — `SetMaskBit(false, ALWAYS)` at z=3 and `SetMaskBit(true, ALWAYS)` at z=size−1 — then `syncFrame_8004dd3c.run()` and `swapDisplayBuffer_8004dd40.run()` = `GsSwapDispBuff` + clear-color setup).
2. `GPU.startFrame()` (upload VRAM GL texture if dirty; re-allocate OT if size changed).
3. timestamp advance (`timestamp_a0 += vsyncMode`), fps/input tick rate (`60 / vsyncMode × speed`).
4. `loadQueuedOverlay`, `renderUi`, `state.tick()`, `RENDER_EVENT` (modern render pass), menus, `uploadRenderables`, `SCRIPTS.tick()`, battle transition, full-screen effects, `startQueuedSounds()` (SPU), textboxes, `overlayTick`, `FUN_80020ed8`, `tickCount_800bb0fc++`.
5. `GPU.endFrame()` → `Gpu.tick()`: (a) legacy mode 1 — blit the *currently displayed* buffer to the window `displayTexture` and draw it; legacy mode 2 — draw full VRAM; (b) render the z-queues back-to-front into the *draw* buffer and clear them; (c) flip `drawBufferIndex`.

Consequences (source-verified + Inferred layer-1):

- **Commands queued during frame N are executed once, at the end of frame N**, before the buffer flip; the result becomes the *display* buffer for frame N+1.
- **The window shows frame N−1's GPU output while frame N is composed** (legacy mode 1 path). This one-frame latency mirrors the PSX vblank model (the display shows what was rendered during the previous vsync; `Inferred` layer-1). In mode 0 the modern renderer presents its own buffers outside `Gpu.tick`; the Gpu model still executes (mask commands, battle scroll) but its draw buffers are not the presentation path (Q-37, M5).
- The `endFrame()` mask-bit pair means: within each frame, everything at z ≥ 4 drawn while `setMaskBit = true` sets the VRAM mask bit; everything at z ≤ 3 does not. Practical effect: scene primitives (z in [0, 0x3ffe]) set the mask; the battle scroll (z=1, §8.2) and the z=3 boundary do not.

## 5. Commands and rasterization

### 5.1 Command set (source-verified)

| Command class | PSX counterpart | Behavior in the reference |
|---|---|---|
| `GpuCommandPoly` | GP0 polygons (FLAT/GOURAUD/TEXTURE/LIT/ST lines/quads) | barycentric software rasterizer, §5.2 |
| `GpuCommandFillVram` | GP0 "clear" (screen clear) | `Gpu.clear(colour)` — fills the **draw buffer** (24-bit) with a solid color |
| `GpuCommandCopyVramToVram` | GP0 VRAM→VRAM copy | per-pixel 15-bit copy with mask honoring, §5.3 |
| `GpuCommandSetMaskBit` | draw-attribute bits 11/12 | sets `Gpu.status.setMaskBit` / `drawPixels` for all subsequently rendered commands in this frame |

There is no GPU read-back command, no palette (CLUT) upload command (CLUTs arrive via `uploadData15` like any image), and no hardware fog — the PSX command vocabulary is partially modeled (source-verified).

### 5.2 Polygon rasterization (`Gpu.rasterizeTriangle`, source-verified)

- **Input:** 3 or 4 vertices (quads split into (v1,v2,v3) + (v1,v2,v3,v4) triangles at render time, `GpuCommandPoly.render`). Vertices are **int** screen coordinates; `GpuCommandPoly.pos(vertex, float, float)` rounds floats.
- **Offset application at render time:** `render()` adds `gpu.getOffsetX()/getOffsetY()` (the center-origin offset, §3.2) to every vertex *when the command executes* — so queued commands can be enqueued with center-relative coordinates and the offset in force at draw time applies (this is what lets `drawBoxagonEffect`, §8.1, work).
- **Winding:** `orient2d` (cross product; ~2× signed area); zero-area primitives are dropped; counter-clockwise input is re-oriented to clockwise by swapping v1/v2 (and matching u/v, color) — so texture/normal data stay vertex-aligned (source-verified; PSX also culls on winding, `Inferred`).
- **Clipping:** bounding box clipped to `scaledDrawingArea` (which in this revision is set identically to `drawingArea` — `Gpu.java:402-409`; the "scaled" copy exists for the widescreen scale path but nothing rescales it in this revision — source-verified, `Inferred` that the scaling is renderer-side in mode 0).
- **Interior test:** barycentric (`w0,w1,w2`) with the **top-left rule** bias (`isTopLeft` → bias 0 else −1; test `(w0+b0 | w1+b1 | w2+b2) >= 0`) — the PSX edge-flag pixel selection rule (source-verified; hardware-faithful pixel selection, `Inferred` layer-1 attribution).
- **Mask check:** if `drawPixels == NOT_TO_MASKED_AREAS` and the destination pixel's bit 24 (alpha/mask) is set, the pixel is skipped.
- **Shading (Gouraud):** `getShadedColor` — per-channel integer barycentric interpolation of the three vertex colors, divided by area (integer division — truncation). Note the 24-bit colors here are *already* the final colors; PSX Gouraud shading is done per-vertex with the lighting stage at submission, so the reference's vertex colors are pre-lit (§8.3).
- **Texturing:** `getTexel` (§2.1) fetches the 15-bit index, then the CLUT pixel from `vram24`. A fetched texel of 0 is treated as "no pixel" (skipped) — a mask-color convention for transparent 4/8-bit texels (source-verified; `Inferred` this matches PSX behavior where a 4-bit index of 0 with the mask bit unset is discarded… actually the PSX discards based on the *pixel* alpha bit, so a 0-index mapping to an alpha-0 CLUT entry is the game-side convention — `Inferred`). If `!raw`, the texel is blended with the Gouraud color via `applyBlending` (§5.4).
- **Translucency:** `handleTranslucence(x, y, colour, mode)` (§5.4) applied when `isTranslucent && (!isTextured || (colour & 0xff000000) != 0)` — i.e. for textured translucents the **front pixel's alpha bit** gates the blend (the PSX "translucent only where the drawn pixel's alpha is set" rule for textured primitives; source-verified, `Inferred` layer-1).
- **Mask-bit write:** `colour |= (status.setMaskBit ? 1 : 0) << 24` before storing — the draw-attribute bit 11 behavior.
- **Overflow/edge cases:** coordinates are `int` throughout (screen-space ints from the GTE, §M4-02); there is no clipping against the *physical* screen — clipping is only against `scaledDrawingArea`; negative/large coordinates are clipped by the bounding-box clamp (source-verified). `GpuCommandPoly.pos` has a (commented-out) "BAD COORD" guard for |x−x0| ≥ 3000 / |y−y1| ≥ 1500 — evidence the author observed out-of-range vertices in practice but did not act on them (source-verified).

### 5.3 VRAM→VRAM copy (`Gpu.copyVramToVram`, source-verified)

Per-pixel: read (src) 15+24-bit, and unless `drawPixels == NOT_TO_MASKED_AREAS` and the *destination* mask bit is set, write dest = src with the current `setMaskBit` applied to bit 15/24. Row-major, no tiling, no overlap protection (a downward overlap would be corrupted — the game's scroll animations, §8.2, are structured so the backing store at (960,256) avoids in-place overlap). Constructor rejects negative coordinates with `IllegalArgumentException` (source-verified).

### 5.4 Blending equations (source-verified, `Gpu.applyBlending`, `Gpu.handleTranslucence`)

- **Texture×color (opaque textured):** `out[c] = min(0xff, (color[c] × texel[c]) >>> 7)` — an 8.7 fixed-point multiply (the PSX texture-shade equation `T×C/128`; source-verified, `Inferred` layer-1 match). Alpha of the texel is OR'd (`texel & 0xff000000` preserved).
- **Translucency modes** (computed in 8-bit against the draw buffer):

| Mode | Equation | PSX counterpart |
|---|---|---|
| `HALF_B_PLUS_HALF_F` | `(B + F) / 2` | "B+F/2" (integer division — truncation) |
| `B_PLUS_F` | `min(0xff, B + F)` | "B+F" |
| `B_MINUS_F` | `max(0, B − F)` | "B−F" |
| `B_PLUS_QUARTER_F` | `min(0xff, B + F / 4)` | "B+F/4" (note: **F/4 first**, truncating, then add — operator order matters) |

  The output's alpha bit is the *front* pixel's alpha bit (`tp << 24`). The `Translucency` enum's ordinal is the 2-bit TPage field, mapped 1:1 (`Translucency.of(tpage >>> 5 & 0b11)`, `Translucency.of(-1)` → null; enum order: 0 `HALF_B_PLUS_HALF_F`, 1 `B_PLUS_F`, 2 `B_MINUS_F`, 3 `B_PLUS_QUARTER_F`) — so in the reference a translucent TMD primitive with TPage rate bits 00 blends with `B+F/2` (source-verified). Whether the PSX hardware treats TPage rate 00 on a translucent GP0 as "no blending" (the common documentation) or as B+F/2 is **Unresolved** (no external PSX documentation was consulted in this audit; register Q-65 covers the external cross-check). The R1 audit F-13/F-18 recorded renderer-side (mode 0) translucency pass quirks — those are a separate layer (Q-37, M5).

### 5.5 Immediate (non-OT) operations

`Gpu.clear` (draw-buffer fill), `uploadData15`/`downloadData15` (CPU↔VRAM), `copyVramToVram` are all **executed when queued** through the OT (they are commands, not immediate API calls), except the uploads/downloads which are immediate Cpu-side calls. `displayMode`/`drawingArea`/`drawingOffset` are immediate state writes (source-verified). The PSX distinction between GP1 (immediate) and GP0 (queued) is preserved in spirit (in-source GP1 comments on `resetCommandBuffer`/`displayMode`) but the reference queues *even clears and copies* (source-verified; this is a structural simplification, `Inferred` it is behaviorally safe because the OT executes in order at frame end).

## 6. Upload / download paths

### 6.1 CPU→VRAM (`Gpu.uploadData15`, source-verified)

- Two overloads: `FileData` (2-byte little-endian 15-bit words) and `int[]`.
- Both assert the rect fits in VRAM (right/bottom overflow → assertion failure in dev builds; in production `assert` is a no-op and the loop would `ArrayIndexOutOfBoundsException` — `Inferred` risk note).
- **Truncation behavior (FileData):** the inner loop `break`s when `i + 1 >= data.size()` — "sometimes the rect is larger than the data (see: the DEFF stuff where animations are loaded into VRAM for some reason)" (in-source comment, source-verified). Partial rects silently leave the rest untouched.
- Every pixel is written to **both** stores: `vram15 = packed 15-bit`, `vram24 = colour15To24(packed)` (bit 24 clear). `vramDirty = true` → the GL texture is refreshed at the next `Gpu.startFrame()`.
- `Tim.uploadToGpu()` (`Tim.java:72-78`) is the canonical caller: image rect from the TIM header, then CLUT rect.

### 6.2 VRAM→CPU (`Gpu.downloadData15`, source-verified)

Reads `vram15` into `FileData` (2-byte LE) or `int[]`; asserts rect bounds; no dirty interaction. In-game users: title-screen fire animations (`Ttle.java:1549-1550`), world-map stored effects re-upload (`WMap.java:672-673`), the game-over "The End" CLUT (`RetailSubmap.java:742`).

### 6.3 CLUT animation support (source-verified, partial)

`VramTextureSingle.applyPalette`/`createOpenglTexture` apply a 15-bit CLUT to a region on the CPU side (used by `Ttle.java:80-81` and the save-card texture extractor); the *shader-side* CLUT animation uniforms (`clutAnimation`, `RenderEngine.java:117-124`, two 1024-entry sets) implement runtime CLUT scrolling without VRAM re-upload — the mechanism is M5 (Q-37).

## 7. Worked examples

### 7.1 `FullScreenEffects.drawBoxagonEffect` (`@Method(0x80013d78)`, `FullScreenEffects.java:167-195`) — 2D GPU primitive, center-origin, z=30

A fade transition draws a rotating boxagon tile:

```java
final int theta = angle * 0x400 / 123;
sin = rsin(theta) * 20 / 0x1000;
cos = rcos(theta) / 17;
final int x0 = x * 40;
final int y0 = y * 40;

GPU.queueCommand(30, new GpuCommandPoly(4)
  .rgb(cos, cos, cos)
  .pos(0, x0 - (sin - 20) - centreScreenX_1f8003dc, y0 - (sin - 20) - centreScreenY_1f8003de)
  .pos(1, x0 - (centreScreenX_1f8003dc - 40), y0 - centreScreenY_1f8003de)
  .pos(2, x0 - centreScreenX_1f8003dc, y0 - (centreScreenY_1f8003dc - 40))
  .pos(3, x0 + sin + 20 - centreScreenX_1f8003dc, y0 + sin + 20 - centreScreenY_1f8003de)
);
```

Hardware-counterpart reading: the original issues a GP0 flat-color translucent quad whose vertices are **screen-center-relative** (the PSX double-buffer offset makes (0,0) the screen center in LoD's setup — §3.2). The reference models the same contract: the command is queued with center-relative ints, and `GpuCommandPoly.render()` adds `offsetX/Y` (= screen center) at execution time, so the on-screen position is identical *if* the offset at execution matches the offset assumed at queue time. **Divergence risk:** on the PSX the offset is fixed per draw packet by the GPU state; here it is whatever `Graphics.GsSetDrawBuffOffset` last set — if a frame changed the offset between queue and execute, the reference and the hardware would disagree (`Inferred`; no such mid-frame offset change was found in the inspected call sites — offset writes are at init/reinit/camera paths, M4-02 §camera). Also note the fixed-point trig: `rsin/rcos` (M4-05 fixed-point section) with the `0x400/123` angle remap — behavior-significant for the fade's look.

### 7.2 `Battle.applyTextureAnimation` (`@Method(0x800ead44)`, `Battle.java:7605-7609`) — frame-store scroll at z=1

```java
GPU.queueCommand(1, new GpuCommandCopyVramToVram(960, 256, rect.x, rect.y + rect.h - h, rect.w, h));
GPU.queueCommand(1, new GpuCommandCopyVramToVram(rect.x, rect.y + h, rect.x, rect.y, rect.w, rect.h - h));
GPU.queueCommand(1, new GpuCommandCopyVramToVram(rect.x, rect.y, 960, 256, rect.w, h));
```

This is a **circular vertical scroll** of a texture rect using the (960, 256) VRAM region as the backing store: (1) the bottom `h` rows move into the store area *behind the rect's top rows*; (2) the middle shifts up by `h`; (3) the top `h` rows move into the store… — re-reading the three copies in execution order (LIFO within z=1 reverses queue order!): **copy 3 executes first** (top rows → store), then copy 2 (middle up), then copy 1 (store bottom → rect bottom). The LIFO execution (§4.2) is therefore **load-bearing** for this animation: FIFO execution would run copy 1 before copy 3 and corrupt the scroll (the store would already be overwritten… actually in FIFO copy 1 reads the store *before* copy 3 writes it — the first frame would still work but subsequent frames' store content would be stale). This is a concrete, in-repo example where the reference's same-z LIFO ordering changes observable behavior, and where the original PSX (FIFO) would have needed the *reverse* queue order to achieve the same effect — i.e. either the decompilation preserved the original queue order and the reference's LIFO compensates, or the original hardware FIFO with this queue order scrolls differently. **Unresolved (Q-66)** — cannot be settled without the original's executed behavior; both orders are internally consistent with the code as decompiled.

### 7.3 `tmd/Renderer.renderTmdPrimitive` (`tmd/Renderer.java:46-208`) — TMD polygon → GTE → OT

The legacy-mode (mode 1) model pipeline, per primitive:

1. Decode TMD primitive header: primitive id (quad/gouraud/textured/translucent/lit bits), TPage/CLUT words from the first two vertex records.
2. Per vertex: `GTE.perspectiveTransform(worldVertex)`; if `GTE.hasError()` → **skip the whole primitive** (clip via GTE error flag — M4-02); else `pos(i, GTE.getScreenX(2), GTE.getScreenY(2))`.
3. Backface culling at vertex 2: `winding = GTE.normalClipping()`; skip if `!translucent && winding <= 0 || translucent && winding == 0` (translucents draw both faces except exact-degenerate).
4. Depth: `z = min((avgZ3/avgZ4 + zOffset_1f8003e8) >> zShift_1f8003c4, zMax_1f8003cc)`; skip if `z < zMin`.
5. Color: unlit → TMD vertex colors; lit → `GTE.normalColour(normal, colour)` (lighting stage — M4-02 §lighting); the CTMD/uniform-lit translucent special case re-scales vertex colors by the current battle light.
6. `GPU.queueCommand(z, cmd)`.

Hardware counterpart: this is the libgs `GsDrawObj`/`GsDrawOt` path — GTE transform → OT insertion at (sz+zo)>>zs clamped to zMax → back-to-front GPU execution. The reference executes the same insertion rule with float intermediate values (the `>>` on a float is a cast; `z` is float-rounded by `queueCommand(float)` — M4-02 details the fixed-point deltas).

## 8. Divergences, edge cases, and open items

1. **Same-z LIFO vs PSX FIFO** (§4.2, §7.2) — the most significant GPU-model divergence; behaviorally observable in at least one in-repo animation whose semantics depend on it. **Unresolved (Q-66).**
2. **Float z quantization:** `queueCommand(float)` rounds to the nearest int; the PSX OT index is an exact integer derived from truncated fixed-point Z (M4-02). Adjacent-deep primitives can land in a different slot in the reference. `Inferred` low-impact in practice (z resolution 16382 over the LoD scene depth), but it is a divergence of mechanism.
3. **The 384→368 clamp** (§3.2) — reference-pinned; original's effective width on NTSC Unresolved (Q-65).
4. **Software rasterizer vs hardware edge flags** — top-left rule, zero-area drop, clockwise re-orientation, barycentric interpolation with integer division: pixel-exactness vs the PSX rasterizer is *claimed in structure* (same edge rule, same pixel test) but **not verified pixel-by-pixel** (no asset/visual baseline; `Inferred`). Gouraud interpolation is done per-pixel in the reference from 3 pre-lit vertex colors; PSX Gouraud is per-pixel interpolation of *lighted* vertex colors — same observable result if vertex lighting matches (M4-02), so the *mechanism* difference is format-only (M4-05).
5. **No overlap handling in VRAM copy** — in-place overlapping copies are corrupted; the game works around it with the backing store (§7.2). A faithful port should preserve the per-pixel semantics; whether the PSX BLT hardware had scanline-ordering guarantees for overlaps is Unresolved (no external docs consulted).
6. **Assert-only bounds on uploads** (§6.1) — production builds have no range checks; the reference relies on the game never overflowing (source-verified).
7. **`scaledDrawingArea` is currently identical to `drawingArea`** (§5.2) — the widescreen scaling happens in the renderer, not the GPU model; M5 must confirm where the 368→render-width scale is applied for the GPU-rasterized content.
8. **`vramTexture24` re-upload every frame (mode 2)** vs `vramTexture15` dirty-upload — a performance/consistency detail, layer 4.
9. **Layered z usage is game-side convention:** z=1 (battle scroll, below everything), z=3 (mask-off boundary), z=30 (full-screen effects), z=0x3fff (clears/mask-on) — the *values* are LoD choices (layer 3); the mechanism (OT slot = draw order) is layer 1/2.
10. **PSDCNT wrap at overflow** (`GsSwapDispBuff`: `< 0 → 1`) — the 32-bit counter wraps every ~136 years of frames; the cache-invalidation semantics (`flg == PSDCNT`) are exact (source-verified).

## 9. Constant cross-check status (external evidence)

No public PSX documentation was fetched (network access is restricted for this milestone). The constants above are **source-verified** against the pinned reference. Where layer-1 attribution matters (1024×512 texel space, FIFO within OT slots, back-to-front OT scan, edge-flag top-left rule, 8.7 texture multiply, B±F blend equations, center-origin double-buffer offset), the attribution is marked `Inferred` and queued for an external cross-check (register Q-21 extension, Q-65, Q-66). The R1 boundary pass already flagged the "GPU model vs renderer" conflation risk (Q-37); M4-01 confirms it and adds the LIFO/FIFO item to the classification work for M4-05.

## 10. What M5 (rendering) must preserve or revisit

- The **VRAM store is the texture path in all modes** (§1.2) — M5's per-object audit (Q-37) must account for objects sampling `vramTexture15` (unit 1) and the CLUT uniforms.
- The **mode-1 GPU-rasterizer output is one frame behind the game state** (§4.4) — visual comparisons between modes must align by frame.
- **TMD legacy path** (mode 1 only): `renderTmdPrimitive` is the full GPU-model polygon consumer; M5 should compare its culling/clamp decisions against the mode-0 `queueModel` path.
- **Translucency pass quirks** (R1 audit F-13/F-18: `B_PLUS_QUARTER_F` pass setting `B_PLUS_F` state; mode-0 battle-color "gross hack") remain in the renderer layer and must be dispositioned by M5.
- The **battle scroll LIFO dependency** (§7.2) must be understood before any "fix" of the queue traversal order.

## 11. Register updates

- **Q-21** (ordering-table requirements): *advanced* — allocation, queueing, traversal, and the zMax/size−1 split are now documented with sources; remaining: same-z order policy (Q-66), hardware cross-check of the scan direction (external).
- **Q-65** (new): the 384→368 display/draw-width clamp — original PSX/LoD effective width behavior; reference-pinned, layer-1 intent Inferred.
- **Q-66** (new): same-z primitive order — reference LIFO vs PSX FIFO; in-repo animation (`Battle.applyTextureAnimation`) depends on the order; needs original-behavior evidence.
