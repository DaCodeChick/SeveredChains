# M5-03 — UI, Text, and Presentation Geometry

Status: Complete (M5, 2026-11-05). Reference: Severed-Chains @ `0c08c679b`, read-only. This document covers the two 2D presentation systems the game presents on top of the 3D world — (a) the decompiled **menu/UI renderable** system (`Menus`/`SItem`/`Renderable58`, driven by the retail `UiFile` glyph-strip assets) and (b) the **text** system (textboxes, `renderText`, the JSON+PNG font stack) — plus full-screen fade/lightning effects, the menu screen stack, and the presentation geometry (the "×4 depth domain", the −512 UI VRAM region, the 384→368 menu display, and the equal-depth 2D census that feeds Q-71). Companion documents: M5-01 (render engine, pools and sorters), M5-02 (TMD pipeline), M5-04 (post-processing/widescreen/FMV). Format-level facts (TIM, DRGN, UiFile bytes) are M2's territory (`research/assets/06`, `08`) and are cited, not re-derived.

Evidence vocabulary: **source-verified** (established from pinned reference source), **experimentally verified**, **format-corroborated**, **asset-validated** (*none exists — no such claim is made*), **Inferred**, **Unresolved**. Layers per `research/psx/01-compatibility-boundaries.md`: (1) PSX platform, (3) LoD-specific, (4) reference-specific. No public documentation was consulted (network-restricted); layer-1 attributions remain `Inferred`/`Unresolved` and travel to the M10 evidence-admission worklist.

## 1. The two 2D presentation surfaces (layer model)

The decompiled ROM draws all 2D content as textured/coloured quads in a 320×240 (sometimes 384×240) model space; there is no PSX hardware text mode and no sprite hardware — `Inferred` layer-1, consistent with M2-08 §1. In the reference, 2D content reaches the screen through **two independent surfaces** (source-verified):

1. **The modern ortho pool** — `RENDERER.queueOrthoModel(obj, transforms, QueuedModelStandard.class)` (139 call sites across ~60 files). Queued models are sorted and rendered into the render FBO by the render engine (M5-01 §5/§6); visible in `legacyMode == 0` (the release default). This is where UI renderables, font glyphs, textbox backgrounds/borders, fade/lightning quads, battle sprite effects, particles, map markers, and MOD icons are drawn.
2. **The GPU-model command queue** — `GPU.queueCommand(z, GpuCommand)` (M4-01). Commands are consumed at the *end* of `gameLoop` by `GPU.endFrame()` → `Gpu.tick()` (`Gpu.java:133-135`, `:174-200`): display presentation for `legacyMode == 1` (display buffer → `drawDisplay()`) and `legacyMode == 2` (full VRAM → `drawVram()`), then the z queues are traversed **far-z → near-z, LIFO within a z** and rasterized into the GPU model's drawing buffer. Source-verified that this traversal runs in *all* modes; **`Inferred`** that in `legacyMode == 0` its raster output is not presented (mode 0 presents the FBO — M5-01 §2.2 phase 8), i.e. GPU-queue-only effects are invisible in the default mode.

The GPU model's two display buffers are **separate 24-bit arrays**, not regions of the shared 1024×512 VRAM (source-verified, `Gpu.java:387-400`: `renderBuffers[i] = new VramTextureSingle(Bits_24, Rect4i(0, 0, w, h), new int[w*h])`). The shared `vram15`/`vram24` arrays hold *textures* (CPU uploads via `GPU.uploadData15`) and are surfaced as the `vramTexture15`/`vramTexture24` GL textures; `vramTexture15` is the texture the modern path binds for models without an explicit texture (M5-01 §9.2, `Gpu.useVramTexture()` → `Gpu.java:137-139`). UI textures therefore live in the shared VRAM array and are sampled through `vramTexture15` even in mode 0.

## 2. UI asset pipeline (source-verified)

### 2.1 Menu textures: DrgnFile 6665 → VRAM at −512

`SItem.menuAssetsLoaded` (0x800fc944, SItem.java:750-771) is fed by two synchronous DRGN loads:

- **DrgnFile 6665** — a TIM-bearing blob sliced five ways: character textures (`data.slice(0x83e0)`), menu textures (whole file), item textures (`data.slice(0x6200)`), plus two further slices (`0x10460`, `0x10580`). Each slice goes through `Menus.loadMenuTexture` (0x80022a94, Menus.java:123-138):
  ```
  imageRect.x -= 512;  GPU.uploadData15(imageRect, tim.getImageData());
  clutRect.x   -= 512; GPU.uploadData15(clutRect, tim.getClutData());
  ```
  (M2-08 §3 identifies 6665 as the save-card/menu texture file — 3 concatenated 4bpp+CLUT TIMs.)
- **DrgnFile 6666** — the `UiFile` glyph-strip metrics (§2.2). DrgnDir 5815 — menu music (`SItem.java:747`).

The **−512 offset** is the key layout fact: VRAM is 1024 wide, and `uploadData15` asserts `rectX + rectW <= 1024` without wrapping (`Gpu.java:208-214`), so the retail menu TIMs carry x-coordinates ≥ 512 in their header and are shifted left by 512 into the left half of VRAM (x 0..~512). Equivalently, the retail layout kept UI textures in the **+512 half** of VRAM. The render side applies the same shift: `Menus.renderUi` computes `tpageX = (tpage & 0b1111) * 64`, `clutX = (clut & 0b111111) * 16`, then `if (!renderable.useOriginalTpage) { tpageX -= 512; clutX -= 512; }` (Menus.java:377-381). `Renderable58.useOriginalTpage` exists because, per its in-source comment, "we moved UI textures to the region of VRAM where the render buffers used to be used, so they can now stay loaded" — some UIs (cited: `PostBattleScreen`) keep their own textures and pass a tpage that must not be re-shifted. The original retail VRAM partition (what occupied +512 in the PSX layout, presumably a display buffer) is `Inferred` layer-1; the constants themselves are source-verified. See Q-72.

Corroboration of the +512 region: the retail **system font TIM** is uploaded at (832, 424) 64×56 with CLUT (832, 422) 32×1 — `Scus94491BpeSegment.loadSystemFont` (0x800e6524, Scus94491BpeSegment.java:647-656) — i.e. 832 = 512 + 320, the same half. The textbox border/arrow quads bake `vramPos(896, 256)` = 512 + 384 and `clut(832|1008, 484)` (Text.java:243-266) — the same region (these are explicit `QuadBuilder` bakes with no −512 override, consistent with `useOriginalTpage` semantics: their file-decoded positions are already the +512-side coordinates).

### 2.2 UiFile: glyph-strip metrics → per-metric quads

