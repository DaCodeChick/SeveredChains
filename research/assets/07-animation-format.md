# M2-07 — Animation Formats (ANIM / CMB / LMB)

Phase M2 deliverable 7 of 9. Reference: Severed-Chains @ `0c08c679b` — `legend/game/types/TmdAnimationFile.java`, `legend/game/combat/deff/{Anim,Keyframe0c usage,Cmb,Lmb,LmbType0,LmbType1,LmbType2,LmbTransforms14}.java`, `DeffPart.AnimatedTmdType` (magic dispatch), unpack patches `Unpacker.patchAnimation` (M2-03 #14-16) and `drgn0_142_anim` (#10). All claims **source-verified** unless noted. Layer note: these are **LoD-specific** model-animation containers (not PSX standard); the apply functions with `@Method(0x…)` annotations are faithful decompilations of the original ROM — layer **original behavior**. Validation categories per `AGENTS.md`: **source-verified** (reference code), **format-corroborated** (independent spec/implementation), **asset-validated** (requires real disc/asset bytes — none yet).

## 1. Common parts (source-verified)

- `Anim` (abstract base): reads `magic_00 = readInt(0)` (no validation at the base level); declares `loadIntoModel(Model124)` / `apply(Model124, animationTicks)`.
- `Keyframe0c` (12 bytes): `[0x0: 3×i16 rotation (PSX degrees → radians via readSvec3Rotation)][0x6: 3×i16 translation]` (+ cached quaternion).
- `LmbTransforms14` (0x14 bytes): `[0x0: 3×i16 scale (×4096, readSvec3_12)][0x6: 3×i16 translation][0xc: 3×i16 rotation (PSX degrees)]`.
- Magic dispatch (`DeffPart.AnimatedTmdType`, M2-05 §4): animation data at DEFF part offset `0x14`; `readInt(0) == Lmb.MAGIC (0x42_4d4c)` → `LmbType0`; `== Cmb.MAGIC (0x2042_4d43)` → `Cmb`; else `TmdAnimationFile`.

## 2. TmdAnimationFile (plain "ANIM") (source-verified)

```
0x00  u32  magic_00     (value: anything other than LMB/CMB magics; retail values asset-validated only)
0x04  4B   (unparsed)
0x08  4B   (unparsed)
0x0c  u16  modelPartCount
0x0e  i16  totalFrames     "one keyframe every two frames" (source comment)
0x10  (totalFrames/2) × modelPartCount × 0xc
      Keyframe0c, layout [keyframeIndex][partIndex]
```

- `keyframeCount = totalFrames / 2`; one `Keyframe0c` per (keyframe, part).
- `apply` `@Method(0x800dd4ccL)`: per tick, current keyframe = `frame >>> 1` (2 ticks per keyframe); on odd ticks (interpolation frames, unless `disableInterpolation_a2`) it interpolates (rotation via quaternion lerp path in the shared `Models.applyKeyframe` machinery — cross-ref M3 for model runtime). Frame index is computed with an **unsigned modulo** to match a retail `divu` bug (negative tick counts stay non-negative) — source comment documents the retail-bug preservation.
- `interpolationScale` (default 1.0) scales `framesPerKeyframe = round(2 * interpolationScale)`.

## 3. LMB family (source-verified)

`Lmb` base: `[0x0 u32 magic 0x42_4d4c ("LMB\0")][0x4 u32 objectCount]`; base `apply`/`loadIntoModel` throw "Not implemented".

### 3.1 LmbType0 (the implemented one)

```
0x08  objectCount × PartInfo0c:
      0x00  i16  partFlagIndex
      0x02  2B   (unparsed)
      0x04  i16  keyframeCount
      0x06  2B   (unparsed)
      0x08  u32  transformsOffset (relative to LMB file start)
keyframes: keyframeCount × LmbTransforms14 at transformsOffset + i*0x14
```

- `loadIntoModel`: `totalFrames = partAnimations[0].keyframeCount * 2` (assumes uniform keyframe count across parts — `Inferred`), `animationState_9c = 1`.
- `apply` `@Method(0x800dd638L)`:
  - frame = `(animationTicks & 0xffff…) % totalFrames` (unsigned-modulo retail-bug pattern, as TmdAnimationFile);
  - per part: **rotation** applied directly (`matrix.rotationZYX(rot)` — no interpolation), **translation** averaged with the next keyframe on odd ticks (wrapping to frame 0 at the end), **scale** applied directly (`scaleLocal`);
  - `remainingFrames` drives `animationState_9c` (0 = done, 1 = playing).
- LmbType0 is also the fallback parse target for DEFF type-1/2/5 animations when the magic is `Lmb.MAGIC`.

### 3.2 LmbType1 / LmbType2 (parsed, NOT applied in the reference)

- **LmbType1**: `[0x8 i16 transformDataCount][0xa i16 keyframeCount][0xc u32 → objectCount × PartInfo04 {u16 transformFlags, u16 ?, u8 partFlagIndex @3}][0x10 u32 → objectCount × LmbTransforms14 (single transform per part)][0x14 u32 → i16 transformData (count = transformDataCount * (keyframeCount-1)/2 bytes)]`.
- **LmbType2**: `[0x8 i16 transformDataPairCount][0xa i16 keyframeCount][0xc u32 → objectCount × i32 transformFlags][0x10 u32 → objectCount × LmbTransforms14 (initial transforms)][0x14 u32 → packedTransformData (byte × transformDataPairCount × (keyframeCount-1))]`.
  - `transformFlags` bit meanings (source comment): `0x80/0x100/0x200` disable z/y/x **rotation** steps; `0x400/0x800/0x1000` disable z/y/x **translation** steps; `0x2000/0x4000/0x8000` disable z/y/x **scale** steps; `0x100_0000` = index into `LmbAnimationEffect5c.deffFlags_14` (battle effect hooks, M9).
- Neither overrides `apply` → `Lmb.apply` throws "Not implemented". **Reference implementation gap**: if retail content uses LmbType1/2 animations, the reference cannot play them (`Inferred`: no retail model uses them, or they appear only in paths the reference never takes — asset-validated-only question, M2-10).

## 4. CMB (comb/chain animation) (source-verified)

Extends `TmdAnimationFile` (so the base ANIM header + keyframe table apply) with **per-frame delta transforms**:

```
0x00  u32  magic 0x2042_4d43 (" CMB")
0x04  4B   (unparsed; Anim base only reads magic)
0x08  u16  size_08
0x0c  u16  modelPartCount      (TmdAnimationFile field)
0x0e  i16  totalFrames         (TmdAnimationFile field; model totalFrames = totalFrames*2)
0x10  keyframe table (as §2: (totalFrames/2) × parts × Keyframe0c)
0x10 + parts*0xc
      (totalFrames - 1) × parts × SubTransforms08, layout [frameIndex][partIndex]

SubTransforms08 (8 bytes):
  0x0  u8  rotScale  = 1 << b
  0x1  3B  bvec3 rotation delta (signed bytes, PSX degrees × rotScale)
  0x4  u8  transScale = 1 << b
  0x5  3B  bvec3 translation delta (× transScale)
```

- `loadIntoModel` `@Method(0x800de210L)`: seeds the running transform from keyframe 0, then `apply(model, 0)`.
- `apply` `@Method(0x800ddd3cL)`:
  - **accumulating deltas**: from the current frame index to the target, each `SubTransforms08` is *added* to the running per-part transform (`rotate += rot`, `translate += trans`);
  - interpolation frames: quaternion `nlerp` + translation lerp between the running transform and the next delta (ratio 0.5);
  - two documented **retail bugs** preserved: (a) the frame modulo is unsigned (`divu` in retail — "the number of ticks to go negative"), (b) a negative frame index is clamped to 0.
  - `animationTicks_00` dedupe: repeated `apply` at the same tick is a no-op; frame wrap (frameIndex > a1_0) resets to keyframe-0 transforms.

## 5. Unpack-time animation patches (cross-ref M2-03 §7.1)

- **Magma fish** (14-16): `patchAnimation(data, expectedObjects = 11)` (`Unpacker.java:850-862`): reads `actualObjects = readUShort(0xc)`, `keyframes = readUShort(0xe) / 2`; rebuilds the file as `[0x10 header][keyframes × (11 × 0xc) keyframe blocks]`, where each new block copies `min(11*0xc, remaining)` bytes from the corresponding old block (so the new parts 11+ get the first part's data of the **next** keyframe — "to emulate retail behaviour") and the last keyframe is zero-filled; sets `newData[0xc] = 11`. This confirms the TmdAnimationFile layout: `0xc` = part count (u16), `0xe` = total frames (u16), keyframes at `0x10`, `0xc` per (keyframe, part) — source-verified by the patcher's arithmetic.
- **drgn0_142_anim** (10): injects 2 missing objects' keyframes into the Meru cutscene ANIM: new file = `[0..0x100 original][12B frame for obj 21][0x100..0x1f0 original][12B frame for obj 21][zeros]`, `newData[0xc] = 22`; comment: "we only create data for object 21, object 22 can be all 0's since it's not visible". The 12-byte frame = `Keyframe0c` (rot 3×i16 + trans 3×i16), consistent with §2.
- Note the two patches' different header sizes (0x10 in `patchAnimation` vs 0xc count field in 142): both agree that `0xc` = part count and keyframes begin at `0x10`.

## 6. psx-sdk / reusability notes (proposed-layer, no design decisions)

- The containers (ANIM/CMB/LMB) and the tick semantics (2 ticks/keyframe, unsigned-modulo retail bugs, delta accumulation) are **game-layer** — they belong with the model/animation runtime (M9), not the PSX platform crate.
- Reusable PSX side: only the unit conversions (PSX-degree → radians, ×4096 scale/normal conventions) — already covered by the GTE helpers (R1 psx doc).
- Faithfulness risk: LmbType1/2 non-implementation (this doc §3.2) is a **known reference gap** a future reimplementation must decide how to handle (original PSX behavior for those types is not captured by the decompilation).
