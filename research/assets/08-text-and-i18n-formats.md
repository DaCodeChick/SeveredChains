# M2-08 — Text, Fonts, i18n, and Adjacent Data Blobs

Phase M2 deliverable 8 of 9. Reference: Severed-Chains @ `0c08c679b` — `core/font/*` (JSON font pipeline), `Scus94491BpeSegment.loadSystemFont` (retail system font), `SaveCardTextureExtractor` (portrait/spirit pixel extraction), `patches/` (mod metadata CSVs), I18n usage; Dig-Dabas @ `4c1fbfebced` — `DabasVars.loadRom` (DABAS.BIN fixed-offset blob). All claims **source-verified** unless noted. Validation categories per `AGENTS.md`: **source-verified** (reference code), **format-corroborated** (independent spec/implementation), **asset-validated** (requires real disc/asset bytes — none yet; no such claims in this document).

## 1. In-game text: where it lives (source-verified observation)

- The original game's dialogue/UI strings are **not** stored in a dedicated text file in the extracted tree — they are embedded in **DRGN scripts** (the `SECT/DRGN*.BIN` file system, M2-02/03) as script data. The decompiled ROM's text rendering (`legend/game/Text.renderText`, used e.g. by `Fmv.displaySkipText`) renders glyphs from the **system font TIM** (M2-06 §5, M2-04 §3 step 4) against glyph indices supplied by scripts/mods.
- Consequence: the "in-game text format" is a **property of the script format** (M3 scope: `scripts/03-binary-formats.md`), not a standalone M2 format. What M2 documents here is the *glyph presentation* side (fonts) and the *reference-side* i18n machinery.
- The original PSX text mode is not a PSX hardware text mode — it is bitmap glyph blitting from a font TIM (source-verified: `GPU.uploadData15` of `font.tim` + `renderText` glyph indexing; `Inferred` that this matches retail behavior since the font rect (832,424,64,56) and CLUT (832,422,32,1) come from the decompiled ROM).

## 2. Reference-side UI fonts: JSON + atlas PNG (source-verified)

`core/font/FontManager.java` + `Font.java` + `Glyph.java` + `RetailFontConfigEntry.java`:

- The reference bundles **JSON glyph-description fonts** in `gfx/fonts/*.json` (repo-shipped, reference-invented representation — **not** an original LoD format):
  ```json
  {
    "name": "<font name>",
    "glyphs": {
      "<char>" or "0x<unicode>": {
        "texU": 0, "texV": 0, "texW": 0, "texH": 0,   // texture-atlas rect (the glyph image)
        "x": 0, "y": 0, "w": 0, "h": 0,               // metrics/advance
        "colour": true|false
      }
    }
  }
  ```
- Keys: single characters, or `"0x…"` hex unicode (parsed with `Integer.parseUnsignedInt(key.substring(2), 16)`).
- The glyph image itself is loaded from a **PNG atlas** (the `Texture` tied to the `Font`; `texU/texV/texW/texH` index it) — `Inferred` from `Glyph` usage in the render path (atlas PNGs ship alongside the JSON in `gfx/fonts/`).
- `RetailFontConfigEntry` (config `UserInterface`): user-selectable font, default **`smooth.json`**, chosen from all `gfx/fonts/*.json`.
- Layer classification: **reference-implementation feature** (proposed design for the UI), not original game behavior. A future reimplementation's UI font format is a design decision (M10), constrained only by: per-glyph atlas rect + metrics + colour flag.

## 3. Save-card portraits/spirits (source-verified)

