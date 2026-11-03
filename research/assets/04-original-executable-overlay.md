# M2-04 — Original Executable, Embedded/Overlay Assets

Phase M2 deliverable 4 of 9. Reference: Severed-Chains @ `0c08c679b` — `Unpacker.engineOverlayExtractor`/`lodEngineExtractor`/`smapAssetExtractor`/`extractBtldDataTransformer`, `legend/game/Scus94491BpeSegment.java` (decompiled ROM segment), runtime consumers in `SMap`/`Battle`. All claims **source-verified** unless noted; `Inferred` marks interpretation.

## 1. The main executable `SCUS_944.91` (source-verified)

- The main disc contains the game's executable under the ISO9660 name **`SCUS_944.91`** (PSX naming: 9-char program name + 2-digit file number — format-corroborated as the standard PSX EXE naming; no$psx "PS-X EXE").
- The reference never parses the PSX EXE header (`PS-X EXE`, 0x800-byte header: entry point, program size, version fields); it treats the file as an **opaque byte stream** and slices it at fixed offsets. Every offset below is therefore empirical (source-verified in the reference; the *reason* for each offset is `Inferred`).
- The decompiled ROM code is organized as `Scus94491BpeSegment` (plus `Scus94491BpeSegment_800b` for globals) — i.e. the reference's code layer **is** a decompilation of this executable (the class name ties the BPE-compressed `SCUS_944.91` to the decompiled segment). Which physical disc carries `SCUS_944.91` is not determinable from source (the ISO9660 tree is only known at runtime) — registered as an open item (needs disc images).

### 1.1 Extraction chain (source-verified)

1. `engineOverlayDiscriminator` (`Unpacker.java:640-642`): matches any node whose **path segment** is `SCUS_944.91` (in practice the root-level file), once (global `flags` guard).
2. `engineOverlayExtractor` (`Unpacker.java:644-648`): keeps the original file in the tree and extracts
   ```
   lod_engine = SCUS_944.91[0xa00 .. 0xa00 + 0x36228)      (0x36228 = 221,736 bytes)
   ```
   (comment: "Spells, XP, and TIMs from lod_engine").
3. `lodEngineExtractor` (`Unpacker.java:1352-1360`, discriminator: path == `lod_engine`) extracts from `lod_engine`:
   | asset | offset in lod_engine | size | kind |
   |---|---|---|---|
   | `shadow.ctmd` | `0x3d0` | `0x14c` (300) | CTMD (compressed TMD, M2-05 §5) — the submap shadow blob model |
   | `shadow.anim` | `0x51c` | `0x28` (40) | TmdAnimationFile (M2-07): header 0x10 + 3 × 0xc keyframes (1 part, 6 total frames at 2 frames/keyframe) — *frame count `Inferred` from layout* |
   | `shadow.tim` | `0x544` | TIM-sized (`getTimSize`) | 4bpp CLUT TIM |
   | `font.tim` | `0xb6744` | TIM-sized (`getTimSize`) | system font atlas TIM |

### 1.2 Relationship to the decompiled segment (Inferred/Unresolved)

- The decompilation covers the executable's **code**; the extracted slices are **embedded data** regions. The exact code/data partition of `SCUS_944.91` (where code ends, where data begins, what occupies `[0, 0xa00)` and `[0xa00+0x36228, end)`) is **not established** by the reference — only the extracted slices are. The original executable is much larger than the extracted region; unextracted regions are presumed code and unextracted data (Unresolved; exhaustiveness cannot be claimed without the binary + a complete symbol table).
- The reference *keeps* `SCUS_944.91` in the `files/` tree (transformer re-adds the original node) — no decompiled code path loads it; retained for mod/tool access (`Inferred`).

## 2. Overlay files (`OVL/*.OV_`) (source-verified)

The disc contains overlay files under `OVL/`; the reference extracts embedded asset data from two of them and keeps the originals in the tree. The original runtime loaded these overlays into PSX memory at appropriate times (standard PSX OVL behavior — `Inferred` mechanism; the *contents* are source-verified):

