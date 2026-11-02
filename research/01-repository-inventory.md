# Repository Inventory

Inventory of the four reference repositories, as inspected on 2026-11-02 (Phase R0).

All four repositories are Gradle-based Java projects, AGPL-3.0 licensed, maintained under the same GitHub organization (`Legend-of-Dragoon-Modding`), and published to Maven Central under `org.legendofdragoon` (the main repo additionally as `legend:lod` and to GitHub Packages / JitPack).

## Summary Table

| | Severed-Chains | Script-Recompiler | Mod-Loader | Dig-Dabas |
|---|---|---|---|---|
| **Revision (2026-11-02)** | `0c08c679b` (2026-11-02) | `3433e6f74` (2026-06-21) | `c69213490` (2026-10-11) | `4c1fbfebc` (2026-05-20) |
| **First commit** | 2021-06-09 | 2022-04-06 | 2023-09-02 | 2024-07-21 |
| **Commit count** | 4545 | 176 | 44 | 73 |
| **Maven coordinates** | `legend:lod` (version = git hash) | `org.legendofdragoon:script-recompiler:0.7.11` | `org.legendofdragoon:mod-loader:4.3.5` | `org.legendofdragoon:mogul-dabas:0.0.1` |
| **Java** | 25 | 25 | 21 | 25 |
| **Entry point** | `legend.game.Main` | `org.legendofdragoon.scripting.Shell` (CLI) | library (no main) | `org.legendofdragoon.dabas.game.Main` |
| **Source files** | 1329 `.java` (1322 main, 7 test) | 61 `.java` + 2 ANTLR grammars | 33 `.java` | 51 `.java` |
| **Tests** | JUnit 6 boot harness (excluded from build) | none | none | none |
| **CI** | GitHub Actions (publish) | none in repo (publish config in build.gradle) | none in repo (publish config in build.gradle) | none |
| **Role** | Engine + game (decompiled ROM) | Fate script tooling | Mod framework | Dabas minigame reference |

## 1. Severed-Chains (primary engine and game)

- **Path:** `~/Documents/GitHub/Severed-Chains`
- **Revision:** `0c08c679b6b54e167b00851f1f1458d4dea3eade` — "Fix movement smoothing blowing away cutscene rotation (closes #2895) (#2902)", 2026-11-02.
- **Self-description (README, `Verified`):** "Legend of Dragoon reverse engineered into high-level code with a modding API. This is not an emulator, but assembly code disassembled and rewritten in Java." The README claims the game is "fully playable with no known crashes" and lists bug fixes and new features over the original.
- **Build:** Gradle (`build.gradle`), Java 25, `maven-publish`. Main class `legend.game.Main`. A second jar target (`updaterJar`, main class `legend.core.UpdaterMain`) bundles the auto-updater with JavaFX.
- **License:** AGPL-3.0 (`LICENSE`).
- **Key dependencies** (from `build.gradle`, `Verified`):
  - `org.legendofdragoon:mod-loader:4.3.5`, `org.legendofdragoon:script-recompiler:0.7.11`, `org.legendofdragoon:mogul-dabas:0.0.1` — the three sibling repositories (published artifacts, not source includes).
  - LWJGL 3.4.3: core, mimalloc, SDL, OpenGL, OpenGL-ES, OpenAL, Opus, STB, Assimp, shaderc, SPIRV-Tools; JOML 1.10.8 (matrix math).
  - JavaFX 21 (controls, fxml) — used for the updater UI and the developer debugger UI.
  - log4j2 (+jansi), JSON/GSON, OpenCSV, java-diff-utils, commons-io, fastutil, JNA, Discord Rich Presence SDK (`discord-game-sdk4j`), JavacV/FFmpeg 8.0 (video decoding for the intro cinematic), slugify.
