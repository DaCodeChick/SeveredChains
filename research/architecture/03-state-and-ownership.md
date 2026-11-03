# State and Resource Ownership

Status: Foundational pass (R1). Repository: Severed-Chains @ `0c08c679b`.

Who owns which state, which thread mutates it, and under what synchronization. This is the ownership map that a reimplementation must preserve *behaviorally* (single-writer invariants, frame-bound lifetimes) even if it uses different mechanics.

## 1. The global game state (original ROM memory)

The decompiled code preserves the original program's global address space. The canonical container is **`GameState52c gameState_800babc8`** (Scus94491BpeSegment_800b.java:24) — a 0x52c-byte struct whose fields are the campaign-persistent state:

| Field | Meaning |
|---|---|
| `campaign` | current campaign (memcard container handle) |
| `scriptData_08[0x20]` | script-visible general-purpose data (indices documented in comments: stardust turned in, hero tickets, …) |
| `charIds_88` | active character slot order |
| `gold_94`, `chapterIndex_98`, `stardust_9c` | currency/progression |
| `timestamp_a0` | game clock (60 Hz units; advanced per frame) |
| `submapScene_a4`, `submapCut_a8` | submap position (scene/cut; saved for crash recovery) |
| `_b0` | "used by the script engine" |
| `battleCount_b4`, `turnCount_b8` | totals |
| `scriptFlags2_bc` (32 Flags), `scriptFlags1_13c` (8), `wmapFlags_15c` (8), `visitedLocations_17c` (8) | flag bit-arrays (scripts read/write; bits 16-27 are the script flag range — M3) |
| `goods_19c` | goods inventory |
| `_1a4[8]` | submap music related |
| `chestFlags_1c4[8]` | chest flags (probably) |
| `equipment_1e8`, `items_2e9` | equipment list + item inventory |
| `charData_32c` | all character data (stats, additions, dragoons, …) |
| `pathIndex_4d8`, `dotIndex_4da`, `dotOffset_4dc`, `facing_4dd`, `directionalPathIndex_4de` | world map position |
| `indicatorsDisabled_4e3` | scripted-movement indicator disable (Bale boat ride) |

Ownership: **the main thread only** (no lock; every access is from game-loop code or natives it calls). It is created at new-game / loaded at save-load and is the object the save serializers walk (M7). `Verified`.

### Segment-class globals (by system)

| System | Globals (class) |
|---|---|
| Engine states | `currentEngineState_8004dd04`, `engineStateOnceLoaded_8004dd24`, `engineStateData`, `previousEngineState_8004dd28`, `lastSavableEngineState`, `postBattleEngineState_800bc91c`, `postCreditsEngineState`, `previousEngineState_800bdb88` (EngineStates) |
| Timing | `tickCount_800bb0fc` (_800b), `vsyncMode_8007a3b8` (Graphics), `time`/`loadingFade`/`eyeFade` (GameEngine, loading only) |
| Graphics/PSX | ordering-table params, clear colors, draw offsets, projection, `renderMode`, `width/height_8004dd34` (Graphics); `syncFrame_8004dd3c`, `swapDisplayBuffer_8004dd40` (Graphics, swappable lambdas); `worldToScreenMatrix_800c3548` (Graphics — written by freecam and cameras) |
| Submap | `submapId_800bd808`, `submapCut/Scene_80052c30/34`, `collidedPrimitiveIndex_80052c38`, `submapCutBeforeBattle_80052c3c`, `shouldRestoreCameraPosition_80052c40`, `submapEnvState_80052c44` (_800b/_8005), `submapFullyLoaded_800bd7b4`, `_800bd7ac/_800bd7b0`, `transitioningFromCombatToSubmap_800bd7b8` (_800b), `sobjPositions_800bd818[24]`, `rview2_800bd7e8` |
| Battle | `battleStage_800bb0f4`, `encounterId_800bb0f8`, `encounter`, `battleFlags_800bc960` (documented bit table), `loadingMonsterModels` (AtomicInteger), `livingChars_800bc968`, `postBattleAction_800bc974`, `battleState_8006e398` (_8006), `stage_800bda0c`, `battlePreloadedEntities_1f8003f4`, `battleUiParts` |
| Random | `simpleRandSeed_8004dd44` (_8004), `randSeed` (segment class, `rand()` LCG 0x41c64e6d/0x3039) |
| Scripting | `scriptSubFunctions_8004e29c` + `engineStateFunctions_8004e29c` (native tables), `doNothingScript_8004f650` |
| World map | `continentIndex_800bf0b0`, `characterIndices_800bdbb8`, `secondaryCharIds_800bdbf8` |
| Misc | `campaignType` (RegistryDelegate), `loadingNewGameState_800bdc34`, `characterStatsLoaded_800be5d0`, `playerPositionBeforeBattle_800bed30` / `screenOffsetBeforeBattle_800bed50` (persist across engine states by explicit design — see source comment) |