`UiFile.fromFile` (UiFile.java) reads four `UiType` sections at fixed offsets: `uiElements_0000` (0x0000), `itemIcons_c6a4` (0xc6a4), `portraits_cfac` (0xcfac), `_d2d8` (0xd2d8). `UiType.fromFile`: entry count (u16 @0x6) + 8-byte entry records. `UiPart.fromFile`: `metricsIndicesIndex` (u16 @0x0) → index into a 4-byte entry-index pool → metrics offset; `metricsCount` (i32) + N × 0x14 `RenderableMetrics14`; `ticksPerFrame` (u8 @0x2). `RenderableMetrics14` bytes (RenderableMetrics14.java:45-62): u (u8), v (u8), x (u8), y (u8), **clut (u16, MSB = translucency enable)**, tpage (u16), width (u16), height (u16), widthScale (s16/0x1000), heightScale (s16/0x1000). (Byte-level M2 territory; cited here as the render contract.)

`SItem.buildUiRenderable` (SItem.java:1660-1685) converts each metric into one **unit quad** (posSize 1×1) in a per-`UiType` persistent `Obj`, baking:

- `uv(metrics.u_00, metrics.v_01)` + `uvSize(width, height)` — the glyph-strip cell on the UI page (u/v are 8-bit page coordinates);
- `clut((clut & 0x3f) * 16, (clut & 0x7fff) >>> 6)` — low 6 bits × 16 = CLUT x (16-entry groups), bits 6+ = CLUT y; the 0x8000 translucency bit is masked out;
- `vramPos((tpage & 0b1111) * 64, (tpage & 0b10000) != 0 ? 256 : 0)` — 4bpp/8bpp page x = low 4 bits × 64, y = top/bottom VRAM half;
- `bpp(Bpp.of(tpage >>> 7 & 0b11))` — tpage bits 7:8 select the bits-per-pixel (M5-01 §9.2 decode).

`metrics.vertexStart` records the quad's vertex index; `Menus.renderUi` later queues only that 4-vertex slice (`.vertices(metrics.vertexStart, 4)`). Each `UiType.obj` is `persistent = true` (SItem.java:762-769). This is the retail "glyph strip" model: an animated menu element is a *range of consecutive quads* (glyphs) in one shared texture page, advanced per tick.

### 2.3 Fonts: JSON + PNG atlas (layer 4), and the retail font TIM (retained, unsampled)

- **Modern font stack (layer 4)** — `FontManager` loads `gfx/fonts/*.json` (glyph table: `texU/texV/texW/texH` atlas rect + `x/y/w/h` metrics + `colour` flag; keys are single chars or `0x…` unicode) paired with a sibling **PNG atlas** (`Font.init()`: `Texture.png("Font <name>", <name>.png)`, `Font.java:31-57`). `Font` builds one persistent `Obj` of 24bpp quads (one per glyph) and `queueChar` queues the glyph's 4-vertex slice with the atlas texture (`Font.java:59-72`). Unknown glyphs render a **5×5 `lineBox` rectangle** (missing-glyph indicator, `Font.java:63-67`); unknown-glyph width defaults to 8. Text metrics: `lineWidth`/`textWidth` (per-glyph `w` accumulation), `textHeight = lines × 12`.
- **Which font** — `GameEngine.DEFAULT_FONT` is initialized to `gfx/fonts/default.json` (GameEngine.java:109) then **re-bound at boot from the `RETAIL_FONT_CONFIG` config** (GameEngine.java:250; `RetailFontConfigEntry` defaults to `smooth.json` and offers every `gfx/fonts/*.json` in a dropdown, RetailFontConfigEntry.java:18-52). So all in-game text in the reference is drawn from a user-selectable PNG-atlas font, **not** from the retail font TIM.
- **Retail font TIM (layer 3, retained)** — `loadSystemFont` (0x800e6524) still uploads `font.tim` to the VRAM model at (832,424) + CLUT (832,422) every boot (Scus94491BpeSegment.java:627). No modern text path samples it — `Text.renderText` (0x80029300) and `Text.renderTextboxText` (0x800282ac) both go through `Font.queueChar`. **This corrects M2-08 §1**, which stated that `Text.renderText` "renders glyphs from the system font TIM": in the pinned reference, `renderText` only uses the configured JSON font; the font TIM upload is a retained original-path artifact (the *original* text quads presumably sampled it — `Inferred`, the upload and the 64×56/32×1 geometry come from the decompiled ROM). M2-08 was updated in place (2026-11-05) with this correction.
- **MOD texture atlas (layer 4)** — `GameEngine.getTextureAtlas()`: a 512×512 icon atlas packed from MOD-provided icons (`GameEngine.java:383-384`), indexed by `RegistryId`; `TextureAtlasIcon.render` → `queueOrthoModel` + `.useTextureAlpha()` (TextureAtlas.java:24-30). Item icons/character portraits in menus can be MOD-supplied through this path (`SItem.renderCharacterPortrait`, SItem.java:941-946). `GameEngine.UI_TEXTURE` (`gfx/ui/ui.png`) is the reference-bundled UI texture used by modern controls (e.g. `Checkbox`, Checkbox.java:76).

## 3. The menu renderable system (`Renderable58` + `Menus`)

### 3.1 `Renderable58` fields and flags (source-verified, Renderable58.java)

ROM struct (0x58 bytes): `flags_00` (**0x4** NO_ANIMATION, **0x8** DELETE_AFTER_RENDER, **0x10** DELETE_AFTER_ANIMATION, **0x20** BACKWARDS_ANIMATION, **0x40** INVISIBLE), `glyph_04` (current glyph index in the strip; −1 = unloaded), `ticksPerFrame_08`, `animationLoopsCompletedCount_0c`, `startGlyph_10`/`endGlyph_14`, `repeatStartGlyph_18`/`repeatEndGlyph_1c`, `uiType_20`, `deallocationGroup_28`, `tpage_2c`/`clut_30` (0 = use the metric's values), `widthScale`/`heightScale_38` (float, 1.0 = metric scale; 0 = use the metric's own s16 scale), `z_3c`, `x_40`/`y_44`, `widthCut`/`heightCut` (reference-added per-renderable scissor), `baseX`/`baseY` (per-`uploadRenderable` offset), plus reference-only `metricsCount` (default `Integer.MAX_VALUE`) and `metricsBlacklist` (per-metric skip lists — layer-4 hooks for e.g. the stat-comparison CLUTs of `SItem.renderNumberComparison`), and `colour` (vec3, multiplies the quad colour). `initGlyph` (SItem.java:1100-1107) sets a static (NO_ANIMATION) glyph on tpage `0x19`; `initHighlight` (SItem.java:1110-1115) sets `z_3c = 35`, scale 0.

### 3.2 `Menus.renderUi` (0x80023c8c LAB chain, Menus.java:198-436) — called at gameLoop phase 6

Per frame, over the per-frame `renderables` list (filled by `uploadRenderable` from the managed `Renderable58`s of the active screens/controls):

