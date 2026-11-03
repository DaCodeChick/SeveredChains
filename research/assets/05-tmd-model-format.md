# M2-05 — TMD / TmdWithId / CContainer / CTMD / DEFF Part Formats

Phase M2 deliverable 5 of 9. Reference: Severed-Chains @ `0c08c679b` — `legend/game/tmd/{Tmd,TmdHeader,TmdObjTable1c,TmdWithId,Renderer}.java`, `legend/game/types/{CContainer,TmdWithId consumers}.java`, `legend/game/combat/deff/DeffPart.java`, `legend/game/unpacker/CtmdTransformer.java`, `legend/game/Models.java` (`updateTmdPacketIlen` `@0x8003e638`). All claims **source-verified** unless noted. Layer note: TMD is a PSX-standard model format (format-corroborated: Sony TMD family, no$psx "PSXSPX" 3D model formats); CContainer/CTMD/DEFF-parts are **LoD-specific** containers.

## 1. TMD (source-verified)

Reference: `Tmd.java`, `TmdHeader.java`, `TmdObjTable1c.java`.

```
TMD (data section starts at 0x0):
0x00  u32  flags        0x1 = fixp (throw "fixp not supported" — reference cannot load)
                        0x2 = CTMD (throw "Found CTMD" — runtime never parses CTMD;
                                unpack-time converter must have run first)
0x04  u32  nobj         object count
0x08  0x1c * nobj       object table (TmdObjTable1c), pointers relative to TABLE START (0x08)
…               data:   vertex arrays, normal arrays, primitive streams (order per pointers)
```

### 1.1 Object table entry — `TmdObjTable1c` (0x1c bytes)

| offset | size | field |
|---|---|---|
| `0x00` | 4 | vertex pointer (relative to table start) |
| `0x04` | 4 | `n_vert` |
| `0x08` | 4 | normal pointer (relative to table start) |
| `0x0c` | 4 | `n_normal` |
| `0x10` | 4 | primitive pointer (relative to table start) |
| `0x14` | 4 | `n_primitive` |
| `0x18` | 4 | `scale` (unused by reference readers found) |

- Vertices: `n_vert` × 8 bytes, each `{i16 x, i16 y, i16 z, 2B pad}` (`readSvec3_0`).
- Normals: `n_normal` × 8 bytes, each `{i16 x, i16 y, i16 z, 2B pad}` scaled by **2^12** (`readSvec3_12` → x/4096 …).
- Primitives: run-length groups (below).

### 1.2 Primitive stream (source-verified)

The stream is a sequence of **runs**; each run = [header u32][N × packetSize bytes]. The run count N is taken from the header's low 16 bits, but **only after** `Models.updateTmdPacketIlen` (`@Method(0x8003e638L)`) rewrites the stream at load time:

- `updateTmdPacketIlen(primitives, n_primitive)` walks all `n_primitive` primitives, computing each packet size from a **mode/flag size table** (below), and **writes a u16 run-length at the start of every maximal run of identical (mode, flag)**: when `(mode, flag)` changes, `primitives.writeShort(packetStartIndex, primitivesSinceLastChange)`.
- Therefore the on-disk low-16 of run-start headers is authoritative only for **CTMD** (see §5); for plain TMDs the reference overwrites it with the computed run length. (An on-disk count that disagrees with the mode/flag table is silently corrected — `Inferred` intent: robustness.)
- `TmdObjTable1c` then reads: `count = header & 0xffff`, `packetSize = primitivePacketSize(header)` (computed, not stored), copies `count` packets of `packetSize` bytes, rounds the stream offset up to 4.

#### Primitive header u32 layout (as used by the reference)