### 2.1 `OVL/SMAP.OV_` — submap engine overlay (smapAssetExtractor, `Unpacker.java:710-730`)

Once (flag `"SMAP"`). Keeps the file and extracts:

| asset | offset | size | runtime consumer |
|---|---|---|---|
| `SUBMAP/savepoint` | `0x10694` | `0x904` (2308) | **no decompiled consumer found** — savepoint data for the original submap code (`Inferred`); retained for tooling |
| `SUBMAP/darker_shadow.tim` | `0x100b4` | TIM-sized | `SMap.java:2815` (hardcoded load) |
| `SUBMAP/alert.tim` | `0x10214` | TIM-sized | `SMap.java:3341` (hardcoded load) |
| `SUBMAP/big_arrow.tim` | `0x10f98` | TIM-sized | `SMap.miscTextures_800f9eb0[0]` |
| `SUBMAP/small_arrow.tim` | `0x115d8` | TIM-sized | `miscTextures[1]` |
| `SUBMAP/savepoint.tim` | `0x11858` | TIM-sized | `miscTextures[2]` |
| `SUBMAP/800d8520.tim` | `0x11e98` | TIM-sized | `miscTextures[3]` |
| `SUBMAP/800d85e0.tim` | `0x11f58` | TIM-sized | `miscTextures[4]` |
| `SUBMAP/savepoint_big_circle.tim` | `0x12098` | TIM-sized | `miscTextures[5]` |
| `SUBMAP/dust.tim` | `0x122d8` | TIM-sized | `miscTextures[6]` |
| `SUBMAP/left_foot.tim` | `0x12518` | TIM-sized | `miscTextures[7]` |
| `SUBMAP/right_foot.tim` | `0x12658` | TIM-sized | `miscTextures[8]` |
| `SUBMAP/smoke_1.tim` | `0x12798` | TIM-sized | `miscTextures[9]` |
| `SUBMAP/smoke_2.tim` | `0x129d8` | TIM-sized | `miscTextures[10]` |

- `SMap.miscTextures_800f9eb0` (`SMap.java:405-417`) is the ROM's 11-entry texture name table; `SMap.java:5756` loads `SUBMAP/<name>` via `Loader.loadFileSync`. The two hardcoded loads (darker_shadow, alert) + 11 table entries = **all 13 TIMs are runtime-consumed** (source-verified).
- The extracted TIMs are 4bpp CLUT TIMs sized by `getTimSize` (M2-06 §2); they live contiguously inside the overlay: `0x100b4` (darker_shadow) … `0x129d8`+size (smoke_2), interleaved with the savepoint blob at `0x10694`.

### 2.2 `OVL/S_BTLD.OV_` — battle overlay (extractBtldDataTransformer, `Unpacker.java:1048-1055`)

Once (path flag). Keeps the file and extracts:

| asset | slice | runtime consumer |
|---|---|---|
| `player_combat_script` | `[0x4, 0x4+0x68d4)` (0x68d4 bytes) | `Battle.java:9301`: `playerBattleScript_800c66fc = new ScriptFile("player_combat_script", Loader.loadFileSync("player_combat_script").getBytes())` — the player battle AI script (M3) |
| `encounters` | `[0x68d8, 0x68d8+0x7000)` (0x7000 = 28,672 bytes) | **no decompiled consumer found** — battle encounter data table; exposed for modding (`EncounterRegistry` uses `RegistryId("lod", "encounters")` for mod-registry purposes only) |

