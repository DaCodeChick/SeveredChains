# Subsystem Inventory — Severed Chains (Java) Reference

Reference commit: `ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb`
Paths relative to `~/Documents/GitHub/Severed-Chains`.
LOC = main source only, measured at the commit above.

## 1. Engine (`legend.core` — 244 files, ~25k LOC)

| Subsystem | Files/LOC | Key classes | Verified behavior | Native mapping |
|---|---|---|---|---|
| Application/runtime | `GameEngine`, `Async`, `Config`, `Version`, `DebugHelper`, `Latch`, `QueuePool`, `MathHelper`, `Transformations`, `Sort`, `LitModel`, `IoHelper` | GameEngine 600+ | Composition root; statics as subsystem handles; Config = low-level prefs (window size, speed multiplier, presets) | `App` + per-subsystem RAII handles; `std::expected` errors |
| Platform (SDL3) | `platform/*` 34/4,327 | `SdlPlatformManager`, `SdlWindow`, `WindowEvents`, `Action`, `Noop*` | SDL3 events (keyboard/mouse/gamepad/window), `Action` scheduler, fullscreen per-monitor (Windows borderless hack: +1px to avoid exclusive FS), rumble + lerp, clipboard, URLs, gamecontrollerdb | Port 1:1 onto SDL3 C API (same events; Action → engine scheduler) |
| Input model | `platform/input/*` 19 | `InputAction(Registry)`, `InputBindings`, `InputBinding`, activations (key/scancode/button/axis), `SdlGamepadDevice`, deadzones (menu vs movement) | Semantic actions + per-device bindings + repeat/hold/axis state; Steam Input keyboard-echo workaround | Port model; store bindings in config |
| Renderer | `renderer/*` 76/8,324 | `RenderEngine` (1,484), `RenderApi`, `GlApi`/`GlesApi`, `RenderBatch`, `QueuePool`, `QueuedModel{,Standard,Tmd,BattleTmd}`, `Obj`/`MeshObj`, `Texture`, `FrameBuffer*`, `ShaderManager`/`Shader`, `Quad/Mcq/Poly/LineBuilder`, `ScissorStack`, `Camera` | See `rendering-analysis.md` | Replace GL layer with SDL3 GPU; port queue/batch/pool logic |
| PS1 GPU (VRAM) | `gpu/*` 13/1,644 | `Gpu` (709, software GS), `VramTexture{Loader,Single,Stitched}`, `GpuCommand*`, `Rect4i`, `Bpp` | Full software rasterizer: ordering table, top-left rule, PS1 blending, perspective-correct interpolation | Port as optional fidelity mode; reusable PS1 tech |
| PS1 GTE | `gte/*` 6/528 | `Gte`, `MV`, `Transforms`, `ModelPart10`, `GsCOORDINATE2` | RTPS/RTPT/NCLIP/NCCS/AVSZ + FLAG semantics | Port exactly (32-bit float math) |
| SPU (SFX) | `spu/*` 9/1,088 | `Spu` (491), `Voice`, `XaAdpcm`, `Reverb`, `ADSR`, `Counter`, `Phase`, `Volume` | 24-voice ADPCM, edge key control, FM/noise/reverb modes, cubic resample, 60TPS tick | Port (audio thread) |
| Audio (music) | `audio/*` 53/4,555 (incl. `sequencer/*` 36/2,919) | `AudioThread` (547), `sequencer.Sequencer` (629), `Voice`, `Channel`, `Instrument`, `AdsrEnvelope`, `LookupTables`, `Reverberizer`, `assets.{BackgroundMusic,Breath,InstrumentLayer}`, `opus.{XaPlayer,OggPage}`, `xa.XaTranscoder` (210) | 120Hz software PS1 SPU synth → OpenAL; XA→Opus unpack transcode; device hot-swap polling | Port synth (measurement-verified); SDL3 audio instead of OpenAL; XA→PCM/Opus decision pending |
| Tags (save data) | `tags/*` 12/647 | `Tag`, `MapTag`, `ListTag`, `String/Int/Long/Float/Bool/Raw/RegistryId/Enum` | Binary tagged tree, offset-based (de)serialization | Port exactly (save compatibility) |
| Font | `font/*` 5/309 | `Font`, `FontManager` | JSON+PNG bitmap fonts (default/smooth), glyph quads | Port (SDL3 GPU quads) |
| Memory | `memory/*` 9/241 | `Ref`, `IntRef`, `FloatRef`, `@Method` | Offset refs into FileData; ROM address annotations | Replace with plain C++ (address annotations become comments/docs) |
| Lang/i18n | `lang/*` 4/67 + external LangManager | `I18nText`, `RawText` | Key-based text with locale overrides | Port key/override model |
| Misc | `SelfUpdater`, `Updater*`, `QueuePool`/`QueueType`, `Tuple` | — | GitHub release updater (JavaFX UI) | Qt 6 launcher handles updates instead |

