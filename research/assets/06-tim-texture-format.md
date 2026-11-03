# M2-06 — TIM Texture Format, PXL Sheets, MCQ Strips

Phase M2 deliverable 6 of 9. Reference: Severed-Chains @ `0c08c679b` — `legend/game/tim/Tim.java` (runtime reader), `Unpacker.getTimSize` (unpacker sizer), `SubmapPxlTransformer.convertPxlSegment` (TIM **writer** — the authoritative layout reference), `legend/game/types/McqHeader.java` + `legend/core/renderer/McqBuilder.java`, runtime consumers (`Scus94491BpeSegment.loadSystemFont`, `SaveCardTextureExtractor`, `SMap`, `Battle`, `GameOver`, `WMap`). All claims **source-verified** unless noted; no disc images (no `asset-validated` claims). Layer note: TIM is a PSX-standard texture format (format-corroborated: Sony TIM/PXL/CLT family, no$psx "PSXSPX"); the LoD TIM header and the no-CLUT corner case below need asset validation.

## 1. TIM layout (source-verified; CLUT case fully pinned by the writer)

`Tim.java` (runtime) + `SubmapPxlTransformer.convertPxlSegment` (writer) agree on this layout:

### 1.1 With CLUT (`flags & 0x8` set)

```
0x00  u32  id          generated PXL TIMs write 0x10; retail value unknown (asset-validated only)
0x04  u32  flags       low 3 bits = bpp (Bpp.of: 0=4bpp, 1=8bpp, 2=15bpp, 3=24bpp)
                        bit 0x8 = has CLUT
0x08  u32  F           single quantity; clut data size = F - 0xc; image segment at F + 8
0x0c  u16  clut rect x, y, w, h   (4 × u16)
0x14  F - 0xc bytes    CLUT data (2 bytes per colour: 5-6-5)
F + 8  image segment:
  +0x0  u32  segSize    = 0xc + image data size
  +0x4  u16  image rect x, y, w, h
  +0xc  image data      (segSize - 0xc bytes)
```

Total file size = `0x14 + F + w*h*bppBytesPerPixel/8` (the PXL writer builds exactly this: `4+4+4+8+clutSize+4+8+w*h*2` for 4bpp).

Cross-checks:
- `Tim.getClutData()` = `slice(0x14, readInt(0x8) - 0xc)` ✓
- `Tim.getImageDataOffset()` = `readUShort(0x8)` = F (low word; assumes `F < 0x10000`)
- `Tim.getImageRect()` = `readRect(F + 0xc)` = image-segment `+0x4` ✓ (since segment start = F + 8)
- `Tim.getImageOffset()` = `F + 0x14` = image-segment `+0xc` ✓
- `Tim.getImageData()` = `slice(F + 0x14, readInt(F + 0x8) - 0xc)` = `slice(data start, segSize - 0xc)` ✓
- `Unpacker.getTimSize` (no-CLUT branch aside) = `slice(0, (F + 8) + readUShort(F + 8))` = full size when `segSize < 0x10000` ✓
- `SaveCardTextureExtractor` advances through concatenated TIMs by `0x14 + getImageDataOffset() + w*h*2` = `0x14 + F + dataSize` ✓

### 1.2 Without CLUT — **ambiguous, open item** (source-verified conflict)

- `Tim.java` no-CLUT path: `getImageDataOffset() == 0`; image rect = `readRect(0xc)`; image data = `slice(0x14, readInt(0x8) - 0xc)` — i.e. it expects a **flat** layout: `[id][flags][u32 @0x8 = 0xc + dataSize][rect @0xc][data @0x14]`.
- `Unpacker.getTimSize` no-CLUT branch: `imageAddr = 0x14`, size = `0x14 + readUShort(0x14)` — i.e. it expects the image segment (with a u32 size field) to start **at 0x14**, making `readUShort(0x14)` the low word of the segment size — a different layout than `Tim.java`'s.
- The two reader paths **disagree** for no-CLUT files; all LoD TIMs the reference *generates* have CLUT (PXL conversion writes `flags = 0x8`), and the known retail TIMs (system font, shadow, submap, save card) are 4bpp+CLUT (`Inferred`), so the no-CLUT path may be untested/defensive in the reference. **Whether retail no-CLUT TIMs exist, and which layout is correct, is asset-validated-only** — registered (M2-10).

## 2. GPU upload (source-verified)

- `Tim.uploadToGpu()`: `GPU.uploadData15(imageRect, imageData)` then, if CLUT, `GPU.uploadData15(clutRect, clutData)` — uploads go through the **modern** VRAM path (15-bit colours), not the PSX GPU command model (cross-ref M2-10 §6 / Q-37).
- 15-bit upload for 4bpp data: `GPU.uploadData15` expands via the CLUT (cross-ref R1 psx doc for the Bpp handling).

## 3. PXL sheets (submap texture sheets) (source-verified)

`SubmapPxlTransformer` (M2-03 branch transformer): submap cut asset MRGs (M2-02 §5.1) end with **three PXL sheet TIMs** (large 4bpp+CLUT sheets holding all submap object textures).

