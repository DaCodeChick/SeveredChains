# Runtime Subsystem Responsibilities

Status: Foundational pass (R1). Deeper per-subsystem analysis lands in the dedicated milestones (M2 formats, M3 scripting, M4 PSX hardware, M5 rendering, M6 audio, M7 saves/config, M8 modding, M9 game systems). Repository: Severed-Chains @ `0c08c679b` unless noted.

This document inventories the major runtime subsystems, their responsibilities and primary interfaces, their communication mechanisms, error propagation, and extension points — and distinguishes architectural requirements from incidental Java structure.

## 1. Subsystem inventory

"Owner thread" per the steady-state thread table (lifecycle doc §8). `MODS/REGISTRIES/EVENTS` are the Mod-Loader framework (Mod-Loader repo @ `c69213490`).

| Subsystem | Location (package) | Responsibility | Key entry points | Owner thread(s) |
|---|---|---|---|---|
| Host bootstrap | `legend.core` (GameEngine, Main, Version, Updater, Async, DebugHelper, Config) | Process start/stop, update check, global flat config, async pool, timers/sleep, loading screen, transition-to-game | `GameEngine.start()`, `Main.main()`, `Async.run` | main (+Async pool) |
| Platform | `legend.core.platform` | SDL window/GL context, event pump, input model, gamepads, rumble, displays, clipboard | `PlatformManager`, `SdlWindow.tick`, `tickInput` | main |
| Renderer | `legend.core.renderer` | GL abstraction (OpenGL/GLES/Noop), shader families, render batches + model pools, sorting, framebuffers, post-processing, texture/obj resource registry, cameras, scissor | `RenderEngine` (onDraw, `renderFrame`, `queueModel`), `RenderApi`, `Obj`, `Texture`, `RenderBatch` | main |
| GPU model | `legend.core.gpu` | **PSX GPU emulation model**: 1024×512 VRAM (15/24-bit), double-buffered display, ordering-table z-queues of `GpuCommand`s, draw area/offsets/masks, bpp encodings | `Gpu` (startFrame/endFrame/queueCommand/uploadData15), `GpuCommand*` | main + async VRAM writes |
| GTE model | `legend.core.gte` | **PSX geometry transform model** (float): the MV register file, per-operation transforms, model parts, projection helpers | `Gte`, `MV`, `ModelPart*` | main |
| SPU model | `legend.core.spu` | **PSX SPU emulation model**: 24 voices, edge-triggered key on/off, ADSR, noise gen, reverb, 16-bit stereo output at `BASE_SAMPLE_RATE`, 60 ticks/s | `Spu` (tick/keyOn/keyOff/reverb) | SFX thread (model), main (control) |
| Audio | `legend.core.audio` (+ `legend.game.sound`) | OpenAL device/contexts, 120 Hz source loop, core SSSQ sequencer (music synthesis), Opus XA player, game-level audio facade (BGM, SFX queue, volumes, fades) | `AudioThread`, `AudioSource`, `legend.game.sound.Audio`, `legend.game.sound.Sequencer` | OPEN_AL (sources), SFX (SPU model + game sequencer), main (control) |
| Scripting | `legend.game.scripting` | **Fate VM**: script files, entrypoints, opcode interpreter, storage/params/flags, call stack, forks, native dispatch (1024-entry tables), frame pacing | `ScriptManager` (tick), `ScriptState`, `RunningScript` | main |
| Unpacker | `legend.game.unpacker` | Startup asset pipeline: 4-disc ISO → MRG file tree → transforms → `files/` cache; script patching; runtime file access | `Unpacker.unpack`, `ScriptPatcher.apply`, `Loader`/`DrgnFiles` (async `CompletableFuture` loads) | unpack thread (startup), Async pool (runtime loads) |
| Saves | `legend.game.saves` | Versioned save formats (V1–V10, retail), memcards/campaigns, migration, serializers, save-manager API, config storage (`.dcnf`), tags | `SaveManager`, `SaveVersion`, `serializers/*`, `ConfigStorage` | main |
| Config | `legend.game.saves` (ConfigCollection/Entry) + `legend.core.Config` | Two layers: typed registry-driven entries (global + per-campaign + per-save, `.dcnf`; M7-02) and flat global properties (`config.conf`) | `CONFIG.getConfig/setConfig`, `Config.*` | main |
| i18n | `legend.core.lang`, `legend.game.i18n`, Mod-Loader `LangManager` | `@I18nText`/`I18n.translate`, `.lang` overrides, locale handling (JVM forced to US; original locale kept) | `I18n.translate`, `LANG` | main |
| Modding | Mod-Loader + `legend.game.modding` | Mod discovery/boot/versioning, 21 registries, event bus, per-registry `Register*` events, core mod (`lod_core`) and retail content mod (`lod`) | `MODS`, `REGISTRIES`, `EVENTS`, `bootMods` | main |
| Menus/UI | `legend.game` (SItem, Menus, Text, GameOverlay, `inventory.screens`) | Menu stack, renderable pool, textboxes, retail UI assets, option screens, reference HUD (notifications/FPS) | `Menus.loadAndRenderMenus`, `SItem.menuStack`, `Text` | main |
| Game content | `legend.game.submap/wmap/combat/characters/inventory/title/fmv/credits/dabas`, `legend.turnorder`, `legend.lodmod` | All gameplay: submap/field, world map, battle, characters/progression, inventory, title, FMVs, credits, Dabas minigame, turn-order mod; retail values via lodmod registries | per-system (M9) | main (Dabas machine + debugger UI aside) |
| Dabas host | `legend.game.dabas` (+ Dig-Dabas lib @ `4c1fbfebc`) | Hosts the minigame: reward mapping, screen integration, save slot (Save60 + tiles), render-mode save/restore | `Dabas`, `DabasInterface` | main |
| Rich presence | `discord` (DiscordRichPresence) | Discord RPC (activity chapter/location) | `DISCORD.init/tick/destroy/updateActivity` | main (tick) + SDK threads |
| Debugger | `legend.game.debugger` (JavaFX) | Development tool: state/registry/texture inspection, pause/frame advance interop with RenderEngine flags | `Debugger` | JavaFX thread |

