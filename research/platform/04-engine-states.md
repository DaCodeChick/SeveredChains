# Engine State Machine

Status: Complete (R1). Repository: Severed-Chains @ `0c08c679b`.

The game is a single-instance, queued-transition state machine. This document covers the state type registry, instance lifecycle, the transition mechanism, per-state hooks, save integration, and the retail state inventory.

## 1. Types and instances

- **`EngineStateType<T>`** — a registry entry pairing a state *type* (registry ID) with a constructor (`constructor_00`, a `Supplier`). Registered into `REGISTRIES.engineStateTypes` via `RegisterEngineStateTypesEvent` at mod boot (core states by `CoreMod`/`CoreEngineStateTypes`, retail states by `LodMod`/`LodEngineStateTypes`; mods may add more).
- **`EngineState<T>`** (EngineState.java) — the abstract instance base. `currentEngineState_8004dd04` (EngineStates.java:22) is the single live instance.

### Retail state inventory (`Verified`)

| Registry ID | Class | Registrar |
|---|---|---|
| `title` | `Ttle` | CoreEngineStateTypes (CoreMod) |
| `credits` | `Credits` | CoreEngineStateTypes (CoreMod) |
| `submap` | `SMap` | LodEngineStateTypes (LodMod) |
| `world_map` | `WMap` | LodEngineStateTypes (LodMod) |
| `battle` | `Battle` | LodEngineStateTypes (LodMod) |
| `final_fmv` | `FinalFmv` | LodEngineStateTypes (LodMod) |
| `game_over` | `GameOver` | LodEngineStateTypes (LodMod) |

Seven retail states. The registry is the extension point: mods register additional `EngineStateType` entries, and any code (menus, scripts via state-transition calls, FMV completion) can queue a transition to any registered type.

## 2. Instance lifecycle

```
construct (type.constructor_00.get())
  → init()                 [base: sssqResetStuff(); submapId = -1]
  → readSaveData(gs, tag)  [only when loading from save data]
  → [per frame, while current: tick() → (scripts) → postScriptTick(ticked) → overlayTick();
       inputActionPressed/Released; menuClosed; restoreMusicAfterMenu; modelLoaded]
  → destroy()              [on transition away]
```

- `init()` (EngineState.java:51) resets the SSSQ music state (`sssqResetStuff`) and clears `submapId`. `Verified`.
- `destroy()` is a no-op in the base; states clean up their own resources.
- Scene objects created during a state's life are handled by the renderer's deletion protocol: `loadGameStateOverlay` calls `Obj.clearObjList(false)` / `Texture.clearTextureList(false)`, which **mark** non-persistent objects/textures for deletion (and log a `Leaked: <name>` warning for each — a reference-implementation diagnostic; retail states generally leave their objects to this bulk clear rather than deleting them in `destroy()`). Physical deletion happens at the **end-of-frame fixed point of the same onDraw** — the post-frame bookkeeping step (execution model §2 step 9) runs after that frame's render pass, so marked objects may be rendered once more by the transition frame before they are freed (audit F-03: the R1 draft said "deferred by one frame", which was wrong — the fixed point is within the same frame, after rendering, not in the next onDraw). `Verified`.

## 3. Transition mechanism

Transitions are **queued, never immediate**:

1. Any caller sets `engineStateOnceLoaded_8004dd24 = <type>` and optionally `engineStateData = <Tag>` (save data). Retail call sites (`Verified`):
   - `SMap`: → battle (SMap.java:4322), → world map / queued target (4312), → title on quit (4347)
   - `Battle`: → `postBattleEngineState_800bc91c` on battle end (Battle.java:2579)
   - `WMap`: → queued target (WMap.java:794)
   - `Fmv`: → `afterFmvState` when the FMV finishes (Fmv.java:270)
   - `Ttle`: → the saved game's engine state on load (Ttle.java:650)
   - `GameOver`: → title (GameOver.java:144)
   - `AboutScreen`: → credits (AboutScreen.java:124)
