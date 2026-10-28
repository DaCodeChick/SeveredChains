# Source Architecture — Severed Chains (Java) Reference Implementation

Reference commit: **`ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb`** (branch `main`)
All paths relative to `~/Documents/GitHub/Severed-Chains`.
Status: **verified from source** unless marked *assumption* or *open*.

## 1. Project Shape

- ~1,326 Java files, Java 25, Gradle. Main class `legend.game.Main`.
- Two top-level packages:
  - `legend.core` (244 files) — reusable engine: audio, font, gpu (PS1 VRAM),
    gte, lang, memory, platform (SDL3/JavaFX/input), renderer (OpenGL), spu, tags.
  - `legend.game` (973 files) — the game: combat, submap, wmap, title, inventory,
    saves, scripting, textures, tim, tmd, dabas, debugger, fmv, i18n, sound,
    unpacker, **modding**, characters, additions, types.
  - `legend.lodmod` (95 files) — **retail content implemented as a mod**
    (`LodMod`, `@Mod(id="lod")`): characters, items, equipment, spells,
    encounters, campaigns, configs, elements.
  - `legend.turnorder` (5 files) — bundled example mod (turn-order HUD).
  - `discord` — Discord rich presence (JNA + discord-sdk4j).
- The codebase **mirrors the original ROM**: classes and fields named after
  decompiled symbols with memory addresses (`Scus94491BpeSegment_8005`,
  `gameState_800babc8`, `vsyncMode_8007a3b8`), `@Method(0x80011e1cL)` annotations,
  and global state held in static fields on segment classes. *This organization
  exists for traceability during reverse engineering; the native rewrite should
  use clean names while preserving behavior.*
- External `org.legendofdragoon` libraries (separate repos, jitpack):
  `mod-loader:4.3.5` (mods/events/lang/registries), `script-recompiler:0.7.11`
  (story-script disassembly/"fate" patching), `mogul-dabas:0.0.1` (DABA save
  types). **Not in this repo** — see open questions.
- Bundled non-game resources: `gfx/` (fonts, UI PNGs, **shaders**, goods icons,
  intro.mp4), `patches/` (script patches, replacements, meta CSVs),
  `gamecontrollerdb.txt` (SDL game controller DB), `lang/` (i18n overrides),
  `log4j2.xml`.

## 2. Startup & Shutdown (verified)

`Main.main`:
1. log4j setup; `Locale.setDefault(US)`; swap staged `updater.jar.new` if present.
2. `GameEngine.start()`:
   - **Updater check** (background thread, `Updater` → GitHub release API).
   - **Unpack thread** (background): `Unpacker.unpack()` (discs → `files/`)
     under `INIT_LOCK`; then `ScriptPatcher.apply()` (`patches/` → fate-patched
     scripts into `files/patches/cache`); status strings drive the intro screen;
     skippable (`Unpacker.stop()` on close).
   - `PLATFORM.init()` — SDL3 init (VIDEO|GAMEPAD), gamepad mappings, displays.
   - `MODS.findMods("./mods", version)` + `bootMods(...)`: load mod classes,
     reset/init event bus, mod registry discovery (`AddRegistryEvent`), core
     registry init (config → campaignTypes → engineStateTypes → inputActions →
     characterTemplates → statTypes → statModTypes → goods), `loadingComplete`,
     default+user input bindings, lang init, mod-error overlay notifications.
   - Global config load (`config.dcnf` → `ConfigCollection`).
   - `AUDIO_THREAD.init()` (OpenAL device, 120Hz tick thread, 24 voices,
     `InterpolationPrecision.Double`, `PitchResolution.Quadruple`,
     `EffectsOverTimeGranularity.Double`), volume config.
   - `SPU.init()` (24 SFX voices → OpenAL source), `RENDERER.init()` (window,
     shaders, UBOs, quads), `GPU.init()` (emulated VRAM), Discord init,
     `openalThread.start()`.
   - `RENDERER.run()` — the main loop (blocking).
3. `finally`: Discord → AUDIO_THREAD → RENDERER → UPDATER → PLATFORM destroy.
4. Top-level `Throwable` → **crash recovery save** (if config-enabled) from
   `lastSavableEngineState` + submap position, detailed crash log
   (version/commit/timestamp, loaded mods, failed mods), `CRASHLOG-<ts>.log`,
   exit(1).

## 3. Main Loop & Threading (verified)

**Single-threaded game loop** (`PlatformManager.run()`):
- `Action` = fixed-period scheduler (nanos per tick, skip-to-catch-up if >2
  periods behind). Two actions per frame:
  - **Input action** @ 60Hz (rate reprogrammed per frame by the game): drains
    `SDL_PollEvent` (keyboard, mouse, gamepad buttons/axes, window events),
    decodes to `InputKey/InputButton/InputAxis`, resolves `InputBinding`s →
    `InputAction` pressed/released (with repeat tracking), posts
    `InputPressedEvent/InputReleasedEvent` to the mod event bus, rumble
    management (incl. intensity lerp over time).
  - **Render action** @ NHz (N = 60 default; reprogrammed per frame):
    `SDL_GL_SwapWindow` → `onDraw` → `PLATFORM.clearPressed()`.