`SaveCardTextureExtractor` (M2-03 #24) converts the retail save-card texture file `SECT/DRGN0.BIN/6665` (3 concatenated 4bpp+CLUT TIMs, M2-06 §5) into PNGs:

- **9 character portraits** (48×48 each): the *third* TIM holds 9×48 portrait rows repeated per colour channel (raw layout `432×48`, 3 channels of 144-wide rows); three CLUTs (`palettes[0..2]`) are applied and channel-combined into RGBA; output `characters/<name>/portrait.png` (dart, lavitz, shana, rose, haschel, albert, meru, kongol, miranda).
- **8 dragoon spirit icons** (16×16 slots, content 11×9): the *first* TIM, source rects `(130,118,11,9)` ×7 (palettes 4..10) and `(242,118,11,9)` (palette 11, Divine); output `goods/<spirit>/icon.png` (red/jade/silver/dark/violet/blue/gold/divine).
- **`retail_atlas.png`** (512×64): the combined output atlas (transparency: any non-zero RGB → opaque).
- The extraction includes a documented oddity: "the lower 32 pixels of the right side of Miranda's blue channel being split up in the last row" is remapped (`appliedY -= 16; appliedX += 16`) — source-verified workaround for a retail layout quirk.
- Runtime use: these PNGs feed the reference's save-screen/mod portrait rendering; the **original** game renders from the raw TIMs on-card (`Inferred`).

## 4. DABAS.BIN (Dig-Dabas mod asset) (source-verified)

Dig-Dabas is a **mod** (a PSX-native game by the LoD modding community), not an original LoD asset; its data blob is relevant to the asset-pipeline research because it is the only *other* PSX-format data source in the reference repositories.

- Loaded as a raw disc image: `Main.java:58` → `hardware.start(Path.of("game.rom"))` — the user places the DABAS disc image as `game.rom`; `DabasVars.loadRom` reads it as one `FileData`.
- **`DABAS.BIN` is a fixed-offset blob, not a self-describing archive**: `loadRom` reads every table at hardcoded offsets, e.g. (source-verified sample):
  | offset | table |
  |---|---|
  | `0x840c` | `audioTimerReloadValues` (u16 array) |
  | `0x8524` | `timerSpeeds` (u16 array) |
  | `0x859c` | `playerVolumes` (u8 array) |
  | `0x85e8` | `backgroundA` (u8 array) |
  | `0x8968` | `backgroundB` (u8 array) |
  | `0x8ce8` | `speechBubbleTextPixels` (i32, row-major) |
  | `0x8f08` / `0x9148` | `speechBubbleBackgroundMaskOff` / `On` (i32, row-major) |
  | `0x9388` | `tileAttributeActions` (u8) |
  | `0x93f8` | `mapTileTypes` (u8) |
  | … | (cave tiles, sprite data, etc. — the full table is the offset map in `DabasVars.loadRom`) |
- Layer: **mod, not original**. For the reimplementation: DABAS.BIN support (if desired) is a fixed-offset loader keyed to the DABAS disc layout — a game-layer concern, no PSX-format content beyond "it's a disc image read at fixed offsets".

## 5. Mod metadata formats (source-verified, supplementary)

The reference's `patches/` directory (M2-03) also carries mod metadata, relevant to the i18n/modding topics:

- `patches/scripts.csv`, `patches/meta/…` (Mod-Loader territory, M8): CSV metadata describing script patches/mods. Format: CSV (ASCII) with per-mod rows — details belong to M8 (`modding`), noted here for completeness.
- `patches/replacements/chester.tim`: a **replacement asset** (TIM, M2-06) — the only shipped content replacement (M2-03 B2).
- i18n at runtime: `I18n.translate("unpacker.…")` etc. — reference-UI strings in mod-provided/localized resources (`legend/lodmod` I18n; M8 scope). The original game's text language is fixed by the disc region (`Inferred`).

## 6. Open items (M2-10 register)

- The **original retail text encoding** (how strings are stored inside DRGN scripts, glyph index → font TIM cell mapping, character set/encoding): M3 (script binary formats) — cross-ref.
- Font TIM glyph cell layout (64×56 at (832,424): cell grid, which row/column = which glyph, whether the 32×1 CLUT row implies a 2-colour… — `Inferred` 32-colour 5-6-5 CLUT): needs asset validation.
- PNG atlas ↔ JSON glyph correspondence (atlas size, cell packing): reference-bundled files are inspectable without discs (repo assets) — can be asset-validated against the repo's own `gfx/fonts/` content (not original-game assets).
