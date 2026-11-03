# Application Lifecycle and Initialization

Status: Complete (R1). Repository: Severed-Chains @ `0c08c679b`. All line references are to that revision unless noted.

The lifecycle has two distinct layers:

1. **Host bootstrap** (reference-implementation code): process start → config/load screens → transition to the game.
2. **ROM lifecycle** (decompiled game code): `Scus94491BpeSegment.main()` → per-frame `gameLoop()` → shutdown.

The host bootstrap wraps the ROM lifecycle; the ROM code itself does not know the host exists (its `main()` is the original ROM entry point, preserved with its address `@Method(0x800e5d44L)`).

---

## 1. Pre-startup: staged updater swap

`Main.main()` (Main.java:51) first swaps in a staged updater if the previous run prepared one: if `updater.jar.new` exists, it is atomically moved over `updater.jar` (with a non-atomic fallback). This means *the currently running updater is the one downloaded by the previous run* — a two-phase update protocol. Failure here is non-fatal (logged, startup continues with the old updater). `Verified`.

The rest of `Main.main()` (Main.java:68-135) is a top-level `try` around `GameEngine.start()` with a **crash-recovery catch** (see §7).

## 2. Class-load-time initialization (service construction)

`GameEngine` builds all services as static fields. Java's class-initialization order fixes the construction sequence (GameEngine.java:92-145):

| # | Service | Note |
|---|---|---|
| 1 | `MODS` (ModManager) | core mod IDs `lod`, `lod_core` reserved |
| 2 | `LANG` (LangManager) | |
| 3 | `EVENTS` (EventManager) | error callback → `GameEngine.onModError` → `GameOverlay` notification |
| 4 | `REGISTRIES` | the 21 game registries (see architecture/01) |
| 5 | `SCRIPTS` (ScriptManager) | patch dirs `./patches/libs`, `./patches/scripts`, `./patches` |
| 6 | `SEQUENCER` (game `legend.game.sound.Sequencer` facade) | distinct from the core audio sequencer (see execution model, audio) |
| 7 | `CONFIG` (ConfigCollection) | registry-driven typed config entries |
| 8 | `SAVES` (SaveManager) | version `SaveVersion.V10`, serializer `V10Serializer::toV10` |
| 9 | `PLATFORM` (SdlPlatformManager) | not yet initialized — construction only |
| 10 | `RENDERER` (RenderEngine) | construction only (main batch + scissor stack) |
| 11 | `FONTS` (FontManager) | `DEFAULT_FONT` from `./gfx/fonts/default.json` |
| 12 | static block (GameEngine.java:126-144) | load-or-create `config.conf` (flat global config); construct `GTE`, `GPU`, `SPU`, `AUDIO_THREAD` (stereo, 24 voices, Double interpolation, Quadruple pitch, Double effects); **name the current thread "Hardware"**; create `openalThread` (named "OPEN_AL", not yet started) |

Key architectural consequence (`Verified`): the **"Hardware" thread is whichever thread first triggers `GameEngine` class initialization** — in practice the process main thread (via `Main.main()` → `GameEngine.start()`). All rendering, game logic, input, and SPU-model tick scheduling anchor to it. This is a language-agnostic property of the design (single owner thread for the hardware frame loop), though the *naming mechanism* is incidental.

## 3. Startup sequence (`GameEngine.start()`, GameEngine.java:182-277)

Numbered in source order. T = executing thread.