1. **Sort: `renderables.sort(comparingDouble(z_3c).reversed())`** — z *descending* (largest z = farthest first), stable sort, so equal-z renderables keep their queue insertion order (which the ortho-pool LIFO tie-break then inverts — §6, Q-71).
2. **Glyph animation** (unless NO_ANIMATION): the tick gate is `tickCount_800bb0fc % max(1, 3 − vsyncMode_8007a3b8) == 0` — i.e. vsync mode 2 ticks the strip every 3rd vsync, mode 1 every 2nd, mode 0 every vsync (the strip animation speed is tied to the presentation rate — the same `vsyncMode` that halves frame rate, R1 §4). When `ticksPerFrame_08` underflows, the glyph advances (or retreats with BACKWARDS_ANIMATION); reaching the range end wraps to the other end, increments `animationLoopsCompletedCount_0c`, and, if `repeatStartGlyph_18 != 0`, re-bounds to the repeat range (and clears BACKWARDS_ANIMATION; with no repeat end it also sets NO_ANIMATION). DELETE_AFTER_ANIMATION sets INVISIBLE and forces unload at wrap.
3. **Geometry** (unless INVISIBLE), per metric of the current glyph (metrics iterated *in reverse index order*, `metricsCount`/`metricsBlacklist` applied):
   - `centreX = displayWidth_1f8003e0 / 2 + 8` — the +8 is a ROM quirk (for the default 320 display: 168; for the menu 384 display: 200).
   - x/y with metric scaling around the metric centre; negative `widthScale_10`/`heightScale_12` flip (the flipped quad gets a 1-px position nudge: `(width < 0 ? 1.0f : 0.0f)`).
   - `clut = renderable.clut_30 != 0 ? renderable.clut_30 : metrics.clut_04 & 0x7fff`; `tpage = renderable.tpage_2c != 0 ? (metrics.tpage_06 & 0x60) | renderable.tpage_2c : metrics.tpage_06 & 0x7f` — a renderable tpage override *preserves the metric's rate bits* (bits 5:6).
   - **`transforms.scaling(width, height, 1)` + `transfer.set(x1 + baseX + centreX − 8 + flip, y1 + baseY + 120 + flip, z_3c * 4.0f)`** — the centre arithmetic cancels out (final position = `x_40 + metrics.x + baseX`, `y_44 + metrics.y + baseY` in the 320×240 centre space); scaling pivots on the metric centre; **depth = `z_3c × 4`** — the "×4 text/UI depth domain" (§6.1).
   - If `!useOriginalTpage`: `tpageX −= 512; clutX −= 512` (§2.1), then `tpageOverride(tpageX, (tpage & 0b10000) ? 256 : 0)`, `clutOverride(clutX, clut >>> 6)` (M5-01 §9.2: overrides replace the vertex-decoded values).
   - **Queue**: `RENDERER.queueOrthoModel(renderable.uiType_20.obj, transforms, QueuedModelStandard.class).vertices(metrics.vertexStart, 4)` with **both depth comparators set to `LESS_THAN_OR_EQUAL`**, `.colour(renderable.colour)`.
   - **Translucency**: if `(metrics.clut_04 & 0x8000) != 0` (the clut-word MSB), `model.translucency(Translucency.of(tpage >>> 5 & 0b11))` — UI glyph translucency reuses the **TPage rate → translucency table** (M5-02 §3, G11: `00` → HALF_B_PLUS_HALF_F, `01` → B_PLUS_F, `10` → B_MINUS_F, `11` → B_PLUS_QUARTER_F).
   - `metrics.useTexture(model)` (no-op in the retail metrics class — a layer-4 extension hook) and, if set, `model.scissor(x, y, w, h)` from `widthCut`/`heightCut` (a *screen-space* scissor captured in native coordinates).
4. **Unload**: DELETE_AFTER_RENDER or forced-unload removes the renderable from the managed list; the per-frame `renderables` list is cleared at the end.

`allocateRenderable` (0x80023b54) adds to `managedRenderables_800bdc5c` *first* (newest managed first); `deallocateRenderables(group)` (0x8002437c) removes everything with `deallocationGroup_28 <= group` and, for non-zero groups, nulls the four arrow renderables (`upArrow_800bdb94` etc.).

### 3.3 Menu open/close and the 384-wide legacy display (source-verified, Menus.java:65-121)