Ownership: **main thread only** (same reasoning as GameState52c). The `AtomicInteger` (`loadingMonsterModels`) is the one atomic in this set — a cross-thread *counter* of async model loads, not a lock. `Verified`.

## 2. Service ownership (threads)

| Service | Owner thread | Mutators besides owner | Synchronization |
|---|---|---|---|
| `PlatformManager`/`SdlPlatformManager` (input state, gamepads, windows) | main | — (all SDL calls from main) | `INPUT_LOCK` around `tickInput`; `clearPressed` from main; rumble lerp state mutated only in tickInput |
| `SdlWindow` (focus, size, close, cursor) | main | — | `shouldClose` read by loop (main) |
| `RenderEngine` (batches, pools, buffers, camera, debug flags) | main (onDraw) | JavaFX debugger (pause/frame-advance/wireframe/reload flags); `addTask` from any thread | `tasks` deque monitor; flag writes are unsynchronized single booleans (benign in practice — `Inferred`); everything else main-only |
| `Gpu` (VRAM, z-queues, display buffers) | main (frame tick) | **Async pool threads** (VRAM uploads from load callbacks) | `vramLock` for all VRAM array writes + dirty flag; GL texture upload in `startFrame` (main) |
| `Gte` | main | — | none (main-only) |
| `Spu` (model) | **SFX thread** (`Spu.tick`) | main (`keyOn/keyOff` words, volumes, reverb, noise mode) | `synchronized(Spu.class)` in tick; **control writes are unlocked and non-volatile** (data race; works via 64-bit word atomicity — `Verified` declaration, `Inferred` benignity); `spuOutput` buffer owned by SFX thread; `GenericSource` consumption by OPEN_AL thread under the source's own monitor (AudioThread holds source monitor while ticking sources) |
| `AudioThread` (device/context/sources list) | **OPEN_AL thread** (run loop) | main (every control method) | `synchronized(this)` for control + run-loop body; per-`AudioSource` monitor (nested inside); `volatile deviceChanged`; `scheduler` (single-thread executor) |
| Core `Sequencer` (SSSQ synth) | OPEN_AL (tick) | main via `AudioThread` facade (synchronized) | AudioThread monitor + source monitor |
| Game `Sequencer` facade (BGM state machine) | **SFX thread** (tick) | main (load/start/stop via `AudioThread` synchronized methods) | AudioThread monitor |
| `XaPlayer` (Opus) | OPEN_AL (tick) | main via facade | AudioThread monitor + source monitor |
| `ScriptManager`/VM | main | — | none (main-only); param pool internal |
| `SaveManager`/campaigns | main | — (async *load* of save files on Async pool; result applied on main) | per-call; `CompletableFuture` handoff |
| `ConfigCollection` + `Config` | main | — (read on any thread? all observed reads are main) | `Properties` (not thread-safe; main-only in practice — `Inferred`) |
| `ModManager`/`Registries`/`EventManager` | main | — | `EVENTS.listeners` monitor around postEvent/register/clearStaleRefs; registry mutation windows (M8); thread-local active-mod (main-thread local in practice) |
| `Async` pool | pool | any thread submits | pool internal |
| `Updater` | main (calls) + HTTP callback thread | — | `synchronized(this)` + `UPDATER_LOCK` for `UPDATE`/`UPDATE_CHECK_FINISHED` |
| `DISCORD` | main (init/tick/destroy) | SDK internal threads | SDK internal |
| `GameOverlay` (notifications) | main | — | main-only (drawn in onDraw) |

`Verified` for the listed locks and call sites; "main-only in practice" rows are `Inferred` (no counter-evidence found in R1; a full audit of every field is M10).