- Everything else runs inside `onDraw` (see §4) — **no separate sim thread**.
- Other threads: OpenAL audio (120Hz, `AudioThread` — music sequencer + SPU mix
  into AL buffers), `Async` fixed pool (n = #cores) for file loads/unpack/video,
  JavaFX toolkit thread (debugger + updater UI), updater HTTP (Completable).
- `SDL_GL_SetSwapInterval(0)`: **uncapped vsync; pacing is via the Action
  scheduler**, not the GPU. The game reprograms the render action's rate per
  frame (see §5).

## 4. Per-Frame Game Loop (verified, `Scus94491BpeSegment.gameLoop`)

Order (fixed): `Graphics.endFrame` → `GPU.startFrame` →
timestamp (`gameState.timestamp_a0 += vsyncMode` if `engineState.advancesTime()`)
→ **dynamic rate**: `hz = 60 / max(1, vsyncMode) * speedMultiplier` → set render
+ input action rates → `loadQueuedOverlay` (deferred engine-state transitions)
→ `renderUi` (background/menu UI) → `engineState.tick()` → post `RenderEvent`
(mods draw) → `loadAndRenderMenus` + `uploadRenderables` → `SCRIPTS.tick()`
(story script VM; returns whether any script ticked) → `engineState
.postScriptTick(scriptsTicked)` → battle-transition render →
`handleFullScreenEffects` → `startQueuedSounds` (SPU key-on/off) → textbox
update+render → `engineState.overlayTick()` → submap transition flags
(`FUN_80020ed8`) → `tickCount++` → `GPU.endFrame` → Discord tick.

Then the RenderEngine continues: post/`screen` shader → buffer swap (§7.1).

**EngineState base class** (`legend.game.EngineState<T>`): `init/destroy`,
`tick`, `postScriptTick`, `overlayTick`, `tickMultiplier`, `advancesTime`,
`writeSaveData`→Tag / `readSaveData(Tag)`, `getRenderMode`, input hooks,
`canSave`, Discord presence, `getChapter`. Concrete states: Title, Submap
(`SMap`, 5,755 lines), Battle (`Battle`, 9,375 lines), WorldMap (`WMap`, 6,245
lines), plus mod-registerable types (`EngineStateType` registry;
`CoreEngineStateTypes`: TITLE, MENU, ...; `LodEngineStateTypes`: SUBMAP, BATTLE,
WORLD_MAP, ...).

## 5. Timing Model (verified — critical for the native port)

- **Native sim rate is 30Hz in most field areas** (`vsyncMode_8007a3b8 = 2`):
  the render loop runs at `60/2 = 30` Hz; in 60Hz states (e.g. some battles)
  `vsyncMode = 1`. `vsyncMode` is part of the original engine's frame timing
  and feeds `timestamp_a0` (game clock) and state ticks.
- **Speed multiplier** (1–16, default 1; turbo=3; bound to input actions)
  multiplies both render and input rates *and* `vsyncCount` — i.e. the game
  simply runs faster (more sim ticks per second), matching the original's
  turbo-mod behavior expectations.
- `frameSkip` rendering trick (RenderEngine): at 30Hz the *render* still occurs
  60Hz; every other frame re-renders the previous simulation state (queue pools
  are "ignored" so no new models are queued). Net effect: 30Hz sim presented at
  60Hz, with even/odd frames alternating render buffers.
- Implication for native: **decouple simulation ticks (30/60Hz, vsyncMode-driven)
  from render presentation (display vsync), but preserve**: (a) state only
  advances on sim ticks; (b) `timestamp_a0` accumulation; (c) the
  double-presented frame behavior is *not* required as long as sim rate is
  exact — but frame-accurate battle timing (Addition windows are in sim frames)
  is required.

## 6. Randomness, Determinism, Saves (verified)

- `legend.core.Random` (custom, seeded); `Scus94491BpeSegment_8004
  .simpleRandSeed_8004dd44` — the original game's simple LCG seed global.
  Randomness sources must be preserved for gameplay determinism where the
  original was deterministic (battle crits? *open — verify which rolls use it*).
- Save system: Tag-serialized (`core.tags`), versioned V8→V10 with migrators;
  per-save configs; `MemcardSavedGame` imports original memory-card saves;
  crash-recovery saves; `WriteSaveDataEvent`/`ReadSaveDataEvent` for mod data.
  Save contents: `GameState52c` (party/inventory/flags), engine state type +
  per-state Tag data (`engineState.writeSaveData`), campaign type, DABA Save60
  (original-layout save slot), mod `ListTag`.

## 7. Graphics (summary — full detail in `rendering-analysis.md`)

- **Modern path** (default): TMD→meshes (`TmdObjLoader`), QueuedModel pools,
  6 shader pipelines (simple/copy/post/standard/tmd/battle_tmd), per-model UBO
  blocks (128), PS1 texture decode in fragment (R32UI `tex15` + RGBA8 `tex24`),
  two-pass translucent quirk, in-shader scissor, CLUT animation UBO, PS1 or
  modern projection, post/CRT shader.
- **Fidelity paths** (legacy/VRAM): full software PS1 GS (`core.gpu.Gpu`) —
  ordering table, per-primitive Z, top-left rule, PS1 blending — driven by
  per-primitive GTE transforms (`tmd.Renderer`); presented via emulated-VRAM
  texture.
- PS1 GTE software model (`core.gte.Gte`) with exact saturation/flag semantics.
- 320×240 native; render resolution scales vertically (multiples of 240);
  widescreen via ortho offset + submap widescreen modes.

## 8. Audio (summary — see subsystem inventory)

- **Music**: software PS1 SPU synthesizer (`core.audio.AudioThread` @120Hz +
  `core.audio.sequencer.*`) decoding SSSQ sequences + SShd soundbanks (ADPCM
  voices, 4-operator FM, noise, ADSR, reverb, key scaling); OpenAL output.
  Configurable interpolation/pitch/effects fidelity.
- **CD audio (XA)**: unpack-time transcode to Opus (`core.audio.xa.XaTranscoder`);
  runtime `XaPlayer` streams it.
- **SFX**: `core.spu.Spu` — 24-voice ADPCM (PS1 SPU), edge-triggered key
  on/off, per-channel FM/noise/reverb, cubic-resampled, 60 TPS on the game
  thread, mixed to an OpenAL source on the audio thread.
- **Volume model**: master × (music | SFX) × per-source; stored per-save.

## 9. Modding (summary — full detail in `modding-analysis.md`)

- JAR-based mods (`mods/`), external mod-loader (events, registries, i18n,
  versioned mod IDs), retail content as the `lod` mod, bundled `turn_order`
  mod, registry-driven configs + input actions, story-script "fate" patch
  pipeline, asset replacements, save `modData`.

## 10. Key Relationships Between Systems

```
GameEngine (composition root; statics = subsystem handles)
├── SdlPlatformManager ── SDL3 events ──► InputBindings ──► InputAction ──► game states + RenderEngine debug
├── RenderEngine ── RenderApi(GlApi) ──► shaders/UBOs/Obj/Texture
│     └── onDraw ──► Scus94491BpeSegment.gameLoop ──► EngineState.tick (Title/Submap/Battle/WMap)
│           ├── GTE/GPU (PS1 state) ◄── Graphics (GS globals, vsyncMode)
│           ├── SCRIPTS (story VM) ◄── patched fate scripts
│           ├── AUDIO_THREAD (music) + SPU (SFX) ◄── SShd/SSSQ/XA assets
│           ├── MENUS/Text (UI quads)
│           └── EVENTS (mod bus) ◄── LodMod + third-party mods
├── Unpacker ── ISOs ──► BPE/MRG/DEFF/Smap ──► files/ ──► Loader ──► Tmd/Tim/Daba/Anm/CContainer/Mcq/SShd/Cmb
├── MODS/LANG/EVENTS/REGISTRIES (mod-loader)
├── SAVES (Tag format, V10, migrators, memcard import)
└── JavaFX (debugger, updater) — separate toolkit thread
```

**Biggest migration challenges (ranked):**
1. Combat system size/fidelity (Battle 9.4k lines + SEffe 4.6k + camera/HUD/
   effects) — largest single porting effort; needs careful state-machine
   decomposition + golden tests.
2. Software SPU music synthesis fidelity (120Hz, 24 voices, FM/noise/reverb) —
   audio must *sound* the same; needs measurement (spectral comparison vs Java
   output).
3. Story script VM + fate patch pipeline (modding dependency).
4. Save compatibility (Tag V10 + original memcard import).
5. Renderer parity (translucency quirk, PS1 depth, scissor, CLUT animation,
   CRT post) — tractable, testable with screenshots.
6. Unpacker (BPE/MRG/DEFF + patchers) — well-specified, self-contained.

## 11. Assumptions & Open Questions (tracked in progress.md)

- Exact vsyncMode table per engine state (partially verified).
- Which RNG draws affect gameplay determinism.
- External mod-loader/script-recompiler internals (separate repos).
- `legacyMode` 1 vs 2 behavioral difference.
- EU/JP/Asia disc content differences.
- FMV/intro.mp4 licensing for reuse.
- Released third-party mods' reliance on Java-specific mechanics.