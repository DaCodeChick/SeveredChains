# M4-02 — Geometry Transformation Engine (GTE)

Status: Complete (M4, 2026-11-04). Reference: Severed-Chains @ `0c08c679b`, read-only. Refines the R1 boundary pass (`research/psx/01-compatibility-boundaries.md` §2.2) and answers register Q-20 at the per-operation level (with the external cross-check split out as Q-67). Consumed by M4-01 §4.3 (OT insertion) and M4-05 (classification, fixed-point helpers).

Evidence vocabulary: **source-verified**, **format-corroborated**, **experimentally verified**, **asset-validated** (*none exists — no such claim is made*), **Inferred**, **Unresolved**. Layers: (1) PSX platform, (3) LoD-specific, (4) reference-specific. No public PSX documentation was consulted in this milestone (network-restricted); PSX GTE register/flag semantics below are attributed from the reference's in-source register comments and preserved libgs/SDK javadoc text, marked `Inferred` layer-1 where they matter.

## 1. What the reference implements

The reference GTE (`legend/core/gte/Gte.java`, 399 lines) is a **register-model reimplementation in single-precision float** of the subset of the PSX GTE that LoD uses. It is not a complete GTE emulator: it has no rotation-delta instructions, no ROP, no normalization, no SYMV (by design — the task scope says not to build one; the gap list in §4 is the evidence).

### 1.1 Register file (source-verified; PSX register numbers from in-source comments)

| Reference field | PSX register | Type in reference | Notes |
|---|---|---|---|
| `V[0..2]` (Vector3f) | R0-1 / R2-3 / R4-5 (V0-2) | float ×3 | in-source comment: "s16" (signed 16-bit); the 12-bit-fraction convention is `Inferred` |
| `RGBC` (Color) | R6 | packed 24-bit + c | written by `normalColour` |
| `OTZ` | R7 | float | PSX: 17-bit unsigned, 4×-scaled average Z |
| `SXY[0..3]` (Vector2f FIFO) | R12-15 | float ×4 | PSX: 22.10 → 16-bit signed screen coords |
| `SZ[0..3]` (float FIFO) | R16-19 | float ×4 | PSX: 16-bit unsigned |
| `MAC0` | R24 | long | PSX: 32-bit |
| `RT` (Matrix3f) | R32-36, R40-44, R48-52 | float 3×3 | in-source comment; PSX: s16.12 (`Inferred` format) |
| `lightDirection`, `lightColour` (Matrix3f) | light matrices (no in-source register numbers; PSX convention R37-41/R42-46, `Inferred`) | float 3×3 | set by `GsSetFlatLight` (M4-01 §7.3 lighting) |
| `translation` (Vector3f) | R37-39 (TR) | float ×3 | PSX: s13.12 |
| `backgroundColour` (public Vector3f) | R45-47 (BG color) | float ×3 | **public field** — game code reads/writes it directly (many sites, e.g. per-scene ambient) |
| `screenOffset` (Vector2f) | R56/57/60 (OFX/OFY/OFFZ) | float ×2 | OFZ (R60) is not modeled |
| `H` | R58 | float | projection-plane distance |
| `FLAG` | R63 | long | error/flag register |

The in-source register comments establish the register numbers and the `s16` width of V; the fractional formats cited above (s16.12, 22.10, 12.12, u16, 4×-scaled OTZ) are standard PSX GTE conventions not carried in the source — `Inferred` layer-1, external cross-check Q-67a.

### 1.2 Operations exposed (source-verified) and their game usage census