```
bits 31-24  mode  = PSX GPU GPRIM command id (0x20-0x3f range; the ROM decompilation's
                  comments label them setPoly*3/4 — e.g. 0x20 setPolyF3, 0x24/0x31 setPolyFT3,
                  0x30 setPolyG3, 0x34/0x39/0x25 setPolyGT3, 0x28 setPolyF4, 0x2c/0x2d setPolyFT4,
                  0x38 setPolyG4, 0x3c/0x35 setPolyGT4)
bits 23-16  flag:
            bit 2 (0x4 in flag byte = 0x40000 in u32): per-vertex-colour size variant in the
                  updateTmdPacketIlen table (F3: 0x10 vs 0x18; G3: 0x14 vs 0x1c; F4: 0x14 vs 0x20)
            bit 4 (0x10 in flag byte = 0x1000 in u32): 0 → normals present in packets
            bit 6 (0x40 in flag byte = 0x4000 in u32): "gradated"/shaded → per-vertex colours
bits 15-0   (TMD)   run length — rewritten by updateTmdPacketIlen; on-disk value otherwise stale
            (CTMD)   number of bit-stream primitives in the run (authoritative on disk)
```

Mode-bit semantics used by the reference (`Renderer.renderTmdPrimitive`, `CtmdTransformer.primitivePacketSize`):

| mode bit | meaning |
|---|---|
| `0b1` | `lit = (id & 1) == 0` (bit clear → lit) |
| `0b10` | translucent (`(id & 0b10) != 0`) — combined with specialFlags bit 0x2 in battle |
| `0b100` | textured |
| `0b1000` | quad (4 vertices) |
| `0b1_0000` | gouraud (per-vertex normal indices) |
| `bits 5-6` | must be `01`, else `RuntimeException("Unsupported primitive type")` — i.e. supported ids are `0x20-0x37` |

Constraint checks (both places): `textured && shaded → "Invalid primitive type"`; `!textured && !lit → "Invalid primitive type"`.

Note: `updateTmdPacketIlen`'s size table also handles ids `0x30-0x3d` (which the *renderer* would reject as unsupported) — the load pass is broader than the render pass (source-verified observation; `Inferred`: models may load but only render in `legacyMode == 1` for supported ids — cross-ref Q-37, M2-10 §6).

#### Packet layout (per primitive, `primitivePacketSize` = `Renderer.renderTmdPrimitive` read order)

```
[textured]  per vertex (v = 0..vertexCount-1):
              u: u8, v: u8
              2-byte field: vertex 0 → clut, vertex 1 → tpage, others ignored
[shaded || !lit]  per vertex: colour u32 (packed ARGB? — consumed as a 32-bit colour int)
[!textured && lit]  one shared colour u32 for all vertices
[lit]         per vertex:
              if gouraud || vertex == 0: normal index u16 (else reuse vertex 0's)
              vertex index u16  (bounds-checked against normalCount when lit)
```

Round up to 4. `Renderer` additionally: battle "special translucency" via `ctmdFlag | (dobj2.attribute_00 & 0x4000_0000 ? 0x12 : 0)` — the **upper 16 bits of the TmdWithId id carry translucency info for battle TMDs** (source comment, source-verified); `legacyMode != 1` → `renderTmdPrimitive` returns immediately (cross-ref Q-37).

## 2. TmdWithId (source-verified)

`TmdWithId.java`:

```
TmdWithId:
0x00  u32  id       battle: upper 16 bits = translucency info (see §1.2)
0x04  …    TMD      (M2-05 §1; the CTMD discriminator checks byte 0 == 0x41 for CTMD files)
```

- `optimisePacketsIfNecessary` `@Method(0x800de76cL)`: if TMD flags & 0x2 == 0 → wraps the obj table in a `ModelPart10` (dobj2); if CTMD → returns the obj table directly (the runtime never expects CTMD post-unpack; `Inferred` defensive branch).

## 3. CContainer (source-verified)

