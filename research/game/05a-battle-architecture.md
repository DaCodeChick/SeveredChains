# 05a — Battle: Architecture, Execution Model, State Machine, and Script Boundary

M9 deliverable 05, part a of c. Covers: encounter entry and exit, the battle loading-stage machine, the timing model, the battle data model, the native turn system, the script-vs-hardcode boundary (RQ7), the player/monster stat setup and core formulas, battle audio, the `SEffe` effect system overview, and the mod/event surface. Entities, formulas, and deff formats in depth are **05b**; additions, battle UI, and post-battle screens in depth are **05c**.

Sources (all read this session):

- Severed-Chains @ `0c08c679b` (pinned, clean): `legend/game/combat/Battle.java` (9,392 lines — the battle engine state; decompiled retail anchors throughout), `legend/game/combat/SEffe.java` (4,592 lines — static effect/addition system), `legend/game/combat/SBtld.java` (509 lines — entry transition + additions load + battle report overlays), `legend/game/combat/Monsters.java` (1,332 lines), `legend/game/combat/types/BattleStateEf4.java` (the battle state struct; turn order), `legend/game/combat/bent/BattleEntity27c.java` (flags `:64–94`), `legend/game/combat/encounters/Encounter.java`, `legend/game/combat/formula/` (package census only), `legend/game/combat/ui/` (package census only), `legend/game/sound/Audio.java` (`:49–60`, `:820–840`, `FUN_80020308`), `legend/game/EngineStates.java` (`:44–130`), `legend/game/EngineState.java` (script-function default), `legend/game/Scus94491BpeSegment.java` (`:146` timestamp, `:174` transition tick), `legend/game/scripting/GameVarParam.java` (`:255–275`, `:370–395`), `patches/scripts.csv` (`:2`), `legend/game/submap/RetailSubmap.java` (`:527`), `legend/game/wmap/WMap.java` (`:3728`), `legend/game/debugger/DebuggerController.java` (`:253–271`)
- Reference-repo history: commit `eae9fb4f6` "Separate turn order RNG (closes #2481)" (the turn-jitter RNG layer-4 replacement, §6)
- Prior research: M2-04 (S_BTLD.OV_ embedded assets), M6-01 (audio slots/packages), M7-02 (config entries incl. `BATTLE_TRANSITION_MODE`, `REDUCE_MOTION_FLASHING`, `ALLOW_WIDESCREEN`), M8-02 (event contract), M8-03 (core mod registries: 512 `LEGACY` encounters, 10 battle actions, post-battle actions), M9-03 (submap encounter start `RetailSubmap.java:527`, `_800bd7b0` write census SM19), M9-04 (wmap encounter start `WMap.java:3728`)
- No retail assets are available: no `asset-validated` claims in this document.

## 1. Scope and layer model

Battle is the one game system where the *turn logic itself* lives in Fate scripts (the player combat script and the combat controller script), while everything underneath — entity state, stat computation, damage/accuracy formulas, turn-gauge simulation, effects, audio — is native (decompiled retail) code exposed to scripts through a **362-slot battle script-function table** (§7). The RQ7 boundary for battle is therefore a *wide* contract surface rather than a point: the script decides *when and what* happens; the engine decides *how much*.

Layer tags used below: **L1** PSX-hardware behavior, **L3** LoD-specific (decompiled retail), **L4** reference-implementation-specific.

## 2. Entry: encounter start and the dissolve transition

### 2.1 Start call sites

`SBtld.startLegacyEncounter(encounterId, stageId)` (`SBtld.java:55–58`) resolves `LodEncounters.LEGACY[encounterId]` from the encounters registry (M8-03: 512 `LEGACY` entries in the `lod` core mod) and calls `SBtld.startEncounter(encounter, stageId)` (`:60–64`), which sets the three globals: `encounterId_800bb0f8 = encounterId` (the legacy path only; the registry-instance path leaves it `-1`), `battleStage_800bb0f4 = stageId`, and `Scus94491BpeSegment_800b.encounter = encounter`.

Four start paths exist (B1, source-verified):

| Path | Site | Notes |
|---|---|---|
| Submap random/forced encounter | `RetailSubmap.java:527` | after the (mutable) submap encounter event, M9-03 §8 |
| World-map encounter | `WMap.java:3728` | after `WorldMapEncounterEvent`, M9-04 §8 |
| Script game var 43 | `GameVarParam.java:271` (int) / `:385` (registry ID) | **dual-mode param**: `set(int)` = legacy ID, `set(RegistryId)` = registry entry; `isRegistryId() = true` for index 43 (`:374–392`) — the moddable path |
| Debugger | `DebuggerController.java:253–271` | `startLegacyEncounter` from the encounter-test UI |

### 2.2 The dissolve transition (`SBtld`, retail @ 0x8001b410 et seq.)

Starting an encounter does **not** switch engine states immediately. `Battle.init()` calls `clearCombatVars()` (`SBtld.java:218–240`, retail @ 0x8001c4ec), which: plays menu sound 16 (the battle transition sound), sets `vsyncMode = 1`, sets `_800bd740 = 2` (2-tick hold), arms the dissolve darkening metrics (`brightnessAccumulator 0x8000`), and sets `_8004f6e4 = 1` (transition active). The battle state is queued as `engineStateOnceLoaded` (the M9-02 T8 pattern — submap: `SMap.java:4322` inside the `mapTransition` encounter branch, type 6 per `SMap.java:3996`; wmap: `WMap.java:3731` via `engineStateToTransitionTo` consumed at `:794`; debugger: `DebuggerController.java:272`) and **the previous engine state keeps rendering** while the transition plays: the global tick (`Scus94491BpeSegment.java:174`) calls `SBtld.tickAndRenderTransitionIntoBattle()` every tick regardless of the current state.

The transition is a **block dissolve of the last rendered frame** (`renderCombatDissolveEffect`, @ 0x8001b54c): rows of 8×8 blocks sweep upward, each block with ±jitter drawn from the retail LCG (`rand() % 4` signed, `rand() % 6`), re-projecting the previous frame as a monochrome-scaled quad; `renderBattleStartingScreenDarkening` (@ 0x8001bbcc) re-renders the last frame with a decaying brightness (`clamp(brightnessAccumulator*1.1 >> 8, 0x80 − 2*vsyncMode, 0x80)/128`). Modern layer-4 behaviors around it: `BattleTransitionMode` config (NORMAL / FAST / INSTANT — M7-02; FAST halves durations and drops the 2-tick parity gate, INSTANT skips the minimum delay), `REDUCE_MOTION_FLASHING` (skips the dissolve entirely), and `ALLOW_WIDESCREEN` geometry adaptation.

Timing: the transition runs at `vsyncMode = 1`; once `battleFlags & 0x2` (controller script loaded, §4) is observed, a 1-tick fade starts and dissolve darkening is armed for `300 / vsyncMode / speedDivisor` frames; the minimum transition length is `150 / vsyncMode / speedDivisor` ticks (`:190–204`), after which `_8004f6e4 = -1` and **`battleFlags |= 0x1`** (entry transition complete) is set. The BATTLE state's `initializeViewportAndCamera` (§4) gates on `battleFlags & 0x3 == 0x3` — *both* the entry dissolve and the controller script — before committing to the battle viewport.

### 2.3 Exit

`Battle.writeSaveData` returns **null** and `readSaveData` is empty (`Battle.java:616–623`): **battle is not a savable engine state** (M7-01 — only WMap/SMap are; a crash mid-battle recovers to the last savable state).