Counts are from the R0 inventory (main repo: 1,322 main files). `Verified` for responsibilities/entry points as listed; per-subsystem behavior detail deferred to its milestone.

## 2. Communication mechanisms

1. **Singleton services** — `GameEngine`'s static fields (`MODS`, `EVENTS`, `REGISTRIES`, `SCRIPTS`, `CONFIG`, `SAVES`, `PLATFORM`, `RENDERER`, `FONTS`, `GTE`, `GPU`, `SPU`, `AUDIO_THREAD`, `DISCORD`, `SEQUNCER` facade). Every subsystem reaches every other through these; there is no DI container. (Architectural: a fixed set of process-wide services; the *static-field* carrier is incidental Java.)
2. **Static global state** — the decompiled ROM's memory: `Scus94491BpeSegment*` segment classes (address-suffixed fields), `Graphics.*`, `GameState52c gameState_800babc8`. This is not an accident of Java — it is the original program's global address space, preserved. (Architectural requirement: the game state *is* global and flat.)
3. **Event bus** — Mod-Loader `EventManager` (typed, priority-ordered, error-isolated, active-mod attribution): mod↔mod and mod↔engine communication, per-frame hooks (`RenderEvent`, `InputPressed/ReleasedEvent`), lifecycle events (registry add/remove, state change).
4. **Window events** — raw I/O callbacks (input, resize, focus, draw, close) fired on the main thread by the platform.
5. **Async completions** — `CompletableFuture` (Async pool) for file loads; `thenAccept` callbacks may run on pool threads (VRAM uploads from async callbacks are the canonical example, guarded by `vramLock`).
6. **Queues** — render batches/model pools (per-frame), menu stack, SPU queued sounds, frame-start task deque, ordering-table z-queues, script fork stack.
7. **Direct calls** — game code calls subsystem facades inline (e.g., `Audio.startSound()` from scripts via natives; `SAVES.newSave` from the menu).

## 3. Service and initialization dependencies

Construction graph (from GameEngine field/static-init order; lifecycle doc §2):

