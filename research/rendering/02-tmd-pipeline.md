# M5-02 — TMD Models, Geometry, and Animation

Status: Complete (M5, 2026-11-05). Reference: Severed-Chains @ `0c08c679b`, read-only. This document covers how TMD model files become GPU renderable objects (the `TmdObjLoader` pipeline), the GTE coordinate-system machinery that positions model parts, the animation flow (keyframes, interpolation, Cmb/Lmb), how parts are queued for both the modern model pools and the GPU-model command path (the M5-01 Q-37 per-object census), and the TMD shader families (`tmd.*`, `battle_tmd.*`) including the geometry-stage near-culling. Format-level facts (TMD/CTMD/TIM/animation container bytes) are M2's territory (`research/assets/05`, `07`) and are cited, not re-derived. Companion documents: M5-01 (render engine), M5-03 (UI/text), M5-04 (post-processing/widescreen/FMV).

Evidence vocabulary: **source-verified** (established from pinned reference source), **experimentally verified**, **format-corroborated**, **asset-validated** (*none exists — no such claim is made*), **Inferred**, **Unresolved**. Layers per `research/psx/01-compatibility-boundaries.md`. No public documentation was consulted (network-restricted); PSX hardware attributions are `Inferred`/`Unresolved` and travel with the M4 register items.

## 1. From TMD file to renderable `Obj`

### 1.1 File structure (source-verified)

- **`Tmd`** (`legend/game/tmd/Tmd.java`): 8-byte header + `nobj` × 0x1c-byte object tables. `TmdHeader` (`TmdHeader.java`): `flags` (u32) + `nobj` (i32). **Flags bit 0 (`fixp`) throws "fixp not supported"; bit 1 (`CTMD`) throws "Found CTMD"** — a CTMD must be transformed to plain TMD by `CtmdTransformer` *before* parsing (M2-05: the CTMD→TMD load-time rewrite). There is no in-loader CTMD support: the two paths are strictly separated at parse time.
- **`TmdWithId`** (`TmdWithId.java`): TMD preceded by a 4-byte `id`. The id's upper 16 bits are documented (javadoc, source-verified but unused) as per-TMD translucency info for battle ("some TMDs have translucency info in the upper 16 bits of their ID"); `optimisePacketsIfNecessary` (0x800de76c, decompiled) wraps the obj table in a fresh `ModelPart10` for plain TMDs and passes through for CTMDs. The consumer of the id's upper bits is not implemented in the reference (the "specialTrans" path is a commented-out TODO in `Renderer.java`) — `Unresolved`, layer 3.
- **`TmdObjTable1c`** (`TmdObjTable1c.java`, 0x1c bytes): `vert_top_00` (n `n_vert_04`) read via **`readSvec3_0` — raw s16 triples, no scaling** (FileData.java:646-649); `normal_top_08` (n `n_normal_0c`) via **`readSvec3_12` — s16 ÷ 4096** (12.4 fixed-point, FileData.java:657-660); `primitives_10` (n `n_primitive_14`); `scale_18`. Primitive storage: a header word (low 16 bits = vertex-packet count), then `count` packets of `CtmdTransformer.primitivePacketSize(header)` bytes, padded to 4. `Models.updateTmdPacketIlen` adjusts the packet ilen fields on load (M2-05 CTMD rewrite, cross-referenced).
- **World-space convention:** vertex positions are the raw s16 values passed straight into the float vertex buffer — the reference's modern world space *is* the GTE's 16.16 fixed-point domain, interpreted as floats (both the GTE path and the modern path consume the same arrays; the GTE matrices are the fixed-point originals re-implemented in float, M4-02). Normals are unit-ish (÷4096). Rotations read via `readSvec3Rotation` = `psxDegToRad` per axis (M4-05 §2.1: PSX 4096 degrees per full turn). (Source-verified readers; the fixed-point-domain interpretation is `Inferred` from the shared-consumer structure.)

### 1.2 Primitive decode (`TmdObjLoader.fromObjTable`, source-verified)

Per primitive, from the header word: `command = (header | specialFlags) & 0xff04_0000`, `primitiveId = command >>> 24`:

| Bit(s) | Meaning |
|---|---|
| `(primitiveId >>> 5 & 3) != 1` | → "Unsupported primitive type" (throw) |
| `primitiveId & 0b1` | **lit = bit 0 clear** (inverted: 0 = lit) |
| `primitiveId & 0b10` | translucent |
| `primitiveId & 0b100` | textured |
| `primitiveId & 0b1000` | quad (else triangle) |
| `primitiveId & 0b1_0000` | gouraud (per-vertex normals; else share vertex 0's normal) |
| `command & 0x4_0000` | shaded (per-vertex colour) |

`coloured = shaded || !lit || !textured`. Invalid combinations throw (`textured && shaded`; `!textured && !lit`). **`specialFlags`** (per-model, not per-file): `0x20` = ctmd, `0x10` = uniformLit (battle lighting), `0x2` = forced translucent (set only on the GPU-model path, §5.5). Battle DEFFs set them from `ModelPart10.attribute_00 & 0x4000_0000` (bit 30 → `0x12`), and `DeffTmdRenderer14`/`SEffe`/`ModelEffect13c` additionally force `0x20` (the ctmd bit). `attribute_00` defaults to `0x8000_0000` (bit 31, "perspective") in `Models.initObjTable2` (0x80021b08) and is zeroed by `addNewDobj2` (0x80021bac).

### 1.3 Mesh splitting and the TPage rate → translucency table (G11)

Primitives are split into per-primitive **meshes by translucency mode** (`getTranslucencySizes` + `fromObjTable`): one opaque mesh, one untextured-translucent mesh, and one mesh **per `Translucency` mode** for textured translucent primitives. The mode for a textured translucent primitive comes from **the TPage word's rate bits: `Translucency.of(tpage >>> 5 & 0b11)`** — where `Translucency` is ordered (`Translucency.java`):

| TPage rate bits (5:6) | `Translucency` (ordinal) | Blend |
|---|---|---|
| `00` | `HALF_B_PLUS_HALF_F` (0) | (B+F)/2 |
| `01` | `B_PLUS_F` (1) | B+F |
| `10` | `B_MINUS_F` (2) | B−F |
| `11` | `B_PLUS_QUARTER_F` (3) | B+F/4 |

**M4 G11 disposition:** M4-01 left the GPU model's TPage rate-`00` handling documented but the rate→mode semantics unattributed externally; this loader (and the GPU path, §5.5) are the in-repo authority for the mapping: rate `00` = (B+F)/2, matching the PSX translucency-mode convention (source-verified mapping; layer-1 attribution remains `Inferred` — no external doc consulted). The untextured-translucent mode is **not** in the TMD data — it is supplied at runtime from the game global `tmdGp0Tpage_1f8003ec` (bits 6:5), §4.1.

### 1.4 Per-vertex data (source-verified)

- **Position:** `objTable.vert_top_00[vertexIndex]` (raw s16 → float, §1.1); `inPos.w` = the vertex index (used by morph effects, §4.5).
- **Normal:** indexed; out-of-range index → zero vector (defensive, in code — an asset-dependent corner, `Inferred` low exposure); unlit primitives skip the normal slots.
- **UV/TPage/CLUT:** stored **per primitive, uploaded per vertex** (in-source TODO: "clut/tpage is only set per face, but we're uploading it per vertex" — a reference efficiency choice). 24bpp primitives use **normalized** UVs (u/textureWidth, v/textureHeight) and *require* texture dimensions to have been supplied at load (`RuntimeException` otherwise); all other bit depths use raw byte UVs.
- **Colour:** per-vertex (shaded/unlit) or per-primitive (untextured — one colour for all vertices). `MathHelper.colourToFloat` (0..255 → 0..1) then, **for textured vertices, ×2.0 on all four components** — in-source comment: "Textures recolours use a range of 0..2 instead of 0..1, so 0xff is actually 2x bright". This is the reference's reproduction of the PSX 8.7-colour convention for textured primitives (the vertex colour is scaled 0..2 so a full-bright 0xff multiplies the texture by 2.0× — the shader side matches, §5.1); the exact hardware semantics remain `Inferred` (no external doc consulted).
- **Flags:** `LIT 0x1 | TEXTURED 0x2 | COLOURED 0x4 | TRANSLUCENT 0x8 | QUAD_FLAG 0x10` (the quad flag feeds the geometry stage, §5.2). Backface culling is disabled for the whole `Obj` when any primitive is `translucent && (!textured || uniformLit)` (in-source comment: "LOD sets some untextured primitives to translucent even though the translucency settings can only come from textures in order to disable backface culling" — the translucent bit doubles as a culling-kill switch; source-verified).

### 1.5 Near-face culling design (source-verified)

TMD meshes use **`GL_TRIANGLES_ADJACENCY`** so a **geometry shader** can cull whole faces at near-z (the in-source comment: "LOD culls the entire face if any vertex causes the GTE to error. In order to access the 4th vertex for faces that were quads, we use GL_TRIANGLES_ADJACENCY, and pass the fourth vertex as the adjacent vertex... for every adjacency even though it's only actually adjacent to one side... to simplify the geometry shader"). Index layout per face (interleaved triangle/adjacency slots 0,2,4 = triangle; 1,3,5 = adjacency):

- Triangle: `(v2, adj, v1, adj, v0, adj)` with `adj = vertexIndex+3` (the *next* vertex in the buffer — never checked or emitted for triangles).
- Quad (second face): `(v1, v0, v2, v0, v3, v0)` — the quad is triangulated on the **v1–v2 diagonal** (triangles `(v2,v3,v1)` and `(v1,v0,v2)`), with `v0`/`v3` carried as adjacency as needed.

The geometry shader (§5.2) checks proximity of slots 0/2/4, plus slot 1 when the quad flag is set — i.e. exactly the face's own 3 or 4 vertices. (The interleaved slot order is source-verified by the loader/gsh agreement; the GL format itself is format-corroborated, not re-derived from the spec offline.)

### 1.6 `MeshObj` assembly

`createMesh` uploads vertices/indices as a `STATIC` `Mesh` with the 7-attribute layout (stride 16 floats, M5-01 §9.2). The final `TmdMeshObj` receives its meshes in **reversed construction order** (opaque last-built → first... concretely: construction order `[opaque, untexturedTranslucent, translucentModes…]` is reversed, so layer 0 is the first translucent mode when present). `TmdMeshObj` overrides `shouldRender(translucency[, layer])` to add `|| translucency != null && this.translucencies.isEmpty()` — letting untextured-translucent `Obj`s pass the should-render gate for every pass (the actual single-draw behavior is enforced by the `QueuedModelTmd`/`QueuedModelBattleTmd` overrides, §4.3).

### 1.7 Where `Obj`s are built (load-time census, source-verified)

`TmdObjLoader.fromModel` is called at load for: the global **Shadow** model (GameEngine.java:435, `shadowModel_800bda10`), **battle combatant** models (Battle.java:2995, BENT `model_148`), battle stage models (Battle.java:5817), submap **Savepoint** (SMap.java:5279), retail-submap **Sobj** models (RetailSubmap.java:651/653 — with 24bpp dimensions when the source texture is 24bpp), and world-map entity models (WMap.java:949). `TmdObjTable1c.getObj()` lazily builds from the obj table otherwise. Per-part `Obj`s can be **rebuilt** with replacement texture dimensions (`fromModel(name, model, w, h)` deletes and re-creates each part's `Obj`) — the 24bpp/replacement-texture path (M2-06; the "CHESTER"-style texture swaps).

## 2. Model containers, coordinate systems, and animation

### 2.1 `Model124` / `ModelPart10` / `Keyframe0c` (source-verified)

- **`ModelPart10`** (legend/core/gte): `attribute_00` (bit flags; bit 30 → battle ctmd/uniformLit, §1.2; bit 31 default "perspective"), `coord2_04` (`GsCOORDINATE2` — the part's local coordinate), `tmd_08` (the `TmdObjTable1c` — a part references an obj table directly).
- **`Model124`** (0x124): `modelParts_00[]`, the **super coordinate** `coord2_14` ("Supercoordinate system for all model part coordinate systems, all model parts are attached to this"), `keyframes_90[keyframe][part]` (`Keyframe0c`), animation state machine (`0` init/reset → `1` playing → `2` paused, re-init loops the animation), `totalFrames_9a`, `interpolationScale`, `disableInterpolation_a2`, `subFrameIndex`, `zOffset_a0` (feeds the ×4 depthOffset, §4.1), `tpage_108` (feeds `tmdGp0Tpage`, §4.1), `partInvisible_f4` (one bit per part — per-part visibility), `shadowType_cc`/`shadowSize_10c`/`shadowOffset_118`, `uvAdjustments_9d` (`UvAdjustmentMetrics14`), `clutAnimations_a4` (copied from `CContainer.clutAnimations_04`), `animateTextures_ec[7]`/`animationMetrics_d0[7]` (per-part texture/UV animation — `Models.animateModelTextures` 0x80022018, "animates the fog in the first cutscene with Rose/Feyrbrand"), `usePs1Depth` (per-model flag feeding the geometry stage, §5.2).
- **`AnimType`** polymorphism (source-verified): **`StandardAnim`** (`TmdAnimationFile`), **`CmbAnim`** (`Cmb` — continuous battle animation with its own `Keyframe0c[]` transform track), **`LmbAnim`** (`Lmb` — looping battle animation). Container format details are M2-07; here only the runtime flow matters.
- **`Keyframe0c`** (12 bytes): `rotate_00` = 3 × s16 via `readSvec3Rotation` (PSX degrees → radians), `translate_06` = 3 × s16 raw (world s16 units), plus a derived `quat` via **Euler ZYX** (`quat.rotationZYX(z, y, x)`).

### 2.2 The coordinate chain (GTE)

- **`GsCOORDINATE2`** (legend/core/gte): `flg` (frame marker), `coord` (`MV` = 3×3 + `transfer` — the PSX D/COD matrix), `workm` (`MV` — the cached "local world" accumulation), `transforms` (`Transforms` = scale/rotate/trans/quat — the reference's parameter representation), `super_`/`sub` (chain links).
- **Part attachment:** `Models.prepareObjTable2` (0x80021ca0, decompiled) sets, per part: `attribute_00 = 0` (via `addNewDobj2` + `GsInitCoordinate2(null, coord2)`), `flg = 0`, **`super_ = coord2` (the model's `coord2_14`)** when not already linked, `tmd_08 = tmd.objTable[i]`, identity matrix, then — source-verified in-source comment: **"Dunno why but models are initialized with a 1-degree rotation on all axes"** — `transforms.rotate = (1°,1°,1°)`, `scale = (1,1,1)`, `trans = (1,1,1)`, applied to `coord`. The 1°/1.0 initialization is original decompiled behavior; animated models get it overwritten by the first keyframe, unanimated ones keep it (source-verified; rationale `Unresolved` — recorded as-is).
- **`Models.applyModelRotationAndScale`** (0x800214bc, decompiled): applies the super coordinate's `scale`/`rotateXYZ` to `coord2_14.coord` and sets `flg = 0` (marks the chain dirty).
- **`GsGetLw`** (0x8003d690, decompiled; javadoc reproduces the libGTE contract: per-node cached results, "libgs replaces 1 in flags already calculated"): walk up from the part's `coord2` to the root (or a `PSDCNT`-marked node), remembering the deepest dirty node (`flg == 0`); then walk back down: `matrix = matrix × coord2.coord` via **`GsMulCoord3`** (0x8003d950: `m1 = m1 × m2` with `transfer` composed — `m2.transfer × m1` added to `m1.transfer`), caching `coord2.workm = matrix` and setting `flg = PSDCNT_800c34d0` ("calculated this frame"). **Result: the part's local-world matrix** (composition of all ancestors' coords including the model super coordinate, × the part's own coord).
- **`GsGetLs`** (0x8003d9d0): same walk, but the final matrix is additionally **× `worldToScreenMatrix_800c3548`** (the ROM's world→screen matrix — the same global the freecam writes, M5-01 §2.2.10) → the **local-screen** matrix (the GTE's camera-applied transform).
- **`GsGetLws`** (0x8003dca0): both in one pass (javadoc: "When you use GsSetLightMatrix(), you pass it the lw matrix").
- The `PSDCNT_800c34d0` flag and the `workm` cache are the reference's software re-implementation of the GTE coordinate-system dirty-flag mechanism (PSX behavior per libGTE contract in the javadoc; layer 4 mechanism, layer 1/3 semantics — the GTE's own coordinate cache is M4-02 territory).

### 2.3 Animation flow (source-verified)

`TmdAnimationFile.apply(model, ticks)` (0x800dd4cc, decompiled):

- File: `modelPartCount` (0xc), `totalFrames` (0xe); **keyframe count = totalFrames / 2** (in-source comment: "seems there is one keyframe every two frames"); `partTransforms[keyframe][part]`.
- `loadIntoModel` → `Models.loadModelStandardAnimation` (LAB): installs `StandardAnim`, keyframes, counts, state 0 → `applyKeyframe` → state 1, `remainingFrames = totalFrames/2`.
- Per tick: paused (state 2) → no-op. `framesPerKeyframe = round(2 × interpolationScale)`. **`disableInterpolation`** → keyframe directly every tick (mod totalFrames/2). Otherwise `frame = ticks % totalFrames`, `currentKeyframe = frame/2`, `subFrameIndex = ticks % framesPerKeyframe`; **if `subFrameIndex == 0 || ub_a3 != 0` → `applyKeyframe`, else → `applyInterpolationFrame`** — in-source comment: "Last condition checks if this is a repeat frame (this was causing Haschel's twitching during transform, Gehrich teabagging, etc.) **GH#1664, GH#1687**" — i.e. the original repeats the keyframe on the first sub-frame of each keyframe pair (a visible stutter in some cutscenes), and the reference preserves that at keyframe boundaries while interpolating the rest (a layer-4 interpolation added on top of decompiled original control flow).
- `Models.applyKeyframe` (LAB_80021320): per part, copy the keyframe's quat/translate into `coord2.transforms` and update `coord2.coord` (rotation from quat, transfer from translate).
- **Two interpolation variants** (both `@Method(0x800213c4L)` — a decompilation address alias between `Models.applyInterpolationFrame` (public static) and `TmdAnimationFile.applyInterpolationFrame` (private) — same LAB address, two reference-authored bodies, noted as a provenance oddity): `Models` lerps `keyframe[current] → current coord` with scale `(subFrameIndex + 1)/framesPerKeyframe`; the `TmdAnimationFile` variant lerps `current coord → keyframe[current+1]` with scale `subFrameIndex/framesPerKeyframe` (comment: "Differs from standard applyInterpolationFrame by considering frame 0 the keyframe"). Both write the result into `coord2.coord` (transfer via the lerp destination, rotation via the quat). Interpolation is reference behavior (LAB-labeled); the original's sub-frame behavior is the repeated keyframe (§2.3 repeat-frame condition).
- **Cmb/Lmb** (battle animations, M2-07): `CmbAnim.apply`/`LmbAnim.apply` drive the same `Model124` keyframe/coord2 machinery (Cmb additionally owns a `Keyframe0c[]` transform track); consumers tick with DEFF parameter ticks (e.g. `ModelEffect13c.java:138`: `model.anim_08.apply(manager.params_10.ticks_24)`).

## 3. Queueing parts for rendering (the per-object census)

### 3.1 Submap — `SMap.renderSmapModel` (0x800daa3c, decompiled)

Per model: **`zOffset_1f8003e8 = model.zOffset_a0`** and **`tmdGp0Tpage_1f8003ec = model.tpage_108`** (ROM globals; the latter's rate bits become the untextured-translucent runtime mode). Per visible part (`partInvisible_f4` bit clear): `GsGetLw(part.coord2_04, smapModelLw)` then:

```
RENDERER.queueModel(part.tmd_08.getObj(), smapModelLw, QueuedModelTmd.class)
  .screenspaceOffset(GPU.getOffsetX() + GTE.getScreenOffsetX() - 184,
                     GPU.getOffsetY() + GTE.getScreenOffsetY() - 120)
  .depthOffset(model.zOffset_a0 * 4)
  .usePs1Depth(model.usePs1Depth)
  .lightDirection(lightDirectionMatrix_800c34e8)
  .lightColour(lightColourMatrix_800c3508)
  .backgroundColour(GTE.backgroundColour)
  .tmdTranslucency(tmdGp0Tpage_1f8003ec >>> 5 & 0b11)
  .translucentDepthComparator(DepthComparator.LESS)   // "Fix for chest shadow
                                                       // rendering on top of lid (GH#1408)"
```

- The model matrix passed is the **local-world matrix** from `GsGetLw` (§2.2) — the same MV also seeds the light-transform (`QueuedModelTmd.acquire` copies `transforms` into `lightTransforms`; the shader's light direction = `lightDirectionMatrix × lightTransforms`, §5.1).
- **Centering constants `−184`/`−120`**: 120 = 240/2 (screen half-height); 184 = 368/2 (`Inferred` — the submap's expanded-width center; the renderer's LEGACY-EXPANDED projection uses `expectedWidth = 368`, M5-01 §8). The GPU/GTE screen offsets are added to keep the modern path aligned with the GPU model's draw-area origin (M4-01 G3).
- **`depthOffset = zOffset × 4`**: the inverse of the GPU path's `(screenZ + zOffset) / (1 << zShift)` (zShift = 2 → ÷4, §5.5) — the same ×4 coupling moved into the depth-buffer domain (M4-01 G7 / Q-67b cross-reference).
- **GH#1408 fix** (reference behavior): the default translucent comparator `LESS_THAN_OR_EQUAL` (M5-01 §4) is overridden to `LESS` for submap models so the chest shadow doesn't render on top of the lid at equal depth.
- Optional per-model `Texture` (24bpp/replacement path, §1.7); then the shadow pass (`shadowType_cc != 0` → `renderSmapShadow`).

### 3.2 Retail submap — `RetailSubmap.renderSubmapModel` (0x800eee48, decompiled)

Same globals/queue as §3.1 but with a **separate model and light matrix**: `queueModel(obj, matrix, submapModelLw, QueuedModelTmd.class)` — `matrix` = the retail container's per-model transform, `submapModelLw` = `GsGetLw` result (light). This is the modern retail-submap path (M2-01/03); it has no `translucentDepthComparator` override and no per-part texture hook in the queue (texture replacement is handled at `fromModel` load time, §1.7).

### 3.3 Battle — `BattleEntity27c` (BENT, ~0x800ec9d0, decompiled) and stage models (Battle.java:8031-8050)

Per combatant: TIM → dedicated 15-bit VRAM texture upload (`uploadData15`, CLUT at rect.y + 240 — M2-06), then per visible part — **dual-path queueing**:

```
GsGetLws(part.coord2_04, lw, ls);
GsSetLightMatrix(lw);            // GTE light matrix (GPU path)
GTE.setTransforms(ls);           // GTE transforms (GPU path NCCS)
Renderer.renderDobj2(part, true, 0);   // GPU-model TMD path (mode 1 only, §5.5)

RENDERER.queueModel(part.tmd_08.getObj(), lw, QueuedModelBattleTmd.class)
  .depthOffset(model.zOffset_a0 * 4).usePs1Depth(model.usePs1Depth)
  .lightDirection(...).lightColour(...).backgroundColour(GTE.backgroundColour)
  .ctmdFlags((part.attribute_00 & 0x4000_0000) != 0 ? 0x12 : 0x0)
  .battleColour(Battle._800c6930.colour_00);
[.scissor(...) when the entity uses a scissor region]
```

- In mode 0 `renderDobj2` early-returns (`legacyMode != 1`, §5.5) and the pool queue is drawn; in mode 1 the GPU path queues commands and the pool queue is discarded (batches are mode-0-only, M5-01 §3). **The game code calls both unconditionally; each path self-gates on `legacyMode`** — the census mechanism for Q-37.
- Stage models (battle backgrounds, `stage.dobj2s_00` + `flags_5e4` per-part visibility, `z_5e8` depth) follow the same dual pattern (Battle.java:8031-8050) with default `tmdTranslucency` 0.
- `ctmdFlags` census (source-verified call sites): `SEffe.java:365`, `DeffTmdRenderer14.java:110`, `ModelEffect13c.java:97` → `0x20 | (bit30 ? 0x12 : 0)`; `TmdParticle.java:161`, `Battle.java:8048`, `BattleEntity27c.java:794` → `bit30 ? 0x12 : 0`. **Bit `0x2` (isTranslucent) is never set by any call site** — the `QueuedModelBattleTmd.shouldRender` extra-pass branch keyed on it is latent dead code in the current game (its potential double-blend consequence, §5.4, is therefore theoretical).

### 3.4 DEFF TMD objects — screen-space un-transform pattern

DEFF effect transforms are **screen-space** in the decompiled original (composed with `worldToScreenMatrix` for the GTE). The modern pool needs world space, so every DEFF TMD consumer branches on `legacyMode` (source-verified, same shape at each site):

- `DeffTmdRenderer14.java:85`: `if (legacyMode == 0) glTransforms = transforms × inverseWorldToScreenMatrix(3×3)`.
- `SEffe.java:325-336`: mode ≠ 0 → `seffeTransforms = transforms × worldToScreen`; mode 0 → copy, then rotation/scale overrides applied and **`seffeTransforms × inverseWorldToScreenMatrix(3×3)`** ("Transform override is already in screenspace, so we need to un-transform it").
- `TmdParticle.java:116`: mode ≠ 0 → `gteTransforms = particleTransforms × worldToScreen`; mode 0 → `gteTransforms = copy`, `glTransforms = particleTransforms × inverseW2S(3×3)`; particle translucency: `tmdGp0Tpage = (flags_00 & 0x4000_0000) ? (flags_00 >>> 23 & 0x60) : tpage` — **the DEFF particle flags carry the TPage rate bits directly (0x60 mask = bits 6:5 only)**.
- `Graphics.java:145` (`swapDisplayBuffer`, 0x80013148): `if (legacyMode == 1) GsSortClear(...)` — the OT clear is mode-1-only (GPU model side).

### 3.5 Other consumers (census tail)

Title logo (Ttle.java:1368, `QueuedModelTmd`), submap model shadows (`renderSmapShadow`, SMap.java:1270/1293 — the shadow part is re-oriented via `rotationZYX`/transfer and re-queued through a dedicated `smapShadowLw`; the GH#1408 chest-shadow fix, §3.1, applies to these), battle shadows (SEffe.java:830; `ShadowEffect`), sprite/trail DEFF effects (`SpriteWithTrailEffect30`, `LmbAnimationEffect5c`, `DeffTmdRenderer14.renderTmdSpriteEffect` — TMD-as-sprite paths), world-map entities (WMap.java:949 load; render via the submap-style path), and **morph/vertex-difference effects** (`VertexDifferenceAnimation18`, LAB_80109b7c/c0e0): a per-vertex exponential "embiggen" (`step = fma(embiggener, step, step)`) that rewrites the mesh vertex buffers every frame (`mesh.update()` re-upload) — a `DYNAMIC`-style CPU morph on top of the `STATIC` TMD meshes.

### 3.6 Q-37 census summary (mode 0, release default)

| Object class | Mode-0 path | GPU path in mode 0 | Notes |
|---|---|---|---|
| Submap models/shadows | `QueuedModelTmd` pools | none (renderDobj2 not called) | GH#1408 LESS fix; ×4 depthOffset |
| Retail submap Sobj | `QueuedModelTmd` pools | none | separate model/light matrices |
| World-map entities | `QueuedModelTmd` pools | none | |
| Battle combatants (BENT) + stage models | `QueuedModelBattleTmd` pools | **queued only in mode 1** (dual call, self-gated) | battleColour; ctmdFlags 0x12/0x32 |
| DEFF TMD effects/particles/models | `QueuedModelBattleTmd` pools (un-transformed) | **mode 1 only** | screen-space un-transform; DEFF rate bits → tmdGp0Tpage |
| Title logo, shadows | `QueuedModelTmd` pools | none | |

Q-37's remaining scope (which of these paths the *original* used per object, and whether mode-0 output is behaviorally faithful) stays open — this document establishes the reference's per-object routing at the mechanism level; visual cross-check against modes 1/2 is the stated follow-up.

## 4. Shader families

### 4.1 `tmd.vsh` (source-verified; read, not copied)

- Vertex layout/UBOs: identical ABI to `standard.vsh` (M5-01 §9.1-9.2).
- **Lighting (lit branch):** `vertColour = clamp( clamp( l.lightColour × clamp(l.lightDirection × vec4(N,1), 0, 8).rgb + l.backgroundColour.rgb, 0, 8 ) × inColour.rgb, 0, range )` with `range = 2.0` for textured vertices else `1.0`. This is the float reproduction of the **NCCS** normal-colour step (M4-02: per-vertex colour = light colour × (N·L) + background colour) — the GPU model's fixed-point version (M4-02 per-operation divergence table) is the compatibility counterpart; the shader's lenient `[0, 8]` inner clamp is a reference choice (the GPU model clamps per its 15-bit fixed-point rules). The `×2` textured range matches the loader's ×2 colour (M5-02 §1.4).
- **TPage/CLUT decode** (identical field arithmetic to `standard.vsh` and the CPU `GetTPage/GetClut`, M5-01 §9.2), **plus `translucency = intTpage >> 5 & 0x3`** passed to the fragment stage (the rate-bit mode, §1.3), and the CLUT-animation remap (M5-01 §9.3).
- **H-division (projection mode 1):** `z = clamp(viewspaceZ, 0, 65536); if (z != 0) xy *= zfar / z` — the PSX projection-plane pseudo-perspective (M4-01 G-row; M5-01 §8) **with a `z != 0` guard** — identical to `standard.vsh` (gfx/shaders/standard.vsh:137-141, which carries the same guard), so there is no asymmetry between the two vertex shaders (a vertex exactly on the projection plane is emitted un-divided in both).
- `viewspaceZ` is passed to the geometry stage (§4.2); `depthOffset` handling identical to standard (mode 2 linearization data).

### 4.2 `tmd.gsh` — geometry stage (source-verified)

- **Near-face culling:** "If any vertex is too close to the camera, cull the whole face" — the face is discarded when **`viewspaceZ <= zfar / 2`** for any of its own vertices (slots 0/2/4; plus slot 1 for quads via the `QUAD_FLAG`). This is the reference's reproduction of the original whole-face cull on GTE transform error (loader comment, §1.5; the GPU path's exact condition is `GTE.hasError()` after per-vertex RTPS, §5.5) — the `zfar/2` threshold is an engineered approximation of the original's error condition (`Inferred`; the original GTE error set is documented in M4-02, the equivalence is not externally established).
- **`usePs1Depth`:** when set (per-model `Model124.usePs1Depth`), the emitted clip z is replaced by **the primitive's average depth** (average of the 3/4 vertices' post-H-division `gl_Position.z`) — the float reproduction of the **AVSZ3/AVSZ4** average-Z operation (M4-02), feeding the depth buffer so the modern path matches the GPU path's per-primitive z (which the original used for ordering-table insertion, §5.5). (Whether the original also ran a PSX Z buffer for these primitives remains `Unresolved` — M4-01 G7; the reproduction is of the *average-z value*, which is source-verified.)
- Emit: triangle-strip of the face's 3 vertices (quads render as two independent triangles, each culled/averaged separately, §1.5).

### 4.3 `tmd.fsh` (source-verified)

- Scissor-discard and depth linearization identical to `standard.fsh` (M5-01 §5.4).
- **Runtime untextured mode:** `translucencyMode = translucency + 1` (per-vertex rate bits for textured); **for untextured translucent: `translucencyMode = tmdTranslucency + 1`** — the queue-time `tmdTranslucency` uniform (§3.1: from `tmdGp0Tpage_1f8003ec` rate bits; SEffe.java:366 / TmdParticle.java:162 set it per queued model).
- **Texture fetch:** 4/8bpp — `texelFetch(tex15, tpage + uv×widthMultiplier, ...)` for the packed index, then the **CLUT pixel is fetched in the fragment stage** from the same `tex15` store at `(clutX + p, clutY)` (the `widthMultiplier/widthMask/indexShift/indexMask` flat transfer from the vertex/geometry stage, §4.1); 15bpp — direct 5-bit-channel unpack; 24bpp — `texture(tex24, uv)` (explicit per-model texture, M5-01 §9.3). Zero-alpha zero-colour pixels discarded; per-pixel translucency deferral (modes 1/2) identical to standard (the CLUT/15-bit alpha channel *is* the pixel's translucent bit, M4-01 §5.2).
- **Recolour:** `outColour.rgb *= recolour` (per-model vec3 uniform) with **no re-clamp** (framebuffer clamps) — combined with the ×2 vertex colours, the TMD colour pipeline is the 0..2 reproduction (§1.4).
- Alpha: `translucent && translucencyMode == 1 → 0.5 else 1.0` (the (B+F)/2 pass, M5-01 §5.3).

### 4.4 `battle_tmd.*` (source-verified)

`battle_tmd.fsh` = `tmd.fsh` + `ctmdFlags`: `translucent |= ctmdFlags & 0x2`; the untextured-mode override also applies to **uniformLit textured** primitives (`translucent && (!textured || uniformLit)`).
`battle_tmd.vsh` = `tmd.vsh` + first color branch: **`textured && translucent && !lit && (ctmd || uniformLit)` → `vertColour = inColour × battleColour`** with per-channel **`mod(channel, 2.0)` wrap when > 2.0** (in-source comment: "Individually checks for retail color overflows") — the float reproduction of the GPU path's battle-tint (whose byte form is `(int)((rgb & 0xff) × colour.x) & 0xff`, §5.5). **The two wrap semantics differ** (byte truncation vs. 2.0-boundary mod) — both source-verified; equivalence `Inferred` (the mod-2.0 wrap is an approximation of the `& 0xff` step at the 0..2 scale). `battleColour` = `Battle._800c6930.colour_00` (the battle light colour struct), pre-bound frame-wide by the "Gross hack bro" state pre-bind (M5-01 §2.2.8, F-18).

### 4.5 The GPU-model TMD path — `Renderer.renderDobj2`/`renderTmdPrimitive` (legend/game/tmd/Renderer.java — reference-authored re-implementation of the original per-primitive loop, LAB-address-anchored; source-verified)

- **Mode gate: `if (RenderEngine.legacyMode != 1) return;`** — the entire GPU-model TMD pipeline runs only in legacy mode 1 (§3.3).
- Per primitive: same decode as the loader (§1.2) with `translucent |= specialFlags & 0x2`; per vertex: **`GTE.perspectiveTransform` (RTPS) → `GTE.hasError()` → skip face** (the original whole-face cull on GTE error — the geometry stage's §4.2 rule is the modern counterpart); screen X/Y from the GTE; **backface culling via `GTE.normalClipping()` (NCLIP) at vertex 2**: opaque faces culled when `winding <= 0`, translucent when `winding == 0` (back-facing translucent faces are *drawn* — winding < 0 passes; source-verified; the PSX winding semantics are M4-02).
- **z:** `screenZ = quad ? GTE.averageZ4() : GTE.averageZ3()` (**AVSZ4/AVSZ3** — M4-02); `z = min((screenZ + zOffset_1f8003e8) / (1 << zShift_1f8003c4), zMax_1f8003cc)`; **`if (z < zMin) skip`** — the ordering-table insertion formula (M4-01 G7; the float division by `1 << zShift` is the Q-67b detail — M4-02 §3.4) plus a zMin floor the OT path enforces before queueing.
- **Colour (three branches):** (a) `textured && translucent && !lit && (ctmd || uniformLit)` → battle tint `rgb × bkLight.colour_00` per-channel, **byte-truncated** (`& 0xff`) — the GPU-side counterpart of §4.4; (b) `!textured || lit` → **`GTE.normalColour(normal, colour)`** (NCCS — M4-02; out-of-range normal index → `ZERO` vector, matching the loader's zero-normal defense); (c) else → raw vertex colour.
- **Translucency:** textured → TPage rate bits (§1.3); final override `if (translucent && (!textured || uniformLit)) cmd.translucent(Translucency.of(tmdGp0Tpage_1f8003ec >>> 5 & 0b11))` — the same game global as the modern path (§3.1/§4.3).
- `GPU.queueCommand(z, cmd)` — ordering-table insertion (M4-01 §4: same-z LIFO traversal, Q-66 — unchanged by this document).
- `tmdGp0CommandId_1f8003ee` ("specialTrans") is read but unused (in-source TODO) — the TMD-id-upper-bits translucency feature (§1.1) is unimplemented (`Unresolved`, layer 3).

## 5. Traced example — a submap object model, load to pixel

(Plan verification criterion: file load → container → model parts → animation → queued model → shader inputs, cited.)

1. **Load:** the submap's CContainer (M2-04) yields the TMD (+ TIM + ANM). `Tmd` parses (header `fixp`/CTMD flags rejected — CTMD was already rewritten by `CtmdTransformer`, M2-05); each `TmdObjTable1c` reads verts (raw s16), normals (÷4096), primitive packets (ilen-adjusted, 4-byte padded). `Models.loadModelAndAnimation` → `prepareObjTable2` (parts: `super_ = model.coord2_14`, identity + the 1° init, `tmd_08 = objTable[i]`) → `TmdObjLoader.fromModel` (each part's obj table → `TmdMeshObj`: per-mode mesh split, TPage-rate modes, ×2 textured colours, TRIANGLES_ADJACENCY indices, `STATIC` GL mesh) → `loadModelStandardAnimation` (keyframes `[keyframe][part]`, first keyframe applied, state 1) → `adjustModelUvs` (0x80021628, decompiled: per-part `UvAdjustmentMetrics14` rewrite of CLUT/TPage/UV words — a load-time primitive-data patch, §2.1).
2. **Per-frame tick:** `anim_08.apply(ticks)` (state 1; repeat-frame condition, §2.3) → `applyKeyframe` (or the LAB interpolation) writes each part's `coord2.coord` (quat rotation + s16 transfer) → `flg` markers make the `GsGetLw` chain cache valid for the frame.
3. **Queue (render phase, phase 9 of gameLoop — M5-01 §13):** `renderSmapModel` sets `zOffset_1f8003e8`/`tmdGp0Tpage_1f8003ec` from the model, then per part `GsGetLw` (chain: part coord × … × model `coord2_14` (camera-applied by the super chain) → local-world MV) → `queueModel(obj, lw, QueuedModelTmd.class)` with §3.1's options; the pool's `needsSorting` flag set; `sequence` assigned (M5-01 §4).
4. **Render (mode-0 pass, M5-01 §5):** sort (model-view z, M5-01 §6) → 128-model UBO upload (`transforms2` 20 floats + `lighting` 32 floats when `lightUsed`: `lightDirection` (mat4 = lightDirectionMatrix × lightTransforms), `lightColour` (mat3), `backgroundColour`) → `useShader(modelIndex, discardMode)` (sets `tmdTranslucency`/`usePs1Depth` uniforms) → **`tmd.vsh`** (NCCS light equation; TPage/CLUT decode; H-division in LEGACY mode — submaps are `RenderMode.LEGACY`, platform/04 §8) → **`tmd.gsh`** (whole-face near-cull at `viewspaceZ ≤ zfar/2`; optional average-Z override) → **`tmd.fsh`** (CLUT `texelFetch` on `vramTexture15` — the same VRAM store the GPU model rasterizes into, M5-01 §9.3; per-pixel translucent-bit deferral; recolour) → opaque pass pixels; translucent pixels deferred to the translucent pass (M5-01 §5.3, depth-mask rules) in the single mode given by the TPage rate (textured) or `tmdTranslucency` (untextured) → composite into `renderBuffers[i]` → post pass → window (M5-01 §2.2.8).

## 6. M4 compatibility obligations (TMD-specific carry-over)

| Row | M5-02 disposition |
|---|---|
| G1 (VRAM dual stores) | **Addressed** — TMD textured pixels sample `vramTexture15` (4/8/15bpp) / explicit `tex24` (24bpp) exactly as M5-01 §9.3; the GPU path's own VRAM pipeline unchanged. |
| G2 (Bpp/TPage/CLUT) | **Addressed** — decode replicated in loader + `tmd.vsh` (same field arithmetic); 24bpp normalized-UV corner documented (§1.4). |
| G7 (z insertion /4 scale, zMax) | **Addressed (both paths)** — GPU path formula source-verified incl. zMin skip (§4.5); modern path inverse ×4 depthOffset (§3.1) + average-Z override (§4.2). Q-67b (float division vs shift) unchanged — it *is* the documented formula. |
| G8 (rasterizer) | **Addressed (culling side)** — whole-face near-cull reproduced in the geometry stage with an engineered `zfar/2` threshold vs the original's `GTE.hasError()` (equivalence `Inferred`); fill-rule/pixel-exactness remains M4-01 §5.2 (no baseline). |
| G10 (blending) | **Addressed** — the four modes flow from TPage rate bits / `tmdGp0Tpage` through the M5-01 §5.3 blend states; the B+F/4 decomposition applies to TMDs like any other model. |
| G11 (TPage rate 00) | **Advanced** — the in-repo rate→mode table (rate 00 = (B+F)/2, §1.3) is source-verified for *both* the modern and GPU paths and is consistent with the PSX convention; layer-1 attribution stays `Inferred` (no external doc consulted) — G11's remaining risk (the GPU model's handling of rate-00 *textured* pixels in M4-01 §5.4) is now corroborated, not resolved externally. |
| G12 (mask bit / per-pixel gating) | **Addressed** — per-pixel alpha deferral (§4.3) + depth-mask passes (M5-01 §5.3). |
| G5/G6 (OT / same-z LIFO) | **Cross-referenced** — the GPU path queues into the same OT (M4-01 §4.2, Q-66 LIFO — unchanged, not re-adjudicated); the modern path sorts (M5-01 §6). |
| G3 (center-origin offsets) | **Addressed (modern side)** — `screenspaceOffset = GPU offset + GTE screen offset − (184, 120)` (§3.1) keeps pool content aligned with the GPU model's draw-area origin; 184 = 368/2 `Inferred`. |
| G14/G15 (upload/VRAM copy) | **Cross-referenced** — GPU path only (M4-01 §6). |

Newly documented layer-4 items (not M4 rows): the 1°/1.0 part-init quirk (§2.2), the potential (latent) battle-CTMD double-blend (§4.4 — bit 0x2 never set, §3.3), the duplicate `@Method(0x800213c4)` interpolation bodies (§2.3), and the unimplemented TMD-id upper-bits translucency feature (§1.1/§4.5).

## 7. Verification criteria cross-check (M5 plan §M5-02)

- Traced example with citations: §5 (full load→pixel trace).
- CPU (GTE) vs GPU (shader) transform split: GTE-side — coordinate chain composition (`GsGetLw`, §2.2), keyframe→coord2 writes (§2.3), the GPU path's per-vertex RTPS/NCLIP/AVSZ/NCCS (§4.5); shader-side — model matrix application, light equation, TPage/CLUT decode, H-division, near-cull, average-Z (§4.1-4.3).
- Model/animation container feed: `Model124`/`TmdAnimationFile`/Cmb/Lmb → part `coord2` → local-world matrix → queued model (§2, §3).
- Every shader family summarized (GLSL read, not copied): `tmd.vsh/gsh/fsh`, `battle_tmd.vsh/fsh` (§4); `standard.*` covered in M5-01; `simple/copy/screen/post` deferred to M5-03/04.

## 8. Register updates

- **Q-37** (render-path faithfulness): *advanced* — the per-object mode-0 census is delivered at the mechanism level (§3.6 table): which TMD/battle/UI objects route to the pools vs the GPU model, the self-gating dual-call pattern, the DEFF screen-space un-transform, and the `ctmdFlags` census (bit 0x2 latent). Remaining scope: which path the *original* used per object + visual cross-check against modes 1/2 (M9 for battle objects; visual evidence required before any claim).
- **Q-67** (GTE external cross-checks + open divergences): *advanced* — the TMD path's exact z formula (incl. zMin skip) and the AVSZ3/4 → `usePs1Depth` reproduction are now source-verified on both paths (§4.2, §4.5); the NCLIP winding pass-through for translucent backfaces is documented (§4.5). The geometry stage's `zfar/2` near-cull threshold vs the original `GTE.hasError()` set is a new divergence to add to the M10 external cross-check list (Inferred equivalence).
- **G11** (compatibility table row): *advanced* — rate→mode table source-verified for both rendering paths (M4-01 G11 remains `Unresolved` externally; the in-repo mapping is now authoritative for the reference and corroborates the PSX convention).
- **Q-66** (same-z LIFO): unchanged — the GPU path's OT traversal is untouched by this document.
- No new register items: the latent battle-CTMD double-blend, the 1° init quirk, the duplicate interpolation method address, and the z=0 H-division corner (both vertex shaders skip the division identically, §4.1) are documented in place (§6) — none requires register tracking (all source-verified, none blocks M5/M9; the battle-CTMD case is unreachable with current call sites).