The post-battle return path (B15, source-verified): `fadeOutBattle` → `deallocateCombat` → `renderPostCombatScreen` → `transitionBackFromBattle`. In `deallocateCombat` (`:2511–2557`): `postBattleEngineState_800bc91c = previousEngineState_8004dd28` (the state the battle came from), then the encounter's post-combat reposition is applied to the *global* submap pair: `submapScene_80052c34 = encounter.postCombatSubmapScene` (if ≠ 0xff) and `submapCut_80052c30 = encounter.postCombatSubmapCut` (if ≠ 0xffff) — the same `(cut, scene)` payload the wmap uses (M9-04 §3), letting a scripted encounter continue the story at a different submap point after the fight. `transitionBackFromBattle` (`:2573–2585`, @ 0x800189b0) waits for the loader, stops music, sets `vsyncMode = 2`, and sets `engineStateOnceLoaded = postBattleEngineState`. On arrival in SUBMAP, `FUN_80020ed8` (`EngineStates.java:96–130`) writes `_800bd7b0 = -4` and `transitioningFromCombatToSubmap_800bd7b8 = true` — the battle-origin case of the transition-mode global (M9-03 SM19, Q-03 closed: `-4` was already in the census).

## 3. The loading-stage machine

`Battle.tick()` (`:710–726`, retail @ 0x800186a0) runs `battleLoadingStage_8004f5d4[loadingStage].run()` every tick (B2) — a 28-entry `Runnable[]` table (`:374–403`, `:367` javadoc lists the same sequence). **Each stage's own code decides when to advance by calling `loadingStage++`**: the `waitForFilesToLoad` entries (`:1955–1960`) advance once `Loader.getLoadingFileCount() == 0` (the same async-load-gating pattern as wmap J36 / submap M9-03); the setup stages (1–20) advance at the end of their work (several are first gated — e.g. stage 8 on `tickCount < encounterIntroFinishedTick`, stage 20 on `battleInitialCameraMovementFinished`); the **`battleTick` stage (21) is resident for the whole battle** and advances only when a post-battle action gets set (the `loadingStage++` at the top when one is already set, or at the bottom of the tick that declares win/loss); **`fadeOutBattle` (23) is resident until its post-combat frame machine completes** (§12.3); `deallocateCombat` (24) advances only once the full-screen effect is done (colour 0xff); `renderPostCombatScreen` (26) advances once the post-battle menu closes (`whichMenu == NONE_0`). When `battleLoaded_800bc94c` is set (from stage 6 on), each tick also runs `checkIfCharacterAndMonsterModelsAreLoadedAndCacheLivingBents()` + `renderBattleEnvironment()` (`:1305–1327`: model-load flag updates §5.3, then camera update, `cacheLivingBents()`, stage rotate+render, skybox render).

| # | State | Method | Work (verified) |
|---|---|---|---|
| 0 | — | `waitForFilesToLoad` | loader gate |
| 1 | INIT | `initBattle` (@ 0x800c7524, `:1962`) | upload `shadow.tim`; `FUN_800c8624` (allocate `BattlePreloadedEntities_18cb0` + `BattleStateEf4` + 3 target lists, `:2587`); `gameState.battleCount_b4++`; reset gold/XP accumulators (`goldGainedFromCombat_800bc920`, `totalXpFromCombat_800bc95c`), `battleFlags = 0`, `postBattleAction = null`, clear drop lists (`itemsDroppedByEnemies`, `itemOverflow`, `equipmentOverflow`); stage-darkening storage; `loadEncounterSoundsAndMusic` (§9) |
| 2 | — | `waitForFilesToLoad` | |
| 3 | SCRIPTS | `loadStageAndControllerScripts` (@ 0x800c7648, `:2029`) | `loadStage(battleStage)` (§10) + `loadStageDataAndControllerScripts` (§7.2) |
| 4 | VIEWPORT | `initializeViewportAndCamera` (@ 0x800c76a0, `:2036`) | gated on `battleFlags & 0x3 == 0x3`; `resizeDisplay(320, 240)`, `setDepthResolution(12)`, **`vsyncMode = 3`**, `battleFlags |= 0x40`, `setProjectionPlaneDistance(320)`, camera reset, `encounterIntroFinishedTick = tickCount + encounter.introWaitTicks` |
| 5 | — | `waitForFilesToLoad` | |
| 6 | PRELOAD | `battleInitiateAndPreload` (@ 0x800c772c, `:2052`) | `FUN_800c8e48` (skybox VRAM 512→320 copy, sets 0x80); `battleLoaded = true`; 30-tick fade in (type 4, or 2 under `REDUCE_MOTION_FLASHING`); `battleFlags |= 0x20`; `battleState.battlePhase_eec = 0`; `clearCombatants()`; `battleState.clear()`; **`loadAdditions()`** (§11); `initVars()` (`:8302`: `monsterBents[9] = -1` × 9, melbu names from ROM `monsterNames_80112068[395..397]`, `spGained` clear, `sortItems()`); `allocateDeffManager()` |
| 7 | — | `waitForFilesToLoad` | |
| 8 | ENEMIES | `allocateEnemyBattleEntities` (@ 0x800c788c, `:2078`) | gated on `tickCount < encounterIntroFinishedTick` (the intro wait); per `encounter.uniqueIds`: `loadEnemyDropsAndScript(addCombatant(id & 0x1ff, -1), id)` (§7.3); per `encounter.monsters`: `MonsterBattleEntity` + `SCRIPTS.allocateScriptState` (ticker `bentLoadingTicker`, destructor), position from `encounterMonster.pos`, rotation Y = 0.75·2π (270°), `FLAG_MONSTER`, `battleState.addMonster`, `loadMonster`; `loadEncounterMonsterSounds` |
| 9 | — | `waitForFilesToLoad` | |
| 10 | PLAYERS | `allocatePlayerBattleEntities` (@ 0x800c78d4, `:2116`) | party from `gameState.charIds_88`; `loadingAudioFiles |= 0x8`; **script slots start at 6** (0–5 reserved); per char: `PlayerBattleEntity` bound to the shared `playerBattleScript`, position x = 0x900 (charSlot 0 with party > 2) else 0xa00, z = `0x800·((slot+1)/2)·(slot%2·2−1) + (partySize%2−1)·0x400` (alternating left/right), y = 0; per-char async battle-SFX directory load (bit 0x8 cleared when all done); `initPlayerBattleEntityStats` (§8) |
| 11 | — | `waitForFilesToLoad` | |
| 12 | ASSETS | `loadEncounterAssets` (@ 0x800c791c, `:2230`) | `loadEnemyTextures` (`monsters/<id>/textures/combat`); per-monster `loadCombatantTmdAndAnims` (async; `loadingMonsterModels` counter); player bent flags `|= 0x2a`; `loadPartyTims`/`loadPartyTmdAndAnims` (async per-char); `battleFlags |= 0x400` (encounter assets requested) |
| 13 | — | `waitForFilesToLoad` | |
| 14 | HUD | `loadHudAndAttackAnimations` (@ 0x800c7964, `:2311`) | `battleFlags |= 0xc`; `loadBattleHudTextures` (VRAM: main sheet 704,256; 16×16 CLUT tiles at 704+{0,16,32,48},496 and 896+{0,16},304, `:8328`); `loadBattleHudDeff`; per-combatant `loadAttackAnimations` |
| 15 | — | `waitForFilesToLoad` | |
| 16 | START | `battleStart` (@ 0x800c79f0, `:2327`) | `currentTurnBent = allBents.getFirst()`; `hud.initCharacterDisplay()`; post **`BattleStartedEvent`** (mod event, §12) |
| 17 | — | `waitForFilesToLoad` | |
| 18 | — | `loadSEffe` (@ 0x800c7a30, `:2338`) | **stub** — advances immediately (the effect system is static; §11) |
| 19 | — | `waitForFilesToLoad` | |
| 20 | TURNS | `calculateInitialTurnValues` (@ 0x800c7a80, `:2343`) | gated on `battleInitialCameraMovementFinished`; `battleFlags |= 0x10`; `battleState.calculateInitialTurnValues()` (§6) |
| 21 | PLAY | `battleTick` (@ 0x800c7bb8, `:2352`) | the per-tick turn/exit dispatcher (§6.3) |
| 22 | — | `waitForFilesToLoad` | |
| 23 | FADE | `fadeOutBattle` (@ 0x800c8068, `:2473`) | post-combat action frame machine (§12.3) |
| 24 | DEALLOC | `deallocateCombat` (@ 0x800c82b8, `:2511`) | gated on full-screen effect color 0xff; full teardown (§2.3) |
| 25 | — | `waitForFilesToLoad` | |
| 26 | POST | `renderPostCombatScreen` (@ 0x80018508, `:2561`) | waits for `whichMenu == NONE_0`; (source comment: the SMAP preload that existed here was removed — "only takes a few milliseconds") |
| 27 | OUT | `transitionBackFromBattle` (@ 0x800189b0, `:2573`) | §2.3 |