```
EVENTS, REGISTRIES (need EVENTS)  →  MODS (needs nothing, reserves core IDs)
SCRIPTS (standalone)   SEQUENCER facade (standalone)   CONFIG, SAVES (standalone)
PLATFORM (standalone)  RENDERER (needs nothing at construction)   FONTS
static block: GTE, GPU, SPU, AUDIO_THREAD            (SPU needs nothing yet)
DISCORD (standalone)
```

Initialization-time dependencies (order in `GameEngine.start`; lifecycle doc §3):

```
PLATFORM.init  (SDL before window exists)
  └─ RENDERER.init  needs PLATFORM (addWindow)
       └─ GPU.init  needs RENDERER (shader manager, window resize event)
  SPU.init  needs AUDIO_THREAD (addSource) — AUDIO_THREAD.init first (device+context, default sources)
  DISCORD.init
  openalThread.start
bootMods (before config.dcnf load) needs: MODS discovered, EVENTS/REGISTRIES reset
ConfigStorage.loadConfig needs bootMods complete (entries must be registered)
Config entries (CoreMod.*/LodMod.*/mod entries) are read only after their registries are initialized
transitionToGame (INIT_LOCK) needs: unpack complete (unpackerLoading=false) + update check finished
  → Scus94491BpeSegment.main (gameInit/preload) needs: GPU initialized (ordering table, VRAM), SCRIPTS, Loader/files present
  → SPU control (startSound) needs SPU.init done; SFX thread then free-runs
  → Fmv.playCurrentFmv needs files + audio
```

Cross-thread initialization handshakes: `INIT_LOCK` (unpack thread ↔ transition), `UPDATER_LOCK` + `UPDATE_CHECK_FINISHED` (HTTP callback ↔ main/unpack thread), `synchronized(AudioThread)` (main ↔ audio thread), source monitors. `Verified`.

## 4. Resource lifetimes

| Resource | Lifetime | Protocol |
|---|---|---|
| `Obj` (GPU meshes) | scene | created on load; `persistent` flag survives; marked-deleted by `clearObjList(false)` (state transitions, `clearRenderables`); physically deleted in onDraw step 9 (`Obj.deleteObjects`) or on pause toggle — **frame-deferred deletion** |
| `Texture` | scene/persistent | same protocol (`clearTextureList`, `deleteTextures`); VRAM textures and render-buffer textures are persistent |
| VRAM (1024×512) | process | owned by `Gpu`; all writes under `vramLock` (async callbacks write from pool threads); GPU→GL upload at `startFrame` when dirty |
| Menu renderables (`Renderable58`) | menu | pooled: `allocateRenderable`/`deallocateRenderables(group)`/`clearRenderables`; group-ID based deallocation (e.g., deallocate group 0xFF on new game) |
| Script files / params / running scripts | state/campaign | `SCRIPTS.clear()` on ROM init; param pool reuse; script objects bound to `ScriptedObject`s (M3) |
| FileData | load | async `Loader` loads; `FileData` handed to decoders/VRAM uploads; no central cache beyond the `files/` directory on disk |
| Engine state instance | state | construct → init → (per frame) → destroy (on transition) |
| Audio sources | process | added/removed via `AUDIO_THREAD.addSource/removeSource` (SPU source at SPU.init; sequencer + XaPlayer at AUDIO_THREAD.init) |

`Verified` (protocols); `Inferred` where the original-PSX analogue is asserted.

## 5. Error propagation

| Mechanism | Scope | Behavior |
|---|---|---|
| Top-level crash catch | process | `Main.main` catches `Throwable` → crash-recovery save + CRASHLOG + exit(1) (lifecycle doc §7) |
| `EventHandlerException` | event bus | only for **critical** events; rethrown from `postEvent` → crash path |
| Event error callback | event bus | non-critical handler exceptions: log + `GameOverlay` notification; remaining listeners still run |
| Mod load failures | boot | recorded per mod (version/duplicate/instance); title-screen notifications; boot continues |
| Unpack failure | startup | loading screen stuck with status (no recovery path in-app) |
| `UnpackerStoppedRuntimeException` | startup | clean abort (user closed the window) |
| Audio init failure | runtime | park + retry loop (device reinit), game continues without sound |
| `NotImplementedException` | scripts | native calls that retail never implemented (e.g., the `ScriptReadable` default read, `scriptReadRegistryEntryVar`); note: `FUN_8001c5fc` throws a plain `RuntimeException("Not implemented")`, not `NotImplementedException` (audit F-08); surfaced as a script error (M3) |
| GL context failure | startup | OpenGL → GLES fallback → crash |
| `RuntimeException` with message checks | game code | decompiled assertion style (e.g., `loadMcq` throws on invalid MCQ header; `scriptSetGlobalFlag1` throws "Are there more flags?") — these are *original* error paths preserved from the ROM |