- Note the extractor's flag check for `player_combat_script`'s *consumer* transformer (`Player script damage caps`, M2-03 #26) requires `readInt(4) != MrgArchive.MAGIC` — a guard so the raw script (which starts with 4 magic bytes) isn't mistaken for an MRG by coincidence.

### 2.3 Why the originals are kept (Inferred)

The reference re-adds each transformed source node (`transformations.addNode(node)`) so `files/OVL/SMAP.OV_`, `files/OVL/S_BTLD.OV_`, and `files/SCUS_944.91` exist alongside the extracted assets. No decompiled runtime code reads the raw overlay files; the likely purpose is mod/tool access (M8 modding layer) — `Inferred`.

## 3. Runtime asset consumers at startup (source-verified)

`Scus94491BpeSegment.gameInit()` `@Method(0x800e5d64L)` (`Scus94491BpeSegment.java:605-629`) — the ROM's initialization sequence (cross-checked with R1 lifecycle/execution docs):

1. GPU ordering-table setup: `orderingTableBits_1f8003c0 = 14`, `zShift = 0`, `orderingTableSize = 0x4000`, `zMax = 0x3ffe` (`GPU.updateOrderingTableSize`).
2. `setDrawOffset()`, clear-colour globals to 0, `InitGeom()`, `setProjectionPlaneDistance(640)`, `initSound()`.
3. `vsyncMode_8007a3b8 = 2` (30 Hz start; R1 F-01: transitions force 30 Hz), `tickCount_800bb0fc = 0`.
4. **`loadSystemFont()` `@0x800e6524`** (`Scus94491BpeSegment.java:647-658`):
   - `font.tim` → `Tim` → image uploaded at VRAM rect **(832, 424, 64, 56)**, CLUT at **(832, 422, 32, 1)** via `GPU.uploadData15` (modern VRAM path, not the PSX GPU command model).
   - This is the **in-game system font** used by `Text.renderText` for all retail text (M2-08 §2).
5. `SCRIPTS.clear()`.
6. **`loadShadow()` `@0x800e6998`** (`Scus94491BpeSegment.java:660-678`):
   - `submapId_800bd808 = 0`;
   - `shadow.tim` → `Tim.uploadToGpu()` (image + CLUT via the TIM's own rects);
   - `shadow.ctmd` → **`CContainer`** (M2-05 §3) → `TmdWithId` → TMD (post-unpack: a plain TMD, since the CTMD transformer already unpacked it — the runtime throws on the CTMD flag, M2-05 §5);
   - `shadow.anim` → `TmdAnimationFile`;
   - `loadModelAndAnimation(shadowModel_800bda10, container, animation)`; rotation zeroed, `uvAdjustments_9d = NONE`, `shadowType_cc = 0`.
   - The shadow model is the submap character-shadow blob (rendered under characters).
7. `clearRenderables()`, `initFmvs()` (`_800bf0cf = 0`).

`preload()` `@0x800e6184`: `drgnBinIndex_800bc058 = 1`, `loadMenuSounds()`, `resizeDisplay(320, 240)`, `vsyncMode = 2`, `loadBasicUiTexturesAndSomethingElse()`.

## 4. Embedded-asset inventory and exhaustiveness (Inferred/Unresolved)

Extracted embedded assets (complete list of what the reference extracts):

| Container | Assets |
|---|---|
| `SCUS_944.91` → `lod_engine` | `shadow.ctmd`, `shadow.anim`, `shadow.tim`, `font.tim` |
| `OVL/SMAP.OV_` | `SUBMAP/savepoint` + 13 `SUBMAP/*.tim` |
| `OVL/S_BTLD.OV_` | `player_combat_script`, `encounters` |

- **Exhaustiveness cannot be established**: the reference extracts only what it (or its modding ecosystem) needs. The original executable and overlays certainly contain more data (dialogue string tables, script data, SFX tables, etc. — the bulk of which the ROM references by virtual `DRGN*` file paths rather than by embedded copies). Any claim that "these are all the embedded assets" is **not** supportable from source — registered as an open item.
- The original game's runtime loaded overlays into memory; in the reference, the decompiled code inlines most of that behavior (font/shadow load at init, battle script load at battle start), so the extracted files serve (a) the decompiled code's loader (font/shadow/battle script) and (b) mod tooling (encounters, savepoint data, overlays).

## 5. psx-sdk / reusability notes (proposed-layer, no design decisions)

- The PSX EXE container itself (header parsing) is standard PSX work (format-corroborated: no$psx "PS-X EXE"); the LoD-specific part is purely the *fixed-offset data map* (§1.1, §2), which belongs to the game layer, not a reusable PSX crate.
- `getTimSize`-based extraction (TIM self-sizing, M2-06) is the generic mechanism for the four TIMs — a reusable TIM reader is the platform-side dependency.