## 4. Timing model (B3)

- `tickMultiplier() = 1` (`:704`) — one engine tick per frame, unlike wmap (3).
- `SCRIPTS.setFramesPerTick(1)` in `loadGameStateOverlay` (`EngineStates.java:70`) — one script frame per tick (submap runs 2, M9-03 SM1).
- `vsyncMode` history: **1** during the entry dissolve (`clearCombatVars`) → **3** at `initializeViewportAndCamera` (and re-asserted on every tick the turn gate opens — i.e. each new-turn selection, `:2364`) → **2** at `transitionBackFromBattle`. The global `timestamp_a0` advances by `vsyncMode` per game tick (`Scus94491BpeSegment.java:146`) — in play it grows 3/tick, and it is the turn-order seed (§6.2).
- Durations scale with `vsyncMode` where retail wrote them that way (the `150/vsync`, `300/vsync` transition constants, `encounter.introWaitTicks` as a plain tick count).
- The 30-tick fade in at stage 6 and the 25-tick minimum post-combat hold (§12.3) are tick counts at the active vsync rate.

## 5. Data model

### 5.1 `BattleStateEf4` — the battle state struct (ROM 0x8006e398)

Allocated per-battle by `FUN_800c8624` (`:2587`), nulled by `FUN_800c8748` (`:2598`). Contents (B5, source-verified):

- **Bent lists**: `allBents_e0c` (all), `playerBents_e40`, `monsterBents_e50[10]` (fixed 10-slot array), `aliveBents_e78`, `alivePlayerBents_eac`, `aliveMonsterBents_ebc` — the alive lists are rebuilt every tick by `cacheLivingBents()` (@ 0x800c7304) from `FLAG_DEAD`; add/remove maintain the `allBentSlot_274` / `typeBentSlot_276` slot indices.
- **Combat vars 1–10** (`combatantBentIndex_184`…`_1a8`) and **vars 11–20** (`scriptIndex_1ac`…`_1d0`) — script-visible scratch the decompiled player combat script and enemy scripts use (cutscene DEFFs store combatant indices in 1–10, script indices in 11–20, per the field comments).
- **Turn state**: `turnOrderSeed` + `turnOrderRng` (layer-4 addition, §6.2), `turnCount` lives in `gameState.turnCount_b8`.
- **Addition (mash) state**: `additionLoadingStage_2c8`, `additionHitIndex_2cc`, `additionTicks_2d0`, `additionRemainingSuccessFrames_2d4`, `additionTotalFrames_2d8`, `additionOverlayHitFrameOffset_2dc`, `additionTotalSuccessFrames_2e0`, `additionHitCount_2e4`, `additionState_324` (0x0 continue, 0x1 counter succeeded, 0x2 addition failed, 0x10 attack missed, 0x20 addition completed, 0x80 counter failed), `additionExtra_474[8]`, `numCompleteAdditionHits_280`, `counterAttackStage_288` (0 none / 1 allocated / 2 animation played / 3 failed) — detailed in **05c**.
- **Script-consumed battle params**: `dragoonTurnsRemaining_294[3]` (per char slot), `specialFlag_2b0` (char ID: 0 Dart … 8 Miranda), `runAwayFlags_2b4` (smoke-bomb state), `magicItemDamageMultiplier_3cc` (clamped 1–999, else 100%), `magicItemLevel_4a8` (clamped 5–8, else 5 — enemy button-mash), `globalMenuBlocks_510`, `fieldFlags_550` (0x1 dragon block staff, 0x2 targeting arrow starts on a random enemy), `attackTargets_558` (target bitset), `monsterMoveId_570`, `battleStage_46c` (staged at player-script init, updated for transformation stages), `sequenceVolume_358`, `succCloneEffectId_248` (Windigo/Melbu), `battlePhase_eec` (boss phase, script-readable via game var 39), `cameraControllerScriptTicksParam_ef0` ("only used in 800db8b0 inside DRGN1/401").
- **Asset pools**: `assets_580[0x100]` (256 global `BattleAsset08` slots; state 0 free / 1 loading / 2 loaded; round-robin `getFreeGlobalAssetIndex` throws if full) and `compressedAssets_d8c[16]`.

### 5.2 `CombatantStruct1a8` — the 10 combatant slots