`legend/game/types/CContainer.java` (doc links to the LoD-modding issue #2 for the original analysis):

```
CContainer (relative to container start):
0x00  u32  tmdPtr      → TmdWithId (§2); 0 = none
0x04  u32  clutAnimPtr → CContainerClutAnimations; 0 = none
0x08  u32  subfile2Ptr → CContainerSubfile2 (size param default 7); 0 = none
```

- This is the container the CTMD discriminator calls the **"0xc container"**: in LoD DEFF parts the TMD is inlined, so `tmdPtr == 0xc` (header size), which is why `CtmdTransformer` checks `readInt(containerOffset) == 0xc` (M2-03 §4 #25). The 0x04/0x08 pointers are the two "subfile" pointers the CTMD transformer adjusts when rewriting sizes.
- `CContainer` consumers: `DeffPart.TmdType.tmd_0c`, `Scus94491BpeSegment.loadShadow` (shadow.ctmd), battle object models.

## 4. DEFF parts (source-verified)

A DEFF *archive* (M2-03 §6) splits into **parts** (one per archive entry). Each part begins with:

```
DEFF part header (first 0x18 bytes for TMD-type parts; layout per type):
0x00  u32  flags_00   byte 3 (MSB) = part type; byte 0 = index; bytes 1-2 = "some kind of ID?" (source comment)
                       types: 0 = LMB, 1 = TMD (animated, optimised), 2 = TMD (animated, unoptimised),
                             3 = TMD (single object, optimised), 4 = sprite, 5 = CMB (animated TMD);
                             "Apparently there is a type 7" (source comment; unhandled → throws)
```

Per-type fields (source-verified from `DeffPart.java`):

| type | additional header fields |
|---|---|
| 0 (LmbType) | `0x04` u32 `type_04` (0/1/2); `0x08` u32 `lmbOffset` → Lmb/LmbType1/LmbType2 (M2-07 §3) |
| 3 (TmdType) | `0x08` u32 `textureOffset`; `0x0c` u32 `tmdOffset` (→ CContainer, §3); `0x14` u32 `offset14` (end of TMD region); `textureInfo_08` = `(tmdOffset - textureOffset)/8` × 8-byte `{vramPos: u16 x,y,w,h}` (pairs of texture, CLUT — source comment); CContainer present iff `tmdOffset != offset14` |
| 1, 2, 5 (AnimatedTmdType, extends TmdType) | as type 3, **plus** `0x14` u32 `animOffset` → animation: dispatch on magic — `0x42_4d4c` → `LmbType0`, `0x2042_4d43` → `Cmb`, else `TmdAnimationFile` (M2-07) |
| 4 (SpriteType) | `0x0c` u32 → 0xc-byte SpriteMetrics `{u16 u, v, w, h, clutX, clutY}` |

- **The u32 at `0x10`** (between `tmdOffset` and `0x14`) is read by the CTMD discriminator/transformer as `nextOffset` (validation: 0, or within `[containerOffset, size)`; for type 1 with `nextOffset == 0` the container region ends at `readInt(0x14)` = `animOffset`). Its exact semantic (end-of-region vs next-part pointer, file-relative vs DEFF-relative) is **not fully established** — the CTMD pointer adjustments treat u16@0x10 and u16@0x14 as the two halves of it and shift them when `> containerOffset`. Registered as an open item (M2-10).
- Note: `undeff` (M2-03) splits parts into separate files, so after unpacking each DEFF part is its own file; the `0x10` pointer then references coordinates inside the *original* DEFF and is mostly stale (the CTMD discriminator's `nextOffset >= size` check is what keeps it safe).

## 5. CTMD — compressed TMD (source-verified)

LoD-specific compressed variant of TMD, flagged `flags & 0x2`. The unpack-time **CTMD converter** (`CtmdTransformer`, M2-03 §4 #25) rewrites qualifying DEFF parts in place; the runtime only ever sees plain TMDs.

### 5.1 CTMD file structure

Same header as TMD (flags with 0x2 set, nobj) and the same 0x1c object table, but the three data sections are compressed, and all table pointers are **relative to `0xc`** (the CTMD data start, i.e. after the TmdWithId id + the 8-byte TmdHeader when inlined in a CContainer at `tmdPtr == 0xc`):

| section | plain TMD | CTMD |
|---|---|---|
| vertices | 8 B/vert: 3×i16 + pad | 4 B/vert: `{x_lo, y_lo, z_lo, hiIndex}` + shared **hi-byte quad table**: `hi = u8[hiIndex]` per channel; `coord = lo + (hi << 8)` → **12-bit** coordinates |
| normals | 8 B/normal: 3×i16(×4096) + pad | 4 B/normal: 32-bit pack, 10 bits/axis: `x = packed << 20 >> 19 & 0xffff_fff8`, `y = packed << 10 >> 19 & …`, `z = packed >> 19 & …` (sign-extended to multiple of 8) |
| primitives | run of raw packets (§1.2) | run of **bit-stream** packets (below) |

### 5.2 CTMD primitive bit-stream (decompiled from ROM, `@Method(0x800de840L)`)

Each run: [header u32][sequential bit-stream data]. Header: low 16 = **number of primitives in the run**; bit 0x8000 must be set (packed); `packetSize = primitivePacketSize(header & ~0x8000)` (same table as §1.2).

Decoding state (per run, `CtmdUnpackingData50`): ring buffer `_10[32]` of u16, write pos `_00` (masked `& 0x1f`), read start `_04`, counter `_0c`, control byte `_08`.

```
while _0c < words_to_unpack:
  if (_08 & 0x100) == 0: _08 = stream.byte | 0xff00      # load next 8-bit control byte
  if (_08 & 0x1) != 0:                                    # literal
    _10[_00] = stream.u16; _00 &= 0x1f; _0c++
  else:                                                   # copy run
    a1 = stream.byte
    length = (a1 >>> 5) + 1                               # 1..8 copies
    for i in length:
      _10[_00] = _10[a1 & 0x1f]                           # ring-distance copy
      _00 &= 0x1f; a1++                                   # distance increases per copy
    _0c += length
  _08 >>= 1
# final: emit ring[_04], ring[_04+1], … for words_to_unpack entries (mod 32)
```

- This is a faithful port of the original PSX unpacker (LAB_800deXXX labels) — layer: **original behavior**.
- Mixed packed/unpacked runs are **unsupported**: `throw RuntimeException("Need to support mixed files")` (`Inferred`: no retail CTMD file mixes them).

### 5.3 Output TMD and container rewriting (source-verified)

- Output TMD: `[u32 id = CTMD magic][u32 flags & ~0x2][u32 nobj][obj table][primitives][vertices][normals]`; obj-table pointers rewritten **table-relative** (`ptr - 0xc`); primitive run headers: `header & ~0x8000 | packetSize/4 << 8` (bits 8-23; harmless because `updateTmdPacketIlen` rewrites low-16 run lengths at load, §1.2).
- Container/DEFF pointer fixes on in-place rewrite:
  - DEFF part u16@0x10 and (if type != 3) u16@0x14: `+ containerSizeDelta` when `> containerOffset`;
  - CContainer u32@+4 and u32@+8 (the two subfile pointers): `+ tmdSizeDelta` when `> 0xc`;
  - bytes after the CTMD inside the CContainer, and bytes after the CContainer inside the DEFF part, are shifted.
- CTMD discriminator (M2-03 #25) — a file qualifies iff: node flag `DEFF`, size ≥ 0x14, part type byte ∈ 0..4, `containerOffset = readInt(0xc)` in `[0x10, size)`, `nextOffset = readInt(0x10)` valid (0, or `[containerOffset, size)`), `readInt(containerOffset) == 0xc`, byte `@containerOffset+0xc == 0x41` (TmdWithId id low byte), and TMD flags `& 0x2` at `containerOffset+0x10`.

## 6. psx-sdk / reusability notes (proposed-layer, no design decisions)

- Reusable PSX side: TMD/TmdWithId parsing (Sony-standard family — the fixp/CTMD flags are LoD usage), the GPRIM primitive-id semantics (PSX GPU primitive commands), the 12-bit/10-bit compression is LoD-specific.
- LoD-specific side: CContainer, DEFF part types, CTMD bit-stream (though the bit-stream *algorithm* is original PSX code — reusable if a future reimplementation wants to *read* retail CTMDs before conversion), the run-length rewrite (`updateTmdPacketIlen`), and all the "type 7" / fixp / mixed-file gaps.
- Layer-1 faithfulness question for M4: does the original PSX GPU accept TMD primitive streams directly (via a TMD→GPU-command converter in the original runtime), or does it render the PSX GPU command stream generated at load? The reference's `legacyMode 1` TMD renderer vs the PSX GPU model coexistence is tracked as **Q-37** (M2-10 §6).