- Per sobj (33-stride, M2-02 §5.1): the sobj's texture comes from a **16×128** slot in one of the three sheets (or **32×128** double-width high-res slots 17/18 for the two high-res sobjs identified by the script MRG's final 8-byte file, stored −1 — `getHighResTextureIndices`).
- Slot table `textureSlotRects[19]` (source-verified): 16 standard 16×128 slots at the listed VRAM-ish sheet coords (608,256 / 624,256 / 576,384 / … / 720,256), slot 16 = (1008,256,16,128) "Submap cut texture (not used here)", slots 17/18 = double-width (576,256,32,128) / (640,256,32,128).
- Per-sobj conversion (`convertPxlSegment`):
  - new 4bpp+CLUT TIM: image `tileRect.w × (tileRect.h - 16)`, CLUT `clutW × 16` (16 or 32 columns — "high-res aren't actually 32 wide, there are two columns of CLUTs", source comment) written at CLUT rect `(0, tileRect.h - 16)`;
  - image rows copied from the sheet rows `[tileY, tileY + h)`; CLUT rows copied from sheet rows `[tileY + h, tileY + h + 16)` (the 16 rows **below** the texture in the sheet are the palette);
  - output written to `<cut>/textures/<sobj>` + `textures/mrg` index (M2-02 §5.1 semantics: virtual→source mapping, `"";0` for missing + warning).
- **Lenus 2 face CLUT fix** (GH#1845, M2-03 P1): for `SECT/DRGN22.BIN/980` sobj 5, a hardcoded 32-entry (64-byte) CLUT is written at TIM offset `0x14`.
- Sobj 16 pointing to the (1008,256) "submap animated overlay" slot produces a warning + null texture file (source comment: "unsure if there's anything else that would get here").

## 4. MCQ strips (source-verified)

`McqHeader` (0x2c bytes) — background strip textures (battle stage, world map, game over screens):

```
0x00  u32  magic      0x151_434d (MAGIC_1, LE bytes 4d 43 4d 01) or 0x251_434d (MAGIC_2, … 02)
0x04  u32  imageDataOffset
0x08  i16  vramWidth
0x0a  i16  vramHeight
0x0c  u16  clutX
0x0e  u16  clutY
0x10  u16  u          texture u within VRAM (×4 for 4bpp: u = u*4 & 0xfc)
0x12  u16  v          texture v (vramY = v & 0x100; v wraps every 16: v = (v+16) & 0xf0)
0x14  u16  screenWidth
0x16  u16  screenHeight
0x18  8B   colour0    (3-byte colour × … — readColour, 8 bytes)
0x20  8B   colour1
0x28  i16  screenOffsetX   (MAGIC_2 only: added to chunk origin)
0x2a  i16  screenOffsetY
imageData at imageDataOffset (4bpp; CLUT implied — 4bpp strip)
```

- `McqBuilder.build()` renders the strip as **16×16 quads** in chunks (`width/16 × height/16`), with PSX-VRAM tiling arithmetic: `vramX = u & 0x3c0` (wraps 960 = 1024−64), `clutY = (clutY+1) & 0xff` with `clutX += 16` on wrap — i.e. the strip is tiled across a 4bpp VRAM region with 16-colour CLUT columns.
- Consumers: `Battle.loadStageMcq` (`Battle.java:2702`, magic checked at 2659 — MAGIC_1 only), `WMap.java:1767`, `GameOver.java:70`, `Scus94491BpeSegment.java:408` (accepts both magics).
- Battle stage MCQs load via `Battle.java:2701-2702` from the battle asset directory (`files.get(1)`, virtual-size existence check at 2701 — M2-01 §6.2).
- The "VRAM height 256 expectation" noted during recon corresponds to `vramHeight_0a` + the 256-based v wrap (source-verified usage; the exact constraint is `Inferred`).

## 5. Known retail TIM uses (source-verified inventory)

| TIM | Source | Usage |
|---|---|---|
| `font.tim` (64×56, CLUT 32×1) | `lod_engine` (M2-04) | system in-game font at VRAM (832,424) |
| `shadow.tim` | `lod_engine` | submap shadow blob |
| `SUBMAP/*.tim` ×13 | `SMAP.OV_` (M2-04) | submap UI/indicators/feet/smoke |
| `<cut>/textures/<sobj>` (generated) | PXL sheets (this doc §3) | per-object submap textures |
| `SECT/DRGN0.BIN/6665` (3 concatenated 4bpp+CLUT TIMs) | DRGN0 MRG | save-card portrait/spirit textures (M2-08 §3) |
| `chester.tim` (replacement) | `patches/replacements/` | Bale library NPC face (M2-03 B2) |

## 6. Open items (M2-10 register)

- No-CLUT layout conflict (§1.2) — needs asset validation.
- Retail TIM `id` field values (generated = 0x10) — needs asset validation.
- Whether any retail TIM uses 8/15/24bpp — all reference-observed uses are 4bpp+CLUT (`Inferred`).
- PSX-standard TIM header comparison (Sony TIM `TIM_`-style id vs LoD u32 id) — the reference's TIM header does not match the no$psx-documented standard TIM header byte-for-byte; `Inferred` LoD variant, needs a documented cross-check (M4).