`combatants_8005e398[10]` (B5): `addCombatant(charId, charSlot)` (`:2798`, @ 0x800c8f50) linear-scans for a free slot; `charSlot < 0` marks a monster (`flags_19e = 0x1`); `charSlot_19c` (−1 = monster) and `charIndex_1a2` index the entity and the ROM tables. Each slot carries the per-combatant assets (`assets_14`, `mrg_00`/`mrg_04`), rewards (`xp_194`, `gold_196`, `_19a`, `drops`), and `scriptPtr_10` (the enemy's script, §7.3). Max party 4 + max monsters — the 10-slot array plus `monsterBents_e50[10]` allow up to 10 bents; encounter construction caps **unique** monster species at 3 (§5.4).

### 5.3 `battleFlags_800bc960` (global) and `loadingAudioFiles_800bcf78`

`battleFlags` is a **reused multi-purpose flag word** (B5, source-verified usages; the word is zeroed in `initBattle`):

| Bit | Meaning (verified use) | Set | Checked |
|---|---|---|---|
| 0x1 | entry dissolve transition complete | `SBtld.tickAndRenderTransitionIntoBattle` (`:203`) | `initializeViewportAndCamera` (`:2037`, with 0x2) |
| 0x2 | combat controller script (DRGN1/401) loaded | `combatControllerScriptLoaded` (`:9322`) | `:2037`; `tickAndRenderTransitionIntoBattle` (`:190`) |
| 0x4 | combatant TMD loaded (per-combatant callback `:2938`, non-monster) **and/or** monster models loaded (reused word) | `:1313`, `:2254`, `:2937` | `:1312` |
| 0x8 | character models loaded (`:1308`) **and/or** HUD assets loaded (reused word) | `:1308`, `:2312` (with 0x4) | `:1307` |
| 0x10 | initial camera movement finished | `calculateInitialTurnValues` (`:2345`) | `:2344` |
| 0x20 | battle live (stage rotation/rendering enabled) | `battleInitiateAndPreload` (`:2063`) | `rotateAndRenderBattleStage` (`:2739`) |
| 0x40 | viewport initialized | `initializeViewportAndCamera` (`:2041`) | — |
| 0x80 | skybox MCQ copied to (320, 0) | `FUN_800c8e48` (`:2775`) | `renderSkybox` (`:2625`), `loadStageMcq` (`:2752`) |
| 0x400 | encounter assets requested | `loadEncounterAssets` (`:2239`) | `:1306` |

`loadingAudioFiles_800bcf78` (async audio gates): 0x4 cutscene SFX, 0x8 player battle SFX, 0x10 monster SFX, 0x40 deff SFX, 0x80 encounter music, 0x4000 music-pending (set when the encounter has a music index).

### 5.4 `Encounter` — the registry content model

`Encounter` (`encounters/Encounter.java`, a `RegistryEntry` — M8-03: 512 `LEGACY` entries registered by the `lod` core mod from the retail encounter tables): `monsters: List<Monster{id, pos}>` (**at least 1**, constructor-enforced), **`uniqueIds` (at most 3 unique species**, constructor-enforced), `musicIndex` (5-bit), `escapeChance`, `playerOpeningCamera` / `monsterOpeningCamera` (jump indices into the DRGN1/401 camera table), `cameraPosIndices[4]`, `postCombatSubmapCut` (0xffff = none), `postCombatSubmapScene` (0xff = none), `introWaitTicks`. `get(index)`/`set(index)` (`:66–92`) expose the "stage data" the script reads/writes: 1 music, 2 escape, 3 post-combat scene, 4/5 opening cameras, 6–9 camera position indices, 10 post-combat cut (only 6–9 are writable). `onBattleWon` sets `postBattleAction = CorePostBattleActions.VICTORY` + `playVictoryMusic()`; `onBattleLost` loads music package 19 (game-over) + `postBattleAction = CorePostBattleActions.GAME_OVER` — the post-battle actions are ordinary registry entries (M8-03: `victory` / `game_over` / `play_fmv`).

## 6. The turn system (native, script-orchestrated)

### 6.1 Initial turn values

`BattleStateEf4.calculateInitialTurnValues()` (called at stage 20, once the opening camera move finishes): for every bent, **monster** `turnValue_4c = simpleRand() · 0xd9 / 0x10000` ∈ [0, 216] (max = 65535 × 217 / 65536 = 216), **player** `turnValue_4c = simpleRand() · 0xa7 / 0x10000 + 0x32` ∈ [50, 216] — both from the **retail global `simpleRand` LCG** (M4-05 §2.3: seed 3, 16-bit word `seed/2 & 0xffff`). Players start with a head start; the values are a "turn gauge" (B4).

### 6.2 Turn selection — `getCurrentTurnBent` (@ 0x800c7ea0)

Turn order is **not** deterministic ATB. It is a bounded random-accumulation simulation (B4, source-verified):

1. `turnOrderSeed = gameState.timestamp_a0`; `turnOrderRng.setSeed(seed)` — i.e. the jitter RNG is **re-seeded at the start of every turn selection** (per `getCurrentTurnBent` call).
2. Repeat up to **32 rounds**:
   - find the alive bent with the highest `turnValue_4c` (ties: **last wins** — the `<=` comparison lets later combatants overwrite);
   - if that value **> 0xd9 (217)** (strictly greater): that bent takes the turn — `turnValue −= 0xd9`; if it is a player, `gameState.turnCount_b8++`; return it;
   - otherwise every alive bent gains `round(SPEED · jitter)` and the loop continues.
3. If no bent exceeds 217 after 32 rounds (practically impossible), the first alive player acts.

**The jitter RNG is a layer-4 replacement** (verified against `eae9fb4f6`, "Separate turn order RNG (closes #2481)"):

| | Retail (before the commit) | Reference (current) |
|---|---|---|
| Source | global `simpleRand()` LCG (shared with submap encounters, M9-03 SM2) | per-battle `legend.core.Random turnOrderRng` field (a `java.util.Random` subclass whose only override is `nextInt` — a mix64 `mix32` remix; `nextFloat` unmodified), **re-seeded with `timestamp_a0` at the start of every `getCurrentTurnBent` call** |
| Jitter formula | `simpleRand() / (float)0xffff · 0.2 + 0.9`, `simpleRand()` ∈ [0, 0xffff] | `nextFloat() · 0.2 + 0.9`, `nextFloat()` ∈ [0, 1) |
| Jitter range / mean | **[0.9, 1.1]**, mean ≈ 1.0 (0.9 + 0.2·(65535/2)/65535 ≈ 0.99998) | **[0.9, 1.1)**, mean 1.0 |
| Granularity | 16-bit LCG word (65,536 distinct jitter values) | 24-bit `nextFloat` (≈16.7M distinct values) |

So the reference battle's turn order is (a) **decoupled from the global LCG stream** — the actual point of the change: the `turnorder` mod (`TurnOrderMod`/`TurnOrderConfigs`, M7-02/M8-03) must be able to steer turn order without perturbing other `simpleRand` consumers — and (b) statistically near-identical in range and mean, but finer-grained and deterministic given the `timestamp_a0` progression (the per-turn re-seed makes the jitter reproducible for a given battle's tick history, though with different values than retail). The initial values (§6.1) and the opening-camera draws (§7.2) still use the retail `simpleRand`. This is the battle leg of the Q-70 seed/RNG-source discussion: the *mechanism* is fully source-verified on both sides of the change; which behavior a "retail-faithful" Rust port wants is an M10 decision.

### 6.3 `battleTick` dispatch (@ 0x800c7bb8, `:2352`)

Every tick: `hud.draw()`; if `postBattleAction != null` → `loadingStage++` and return (the machine passes the loader gate to the resident `fadeOutBattle` stage, §12.3). Otherwise, when the loader is idle, bents exist, `!combatDisabled`, and **no bent has `FLAG_400 | FLAG_CURRENT_TURN`** (`FUN_800c7da8`, @ 0x800c7da8 — the "nobody is mid-action" gate, B6):

- `vsyncMode = 3`, `mcqColour = 0x80`, clear `FLAG_RELOAD_BATTLE_ACTIONS` on the previous current bent, `battleMenu.currentAction = null`;
- **players alive?**
  - `forcedTurnBent = battleState.getForcedTurnBent()` (first alive bent with `FLAG_TAKE_FORCED_TURN` (0x20), @ 0x800c7e24) — if set: clear `FLAG_TAKE_FORCED_TURN`, set `FLAG_RELOAD_BATTLE_ACTIONS | FLAG_CURRENT_TURN`, make it `currentTurnBent`. Two script-facing ways to request a forced turn: setting `FLAG_TAKE_FORCED_TURN` on a bent (the effective mechanism), or **game var 42** assigning `battle.forcedTurnBent` directly (§7.4) — but note the gate **unconditionally re-resolves** `forcedTurnBent` from the flag scan on every gate tick (`:2368`), so a bare game-var-42 write is overwritten by the next gate evaluation; the flag is what persists. (Engine code sets neither — the retail usage is scripted sequences);
  - else if **monsters alive**: `currentTurnBent = battleState.getCurrentTurnBent()` (§6.2) + same flags;
  - else `allMonstersDead()` → `Audio.FUN_80020308()` (stop music + SFX slots 1/3/4/5/6) + `encounter.onBattleWon(this)`;
- else `allCharactersDead()` → `encounter.onBattleLost(this)`.
- After the branch, if `postBattleAction` was set (a win/loss was declared this tick) → advance the stage machine.

On every new current turn the engine posts **`BattleEntityTurnEvent`** (mod event; M8-02 family table) and clears `hud.currentAttackElements`. The *flow of the turn itself* (menu, targeting, animation sequencing, damage application) is the player combat script's job (§7) — the native engine only exposes the actions as script functions.

## 7. The script boundary (RQ7, B7)

Battle is the most script-driven system in the game — the opposite pole from title/wmap (zero scripts, M9-02 T15 / M9-04 W14) and the submap (per-map controller scripts, M9-03 §3.2).

### 7.1 The 362-slot battle script-function table

`Battle.getScriptFunctions()` (`:734–1131`) returns a 1024-slot array; **362 slots are actively assigned** (mechanical census this session — matches the M3-06 592-slot catalog exactly: 86 base + 362 Battle + 144 Submap; four further assignments, slots 445, 603, 617, 842 — `Temp::` methods — are commented out in source and appear in the meta catalog as no-ops). By owning class: **243 `Battle` methods, 111 `SEffe` static methods** (the effect system, §11), **8 `Scus94491BpeSegment` methods** (mostly the `scriptRewindAndPause2` no-op). **69 slots remain `FUN_-`named** in the reference — a direct input to the semantic-naming policy for the Rust port. The full parameter-level contract catalog is M3-06 §3 + `patches/meta/*.csv` (mechanically verified there). Slots are grouped in 32-aligned ranges (B8, source-verified):