- **Entry point and startup** (`legend/game/Main.java`, `Verified`): `main()` swaps in a staged updater jar if present, calls `GameEngine.start()`; on any throwable it may generate a "Crash Recovery" save (via `SaveManager.newSave`) and logs version/commit/loaded mods/failed mods.
- **Engine core** (`legend/core/GameEngine.java`, 580 lines, `Verified`):
  - Static singletons: `MODS` (ModManager, force-loads mods `lod` and `lod_core`), `LANG`, `EVENTS`, `REGISTRIES`, `SCRIPTS` (ScriptManager with include dirs `./patches/libs`, `./patches/scripts` and patch dir `./patches`), `SEQUENCER`, `CONFIG` (ConfigCollection), `SAVES` (SaveManager at version V10), `PLATFORM` (SdlPlatformManager), `RENDERER` (RenderEngine), `FONTS`, `GTE`, `GPU`, `SPU`, `AUDIO_THREAD` (stereo, 24 voices, configurable interpolation/pitch/effect granularity), `DISCORD` (rich presence).
  - `start()` ordering (`Verified`): updater check (async) → load lang overrides → spawn unpack thread → `PLATFORM.init()` → mod find+boot (`bootMods`) → load global config (`config.dcnf`) → font load → `AUDIO_THREAD.init()` → `SPU.init()` → `RENDERER.init()` → `GPU.init()` → `DISCORD.init()` → start OpenAL thread → `RENDERER.run()` (game runs on the current thread, named "Hardware"); teardown in `finally`.
  - Unpack thread (`Verified`): `Unpacker.unpack()` (ISO extraction to `files/`), then `ScriptPatcher` applies script patches (`patches/` → `files/`, cache + backups in `files/patches/`).
  - `transitionToGame()` (`Verified`): after loading finishes, calls `Scus94491BpeSegment.main()` (original ROM entry), loads the shadow model, menu assets, textbox geometry, battle UI parts, starts sound, plays FMV slot 0 into the TITLE state.
- **Decompilation artifacts** (`Verified`, pervasive):
  - `@Method(0x800XXXXXL)` annotations record the original PSX RAM address of each method (e.g. `gameLoop()` at `0x80011e1cL`, `main()` at `0x800e5d44L`).
  - Global state is held in classes named after the original executable's segments: `Scus94491BpeSegment`, `Scus94491BpeSegment_8004`, `_8005`, `_8006`, `_800b` (123 files reference `Scus94491`), with address-suffixed fields (e.g. `gameState_800babc8`, `tickCount_800bb0fc`, `vsyncMode_8007a3b8`, `submapId_800bd808`). `SCUS94491` is the North-American PSX disc-1 region code; addresses sit in the 0x80000000 (main RAM) and 0x1F800000 (VRAM) ranges.
  - Some functions remain unnamed (`FUN_80013404`, `FUN_80020ed8`), indicating incomplete symbol recovery.