| Operation (GTE opcode) | Reference method | Used by the game? | Call sites (representative) |
|---|---|---|---|
| RTPS (0x1) | `perspectiveTransform()` / `(v)` / `(x,y,z)` | **Yes — the workhorse** | every 3D projection: TMD renderer (`tmd/Renderer.java:144`), battle effects (`SEffe.java:279`, `ScreenCaptureEffect1c.java:147,186`, `LmbAnimationEffect5c.java:172`), submap (`SMap.java:2140`), camera/obj screen projection (`Battle.java:3476-3641, 5782-6164`, `Ttle`, `WMap`) |
| RTPT (0x30) | `perspectiveTransformTriangle()` / `(v0,v1,v2)` | **Yes** | `Graphics.RotTransPers4` (`Graphics.java:942`) → `GradientRaysEffect24.java:162`, TMD renderer (`tmd/Renderer.java:142`) |
| NCLIP (0x6) | `normalClipping()` → winding via MAC0 | **Yes** | `tmd/Renderer.java:162` (backface culling) |
| AVSZ3 (0x2d) | `averageZ3()` | **Yes** | `tmd/Renderer.java:170` (triangles) |
| AVSZ4 (0x2e) | `averageZ4()` | **Yes** | `tmd/Renderer.java:170` (quads) |
| NCCS (0x1b) | `normalColour(normal, colour[, out])` | **Yes** | `tmd/Renderer.java:172,191,134` (lit TMD primitives) |
| Flag/error | `getFlags()`, `hasError()` (bit 31) | **Yes** | `tmd/Renderer.java:146` (primitive discard on any GTE error) |
| Control accessors | `set/getRotationMatrix`, `set/getTranslationVector`, `set/getScreenOffset(X/Y)`, `set/getProjectionPlaneDistance`, `setLightSourceMatrix`, `setLightColourMatrix`, `set/getBackgroundColour`, `set/getTransforms` (MV = 3×3 + translation), `setVertex` | **Yes** | `GsSetRefView2L`/`GsSetSmapRefView2L` (`Graphics.java:757-841`), `Graphics.PushMatrix/PopMatrix` (0x8003ef80/0x8003f024, 20-deep `matrixStack_80054a0c`), `GsSetFlatLight` (0x8003c6f0), camera offsets (`BattleCamera.java:1629-1634`, `SMap.java:4402,4483,4639-4687`), projection plane (`Graphics.setProjectionPlaneDistance` 0x8003f8f0; defaults 1000 in `InitGeom` 0x8003e958, **640** in `gameInit`), `ScreenCaptureEffect1c.java:109-110` (direct RT/TR set) |
| Depth-cue MAC0 term in RTPS | (internal; `getMac0()` after RTPS) | **No — exposed but never read** | the RTPS 4th MAC0 term (`n × (−4194) + 0x1400000`, IR0 = MAC0>>12, out-of-range flag 0x1000) is implemented for PSX-instruction parity, but the only MAC0 consumer in the game is `normalClipping()` (NCLIP overwrites MAC0). Dead parity code in the reference (source-verified). |
| `getAverageZ()` (OTZ/4) | helper | **Yes** (via averageZ3/4) | `tmd/Renderer.java:170` path |

**Not implemented** (no reference, no game call): RTD/RTM (rotation by angle — the game builds rotation matrices in float, §3), ROP (register operations), NRM (normalization), NCSR (normal colour, rotate+scale), DPA (depth cueing), MV10/MV20/RSQ (square root), I (immediate), and the full register-op pipeline. The `Transforms` (scale/rotate/trans/quat) and `MV` (JOML Matrix3f + translation) classes are the game-side matrix representation; `GsCOORDINATE2` (flg/coord/workm/super_/sub + `Transforms`) is the libgs hierarchical coordinate node with the `PSDCNT` frame-cache flag (M4-01 §3.3/§4.4). `ModelPart10` ties a TMD object table to a `GsCOORDINATE2` (TMD rendering context, M5).

## 2. Per-operation semantics, rounding, saturation (source-verified)

### 2.1 RTPS — perspective transform single (`perspectiveTransform`)

Sequence (float):