Notable: the decompiled game code **has no try/catch** in its gameplay paths — exceptions are expected to reach the top-level handler. The reference infrastructure (unpacker, audio, mods, renderer) is the layered, error-handled part. `Verified`.

## 6. Extension points (modding surface — behavioral level)

The mod contract surface (class-loading mechanics are M8):

1. **Registries** (21): campaignTypes, engineStateTypes, inputActions, characterTemplates, levelUpActions, statTypes, statModTypes, elements, battleEntityTypes, items, equipment, goods, shop, spells, config, deff, encounters, additions, battleActions, postBattleActions, elementIcons — each with a `Register*Event` and per-entry add/remove events.
2. **Events**: any `@EventListener` method in a mod class; per-frame and lifecycle event families.
3. **Engine states**: register an `EngineStateType` + implement the `EngineState` interface (incl. per-state script-native table via `getScriptFunctions()`).
4. **Input actions**: register actions + default bindings; user-rebindable.
5. **Config entries**: typed entries with default/preset/user value lifecycle; global or per-save storage.
6. **i18n**: `.lang` overrides + `@I18nText` keys.
7. **Atlas textures**: `RegisterAtlasTexturesEvent` (one mod-atlas pass at `bootRegistries`).
8. **Script patching**: replacement/diff Fate patches applied at unpack (the `patches/` tree is part of the *engine distribution*, not a mod — mod patches are a separate mechanism, M3).
9. **Mod-registry-adjacent interfaces**: `ScriptReadable` (script-readable registry entries), `TurnOrderMod`, `DabasInterface` consumers, `VideoPlayer`-level content (FMV slots), etc.

`Verified` for the surface; semantics (locking, error behavior, lifecycle) are M8.

## 7. Architectural requirements vs incidental Java structure

**Architectural requirements (survive any reimplementation):**
- A fixed set of process-wide services with a defined construction/init order and a single owner thread for the frame loop (lifecycle doc §2).
- Global, flat game state (the ROM's address space) — `GameState52c` + segment globals; state is shared by reference, not passed around.
- Queued, frame-top state transitions with deferred one-frame scene teardown.
- Frame-locked simulation with per-state frame rates and a 60 Hz game-time unit (execution model §5).
- Layered error model: crash-everything in gameplay; recoverable infrastructure (audio, mods, I/O).
- Two-tier input: semantic actions (game-facing) over physical activations (platform-facing), sampled between frames.
- Decoupled real-time audio streams (SPU 60 Hz, music 120 Hz) vs frame-locked logic, coupled only via queues.
- Startup asset pipeline producing a plain-file cache that all runtime file access reads from.

**Incidental Java structure (do not carry over without reason):**
- Static field singletons (could be any service registry).
- `Object`-monitor locking and `synchronized` blocks (any mutual-exclusion scheme).
- Class-loading + reflection for mod discovery (any plugin mechanism).
- `CompletableFuture` + fixed thread pool for I/O (any async I/O).
- Exception-based control flow (critical events, unpack abort).
- Inheritance hierarchies (EngineState generics, tag types) where a data-driven alternative is viable.
- The JavaFX debugger and updater UI (tooling, not architecture).
- The "Hardware"/"OPEN_AL"/"SFX" thread *naming* (observability convenience).

## 8. Open items

- Per-subsystem deep dives: M2 (unpacker/formats), M3 (scripting), M4 (GPU/GTE/SPU), M5 (renderer details), M6 (audio), M7 (saves/config), M8 (modding framework), M9 (game content).
- The `legend.core.memory` package (address annotations, ref types) is a decompilation artifact layer — documented as such, not a runtime subsystem.