2. At the **top of the next frame**, `loadQueuedOverlay()` (EngineStates.java:47, `@Method(0x800128c4L)`):
   - `menuStack.reset()` (any open menu is discarded);
   - record `previousEngineState_8004dd28` (the state being left);
   - force `vsyncMode = 2` (30 Hz);
   - `loadGameStateOverlay(type, tag)`;
   - clear the queue;
   - post `EngineStateChangeEvent(previous, current)`;
   - update Discord rich presence.
3. `loadGameStateOverlay()` (EngineStates.java:69, `@Method(0x80012a84L)`):
   - `SCRIPTS.setFramesPerTick(1)` (script cadence reset);
   - **no-op if the current state already *is* the target type** (self-transitions are dropped);
   - `Obj.clearObjList(false)`, `Texture.clearTextureList(false)` (deferred teardown);
   - `current.destroy()`;
   - `current = type.constructor_00.get()`; `current.init()`;
   - if `tag != null`: `current.readSaveData(gameState, tag)`;
   - `loadEngineStateFunctions(current)` — installs the state's **per-state script-native function table** (§5);
   - `renderMode = current.getRenderMode()`; `RENDERER.setRenderMode(...)`; `RENDERER.updateProjections()`.

Because transitions apply at frame top, *the frame on which the queue is set still runs the old state to completion* (including its script tick). No transition can happen mid-frame. `Verified`.

### Transition support globals

| Global (EngineStates.java) | Purpose |
|---|---|
| `engineStateOnceLoaded_8004dd24` | queued target type |
| `engineStateData` | queued Tag save data |
| `previousEngineState_8004dd28` | state being left (set at apply time) |
| `lastSavableEngineState` | last state where saving was possible (crash recovery) |
| `postBattleEngineState_800bc91c` | where the current battle returns (set by the encounter/transition code) |
| `postCreditsEngineState` | state after credits (mod-facing) |
| `previousEngineState_800bdb88` | raw previous-state tracker used by `FUN_80020ed8` (submap transition flags) |

## 4. Per-state hooks (EngineState interface)

| Hook | When it runs | Base behavior |
|---|---|---|
| `init()` / `destroy()` | construct / transition away | `sssqResetStuff()`, `submapId = -1` / no-op |
| `tick()` | frame phase 6, **before** scripts | compute analogue stick magnitude/angle from the four movement-axis actions |
| `postScriptTick(ticked)` | frame phase 11, after scripts | no-op |
| `overlayTick()` | frame phase 16, "after everything else is rendered" | no-op |
| `inputActionPressed/Released` | during the platform input tick (between frames) | no-op |
| `advancesTime()` | frame phase 2 | `true` (freezes `timestamp_a0` when false) |
| `tickMultiplier()` | on demand (text cadence, WMap/SMap per-tick cadences) | 2 |
| `menuClosed()` | when a menu closes on top of this state | no-op |
| `restoreMusicAfterMenu()` | after menu closes | no-op |
| `getScriptFunctions()` | at transition apply | null (use the global native table) |
| `modelLoaded(model, container)` | when a model finishes loading | no-op |
| `renderTextOnTopOfAllBoxes()` | text rendering | true |
| `getRenderMode()` | at transition apply | `RenderMode.PERSPECTIVE` (true perspective); `LEGACY` = "pseudo-perspective using orthographic projection with H division" (the PSX-style projection; submap/title use it per the enum doc) |
| `writeSaveData(gs)` / `readSaveData(gs, tag)` | save write / load | abstract |
| `canSave()` | save-menu gating | false |
| `loadSaveFromMenu(save)` | menu load | throws (not implemented in base) |
| `updateDiscordRichPresence(gs, activity)` | at transition apply | chapter + location |
| `getCamera()` | per-frame camera access | abstract (returns `GsRVIEW2`) |
| `getInputsPressed/Repeat/Held`, `getAnalogueAngle/Magnitude` | game/script input queries | 0 / computed in `tick()` |

`Verified` (interface source).

## 5. Per-state script-native tables