## 2. Game (`legend.game` — 973 files, ~116k LOC)

| Subsystem | Files/LOC | Key classes | Notes |
|---|---|---|---|
| **Combat** | 290/40,023 | `Battle` (9,375), `SEffe` (4,592), `BattleCamera` (2,911), `BattleHud` (2,186), `Monsters` (1,332), `SBtld` (509, battle load/transition), `bent.{BattleEntity27c (825), PlayerBattleEntity (600), MonsterBattleEntity}`, `deff.Cmb` (238) + DEFF registry, `formula/*`, `encounters/*`, `environment/*`, `effects/*` (17+ effect classes), `particles/*`, `postbattleactions/*`, `types/battlestate/*`, `ui/*` | The largest subsystem. Turn-based battle: menus, camera scripts, hit/effects pipelines, monster AI via CMB/DEFF scripts, report overlays, addition timing (`AdditionOverlaysEffect44`). |
| Submap (field) | 41/11,362 | `SMap` (5,755), `RetailSubmap` (1,994), `CollisionGeometry` (615), `AttachedSobjEffect` (750), `SubmapObject210`, `ChapterTitleCard`, `SmokeParticleEffect`, `SnowEffect`, `TheEndEffect` | Field navigation, collision (sobjs), camera, objects/effects, scripts |
| World map | 31/10,314 | `WMap` (6,245), `WmapStatics` (1,883), `WmapMenuTextHighlight40`, `WmapPromptPopup`, `WMapModelAndAnimData258` | Ship/airship world travel, NEWROOT.RDT data |
| Inventory/UI | 104/18,655 | `ItemIcon`, `screens/*` (controls, FontOptions, TextColour, HorizontalAlign), `ItemStack`, registries (items/goods/equipment/spells/shops), `shop` screens | Menus rendered as MCQ quads + text; fully registry/mod-driven |
| Title | 5/1,758 | `Ttle`, `TtleMenu*` | Title screen, save cards, options menu, mod notifications |
| Saves | 49/4,042 | `SaveManager` (442), `SavedGame`/`RetailSavedGame`/`MemcardSavedGame`, `Campaign`, `ConfigCollection`/`ConfigStorage`/`ConfigPresetManager`, `serializers.{V10,V9,V8,Retail,Legacy}` + `SaveMigrator`, `SavedCharacter`, `SeveredSavedCharacterV1/V2` | Tag-format saves, V8→V10 migration chain, per-save config, memcard import |
| Scripting (story VM) | 30/4,074 | `ScriptManager` (470), `ScriptState` (1,522), `GameVarParam` (699), `RunningScript`, `OpType` (93), `Param` hierarchy, `ScriptDescription/@ScriptParam` (script functions callable from VM!), `TempTicker` | Original event script VM; `@ScriptDescription` exposes Java methods as script ops (mod extensibility) |
| Modding (in-repo) | 156/3,895 | `coremod.CoreMod` (378, config/input/element entries), `coremod.elements.*` (9 elements), `coremod.shops.EquipmentShopExtension` (434), `events/*` (full event catalog), `coremod.config.*` | Engine-side mod API surface |
| Text (textboxes) | in `game.Text` | `Text` (initTextboxGeometry, renderText, handleTextboxAndText), `types.Textbox*` | Dialogue boxes, arrow metrics, text Z |
| SItem (item logic) | `SItem` | give/take items, gold, equipment, chapter/submap/worldmap names, menu asset loading | Game item logic + name tables |
| Sound (SFX state) | 25/4,236 | `Audio` (1,581 — SPU command state), `Sshd` (79), `Sssq` (109), `SequenceData124`, `PlayingNote66`, `ReverbConfig` | Game-side soundbanks/sequences → SPU |
| FMV/video | 17/3,394 | `Fmv`, `VideoPlayer` (ffmpeg/javacv), cutscene FMV tables | Intro MP4 (bundled), disc FMVs |
| Models | `Models`, `tmd/*` 13/990 | `TmdObjLoader` (429), `tmd.Renderer` (209, legacy path), `types.Model124`, `ModelPart10` | TMD→mesh + legacy per-primitive GTE path |
| Unpacker | 42/5,961 | `Unpacker` (1,531), `IsoReader`, `MrgArchive`, `DeffArchive`, `Ctmd`/`CtmdTransformer`, `SubmapPxlTransformer`, `SaveCardTextureExtractor`, `FileData`/`ExpandableFileData`/`FileBackedFileData`, `Loader`, `Finderator`, `scripts.ScriptPatcher`, `midi/*` (mod tools) | Disc → `files/` pipeline (see `asset-pipeline.md`) |
| Textures | 7/324 + `textures` | `TextureAtlas`/`TexturePacker`/`Image`/`PngWriter` | Mod atlas packing |
| Credits | 3/832 | `CreditsScreen` etc. | End credits |
| Debugger | 22/3,827 | `Debugger` (JavaFX app), script/gamestate/combat/smap debuggers (FXML) | Dev tooling → Qt 6 |
| Types (data structs) | 50/2,353 | `GameState52c`, `Model124`, `AnmFile*`, `CContainer*`, `McqHeader`, `MenuEntries`, `Shop`, `Textbox*`, `Renderable58`, `BattleUiParts`, `Flags`, `NewRootStruct` | Game data structures (ROM-derived) |
| EngineStates/state | `EngineState*`, `Scus94491BpeSegment*` | See source-architecture §4/5 | State machine + ROM global segments |
| DrgnFiles/DabasManager | — | DRGN bin index cache; DABA Save60 in/out | Asset index; original save layout |