- **Game loop** (`Scus94491BpeSegment.gameLoop()`, line 137, `Verified`): per frame — `endFrame()` → `GPU.startFrame()` → advance game timestamp by `vsyncMode_8007a3b8` (when the state advances time) → FPS limit `hz = 60 / frames * speedMultiplier` (base `vsyncMode = 2` ⇒ 30 Hz) → `loadQueuedOverlay()` → `renderUi()` → `engineState.tick()` → post `RENDER_EVENT` → load/render menus → `uploadRenderables()` → `SCRIPTS.tick()` → `engineState.postScriptTick(ticked)` → battle transition tick → full-screen effects → SPU queued sounds → textboxes → `engineState.overlayTick()` → `FUN_80020ed8()` → `tickCount++` → `GPU.endFrame()` → Discord tick.
- **Game initialization** (`Scus94491BpeSegment.gameInit()`/`preload()`, lines 599–640, `Verified`): ordering-table config (bits 14, size `0x4000`, zMax `0x3ffe`, zShift 0), clear colors, `InitGeom()`, projection plane distance 640, `initSound()`, `vsyncMode = 2`, system font load (TIM at VRAM rect 832,424 64×56 with 32-entry CLUT), shadow model load, FMV init; `preload()` sets `drgnBinIndex = 1`, `resizeDisplay(320, 240)` — the original native resolution.
- **Source layout** (`src/main/java`, `Verified`, file counts):

  | Package | Files | Responsibility (high-level) |
  |---|---|---|
  | `legend.core` | 244 | Reusable platform/PSX-facing: `audio` (53: AudioThread, sequencer/SSSQ, XA transcode, Opus), `font` (5), `gpu` (13: GPU/VRAM model, GpuCommand*, VramTexture*, Bpp, Rect4i), `gte` (6: Gte, MV, Transforms, GsCOORDINATE2, ModelPart10), `lang` (4: I18nText), `memory` (9: Method annotation, Ref, IntRef, FloatRef), `platform` (34: SDL window/input, input actions/bindings/devices), `renderer` (76: RenderEngine, RenderApi, OpenGL/OpenGL-ES/Noop APIs, meshes, shaders, textures, cameras, framebuffers, queued models), `spu` (9: Spu, Voice, ADSR, Reverb, XaAdpcm, Counter, Phase, Volume), `tags` (12: Tag data model used for save data) |
  | `legend.game` | 975 | The decompiled game: `combat` (291: battle state, bent/deff entities, effects, encounters, formulas, particles, UI, post-battle actions), `modding` (157: CoreMod, event definitions for battle/characters/config/engine/gamestate/input/inventory/scripting/submap/worldmap, debugger), `inventory` (104: goods/items/equipment, menu screens, controls), `types` (50: PSX struct types incl. GameState52c), `saves` (49: SaveManager, SaveVersion, serializers V8/V9/V10/legacy/retail, config entries), `characters` (44: stats, elements, level-up, templates, unlocks), `unpacker` (42: Unpacker, FileData, IsoReader, MrgArchive, transformers, script patching, MIDI/SSSQ conversion), `submap` (41), `wmap` (31), `scripting` (30: runtime interpreter), `sound` (25), `debugger` (22, JavaFX), `fmv` (17: MDEC decoder, VideoPlayer), `tmd` (13: TMD loaders), `additions` (9), `spu`-related, `title` (5), `textures` (7: TextureAtlas, TexturePacker, Image), `tim` (2), `ui` (4: GameOverlay), `credits` (3), `dabas` (3: Dabas host integration), `tmd`/`tim` format classes, `i18n` (2) |
  | `legend.lodmod` | 96 | Retail LoD content as a mod (`LodMod`, mod id `lod`): items, equipment, goods, spells, shops, characters, campaign types, engine state types, input actions, config. |
  | `legend.turnorder` | 5 | Optional turn-order display mod. |
  | `discord` | 2 | Discord Rich Presence bridge. |

  (Plus `src/main/resources`: JavaFX FXML/CSS for the debugger and updater, `en.lang` files for `lod`, `lod_core`, `turn_order`.)