Scripts call native functions through a 1024-entry jump table: `scriptSubFunctions_8004e29c` (global, Scus94491BpeSegment_8004.java:122-196) maps native-call IDs to `Function<RunningScript, FlowControl>` (indicators, global flags, textboxes, audio, SFX, gold/items, registry reads, input queries, aspect multiplier, …). `loadEngineStateFunctions(state)` (Scus94491BpeSegment_8004.java:199) optionally installs a **state-specific override table** (`engineStateFunctions_8004e29c`); `getScriptFunction(index)` resolves global-first, then state table (Scus94491BpeSegment_8004.java:188-197). The table is reinstalled on every transition. `Verified`. (Full native-surface analysis is M3; the 533 distinct native functions are distributed across game classes — Battle 181, SMap 128, SEffe 96, ScriptState 54, Audio 26, SItem 19, Text 13, segment class 13, others.)

## 6. State machine diagram (retail)

```
                 ┌─────────────┐  FMV 0 done   ┌────────┐
   (loading) ───▶│  FINAL_FMV  │──────────────▶│ TITLE  │
                 └─────────────┘               └───┬────┘
                       │ (skipped path: title)     │ new game / load save
                       ▼                           ▼
                    TITLE ◀───────────────────── ┌───────┐
                       │  menu: credits          │ SUBMAP│◀──────────────┐
                       ▼                         └───┬───┘               │
                    ┌─────────┐        encounter     │    battle-end     │
                    │ CREDITS │                      ▼                   │
                    └─────────┘                 ┌────────┐   ┌───────────┴──┐
                       ▲                       │ BATTLE │   │   WORLD_MAP   │
                       │ about-screen (any)    └───┬────┘   └───────┬───────┘
                       │                           │ defeat          │ map exits
                       │                           ▼                 ▼
                       │                      ┌──────────┐        SUBMAP
                       │                      │ GAME_OVER│──(menu)──▶ TITLE
                       │                      └──────────┘
                       └────── CREDITS reachable from menus; FINAL_FMV → CREDITS at game end
```

Edges are the verified call sites in §3 plus the menu-driven transitions (title options → credits via AboutScreen; game over → title). The menu itself (inventory, options, load/save) is **not** an engine state — it is an overlay driven per-frame by `Menus.loadAndRenderMenus()` / `menuStack` (SItem) on top of whatever state is current; closing a menu calls `state.menuClosed()`. `Verified` (mechanism); the exact retail menu→state edge set is M9 territory.

## 7. Save integration

- `writeSaveData(GameState52c)` returns the state's own `Tag` (nested tag tree); the campaign-level `GameState52c` fields (characters, inventory, gold, flags, chapter, …) are serialized by the save serializers (M7).
- `canSave()` gates the in-game save option; `lastSavableEngineState` (updated when a savable state is entered — see crash recovery, lifecycle doc §7) is what the crash handler reconstructs.
- Loading a save from the title sets the queue to the *save's recorded engine state* with the saved Tag (Ttle.java:650). `Verified`.

## 8. Render mode per state

Only two retail states override the render mode (`Verified`): `SMap.getRenderMode()` → `LEGACY` (SMap.java:908) and `Ttle.getRenderMode()` → `LEGACY` (Ttle.java:218). All other states (WORLD_MAP, BATTLE, CREDITS, FINAL_FMV, GAME_OVER) use the base default `PERSPECTIVE`. `LEGACY` is the PSX-style centered-ortho "H-division pseudo-perspective" (RenderBatch.java:140+); `PERSPECTIVE` is true perspective with the LOD left-handed/negated-Y projection (RenderBatch.java:126-138).

Note: the Dabas minigame, FMV playback, and the modern video player all **save and restore the render mode** around their content (`Dabas.java:85`, `Fmv.java:334`, `VideoPlayer.java:108`) — they are temporary render-mode switches, not engine-state changes.

## 9. Open items

- ~~The exact update point of `lastSavableEngineState` (menu save path) — verify in M7.~~ **Resolved by M7** (M7-01, `research/saves/01-save-formats.md` §10): it is set in the `init()` of the two savable states only — `WMap.init()` (`WMap.java:435`) and `SMap.init()` (`SMap.java:451`) — each assigning `EngineStates.lastSavableEngineState = this.type`; no other code path updates it (source-verified by repo-wide grep).