## 3. Content (`legend.lodmod` — 95 files, ~8.4k LOC)

| Area | Files | Notes |
|---|---|---|
| `LodMod` (972) | mod bootstrap: registers retail elements, elements, input actions, configs, fonts, textures | The "retail mod" entry point |
| `LodEncounters` (1,049) | all battle encounter tables (by chapter/area) | Data-driven content |
| `LodItems`/`LodEquipment`/`LodSpells` + `items/*` (BuffItem 183, RecoverStatusItem 152, BattleItem 121, ...) | item/equipment/spell definitions with effect lambdas | Code-as-data; effects are Java lambdas → native: registry data + effect functions (C++/Luau) |
| `characters/*` (RetailCharacterTemplate 448; per-character templates 147–434: Dart, Haschel, Rose, Lavitz, Shana, Miranda, Albert, Meru, Kongol; `DartCharacterData` 123) | stats, addition unlocks, spell unlocks, element sets, level-up actions | Registry entries + data tables |
| `additions/*`, `spells/*` | retail additions (per-character, per-level), spells | `Addition`/`SimpleAddition` subclasses with hit tables |
| `RetailCampaignType` (129) | the single retail campaign type | |
| `config/*` | retail config entries (e.g. `DABAS_SAVE_DATA`) | |

## 4. Bundled Mod (`legend.turnorder` — 5 files, 280 LOC)

`TurnOrderMod` (`@Mod(id="turn_order")`), `TurnOrder` (draws the HUD), `TurnOrderConfigs`
(`SHOW_TURN_ORDER`), `ShowTurnOrderConfig`. Uses RenderEvent + battle turn events +
`turn_order_bounds` post-shader uniform. Template for a native mod.

## 5. Porting Size Estimate (from measured LOC)

| Native milestone | Approx. Java LOC to port | Effort driver |
|---|---|---|
| Platform/input/timing | ~4.5k (platform + Action) + Config | 1:1 port, SDL3 C API |
| Rendering | ~8.3k renderer + 528 GTE + 1,644 GPU (optional fidelity) + shaders 1,342 | Redesign on SDL3 GPU; fidelity tests |
| Audio | ~4.6k + 1.1k SPU + 4.2k game sound | Synthesis fidelity measurement |
| Unpacker | ~6k + external fate tooling | Well-specified codecs |
| Saves/config | ~4k + tags 647 | Binary compat tests |
| Script VM | ~4k (+ fate pipeline, external) | VM correctness tests |
| Submap+wmap+title | ~23.5k | State machines, data |
| Inventory | ~18.7k | Registry-driven screens |
| **Combat** | **~40k** | **Dominant cost; split across multiple milestones** |
| Content (lodmod) | ~8.4k | Data + effect functions |

Total ≈ 116k LOC Java (main) + external libs (mod-loader, script-recompiler) —
**the combat + field content is the bulk of the work; the engine core (platform,
render, audio, assets, saves, scripting host, mod API) is ~35–40k LOC equivalent.**

## 6. Cross-Cutting Observations

1. **Static globals everywhere** (ROM-derived). Native: namespace subsystem state
   into owning objects; keep the *semantics* (e.g. `vsyncMode`, `timestamp_a0`,
   light matrices) but not the address-named statics.
2. **CompletableFuture for async** (file loads, anim loads, unpack). Native:
   async task system with completion handles (no exceptions; error results).
3. **Mod-facing surface = events + registries + i18n + config + input actions +
   script functions (`@ScriptDescription`)**. Native Luau API must expose the
   same capability surface (see `luau-api.md`).
4. **`@ScriptDescription`** — Java methods callable from story scripts
   (e.g. `RenderEngine.scriptGetRenderAspectMultiplier`). This is how mods add
   story-script opcodes; native equivalent = registered script functions.
5. **Two debugger layers**: in-game free-cam (engine) + JavaFX debuggers
   (script/gamestate/combat/smap inspectors) → Qt 6 tools in native.
6. **Test suite**: `src/test/java` exists (JUnit 6) but is excluded from builds
   (`test { exclude '**/*' }` in build.gradle) — the reference relies on manual
   verification; our native project needs real automated tests (see `testing.md`).