`initMenu(destMenu, destScreen)` (Menus.java:65-80): `startMenuMusic()`, `SCRIPTS.stop()`, clear managed renderables, **`resizeDisplay(384, 240)`**, **`renderMode = EngineState.RenderMode.LEGACY`** (the *world* projection mode — "pseudo-perspective using orthographic projection with H division", EngineState.java:183-189 — not the engine's presentation mode), **`textZ_800bdf00 = 33`**, then push the screen onto `menuStack`. `loadAndRenderMenus` (0x80022590, gameLoop phase 9): `RENDER_NEW_MENU`/`RENDER_SAVE_GAME_MENU_19` → `menuStack.render()`; `UNLOAD*` → `menuStack.reset()`, `SCRIPTS.start()`, `deallocateRenderables(0xff)`, `startFadeEffect(2, 10)` (fade-in, 10 frames), `textZ_800bdf00 = 13`. So menus present at **384×240**, which `Gpu.drawingArea` clamps to **368** (`if (width == 384) width = 368`, Gpu.java:402-409 — the M4 G4 row) for the GPU-model path, while the modern path renders the FBO at 384 and post-processes it (M5-01 §10, M5-04 scope). `resizeDisplay` defers a full re-init to `syncFrame_reinit` on the next frame (Graphics.java:153-168). `Graphics.initDisplay` sets `centreScreenX/Y = 0` and the display aspect matrix `set(1, 1, displayHeight/displayWidth * 4/3)` (Graphics.java:259-277).

## 4. The menu screen stack and message boxes (layer 4 over layer 3)

### 4.1 `MenuStack` (MenuStack.java, 268 lines — reference input/rendering harness)

A `Deque<MenuScreen>`; input handlers (mouse, keys, buttons, axes, input actions, input-class changes) are registered only while the stack is non-empty. `render()`: the scroll accumulator converts wheel deltas to ±1 per frame, then **`propagate(it, renderScreen, propagateRender, lowerFirst = true)`** — lower screens render first, upper screens render on top (painter's algorithm through call order; combined with the ortho-pool LIFO tie-break at equal depth, the *earlier-queued* lower screen would draw last — screens in practice sit at different z levels, §6). Input propagates with `lowerFirst = false` (top screen first), and only continues if the screen's `propagateInput` allows — modal semantics. Mouse coordinates are mapped into **display-texture (model) space**: in `STRETCHED` widescreen mode `x / windowWidth * nativeWidth`; otherwise a centered 4:3 letterbox with `scaleX = w / GPU.getDisplayTextureWidth()` (MenuStack.java:190-217) — i.e. all UI hit-testing happens in the 320×240/384×240 model space, never window pixels. "Click" = press and release within 4 px (MenuStack.java:183-187).

### 4.2 The ROM message-box state machine (`SItem.messageBox`, 0x8010ecec, SItem.java:1685-1795)

`MessageBox20` (ROM struct, 0x20 bytes: `text_00` String[], `highlightRenderable_04`, `backgroundRenderable_08`, `state_0c`, `ticks_10`, `type_15` ALERT/CONFIRMATION, `menuIndex_18`, `x_1c`/`y_1e`, `result`). `setMessageBoxText` (0x8010f130) defaults to x=120, y=100. State flow (source-verified):

- **1 → Allocate**: background glyph strip `allocateUiElement(149, 142, x−50, y−10)`, `z_3c = 32`, `repeatStartGlyph_18 = 142` (a one-shot in-animation on the uiElements strip); ALERT and CONFIRMATION types.
- **3 → Active**: `textZ = 31` for the body text (leftPadding 60, topPadding 14 for CONFIRMATION / 29 for ALERT; each line `renderText(line, x, y, UI_TEXT_CENTERED)`, line advance 12 px, vertical centreing by `textHeight = lines × 12 / 2`; CONFIRMATION compacts by `(lines − 1) × 3`), then `textZ = 33` for the Yes/No line; the `Highlight` renderable (`new Highlight()`, `setZ(31)`) sizes itself from `DEFAULT_FONT.textWidth/Height` of the Yes/No strings plus 12 px spacing. Yes at `textZ = 30` (rendered at `y + 7`), No at `y + 21`; the selected line uses `UI_TEXT_SELECTED_CENTERED` (red text), the other `UI_TEXT_CENTERED`; `textZ` restored to 33.
- **4 → 6 → 0**: out-animation (glyphs 0x8e→0x95, i.e. 142→149, `DELETE_AFTER_ANIMATION`), then completion.

### 4.3 `MessageBoxScreen` (layer-4 wrapper, MessageBoxScreen.java)

Wraps the ROM state machine as a `MenuScreen`: `render()` calls `SItem.messageBox(...)` every frame and pops the screen when state 0. Mouse hitboxes for Yes/No: `(x+4, selectionY, 112×14)` and `(x+4, selectionY+14, 112×14)` where `selectionY = y + 21 + lines×12/2 − (lines−1)×3`; keyboard: UP/DOWN navigate (wrap allowed only on *fresh* input — `allowWrapY` resets on action release), CONFIRM selects, BACK cancels (ALERT: any confirm/back dismisses). `propagateRender()` returns `true` (the screen below keeps rendering — the world stays visible behind the box).

### 4.4 `Highlight` (layer-4 control, Highlight.java)

Programmatically built from synthetic `UiPart`/`RenderableMetrics14` data (9 background quads: 4 corners + 4 edges + 1 centre fill; 6 bracket quads: 2 top + 2 side + 2 bottom corner brackets). Notable decoded values: background clut `0xff29` (MSB set → translucent) with tpage `0x240c` (8bpp, page x 384 → −512 → 896, rate `00` → HALF_B_PLUS_HALF_F); bracket clut `0xfc29` (translucent) with tpage `0x2c` (4bpp, page x 384 → 896, rate `10` → **B_MINUS_F** — the corner brackets are a *darkening* overlay). Because both are translucent B±F quads in the ortho pool, the highlight box blends with whatever 3D content is behind it at the same z (31 → depth 124, §6.1).

## 5. The text system

### 5.1 `Text.renderText` (0x80029300, Text.java:1814-1907; main body :1833-1907) — the general path

- `font.init()` (lazy Obj/texture build), `height = 12 × size`, `trim` clamped to ±height (negative trims the top, positive the bottom — used by the loading-screen fade, GameEngine.java:569-575, where each queued text model also gets `alpha(loadingFade).translucency(HALF_B_PLUS_HALF_F)` via the `QueueCallback`).
- **Passes**: without shadow, 1 pass; with shadow, **4 passes** — i=0 the main text at `(x, y, textZ × 4)`; i=1..3 shadow passes offset by `(i&1)×size` right and `(i>>1)×size` down, at depth `textZ×4 + i×0.1` (0.1 = one-tenth of a text-z level, i.e. 0.4 depth units — the shadow is *behind* the main text since smaller depth is nearer, §6.1). A glyph is drawn in a pass only if `i == 0 || font.usesColour(c)` — monochrome glyphs get the main pass only; colour glyphs (the gamepad-action glyphs, see below) get all four.
- Alignment LEFT/CENTRE/RIGHT from `font.lineWidth`; newline handling resets x to the next line's alignment origin; `y += height` per line (12 px × size).
- **In-source texture-adjustment note** (Text.java:1848, repeated at :1671): "I adjusted the texture so that glyphs start 1 pixel lower to fix bleeding - subtract 1 here to compensate" — the reference's atlas PNGs were shifted 1 px down by the reference authors; both text paths compensate with `y − 1`. This is a **layer-4 artifact** of the PNG-atlas substitution: a reimplementation using the original font TIM cells would not need the −1 (the original cell grid is `Inferred` from the 64×56 TIM geometry — M2-08 §7).
- Trim scissoring: when `trim != 0`, the *main* pass model gets a full-width screen-space scissor window clipping the trimmed band (Text.java:1886-1892).

### 5.2 Gamepad action glyphs (`InputCodepoints`)

`renderText` and `renderTextboxText` translate characters through `InputCodepoints.getCodepoint(PLATFORM.getGamepadType(), c)` — the retail `ACTION` (0xc0) textbox opcode and the `TEXTBOX_INPUT_ACTION` codepoint range map to platform-specific button glyphs (triangle/circle/square/cross → A/B/X/Y etc.), with `getActionName` for the printable name (Text.java:1639-1647). Layer 4 (input-class abstraction) over a layer-3 original feature (context-sensitive button icons in text).

### 5.3 Textboxes: structs

- **`Textbox4c`** (0x4c bytes): `state_00`, `backgroundType_04` (0 none / 1 normal / 2 animate in-out), `renderBorder_06`, `flags_08` (**0x1** ANIMATING, **0x2** movement, **0x4** NO_ANIMATE_OUT, **0x8000_0000** RENDER_BACKGROUND), `z_0c` (default **14** on `clear()`), `currentTicks_10`, `x_14`/`y_16`, `chars_18`/`lines_1a`, `width_1c`/`height_1e`, `animationWidth_20`/`animationHeight_22` (.12 fixed, 0x1000 = 1.0), `animationTicks_24`, `currentX_28`/`currentY_2c`, `stepX_30`/`stepY_34`, plus reference-added `backgroundTransforms` MV, `borderTransforms` MV[8], dirty-cached `oldX/oldY/oldW/oldH/oldScaleW/oldScaleH`, and `colour` (config `UI_BACKGROUND_COLOUR`, default white).
- **`TextboxText84`** (0x84 bytes): `state_00`, `type_04` (0 normal, 1 …, 2 scroll-up variant, 3 selection, 4 named), `flags_08` (INITIALIZED 0x1, TEXT_PARSE_COMPLETE 0x2, …, HAS_NAME 0x200, SELECTION 0x800, SHOW_ARROW 0x1000, **NO_ARROW 0x2000 — "not a retail flag"** per in-source comment: reference addition for Phantom Ship's code-locked chest), `str_24` (the raw string, `LodString`), `chars_1c`/`lines_1e`, `x_14`/`y_16`, `z_0c` (default **13**, TextboxText84.java:133), `chars_58` (TextboxChar08[]: `x_00`, `y_02`, `char_06`, `colour_04` — pre-laid-out character grid), `scrollAmount_2c`, `scrollSpeed_2a`, selection indices, and the inline-opcode constants (END 0xa0, LINE 0xa1, BUTTON 0xa2, MUTLIBOX 0xa3 [sic], SPEED 0xa5, PAUSE 0xa6, COLOUR 0xa7, VAR 0xa8, X_OFFSET 0xad, Y_OFFSET 0xae, SAUTO 0xb0, ELEMENT 0xb1, ARROW 0xb2, ACTION 0xc0).
- Eight textboxes: `textboxes_800be358[8]`, `textboxText_800bdf38[8]`, `textboxArrows_800bdea0[8]`, `textboxVariables_800bdf10[10]` (VAR opcode values). `initTextboxes` runs from `loadBasicUiTexturesAndSomethingElse` (Scus94491BpeSegment.java:566-571), which also sets `textZ_800bdf00 = 13`.

### 5.4 Script entry points (source-verified, @Method-anchored)

- `scriptSetTextboxContents` (0x80025158): sets text + type, SHOW_ARROW, allocates the `chars_58` grid at `chars_1c × (lines_1e + 1)`, `calculateAppropriateTextboxBounds`.
- `scriptAddTextbox` (0x800254bc): the "packed data" word selects three ROM .rodata tables — `textType = textboxTextType_80052ba8[packed >>> 8 & 0xf]`, `backgroundType = BackgroundType.fromInt(textboxMode_80052b88[packed >>> 4 & 0xf])`, `renderBorder = renderBorder_80052b68[packed & 0xf]` (tables at 0x80052b68/0x80052b88/0x80052ba8; e.g. packed type 0 → no border/normal type 0, type 1 → border + type 1 + NO_INPUT when `packed & 0x1000`). Includes the in-source "stupid hack to allow inns to display 99,999,999 gold without the G falling down to the next line (see GH#546)" (a **layer-4 fix** for a retail width bug) and the HAS_NAME flag for type 4 (yellow-name textboxes).
- `scriptAddSelectionTextbox` (0x80025718): selection bounds + SELECTION flag; if the box is in PROCESS_NO_INPUT_ADVANCED_13 it transitions to SELECTION_22 after `10 × tickMultiplier()` ticks with a menu sound.

### 5.5 `handleTextbox` (0x80025a04, Text.java:302-436) — background state machine

- `_1` (idle) → per backgroundType: NO_BACKGROUND → state 4 (RENDER_BACKGROUND toggled); NORMAL → **`width = chars × 9 / 2`, `height = lines × 6`** (4.5 px per column, 6 px per row — the textbox size contract), state 5/6 by NO_ANIMATE_OUT; ANIMATE_IN_OUT → state 2 with ANIMATING, `animationTicks = QUICK_TEXT INSTANT ? 1 : 60 / vsyncMode / 4` (the `QUICK_TEXT_CONFIG` is a **layer-4 config**; the `60 / vsyncMode / 4` rate is layer-3 — animation duration tracks the presentation rate), and, with the movement flag (0x2), per-tick `stepX/stepY`.
- `_2` (animating in): `animationWidth/Height = (currentTicks << 12) / animationTicks`; `width = chars × 9 / 2 × animationWidth >> 12`, `height = lines × 6 × animationHeight >> 12`; position steps when movement is set; on completion snaps to full size.
- `ANIMATE_OUT_3`: mirror of `_2` with a decrementing tick counter; ends at UNINITIALIZED_0 with 0×0 size.
- `_4`/`_5` (shown): when the text has finished (UNINITIALIZED_0), either animate out (if ANIMATE_IN_OUT) or drop to UNINITIALIZED_0; the scroll arrow is reset.

`handleTextboxAndText` (Text.java:2093-2106, gameLoop phase 16) iterates all 8 slots; `renderTextboxes` (0x8002a0e4, :2108) renders backgrounds first for every box, then overlays — with an engine-state override `renderTextOnTopOfAllBoxes()` that, when true, renders *all* overlays after *all* backgrounds (layer-3 hook, `Inferred` to be the battle HUD keeping its text above other boxes).

### 5.6 `renderTextboxBackground` / `renderTextboxBorder` (0x80025f4c / 0x800261c0)

- Background (Text.java:438-465): dirty-checks x/y/w/h; `backgroundTransforms.transfer.set(x, y, z_0c × 4 + 1)`, `scaling(width, height, 1)`; queues the persistent `textboxBackgroundObj` (a 2×2 white quad, **HALF_B_PLUS_HALF_F** — a translucent panel — Text.java:229-237) with `.colour(textbox.colour)` and a full-window `worldScissor()` (the box ignores the captured scissor stack). Depth: **`z×4 + 1`** — one unit in front of the border/text plane.
- Border (Text.java:467-517): 8 persistent 16×16 4bpp quads baked at `vramPos(896, 256)`, `clut(832, 484)` (§2.1), placed at the box corners with insets **±4 px horizontal, ±5 px vertical** (`xs = {left+4, right−4, left+4, right−4}`, `ys = {top+5, top+5, bottom−5, bottom−5}`); each piece is scaled by `(right−left)/16`, `(bottom−top)/16` (the 16×16 source cell stretched to the piece size); during animation the piece w/h are scaled by the .12 `animationWidth/Height`; depth **`z × 4`** (one unit *behind* the background quad, i.e. nearer in this domain — §6.1). Border metrics (corner/edge cells) come from the ROM table `textboxBorderMetrics_800108b0` (Text.java:63).
- The arrow objects (7 × 16×14, `clut(1008, 484)`, `vramPos(896, 256)`, u = 64 + i×16) are the scroll indicator; the selection object (1×12, HALF_B_PLUS_HALF_F) is the Yes/No line highlight.

### 5.7 `renderTextboxText` (0x800282ac, Text.java:1617-1706) — glyph layout and drawing

Per laid-out character (`chars_58[i]`; the name line, when HAS_NAME, occupies indices `[chars_1c, 2×chars_1c)` and does *not* scroll):

1. The character may expand to a multi-codepoint string (action glyphs, §5.2).
2. **Scroll window**: for the active (scrolling) line, `scrollY = −round(scrollAmount_2c)`, `scrollH = round(scrollAmount_2c)`; the line *below* gets `scrollY = 0`, `scrollH = 12 − round(scrollAmount_2c)`. When `scrollH < 13` the character is drawn (otherwise fully clipped) — this is the smooth one-line scroll: the outgoing line shifts up by `scrollAmount` and is clipped by a shrinking 12-px window.
3. **Position**: `x = (int)(_18 − centreScreenX + nudgeX)`; `y = (int)(_1a + chr.y_02 × 12 − centreScreenY − scrollY [− scrollAmount]) − 1` — **12 px line height** (`chr.y_02 × 12`), the −1 is the atlas-bleeding compensation (§5.1).
4. **Two passes per character** (Text.java:1683-1700; shadow monochrome pass at :1685):
   - **Shadow**: `DEFAULT_FONT.queueChar(c, transforms).monochrome(0.0f)` at `(GPU.getOffsetX() + x + 1, GPU.getOffsetY() + y − scrollH + 1, (z_0c + 1) × 4)` — 1 px right/down, **one full text-z level behind** (4 depth units), with a per-char scissor `(offsetX + x, offsetY + y + 2, charWidth(c) + 1, 12 − scrollH)`.
   - **Main**: at `(x, y, z_0c × 4)` (after `transfer.x--; y--; z −= 4`) with `.colour(chr.colour_04 / 255)` and scissor `(offsetX + x, offsetY + y + 1, charWidth(c) + 1, height)` — note the asymmetric +2/+1 scissor tops (the shadow's window starts 1 px lower, preventing the shadow of line N from bleeding onto line N−1).
   - `GPU.getOffsetX()/getOffsetY()` are the widescreen content offsets (M5-01 §10) — text is placed in native 320-wide space and the offset shifts it into the presented 368/384 frame.
5. Advance: `nudgeX += charWidth(c) + 1` (1-px tracking); `nudgeX` resets per line (`chr.x_00 == 0` marks a line start) and absorbs the action-glyph width differences.

`scrollTextboxDown/Up` (0x80028828/0x800288a4) step `scrollAmount_2c` by `scrollSpeed_2a`; at 12 px (or QUICK_TEXT INSTANT) the box advances a line (`advanceTextbox`, 0x80027eb4) or rewinds (`rewindTextbox`, 0x800280d4). `handleTextboxText` (0x800264b0, 700-line method) is the character-processing state machine: it parses the `str_24` inline opcodes (PAUSE, COLOUR, VAR, X/Y_OFFSET, LINE, BUTTON, SAUTO, ARROW, ACTION…), lays out `chars_58`, and drives the typing/scroll states; the retail opcode set above is source-verified, its exact timing tables are `Inferred` where the decompilation is ambiguous (state-machine semantics otherwise follow the @Method-anchored code).

### 5.8 Menu text and colour presets

`SItem.renderMenuCentredText` (0x80103e90, SItem.java:987-1053): greedy word-wrap at `maxWidth` (backtracking up to 10 chars for a space), each line centred via `renderText` at `y + height`, line advance `font.textHeight(str) × size` (12 × size). The colour presets (SItem.java:137-142): `UI_TEXT` / `UI_TEXT_CENTERED` — brown text (0x58,0x40,0x30) with middle-brown (0x90,0x68,0x40) shadow; `UI_TEXT_SELECTED_CENTERED` — **red** text (0xd8,0x40,0x20), middle-brown shadow. The `TextColour` palette (TextColour.java): WHITE 0xf6f6f6, BROWN, RED, MIDDLE_BROWN, LIGHT_BROWN, YELLOW 0xf8f800 — the LoD menu palette (layer 3, source-verified as the reference's palette constants; whether the original used exactly these 15-bit values is `Inferred` — they round-trip through the decompiled ROM).

i18n: text content flows through `TextComponent`/`LodString`/`I18nText` (M2-08 §4, layer-4 localization over layer-3 embedded strings); `Fmv.displaySkipText` and the like call `renderText` directly.

## 6. Full-screen effects and the GPU-model 2D path

`FullScreenEffects` (0x800136dc/0x80013778/0x80013c3c/0x80013d78; `scriptStartFadeEffect` 0x8001751c):

- **Fades** (`startFadeEffect(type, frames)`): type 1 = fade out (darken), 2 = fade in (un-darken); `currentColour` ramps `v1 × 255 / totalFrames` (type 1) or `^0xff` of it (type 2), where `v1 = min(totalFrames, (vsyncCount − startTime) / 2)` — **the fade advances at half the vsync rate** (a 10-frame fade spans 20 vsyncs). Drawn via the **modern** path: `RENDERER.queueOrthoModel(RENDERER.plainQuads.get(B_MINUS_F|B_PLUS_F), transforms)` — a full-width quad scaled to `max(nativeWidth, renderW/renderH × displayH × 1.1)` (the 1.1× overfill covers wide contexts — M5-01 §10), translated to **z = 120** (i.e. text-z 30), monochrome `colour/255`, full-window world scissor.
- **Lightning flash/darken** (types 3/4 via the `red0/green0/blue0` and `red1/green1/blue1` channels, driven by the game's lightning state): two modern-path quads at **z = 156** — `plainQuads.get(B_PLUS_F)` for the bright flash, `plainQuads.get(B_MINUS_F)` for the darkening.
- **Boxagon tile effect** (types 5/6): an 8×6 grid of 40 px cells, each a monochrome quad with colour `base − (s0 + s1) × 11` (a diagonal light ramp; effect 6 uses the inverse ramp), positioned by `rsin`/`rcos` of `angle × 0x400 / 123` (clamped at angle ≥ 124: sin = 20, cos = 0) — the **M4-05 §2.3 PSX fixed-point trig** (`rsin`/`rcos`, `Scus94491BpeSegment`) is used live here. These tiles are the only 2D content in this file drawn through the **GPU-model queue**: `GPU.queueCommand(30, GpuCommandPoly(4).rgb(cos,cos,cos).pos(...))` — z 30 in the GPU model's ordering table (M4-01). The tile cells do not overlap, so the same-z LIFO-vs-FIFO traversal order (Q-66) is visually irrelevant for this effect. As established in §1, GPU-queue output is presented only in `legacyMode` 1/2; **`Inferred`** that in the default mode 0 the boxagon tiles are not visible (the fades/lightning quads above, being modern-path, are).

## 7. Presentation geometry: the ×4 depth domain and the Q-71 census

### 7.1 The ×4 text/UI depth domain (source-verified pattern, `Inferred` origin)

2D depth in the ortho pool is `transforms.m32 + screenspaceOffset.z` (M5-01 §5/§6). All UI/text code writes depth as **`level × 4.0`** (plus small offsets):

| Content | Depth expression | Default level → depth |
|---|---|---|
| `GameOverlay` game-var text (GameOverlay.java:27-33) | `textZ × 4` | 1 → 4 |
| Default text (`textZ_800bdf00`, set in Scus94491BpeSegment.java:569) | `textZ × 4` | 13 → 52 |
| Textbox text / border (TextboxText84.z_0c = 13; Textbox4c.z_0c = 14) | `z × 4` / `z × 4 + 1` (background) | 13/14 → 52 / 56/57 |
| Message box background / text / highlight / Yes-No (SItem.java:1708-1763) | `z × 4` | 32/31/30/33 → 128/124/120/132 |
| Menu text (`initMenu`, Menus.java:77) | `textZ × 4` | 33 → 132 |
| UI renderables default (`allocateManualRenderable`, Menus.java:177) | `z_3c × 4` | 36 → 144 |
| `initHighlight` (SItem.java:1114) | `z_3c × 4` | 35 → 140 |
| Battle HUD box (`UiBox`, UiBox.java:25-40, 0x800f1268 buildBattleHudBackground) | raw z (already in the domain) | 124 → 124 |
| Fade rect (FullScreenEffects.java:153-165) | raw | 120 |
| Lightning quads (FullScreenEffects.java:59-86) | raw | 156 |
| GPU-model boxagon tiles | GPU z queue | 30 (separate surface, §1) |

The ×4 spacing matches the GTE depth convention (the legacy TMD path divides GTE z by 4 into the 14-bit ordering table, M4-01/M5-02) — the 2D system's "text z" levels sit on the same 4-unit lattice as the legacy 3D z steps. *Why* the ROM chose ×4 (e.g. sub-text-z ordering headroom in a coarser domain) is `Inferred`.

### 7.2 Equal-depth collisions (the Q-71 population)

Within the ortho pool, content frequently shares a depth exactly, so the **equal-depth tie-break decides the draw order** (M5-01 §6: perspective pool FIFO, ortho pool **LIFO** — equal-depth items are drawn in *reverse* queue order, so the *earlier-queued* item renders last, i.e. on top):

- **Textbox border vs text** — both at `z × 4` (default 52/56): `renderTextboxBackground` queues border pieces before `renderTextboxOverlays` queues the glyph shadow/main quads at the same depth. The border is queued *earlier* → drawn *later* → the border quads overdraw the glyph pixels they cover. With the 4-px horizontal / 5-px vertical corner insets (§5.6) the border covers the text's extreme corners by design intent `Inferred` (the retail box visually clips text at the corners — needs visual verification, Q-71).
- **Menu screens** — `MenuStack.render` queues lower screens before upper ones; at equal z the lower screen would draw on top (LIFO inversion). Screens avoid this by z-leveling (the census below); whether any retail screen pair relies on the tie-break is `Unresolved` (visual check, Q-71).
- **UI renderables at the default z 36** — many menu elements (arrows, icons) share depth 144; their relative order is the *reverse* of `uploadRenderable` call order within the stable z-sorted list.
- **Fade/lightning (120/156) vs message-box text (124/132)** — deliberately interleaved levels, no collision.
- **HUD (124) vs message-box text (124)** — the `renderTextOnTopOfAllBoxes()` hook (§5.5) re-orders overlays when the engine state demands it, an explicit workaround for pool-ordering rather than a depth change.

**Ortho-pool population census** (source-verified): 139 `queueOrthoModel` call sites in ~60 files — the full 2D/overlay surface: `Menus` (UI renderables), `Text` (font glyphs, textbox bg/border/arrow/selection), `FullScreenEffects` (fades/lightning), `BattleHud`/`ListMenu`/`SpellListMenu`/`BattleUiParts` (battle UI, target-arrow z = `−monster.targetArrowPos.x × 100` or 0), `UiBox` (HUD background), battle sprite effects (`GenericSpriteEffect24`, `ElectricityEffect38`, `AdditionStarburstEffect10`, `StarChildrenMeteorEffect10`, `LensFlareEffect50`, `GuardEffect06`, `RadialGradientEffect14`, `GradientRaysEffect24`, `WeaponTrailEffect3c`, `FullScreenOverlayEffect0e`, `LmbAnimationEffect5c`, `AdditionOverlaysEffect44`, `ScreenDistortionEffectData08`, `SBtld`), particles (`PixelParticle`, `QuadParticle`), submap overlays (`SmokeParticleEffect`, `SnowEffect`, `MapIndicator`, `ChapterTitleCard`, `AttachedSobjEffect`, `TheEndEffectDatab0`, `RetailSubmap`, `SMap`), title screens (`Ttle`, `GameOver`), `Credits`, `Dabas`, `Fmv`/`VideoPlayer` (FMV frame quads — M5-04), `WMap` (`MapMarker`, `ZoomOverlay`, `CoolonQueenFuryOverlay`, `WmapPromptPopup`), MOD layer (`TextureAtlas` icons, `RetailBattleAction`/`SeveredBattleAction`/`SpecialBattleAction`/`TransformAction`), and controls (`Checkbox`, `ColourPicker`, `SeveredSaveCard`, `Textbox`, `PostBattleScreen`, `CreditsScreen`). TMD/particle *models* (`TmdParticle`, `DeffTmdRenderer14`, `SEffe` effect meshes) go through `queueModel` (perspective pool, M5-02 §7) and do **not** participate in the ortho tie-break. There is no PSX-hardware counterpart to the modern tie-break at all (the original ordered 2D content by z in the ordering table; same-z order was the GPU's own, M4-01/Q-66) — the LIFO asymmetry vs the perspective FIFO is purely a layer-4 property (Q-71, now censused; a fix is premature without visual evidence).

### 7.3 Coupling to the M5-01 presentation rules

- **Multiple-of-240 render height** (M5-01 §3, `onResize`): UI geometry is authored in the 320×240 native space; the render buffer height is forced to the next multiple of 240 so that the ortho content (text lines at 12 px, glyph strips at 8-bit y positions, 1-px scissor windows) scales by an integer factor. A non-multiple height would smear the 1-px scissor borders and the shadow 1-px offsets computed in native space.
- **384 → 368** (G4): the menu display is 384 wide; the GPU-model drawing area clamps to 368, the modern path renders 384 and post-processes to the window (M5-04). The menu's `centreX = displayWidth/2 + 8` therefore differs between the GPU-model (368 → 184+8 = 192) and modern (384 → 192+8 = 200) presentations `Inferred` (the two modes are not expected to be pixel-identical for menus; Q-37 visual cross-check).
- **Widescreen offset** (M5-01 §10): `GPU.getOffsetX()/getOffsetY()` shift all textbox text and the letterbox click mapping (§4.1) into the centered 320-column of a wider frame.
- **Shader scissoring** (M5-01 §7): the per-character 1-px scissor windows (renderTextboxText) and per-renderable `widthCut/heightCut` scissors rely on the fragment-shader scissor (the Intel iGPU workaround), not on GL scissor rectangles.
- **FOV aspect pin** (M5-01 §8, 320/nativeHeight): menus set `renderMode = LEGACY`, whose projection is the centered-ortho + H-division pseudo-perspective — the *world* behind the menu uses the 320-pinned aspect; the UI itself is ortho and aspect-independent.

## 8. Register impact

- **Q-71** (modern equal-depth ordering asymmetry): **advanced** — the ortho-pool population is now censused (139 call sites, §7.2), the equal-depth collision classes are identified (border-vs-text, same-z renderables, screen stacking), and the fade/lightning/HUD z-lattice is documented. Remaining: visual comparison of equal-depth pairs (border-corner text clipping; any menu pair at shared z) against the original — asset-validated follow-up.
- **Q-37** (render-path faithfulness): **advanced** — UI/text is documented as a two-surface system; GPU-queue-only 2D (boxagon tiles) is invisible in mode 0 (`Inferred`); menus present at 384×240 LEGACY world mode with the G4 368 clamp on the GPU side; the font system is layer 4 with the retail font TIM retained but unsampled.
- **Q-66** (same-z ordering-table traversal): **advanced** (minor) — the boxagon tile grid is same-z (30) content in the GPU queue but non-overlapping, so it cannot distinguish LIFO from FIFO; the census remains the discriminator for the ordering table.
- **Q-72** (new): **Original UI VRAM partition.** The −512 shift of the retail menu TIMs (and the +512-side placement of the font TIM at (832,424) and the textbox border/arrow quads at (896,256)) implies the retail layout kept UI textures in the +512 half of VRAM — presumably where a display buffer lived (the in-source comment says the reference "moved UI textures to the region of VRAM where the render buffers used to be used"). What the original occupied there, and whether the original UI texture coordinates ever addressed past x = 1024 (PSX VRAM does not wrap), is `Unresolved` — layer 1, asset-validated follow-up (retail TIM headers + the original GsInitGraph/DISPBUF setup).
- **M2-08 correction applied**: §1's statement that `Text.renderText` renders glyphs from the system font TIM is not supported by the pinned source — `renderText` uses only the configured JSON+PNG font (`Font.queueChar`); `loadSystemFont` still uploads `font.tim` at (832,424) but no modern path samples it. Updated in place with this M5-03 cross-reference.

## 9. Source index (pinned @ `0c08c679b`)

- `legend/game/Menus.java` — `initMenu` :65-80; `loadAndRenderMenus` (0x80022590) :89-121; `loadMenuTexture` (0x80022a94) :123-138; `allocateRenderable` (0x80023b54) :147-151; `allocateManualRenderable` :153-183; `uploadRenderables` :185-189; `uploadRenderable` :192-196; `renderUi` (0x80023c8c) :198-436; `unloadRenderable` (0x800242e8) :438-441; `deallocateRenderables` (0x8002437c) :443-460; `clearRenderables` (0x800e6d60) :462-464.
- `legend/game/SItem.java` — menu asset loads :745-748; `menuAssetsLoaded` (0x800fc944) :750-771; `buildUiRenderable` :1660-1683; `messageBox` (0x8010ecec) :1685-1795; `setMessageBoxOptions` :1797-1800; `setMessageBoxText` (0x8010f130) :1803-1819; `initGlyph` :1100-1107; `initHighlight` (0x80104b60) :1110-1115; `renderMenuCentredText` (0x80103e90) :987-1053; `renderCharacterPortrait` (0x80103910) :941-946; `FontOptions` presets :137-142.
- `legend/game/Text.java` — tables :63-97; `initTextboxes` :99-119; `scriptSetTextboxContents` (0x80025158) :125-146; `scriptAddTextbox` (0x800254bc) :149-207; `scriptAddSelectionTextbox` (0x80025718) :210-227; `initTextboxGeometry` :229-274; `clearTextbox` (0x800257e0) :277-285; `handleTextbox` (0x80025a04) :302-436; `renderTextboxBackground` (0x80025f4c) :438-465; `renderTextboxBorder` (0x800261c0) :467-517; `handleTextboxText` (0x800264b0) :520-1212; `processTextboxCharacter` (0x800274f0) :1214-1520; `calculateAppropriateTextboxBounds` (0x80027d74) :1522-1564; `advanceTextbox` (0x80027eb4) :1567-1588; `rewindTextbox` (0x800280d4) :1591-1614; `renderTextboxText` (0x800282ac) :1617-1706; `scrollTextboxDown/Up` (0x80028828/0x800288a4) :1708-1739; `textboxFits` (0x80028f20) :1742-1763; `renderTextboxSelection` (0x80029140) :1793-1809; `renderText` (0x80029300) :1814-1907 (main body :1833-1907); `setTextboxArrowPosition` :1909-1925; `renderTextboxArrow` :1927-1941; `handleTextboxAndText` :2093-2106; `renderTextboxes` (0x8002a0e4) :2108-2130.
- `legend/game/FullScreenEffects.java` — `startFadeEffect` (0x800136dc) :32-54; `handleFullScreenEffects` (0x80013778) :57-151; `drawFullScreenRect` (0x80013c3c) :153-165; `drawBoxagonEffect` (0x80013d78) :167-197; `scriptStartFadeEffect` (0x8001751c) :200-204.
- `legend/game/inventory/screens/MenuStack.java` (whole, 268 lines); `MessageBoxScreen.java` (whole, 252 lines); `FontOptions.java` (whole, 128 lines); `controls/Highlight.java` (whole, 134 lines).
- `legend/game/types/` — `Renderable58.java`, `RenderableMetrics14.java` :45-62, `UiFile.java`, `UiType.java`, `UiPart.java`, `MenuGlyph06.java`, `MessageBox20.java`, `Textbox4c.java`, `TextboxText84.java` :1-75.
- `legend/game/ui/UiBox.java` (whole, 99 lines; `setZ(124)` at :39).
- `legend/core/font/` — `FontManager.java` (whole, 77 lines), `Font.java` (whole, 146 lines; `init` :31-57, `queueChar` :59-72), `Glyph.java` (whole), `RetailFontConfigEntry.java` :18-52.
- `legend/game/Scus94491BpeSegment.java` — `gameLoop` :137-191; `loadBasicUiTexturesAndSomethingElse` :566-571; `rand/srand` (0x8002d260/0x8002d270) :586-596 (M4-05); `preload` (0x800e6184) :635-643; `loadSystemFont` (0x800e6524) :647-656; `loadShadow` (0x800e6998) :660-675.
- `legend/game/Graphics.java` — `centreScreenX/Y` :44-45; `resizeDisplay` :153-168; `initDisplay` (0x8003be28) :259-277; `GsSortClear` (0x8003c048) :283-286; `GsSetDrawBuffOffset` (0x8003c1c0) :292-295.
- `legend/core/gpu/Gpu.java` — `init/initVram` :85-115; `updateVramTexture` :116-124; `startFrame/endFrame` :125-135; `useVramTexture` :137-139; `tick` :174-200; `uploadData15` :208-267; `queueCommand` :346-355; `displayMode` :368-385; `displaySize` :387-400; `drawingArea` (384→368) :402-409; `drawingOffset` :411-414; `updateOrderingTableSize` :416-422; `drawDisplay` :424-436; `getOffsetX/Y` :453-459.
- `legend/core/gpu/GpuCommandPoly.java` :1-80; `legend/core/gpu/VramTextureSingle.java` :1-60.
- `legend/core/GameEngine.java` — `DEFAULT_FONT` :109, :250; `getTextureAtlas` :278-280; `getUiTexture` :282-284; atlas/UI texture init :383-387; loading-screen text fade :569-575.
- `legend/game/textures/TextureAtlas.java` :1-40; `legend/game/inventory/screens/TextColour.java` :4-12; `legend/game/ui/GameOverlay.java` :27-33.
- `gfx/fonts/*.json` + sibling PNGs (repo-shipped, layer 4); `gfx/ui/ui.png`.