| Range | Count | Content (verified examples) |
|---|---|---|
| 32–50 | 19 | camera: reset/set/move/accelerate viewpoint & refpoint, `scriptIsCameraMoving` (41), `scriptCalculateCameraValue` (42), wobble (44), stop (45), viewport twist (46), projection plane 48–50 |
| 128–178 | 51 | bent manipulation: set/get pos (128/129), rotation (130/132/134), hide (138), interpolation (139), animation index 140–155 (pause/resume/loop/finished), look-at (156), **`scriptSetUpAndHandleCombatMenu` (160)**, **`scriptOverrideBattleMenuActions` (161)**, **`scriptRenderDamage` (164)**, **`scriptAddFloatingNumberForBent` (165)**, char/monster ID (167), **set/get stat (168/169)**, **`scriptSetPostBattleAction` (170)** |
| 182, 226, 228–229, 238–239, 245–247 | 9 | scattered helpers |
| 320–324 | 5 | |
| 352–372 | 21 | |
| 416, 419–444 | 27 | |
| 480–509 | 30 | |
| 512–526 | 15 | |
| 544–602 | 59 | (largest block — `SEffe` effect allocation/attach) |
| 605–608, 610–616, 618–632 | 26 | (`SEffe` LMB/deff/particle) |
| 634–651 | 18 | (`SEffe` XA/deff-stage/shadow) |
| 712–713, 715–718 | 6 | |
| 736–767 | 32 | incl. `scriptAllocateFullScreenOverlay` (737, 8 params) |
| 800–807 | 8 | (`SEffe` big-effect allocators: lens flare, Dragoon transforms, Star Children, moonlight stars) |
| 832–841, 843–854 | 22 | (`SEffe` rain/electricity/addition overlays) |
| 896–897 | 2 | (`SEffe` gradient rays / screen capture) |
| 1000–1005, 1010–1013, 1020–1021 | 12 | high-range functions |

The formulas sit *inside* this surface: `scriptPhysicalAttack` (@ 0x800f2500, `:8637`) takes attacker/defender script indices and returns damage + a special-effects bitset; `checkHit` (§8.2) and `calculateMagicDamage` (§8.3) are native callees the script invokes (directly or through these wrappers). The script never computes damage — it only sequences.

### 7.2 The four script layers

| Layer | Asset | Loaded by | Script slot(s) | Provenance |
|---|---|---|---|---|
| Player combat script | `player_combat_script` — **embedded in `S_BTLD.OV_` at slice (0x4, 0x68d4)** (M2-04 §2); the retail base script | `loadStageDataAndControllerScripts` (`:9300`), synchronous `loadFileSync` | 6, 7, 8, 9 — one `ScriptState<PlayerBattleEntity>` per party member, each bound to the same `ScriptFile` (`allocatePlayerBattleEntities`, `:2116`) | **replaced** in the reference: `scripts.csv:2` = `replacement, player_combat_script, scripts/player_combat_script.txt, "Major rewrites to support modding"` — the shipped script is an L4 assembly rewrite of the retail base; the turn-flow behavior documented in 05c is the *rewritten* script's behavior (the retail base's exact behavior is asset-validated-pending, §13) |
| Combat controller script | `DRGN1/401` (MRG file 401 in the SECT DRGN1 bank) | async `loadDrgnFile(1, "401")` → `combatControllerScriptLoaded` (`:9306`) | **slot 5** (`SCRIPTS.allocateScriptState(5, "DRGN1.401", null)`) | retail (decompiled) |
| Enemy scripts | `SECT/DRGN1.BIN/<enemyId+1>` — one Fate script per enemy species | `loadEnemyDropsAndScript` (`:9326`), async, stored as `combatant.scriptPtr_10`; **each monster bent's ScriptState loads the shared species `ScriptFile`** — `MonsterBattleEntity.getScript()` (`MonsterBattleEntity.java:53`) is consumed by `BattleEntity27c.java:688` (`state.loadScriptFile(this.getScript())`), so one script file runs per monster instance | per-combatant states | retail, with **diff patches** from `scripts.csv` (e.g. `SECT/DRGN1.BIN/20` "Lizardman deallocate leaked script state", `/84` "Crafty Thief steal item registry", `/254–258` tutorial/boss fixes) — L3 content moddable by recompile-patch (M8-03 §3) |

(The model-directory `files.get(34)` of each combatant TMD load is also assigned to `combatant.scriptPtr_10` (`:2945`, `combatantTmdAndAnimLoadedCallback` @ 0x800c941c) with the in-source note "I don't think this is actually used?" — for players `getScript()` returns the shared player combat script instead, so that assignment is inert for player bents; the ordering between the two monster assignments is async and unsequenced in source.)
| Camera script data | the DRGN1/401 camera table | consumed via `cameraScriptMainTableJumpIndex_800c6748` + subtable jump indices + `currentCameraIndex_800c780` | — | the "camera controller" inside the controller script (`cameraControllerScriptTicksParam_ef0`, §5.1) |

At controller-script load time (`combatControllerScriptLoaded`, @ 0x80109170): the **opening camera is chosen 50/50 by `simpleRand() & 0x8000`** between `encounter.monsterOpeningCamera` and `encounter.playerOpeningCamera`; `cameraScriptMainTableJumpIndex = openingCamera + 1`; one of `encounter.cameraPosIndices[4]` is picked by `simpleRand() & 3` (B9 — two retail-LCG draws per battle); `battleFlags |= 0x2`. Scripts can later override the jump index via **game var 44**.

### 7.3 Enemy rewards and drops (`loadEnemyDropsAndScript`, `:9326`)

Rewards come from the ROM table `enemyRewards_80112868[enemyId]` (`EnemyRewards08`: xp, gold, item chance, item drop, unknown `_06`); the **`EnemyRewardsEvent`** is posted and its fields read back (the M8-02 "event as RPC" mutability idiom — mods can rescale XP/gold/drops). The enemy script is resolved as `Loader.resolve("SECT/DRGN1.BIN/" + (enemyId + 1))` through a **`LoadEnemyEvent`** (mutable `scriptFuture`) and attached to the combatant.

### 7.4 Battle game vars (script-visible battle state)