## 3. Frame-bound and lifetime-bound state

| State | Scope | Lifetime rule |
|---|---|---|
| Render model pools (per batch) | frame | queued during gameLoop (between `resetBatches` boundaries); consumed by the render pass; reset at frame end (skipped frames: `ignoreQueues` discards new entries) |
| One-frame input `pressed` | frame | set in input tick; cleared after onDraw |
| CLUT-animation uniform buffer | frame | reset at `renderFrame` start; uploaded at end |
| `RenderEvent` (shared instance) | frame | one static instance posted each frame (no per-frame allocation) |
| Obj/Texture marked-for-deletion | frame-deferred | marked (state transitions, `deallocateRenderables`, `clearRenderables`); deleted in the *next* onDraw's post-frame step (or immediately on pause toggle) |
| Engine state instance | state | construct at frame-top transition; destroy at next transition (before the new one's `init`) |
| Menu renderables | menu | pooled; group deallocation; `clearRenderables` on transition |
| `statusText` (loading) | startup | written by unpack thread, read by renderIntro (unsynchronized String — benign: single writer, last-writer-wins display; `Inferred`) |
| `GameState52c` + segment globals | campaign/process | created at new game; reloaded at load-save; process-lived after |
| VRAM contents | process | persists across states; only explicit re-uploads change it (fonts, UI, DEFF animations, shadows) |

## 4. Invariants to preserve (behavioral contract)

These are the ownership/synchronization invariants that observable behavior depends on. A reimplementation may use any mechanics, but must preserve the *properties*:

1. **Single-writer per service** for all frame-loop services (renderer, GPU, GTE, scripts, engine state, menus, saves, config, registries, events): exactly one thread mutates each; cross-thread contact is through (a) explicit locks on narrow surfaces (VRAM, audio control, SPU model tick), or (b) one-shot handoffs (futures, task queues, flag words).
2. **Frame-bound deletion**: scene resources are never freed mid-frame; freeing is coalesced to a fixed point after the frame's render decisions.
3. **Input edge state is one-frame**: "pressed" must not survive past the frame in which it was sampled; held/repeat persist independently.
4. **State transitions are atomic and frame-top**: no phase observes a half-constructed state; the old state is destroyed before the new one inits; scene teardown is deferred exactly one frame.
5. **Audio decoupling**: SFX and music streams run on their own real-time clocks; the game thread communicates with them only via queues and control calls, never by sharing clocks or sample state.
6. **Event delivery is isolated**: one handler's failure cannot skip other handlers (non-critical) and every delivery is attributable to a mod.
7. **Config is read-mostly**: entries are registered at boot, written by the options UI (main thread), read everywhere; there is no runtime re-registration.
8. **The game's globals are the shared state surface**: scripts, mods, and game code all address the same `GameState52c`/segment globals by reference; there is no per-caller copy.
9. **Async I/O callbacks may run off the main thread** and therefore any resource they touch (notably VRAM) must be locked.

## 5. Known unsafe corners (observed, documented for the record)

| Corner | Observation | Assessment |
|---|---|---|
| `Spu.keyOn/keyOff` writes | plain `long`, set from main, read under `Spu.class` on SFX | data race; benign on 64-bit JVM (`Inferred`); a reimplementation should keep the *edge-trigger* semantics (level words consumed per tick) whatever the mechanism |
| `RenderEngine` debug flags (pause/frameAdvance/wireframe/reload) | written from input tick (main) and debugger (JavaFX thread for some paths), read in onDraw | unsynchronized booleans; single-consumer in practice (`Inferred` benign) |
| `statusText` | String written by unpack thread, read by render thread | single-writer, display-only (`Inferred` benign) |
| `Config.properties` | unsynchronized `Properties`, main-only in observed use | `Inferred` safe; would need a lock if I/O callbacks ever read it |
| `EVENTS.postEvent` reentrancy | events fired while iterating listeners can mutate registries (mod code) | protected by the listeners lock only for the listener list itself; registry mutation rules are M8 |

## 6. Open items

- Full field-level audit of every global (M10; R1 covered the state-bearing ones).
- Registry mutation windows and lock semantics (M8).
- `GameState52c` tag serialization layout (M7).
- Confirmation that no subsystem reads `Config` from a non-main thread (M10 sweep).