1. **T=main**: `UPDATER.check(callback)` (185). Asynchronous HTTP GET to the GitHub Releases API (`https://api.github.com/repos/Legend-of-Dragoon-Modding/Severed-Chains/releases`), filtered by channel tag (`Version.CHANNEL` or `release`) and build timestamp; on success sets `UPDATE` under `UPDATER_LOCK`. Skipped for builds without a timestamp (IDE/custom builds). Never blocks startup; failure is logged. `Verified`.
2. **T=main**: `loadLangOverrides(ORIGINAL_LOCALE)` (192) — loads `lang/` overrides for the user's original locale (the JVM default locale was captured before `Main`'s static block forced `Locale.US`).
3. **T=main**: create the **unpack thread** (194). Its body (196-238):
   1. sets the render callback to `GameEngine::loadGfx` (loading screen) — *cross-thread render-callback registration before `RENDERER.init()`*;
   2. `Files.createDirectories("saves")`;
   3. under `INIT_LOCK`: `Unpacker.unpack()` with a status listener (→ `statusText` on the loading screen); on `UnpackerException`: statusText = "unpacker.failed", log, `skip()`, **return** (see §6 failure); on `UnpackerStoppedRuntimeException` (user closed the window during load): return; then `ScriptPatcher.apply()` (patch `files/` scripts, cache in `files/patches/cache`, backups in `files/patches/backups`); then wait (statusText "checking for updates") for `UPDATE_CHECK_FINISHED`; finally `unpackerLoading = false`.
4. **T=main**: `PLATFORM.init()` (240) — SDL3 init (video + gamepad), `gamecontrollerdb.txt` mappings, open all gamepads. *Note: happens before the unpack thread is started.*
5. **T=main**: `thread.start()` (242) — unpacking begins in parallel with the rest of startup.
6. **T=main**: `MOD_ACCESS.findMods("./mods", Version.VERSION)` (245) — scan mod jars.
7. **T=main**: `bootMods(MODS.getAllModIds())` (246) — full mod boot (see §4).
8. **T=main**: `ConfigStorage.loadConfig(CONFIG, GLOBAL, "config.dcnf")` (248) — load the typed global config.
9. **T=main**: `DEFAULT_FONT = FONTS.get(...)` (250) — font selected by `RETAIL_FONT_CONFIG` (retail TIM font vs modern TTF).
10. **T=main**: `AUDIO_THREAD.init()` (252) — open OpenAL device (selected from config, else default, else first), create context; start a single-thread scheduler that polls the default device every 2 s; register the two default sources: the **core sequencer** (SSSQ music synthesis) and the **XaPlayer** (Opus). Then apply volume/interpolation/pitch/effects configs from `CONFIG`.
11. **T=main**: `SPU.init()` (258) — registers the SPU's `GenericSource` (stereo 16-bit at `BASE_SAMPLE_RATE`) on the audio thread.
12. **T=main**: `RENDERER.init()` (259) — create SDL window (OpenGL 3.3 core, fallback GLES 3.2; min size 320×240; swap interval 0 = no vsync), register window event handlers (resize, mouse, key, input actions), load the five shader families (simple/copy/screen/standard/tmd/battle_tmd), create uniform buffers, post quad, quad/line helpers, double render buffers; register the big `onDraw` handler (see execution model).
13. **T=main**: `RENDERER.events().onClose(Async::shutdown)` (260).
14. **T=main**: `GPU.init()` (261) — VRAM textures (1024×512, 15-bit packed + 24-bit), display mesh, display size 320×240, resize hook.
15. **T=main**: `DISCORD.init()` (262).
16. **T=main**: `openalThread.start()` (263) — the audio thread's 120 Hz loop starts.
17. **T=main**: `time = System.nanoTime()`; `RENDERER.run()` (265-268) — enter the render loop (blocking; see execution model).
18. **T=main** (after `run()` returns, `finally` 269-275): `DISCORD.destroy()` → `AUDIO_THREAD.destroy()` → `RENDERER.delete()` → `UPDATER.delete()` → `PLATFORM.destroy()`.

`INIT_LOCK` serializes the unpack thread's completion against the main thread's transition-to-game (which also takes the lock, §5).

## 4. Mod boot (`bootMods`, GameEngine.java:289-335)

`bootMods(modIds)` is the single re-entrant boot routine:

1. `MOD_ACCESS.reset()`, `EVENT_ACCESS.reset()`, `REGISTRY_ACCESS.reset()` — full framework reset.
2. `MOD_ACCESS.loadMods(modIds)` → returns missing mod IDs. Per-mod failures are recorded (version mismatch → `wrongVersion`; instance-creation failure → `failedToLoad`) rather than thrown.
3. `EVENT_ACCESS.initialize(MODS)` — reflectively discover all event listeners in loaded mods.
4. `EVENTS.postEvent(new AddRegistryEvent(REGISTRIES))` — mods register their custom registries.
5. Initialize a fixed set of registries needed by the title screen: `config`, `campaignTypes`, `engineStateTypes`, `inputActions`, `characterTemplates`, `statTypes`, `statModTypes`, `goods` (the rest via `bootRegistries()` later, §5).
6. `MOD_ACCESS.loadingComplete()`.
7. `InputBindings.initBindings()` (reset action states + `RegisterDefaultInputBindingsEvent` so mods contribute default bindings) and `InputBindings.loadBindings(CONFIG)` (user overrides from config).
8. `loadLang()` — reset + reinitialize the i18n for the configured language, reload `lang/` overrides.
9. Post `GameOverlay` notifications (7 s) for failed and wrong-version mods.