- **Runtime directory layout** (`Verified` from `build.gradle`, `GameEngine`, `Unpacker`): `isos/` (user-provided PSX CD images), `files/` (unpacked asset cache; gitignored), `patches/` (script patch lists, diffs, Fate libraries, replacement scripts, meta CSVs, replacements, lookup tables), `mods/` (mod JARs), `saves/` (campaigns of `.dsav` files), `config.dcnf` (global config), `lang/` (locale override `.lang` files), `gfx/` (engine-provided assets: TTF font descriptors, UI textures, loading textures, shaders `.vsh/.gsh/.fsh`, `intro.mp4`), `gamecontrollerdb.txt` (≈588 KB gamepad mapping DB), `launch*` scripts, `log4j2.xml`, `lwjglx-debug-1.0.0.jar`.
- **Tests** (`Verified`): `src/test/java/legend/game/` — 7 files: `EngineBootTest`, `ExampleTest`, `Bootstrapper`, `TestConfig`, `Harness`, `Input`, `Wait`. A boot harness that can start the engine (with `NoopPlatformManager`/`NoopWindow` available in `legend.core.platform`). **Note:** `build.gradle` configures `test { exclude '**/*' }` — tests do not run in normal builds.
- **CI** (`Verified`): `.github/workflows/publish.yml` (on push to `main`): substitutes build metadata into `legend/core/Version.java` (build number, commit, channel `devbuild`, timestamp), `publishToMavenLocal`, clones a separate repo `Legend-of-Dragoon-Modding/Severed-Chains-Metadata-Utility` (a "meta scraper"), builds and runs it (publishes script metadata to a remote host via `META_HOST` secret), publishes to GitHub Packages. `publish-release.yml` handles releases. `jitpack.yml` present.
- **IDE run configs** (`.run/`, `Verified`): VM flags include `-Xms1g -Xmx2g -ea -Djoml.fastmath -Djoml.sinLookup -Djoml.useMathFma` plus module `--add-opens/--add-exports` for JNA/reflections.

## 2. Script-Recompiler (Fate tooling)

- **Path:** `~/Documents/GitHub/Script-Recompiler`
- **Revision:** `3433e6f74bb6d562aeff0490d11e8937da437511` — "Find functions from all includes", 2026-06-21.
- **Maven coordinates:** `org.legendofdragoon:script-recompiler:0.7.11` (`Verified` from `build.gradle`; used by the main repo).
- **Build:** Gradle, Java 25, ANTLR 4.13.2 (grammar generated into a separate source set), `commons-cli` CLI, log4j2, OpenCSV, java-diff-utils.
- **Entry point** (`org/legendofdragoon/scripting/Shell.java`, 494 lines, `Verified`): CLI with commands:
  - `versions` — list meta versions from remote `https://legendofdragoon.org/scmeta/` (cached in `./cache`).
  - `decompile` (`-i in -o out -v version [-b branch] [-t table-length] [-C] [-N] [-l] [-e entrypoint-count]`) — binary → Fate text.
  - `compile`/`assemble` (`-i -o -v [-L libs]`) — Fate text → binary (ANTLR parse + visitor compiler).
  - `genpatch`/`patch` (apply)/`undopatch` — unified-diff generation/application against decompiled text.
  - `strip` — (implementation not yet examined in depth).
  - `-v` selects a **meta version** — named calls/parameters/strings come from versioned remote metadata, not hardcoded.
- **Source layout** (`src/main/java`, 61 files, `Verified`):

  | Package | Files | Responsibility |
  |---|---|---|
  | `scripting` | 17 | `Shell` (CLI), `Disassembler` (846 lines: binary → token stream, branch/table disambiguation, register tracking), `Tokenizer` (715), `Translator` (319: tokens → text with labels/comments/friendly names), `Assembler` (73), `Patcher` (128: diff application), `OpType` (160: **authoritative opcode table** with header param + typed op params), `OpParam`, `ParameterType`, `State`, `Direction`, `MathHelper`, `StringInfo`, `Include`, exceptions |
  | `scripting.tokens` | 8 | `Script` (entry array indexed by address, entrypoints, branches, subs, subTables, forkJumps, jumpTableDests, warnings, strings, labels, per-scope register stack), `Op`, `Param`, `Data`, `Entry`, `Entrypoint`, `LodString`, `PointerTable` |
  | `scripting.compiler` | 25 | ANTLR-generated parser + `FateCompiler`, `FateCompilerVisitor`, and text-language model: `FateContext/Scope/Function/FunctionDefinition/FunctionRef/Global/Include/Label/LabelRef/Loop/Op/Reg/RegId/Stor/Value/ValueList/Variable/ArrayVariable/GameVar/GameVarArray/Immediate/String/Enum/Preprocessor` |
  | `scripting.meta` | 3 | `Meta`, `MetaManager` (remote versioned metadata over HTTP, local cache), `NoSuchVersionException` |
  | `scripting.resolution` | 5 | `Register`, `RegisterSet`, `RegisterValueType`, `ResolvedValue`, `ScriptRegisters` — dataflow tracking of script registers used by the decompiler |

