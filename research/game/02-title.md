# M9-02 — The Title Screen: Boot Chain, FMV Intro, Menu, and Update Check

**Milestone:** M9 (Game Systems: Content and Rules), deliverable 2 of 7 (01 Dabas, 02 title, 03 submap, 04 world map, 05 battle, 06 characters & progression, 07 inventory & items). **Date:** 2026-11-08. **Approved starting point:** `5cbdd65`.

**Scope.** Everything the player crosses between process start and the first playable state: the layer-4 cinematic/loading prelude, the retail ROM initialization, the retail FMV 0 intro cutscene and its hand-off to the TITLE engine state, the title screen itself (loading choreography, the five-option menu, mouse/hotkey layers, the uncategorized-save and PS1-memcard adoption flows, the update check), the GAME_OVER state and its return path, and the cross-state hooks the title leaves for the submap. The script-vs-hardcode boundary (RQ7) is settled in §8.

**Primary sources.**
- Severed-Chains @ `0c08c679b` (pinned, clean): `legend/game/title/Ttle.java` (1565 lines — the title state, decompiled retail anchors @ `0x800c66xx`–`0x800ce7xx`), `legend/game/title/GameOver.java`, `FireAnimationData20.java`, `TmdRenderingStruct.java`, `legend/game/credits/Credits.java` (384×240 credits, 16 TIM slots from DRGN0/5720), `legend/game/credits/FinalFmv.java`, `legend/game/fmv/Fmv.java` (MDEC playback + `playCurrentFmv`), `legend/game/fmv/RumbleData.java`, `legend/core/GameEngine.java` (`start()`, `loadGfx()`, `renderIntro()`, `transitionToGame()`), `legend/core/Updater.java`, `legend/core/SelfUpdater.java`, `legend/game/Main.java` (staged updater swap, crash handler), `legend/game/EngineStates.java` (overlay load, `FUN_80020ed8`), `legend/game/Scus94491BpeSegment.java` (`main()` @ 0x800e5d44, `gameInit` @ 0x800e5d64, `preload` @ 0x800e6184, `gameLoop` @ 0x80011e1c, `resetSubmapToNewGame` :574), `legend/game/Menus.java` (`initMenu`, `loadAndRenderMenus` @ 0x80022590), `legend/game/SItem.java` (`loadMenuAssets` :744, `startMenuMusic`/`stopMenuMusic` @ 0x8001e010), `legend/game/FullScreenEffects.java` (fade model), `legend/game/sound/Audio.java` (`loadMusicPackage` :737, `musicPackageLoadedCallback`), `legend/game/saves/SaveManager.java` (memcard/uncategorized flows :96–260, `loadGameState` :403–442), `legend/game/inventory/screens/AboutScreen.java` (:123), `legend/game/inventory/WhichMenu.java`, `legend/game/modding/coremod/CoreEngineStateTypes.java`, `CoreMod.java` (:215–216, :324–327), `legend/lodmod/LodEngineStateTypes.java` (:22–23), `legend/lodmod/RetailCampaignType.java` (:116–130), `patches/scripts.csv` (negative evidence).

**Validation.** All findings are **source-verified** (reference code) unless noted. The `@Method` anchors prove the title state is the decompiled retail title (ROM-provenance leg); its exact retail rendering equivalence is **asset-validated-pending** (no disc images, no retail bytes). The FMV 0 file, the rumble tracks, and the DRGN0/5718 MRG are retail disc assets consumed as-is. No `asset-validated` claims.

**Numbering.** M9's J/D series continues from M9-01 (J20–J24, D1–D13): this document adds **T1–T15** (title classification rows) and **J25–J27**.

---

## 1. System overview and layers