From `GameVarParam.java:255–275` (the M9-03/04 "game var as script global" pattern, extended to battle): 39 `battlePhase` (boss phase), 40 no-op (retail full-inventory drop flag — documented as ignored), 41 add drop (the **same 192-boundary encoding** as M9-01 D7: `id < 192` → equipment, else item `id−192`), 42 `forcedTurnBent` (set from a script state — the direct forced-turn entry point; §6.3 notes the gate's unconditional flag-scan re-resolution makes the bent *flag* the persistent mechanism), **43 start encounter** (dual int/RegistryId, §2.1), 44 `cameraScriptMainTableJumpIndex`, 47 `battleStage` (mid-battle stage change), 48/50/52 replace `aliveBents[0]` / `alivePlayerBents[0]` / `aliveMonsterBents[0]` (char swap-in mechanism, GH#2882). Commented-out cases 45/46/49/51 are decompiler artifacts of removed fields.

### 7.5 Gaps and preserved bugs

- `scriptStartEncounterSounds` / `scriptStopEncounterSounds` (@ 0x8001af34 / 0x8001afa4, `:1469–1483`) **`throw new RuntimeException("Not implemented")`** — decompiled retail script functions the reference does not implement (the M6-01 "No longer implemented" pattern). Any retail script that calls them would crash the reference; none of the shipped patched scripts are known to (asset-validated-pending).
- **Preserved retail bug** (`:1352` comment): one of the Divine Dragon Spirit's attack scripts requests `soundIndex 10` in a 10-entry sound file (`DRGN0.1225.1.1`); the reference bounds-checks and skips (the retail behavior is the same skip, since the retail SPU lookup would also miss).
- **GH#1931 workaround** (`loadStage`, `:2689–2707`): texture animations are disabled on the old stage model and the new stage load is deferred a frame via `RENDERER.addTask` to avoid VRAM corruption.

## 8. Stat setup and core formulas

### 8.1 Player battle stats (`initPlayerBattleEntityStats`, @ 0x800ef28c + `:8413`)

Battle stats = the character's persistent stats **plus every equipped item** (B10, source-verified): per slot the code adds `attack`/`magicAttack`/`defence`/`magicDefence`/`attackHit`/`magicHit`/`attackAvoid`/`magicAvoid`/guard-heal to the raw stats and accumulates the equipment bitmasks/lists — `specialEffectFlag_14`, `equipmentAttackElements_1c`, `equipmentElementalResistance_20`, `equipmentElementalImmunity_22`, `equipmentStatusResist_24`, `onHitStatusChance_44` (+) / `equipmentOnHitStatus_4a` (|), physical/magical immunity + resistance masks, SP/MP multipliers, per-hit SP/MP gains, `escapeBonus`, HP/MP/SP regen, revive, HP/MP multipliers. Also copied: level/dlevel, `status_0e = character.getStatusAndFlags()`, the selected addition (registry instance), and the "original" per-hit SP/MP values (restoration points for stat-mod effects).

### 8.2 Hit check (`checkHit`, @ 0x800f1aa8, `:8477`)

```
effectAccuracy =
  PHYSICAL:        monster attacker → attacker.spell_94.accuracy_05 ; player → ATTACK_HIT stat
  DRAGOON_MAGIC_STATUS_ITEMS: attacker.spell_94.accuracy_05
  else (MAGIC/ITEM):          100
effectAccuracy   = effectAccuracy · (tempHitStat + 100) / 100          // hit-rate modifier %
avoidChance      = defender.ATTACK_AVOID (physical) | MAGIC_AVOID (magic)
modifiedAvoid    = avoidChance · (tempAvoidStat + 100) / 100           // avoid modifier %
HIT ⟺ modifiedAvoid < effectAccuracy
        AND (effectAccuracy − modifiedAvoid) ≥ (simpleRand() · 101 >> 16)   // roll ∈ [0, 100]
overrides: ITEM_MAGIC + item.alwaysHits(); (alwaysHitStat & alwaysHitMask) ≠ 0
```

The roll is a uniform integer in [0, 100] (16-bit `simpleRand` word × 101, 16-bit right shift — max = 65535 × 101 >> 16 = 100) from the **retail `simpleRand`** — the accuracy must strictly exceed the defender's modified avoid, and the margin must be at least the roll (B11).

### 8.3 Magic damage (`calculateMagicDamage`, @ 0x800f204c, `:8577`)

- Stat-mod items: `item.calculateStatMod(attacker, defender)`.
- **HP-percentage spells** (`spell.flags_01 & 0x4`): `damage = defender.maxHP · spell.multi_04 / 100`, applied via `applyBuffOrDebuff` to all targets when `targetType_00 & 0x8` (attacker's side — players for player casters, alive monsters otherwise), else the single defender; sets `attacker.status_0e |= 0x800`.
- Normal magic: `damage = attacker.calculateMagicDamage(defender, magicType)` (entity-level, 05b) → **attacking-element adjustment** (`attackElement.adjustAttackingElementalDamage(type, damage, defenderElement)`) → **defending-element adjustment** (`defenderElement.adjustDefendingElementalDamage(...)`) → **`adjustDamageForPower(damage, attacker.powerMagicAttack_b6, defender.powerMagicDefence_ba)`** → Dragoon-Space element adjustment when `dragoonSpaceElement != null`. Clamped ≥ 0.
- `adjustDamageForPower` (`:8547`, static): base 1.0 — attacker power < 0 ⇒ ÷2; defender power > 0 ⇒ ÷2; attacker power > 0 ⇒ +0.5; defender power < 0 ⇒ +0.5 (i.e. ±½-step power-stat damage scaling, sign-dependent per side; the same function serves the physical path via the physical power stats).

The general-purpose `formula/` package (`Formula`, `FormulaBuilder`, `Operation`, `Side`, `Step`, `State`, `PhysicalDamageFormula`) is a **data-driven formula system** — the exact damage pipelines are stored as formula objects (decompiled retail data), detailed in 05b.

### 8.4 End-of-battle state write-back (`updateGameStateAndDeallocateMenu`, @ 0x800eeaec, `:8361`)

Per surviving player: `character.HP = max(1, battleHP)` (**at least 1 HP survives a won battle**), MP written back only `if (character.canUseSpells())`, `character.status_10 = bent.status_0e`, SP current copied. Then **all characters**: `status_10 &= 0xc8` — only bits 0x40 and 0x80 persist past battle (all other statuses clear; the bit identities are 05b material). `usedRepeatItems` (repeatable battle items) are refunded to the inventory, overflow → `itemOverflow`. `hud.delete()`.

## 9. Battle audio (B13)

Music selection (`loadEncounterSoundsAndMusic`, @ 0x8001de84, `:1989`): unloads SFX slots 1/3/4/5/6; if `encounter.musicIndex != 0xff`: stop music, unload slot 8, then through the ROM tables in `Audio.java:49–60` (32 entries each, indexed by `musicIndex & 0x1f`): `musicIndex = combatMusicFileIndices_800501bc[i]` and `victoryType = combatSoundEffectsTypes_8005019c[i]` mapped **0xc→696, 0xd→697, 0xe→698, 0xf→699, 0x56→700, 0x58→701, else `parseMelbuVictory` (index 0x13 → 732, else −1)**. The table resolves to: index 0 → type 0xc, file **702**; index 1 → 0xd, **707**; index 2 → 0x56, **722**; indices 3–15 → 0xc, **702**; 16 → 0xe, **712**; 17 → 0xf, **717**; 18 → 0x58, **727**; 19 → 0x5a, **732** (Melbu — the `parseMelbuVictory` default leg); 20–31 → 0xe, **712**. Victory jingles: 696–701 and 732 (732 doubles as the Melbu battle song and its victory song, loaded via `musicPackageLoadedCallback(files, 732, true)`). A **`BattleMusicEvent`** (mutable) is posted before the load. `playVictoryMusic()` (`:1464`) = `AUDIO_THREAD.loadBackgroundMusic(victoryMusic) + startSequence()`; the game-over path loads music package 19 directly (`Encounter.onBattleLost`).

Per-bent SFX: players load `<char>/battle/sounds` (4 files: id, indices, SSHD, soundbank) per character at stage 10 (bit 0x8); monsters load `monsters/<id>/sounds` (soundbank must exceed 48 bytes; sequence volume 0x7f, bit 0x10); **boss phase changes** load `monsters/phases/<boss>/<phase>/<slot>` for up to 4 monster slots (`loadBattlePhaseSounds`, `:1595`, gated by `battleState.battlePhase_eec` / game var 39); battle **cutscenes** load sound packages from DRGN0 dir `2437 + cutsceneIndex·3` plus extra soundbanks `2437 + id·3 + i` concatenated into one soundbank (`:1737–1790`, script-triggered via `scriptLoadBattleCutsceneSounds`); deff SFX use the `deffSounds` file (bit 0x40). `sssqResetStuff()` (@ 0x80020008, `Audio.java:824`) = stop all + unload slots 1/3/4/5/6/8 + stop music — called on `destroy()`, `restoreMusicAfterMenu`, and cutscene-sound load; it also has a script wrapper (`scriptStopAndUnloadSequences`). SPU sequencing honors the 24-voice model (M4-03: `stopBentSound` scans the 24 `SpuStruct08` entries).

## 10. Stage and environment

`loadStage(stage)` (`:2682`): battle stages live at **`DRGN0/2497+stage`** (three assets: MCQ skybox if `hasVirtualSize`, TIM texture, and the `"/0"` subdir holding a **CTMD** `CContainer` model + `TmdAnimationFile` — stages are animated models, rotated 90°, `:2603`); `currentStage_800c66a4 = stage`. The skybox MCQ uploads at x=512 and is copied to (320, 0) by `FUN_800c8e48` (bit 0x80) when the battle viewport initializes; `mcqBaseOffsetX = (0x400/screenWidth + 1) · screenWidth`. Stage ambiance (wind/ambient SFX) and per-stage deff configuration come from the ROM tables `stageAmbiance_801134fc[stage]` (dragoon-space ambiance from entries 71+i) and `_8011517c[stage]` (`StageDeffThing08[90]`, `SBtld.java:292+`; nonzero entries at stages 10, 15, 23, 30, 32, 58, 61, 62, 77, 82, 87); Melbu stage indices `{93, 94, 95, 25, 52}` get the stage's deff settings too (`loadStageAmbiance`, @ 0x801098f4, `:9349`). Stage darkening (per-stage 7200-byte table, `BattleStageDarkening1800 stageDarkening_800c6958` static) is allocated per battle.

## 11. `SEffe` — the static effect and addition system (overview; 05c for additions)

`SEffe` (`:151`, `public final class` with a **private constructor**) is entirely static state with ROM addresses — the effect layer the battle scripts drive (B16). It owns: all visual battle effect types (lightning bolts, rain + raindrops, electricity, frozen jets, gradient rays, lens flares, moonlight stars, star-children impacts, screen capture/darkening/distortion, shadows, Shirley transform wipes, gold Dragoon transforms, billboard/sprite/trail sprites, `LmbAnimationEffect` — deff-LMB-driven effect animation, `ModelEffect`, addition overlays), plus the **addition (mash) spinner HUD**:

- `daddyHitsCompleted_80119f40` (−1 failed / 0 continue / 1–4 successful presses), `additionOverlayActive_80119f41`, `daddyMeterSpinning_80119f42`;
- **spinner step counts per addition hit** `daddyHudSpinnerStepCounts_80119f44 = {14, 12, 10, 8, 1, 1, 0}` with **success windows** `daddyHitSuccessWindows_80119f60 = {4, 3, 2, 1, 0, 0, 0}` frames;
- a **Kongol-specific variant** (`"special boi Kongol"` in-source comment): steps `{14, 12, 10, 1, 1, 1, 0}`, windows `{4, 3, 2, 0, 0, 0, 0}`;
- `additionHitCompletionState_8011a014[]` per hit: 0 not attempted, 1 success, −1 too early, −2 too late, −3 wrong button;
- per-character HUD meter geometry (`daddyHudMeter*` 5-slot tables, eye/frame CLUT offset tables), `perfectDaddyGlyphUs/Vs` (8-glyph perfect-addition banner).

The reference's internal name for the addition overlay is "daddy" (`daddy*` identifiers); this document uses "addition overlay (spinner)" and cites the `daddy*` symbols for traceability. The battle-side addition variables live in `BattleStateEf4` (§5.1) and `BattlePreloadedEntities.dragoonAdditionHits_38` (per-char, filled by `SBtld.loadAdditions` @ 0x80109250 from `character.getDragoonAddition()`). Full addition semantics (counter attacks, per-character button maps, Starbusts) are 05c.

## 12. Mod and event surface

### 12.1 Events (all M8-02 contract: exact-class dispatch, mutable = "event as RPC")

| Event | Posted at | Mutable payload |
|---|---|---|
| `BattleMusicEvent(battle, victoryType, musicIndex, encounter)` | `loadEncounterSoundsAndMusic` | musicIndex, victoryType |
| `EnemyRewardsEvent(enemyId, xp, gold, drops)` | `loadEnemyDropsAndScript` | xp, gold, drops |
| `LoadEnemyEvent(battle, enemyId, combatant, script)` | `loadEnemyDropsAndScript` | scriptFuture |
| `BattleStartedEvent(battle, encounter)` | `battleStart` (stage 16) | — |
| `BattleEntityTurnEvent(battle, encounter, currentBent)` | `battleTick` on every turn start (forced and normal) | — |
| `BattleEndedEvent(battle, encounter)` | `fadeOutBattle` completion | — |

### 12.2 Registries consumed by battle

`encounters` (512 LEGACY), `spells`, `additions`, `items`/`equipment` (drop encoding §7.4), `elementIcons` (HUD attack elements), `element` (element adjustments), the 10 `battle_actions` + `stat` types (M8-03), post-battle actions (`victory`/`game_over`/`play_fmv`), and the battle-action registry (`GatherBattleActionsEvent` / `RegisterBattleActionsEvent`, `ui/BattleActionRegistry`) — the moddable per-turn action set, 05c. Config entries: `BATTLE_TRANSITION_MODE`, `REDUCE_MOTION_FLASHING`, `ALLOW_WIDESCREEN` (M7-02). The 362-slot script-function table is the engine-side contract; the in-repo 353 `scripts.csv` patches (2 replacements — `player_combat_script`, `throw_item` — + 351 diffs, M8-03 §3) are the shipped script layer.

### 12.3 Post-combat frame machine (`fadeOutBattle`, @ 0x800c8068)

Frame-counted per tick: frame 0 fires `postBattleAction.onCameraFadeoutStart` and snapshots `livingChars_800bc968` (a **global** on `Scus94491BpeSegment_800b`, not a `Battle` field — surviving player characters, consumed by `PostBattleScreen` for the XP report, 05c); the action ends when `currentPostCombatActionFrame ≥ action.getTotalDuration(this)` **or** (menu confirm/back pressed **and** frame ≥ 25 — the 25-tick skip guard); then `BattleEndedEvent`, lighting/deff deallocation, optional fade (`startFadeEffect(1, action.getFadeDuration)` only when the full-screen effect colour is 0), `onCameraFadeoutFinish`, frame reset, `loadingStage++` → the resident stage leaves and the machine proceeds to `deallocateCombat` (which runs `postBattleAction.performAction(this)` — the actual XP/gold/drop/level-up application — after the submap reposition, §2.3).

## 13. Findings (B1–B20)

| # | Finding | Layer | Status |
|---|---|---|---|
| B1 | Entry model: 4 start paths (submap `RetailSubmap.java:527`, wmap `WMap.java:3728`, game var 43 dual int/RegistryId, debugger); `SBtld` dissolve transition ticks globally at vsyncMode 1 (`Scus94491BpeSegment.java:174`), `BattleTransitionMode` + `REDUCE_MOTION_FLASHING` L4 modifiers, `battleFlags 0x1/0x2` gate the viewport init | 3 (+4 modifiers) | source-verified |
| B2 | 28-entry `battleLoadingStage` machine with `waitForFilesToLoad` interlace; per-stage work as tabled in §3 | 3 | source-verified |
| B3 | Timing: `tickMultiplier 1`, `framesPerTick 1`, vsyncMode 1→3→2 lifecycle, `timestamp_a0 += vsyncMode` (3/tick in play), `encounter.introWaitTicks`, vsync-scaled transition constants | 3 | source-verified |
| B4 | Turn system: gauge init players [50, 216] / monsters [0, 216] via retail `simpleRand` (16-bit); selection = bounded (32-round) max-gauge simulation, strict `> 0xd9` trigger, ties last-wins, increment `round(SPEED · U)`; **jitter RNG is L4** (`eae9fb4f6`/GH#2481: retail `simpleRand()/0xffff·0.2+0.9` ∈ [0.9, 1.1] (16-bit) → per-turn-reseeded `legend.core.Random` (`java.util.Random` subclass, `nextFloat`) jitter ∈ [0.9, 1.1) (24-bit) — range/mean near-identical, generator + stream independence differ); seed = `timestamp_a0` re-applied per `getCurrentTurnBent` call; fallback = first alive player; `turnCount_b8` per player turn | 3 (+4 RNG) | source-verified (both sides of the RNG change) |
| B5 | Data model: `BattleStateEf4` combat vars 1–20 + addition/counter/script-param fields + 256-slot asset pool; `CombatantStruct1a8[10]`; `battleFlags` multi-purpose bit table (§5.3); `loadingAudioFiles` bit table | 3 | source-verified |
| B6 | Turn gating: a new turn starts only when no bent has `FLAG_400 \| FLAG_CURRENT_TURN` (`FUN_800c7da8`); forced turns via `FLAG_TAKE_FORCED_TURN` (0x20) — the persistent mechanism — with game var 42 as a direct `forcedTurnBent` assignment that the gate's unconditional flag-scan re-resolution overwrites (§6.3); `BattleEntityTurnEvent` per turn start | 3 | source-verified |
| B7 | Script boundary: battle turn flow lives in the (replaced) player combat script + DRGN1/401 controller + per-enemy scripts; the engine is the 362-slot function table + native formulas | 3 (scripts) + 4 (replacement) | source-verified (retail base-script behavior asset-validated-pending) |
| B8 | 362 active script-function slots in 32-aligned ranges (32–50 camera … 1020–1021), 243 Battle / 111 SEffe / 8 segment-owned; 4 further slot assignments commented out (445/603/617/842); 69 still `FUN_-`named; formulas embedded (`scriptPhysicalAttack` @ 0x800f2500); full contract catalog = M3-06 §3 + meta CSVs | 3 | source-verified (census, matches M3-06) |
| B9 | Opening camera: 50/50 `simpleRand() & 0x8000` monster/player camera; `cameraPosIndices[simpleRand() & 3]`; jump index overridable via game var 44 | 3 | source-verified |
| B10 | Player battle stats = character stats + full equipment aggregation (raw stat sums + bitmask/list accumulation, §8.1); end-of-battle write-back: HP ≥ 1, MP if castable, `status &= 0xc8` persistence mask, repeat-item refund | 3 | source-verified |
| B11 | `checkHit` formula: accuracy (stat or 100 or monster spell accuracy) × hit% vs avoid × avoidMod%; roll `simpleRand()·101>>16` ∈ [0, 100] (16-bit LCG word; hit iff acc > avoid and acc − avoid ≥ roll, i.e. P = (d+1)/101 for 1 ≤ d ≤ 99); always-hit overrides (§8.2) | 3 | source-verified |
| B12 | Magic damage pipeline: HP% spells (flag 0x4, multi/100, targetType 0x8 AoE), attacking/defending element adjustments, power-stat ±½ scaling (sign-dependent, `adjustDamageForPower`), Dragoon-Space element pass, clamp ≥ 0; `formula/` package = data-driven formula storage (05b) | 3 | source-verified (formula objects 05b) |
| B13 | Music: 32-entry ROM tables → battle files 702 (idx 0, 3–15), 707 (idx 1), 722 (idx 2), 712 (idx 16, 20–31), 717 (idx 17), 727 (idx 18), 732 (idx 19, Melbu); victory jingles 696–701 + 732 (Melbu dual-use); `sssqResetStuff` teardown; per-bent/phase/cutscene/deff SFX packages; 24-voice SPU sequencing (M4-03) | 3 | source-verified |
| B14 | Rewards: ROM `enemyRewards_80112868` + mutable `EnemyRewardsEvent`; XP/gold accumulators reset in `initBattle`; drop lists with overflow (`equipmentOverflow`/`itemOverflow`); 192-boundary drop encoding (same as M9-01 D7) | 3 | source-verified (retail reward values asset-validated-pending — from registry content, M8-03) |
| B15 | Exit: post-battle action registry entries (VICTORY/GAME_OVER), 25-tick skip-guard frame machine, `deallocateCombat` teardown order, `postCombatSubmapCut/Scene` reposition, `_800bd7b0 = -4` (M9-03 SM19), `transitionBackFromBattle` vsyncMode 2; battle not savable | 3 | source-verified |
| B16 | `SEffe`: static (private-ctor) effect + addition-spinner system; spinner step/window tables incl. Kongol variant; per-hit completion states; "daddy" in-source naming; driven by scripts | 3 (+4 naming) | source-verified |
| B17 | Non-savable battle (`writeSaveData` null, `readSaveData` empty) — crash recovery targets the last WMap/SMap state (M7-01) | 3 | source-verified |
| B18 | Preserved bugs / workarounds: Divine Dragon Spirit soundIndex-10 OOB skip; GH#1931 stage texture-animation deferral; `scriptStartEncounterSounds`/`scriptStopEncounterSounds` unimplemented (runtime `throw`) | 3 (+4) | source-verified |
| B19 | Encounter model: ≥1 monster, ≤3 unique species (constructor-enforced), stage-data `get/set` indices 1–10 (6–9 writable), `introWaitTicks`, post-combat reposition defaults 0xffff/0xff | 3 | source-verified |
| B20 | Event surface: 6 battle events (table §12.1), all mutable where payload-bearing (M8-02 idiom); registries: encounters/spells/additions/battle actions/post-battle actions/element icons | 3 | source-verified |

## 14. Register and open items

- **Q-70** (battle leg, note only — no status change): battle consumes the retail `simpleRand` LCG (seed 3) at 13 call sites — 7 gameplay (gauge init ×2, `checkHit` roll, drop roll, item roll, opening-camera mode + position index), 1 presentation (battle-report overlay duration roll, `SBtld`), 5 cosmetic (`SEffe` rotation-step sign/draw); the *other* retail LCG `rand()` (seed `0x24040001`) appears at 33 call sites, all presentation/cosmetic (entry-dissolve jitter `SBtld` ×3, `SEffe` effect jitter ×28, effect classes ×2) — no gameplay draw. The reference adds two layer-4 RNGs to battle: the per-turn-reseeded `turnOrderRng` for turn jitter (B4) and the static `seed_800fa754` `legend.core.Random` (mix32-remixed `nextInt`) for L4 effect positions/scales and in-repo item selection (`RecoveryBallItem`/`AttackBallItem`). No `srand` call in the battle path — battle inherits the un-reseeded submap/wmap-seeded stream (Q-70(c) non-reproducibility extends to turn order, hit rolls, and drops).
- **No new questions opened** by 05a. The one genuine gap — the exact behavioral difference between the retail `player_combat_script` base (S_BTLD.OV_ slice) and the in-repo replacement — is *not* registered as a question because the research subject is the reference implementation's behavior (which is fully source-verified through the replacement + native surface); the retail-base comparison is an asset-validated-pending note attached to B7, to be revisited if the 05c script reading surfaces a place where the rewrite diverges in observable behavior.
- Deferred to 05b: `BattleEntity27c` full field map and stat types; the `formula/` formula objects (physical/magic/HP%/stat-mod pipelines, element adjustment tables); deff formats (Lmb/Cmb/Anim, `DeffManager7cc`, `enemyDeffFileIndices`, dragoon deff tables); status bit identities (the `0xc8` mask); SP/MP gain rules; escape mechanics (`escapeChance`); character swap (GH#2882 mechanics).
- Deferred to 05c: additions (spinner interaction, per-character button maps, counter attacks, Starbusts, `AdditionMode`/`MashMode`), battle UI (menus, HUD, floating numbers, `BattleAction` registry, `GatherBattleActionsEvent`), post-battle screens (XP/gold/drop/level-up report, `drawBattleReportOverlays`, `addLevelUpOverlay`), `Monsters.java` (monster stat/behavior tables).