- **Grammar** (`src/main/antlr/.../FateLexer.g4` 91 lines, `FateParser.g4` 68 lines, `Verified`): defines the Fate text language — `entrypoint`/`def`/`var`/`include`; `if`/`while`/`do-while` blocks; declarations, (augmented) assignments, postfix `++/--`, calls with expression-or-string argument lists, `return`/`continue`/`break`; C-style expression precedence with distinct logical (`&&`/`||`/`!`) and arithmetic (`&`/`|`/`^`) operator families; `stor[n]`, `var[n][m]` (game vars), `reg[n]`, scoped identifiers `mod:name`, array initializers/lookups.
- **Tests:** none. **CI:** none in-repo (Maven publish config with signing in `build.gradle`).

## 3. Mod-Loader (mod framework)

- **Path:** `~/Documents/GitHub/Mod-Loader`
- **Revision:** `c692134907a070a8b2c7db7b2b779010bc16deb9` — "Improve mod id tracking", 2026-10-11.
- **Maven coordinates:** `org.legendofdragoon:mod-loader:4.3.5` (`Verified`).
- **README (Verified):** "The mod-loading component used in several Severed Chains projects. Not for player use."
- **Build:** Gradle, Java 21 (note: lower than the other repos), dependencies: `org.reflections` (classpath scanning), commons-collections4, semver4j, log4j2.
- **No entry point** (library).
- **Source layout** (`src/main/java`, 33 files, `Verified`):

  | Package | Responsibility |
  |---|---|
  | `modloader` (13) | `ModManager` (mod discovery/loading lifecycle; scans `*.jar` in a mods dir, builds a `URLClassLoader`, finds `@Mod(id, version)` classes on the app classpath **and** in mod jars via `org.reflections`; tracks wrong versions (semver) and failed loads; "required" mod ids force-load), `Mod` annotation (id + semver version string), `ModContainer` (per-mod instance + active-mod attribution), `ModState`, `MissingRequiredModException`, `ModNotLoadedException`, `Latch`, `IoHelper`, `LocaleSpecificity` |
  | `modloader.events` (7) | `EventManager` (discover event listeners across mods via reflection; `postEvent` dispatch with per-mod error handling; `reset/initialize`), `Event`, `EventListener`, `EventBinding`, `CancelableEvent`, `Priority`, `RegistryEvent` |
  | `modloader.registries` (11) | `Registries` (collection of named registries with `initialize`/`initializeRemaining`), `Registry`/`MutableRegistry`/`RegistryDelegate`, `RegistryEntry`, `RegistryId` (namespaced identifiers), `Registrar` (mod-scoped registration helper), duplicate/mod-id error types, `RegistryLockedException` |
  | `modloader.i18n` (1) | `LangManager` (loads `.lang` files from mod jars + an overrides directory; locale-specificity; reset/initialize per locale) |

- **Tests:** none. **CI:** none in-repo (Maven publish + signing config).

## 4. Dig-Dabas (Dabas minigame reference)