Call sites (`Verified`): startup (GameEngine.java:246); new game (`resetSubmapToNewGame`, Scus94491BpeSegment.java:582); title (`Ttle.java:570`); save management (`SaveManager.java:232` — with an empty mod set); and **campaign selection** (`CampaignSelectionScreen.java:133-255` — per-campaign, using the campaign's `ENABLED_MODS_CONFIG` set, falling back to all mods). The reset semantics mean *boot is a re-entrant operation tied to campaign changes, not just a startup phase*: events, registries, bindings, and language are all rebuilt from scratch on every boot, and different campaigns can run with different mod sets. `Verified`.

## 5. Loading screen → transition to game

While the render loop runs, the render callback is `GameEngine.loadGfx` (GameEngine.java:449), invoked once per presented frame by the renderer:

- Sets translucency, loads `gfx/ui/ui.png` (persistent), the "loading eye" and "no slop" textures, a textured quad, the window icon.
- Plays the intro video `gfx/intro.mp4` via `VideoPlayer.play` with `renderIntro` as its draw callback; on completion sets `cinematicFinished = true` and resets the render callback to `renderIntro`.
- Registers one-shot loading-screen input handlers: any key (no mods), button, or mouse release → `skip()` (forces the update-check flag, zeroes the animation clock, starts the fades).
- Window close during loading → `Unpacker.stop()` (raises `UnpackerStoppedRuntimeException` in the unpack thread; the window also closes the loop).

`renderIntro` (GameEngine.java:507) draws the loading UI every frame. The **transition gate** is:

```
cinematicFinished && !unpackerLoading && UPDATE_CHECK_FINISHED  →  transitionToGame()
```

`transitionToGame()` (GameEngine.java:389) runs under `INIT_LOCK` (serialized with the unpack thread's final flag write):

1. Deletes loading-screen resources (eye/noslop textures, textured quad) and removes the loading-screen event handlers (resize, key, mouse, button, close).
2. **`Scus94491BpeSegment.main()`** (605-608) — the ROM entry: `gameInit()` then `preload()`.
3. `TmdObjLoader.fromModel("Shadow", shadowModel)` + mark shadow model parts persistent; `loadMenuAssets()`; `initTextboxGeometry()`; `battleUiParts.init()`.
4. **`Audio.startSound()`** — starts the **SFX thread** (60 Hz loop ticking the game sequencer facade + the SPU model; see execution model, audio).
5. **`bindRendererEvents()`** (Scus94491BpeSegment.java:196-224) — registers: key-press debug hotkey (Ctrl+W in battle), input-action press/release → `currentEngineState.inputActionPressed/Released`, **`RENDERER.setRenderCallback(Scus94491BpeSegment::gameLoop)`** (the game loop becomes the frame callback), and window-close → `stopSound()`, `AUDIO_THREAD.stop()`, JavaFX `Platform.exit()`.
6. `Fmv.playCurrentFmv(0, CoreEngineStateTypes.TITLE)` — plays FMV slot 0 (the intro cutscene) with the TITLE state after it.

### ROM initialization

`gameInit()` (Scus94491BpeSegment.java:605-632, `@Method(0x800e5d64L)`), in order:

1. `ResetGraph()`.
2. Ordering-table configuration: `orderingTableBits = 14`, `zShift = 0`, `orderingTableSize = 0x4000`, `zMax = 0x3ffe`, `GPU.updateOrderingTableSize(...)`.
3. `setDrawOffset()`; clear color RGB = 0.
4. `InitGeom()`; `setProjectionPlaneDistance(640)`.
5. `initSound()`.
6. `vsyncMode_8007a3b8 = 2` (→ 30 Hz base); `tickCount = 0`.
7. `loadSystemFont()` — `font.tim` (from `files/`) → VRAM rect (832, 424, 64×56) + 32-entry CLUT at (832, 422).
8. `SCRIPTS.clear()`.
9. `loadShadow()` — `shadow.tim` → GPU; `shadow.ctmd` + `shadow.anim` → shadow model.
10. `clearRenderables()`.
11. `initFmvs()` — resets the FMV counter.

`preload()` (635-644, `@Method(0x800e6184L)`): `drgnBinIndex = 1`; `loadMenuSounds()`; `resizeDisplay(320, 240)` (native resolution); `vsyncMode = 2`; `loadBasicUiTexturesAndSomethingElse()` — asynchronously loads the DRGN0 MRG directory 6669 (basic UI: game font, JP font, JP text, dialog border, arrow) and on completion uploads to VRAM (async-thread VRAM write under the GPU's `vramLock`), sets `textZ = 13`, `initTextboxes()`. `Verified`.

## 6. Engine state machine (summary)

The game is a single-instance engine-state machine (registry of 7 retail states: TITLE, CREDITS, SUBMAP, WORLD_MAP, BATTLE, FINAL_FMV, GAME_OVER; mod-extendable). Transitions are *queued*: any code path sets `engineStateOnceLoaded_8004dd24` (+ optional `Tag` save data), and the **top of the next frame** (`loadQueuedOverlay()`, EngineStates.java:47) performs the switch: reset menu stack, record previous state, force `vsyncMode = 2`, `loadGameStateOverlay()` (destroy old state, construct new, `init()`, optional `readSaveData`, install per-state script-native function table, set render mode + projections), then post `EngineStateChangeEvent` and update rich presence. Full details: `research/platform/04-engine-states.md`.

## 7. Shutdown, cleanup, and failure behavior

### Normal shutdown (window close)

1. SDL posts a close request; `tickInput` dispatches `window.close()` (SdlWindow marks `shouldClose`).
2. Close callbacks fire (all registered on the window): the game's handler (`stopSound()` → SFX thread stops; `AUDIO_THREAD.stop()` → audio thread exits its loop and destroys OpenAL; JavaFX `Platform.exit()` — only meaningful if the debugger opened a JavaFX app), `Async::shutdown` (file-IO pool), `PLATFORM::stop` (sets `running = false`).
3. The platform loop exits after the current iteration (it also destroys windows whose `shouldClose` is set at loop top).
4. `RenderEngine.run()`'s `finally`: `Config.save()` (write `config.conf`).
5. `GameEngine.start()`'s `finally`: `DISCORD.destroy()`, `AUDIO_THREAD.destroy()` (no-op if the thread already cleaned up), `RENDERER.delete()` (delete all shaders/objs/textures), `UPDATER.delete()` (close HTTP client), `PLATFORM.destroy()` (free SDL event struct, `SDL_Quit`).
6. The main thread returns from `main()`; remaining non-daemon threads (audio/SFX already stopped; Async pool shut down; the 2 s audio device scheduler is shut down inside `AudioThread.destroy()`) end, and the JVM exits.

`Verified` for the ordering; the "JVM exits" step is `Inferred` (no explicit exit call on the clean path).

### Failure and recovery

| Failure | Detection | Behavior |
|---|---|---|
| Any `Throwable` escaping `GameEngine.start()` (including mid-game crashes) | `Main.main()` catch (Main.java:72-135) | If `gameState != null` and `CREATE_CRASH_SAVE_CONFIG` is enabled: load all existing saves (async), generate a unique name, restore `submapScene`/`submapCut` from the live collision globals, restore `lastSavableEngineState` (fresh instance), and write a **"Crash Recovery"** save via `SAVES.newSave`. Then log version/mods/failed-mods, `LogManager.shutdown()`, copy `debug.log` to `CRASHLOG-<timestamp>.log`, `System.exit(1)`. |
| Unpack failure (`UnpackerException`) | unpack thread | Status text "unpacker.failed" shown on the loading screen; the game **does not proceed** — the transition gate never opens (`unpackerLoading` stays true). The user is left on the loading screen with the error. `Verified`. |
| Unpack stopped (close during load) | `UnpackerStoppedRuntimeException` | Unpack thread returns; window close ends the loop; clean shutdown. |
| Update check failure | `Updater.get()` | Logged; `UPDATE` stays null; the game runs as-is. |
| Mod fails to load (version/instance/duplicate) | `ModManager.loadMods` | Recorded in `wrongVersion`/`failedToLoad` maps; notifications on the title screen; boot continues. |
| Mod event handler throws | `EventManager.postEvent` | Non-critical events: logged + error callback → `GameOverlay` notification, other listeners continue. **Critical** events: rethrown as `EventHandlerException` → crash path above. `Verified`. |
| Audio device lost/changed | `ALC_EXT_disconnect` poll + 2 s default-device poll (audio thread + scheduler) | `AudioThread.reinit()` reopens device/context and re-`init`s every source, preserving active-source state; if init fails, the thread parks (`paused`) and retries on the next poll. The game continues silently. `Verified`. |
| OpenGL context creation failure | `SdlWindow` ctor | Falls back GLES 3.2 → if that also fails, `RuntimeException` (crash path). |

### Crash-recovery state provenance

`lastSavableEngineState` (EngineStates.java:29) records the last state in which the game could save; the crash handler reconstructs that state type and writes the save through it. `submapScene_80052c38`/`submapCut_80052c30` are copied into the new `GameState52c` before the save, so a crash mid-submap recovers to the current cut/scene. `Verified`.

## 8. Thread inventory at steady state

| Thread | Name | Rate | Responsibility |
|---|---|---|---|
| Main | "Hardware" | per-frame (30 Hz base) | platform loop: input tick, render tick (swap + onDraw → gameLoop + GPU model + render pipeline), engine states, scripts, GPU/GTE/SPU control |
| Audio | "OPEN_AL" | 120 Hz (tight loop while the sequencer can buffer) | OpenAL: core sequencer PCM synthesis, Opus decode (XaPlayer), buffer management, SPU sample consumption, device watch |
| SFX | "SFX" | 60 Hz | game sequencer state machine (BGM commands/fades) + SPU model (24 voices, ADSR, reverb) → SPU GenericSource |
| Unpack | (unnamed) | startup only | ISO unpack + script patching |
| Async pool | fixed, `availableProcessors` threads | on demand | file I/O (CompletableFutures), ad-hoc async work |
| Audio device poll | single-thread scheduled executor | every 2 s | default-device change detection |
| JavaFX | on demand | — | debugger (and updater UI) |
| Discord SDK | internal | — | rich presence (ticked from the main thread in gameLoop) |

Cross-thread interaction details: `research/architecture/02-subsystem-interactions.md` and `03-state-and-ownership.md`.

## 9. Open items

- The exact original timing of `gameInit`/`preload` relative to the ROM's vblank (the reference runs them inside the transition callback; the ROM ran its equivalent at boot) — immaterial to observable behavior, noted for completeness. Register: Q-39.
- Whether any code path relies on the *order* of the close callbacks (game handler vs `Async::shutdown` vs `PLATFORM::stop`) — not observable; list order is registration order.