The title sequence is the one place where the reference layers are stacked visibly: a **layer-4 prelude** (the `gfx/intro.mp4` cinematic and loading eye) wraps the **retail ROM boot** (`Scus94491BpeSegment.main()`), which ends in the **retail FMV 0** (a real MDEC cutscene from the disc, played by the reference's own MDEC decoder), which hands off to the **decompiled retail TITLE state** (`Ttle`) — whose menu, however, has been partially reskinned (modern logo/menu-texture PNGs) and extended (mouse, update banner, campaign selection, memcard adoption). Everything after the title (submap, world map, battle) is documented in later M9 deliverables; this document owns the boundary.

| Part | Layer | Basis |
|---|---|---|
| `intro.mp4` cinematic + loading eye + skip | **4 — reference-specific** | `GameEngine.loadGfx`/`renderIntro`, `VideoPlayer` (FFmpeg/JavaCV; M5-04 §4.6) |
| Unpack → `Scus94491BpeSegment.main()` (gameInit/preload) | **3, decompiled retail** | `@Method` anchors (M1-01 §5, M2-04) |
| FMV 0 (MDEC `.IKI` + in-band XA + rumble track) | **3 — retail asset, retail behavior**; the decoder/skip UI/rumble playback are reference implementations | `Fmv.playCurrentFmv` (M5-04 §4, M6-03) |
| TITLE state: layout, choreography, 5-option menu, fade transitions | **3, decompiled retail** (retail anchors throughout) with **layer-4 reskin** (logo/menu-texture PNGs, mouse, hotkey banners) | §3 |
| Campaign-based "Continue", uncategorized-save prompt, PS1-memcard prompt, update check, Discord presence | **4 — reference-specific** | §3.7–§3.9, §4 |
| GAME_OVER state | **3, decompiled retail** (anchor @ 0x800c7558–0x800c7788) | §3.10 |

---

## 2. The boot → title chain

The full boot sequence is documented in M1-01 §3–§6; the title-relevant spine:

1. **Prelude (layer 4).** `GameEngine.start()` (GameEngine.java:182) kicks the async update check (§3.9), then `RENDERER.run()` with render callback `loadGfx`: plays `gfx/intro.mp4` via `VideoPlayer` (skippable by any key/button/click — `GameEngine.skip()`), renders the loading eye/loader overlay while a thread runs `Unpacker.unpack()` + `ScriptPatcher.apply()` (M2-03, M8-03 §3). (M1-01 §5.)
2. **ROM init.** On unpack completion, `transitionToGame()` (GameEngine.java:389) disposes the prelude handlers and, inside `INIT_LOCK`: `Scus94491BpeSegment.main()` → `gameInit()` @ 0x800e5d64 (GPU ordering table 0x4000/14 bits/zMax 0x3ffe — M4-01; `InitGeom`, projection distance 640, `initSound`, `loadSystemFont`, `SCRIPTS.clear()`, `loadShadow`, `initFmvs`) + `preload()` @ 0x800e6184 (`drgnBinIndex = 1`, `loadMenuSounds`, `resizeDisplay(320, 240)`, `loadBasicUiTexturesAndSomethingElse`). Then: shadow-model load, `loadMenuAssets()` (menu textures DRGN0/6665–6666 + menu music package 0, §5), `initTextboxGeometry()`, `battleUiParts.init()`, `startSound()`, `bindRendererEvents()` (installs `gameLoop` @ 0x80011e1c as the render callback).
3. **FMV 0 → TITLE.** The last line of `transitionToGame` is `Fmv.playCurrentFmv(0, CoreEngineStateTypes.TITLE.get())` (GameEngine.java:445). The FMV playback *replaces* the render callback (§2.1); when it finishes, `gameLoop` resumes and its `loadQueuedOverlay()` (top of every frame — Scus94491BpeSegment.java:154 calling EngineStates.java:47) consumes the queued TITLE state (§2.2).

At the moment FMV 0 starts, `currentEngineState_8004dd04` is `null` — the engine state machine does not start until the queue is consumed. There is no "FMV engine state"; the FMV is a render-callback takeover (§2.1, T8).

### 2.1 FMV 0: the retail intro cutscene (T8)

`Fmv.playCurrentFmv(fmvIndex, afterFmvState)` (Fmv.java:259):

- `sssqResetStuff()` and `submapId_800bd808 = -1` (leave the submap-id global out of the way).
- Loads the **rumble track** `RumbleData.load(Loader.loadFileSync("SECT/DRGN0.BIN/5721/" + fmvIndex))` — a 0xc-byte LE record stream `{i32 frame, u16 initialIntensity, u16 endingIntensity, i32 duration}` terminated by a `frame == −1` record (RumbleData.java:9–25; M2-09 documents the path and record layout) — retail disc data consumed as-is, with the rumble *playback* (`startRumbleIntensity`/`adjustRumbleOverTime`/`stopRumble` on port 0) a reference DualShock implementation (layer 4).
- `Fmv.play(diskFmvs_80052d7c[fmvIndex], true)` — the second argument is `doubleSpeed`: **10 sectors per frame at 150 sectors/s (15 fps)** — the 2×-CD cadence (vs 5 sectors/frame at 75 sectors/s for single speed) (Fmv.java:312–330; M5-04 §4.2).
- Queues the hand-off: `engineStateOnceLoaded_8004dd24 = afterFmvState`.

The playback loop (Fmv.java:380–643, fully documented in M5-04 §4) demuxes each frame's sectors (DATA sectors → MDEC decode; AUDIO sectors → `XaAdpcm.decode` at the native **37800 Hz stereo** rate, `>>1` halved, volume-scaled into a 37800 Hz `GenericSource` — M6-03/Q-48), applies the per-frame rumble events, renders the decoded frame into a textured quad (letterboxed to the window, 320-wide basis), and advances `frame`. Skip flow (M5-04 §4.3): the **first** press of the configured skip action on a source shows a 60-frame hint ("press <key> again"); the **second** press from the *same source class* sets `shouldStop` → `stop()`. `stop()` (Fmv.java:644–696) deletes the FMV objects/input handlers, restores the saved mode state (fps limit, projection, render mode, widescreen config, clear colour), removes the audio source, and reinstalls `oldRenderer` — which, on this boot path, is `gameLoop`.

FMV call sites (M5-04 §4.7, re-verified for this document; disc files from the 18-FMV `STR/*.IKI` inventory, M2-09):

| Call site | FMV index | Disc file | After-state |
|---|---|---|---|
| `GameEngine.transitionToGame` :445 (boot) | 0 | `STR/DEMOH.IKI` | `TITLE` |
| `RetailCampaignType.transitionToNewCampaign` :117 | 2 | `STR/OPENH.IKI` | `SUBMAP` |
| `SMap` :4377 (script-driven `fmvIndex_800bf0dc`) | data | data (one of the 18) | `afterFmvLoadingStage_800bf0ec` (script data) |
| `PlayFmvPostBattleAction` :35 (campaign end) | data (`inst.fmv`) | data (one of the 18) | `FINAL_FMV` |
| `FinalFmv` :45 | 17 | `STR/ENDING2H.IKI` | `CREDITS` |

FMV 0 is therefore the only boot-path FMV; FMV 2 (new campaign) and the script-driven FMVs belong to M9-03/M9-05 scope.

### 2.2 The state hand-off

`gameLoop` @ 0x80011e1c runs `loadQueuedOverlay()` (EngineStates.java:47) at the top of every frame: when `engineStateOnceLoaded_8004dd24 != null` it resets the menu stack, records `previousEngineState_8004dd28`, forces `vsyncMode = 2`, calls `loadGameStateOverlay` (destroy old state — none here, construct `Ttle`, `init()`, install the per-state script-native function table via `loadEngineStateFunctions`, set render mode LEGACY), posts `EngineStateChangeEvent`, and updates Discord rich presence (M1-01 §6; M8-02 event contract).

---

## 3. The TITLE state (`Ttle`)

### 3.1 Identity and lifecycle (T1)

- Engine state type **`title`**, registered by the in-repo `lod_core` mod via `CoreEngineStateTypes.TITLE` (CoreEngineStateTypes.java:19; M8-02 registry census). It is swappable registry content, like every other engine state (M8-03 D20).
- The class carries decompiled retail anchors throughout (`tick` @ 0x800c7798, `initializeMainMenu` @ 0x800c77e4, `renderMainMenu` @ 0x800c8298, `handleMainInput` @ 0x800c8484, `renderMenuOptions` @ 0x800c8634, `renderLogoFlash` @ 0x800cb974, `animateFire` @ 0x800cdcb0, field addresses `0x800c66d0`–`0x800c6754`, `0x800ce7b0`) — the retail title screen, re-hosted.
- `advancesTime() = false` — the title does not advance `gameState.timestamp_a0` (the game clock stays put while at the title; gameLoop only adds `vsyncMode` when the current state advances time, Scus94491BpeSegment.java:137–139).
- Not savable: `writeSaveData` returns `null`, `readSaveData` is a no-op — the title is never a `lastSavableEngineState` (M7-01 §10: savable states = WMAP + SMAP only).
- `getRenderMode() = LEGACY` (the 2D ortho legacy path, M5-01).
- `init()`: clears the SItem sound-env fade flags (`fadingIn_2a`/`fadingOut_2b`), and sets the **main volume to 0x7f/0x7f on both engines** — the 8-bit music domain and the 15-bit SPU domain (M6-01 §5, the dual-domain main volume) — then `FUN_8001aa90()` (sound reset).
- `vsyncMode_8007a3b8 = 2` (30 fps — the title ticks at half rate; credits use `vsyncMode = 1`, Credits.java:239) and `resizeDisplay(384, 240)` — the 384-wide **mode-0 menu width** (M5-03 G4: mode-0 menus render 384-wide in the 4:3 window).

### 3.2 Asset map (T2)

| Asset | Disc path | Use |
|---|---|---|
| Menu MRG (11 files) | `DRGN0/5718` (dir, async) | files 0+1 → background (stitched **vertical**; the obj is 368×424); file 4 → trademark (16×8 @ 326,86); files 5+6 → copyright (stitched **horizontal**; 184×16 @ 92,220); files 7–10 → fire texture-source TIMs (uploaded straight to GPU VRAM — the four 16×64 blocks at (944+i·16, 256) are row-rotated per tick, see below) (Ttle.java:453) |
| Logo | `gfx/ui/logo.png` (**layer-4 asset**, 1920×1080 in-repo PNG) | 352×198 quad, z 10000, scale (1.06, 0.82) — **replaces MRG file 2** ("Logo" per the in-source comment, unused) |
| Menu text atlas | `gfx/ui/title_menu.png` (**layer-4 asset**, 1024×512 in-repo PNG) | two 512-wide columns (left = sharp, right = blurred) × 8 rows of 512×64: options 0–4, memcard banner, update banner, icon row — **replaces MRG file 3** ("Menu text", unused) |
| Fire model | `DRGN0/5719` (file, async) | TMD with `nobj` objects (4 in `count_08`); the logo-flame animation |
| Game-over background | `DRGN0/6667` (file, sync) | MCQ (`McqHeader`), uploaded at VRAM (640, 0) — GAME_OVER state |
| Menu music | `DRGN0/5815–5819` (5-file dir) | package 0, `SItem.menuMusic`, preloaded at boot |
| Title music | `DRGN0/5820–5824` (5-file dir) | package 1, loaded + auto-played by the title |

The MRG file list is documented in the in-source comment (Ttle.java:437–452): `0 background-top, 1 background-bottom, 2 Logo, 3 Menu text, 4 TM, 5 copyright-left, 6 copyright-right, 7–10 Logo fire 2/3/4/1`. Files 2 and 3 are **never used** by `menuTexturesMrgLoaded` — the modern logo/menu-texture PNGs (1920×1080 and 1024×512 RGBA, layer-4 reskin assets committed to the repo — verified from the in-repo files) are drawn instead (T2). The retail label glyphs behind the five options are therefore not in-repo evidence (asset-validated-pending); the option *functions* are (below).

Fire animation: `FireAnimationData20` structs (16×64 rect at VRAM (944 + i·16, 256), two pixel buffers, initial rotation offset from `_{255, 1, 255, 255}`) — `animateFire` (Ttle.java:1520–1553) **cyclically row-rotates the 16×64 15-bit pixel block in VRAM every tick** (splits the rect at the offset, `GPU.downloadData15` the two halves, `GPU.uploadData15` them swapped; the offset is fixed per flame — 255/1/255/255 mod 64 = 59/1/59/59 — so each block scrolls continuously), producing the scrolling flame texture (M4-01 VRAM/15bpp; T2).

The flame model itself: `renderMenuLogoFire` (Ttle.java:1338) — per object: rotate about Y by −π/2 (`FUN_800cc26c`), fetch the GTE light, scale (0.8544922, 1, 1), `transfer.y −= 250`, then queue with `monochrome(flameColour/128)` (a warm-up ramp `flameColour += 2`, cap 0xff), screen offset (+8, 0), the retail light direction/colour matrices `lightDirectionMatrix_800c34e8`/`lightColourMatrix_800c3508`, and the current GTE background colour.

### 3.3 The loading-state machine (T3)

`tick()` dispatches on `loadingStage` (Ttle.java:239–251):

| Stage | Method | Behavior |
|---|---|---|
| 0 | `initializeMainMenu` | `loadMusicPackage(1)` (auto-plays, §5); resets all choreography fields (backgroundScrollAmount = −176, menuState = 0, fade-out timer 0, selected option 0); `resizeDisplay(384, 240)`, projection distance 320, RVIEW2 viewpoint (0,0,2000) / refpoint (0,0,−4000); `vsyncMode = 2`; kicks the async MRG + fire-TMD loads and the async **save scan** (`SAVES.hasCampaigns()`, `findMemcards()`, `findUncategorizedSaves()` on `Async.run` — a failure logs and leaves empty lists); builds the 4 fire-anim structs; `startFadeEffect(2, 15)` (fade **in** from black, 15 half-frames = 30 vsyncs); `GTE.setScreenOffset(0, 0)`; installs input handlers; caches `update = getUpdate()`; → stage 1 |
| 1 | `loadTextures` | no-op until the MRG callback sets `texturesLoaded`; then builds all GL objects (background/logo/trademark/menu-text/copyright quads, the 5-option + banner + icon vertex layout) → stage 2 (stage 3's rendering additionally waits on `fireLoaded`) |
| 2 | `waitForFilesToLoad` | when the save scan is done: `selectedMenuOption = hasCampaigns ? 1 : 0` (land on **Continue** when a campaign exists, else **New Game**); then the first-run prompts win: uncategorized saves → stage 9; else memcards → stage 10; else stage 3 |
| 3 | `renderMainMenu` | steady state: renders the choreography (§3.4) and the menu; input active |
| 4 | `fadeOutForNewGame` | option 0 confirmed (§3.6) |
| 5 | `waitForSaveSelection` | option 1 confirmed (§3.6) |
| 7 | `fadeOutForOptions` | option 2 confirmed |
| 8 | `fadeOutForQuit` | option 4 confirmed (fade out 16 ticks → `RENDERER.window().close()` — the normal shutdown path, M1-01 §7) |
| 9 | `fadeOutForCategorizeSave` | uncategorized-save prompt (§3.7) |
| 10 | `fadeOutForMemcard` | PS1-memcard prompt (§3.8) |
| 11 | `fadeOutForLinks` | option 3 confirmed → AboutScreen |

**Stage 6 does not exist** (the decompiled switch simply has no case 6).

Note the asymmetry: the two **first-run prompts** (9, 10) are driven by `menuState_800c672c = 4` + `menuTransitionState = 2` and reuse the generic `fadeOutToMenu` machinery *without* a fade-out timer of their own — they are one-shot per process (session flags, not persistent: `saveCategorizationShown`/`memcardConversionShown` are instance fields, re-created each title entry; the prompts will re-appear after every return to the title until the user either confirms or has nothing left to categorize/convert — T3).

### 3.4 Intro choreography (`menuLoadingStage` 0–3, T3)

`renderMainMenu` (Ttle.java:663) layers the entry animation:

- **Stage 0:** background only, scrolling up 1 px/tick from −176 until it reaches −40 (the background is 424 tall; 176 px of overscan slides in).
- **Stage 1:** + logo, fading in at 8/255 per tick.
- **Stage 2:** + logo flash — `renderLogoFlash`: a sine flash over the **previous frame buffer** (`RENDERER.getLastFrame()` + `renderBufferQuad`, monochrome `rsin(logoFlashColour)·160 >> 12`, `logoFlashColour += 0x60`, cap 0x800) — decompiled retail anchor @ 0x800cb974, implemented on the reference's frame-fence texture (M5-04 G13 region). Gated by `REDUCE_MOTION_FLASHING_CONFIG`: when set, the flash is skipped (`logoFlashStage = 2` immediately).
- **Stage 3:** + menu options + fire + copyright (steady state). Any key/button press calls `resetIdleTime()` which bumps stage 3→4 (re-arms the option animations); stage 4 decays back to 3 after one frame.

### 3.5 The menu model (T4)

Five options, `MENU_OPTIONS = 5`. Option widths (for centering): `{407, 257, 227, 189, 141}` (Ttle.java:184). Each option is drawn as **two quads** at 1/3 scale: a shadow at z 100.0 tinted (0xf8, 0x80, 0x10) and the face at z 100.1 coloured `lerp((0xf0,0xf0,0xf0), (0xc0,0x38,0x10), (alpha−0x40)/0x60)` — i.e. the selected option sweeps from near-white to the retail orange as its transparency climbs (T4). Position: x = 184 − width·(1/3)/2 (+ widescreen ortho offset), y = 130 + i·16 (T4).

`menuState_800c672c` drives the per-option transparency dynamics in `renderMenuOptions` (Ttle.java:982–1216):

| State | Dynamics (per tick, per option) |
|---|---|
| 0 | reset: all transparencies 0, update-row transparency 0 → state 1 |
| 1 | entry fade: all `+= 4`; selected capped at **176**, others capped at **64**; update/memcard banner `+= 4`, cap **160** |
| 2 | selection transition (set by any UP/DOWN): selected `+= 8` cap **160**, others `−= 16` floor **64** |
| 3 | confirmed → immediately state 4 + the option's loading stage |
| 4 | done (no render) |

Option **1 ("Continue") is special**: when `!hasCampaigns`, it is skipped by both mouse hover (the hit-test loop `continue`s past i == 1) and gamepad navigation (UP/DOWN immediately step past it, Ttle.java:947/960), and its alpha is halved at draw time (T4). It is never fully hidden — a decompiled-retail vestige of the always-present continue slot.

Extras drawn with the menu: the **update-available banner** (when `update != null`) and the **memcard-found banner** (when `!foundMemcards.isEmpty()`), each a 0.2-scale banner + shadow + icon quad plus an i18n hotkey label (`lod_core.ui.title.hotkey` with the bound action name), stacked top-left at y = 5/19; the **version string** top-right at (364, 4) (0.5-size light-brown right-aligned font, alpha tied to the banner transparency; Ttle.java:1213); the **updater progress line** at (10, 230) whenever `updateProgress != null` (T4).

### 3.6 Input and the five options (T5, T6)

Input actions (M1-05 input model; core bindings CoreMod.java:324–327):

- `menu_up` / `menu_down` — wrap 0↔4, skip disabled option 1, play menu sound 1, re-arm selection (state 2).
- `menu_confirm` — play menu sound 2, state 3 → the option's stage.
- `title_update` (default **Y** / **U**) — `autoUpdate()` (§4.2).
- `title_convert_memcard` (default **X** / **C**) — force the memcard prompt (stage 10) even past the first-run auto-prompt.
- **Mouse (layer 4):** hover selection with 155×16 hit boxes at y = 134+i·16 (4 px below the 130+i·16 draw position, centered on the 384-wide layout; scaled for the expanded widescreen layout), hover-change → menu sound 1, click → confirm; the two banner rows are clickable too (update → `autoUpdate`, memcard → stage 10) and show a pointer cursor on hover. Any click before the menu is ready (stage < 3) jumps the choreography to stage 3. All of this is skipped when `DISABLE_MOUSE_INPUT_CONFIG` is set and a gamepad is connected.

The five options and their transitions (all share the fade contract below):

| # | Function | Stage | Destination |
|---|---|---|---|
| 0 | New game | 4 | `NewCampaignScreen` (M9-07); on confirm: `campaignType.get().transitionToNewCampaign(gameState_800babc8)` → retail campaign type plays **FMV 2** then queues SUBMAP (RetailCampaignType.java:116–118; the submap itself starts at cut 675 "Opening", M9-03) |
| 1 | Continue / load | 5 | async `SAVES.loadAllCampaigns()` → `CampaignSelectionScreen` (M9-07); on selection: `loadGameState` (M7-01 §12: config copy, `bootRegistries`, bindings, `GameLoadedEvent`, `campaignType.setUpLoadedGame`) sets `loadingNewGameState_800bdc34 = true` (SaveManager.java:439) and the title queues `engineStateOnceLoaded_8004dd24 = REGISTRIES.engineStateTypes.getEntry(save.engineState)` + `campaignType.get().transitionToLoadedGame(state)` (no-op for the retail campaign type) |
| 2 | Options | 7 | `OptionsPresetsScreen` (M7-02 presets) |
| 3 | Links | 11 | `AboutScreen` (layer-4 links; it also offers a credits watch, setting `postCreditsEngineState = TITLE` — AboutScreen.java:123) |
| 4 | Quit | 8 | fade out (16 ticks) → window close |

**The fade contract** (`fadeOutToMenuAsync`, Ttle.java:584–635; T6): the first tick starts `startFadeEffect(1, 15)` (fade **to** black, 30 vsyncs) and optionally an async load action; while fading it keeps rendering the (fading) title; at tick ≥ 16 with the load done and `menuTransitionState == 2` it calls `initMenu(WhichMenu.RENDER_NEW_MENU, …)` (which also calls `startMenuMusic()`, §5), `removeInputHandlers()`, `deallocate()` (frees every title GL object), and sets `menuTransitionState = 3`. The destination menu is then rendered by the **global menu path** (`loadAndRenderMenus` @ 0x80022590 in `gameLoop`, M9-07 territory), not by `Ttle`. When the menu closes (`whichMenu == UNLOAD` → `loadAndRenderMenus`: menu stack reset, `stopMenuMusic()`, `SCRIPTS.start()`, fade-in 10, `currentEngineState.menuClosed()`) and the transition predicate returns false (user cancelled), `Ttle` sets `loadingStage = 0` — a **full re-initialization of the same state instance** (stage 0 re-runs: music reloaded, the MRG/TMD loads re-issued, every VRAM/GL texture and renderable rebuilt from scratch, choreography replayed). A confirmed selection instead lets the queued engine-state transition consume the next frame (`loadQueuedOverlay`), with `Ttle.deallocate()` already done.

`Ttle` does **not** override `restoreMusicAfterMenu()` (EngineState.java:101 default no-op) — the title music's resume is exactly that re-initialization's `loadMusicPackage(1)` (T6).

### 3.7 The uncategorized-save prompt (layer 4, T7)

When the save scan finds `.dsav` files sitting directly in `saves/` (pre-campaign layout; `SAVE_MATCHER` filenames — M7-01 §11): the title auto-opens (stage 9, first run only) a `FullScreenInputScreen` titled `lod_core.ui.title.uncategorized_saves` with a generated campaign name (`New Campaign` / `New Campaign 2` / … — `SAVES.generateCampaignName`, SaveManager.java:96–105). On YES: name-in-use check (`campaignExists` → alert `…campaign_name_in_use`), else `SAVES.moveCategorizedSaves` — a plain directory move into `saves/<slug>/` (SaveManager.java:215–226). On NO/cancel: just dismiss. No save data is touched; only the filesystem layout changes (M7-01 §11 campaign model).

### 3.8 The PS1-memcard prompt (layer 4, T7)

`SAVES.findMemcards()` (SaveManager.java:169–207) lists files in `saves/` that **are** PSX memory cards: exactly `0x20000` bytes, `0x434d` ("MC") at 0, and at least one of the 16 directory entries (at `(i+1)·0x80`, i = 0..15) with flag byte `0x51` whose 16-byte ASCII product code at +0xa matches one of the seven retail `REGIONS`. (M7-01 §11 documents the card interior — 15 usable 0x720 slots — and Q-80 on the image's provenance; this is the *detection* leg, which checks 16 directory headers.)

On prompt (stage 10 — auto first-run or the `title_convert_memcard` hotkey/mouse): a `FullScreenInputScreen` (PS1 memcard, generated campaign name) → on YES: name-in-use check, then a `CONFIRMATION` box asking whether to **delete the source files** after conversion → `SAVES.splitMemcards(memcards, name, deleteFile)` (SaveManager.java:228–284):

- reboots mods with an **empty** set (`bootMods(Set.of())` + `bootRegistries()`) — a clean-slate registry boot inside the title (M8-01 boot model);
- creates the campaign and extracts every retail slot from every card into `MemcardSavedGame`s (M7-01 §4);
- sorts them by timestamp, then for each save: restores the location globals (`locationType 1` → WORLD_MAP + `continentIndex`; `3` → SUBMAP with `RENDER_SAVE_GAME_MENU_19` in-menu context; else SUBMAP + `submapId`), renames to `<locationName> <n>` (a per-location-name counter), re-serializes via `newSave(...)`, and staggers the file's last-modified time by 1 s per save in original order;
- writes the campaign's `CAMPAIGN_NAME` config to `campaign_config.dcnf`; deletes the source card files if confirmed;
- on `IOException | InvalidSaveException | SaveFailedException` (in the title's callback): alert `…convert_failed`;
- **`finally: bootMods(MODS.getAllModIds())`** (in the title's confirm callback) — restores the full mod set no matter what happened.

After the dialog closes, `foundMemcards` is re-queried — a declined conversion, or one that kept the source files, re-prompts on the next title visit; a conversion that deleted the source files clears the condition (T7).

### 3.9 The update check and self-update (layer 4, T9)

- **Check.** At process start, `GameEngine.start()` runs `UPDATER.check(callback)` asynchronously: `Updater` (Updater.java:37–84) does a one-shot GitHub Releases fetch (`https://api.github.com/repos/Legend-of-Dragoon-Modding/Severed-Chains/releases`, 10 s connect timeout, redirects followed; IDE/custom builds with `Version.TIMESTAMP == null` skip the check). `onCheckComplete` filters the release list to tags starting with the build's `Version.CHANNEL` (or the literal `release`) **and** an `updated_at` strictly after the build timestamp, sorts by `updated_at` descending (`Release.compareTo`), and reports the first — the newest matching `Release{tag, uri, timestamp, prerelease, assetUrls}` (prerelease is parsed and displayed but not filtered out). Results sit behind `UPDATER_LOCK` as `UPDATE`/`UPDATE_CHECK_FINISHED`; `getUpdate()` (GameEngine.java:176) is the title's read side. A pre-unpack `skip()` marks the check finished so the loading screen doesn't hang on it (GameEngine.java:490–498).
- **Banner.** The title caches `update` at init and draws the banner row (§3.5).
- **`autoUpdate()`** (Ttle.java:891–913): if a platform-specific download URL exists in the release assets (asset-name keyword match, `Updater.getPlatformDownloadUrl`), `SelfUpdater.launchUpdater(url, progressCallback)` — which locates `updater.jar` and a Java executable, spawns `java -jar updater.jar <url> <gameDir> <pid> <version>`, and **the game exits** so the updater can overwrite files; the updater downloads/extracts/relaunches (SelfUpdater.java:30–96). If no platform URL, it opens the browser at the release URI instead. Progress (`UpdateState` LAUNCHING_UPDATER/DONE/FAILED + i18n message) is shown on the (10, 230) line while at the title.
- **Staged swap.** `Main.main` (Main.java:47–60) moves a staged `updater.jar.new` → `updater.jar` at the next boot before `GameEngine.start()` — the updater's own update path (M1-01 §1).

### 3.10 The GAME_OVER state (T10)

Battle defeat routes through the post-battle action registry: `Battle.java:4300` (case 2) → `CorePostBattleActions.GAME_OVER` → `GameOverPostBattleAction` sets `postBattleEngineState_800bc91c = LodEngineStateTypes.GAME_OVER` (M9-05 documents the post-battle dispatch; the state itself is owned here).

`GameOver` (LodMod `game_over`, LodEngineStateTypes.java:23; decompiled anchors @ 0x800c7558–0x800c7788): 640×240 display (`resizeDisplay(640, 240)` — the wide cinematic width), not time-advancing, not savable, LEGACY render mode, `getCamera() = null` (pure ortho). Stages (GameOver.java:78–128):

| Stage | Behavior |
|---|---|
| 0 | waits for the loader to drain (`Loader.getLoadingFileCount() == 0`), then `resetSubmapToNewGame()` — `deallocateRenderables(0xff)`, **`submapCut_80052c30 = 675` ("Opening")**, `submapScene_80052c34 = 4`, `bootMods(all)` (Scus94491BpeSegment.java:574–584: the submap is reset to the opening cut on game over) — and → 1 |
| 1 | → 2; `loadDrgnFile(0, 6667)` (the MCQ background) |
| 3 | the async `gameOverLoaded` callback (GameOver.java:69) has parsed the `McqHeader` and uploaded the image to VRAM (640, 0); tick: `deallocateRenderables(0xff)` + fade-in (10) → 4 |
| 4 | the game-over screen: renders the MCQ (ortho, z 144, offset by the GPU offset); **confirm or back** → menu sound 2, fade out (10 half-frames) → 5 |
| 5 | waits for the fade (`currentColour_28 >= 0xff`) → 6 |
| 6 | deallocates, `engineStateOnceLoaded_8004dd24 = TITLE`, `vsyncMode = 2` — **back to the title** (not directly to a new game; the player re-enters via the normal menu) |

So a defeat is: battle → post-battle action → GAME_OVER (submap silently reset to the opening cut) → title → New game → FMV 2 → submap (cut 675). The reset in stage 0 is why returning from a game-over title session always starts the opening submap regardless of where the defeat happened (T10; M9-03 will document `submapCut` semantics).

### 3.11 Cross-state hooks left by the title (T11)

- **`previousEngineState_800bdb88 == TITLE`** → in `FUN_80020ed8` (EngineStates.java:96–139, run at the end of every `gameLoop` frame): if the *new* state is SUBMAP, `_800bd7b0 = 9` (the submap transition table's "entered from the title" row) and `_800bd7ac = true` (a latch that `FUN_800218f0` re-applies as `_800bd7b0 = 9` once). These drive the submap's special entry-from-title behavior — **M9-03 scope** (documented here only as the hook; its effect is on the submap side).
- **`postCreditsEngineState`** (EngineStates.java:45): the return destination after CREDITS, set by the caller — `TITLE` from `AboutScreen` (title Links → watch credits) and `SUBMAP` from `FinalFmv` (campaign end: FMV 17 → CREDITS → the "end" submap) (Credits.java:723; Credits renders 16 TIM slots from DRGN0/5720 at 384×240, `vsyncMode = 1` — Credits.java:239 — 60 fps, and plays XA track 3 file 3 (`playXaAudio(3, 3, 1)` — the credits music leg, M6-03/Q-57 evidence)).
- **Music globals**: the title leaves `musicLoaded_800bd782 = true` and `_800bd0f0 = 2` for package 1 (§5); `loadingNewGameState_800bdc34` (set by every `loadGameState`, SaveManager.java:439) gates `stopMenuMusic`'s restore (SItem.java:792–810: when *not* mid-load, `stopMenuMusic` restores the current state's music via `restoreMusicAfterMenu()`) and the WMap/SMap re-init paths (M9-03/M9-04).

---

## 4. The update check (layer 4) — classification (T9)

Covered in §3.9. Layer-4 throughout: the reference game is a distributed application; the retail PSX binary has no updater. No game content depends on it; worst case the banner is absent (`update == null`) or the browser opens instead of the staged updater.

---

## 5. Music at the title (T12)

- **Boot preload.** `SItem.loadMenuAssets()` (SItem.java:744) loads DRGN0/6665–6666 (menu texture atlas + `UiFile`), and **music package 0** (`loadDrgnDir(0, 5815)` → `SItem.menuMusic = new BackgroundMusic(files, 5815)`) — the *menu* music, held but not played.
- **Title music.** `Ttle.initializeMainMenu` → `Audio.loadMusicPackage(1)` (Audio.java:737): `unloadSoundFile(8)`, set flag 0x80, `loadDrgnDir(0, 5820)`, and on load `musicPackageLoadedCallback(…, true)` → `playMusicPackage(new BackgroundMusic(files, 5820), true)` → `AUDIO_THREAD.loadBackgroundMusic`, **sequence volume 40** (7-bit), `startSequence()`, `_800bd0f0 = 2` (Audio.java:747–764). So the title's music **auto-starts** when its 5 files land (package index 1 → directory `5815 + 1·5 = 5820`; M6-01 §6.1 documents the 5-file package layout and the `5815 + songId·5` scheme, song id = package index).
- **Menu entry/exit.** `Menus.initMenu` calls `startMenuMusic()` (SItem.java:781, decompiled @ 0x8001e010 param 0): backs up the playing SFX queue, stops sequences, unloads package 8, `menuMusic.reset()`, plays package 0. On menu close, `loadAndRenderMenus`'s UNLOAD arm calls `stopMenuMusic()` (param −1): if not mid-game-load, `stopMusicSequence()` + `unloadSoundFile(8)` + `currentEngineState.restoreMusicAfterMenu()` (no-op for title/game-over; submap/wmap/battle restore their own — M9-03/04/05), then restores the SFX queue.
- The dual-domain main volume is set to 0x7f/0x7f by `Ttle.init()` (M6-01 §5) — the title always starts at full main volume.

---

## 6. Rendering notes (T13)

- The title renders entirely in the **legacy ortho 2D path** (`queueOrthoModel`), z-layering: background (screenspace offset, monochrome fade-in 4/255 per tick, scroll −176 → 0, clamped at 0) → logo (z 10000, texture alpha) → trademark (z 200, monochrome) → options (z 100.0/100.1) → banners/version (z 100.0/100.1) → copyright (z 200, monochrome fade-in 8/255) → fire (perspective GTE pass inside the ortho frame — `renderMenuLogoFire` re-sets the RVIEW2 and queues `QueuedModelTmd` with the retail light matrices) → logo flash (frame-buffer re-blend, z 30, only during `menuLoadingStage` 2, T3/T4).
- Widescreen: every ortho draw adds `RENDERER.getWidescreenOrthoOffsetX()`; the logo flash has three explicit layout branches for `FORCED_4_3` / `STRETCHED` / (expanded default) (Ttle.java:1411–1432 — M5-04 §2/§4.5); the mouse mapping has matching `EXPANDED` vs letterbox branches (§3.6).
- `vsyncMode = 2` throughout the title (30 fps); `GameOver` inherits it (it does not change it) until stage 6 forces 2 again.
- `renderMainMenu` is at Ttle.java:663; `resetIdleTime` at :716; the option render loops and banner draws span `renderMenuOptions` (:982–1216).

---

## 7. Discord rich presence (layer 4, T14)

`updateDiscordRichPresence`: TITLE → details "Title Screen", state null; GAME_OVER → "Game Over"; CREDITS → "Watching the Credits" (Ttle.java:1557, GameOver.java:131, Credits.java:730). Updated on every state change by `loadQueuedOverlay` (M1-01 §6).

---

## 8. The script boundary (RQ7) — zero script involvement at the title (T15)

The title sequence is the **purest native (hardcode) system** in the game:

- **No Fate scripts run at the title.** `Ttle`/`GameOver`/`Credits` carry no `getScriptFunctions` overrides, no script state of their own, and `SCRIPTS.clear()` runs in `gameInit` before FMV 0. The script engine is dormant until the submap/wmap/battle enters (their script natives are installed per-state by `loadEngineStateFunctions`, M1-02/M3-05).
- **No script patches touch the title.** `patches/scripts.csv` (353 entries, M8-03 §3) has no title-state script; the only "title" mention is the campaign-end script `SECT/DRGN24.BIN/834/1` ("The end, use confirm or cancel to return to title screen") — a *return-to-title* exit path, not title logic.
- **The only retail cutscenes in the chain are FMV assets, not scripts.** FMV 0 (boot) and FMV 2 (new campaign) are disc MDEC files played by the native `Fmv` path; the transition *after* them is a native engine-state queue, not a script hand-off. (By contrast, in-map FMVs are script-triggered — `SMap.fmvIndex_800bf0dc` — M9-03.)
- **Everything gameplay-facing at the title is decompiled native code**: the menu, the fades, the game-over reset. The layer-4 additions (campaign selection, memcard/uncategorized flows, update, mouse) are reference Java, not scripts.
- The **retail-vs-reference delta** at the title is therefore: (a) the reskin (logo/menu-texture PNGs replace MRG files 2/3 — T2); (b) "Continue" opens a **campaign selection screen** (layer 4, M9-07) instead of the retail save-list UI — the `@Method(0x800c7fa0)` anchor on `waitForSaveSelection` shows the method sits at a retail title-function address (the decompiler preserves the original address of the function it replaced); whether the retail function there was the save-slot-list wait or something else is **asset-validated-pending** — recorded as Info, not a register item, since the reference behavior is fully determined and the retail UI has no bearing on the Rust port's behavior contract; (c) the layer-4 banners/hotkeys and the Discord presence.

---

## 9. Classification table (T1–T15)

| # | Finding | Layer | Confidence |
|---|---|---|---|
| T1 | Title identity/lifecycle: `title` engine state (CoreMod), decompiled retail (@Method anchors), `advancesTime = false`, not savable, LEGACY render mode, `vsyncMode = 2`, 384×240, `init()` resets sound env + main volume 0x7f/0x7f both engines | 3 (decompiled) | source-verified (retail rendering identity: asset-validated-pending) |
| T2 | Asset map: DRGN0/5718 MRG (background/TM/copyright/fire texture source; **files 2/3 unused — replaced by `gfx/ui/logo.png` and `gfx/ui/title_menu.png`**), DRGN0/5719 fire TMD, DRGN0/6667 game-over MCQ, fire = per-tick 15-bit pixel-row rotation of the 16×64 VRAM blocks at (944+i·16, 256) (fixed per-flame offsets 59/1/59/59) | 3 + 4 reskin | source-verified (retail labels behind the modern menu texture: asset-validated-pending) |
| T3 | 11-case `loadingStage` machine over 0–11 (stage 6 absent), 4-stage intro choreography (background scroll −176→−40, logo fade 8/255, sine logo flash over the previous frame buffer, `REDUCE_MOTION_FLASHING_CONFIG` gate), idle 3↔4 re-arm, first-run prompts (9/10) are session-flagged one-shots that re-prompt per title visit | 3 (choreography) + 4 (prompts, gate) | source-verified |
| T4 | Menu model: 5 options, widths {407,257,227,189,141}, dual-quad shadow/face at 1/3 scale, per-state transparency dynamics (entry +4 caps 176/64; selection +8/−16 caps/floors 160/64), option 1 disabled-skip + alpha/2 when no campaigns, banner rows + version + progress line | 3 (decompiled) + 4 (banners, mouse) | source-verified |
| T5 | Input: `menu_up`/`menu_down`/`menu_confirm` (wrap, skip, sounds 1/2), `title_update` (Y/U), `title_convert_memcard` (X/C), full mouse hover/click layer with pointer cursor (gated by `DISABLE_MOUSE_INPUT_CONFIG` + gamepad presence) | 3 (gamepad/keyboard) + 4 (mouse) | source-verified |
| T6 | Option flows and the fade contract: fade out 30 vsyncs → `initMenu(RENDER_NEW_MENU, …)` + `deallocate()` + menu rendered by the global menu path; cancel → **full title re-initialization** (stage 0, music replayed, choreography replayed); quit → 16-tick fade → window close; new game → FMV 2 → SUBMAP; load → campaign selection → `loadGameState` + queued save state | 3 (flow shape) + 4 (campaign selection, AboutScreen) | source-verified |
| T7 | Adoption flows: uncategorized-save prompt (directory move into a new campaign, name generation `New Campaign [n]`, in-use alert) and PS1-memcard prompt (detection: 0x20000 bytes + "MC" + 16 slot headers `0x51`+region; conversion: name prompt → delete confirm → clean mod boot → per-slot extraction → timestamp-sorted re-serialization with 1 s-mtime staggering and `<locationName> <n>` renames → campaign config write → `finally bootMods(all)`; re-query after dismiss) | 4 | source-verified (retail memcard bytes: asset-validated-pending) |
| T8 | FMV 0 = `STR/DEMOH.IKI` (the retail disc's opening cutscene; 18-FMV `STR/*.IKI` inventory per M2-09): `playCurrentFmv(0, TITLE)` — sssq reset, `submapId = −1`, rumble track from `DRGN0/5721/<idx>` (0xc-byte records, −1 terminated; M2-09), 2×-CD playback (10 sectors/frame, 150 sectors/s, 15 fps), in-band XA 37800 Hz stereo halved, double-press skip, mode-state save/restore, hand-off via `engineStateOnceLoaded` consumed by `loadQueuedOverlay` at the next frame top; the FMV is a render-callback takeover, not an engine state (no state exists before TITLE) | 3 (asset/behavior) + 4 (decoder, skip UI, rumble) | source-verified (M5-04 §4, M6-03) |
| T9 | Update check (one-shot GitHub Releases fetch at boot: channel-tag + newer-than-build-timestamp filter, newest `updated_at` wins, IDE builds skipped; `getUpdate()` read side), banner + hotkey, `SelfUpdater` external `updater.jar` process (game exits; staged `updater.jar.new` swap at next boot), platform archive by asset-name keyword (Windows / MacOS_M1 / MacOS_Intel / Linux_ARM64 / Linux, excluding `Steam_Deck` assets), browser fallback, progress line | 4 | source-verified |
| T10 | GAME_OVER: post-battle action `GAME_OVER` (Battle.java:4300 case 2), 640×240, 7-stage machine, `resetSubmapToNewGame()` at stage 0 (submapCut 675 "Opening", `submapScene 4`, `bootMods(all)`), MCQ 6667 at VRAM (640,0), confirm/back → fade → TITLE (never directly to a new game) | 3 (decompiled) | source-verified |
| T11 | Cross-state hooks: `previousEngineState == TITLE` → submap `_800bd7b0 = 9` + `_800bd7ac` latch (FUN_80020ed8/FUN_800218f0 — effect is M9-03); `postCreditsEngineState` caller-set (TITLE from AboutScreen, SUBMAP from FinalFmv); credits = 16 TIMs DRGN0/5720, 384×240, 60 fps, XA (3,3,1) | 3 | source-verified |
| T12 | Music: menu package 0 (5815) preloaded at boot, title package 1 (5820) auto-plays at 7-bit sequence volume 40, `startMenuMusic`/`stopMenuMusic` (0x8001e010 params 0/−1) around menu entry/exit, `restoreMusicAfterMenu` no-op for title (music resumes via re-init) | 3 (packages) | source-verified |
| T13 | Rendering: legacy ortho z-layering (background → logo z 10000 → TM/copyright z 200 → options z 100.0/100.1 → fire (perspective pass with retail light matrices) → flash z 30), widescreen ortho offset on every draw + three flash layout branches, 30 fps | 3 (decompiled) + 4 (frame-buffer flash implementation) | source-verified |
| T14 | Discord rich presence strings per state | 4 | source-verified |
| T15 | Script boundary: zero scripts at the title (no script functions, `SCRIPTS.clear()` pre-FMV, no title patches in `scripts.csv`); FMVs are assets not scripts; the retail-vs-reference deltas are the reskin, the campaign-selection "Continue" (retail save-list UI: Inferred, asset-validated-pending), and the layer-4 flows | 3/4 | source-verified (retail "Continue" UI: Inferred) |

**Java-mechanism table (J25–J27):**

| # | Mechanism | Observable consequence | Incidental (not a reference requirement) |
|---|---|---|---|
| J25 | Async load pipeline: `loadDrgnDir`/`loadDrgnFile` futures set `texturesLoaded`/`fireLoaded` flags; the save scan runs on `Async.run` with an `exceptionally` fallback to empty lists | The title never blocks a frame on IO; a failing save scan degrades to "no campaigns / no memcards / nothing to categorize" | The future/completable type, the flag fields |
| J26 | Session-flag one-shot prompts (`saveCategorizationShown`, `memcardConversionShown` are instance fields, not config) | Prompts re-appear on every title visit until the condition clears; a game restart re-prompts if the condition still holds | The field placement (vs a persistent config entry) |
| J27 | `bootMods(Set.of())` + `bootRegistries()` inside `splitMemcards`, with `finally bootMods(all)` in the title's confirm callback | Memcard conversion deserializes retail saves against a clean (no-mod) registry view, then restores the full mod set — a mid-title registry reboot (the memcard-flow boot among the M8-01 `bootMods` call-site census) | The empty-set idiom |

**Register impact:** none. The retail "Continue" UI (T15) is recorded as Info (Inferred) — the reference behavior is fully source-verified and the retail UI has no bearing on the behavior contract; no new register item is warranted.

---

## 10. Cross-references

- M1-01 §3–§7 — boot sequence, `transitionToGame`, engine-state machine, shutdown (this document's §2 spine).
- M1-02 — `gameLoop` @ 0x80011e1c frame order (`loadQueuedOverlay` at top, `loadAndRenderMenus`, `handleFullScreenEffects`, `FUN_80020ed8` at bottom).
- M2-01 §6/§8 — DRGN virtual-path scheme and top-level tree; M2-09 — FMV file layout incl. the per-FMV rumble tables at `DRGN0.BIN/5721/<idx>` (this document's §3.2 first documents the title's specific sector list: 5718/5719/5720, 5815–5824, 6665–6667).
- M5-03 G4 — the 384-wide mode-0 menu width (title) vs 320 vs 640 (game over).
- M5-04 §4 — FMV presentation, 15 fps / 2×-CD cadence, skip flow, `VideoPlayer`, FMV call sites; §2/§4.5 widescreen mode handling (the flash's three branches).
- M6-01 §5.1/§6.1 — dual-domain main volume (0x7f at title init) and the 5-file music packages (`5815 + songId·5`, sequence volume 40).
- M6-03 — FMV in-band XA at 37800 Hz (Q-48 closed), `playXaAudio(3, 3, 1)` credits leg (Q-57 evidence).
- M7-01 — campaign model, save naming, memcard slot layout (this document's §3.7/§3.8 are the title-side flows), `loadGameState` ordering.
- M7-02 — `OptionsPresetsScreen` (option 2 destination), preset/config model.
- M8-01 — `bootMods` call sites (the J27 mid-title reboot); M8-02 — `EngineStateChangeEvent`, the `title`/`game_over`/`credits` engine-state registrations; M8-03 — `scripts.csv` census (T15 negative evidence), i18n keys (`lod_core.ui.title.*`).
- Upcoming M9-03 (submap) — `submapCut` 675 "Opening", the `_800bd7b0 = 9` / `_800bd7ac` from-title entry, `restoreMusicAfterMenu` in SMap, script-driven FMVs; M9-04 (world map) — `loadingNewGameState` in WMap; M9-05 (battle) — the `GAME_OVER` post-battle action dispatch and `PlayFmvPostBattleAction`; M9-07 (inventory & items) — `CampaignSelectionScreen`, `NewCampaignScreen`, `AboutScreen`, `OptionsPresetsScreen`, `FullScreenInputScreen`, the menu stack and `WhichMenu` machinery.