1. `V' = RT·V + TR` (row-vector matrix multiply + translation).
2. **z clamp:** `z = clamp(z, 0, 65536)` (0x10000 — the PSX SZ/OTZ 16-bit span).
3. SZ FIFO shift: `SZ[0..2] = SZ[1..3]`, `SZ[3] = z` (the *new* Z is `SZ[3]` in PSX convention; `getScreenZ(3)` is therefore "the current vertex's Z" — used as such by `RotTransPers4`).
4. **Projection-plane test:** if `2z ≤ H` → `n = 1` and `FLAG |= 1<<17` (PSX flag: vertex on/behind projection plane); else `n = H / z`. (PSX: the same test protects the division; `Inferred` layer-1 from the comment.)
5. **Screen coords:** `SXY = n·(x, y) + screenOffset` — SXY FIFO shifted like SZ; each axis **saturated to [−0x400, +0x3ff]** with `FLAG |= 0x2000` (bit 13, SXY saturation) on clamp. (PSX SXY range; `Inferred` layer-1.)
6. MAC0 depth-cue term (unused by the game, §1.2): `MAC0 = n·(−4194) + 0x1400000` (constants = DQA/DQB in the in-source comment); `IR0 = MAC0 >> 12`; `FLAG |= 0x1000` if `IR0` outside [0, 0x1000].
7. `setMAC0` overflow: `< −2^31 → FLAG |= 0x8000` (bit 15), `> 2^31−1 → FLAG |= 0x10000` (bit 16).
8. `endCommand()`: if any of `(FLAG & 0x7f87_e000)` is set → `FLAG |= 0x80000000` (bit 31, the error bit). `hasError()` = `FLAG < 0`.

Observable consequences (source-verified + Inferred layer-1):

- A vertex **on/behind the projection plane** (2z ≤ H) sets the error bit → the TMD renderer **discards the whole primitive** (`tmd/Renderer.java:146`). LoD's default H = 640 means vertices with z ≤ 320 (world units, camera-relative) error out; camera code keeps subjects beyond that.
- A vertex **outside the [−1024, 1023] screen box** (after offset) sets the error bit → the same discard. **LoD's legacy TMD path does not split screen-edge polygons** — primitives crossing the screen boundary vanish rather than clip (the classic PSX GTE edge behavior; `Inferred` layer-1 that this matches the hardware, which also sets the SXY-saturation error and does not clip). Visible at screen edges in legacy mode 1; the mode-0 path (M5) uses GPU clipping instead.
- All saturation/clamping happens **in float space**; the hardware truncates 22.10 results to 16-bit ints. The reference's float SXY is later int-rounded at the point of use (`GpuCommandPoly.pos(float)` rounds, M4-01 §5.2; `RotTransPers4` passes floats into `Vector2f`).

### 2.2 RTPT — perspective transform triple (`perspectiveTransformTriangle`)

Three consecutive RTPS (v0, v1, v2; the third sets MAC0). After RTPT: SXY[1..3] / SZ[1..3] hold the three vertices (index 0 is stale from the previous single transform — which is exactly why AVSZ3 averages `SZ[1..3]`).

### 2.3 NCLIP — normal clipping (`normalClipping`)