- **Path:** `~/Documents/GitHub/Dig-Dabas`
- **Revision:** `4c1fbfebced123d43282b6e3b08286336b6b05f0` — "Implement next dig", 2026-05-20.
- **Maven coordinates:** `org.legendofdragoon:mogul-dabas:0.0.1`, root project name `diiig` (`Verified`).
- **Build:** Gradle, Java 25; LWJGL 3.4.1 (GLFW, OpenGL, STB), JOML, LMAX Disruptor, JNA, mod-loader 4.2.5, log4j2.
- **What it is** (`Verified` from source + git history):
  - A standalone reimplementation of the **"Dabas" minigame** that appears in the LoD game (playable in Severed-Chains from an inventory screen). The game is a tile-based cave digging game on a **32×32, 1bpp** screen (`Main` allocates `int[32]` pixels; `game/Dabas.java` is 6029 lines of decompiled minigame logic with address-named variables, e.g. `caveTiles_200d198`, `bossRewards_200e80c`, `speechBubbleTextPixels_2008ce8`).
  - It runs on a modeled **"Mogul" machine** — *not* PSX hardware: an ARMv4-style CPU model (`core/cpu/Cpu.java` — CPSR flag helpers, shifts, compare helpers), an `InterruptController` (0xa00_0000), `PowerControl` (0xb00_0000/0xd80_0000), `Timers` (0xa80_0000/0xb80_0000, dedicated timer thread), and an 8-bit mono `Spu` (0xd80_0010). Memory addresses for game data sit at 0x200xxxxx.
  - **De-emulation history** (`Verified` from git log): early commits ("Dememulate everything but animations" `622a25f`, "Dememulate remaining ram" `f11fbef`, "Finish dememulation" `34472fd`) show the project began as a full emulator of the machine and was deliberately refactored into high-level code; later commits externalized save/load/quit, refactored graphics/audio/windowing out of game code, and fixed gameplay details ("Fix encounter weights" `b4fd98a`, "Implement next dig" `4c1fbfe`).
  - `DabasVars.loadRom(path)` (`Verified`): the minigame's data ("ROM") is a single file with fixed internal offsets — audio timer reload values (0x840c, u16[]), timer speeds (0x8524, u16[]), player volumes (0x859c, u8[]), background A/B (0x85e8/0x8968, u8[]), speech-bubble text pixels (0x8ce8, int[]), bubble background masks off/on (0x8f08/0x9148, int[]), tile attribute actions (0x9388, u8[]), map tile types (0x93f8, u8[]), cave tiles (0xd198, u8[]), speech bubbles (0xd2e8, 8-byte structs), sprite pixels (0xd398, offset table into ROM slices).
  - `DabasInterface` (`Verified`): the host bridge — `setTicker(Runnable)`, `setFps(int)`, `bufferAudio(byte[])`, `loadGame(Save60, int[][] tiles)`, `saveGame(Save60, int[][] tiles)`, `shutdown()`. The standalone `Main` drives it with a GLFW window (640×640), WASD+Space input, and a local `game.rom` file.
- **Integration into Severed-Chains** (`Verified`): `legend/game/dabas/Dabas.java` implements `DabasInterface` (32×32 pixels rendered as a GL texture, 8-bit mono audio through the engine's audio thread, input mapped from engine input actions, suspends/resumes the host renderer state); `legend/game/inventory/screens/DabasScreen.java` provides the in-game play entry point; `legend/game/DabasManager.java` persists the minigame save (`Save60`) as a byte array inside the game's config storage (`DABAS_SAVE_DATA`); `DabasRewardsEvent` hands minigame rewards back to the host as in-game goods. The minigame ROM is loaded from the extracted asset path `OHTA/MCX/DABAS.BIN` (`Verified`, `dabas/Dabas.java` line 76).
- **Tests:** none. **CI:** none.

## Cross-Repository Build Observations

- All repos target Maven Central with identical POM metadata conventions (same developer, same license, `promote` task for the new Sonatype portal).
- The main repo consumes the other three as **published artifacts** (version-pinned), so the source revisions consumed at build time are whatever the pinned versions were cut from — not necessarily the local revisions listed above (`Inferred`: the local checkouts are the canonical upstream of those artifacts; exact source-to-artifact mapping per version is an M8 verification item).
- Java level is not uniform (25/25/21/25) — Mod-Loader intentionally supports older runtimes.