`MAC0 = SX0(SY1−SY2) + SX1(SY2−SY0) + SX2(SY0−SY1)` — the 2D orientation (double signed area) of the three screen-space vertices; returned as the winding (positive = the PSX "visible" orientation given the renderer's front-face convention, M4-01 §7.3). Overflow flags as in §2.1.7. The TMD renderer culls `!translucent && winding <= 0` (backfaces) and `translucent && winding == 0` (degenerate only) (source-verified).

### 2.4 AVSZ3 / AVSZ4 — average Z (`averageZ3`/`averageZ4`)

- AVSZ3: `OTZ = (SZ[1]+SZ[2]+SZ[3])/3`; AVSZ4: `OTZ = (SZ[0]+SZ[1]+SZ[2]+SZ[3])/4`; both **saturated to [0, 0xffff]** with `FLAG |= 0x40000` (bit 18, SZ3/OTZ saturation) on clamp (source-verified).
- `getAverageZ()` = `OTZ / 4.0f` — the reference exposes the **4×-unscaled** average (the PSX OTZ register holds 4× the average; the /4 matches the preserved SDK javadoc convention — see §3.4).

### 2.5 NCCS — normal colour single (`normalColour`)

Given a (unit) normal and a 24-bit vertex colour:

1. `light = (normal × lightDirection)⁺` — row-vector × the **light-direction** matrix, then **clamped per-channel to ≥ 0** (negative light contribution = 0; the positive-part stage),
2. `light = light × lightColour` (light-colour matrix — per-channel colour),
3. `light += backgroundColour` (the R45-47 ambient; game code sets it per scene),
4. `out[c] = clamp(colour[c] × light[c], 0, 0xff)` — **(int) cast of the clamped float product** (truncation toward zero; the product is ≥ 0 so truncation ≈ floor).

The light **direction** matrix stores **negated** normalized directions (`Graphics.GsSetFlatLight` 0x8003c6f0 writes `−dir/|dir|` into the columns; id 0/1/2 select the three parallel light sources; `mag == 0` → returns −1; `GTE.setLightSourceMatrix` copies the result, `Graphics.java:351-356`). Net: `N·(−L)` per channel — the standard PSX "light points toward the surface" convention (`Inferred` layer-1). Up to three parallel lights + per-colour + ambient; no specular, no per-pixel lighting (vertex colour × per-vertex normal), no fog stage (the depth-cue MAC0 term is dead, §1.2).

Divergence note: the PSX NCCS operates in 12.12 fixed-point with its own saturation points; the reference is float with saturation only at the final 0..0xff. See §3.

## 3. Float vs fixed-point — where the model diverges (Q-20, per-operation)

The reference holds **every** GTE register in float; the PSX GTE is fixed-point (V s16.12, RT s16.12, TR s13.12, SXY 22.10→s16, SZ/OTZ u16, MAC0 s32). Per-operation divergence table (source-verified mechanism; layer-1 attribution `Inferred` — external GTE documentation not consulted):

| Operation | PSX mechanism | Reference mechanism | Divergence class |
|---|---|---|---|
| RTPS screen coords | 32-bit integer divide, 22.10→16-bit **truncation** | float divide, float result, sat in float, int-rounded at use | **Rounding** — up to ±1 screen px on int truncation/round; sub-pixel differences accumulate across the pipeline |
| RTPS z handling | SZ u16; division on clamped u16 | float; clamp [0, 65536] before divide | value-identical in range; the float path cannot represent the 16-bit wrap (there is none — SZ is unsigned) |
| Projection test | `2z ≤ H` integer | `2z ≤ H` float | identical for integer H/z (H is set from int constants: 640/1000/320) |
| SXY saturation | at the 16-bit load | at the float store | a value like 1023.9 is **kept** in the reference (no flag) but **saturates + flags** on PSX — primitives hovering on the screen edge can be drawn in the reference and discarded on PSX (or vice versa at −1024) |
| NCLIP | 32-bit integer MAC0 | float→long cast of the cross product | identical for integer SXY; diverges for float SXY (±1) — culling decisions on exact-edge windings can flip |
| AVSZ3/4 | integer average ×4, u16 | float average, /4 exposed | see §3.4 |
| NCCS | 12.12 fixed-point, per-stage saturation | float, clamp≥0 then 0..0xff final | **Rounding + stage quantization** — light values differ by ≤1/4096 in 12.12; visible as ±1 color steps on lit edges (`Inferred` low-impact) |
| Matrix build (RT/TR) | PSX: RTD/RTM from s13.12 angle deltas (original) | **float** JOML matrices from `psxDegToRad` angles (`rotationXYZ`/`rotationZYX`, `MV`) — `Keyframe0c.java:19`, `BattleObject.java:34,58`, `SEffe.java:228-332`, `SMap.java:1810-1812,2066` | **Precision** — the single largest source of GTE divergence: the original's rotation matrices were quantized to s16.12; the reference's are full-precision float. Small per-rotation deltas accumulate over animation chains (M9-adjacent; Q-67) |
| `GsSetSmapRefView2L` | — | **deliberate integer variant**: integer deltas, `(int)Math.sqrt`, `×0x1000/len` normalization before matrix build (`Graphics.java:793-841`) | see §3.3 — the in-repo proof that float-vs-fixed divergence is *observable in this game* |

### 3.1 Where rounding occurs (summary)

- **In the reference:** int truncation at NCCS final colour (`(int)Math.clamp(…)`), float→int at SXY/MAC0 stores, `Math.round` at the GpuCommandPoly vertex intake (M4-01 §5.2), `Math.round` in `Gpu.queueCommand(float z)` (M4-01 §4.2). The TMD OT insertion is a **float division** by `1 << zShift` (`tmd/Renderer.java:169`: `(screenZ + zOffset) / (1 << zShift)`), not a hardware shift — value-identical for integer zShift (0 in the standard config), but a float intermediate where the PSX shifts a 32-bit integer (source-verified).
- **On PSX:** 22.10 truncation (SXY), 16-bit quantization (SZ/OTZ), 12.12 quantization (RT/TR/NCCS), 32-bit truncation (MAC0).

### 3.2 Rounding-sensitive behaviors

- **Animation smoothness/alignment:** TMD animation interpolation runs through float matrices in the reference; the original quantized each RTD. Long animation chains (battle BENTs, submap sobjs) can drift by ±1–2 px screen-space relative to the original — *unless* the game's own fixed-point helpers (below) dominate, which they do for the camera.
- **Screen-edge culling** (§2.1): the float saturation boundary admits ±1 px more primitives than the hardware.
- **Lighting steps:** NCCS ±1 color steps.

### 3.3 The in-repo fixed-point fix: `GsSetSmapRefView2L` (source-verified, layer-3/4 boundary case)

`Graphics.GsSetSmapRefView2L` (`Graphics.java:793-841`) is a **non-`@Method`** (reference-authored) twin of the ROM `GsSetRefView2L` (0x8003dfc0), used for submap cameras. Its own comment: *"This method is the same as GsSetRefView2L, except that internally the math is performed on integers prior to normalization. This is done so that sobj and collision placements match retail, as higher precision math was causing noticeable shifts."* It replaces the float normalize with `int` deltas, `(int)Math.sqrt`, and `× 0x1000 / len` 12.10-style normalization before building the view matrix. **This is the strongest in-repo evidence that float GTE math diverges observably from the original** — the decompiler team found float camera math shifted submap objects/collisions vs retail and patched the submap path with integer math. Implications: (a) the original's camera math was effectively integer/fixed-point (strong `Inferred`, consistent with fixed-point GTE usage); (b) any Rust port must reproduce the **integer** variant for submap cameras (behavior-significant for sobj placement and collision); (b) other camera contexts (battle `GsSetRefView2L` float path, `BattleCamera` offsets) remain float in the reference — whether they were also integer in the original is Unresolved (Q-67).

### 3.4 OT-insertion scale — the /4 convention (Unresolved, Q-67)

LoD's TMD path inserts `(averageZ3/4 + zOffset) / 2^zShift` clamped to `zMax = 0x3ffe` (M4-01 §4.3; `tmd/Renderer.java:169-172`). The reference's `averageZ3/4()` returns `OTZ/4` (the 4×-unscaled average), and `Graphics.RotTransPers4` (0x8003f9c0) returns `GTE.getScreenZ(3)/4.0f` with preserved SDK javadoc: *"Returns 1/4 of the Z component sz of the screen coordinates corresponding to v3"* — i.e. the /4 is the **original libgs convention**, decompiled as-is (source-verified for the reference code + preserved SDK text). Whether the PSX libgs *OT-insert* path (`GsDrawObj`-family, which the TMD code evidently does not use) inserts the 4×-scaled OTZ instead is **not resolvable from the repository** (the libgs internals were decompiled away), and `zMax = 0x3ffe ≈ 4 × 4095.5` is consistent with *either* a scaled-OTZ/unscaled-average convention depending on LoD's world depth. Recorded as **Q-67** (external cross-check / asset-census item); for the Rust port the safe contract is "reproduce the decompiled formula exactly" (behaviorally equivalent to the reference) — which is what M4-05 classifies.

### 3.5 Fixed-point math helpers used by the game (M4-05 cross-reference)

Documented in full in M4-05; the GTE-relevant facts (source-verified):

- `MathHelper.psxDegToRad` (`MathHelper.java:190-196`): PSX degree = 4096 per full turn (`2π/4096`); all game rotation angles are converted through it.
- `rsin(theta)` (0x80013598) / `rcos(theta)` (0x800135b8) (`Scus94491BpeSegment.java:250-258`): `(short)(MathHelper.sin/cos(psxDegToRad(theta)) * 0x1000)` — **float** sin/cos scaled to s13.12 output, not a hardware table lookup. The in-source javadoc "ALWAYS returns a positive value even for negative angles" holds only for the call sites that constrain `theta` to [0, 0x400] (e.g. `drawBoxagonEffect`, M4-01 §7.1, where `theta = angle·0x400/123`, angle ≤ 123); with unrestricted input the value is signed. (`Inferred`: the PSX `ROTS`-family table is a signed 13.12 lookup; the reference's float equivalence is `Inferred` to the hardware within 1/0x1000.)
- `simpleRand()` (0x800133ac) and `rand()/srand()` (0x8002d260/0x8002d270) — see M4-05 §4; the battle-accuracy/loot/drop call sites make them **gameplay-significant** (`Battle.java:4331,8527,9124`, `BattleStateEf4.java:623-626`, `SBtld.java:118`, `SEffe.java:1966-2055`, `ScriptState.java:1191` = the Fate `rand` op).

## 4. Worked example — a lit TMD quad, battle (legacy mode 1)

`Battle` renders a lit, untextured TMD quad for an effect (the full path is `tmd/Renderer.renderTmdPrimitive`, M4-01 §7.3; here the GTE side, source-verified):

1. **Camera:** `GsSetRefView2L(rview)` (0x8003dfc0, float path) builds `worldToScreenMatrix_800c3548` = aspect(1,1,h/w·4/3) × twist(−viewpointTwist) × pitch × yaw × −viewpoint·M, and pushes it to the renderer camera. `GsMulCoord2(worldToScreen, lw)` gives each node's **local-screen** matrix (libgs `ls` = `wsMatrix × lw`), cached per `PSDCNT` frame in `GsCOORDINATE2.workm` (`GsGetLws` 0x8003dca0).
2. **Vertex:** for quad vertex v3: `GTE.setVertex(2, worldVertex)`; `GTE.perspectiveTransform()` → RTPS: `V' = RT·V + TR` (RT/TR = the node's local-screen matrix, pushed by the model code via `GTE.setTransforms`), `n = H/z` (H = 640 unless the camera changed it), `SXY[2] = n·(x,y) + OF`, `SZ[3] = z`.
3. **Error check:** `GTE.hasError()` — true if the vertex is behind the projection plane (2z ≤ 640 → z ≤ 320) or outside [−1024, 1023] screen space → **the quad is dropped** (no clipping).
4. **Culling (vertex 2 of the fan):** `winding = GTE.normalClipping()`; unlit path N/A; here lit → `cmd.rgb(i, GTE.normalColour(normals[ni], tmdVertexColour))` — NCCS: `(N·(−L))⁺ × Lc + B` per channel × vertex colour, 0..0xff.
5. **Depth:** after the third vertex, `screenZ = GTE.averageZ4()` = (SZ0+SZ1+SZ2+SZ3)/4 (float); `z = min((screenZ + 0) / 2^0, 0x3ffe)` → `GPU.queueCommand(z, cmd)` (M4-01 §4.2 — float rounded to the OT slot).

**Where the two could diverge (annotated):** step 2 integer truncation of SXY (PSX) vs float+round (reference, ±1 px); step 2 RT/TR s16.12 quantization (original) vs float (reference, accumulation over animation); step 3 the edge-culling boundary (±1 px box, §3.2); step 5 the /4-scale convention (Q-67). On PSX the identical call sequence ran on the fixed-point GTE with the same control values — the *algorithm* is reproduced, the *arithmetics* are not bit-identical.

## 5. Register updates

- **Q-20** (GTE float-vs-fixed divergence): *advanced* — per-operation table delivered (§3.1–§3.2); the "does any game behavior depend on fixed-point artifacts" half is answered **yes, in the reference itself** via the `GsSetSmapRefView2L` integer patch (§3.3, sobj/collision placement); the residual (which other camera/animation contexts were integer in the original; external GTE doc cross-check) moves to **Q-67**.
- **Q-67** (new): GTE external cross-check + open divergences: (a) PSX GTE register/flag semantics (SXY range, OTZ 4×-scaling, flag bits) — `Inferred` from in-source comments, external doc pending; (b) the /4 OT-insertion scale convention (§3.4); (c) whether the original's non-submap camera math was integer (Q-67c); (d) s16.12 matrix quantization accumulation vs float for long animation chains. — `Open` — M4/M5/M10 — needs: external PSX GTE documentation (no$psx etc.) and/or emulator comparison; not blocking M4-05 classification (which classifies against the *decompiled contract*).
