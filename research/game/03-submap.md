# M9-03 — The Submap: Engine State, Media Pipeline, Movement, Encounters, and Transitions

**Milestone:** M9 (Game Systems: Content and Rules), deliverable 3 of 7 (01 Dabas, 02 title, 03 submap, 04 world map, 05 battle, 06 characters & progression, 07 inventory & items). **Date:** 2026-11-09. **Approved starting point:** `5cbdd65`.

**Scope.** The submap ("smap") is the game's overworld/exploration engine state: the player walks 3D-modeled environments (the "submap model") over a composite 2.5D background, scripted by per-map Fate scripts, with random encounters leading into battle. This document covers: the timing model (§2); the three nested state machines and per-frame orchestration (§3); the asset model and staged media-loading pipeline, including the `NEWROOT.RDT` cut table, the per-cut DRGN file-group layout, and the SMAP-overlay savepoint assets (§4); environment rendering, the depth-cutout model, and the integer-precision camera (§5); the player/sobj model — movement, collision, unstuck budget, rotation smoothing, animation, sobj-sobj collision, attached effects (§6); the encounter system end-to-end (§7); the full transition dispatch (§8); the script-vs-hardcode boundary (RQ7, §9); the save contract (§10); music and audio (§11); and the modding surface (§12).

**Primary sources.**
- Severed-Chains @ `0c08c679b` (pinned, clean): `legend/game/submap/SMap.java` (5,791 lines — the submap engine state; decompiled retail anchors throughout), `legend/game/submap/RetailSubmap.java` (1,994 lines — the retail submap content layer, ROM tables @ `0x8004fa98`–`0x800f74c4`), `legend/game/submap/CollisionGeometry.java`, `legend/game/submap/SubmapObject210.java`, `legend/game/submap/SubmapState.java`, `SubmapEnvState.java`, `SubmapMediaState.java`, `SubmapSavable.java`, `EncounterRateMode.java`, `IndicatorMode.java`, `SubmapEncounterData_04.java`, `Submap.java` (abstract), `MapTransitionData4c.java`, `legend/game/types/NewRootStruct.java` (`getDrgnFile` @ 0x800e6504), `legend/game/EngineStates.java` (`FUN_80020ed8`, `FUN_800218f0` @ 0x800218f0), `legend/game/Scus94491BpeSegment_800b.java` (`_800bd7b0`, `sobjPositions_800bd818`, `submapFullyLoaded_800bd7b4`), `legend/game/Scus94491BpeSegment_8005.java` (`standingInSavePoint_8005a368`), `legend/game/Scus94491BpeSegment.java` (`rand` @ 0x8002d260, `srand` @ 0x8002d270, `resetSubmapToNewGame` @ 0x8002a9c0), `legend/game/DrgnFiles.java`, `legend/game/unpacker/Loader.java` (:125–281), `legend/game/unpacker/Unpacker.java` (:79, :591–624, :715–733, :1383–1404), `legend/game/unpacker/Transformations.java` (:66–107), `legend/game/combat/SBtld.java` (:63–73, :188–230), `legend/game/combat/encounters/Encounter.java`, `legend/lodmod/LodEncounters.java` (512 `LEGACY` entries), `legend/game/modding/events/submap/` (7 event classes), `legend/game/scripting/GameVarParam.java` (:155/:303 game var 67, :199/:347 game var 124), `patches/scripts.csv` (M8-03 §3).
- Cross-referenced documents: M1-02 §5 (timing), M2-01 §6 (DRGN addressing), M2-04 (overlays), M3-05/M3-06 (script VM + native functions), M4-01 (GPU ordering), M4-02 (GTE), M5-01 §8/§10 (LEGACY render mode), M5-04 §widescreen, M6-01 §clock domains, M7-01 (save tags), M8-02 (events), M8-03 (core mods, patches).

**Validation.** All findings are **source-verified** (reference code) unless noted `Inferred`/`Unresolved`. The `@Method` ROM anchors prove the submap state and `RetailSubmap` are decompiled retail code (ROM-provenance leg); exact retail rendering equivalence (e.g. the integer-precision camera's visual identity, cutout depth behavior on hardware) is **asset-validated-pending** (no disc images, no retail bytes). No `asset-validated` claims.

**Numbering.** M9's J/D series continues from M9-01 (J20–J24, D1–D13) and M9-02 (T1–T15, J25–J27): this document adds **SM1–SM20** (submap classification rows, §13) and **J28–J33** (Java-mechanism rows, §13.2).

---

## 1. System overview and layers

The submap is the reference's largest decompiled retail system: a full `EngineState` (`LodEngineStateTypes.SUBMAP`, `SMap.java:445`) running in `RenderMode.LEGACY` (`SMap.java:943–945`) at **60 Hz** with **2 script frames per game tick** (§2). It renders a composite background (2.5D parallax cutouts) with a 3D submap model (TMD) on top, places up to 20 objects ("sobjs" — the player is sobj 0, the remaining slots are scripted objects, §6.1) in the scene, and drives most of its behavior from a per-cut Fate script (the "submap controller") plus one script per sobj (§9).

| Part | Layer | Basis |
|---|---|---|
| `SMap` engine state: timing, state machines, transitions, movement, collision, encounter accumulation | **3, decompiled retail** | `@Method` anchors throughout (e.g. `tick` cases, `mapTransition` @ 0x800e5534, `canEncounter` @ 0x800e4b20) |
| `RetailSubmap` content layer: background compositing, sobj/texture prep, ROM music/encounter tables | **3, decompiled retail** (tables are literal ROM data) | `RetailSubmap.java:120–1955`; tables @ `0x8004fa98`+ |
| Per-cut Fate scripts (controller + sobj scripts) | **3 — retail script data** (disc content, patchable — M8-03 §3) | `SECT/DRGN2x.BIN/<idx+2>/…` |
| SMAP-overlay assets (savepoint model/anims, 13 indicator/effect TIMs) | **3 — retail assets** (embedded in `OVL/SMAP.OV_`, extracted at unpack time) | M2-04 §2.1; §4.3 here |
| Integer-precision submap camera (`GsSetSmapRefView2L`) | **3, decompiled retail** (deliberate integer math to match retail) | Q-20/Q-67(c); §5.2 |
| Async media pipeline (futures over the file loader, tick-polled stage gates) | **4 — reference-specific mechanism** (the staging structure is decompiled, the future/`getLoadingFileCount` gating is the reference's async execution model) | §3.4, J28 |
| Config-driven overrides: encounter-rate modes (incl. the preserved retail abs-bug), indicator modes, widescreen modes, save-blacklist cuts, reduce-motion | **4 — reference-specific** (retail-justified by in-source comments) | §7.2, §6.3, §12.2 |
| `srand((int)System.nanoTime())` at submap entry | **3 call, 4 argument** (decompiled call site; host-clock placeholder argument) | §2.2, Q-70 |
| Save-point model rendering, triangle indicators, chapter save flow | **3, decompiled retail** (rendering constants are ROM data) | §6.7, §8.5 |
| Mod events (7 submap events), encounter registry, config entries | **4 — reference modding surface** (M8-02) | §12 |

The submap is the one M9 system where **script and hardcode are co-equal**: the engine provides the machinery (movement integration, collision, camera, encounter accumulation, transition dispatch, savepoints, indicators) and the per-cut script supplies the policy (where things move, when doors open, when encounters are forced, where the player spawns, what can be saved). The boundary is settled per behavior in §9.4.

---

## 2. Timing model

### 2.1 60 Hz, two script frames per tick (SM1)

`SMap.tick()` `INIT_0` (`SMap.java:4088–4093`, decompiled @ 0x800e5ac4):

```
vsyncMode_8007a3b8 = 1;            // 60 Hz (audit F-01, Q-04)
SCRIPTS.setFramesPerTick(2);       // the script VM runs 2 frames per game tick
srand((int)System.nanoTime());     // see §2.2
resizeDisplay(384, 240);           // 4:3 basis; the GPU model clamps drawing width to 368 (Q-65)
```

The submap therefore runs the game loop at 60 Hz (vsyncMode 1) while the script VM executes **two script frames per tick** — the retail submap script's own timing unit is the 60 Hz frame, and the decompiled code consistently normalizes its timing constants for the current `vsyncMode` with the two expressions **`2 / vsyncMode`** (retail 60-Hz-frame count at the current rate; at vsyncMode 1 this is 2, but the expression generalizes to 30/20 Hz states) and **`3 − vsyncMode`** (the retail-30-Hz unit count; at vsyncMode 1 this is 2). Representative sites:

- Encounter grace period: `smapTicks < 15 × (3 − vsyncMode)` (`SMap.java:3664`) = 30 ticks at 60 Hz.
- Transition minimum: `smapTicks < 3 × (3 − vsyncMode)` (`SMap.java:3973`) = 6 ticks.
- Player movement interpolation length: `interpMovementTicksTotal = 2 / vsyncMode` (`SMap.java:1432`).
- Animation advance: `animateModel(model, 4 / vsyncMode)` (`SMap.java:3037`), save-point ring fade `0.0078f / (3 − vsyncMode)` (`SMap.java:5296`), momentary-indicator window `33 × (2 / vsyncMode)` (`SMap.java:5477`), encounter-disable window `−30 × (3 − vsyncMode)` (`SMap.java:3771`).

`smapTicks_800c6ae0` is the state's own tick counter, incremented once per game tick in `loadAndRenderSubmapModelAndEffects` (`SMap.java:3764`), and reset to 0 when the submap finishes loading and fades in (`SMap.java:4172`).

### 2.2 The submap RNG seed (SM2, Q-70)

`srand((int)System.nanoTime())` (`SMap.java:4091`) is the **only game call site of `srand`** in the reference. The call sits inside the LAB-labeled (decompiled) `INIT_0` case — the submap-load path is original structure — but the argument is a reference-authored host-clock placeholder (the decompilation does not preserve the original seed value). The LCG itself is decompiled retail (`rand` @ 0x8002d260: `seed = seed * 0x41c64e6d + 0x3039; return (seed >>> 16) & 0x7fff` — `Scus94491BpeSegment.java:590–595`; initial seed **`0x24040001`** from `Scus94491BpeSegment.java:585` — M9-04 §16 correction: the "initial seed 3" previously documented here was the seed of the *other* LCG, `simpleRand` (`simpleRandSeed_8004dd44 = 3`, `Scus94491BpeSegment_8004.java:58`, M4-05 §2.3); the two LCGs were conflated).

Consequences, all source-verified:

- Every submap entry re-seeds the gameplay RNG from the host clock, so the reference's `rand()` stream (the 50 call sites documented in Q-70: encounter weighting §7.3, battle accuracy/drops/loot in M9-05) is **non-reproducible across runs**.
- The only in-submap consumer is `randomEncounterIndex()` (`RetailSubmap.java:748–764`) via `rand()`; battle re-uses the same stream after combat return (no re-seed on return — the seed persists from entry).

The design decision for a deterministic reimplementation (seed source) remains **Proposed/M10** (Q-70); this document documents the observed behavior only.

---

## 3. State machines and per-frame orchestration

### 3.1 `SubmapState` — the loading/transition machine (SM13 part)

`SubmapState` (`SubmapState.java`; the numbers are the decompiled case labels) has 18 values; the tick machine (`SMap.tick()`, :4077–4440) drives it:

| State | Label | Effect (abridged) |
|---|---|---|
| `INIT_0` | 0 | Set 60 Hz / 2 frames-per-tick / srand / 384×240; reset encounter accumulator; → `LOAD_NEWROOT_1` |
| `LOAD_NEWROOT_1` | 1 | Queue `\\SUBMAP\\NEWROOT.RDT` + `\\SUBMAP\\savepoint` (directory, §4.3) loads; → `WAIT_FOR_NEWROOT_2` |
| `WAIT_FOR_NEWROOT_2` | 2 | Wait for `Loader.getLoadingFileCount() == 0`; → `LOAD_ENVIRONMENT_3` |
| `LOAD_ENVIRONMENT_3` | 3 | Construct `RetailSubmap(cut, newRoot, screenOffset, collisionGeometry)`; set stage to `WAIT_FOR_ENVIRONMENT` (:4127); queue `submap.loadEnv()`, whose `thenAccept` callback (:4128) jumps to `START_LOADING_MEDIA_10` |
| `WAIT_FOR_ENVIRONMENT` | — | **Dead label**: assigned but never branched on by the tick switch — the environment-load completion is purely callback-driven (the `thenAccept` above); no tick polls this state |
| `CHANGE_SUBMAP_4` | 4 | Warp into another cut/scene (called from `mapTransition` for cut ≥ 2); → `TRANSITION_TO_SUBMAP_17` |
| `START_LOADING_MEDIA_10` | 10 | Transpose WtS → screen-to-world; `prepareEnv()`; restore-or-set player position (§6.3); `initCamera(scene)`; reset submap flags; start media stage 0; → `FINISH_LOADING_AND_FADE_IN_11` |
| `FINISH_LOADING_AND_FADE_IN_11` | 11 | Run media stages until `DONE` and scripts will tick; set player collided primitive; `loadMapTransitionData`; `finishLoading()`; autosave-if-enabled; fade in (10 ticks); reset `smapTicks`; → `WAIT_FOR_FADE_IN` |
| `WAIT_FOR_FADE_IN` | — | Wait for the fade color to reach 0; → `RENDER_SUBMAP_12` |
| `RENDER_SUBMAP_12` | 12 | Steady state; the `general_open_inventory` input action (△ in retail — `Inferred` retail binding; the reference binding is user-editable, `LodMod.java:128`) opens the inventory (`mapTransition(-1, 0x3ff)`, :4207) unless `indicatorsDisabled_4e3` is set |
| `LOAD_MENU_13` | 13 | Fade out (10-tick gate on `mapTransitionTicks`), then run the queued `menuTransition` (char swap / too-many-items / chapter save / shop / inventory); → `RENDER_MENU_14` |
| `RENDER_MENU_14` | 14 | Dispatch on `whichMenu`: `UNLOAD` → 15; `UNLOAD_SAVE_GAME_MENU_20` → re-warp to the chapter destination (the `mapTransition(cut, scene)` from `submapChapterDestinations_800f7e2c[chapterIndex]` — §8.1's 0x3fd row); `QUIT` → `mapTransition(-1, 0x3fb)` (title) + `drgnBinIndex = 1` |
| `UNLOAD_MENU_15` | 15 | Fade in, resume scripts, clear transition lock; → `RENDER_SUBMAP_12` |
| `TRANSITION_TO_SUBMAP_17` | 17 | After the 10-tick fade-out gate: → `LOAD_ENVIRONMENT_3`, with env state `UNLOAD_3` (same-cut return after battle) or `RENDER_AND_UNLOAD_4_5` |
| `TRANSITION_TO_ENGINE_STATE_18` | 18 | After the fade gate: arm `engineStateOnceLoaded` (world map / queued state), env → `RENDER_AND_UNLOAD_4_5`, resume scripts |
| `TRANSITION_TO_COMBAT_19` | 19 | Arm `engineStateOnceLoaded = BATTLE`, env → `RENDER_AND_UNLOAD_4_5`, resume scripts (no fade gate — the battle transition owns its own fade, SBtld) |
| `TRANSITION_TO_TITLE_20` | 20 | After the fade gate: `resetSubmapToNewGame()` (@ 0x8002a9c0: cut 675 / scene 4 — M9-02 T10), arm `TITLE`, env → `RENDER_AND_UNLOAD_4_5` |
| `TRANSITION_TO_FMV_21` | 21 | After the fade gate: `Fmv.playCurrentFmv(fmvIndex, afterFmvState)` (§8.1), env → `RENDER_AND_UNLOAD_4_5`; machine returns to `INIT_0` |

`loadSaveFromMenu` (`SMap.java:529–549`) bypasses the machine from the menu: same-submap save → straight to `RENDER_SUBMAP_12` with `loadingSave = true` and a re-run of `mapTransition(cut, scene)`; different state → `TRANSITION_TO_ENGINE_STATE_18`.

### 3.2 `SubmapMediaState` — the media pipeline stages

`SubmapMediaState.java` (8 stages + `DONE`), executed by `executeSubmapMediaLoadingStage` (`SMap.java:3187–3262`, @ 0x800e13b0), polled once per tick from `loadAndRenderSubmapModelAndEffects` and from `FINISH_LOADING_AND_FADE_IN_11`:

| Stage | Effect |
|---|---|
| `LOAD_SHADOW_AND_RESET_LIGHTING_0` | Upload `shadow.tim`; set the three GsF_LIGHT flat lights (0: white 0.5, 1/2: black, all pointing +Y) via `GsSetFlatLight`; `GTE.setBackgroundColour(0.5, 0.5, 0.5)` |
| `LOAD_MUSIC_1` | Skip if the initial player position is invalid (−1); else `submap.loadMusicAndSounds()` |
| `WAIT_FOR_MUSIC_2` | Wait for `musicLoaded_800bd782` and audio bit 0x2 clear (§4.5) |
| `LOAD_EFFECT_TEXTURES_3` | `reloadSubmapEffects()` mode 0: upload the 11 misc TIMs (§4.4) + a VRAM copy of the savepoint texture (992,288 → 984,288) |
| `LOAD_EFFECT_MODELS_4` | `reloadSubmapEffects()` mode 1: init triangle indicators, savepoint model, attached-sobj effects |
| `LOAD_SOBJ_ASSETS_AND_SCRIPTS_5` | `submap.loadAssets(onLoaded → stage 7)` (§4.2) |
| `WAIT_FOR_SOBJ_ASSETS_AND_SCRIPTS_6` | (pass-through; the `onLoaded` callback jumps to 7) |
| `FINALIZE_SUBMAP_LOADING_7` | `FUN_800218f0()` (title-origin flag re-sets `_800bd7b0 = 9`, EngineStates.java:139–143); post **`SubmapLoadEvent`** (mutable submap script, §12.1); allocate the controller `ScriptState` ("Submap controller", script file 0) and one `ScriptState<SubmapObject210>` per sobj with per-object ticker/renderer/destructor (§6.1); init models from the sobj asset models; set sobj defaults (collision sizes 20/20, reach 50, player collision sizes 20/20, flat light off with RGB 0x80, `cameraAttached = (i == 0)`, `movementStepAccelerationY_18c = 7 / (2 / vsyncMode)`); place sobjs from `sobjPositions_800bd818[i]` (a 24-slot static `SobjPos14` array — `Scus94491BpeSegment_800b.java:86` — filled by scripts during the transition via `MapTransitionData4c`); restore-or-set the player position (§6.3); → `DONE` |

### 3.3 `SubmapEnvState` — the render/unload funnel

`SubmapEnvState.java` (4 values), consumed by `renderEnvironmentAndHandleTransitions` (`SMap.java:1131–1163`, @ 0x8002aae8), which runs **every frame after scripts are ticked** (from `postScriptTick`, :4446):

| Env state | Behavior |
|---|---|
| `RENDER_AND_CHECK_TRANSITIONS_0` | Render the environment, then fall through to the transition check |
| `CHECK_TRANSITIONS_1_2` | (a) If submap flag 0x1 is clear and `canEncounter()` → `submap.prepareEncounter(false)` + `mapTransition(-1, 0)` (random encounter, §7); (b) if scripts ticked this frame and the player's collided primitive carries the transition bit (0x10) → `mapTransition(cut = info >>> 22, scene = (info >>> 16) & 0x3f)` (collision-driven transition, §8.2). The `scriptsTicked` gate is a documented edge-case fix (GH#1195 comment, :1147–1148) |
| `UNLOAD_3` | `unloadSmap()` — deallocate every sobj state (storing pos/rot into `sobjPositions_800bd818` only for live sobjs), deallocate the savepoint, **write `_800bd7b0 = 1`** (:3452), clear all textboxes, mark particles for unload, `submap.unload()`, `submap = null`, reset the effects state machine to −1 (deallocate mode), re-upload `shadow.tim`, destroy the map indicator — then → `CHECK_TRANSITIONS_1_2` |
| `RENDER_AND_UNLOAD_4_5` | Render, then → `UNLOAD_3` |

So the funnel is: steady state renders-and-checks; outgoing transitions render while fading, then unload once, then check again (the unload is one-shot; after it the state sits in `CHECK_TRANSITIONS_1_2` with `submap == null` until the next state machine advances).

### 3.4 Per-frame orchestration and latches (J29, J30)

Per frame at 60 Hz (M1-02 §2–3): `tick()` (the `SubmapState` machine; `INIT_0` runs once) → the script VM executes 2 frames per tick of the controller + all loaded sobj scripts (each script state's ticker — `submapObjectTicker`, :2956–3050, @ 0x800e0ff0 — runs inside the sobj script's frame) → `postScriptTick(scriptsTicked)` (:4442–4466): `renderEnvironmentAndHandleTransitions`, indicator status, the two **offset lerps**, then input reset.

`postScriptTick` lerps two camera-related offsets toward their destinations when their tick counters run out (each is driven by a **latch**: `screenOffsetLatch_800cbd38` / `geomOffsetLatch_800cbd3c`, added in `initCamera` (:4692–4694), ticked in `resetLatches` (:3741–3746), and set to idle once the lerp completes — J30: the latches are frame-batched "hold until the script settles the target" handles so that a script can set an offset target and the engine animates it over the remaining ticks without the script re-asserting it):

- `screenOffset_800cb568` — the render offset added to all submap drawing (VRAM/GPU offset domain, M5-01).
- `geomOffset` — pushed to the GTE as `GTE.setScreenOffset(x, y)` (:4460), i.e. it shifts the world-to-screen transform (the camera "pan").

`loadAndRenderSubmapModelAndEffects` (:3760–3782, @ 0x800e5104) is the per-tick workhorse for every active stage: run the current media stage, run the `MapTransitionData4c` callback on the player sobj (§8.3), increment `smapTicks`, maintain the encounter-disable window (§7.1), `resetLatches()`, `collisionGeometry.tick()`, and `renderSubmap()` — but only when `submapFullyLoaded_800bd7b4 && !loadingSave` (`submapFullyLoaded` is reset every frame by `FUN_80020ed8`, EngineStates.java:96–139, and set by the finish-loading path).

`renderSubmap()` (:3623–3635, @ 0x800e4708): `submap.preDraw()` (pushes the background scissor), render attached-sobj effects (dust/footprints etc. in screen space), `renderSubmapOverlays()` (triangle indicators + savepoint, §6.7), apply the player model's rotation/scale, `submap.draw()` (environment cutouts + sobjs, §5).

### 3.5 The transition-mode global `_800bd7b0` (SM19, closes Q-03)

The Q-03 question — how the submap consumes the transition-mode global `_800bd7b0` — is now fully answered by the consumer census:

| Value | Writer | Meaning |
|---|---|---|
| `2` | `FUN_80020ed8` (EngineStates.java:109) on every engine-state change | normal entry |
| `9` | :113 when the previous state was TITLE; re-asserted by `FUN_800218f0` (:139–143) while the one-shot `_800bd7ac` title-origin flag is set (flag set at :116–117, consumed once) | from the title (new game / continue) |
| `-4` | :118 when previous state was BATTLE (also sets `transitioningFromCombatToSubmap_800bd7b8`) | from battle |
| `3` | :124 when previous state was WORLD_MAP | from the world map |
| `1` | `SMap.unloadSmap` (:3452) | submap unloaded / mid-transition |

**Runtime reads: exactly one** — game variable **67** (`GameVarParam.java:155` read / :303 write). There is no other Java-level consumer in the reference: the value is a **script-visible signal** (retail scripts can branch on "entered from battle / title / world map", e.g. post-combat dialog or first-visit sequences), and the reference's own scripts may read it — which is asset-validated-pending, but the *mechanism* (a script-readable/writable global set at state transitions) is source-verified. Q-03 is closed on this evidence (§14).

---

## 4. Asset model and media pipeline

### 4.1 `NEWROOT.RDT` — the cut table and disc selection (SM3)

`\\SUBMAP\\NEWROOT.RDT` is a disc file in the `SUBMAP/` directory (it is not extracted from any overlay — the reference reads it from the unpacked tree, `SMap.java:4102`); its layout (`NewRootStruct.java:14–47`):

- `[0, 0x2000)` — `SubmapCutInfo submapCutInfo_0000[0x400]`, 8 bytes per cut (two 16-bit packed records).
- `[0x2000, 0x2000 + 0x63c×4)` — `collisionAndTransitions_2000[0x63c]`: the per-collision-primitive packed values consumed by `CollisionGeometry.getCollisionAndTransitionInfo` (bit field documented on the field, NewRootStruct.java:20–31: bit 3 = blocked, bit 4 = has map transition, bits 16–21 = transition scene, bits 22+ = transition cut, bits 8–15 = index into `collisionAndTransitions_800cb460` / 4).

`getDrgnFile(submapCut, drgnIndexOut, fileIndexOut)` (@ 0x800e6504, NewRootStruct.java:40–60) resolves a cut to its on-disc file group:

```
drgnIndex1 = earlyGameFile_00 >>> 13        // 3-bit disc-group field of the early-game record
drgnIndex2 = lateGameFile_02  >>> 13
if (drgnIndex1 == drgnBinIndex_800bc058 − 1 || drgnIndex2 > chapterIndex)
    use the early record      // early file already on the current disc, or late file not yet unlocked
else
    use the late record       // chapter content migrated to a later disc (e.g. Fletz docks, ch. 4)
t0 = (drgnIndexOut in 0..3) ? 4 : 0          // always 4 for 3-bit values
drgnIndexOut++                                // 0→1, 1→2, 2→3, 3→4
fileIndexOut = ((file & 0x1fff) × 3) + t0     // 13-bit index → three-way file-group base
```

The 16-bit records pack a **13-bit file index and a 3-bit disc-group field**. Every path built from the resolved index eventually lands on the **current chapter disc** (`SECT/DRGN{20 + drgnBinIndex_800bc058}.BIN`, i.e. DRGN21–24; the remap rule `arg ≥ 2 → 20 + current` in `DrgnFiles.loadDrgnFile/Dir`, DrgnFiles.java:52–121 — M2-01 §6.4), because each consumer adds a constant that pushes the argument past the remap threshold: the environment/background group loads as `loadDrgnDir(2, fileIndex)` (`RetailSubmap.java:228`), the sobj asset group as `loadDrgnDir(drgnIndex + 2, fileIndex + 1)`, the script group as `loadDrgnDir(drgnIndex + 2, fileIndex + 2)`, and the texture directory explicitly as `SECT/DRGN{20 + drgnIndex}/…/textures` (`RetailSubmap.java:232–238`). The three-way group is therefore **`fileIndex+0` = environment/background MRG, `fileIndex+1` = sobj-asset MRG (+ `textures/` directory), `fileIndex+2` = script MRG** — matching M2-01 §6.4's DRGN21.BIN/402 example (402 = sobj asset group, /402/3 = one script file). The 3-bit field's finer semantics (why four values when the paths all remap) are `Inferred`: it participates only in the early/late selection as written.

`drgnBinIndex_800bc058` is set by `RetailSubmap.loadEnv()` to the submap's resolved group (`RetailSubmap.java:227`), so all subsequent `≥ 2` arguments during that submap's lifetime resolve to the same chapter disc; it is reset to 1 on submap entry (`SMap.java:4238` per Q-04/Q-12) and on the in-submap "quit to title" menu path (`SMap.java:4249`).

### 4.2 Per-cut content layout (SM4)

`RetailSubmap.loadAssets` (:232–310) then:

1. **sobj objects** (`prepareSobjs`, :553–640): `objCount = scripts.size() − 2` — the script MRG's file 0 is the **submap controller script**, files 1..N are the per-sobj scripts. For each sobj `i`: the model is `assets[i×33]` (a **33-entry stride**: entry 0 = model, entries 1–32 = 32 animation slots) — a `CContainer` TMD (M2-05), or `null` when the entry is a virtual (symlinked) file; a virtual model entry re-uses the model of `objects[realFileIndex() / 33]` (the shared-model case, e.g. one model across 33 sobjs — M2-01 §6.2). The 32 animation slots are `TmdAnimationFile`s (M2-07), with a documented retail-bug workaround: an animation entry whose leading byte is `0x11` (a PXL texture — "almost all sobj animations in DRGN24.938 are symlinked to a PXL file", GH#292) is stored as `null`. sobj 16 is special: it always uses the **submap overlay texture** (uvAdjustment `(17, 1008, 256)` — :700–703).
2. **sobj textures**: `textures/` (a plain directory, not an MRG) → `pxls` list; texture *overrides* from `SubmapObjectTextureEvent` (§12.1) replace individual sobj textures with `PNG` uvAdjustments (modern mod surface); slot allocation in `calculateTextureLocations` (:691–727): each distinct TIM claims `width/16` of the 20 TPage slots in a 12-wide grid at VRAM `(576 + slot%12 × 16, 256 + slot/12 × 128)` (M5-01's VRAM layout), wide textures only landing on aligned slots; shared TIMs (via virtual files) occupy one slot.
3. **the 3D overlay model** (the "cut" model — the large foreground 3D piece, e.g. a cliff or building rendered without the camera): only when the resolved group is a chapter disc (drgnIndex 1–4 after increment). `smapFileIndices_800f982c[cut]` gives the overlay's base directory `cutFileIndex` in **DRGN0.BIN** (the shared disc): `loadDrgnDir(0, cutFileIndex)` → `CContainer` + `TmdAnimationFile`; `loadDrgnDir(0, cutFileIndex + 1)` → the overlay TIM + an `MV` matrix. `prepareMap` (:642–665) uploads the TIM to VRAM **(1008, 256)** and pre-multiplies the stored matrix by the **inverse of the world-to-screen matrix** — "the submap cut model is rendered without using the camera matrix, so we multiply its transforms by the inverse of the camera matrix to cancel out the camera multiplication in the shader" (source comment).
4. **Cut 673 special case** ("The End"): `theEnd_800d4bd0 = new TheEndEffectDatab0()`, its TIM from `loadDrgnFile(0, 7610)`, flame CLUT animation initialized after texture upload (:752–755) — the end-credits submap is a retail-internal effect, `Inferred` retail-identical (the anchor is decompiled).

### 4.3 The savepoint assets — `SUBMAP/savepoint` is an MRG directory (SM5; resolves Q-11/Q-50 savepoint leg)

`LOAD_NEWROOT_1` (`SMap.java:4103–4110`) does:

```
loadDir("\\SUBMAP\\savepoint").thenAccept(files -> {
  savepointAnm1      = new AnmFile(files.get(0));
  savepointAnm2      = new AnmFile(files.get(1));
  savepointTmd       = new CContainer("Savepoint", files.get(2));
  savepointAnimation = new TmdAnimationFile(files.get(3));
  attachedSobjEffect.dustTmd       = new CContainer("Dust", files.get(4));
  attachedSobjEffect.dustAnimation = new TmdAnimationFile(files.get(5));
});
```

`loadDir` (`DrgnFiles.java:31–36` → `Loader.loadDirectorySync`, :159–281) resolves `files/SUBMAP/savepoint` on the **unpacked tree** and requires it to be a **directory** (MRG-index mode if a `mrg` file is present, else a plain listing) — `files.get(0..5)` are the first six directory entries.

The only thing that ever creates `files/SUBMAP/savepoint` is the OVL extraction node: `smapAssetExtractor` adds `SUBMAP/savepoint` = the 0x904-byte slice of `OVL/SMAP.OV_` at `0x10694` (`Unpacker.java:720`), queued as an ordinary node. That node's data is transformed by the generic **"Unmrg" leaf transformation** (`Unpacker.java:79`; `mrgDiscriminator` :591–593 = leading `MrgArchive.MAGIC`) → `unmrg` (:595–624), which re-inserts the path as a **directory** with one child per MRG entry plus a `mrg` index file — and it is the *only* transformation that can turn a leaf into a directory (no other transformer references the path; `writeFile` writes only final-tree nodes, :1383–1404, so the write order cannot bypass this). Since the reference reads six files from the directory and works, the slice is (source-forced) an MRG archive with **≥ 6 entries**: the two indicator ANMs (player + door triangle-indicator sprite animations, §6.7), the savepoint TMD, its TmdAnimationFile, and the attached-sobj dust TMD + animation. The byte-level "slice starts with the MRG magic" check is asset-validated-pending, but the mechanism is a forced consequence of the source (the M2-04 §2.1 note "no decompiled consumer found" was an M2 gap — the consumer is this load; corrected in place, §14).

The other 13 OVL TIMs (M2-04 §2.1) are the submap's effect/indicator textures; 11 of them form the ROM's texture-name table `miscTextures_800f9eb0` (`SMap.java:405–417`: `big_arrow.tim`, `small_arrow.tim`, `savepoint.tim`, `800d8520.tim`, `800d85e0.tim`, `savepoint_big_circle.tim`, `dust.tim`, `left_foot.tim`, `right_foot.tim`, `smoke_1.tim`, `smoke_2.tim`) uploaded in `loadMiscTextures` (:5744–5760, @ 0x800f4754) — image + CLUT via `GPU.uploadData15`, TPage/CLUT records captured in `texPages_800d6050`/`cluts_800d6068` — and the remaining two are hardcoded loads: `SUBMAP/alert.tim` (:3346, the `!` alert-indicator texture) and `SUBMAP/darker_shadow.tim` (:2815).

### 4.4 Media pipeline walkthrough

The full entry path for `mapTransition` → new cut, with the future fan-out:

```
mapTransition(cut, scene)            // §8
  → CHANGE_SUBMAP_4 / TRANSITION_TO_SUBMAP_17 (fade out, 10 ticks)
  → LOAD_ENVIRONMENT_3               // new RetailSubmap; loadEnv() → background MRG (fileIndex+0)
  → START_LOADING_MEDIA_10           // prepareEnv, position, camera
  → media stage 0                    // shadow.tim, lights, GTE bg colour
  → media stage 1→2                  // music package + per-submap SFX (§4.5), gated on bit 0x2
  → media stage 3→4                  // 11 misc TIMs, then effect models/indicators
  → media stage 5→7                  // sobj MRG + script MRG + textures dir + overlay MRG (all async),
                                     //   onLoaded → FINALIZE: SubmapLoadEvent, script-state allocation
  → FINISH_LOADING_AND_FADE_IN_11    // stage polling until DONE + SCRIPTS.willTick()
                                     //   loadMapTransitionData, finishLoading, autosave, fade in
  → WAIT_FOR_FADE_IN → RENDER_SUBMAP_12
```

Loading is asynchronous over the file loader's thread pool (`Loader` futures, J28); the stage machine is **tick-polled** (a stage either completes or yields, checked every tick) rather than callback-chained — except the sobj-asset stage, whose `onLoaded` callback jumps straight to stage 7. `WAIT_FOR_NEWROOT_2` gates on the global `Loader.getLoadingFileCount() == 0`.

---

## 5. Environment rendering

### 5.1 Background and depth cutouts (SM6)

`RetailSubmap.drawEnv` (:1213–1437, @ 0x800e7954):

1. **Background**: a single ortho quad (24-bit texture, `QuadBuilder`) covering `backgroundRect` at the **maximum ordering-table z** `((1 << orderingTableBits) − 1) × 4.0f` (= `(0x4000 − 1) × 4` at the 14-bit config — M4-01), positioned with the GPU offset + `submapOffsetX` + the lerped `screenOffset`. The background is the **composite background texture** (`loadBackground`, :784–828, over the fileIndex+0 MRG files — the per-cut 2.5D backdrop).
2. **Cutouts (foreground/background layer textures)**: `envRenderMetrics_800cb710` holds `envBackgroundTextureCount_800cb57c` background-cutout + `envForegroundTextureCount_800cb580` foreground-cutout entries, each an `EnvironmentRenderingMetrics24` with a `zFlags_22` depth-mode field. Each cutout's screen z is computed relative to the sobjs' world-to-screen z (`sobjScreenZs[i]` = WtS transform of the sobj translation, scaled by `1 << (16 − orderingTableBits)`), with three modes documented in the source comment (:1266–1279):
   - **0** — use the stored z;
   - **1** — dynamic: classify each sobj in the cutout's sobj range (`minSobj_800cbd60`..`maxSobj_800cbd64`) by the sign of `cutoutScreenZ + cutoutWorldToScreenZTransformVector · sobjTranslation`; if all sobjs are one way, the cutout z is pinned relative to the min/max sobj screen depth (`maxZ − 50`, clamped ≥ 40 when no positive-z sobjs); if sobjs are on both sides, the cutout averages the sobj depth range when it extends past the stored z, else uses the stored z;
   - **2** — use the lower 14 bits of `zFlags` as the z.
   This is the classic PSX 2.5D trick: flat layer textures interleaved with the 3D model by screen depth, with the dynamic mode making doorways/archways follow the player. `Inferred` retail-identical (the code is decompiled; the hardware OT behavior is M4-01).
3. The submap **cut model** (the 3D overlay, §4.2 step 3) is rendered via `animateAndRenderSubmapModel` with the camera-cancelled matrix.
4. **sobjs** are rendered after the environment (player included), with shadows: the global `shadow.ctmd` blob model (M2-04 §1.1) is rendered under each character (`renderSmapModel` with the shadow texture; `scriptSwapShadowTexture` can switch it).

### 5.2 Camera (SM7)

The camera is a `GsRVIEW2` (`rview2_800bd7e8`, `Scus94491BpeSegment_800b.java:80`) exposed by `SMap.getCamera()` → `submap.getCamera()` (:5776–5778). `RetailSubmap.updateRview2` (:1072–1095) builds it with **`GsSetSmapRefView2L`** — the reference's deliberate **integer-precision** twin of `GsSetSmapRefView2` ("so that sobj and collision placements match retail, as higher precision math was causing noticeable shifts" — the Q-20/Q-67(c) finding: the submap camera must be integer math). The camera viewpoint is initialized by `initCamera(scene)` (:4691–4699, @ 0x800e81a0) to the **middle of the collision primitive** numbered `scene` — i.e. the *scene* in "cut/scene" addressing is a camera/spawn collision-primitive index, not an arbitrary sub-location.

Camera control thereafter has three sources, in priority of update:

1. **Sobj attachment**: an sobj with `cameraAttached_178` (player by default, §3.2) pins the camera viewpoint to its model position every tick — `submapObjectTicker` (:2959–2962) calls `setCameraPos(1, modelTransfer)` when attached; `scriptSelfAttachCameraToSobj`/`scriptAttachCameraToSobj` (functions 117/688) move the attachment to NPCs/objects (cutscene cameras).
2. **Scripts**: `scriptSetCameraPos` (function 119) and the offset pair `scriptGetCameraOffset`/`scriptSetCameraOffset` (:4512–4530, the latter mode-gated by `scriptSetModeParamForNextCallToScriptSetCameraOffsetOrHideSubmapForegroundObject` :4493–4510 — mode < 3 sets the lerp mode for the next set; mode 3 hides a foreground cutout by pushing it to (1024, 1024), "for revealing areas when walking through doors, for example" (source comment)).
3. **`calcGoodScreenOffset`** (:1389–1437): clamps the camera offset so the background stays in frame (the `backgroundRect`/`submapOffset` bounds).

The `screenOffset` (render offset) and `geomOffset` (GTE screen offset) lerps of §3.4 are how script-set camera moves animate; the `shouldRestoreCameraPosition_80052c40` flag (set on battle entry, §7.4) restores the pre-battle camera on return.

### 5.3 Widescreen (SM6 extension)

`SubmapWidescreenMode` (EXPANDED / FORCED_4_3 / STRETCHED, `SubmapWidescreenMode.java`) selects the submap's modern widescreen behavior; the renderer-side counterpart is the LEGACY-mode `expectedWidth = 368` expansion (`RenderBatch.java:143–150`, M5-01 §10 G4 — the second in-repo expression of the 368 constant, Q-65). The decompiled 384×240 basis is the retail one (SM1).

---

## 6. Player and sobj model

### 6.1 Sobj structure and allocation (J29)

Up to **20 sobjs** (`sobjs_800c6880 = new ScriptState[20]`, :234; the actual count is `scripts.size() − 2` per cut). Sobj **0 is the player** (Dart): its model is also aliased to `playerModel_800c6748`, it starts with `flags_190 = 0x1`, `cameraAttached = true`, and its script state is allocated under the name "Submap object 0 (file 0)". Each sobj's state wraps a `SubmapObject210` (0x210-byte struct) carrying: the `Model124 model_00` (the TMD model with its coord2/transforms), `hidden_128`, `sobjIndex_12e/130`, `animIndex_132`, the movement fields (`movementDestination_138`, `movementTicks_144`, `movementStep_148`, `movementType_170` ∈ {0 = lerp-to-point, 1 = arc, 2 = arc-2}), the interpolated movement/rotation state (`interpMovementStart/Dest/Ticks/TicksTotal`, `interpRotationStart*/Dest*/Ticks*/TicksTotal*` for X/Y/Z), `cameraAttached_178`, `flags_190` (bit-packed behaviors, §6.6), the collision metrics (`collisionSizeHorizontal/Vertical_1a0/1a4`, `collisionReach_1b4 = 50`, player-side metrics `_1b8/_1bc/_1c0`), flat-lighting fields (`_1c4.._1c7`), the sobj-sobj collision results (`collidedWithSobjIndex_19c/_1a8`), and the attached-effect data (`attachedEffectData_1d0`).

Per sobj, `SCRIPTS.allocateScriptState(name, obj.constructor.apply(name))` creates the script state; `setTicker(submapObjectTicker)`, `setRenderer(submapObjectRenderer)`, `setDestructor(scriptDestructor)` wire the per-frame ticks; the sobj's script (file i+1) is loaded last. Scripts are the **only** source of sobj movement/visibility/animation changes (see §9.4).

### 6.2 Player movement (SM8)

The player does not move by itself: input is read by the **controller script** (per-cut), which calls **`scriptMovePlayer`** (function 123, :1398–1448, @ 0x800de1d0) with screen-space deltas (shorts, one per axis). The handler:

1. Transforms the delta into world space via the transposed screen-to-world matrix (`transformToWorldspace`, :4708–4718 — identity when the camera has no z-rotation component, i.e. top-down maps pass screen deltas through).
2. Runs `collisionGeometry.checkCollision(isNpc = sobjIndex != 0, playerCoord2, worldDelta, true)` (§6.3).
3. If the collided primitive is **walkable** (bit 0x8 clear — :4468–4472), starts a **2-tick lerp** to the destination (`interpMovementTicksTotal = 2 / vsyncMode`), records the movement for the unstuck budget (§6.3), and stores the collided index on the sobj.
4. Sets `movementType_170 = 0` and caches the movement in `caches_800c68e8.playerPos_00` (consumed by scripts via `scriptGetPlayerMovement`, function 316).

`submapObjectTicker` (:2956–3050, @ 0x800e0ff0) executes the lerp each tick (`interpMovementStart.lerp(dest, (ticks+1)/total, modelTransfer)`), the per-axis rotation lerps, the forced-rotation counter (`rotationFrames_188` / `rotationAmount_17c`), the animation advance (`animateModel(model, 4 / vsyncMode)`), animation-finish looping (flag 0x20000000 → restart anim 0, :3038–3043), and the alert indicator. So movement is **script-driven, engine-integrated, 2-tick-smoothed** — the decompiled equivalent of the original's per-frame script `call MovePlayer` loop.

### 6.3 Collision and the unstuck budget (SM8, SM9)

`CollisionGeometry.checkCollision` (:139+, `CollisionGeometry.java`):

- **NPCs** (`isNpc = true`): pure position+delta ray/box test → `handleCollision` → primitive index or −1; no player state is touched.
- **Player**: gated by the `playerCollisionLatch_800cbe34` — collision runs **at most once per tick** (subsequent calls return the cached movement — the player can be checked by multiple script functions in one frame). On the first call: `playerRunning = (dx² + dz²) > 64.0f` (i.e. speed > 8.0 world units/tick — the walk/run split, reused by the encounter multiplier, §7.2); `handleCollision` sets `collidedPrimitiveIndex_800cbd94`; if the player's facing was not yet locked this tick, the post-collision facing is computed as `floorMod(atan2(dx, cos(rotX) × dz) + π, 2π)` ("cos on X makes rotation calculation use player's local rotation (otherwise rotates when X is non-zero, GH#2316)").

`handleCollision` itself is the primitive-vs-primitive walk test over the submap's collision geometry (vertices/normals/primitives from the environment file group — `CollisionGeometry` fields `verts_04/normals_08/primitiveCount_0c/primitives_10/primitiveInfo_14/vertexInfo_18`); it returns the **index of the primitive the player now rests on** (−1 when blocked). The per-primitive packed info (`getCollisionAndTransitionInfo`, NEWROOT `collisionAndTransitions_2000`): **bit 0x8 = not walkable** (blocks the move), **bit 0x10 = has map transition** (with scene/cut bits, §8.2), bit 0x20 = shop/inn (retail flag; the reference's shop flow is script-driven, §8.4).

**Unstuck budget (SM9).** `unstuckMovementBudget` (initialized to `UNSTUCK_SAVE_LOADED_BUDGET = 60.0f` in `readSaveData`, :436/:491): "player is able to walk through other submap objects if they are already inside of one. This allows the player to walk out of NPCs or chests if they load into a save inside of one" (source comment, :438–441). Every successful player move subtracts `|dx| + |dz|` (:1437–1441); while the budget is exhausted, scripted movement into colliding objects is limited so a save loaded *inside* a model cannot soft-lock the player. M7-01 documents the save-side counterpart (the SMap save blob and the legacy-blob migrator's unstuck budget).

### 6.4 Rotation smoothing (SM8)

`smoothPlayerRotation` (:4720+, @ 0x800ea4c8): when the collision layer updated the player's facing (`playerRotationWasUpdated_800d1a8c` countdown) and the facing change came from movement (`scriptFacePointWasFromPlayerMovement`), the ticker blends the model's yaw toward the post-collision facing — the retail "don't snap, ease" turn behavior; `firstMovement` re-arms the blend after submap load and after forced rotations (:3021–3024).

### 6.5 Sobj scripted movement and animation (SM15 support)

The sobj-side twins of the player functions (672–703 range): `scriptSobjMoveToPosition` (:1528–1563, @ 0x800de7a8 — sets destination + tick count, the ticker lerps), `scriptSobjMoveAlongArc`/`scriptSelfMoveAlongArc2` (circular interpolation with a center + radius + angle delta), `scriptSelfFacePoint`/`scriptSelfFaceCamera` (set the rotation-lerp targets), `scriptSelfSetSobjHidden` (toggle `hidden_128`), the animation control set (`scriptSelfLoadSobjAnimation`, `scriptSelfToggleAnimationDisabled`, `scriptSelfIsAnimationFinished`, `loadInterpolatedSobjAnimation`/`loadUninterpolatedSobjAnimation` — function 696/697: interpolated animations advance through the model's frame latches, uninterpolated jump), and `scriptSelfGetSobjNobj` (script-facing sobj handle). `movementType_170` selects the integration mode in the ticker (0 = point lerp, 1 = arc, 2 = arc-2).

### 6.6 Sobj-sobj collision flags (SM15 support)

`flags_190` bits drive the per-tick sobj-vs-sobj proximity tests (`checkSobjSobjCollisionNoReach`/`WithReach`, :3046–3056): flags `0x800_0000`/`0x200_0000` (no-reach, results in `collidedWithSobjIndex_19c`/`_1a8` via metric slots `0x1000_0000`/`0x400_0000`) and `0x80_0000`/`0x20_0000` (with-reach — the reach is the sobj's `collisionReach_1b4`, default 50) let scripts ask "which sobj am I touching / within reach of" (`scriptGetSobjCollidedWith1/2`, functions 306/308) — the chest/NPC-interaction mechanism (a player "touching" a chest triggers the controller script's text/dialog flow). Metrics are script-settable (`scriptSetSobjCollision1/2`, functions 305/307, and `scriptSetSobjPlayerCollisionMetrics`, function 318).

### 6.7 Attached effects, indicators, savepoint (SM15 support)

**Attached-sobj effects** (`attachedSobjEffect`, the `SubmapEffect` family — ticked in `submapObjectTicker` :3184–3188 and rendered in `renderSubmap` :3625): dust puffs under moving sobjs (the `dust.tim` texture + the `Dust` TMD/ANIM loaded from the savepoint MRG, §4.3), plus the script-allocated effects: smoke plumes (`scriptAllocateSmokePlumeEffectData`/`scriptReinitializeSmokePlumeForIntermittentBursts`, 770/775), smoke clouds (782–784), footprints (`scriptInitFootprints` 771, `scriptSetFootprintsInstantiationInterval` 785, `scriptChangeFootprintsMode` 787 — left/right foot TIMs), snow (`scriptInitSnowParticleData` 776, `snowState_800f9e60`), and the Law Pod trail (`scriptInitLawPodTrail` 786). `handleAndRenderSubmapEffects` (:5683–5700) ticks/renders the global smoke plume + snow + smoke cloud each frame, and the unload path (`unloadSubmapParticles_800c6870`) deallocates them during `UNLOAD_3`.

**Triangle indicators** (the yellow `!` / door arrows): 21 indicator slots (`_800d4ff0[21]` — slot 0 = the player's encounter indicator, slots 1–20 = door indicators, `triangleIndicators_800c69fc` holds their screen positions, :5434–5488 @ 0x800f31bc). `handleTriangleIndicators` computes the player's screen position (a perspective transform 48 px above the player's projected depth, :5459–5472) and, for door indicators, the stored world-projected position adjusted by the screen offset at insertion time (so they track camera moves, :5513–5516). Rendering (:5490–5573, @ 0x800f352c) plays the two indicator ANMs (from the savepoint MRG: `savepointAnm1` = player, `savepointAnm2` = door) as CLUT-animated sprites; the **player indicator's CLUT is colored by encounter proximity** (`getEncounterTriangleColour`, :5576–5590 @ 0x800f3a00: accumulator ≤ 0xa00 → color 0 (green, calm), ≤ 0xf00 → color 1, else color 2 (red, about to fight) — the in-repo "danger meter"). Indicator visibility is gated by the `INDICATOR_MODE_CONFIG` config (OFF/MOMENTARY/ON — MOMENTARY shows for `33 × (2/vsyncMode)` ticks after a script re-enables, :5475–5481), the `INPUT_ACTION_SMAP_TOGGLE_INDICATORS` input toggles OFF⇄ON live (:5450–5457), and `gameState.indicatorsDisabled_4e3` (a save-persisted game flag, §10) hides them. `REDUCE_MOTION_FLASHING_CONFIG` stops the ANM time-advance for accessibility (:5524).

**Savepoint** (the glowing ring where the player can always save): `scriptAddSavePoint` (function 772, :4851–4905, @ 0x800f179c) is called by the controller script with a world position; it projects the position to screen space, builds two ring quads (`savePoint_800d5598[2]` — inner + outer, the outer fading between 0.0 and 0.5 at `0.0077/(3−vsyncMode)` per tick) and 8 "petal" quads (`savePoint_800d5630[32]`, rotating at the per-frame `_800d6c58[i]` degree table, `psxDegToRad` — M4-02), and sets `hasSavePoint_800d5620` + `savePointPos_800d5622`. `renderSavePoint` (:5304–5383, @ 0x800f28d8) renders the `Savepoint` TMD (from the MRG, scaled (1.5, 3.0, 1.5)) plus the quads, and scrolls the savepoint texture in VRAM (the 8×64 strip at (984,288) rotated by `_800f9ea0` via two VRAM copies — :5324–5328). Critically, **standing in a savepoint sets `standingInSavePoint_8005a368`** (script-written; the decompiled original sets it when the player's collided primitive matches the savepoint's — the global is also script-visible as **game var 124**, `GameVarParam.java:199/:347`), which flips `canSave()` to `ALWAYS` (§10).

---

## 7. The encounter system

### 7.1 The gate (SM10)

`canEncounter` (`SMap.java:3662–3700`, @ 0x800e4b20) — called every tick from the `CHECK_TRANSITIONS_1_2` env state when submap flag 0x1 is clear (the flag is the script-side "encounters paused" latch):

1. `smapTicks < 15 × (3 − vsyncMode)` (30-tick post-load grace) → no.
2. Any file still loading (`Loader.getLoadingFileCount() != 0`) → no.
3. `gameState.indicatorsDisabled_4e3` (cutscenes disable indicators *and* encounters) → no.
4. `ticksUntilEncountersAreEnabled_800c6ae4` counts up each tick; while it is negative → no. It is driven to `−30 × (3 − vsyncMode)` (= −60) **every tick while indicators are disabled** (:3770–3772) — i.e. encounters stay off for 60 ticks after a cutscene re-enables indicators. (It is also the script-visible "encounters disabled" control surface, function 265 `scriptGetSetEncountersDisabled`.)
5. The player's collided primitive must have **no special info** (`getCollisionAndTransitionInfo(...) == 0`) — standing on a transition/shop/savepoint primitive suppresses random encounters. (The comment documents the GH#304 fix: after a fast post-combat triangle-escape, the index can still be −1/stale.)
6. The controller script (index 0) must be loaded.
7. `hasPlayerMoved` (:3637–3660, @ 0x800e49f0) must be true — the player's model transfer changed this tick (exact-float compare against `prevPlayerPos_800c6ab0`).

### 7.2 The accumulator and the rate (SM11)

While the gate passes, the player's movement also classifies walk vs run: `distance = mode.modifyDistance(Δx + Δz) × (2 / vsyncMode)`; **`distance < 9.0` → `walkModifier`, else `runModifier`** (:3648–3656), stored in `encounterMultiplier_800c6abc`.

The per-tick accumulation (:3689–3696):

```
limit = 0x1400 (5120)
step  = submap.getEncounterRate() × encounterMultiplier × vsyncMode / 2.0
event = EVENTS.postEvent(new SubmapEncounterAccumulatorEvent(…, accumulator, step, multiplier, vsyncMode, limit, 2.0))
accumulator += event.encounterAccumulatedStep        // mod-mutable (M8-02)
encounter fires iff accumulator > limit
```

`getEncounterRate` (`RetailSubmap.java:513–519`) reads `encounterData_800f64c4[cut].rate_02` and posts **`SubmapEncounterRateEvent`** (mod-mutable). The `EncounterRateMode` config (`EncounterRateMode.java:5–14`) is where the reference exposes the retail behavior and its known bug:

| Mode | distance modifier | walk | run | Notes |
|---|---|---|---|---|
| `RETAIL` | identity (no abs) | 1.0 | 4.0 | "Retail doesn't abs the values so running up/right gave 1/4 the intended encounters" (source comment) — the retail signed-distance bug, preserved |
| `AVERAGE` | abs | 1.0 | 2.5 | "Averages up/right and down/left values when running to give the overall average" |
| `INTENDED` | abs | 1.0 | 4.0 | "The encounter rate the LOD devs intended (a lot)" |
| `NONE` | identity | 0.0 | 0.0 | no encounters |

(The `worldMapWalkModifier/worldMapRunModifier` fields serve the world map — M9-04.) At the submap's 60 Hz with `RETAIL`, walking at 1 unit/tick accumulates `rate × 1.0 × 1 / 2` per tick → an encounter after `5120 / (rate/2)` ticks = `10240/rate` ticks ≈ `170.7/rate` s of walking; the rate field is 0–21 in the ROM table (distinct values {0, 4, 5, 7, 8, 9, 10, 14, 17, 19, 21}; 0 = no encounters on that cut; 189 of the 1024 cuts carry a nonzero rate).

### 7.3 Encounter selection (SM12)

`prepareEncounter(boolean useBattleStage)` (`RetailSubmap.java:535–541`):

```
sceneId   = encounterData_800f64c4[cut].scene_00          // 0–294 (the sceneEncounterIds row)
encounterId = sceneEncounterIds_800f74c4[sceneId][randomEncounterIndex()]
encounter   = REGISTRIES.encounters.getEntry("lod", LodEncounters.LEGACY[encounterId]).get()
prepareEncounter(encounter, useBattleStage)
```

- `encounterData_800f64c4` (:1870–1936, @ 0x800f64c4): 0x400 × `SubmapEncounterData_04{scene_00, rate_02, stage_03}` — per cut: the encounter *scene* (row into the pools), the rate (§7.2), and the default battle stage id.
- `sceneEncounterIds_800f74c4` (:1937–1953, @ 0x800f74c4): **300 rows** × 4 encounter ids (0 = "no encounter" slot) — each encounter *scene*'s four possible random encounters (max id in use: 319, comfortably inside the 512-entry `LEGACY` name table); 189 distinct rows are referenced by nonzero-rate cuts.
- `randomEncounterIndex()` (:748–764, @ 0x800e49a4) uses the gameplay `rand()` (§2.2) with the ROM's weighted table: `rand < 0x2ccc` → 0, `< 0x5999` → 1, `< 0x7333` → 2, else 3 — over the 15-bit range (0x8000) that is **35% / 35% / 20% / 10%** (11468/32768 ≈ 0.35, 22937/32768 ≈ 0.70, 29491/32768 ≈ 0.90).
- The **`Encounter` registry entries** (`LodEncounters.LEGACY[512]`, M8-03: 512 entries) carry the per-encounter content: `musicIndex`, `escapeChance`, `playerOpeningCamera`, `monsterOpeningCamera`, `cameraPosIndices[4]`, **`postCombatSubmapCut`/`postCombatSubmapScene`** (the return destination — the scene is again a camera/spawn primitive index), `introWaitTicks`, and up to 3 unique monsters with world-space spawn positions (`Encounter.Monster`). `prepareEncounter(encounter, useBattleStage)` (:521–533) resolves the battle stage (`useBattleStage ? battleStage_800bb0f4 : data.stage_03`), posts **`SubmapEncounterEvent`** (mod-mutable encounter + stage), and calls `startEncounter` (→ `SBtld.startEncounter`, §7.4). The `encounters` blob from `S_BTLD.OV_` (M2-04 §2.2) is the original's stage table; in the reference the registry is the live source and the blob has no runtime consumer (exposed for tooling) — Q-50's encounters leg remains open on that evidence.

### 7.4 Battle hand-off and return (SM13 support)

Random encounter: `prepareEncounter(false)` + `mapTransition(-1, 0)` → `TRANSITION_TO_COMBAT_19` (no fade gate; `envState = RENDER_AND_UNLOAD_4_5`, arm `engineStateOnceLoaded = BATTLE`). Scripted encounter: `scriptMapTransition(cut = −1, scene = encounterId)` — when `cut == −1 && scene < 0x200`, the handler **pre-loads the named encounter**: `prepareEncounter(LodEncounters.LEGACY[scene], true)` (:4478–4482) — i.e. for a scripted battle the `scene` parameter *is* the encounter id. Either way the battle transition (`SBtld.tickAndRenderTransitionIntoBattle` @ 0x8001b410, :188–230) runs the retail transition-in (Dart zoom, `vsyncMode = 1` at :359), and `Encounter.onBattleWon/onBattleLost` set the post-battle action (`VICTORY` → `playVictoryMusic`; `GAME_OVER` → music package 19 — M8-03 core post-battle actions).

Return: the battle-end code (M9-05) writes `postCombatSubmapCut/Scene` into `submapCut_80052c30/scene_80052c34` and re-enters the SUBMAP state. On re-entry the state machine takes the normal load path, but:

- `mapTransition` recorded `submapCutBeforeBattle_80052c3c = cut` via `storeStateBeforeBattle()` (:544–546) at battle entry; `isReturningToSameMapAfterBattle()` (:549–551) compares it against the new cut — when returning to the *same* cut, `TRANSITION_TO_SUBMAP_17` uses `UNLOAD_3` (skip the render-while-unloading pass) and sets `returnedToSameSubmapAfterBattle_800cb448` (cleared after 15×(3−vsyncMode) ticks, :3983–3985).
- `restoreSubmapAfterBattleOrSetPositionToCollisionPrimitive` (:3703–3720, @ 0x800e4d00) — on a same-map return, the player's **pre-battle position** (`playerPositionBeforeBattle_800bed30`, captured at battle entry :4063) is restored instead of the scene-primitive center; otherwise the scene-primitive middle is used, **rounded to integer coordinates** ("if middle of collision primitive has a fraction, player will have non-zero movement step on submap load (GH#1142)").
- `shouldRestoreCameraPosition_80052c40` (set at battle entry :4064) restores the pre-battle camera.

Defeat path: `Encounter.onBattleLost` → `GAME_OVER` post-battle action → the GAME_OVER state resets the submap to cut 675 (M9-02 T10) — defeat never returns to the submap.

### 7.5 Encounter disable surfaces

Scripts can pause encounters entirely (submap flag 0x1 via the script surface), per-cut (rate 0 in the table), globally per cutscene (`indicatorsDisabled_4e3`), via the config `NONE` mode, or via `SubmapEncounterAccumulatorEvent`/`SubmapEncounterRateEvent`/`SubmapEncounterEvent` (mods). The player indicator color (§6.7) is the only in-world visibility of the accumulator.

---

## 8. Transitions

### 8.1 `mapTransition` dispatch (SM13)

`mapTransition(newCut, newScene)` (`SMap.java:3959–4075`, @ 0x800e5534) — the single transition funnel (Javadoc :3940–3957 documents the special scenes). Guards first: only from `RENDER_SUBMAP_12` or `WAIT_FOR_FADE_IN` (maps that raise screens on load are allowed during the fade-in, :3963); `smapTicks ≥ 3 × (3 − vsyncMode)`; not already transitioning and no audio loading (bits **0x82** of `loadingAudioFiles_800bcf78` — 0x2 = submap SFX, 0x80 = music package; :3977). It then starts the fade-out (`startFadeEffect(1, 10)` — 10-tick fade), sets `transitioning_800f7e4c`, clears `submapFullyLoaded`, and dispatches:

| Condition | Effect |
|---|---|
| `newCut > 0x7ff` | **FMV**: `fmvIndex_800bf0dc = newCut − 0x800` (FMV ids 0–31, the 18-disc-FMV index space, M5-04/M6-03); after-state from `newScene`: 2 = TITLE, 5 = SUBMAP, 6 = BATTLE, 7 = GAME_OVER, 8 = WORLD_MAP (else `IllegalArgumentException`); → `TRANSITION_TO_FMV_21` |
| `0 ≤ newCut < 2` | **World map** (cuts 0/1 are the wmap-exit pseudo-cuts); → `TRANSITION_TO_ENGINE_STATE_18` |
| `newCut ≥ 2` | **Submap warp**: set `submapCut/scene` globals, → `CHANGE_SUBMAP_4`, post **`SubmapWarpEvent`** |
| `newScene == 0x3fa` | char-swap screen (`cacheCharacterSlots` + `CharSwapScreen`) → `LOAD_MENU_13` |
| `newScene == 0x3fb` | **Title** (→ `TRANSITION_TO_TITLE_20`; `resetSubmapToNewGame` inside — M9-02 T10) |
| `newScene == 0x3fc` | "too many items" screen → `LOAD_MENU_13` |
| `newScene == 0x3fd` | **chapter save**: swap the player's collided index with the per-chapter destination (`submapChapterDestinations_800f7e2c[chapterIndex].submapScene/cut` — the "save at end of chapter" destination table), → `LOAD_MENU_13` with `RENDER_SAVE_GAME_MENU_19` (SaveGameScreen); after the save, `UNLOAD_SAVE_GAME_MENU_20` re-warps to the chapter destination (:4231–4236) |
| `newScene == 0x3fe` | **shop**: `ShopScreen(REGISTRIES.shop.getEntry(shopId_8007a3b4))` — the shop id is a global the controller script set beforehand (bit 0x20 of the collision info marks shop/inn primitives, §6.3) → `LOAD_MENU_13` |
| `newScene == 0x3ff` | **inventory** (`Menus::initInventoryMenu`) → `LOAD_MENU_13` |
| `newScene < 0x200` | **battle** (scripted; the encounter was pre-loaded by `scriptMapTransition`, §7.3): if the controller script is loaded, capture `screenOffsetBeforeBattle_800bed50`, `storeStateBeforeBattle()`, `playerPositionBeforeBattle_800bed30`, `shouldRestoreCameraPosition = true`; → `TRANSITION_TO_COMBAT_19` |

The fade-out is 10 ticks (`startFadeEffect(1, 10)`); each outgoing stage gates on `mapTransitionTicks ≤ 10` before proceeding (§3.1). FMVs replace the render callback (M9-02 T8) — the submap machine is left at `INIT_0` for the re-entry after the FMV.

### 8.2 Collision-driven transitions (SM14)

The per-primitive transition encoding (§4.1) is consumed in the env-state funnel (:1150–1156): when the player's collided primitive carries bit 0x10, `mapTransition(info >>> 22, (info >>> 16) & 0x3f)` — the target cut (upper bits) and 6-bit scene are data in NEWROOT, walked into by the player (the retail "walk through a doorway" transition). The `scriptsTicked` condition (GH#1195) prevents a stale index from firing a transition on a frame where scripts did not run. World-map exits are a special case: `loadMapTransitionData` (`MapTransitionData4c`) caches the 16 boundary primitive indices (`worldMapExitPrimitiveIndices_00`) and latches `shouldUpdateCollidedWith_44` false once the player steps onto a boundary, "stops updating the collided index while Dart leaves the map" (source comment) — the wmap hand-off documented in M9-04.

### 8.3 Transition choreography data (J31)

`MapTransitionData4c` (`mapTransitionData_800cab24`) runs its `collidedPrimitiveSetterCallback_48` on the player sobj every tick during active stages (:3766–3768) — it is the per-warp mechanism by which the *old* map's script (still ticking during the fade) keeps setting the player's collided index and sobj positions (into `sobjPositions_800bd818`) until `UNLOAD_3` freezes them; the *new* map's `restoreOrSetPlayerPosition` (:3722–3738, @ 0x800e4d88) then chooses between the scene-primitive center (mode 0), the full pre-battle coord (mode 1, same-map battle return), or the pre-battle transfer only (mode 2, cross-map), and clears the mode.

### 8.4 Script-side transition surface

`scriptMapTransition` (function 256, :4474–4491, @ 0x800e67d4) is the script's only transition call: it returns `CONTINUE` for the menu scenes (0x3fa/0x3fc/0x3fe/0x3ff — the script can keep running under the menu) and `PAUSE_AND_REWIND` otherwise (the script stalls until the new map's script takes over — the M3-05 rewind contract). `scriptOpenShop` (function 791, :5270–5274) is a registry-based shop opener (mod surface; the retail path is the 0x3fe scene with the script-set shop id). `scriptStartFmv` (function 271) is the direct FMV-start variant.

---

## 9. The script-vs-hardcode boundary (RQ7)

### 9.1 The script function surface (SM15)

`SMap.getScriptFunctions()` (:749–940) installs **144 functions** into the 1024-slot native table (M3-06: 592 slots total, 578 with implementations). The submap's slots, by family:

| Slots | Family |
|---|---|
| 12–15, 18–19 (6; 16/17 uninstalled) | character data (`scriptRestoreCharDataVitals` 12, `scriptClearStatusEffects` 13, `scriptCloneCharacterData` 14 — posts `CloneCharacterEvent`, `scriptSetCharAddition` 15, `scriptGetCharAddition` 18, `scriptMaxOutDartDragoon` 19) |
| 96–127 | **self** sobj ops (model/anim load 96–98, animation control 99–100, model pos/rot get/set 101–104, face point 105, hide 106, move-to 107, move-arc 108, check-collision 109, 110/112/114 unnamed decompiled, ignore-collision 111, texture anim 113/115, movement type 116, camera attach/is-attached 117/118, set camera pos 119, rotate sobj (relative/absolute) 120/121, arc-2 122, **move player** 123, face player 124, 125 unnamed, current submap id 126, sobj nobj 127) |
| 198 | sobj textboxes (`scriptAddSobjTextbox` @ 0x80025218 — the chest/dialog textbox system, packed mode flags, the "?Cannot carry any more" chest-full special case) |
| 256–271 | map control (**map transition** 256, camera offset get/set 257/258, collision+transition info 259, 260 unnamed, block/unblock collision primitive 261/262, set env foreground position 263, camera-offset mode / hide foreground object 264, encounters-disabled get/set 265, env overlay depth mode+Z 266, collision primitive pos 267, 268/269/270 unnamed, **start FMV** 271) |
| 288–319 | sobj presentation + collision + indicators (hide/show model part 288/289, face camera 290, scale XYZ/uniform 291/292, model Z offset 293, sobj flag set/get 294/295, triangle indicators 296–300, 301/302 unnamed, sobj flat light 303/304, sobj collision set/get 305–308, ambient colour 309/310, shadow enable/disable/size/offset 311–314, check player collision 315, get player movement 316, swap shadow texture 317, player collision metrics 318, 319 unnamed) |
| 672–703 | **other-sobj** ops (the 96–127 twins with an explicit index: model/anim 672–676, face 677, hide 678, move 679/680, collision 681/682, reset CLUT anims 683, texture anim 684–686, movement type 687, camera attach 688, arc-2 689, nobj 690, model parts 691/692, face camera 693, flags 694/695, interpolated/uninterpolated anim load 696/697, ignore collision 698, chapter title card 699–703) |
| 768–791 | effects + savepoint + shop (sobj dust 768/769, smoke plume 770, footprints 771, **add savepoint** 772, ortho dust 773, 774 unnamed, intermittent smoke 775, snow 776, 777 unnamed, 2D/offset triangle indicators 778–781, smoke cloud 782–784, footprint interval 785, Law Pod trail 786/789, footprint mode 787, 788 unnamed, unused smoke 790, **open shop** 791) |
| 940 | `scriptSetSobjUsePs1Depth` (the AVSZ3/AVSZ4 override surface — M5-02/Q-67) |

### 9.2 The script layer

Per cut, the script MRG (fileIndex+2) provides **file 0 = the submap controller script** (allocated as its own `ScriptState`, §3.2) and **files 1..N = one script per sobj** (the sobj script's "self" is the sobj's `SubmapObject210`). The controller script is the map's policy layer: it reads input (via the engine's input actions, M1-05), calls `MovePlayer` (123) every frame with the resulting deltas, runs the map's logic (doors, cutscenes, forced encounters, textboxes 198, savepoints 772, indicators 296–300, FMVs 271, transitions 256). Scripts can read/write game vars (the M3-06 table — including var 67 = `_800bd7b0`, §3.5, and var 124 = `standingInSavePoint`, §6.7), allocate/latch, and block on file loads (`scriptWaitForFilesToLoad`, DrgnFiles.java:113–126). The patchable-script surface is the in-repo `patches/scripts.csv` (353 entries: 351 diff + 2 replacement; submap scripts under `SECT/DRGN21.BIN/…` — M8-03 §3) — retail scripts are disc content, moddable by recompile-patch, which is why the engine-side function set is the stable contract.

### 9.3 The hardcode layer

Everything in §2 (timing), §3 (the state machines, latches, fades), §4 (asset resolution, NEWROOT, media staging), §5 (environment rendering, cutout depth, camera construction), §6.2–6.4 (movement integration, collision, unstuck, rotation smoothing), §7 (gate, accumulator, weighting, hand-off), and §8 (dispatch, collision-driven transitions) is **engine hardcode** — decompiled retail. The script supplies none of the mechanics; it supplies *when and where* they fire.

### 9.4 Boundary verdict table (RQ7 for the submap)

| Behavior | Where | Boundary |
|---|---|---|
| Frame rate, tick structure, script frames-per-tick | engine | hardcode (SM1) |
| RNG seed at entry | engine | hardcode call, layer-4 argument (SM2/Q-70) |
| Map/cut/scene addressing, disc selection | engine + NEWROOT data | hardcode over data (SM3) |
| Background/cutout composition, depth modes | engine + per-cut metrics data | hardcode over data (SM6) |
| Camera construction + integer precision | engine | hardcode (SM7) |
| Camera *movement* (cutscene cams, offsets, sobj attach) | scripts (119/257/258/117/263/264/266) | script policy, engine lerp integration |
| Player walking | scripts call 123 every frame; engine integrates + collides + lerps | co-equal (SM8) |
| NPC/chest/object motion | scripts (107/108/679/680/120/121/122) | script policy, engine integration |
| Collisions, walkability, transitions-on-primitives | engine + NEWROOT bit data | hardcode over data (SM8/SM14) |
| Soft-lock prevention (unstuck budget) | engine | hardcode (SM9) |
| Random encounters (gate, accumulation, selection, weighting) | engine + ROM tables | hardcode over data (SM10–12) |
| Forced/scripted encounters | scripts (256 with cut=−1, scene=encounterId) | script policy over engine machinery |
| Encounters pausing (cutscenes, flags, config, mods) | script flag + engine gate + config + events | layered (SM10/§7.5) |
| Battle hand-off/return choreography | engine (SBtld, M9-05) | hardcode |
| Doorway/exit transitions | engine, data-driven (NEWROOT bits) | hardcode over data (SM14) |
| FMV playback from submaps | scripts (271 / 256 with cut > 0x7ff) + engine machine | script policy, engine machinery |
| Menus from submaps (char swap/shop/inv/too-many/chapter save) | scripts (256 with 0x3fa–0x3ff, 791) | script policy (M9-07 owns the screens) |
| Savepoints | script 772 places; engine renders + `standingInSavePoint` → canSave | co-equal |
| Triangle indicators | scripts add (296–300, 778–781); engine ticks/renders/colors | co-equal |
| Textboxes/dialog | scripts (198 + the text system, M5-03) | script policy, engine rendering |
| Music selection | engine tables + flag-gated overrides + chapter state | hardcode over data (§11) |
| What can be saved, where | engine (`canSave`: savepoint global / blacklist / config) + save tags | hardcode + config (§10) |

**Verdict.** Unlike the title (M9-02 T15: zero script) or Dabas (M9-01 D12: zero script), the submap is the **maximally partitioned** M9 system: the engine owns all mechanics and the per-cut scripts own all policy, with the partition line drawn exactly at "which primitive, when, where, for whom". The decompiled ROM anchors prove the partition is retail's own (the `@Method` functions are the ROM's native script table, M3-06), and the 353-entry patch corpus confirms the script side is the modding seam.

---

## 10. The save contract

### 10.1 Save tags (SM17)

`writeSaveData`/`readSaveData` (`SMap.java:462–522`): the SMap contributes a `MapTag` with `scene` (int), `cut` (int), `indicatorsDisabled` (bool — the script-controlled cutscene flag is save-persistent), `primaryPartyBackup` (int list — the party-order backup for the char-swap system, M9-06), and — "we need wmap stuff for when we're sailing the Queen Fury, go inside, and save" (source comment, :483) — the world-map sailing state: `pathIndex`, `dotIndex`, `dotOffset`, `facing`, `directionalPathIndex`. On read, `unstuckMovementBudget` is reset to 60.0 (§6.3) and a legacy-save fallback (no `scene` tag) restores cut/scene from the flat `GameState52c` fields (`submapScene_a4`/`submapCut_a8` — the M7-01 V1–V10 migration chain). The WMap/SMap-only savable-state model and the legacy-blob migrator (WMAP 0x3156 / SMAP 0x3256) are M7-01; this document fixes the SMap side's field set.

### 10.2 Savability (SM17)

`SMap.canSave` (:551–555) delegates to `submap.canSave()` → `RetailSubmap.canSave` (:448–457): **`ALWAYS` when `standingInSavePoint_8005a368`** (the savepoint global, §6.7), **`NEVER` when the cut is in `saveBlacklist`** — a hardcoded list of softlock-prone cuts with in-source retail-bug justifications ("Neet inner", "Entering wingly forest as Meru — Guaraha disappears and trying to exit softlocks", "Psyche Bomb trials entry — saving on the other side of the bridge before the bridge is there…", "Hero Competition waiting room / arena", :427–446 — layer-4 protection over decompiled maps), else **`SAVE_ANYWHERE`** (honoring the `SAVE_ANYWHERE_CONFIG` config — M7-02). `autoSaveAfterBattlePending` (set in `init()` :449–456 when both `SAVE_ANYWHERE_CONFIG` and `AUTO_SAVE_AFTER_BATTLE_CONFIG` are on and the previous state was BATTLE) triggers `autoSaveIfEnabled()` at finish-loading (:4168) — the reference's crash-safety auto-save (M7-01 §crash-recovery).

### 10.3 Loading

`loadSaveFromMenu` (:529–549, §3.1) plus `restoreMusicAfterMenu` → `submap.startMusic()`; the load path resets `encounterAccumulator_800c6ae0 = 0` and re-arms the music. The `loadingSave` flag suppresses `renderSubmap` during the load-in (:3778–3780) so the fade-in shows the save's position cleanly.

---

## 11. Music and audio

### 11.1 Music selection (SM18)

`startMusic` (`RetailSubmap.java:370–385`) calls `getSubmapMusicChange` (:1488–1560, @ 0x8001c60c), which resolves a music-package index with a three-tier precedence:

1. **`_8004fb00`** (flag-gated per-cut overrides, :1606–1737): entries `{submapId, musicIndex, cut…}`; a cut matches only if its bit is set in `gameState_800babc8._1a4[submapMusicIndex >>> 5] & (1 << (submapMusicIndex & 0x1f))` — a story-progress bit per table entry (e.g. the same cut plays a different song before/after a plot beat). Submap **57** (the opening: Rose intro / Dart forest / horses) is special-cased: its first entry (music −1, cuts 675–677) requires `gameState._1a4[0] & 0x1` clear (the intro not yet done).
2. **`_8004fa98`** (unflagged per-cut overrides, :1591–1605): 13 entries — direct submapId+cut → music.
3. **Base**: `getCurrentSubmapMusic` (:1576–1590, @ 0x8001c874) — submap **56** (Moon) uses its own cut table `moonMusic_8004ff10` (:1738–1784, 43 entries — the moon's per-cut song changes); all other submaps use `submapMusic_80050068[submapId]` (:120–130 — an 8×8 table, **64 entries** for submap ids 0–63 → music package id, −1 = silence). The base table is 64-wide while `cutToSubmap` values reach 800: the only entries producing a submap id ≥ 64 are **cuts 0/1** (→ 800/610) — the world-map-exit pseudo-cuts that `mapTransition` dispatches to `WORLD_MAP` before any submap load, so they never reach this index with the shipped data (an uncovered id ≥ 64 would be an AIOOBE — a retail data invariant, `Inferred` from table inspection).

The result passes through `FUN_8001c84c` (:1563–1573, @ 0x8001c84c) — a change detector: returns the new index if it differs from the current song id, −1 if both are −1 (silence → silence), else **−2** ("start the current sequence again"). `startMusic` then: −1 → `stopCurrentMusicSequence`, −2 → `startCurrentMusicSequence` (retrigger), −3 → do nothing (music already playing, `AUDIO_THREAD.isMusicPlaying()` check :1556–1558), else → `loadMusicPackage(index)` (M6-01: 5-file packages, async). `loadMusicAndSounds` (:312–330) first maps cut → submap id via `cutToSubmap_800d610c` (:1785–1845 — the **794-entry** cut→submap-id table, 61 distinct submaps; the `submapId_800bd808` global is also the Discord-location name source, :5756–5760, and the per-submap SFX index) and, on a submap change, re-loads the per-submap SFX set.

### 11.2 Per-submap SFX

`loadSubmapSounds(submapIndex)` (:332–347): loads `DRGN0.BIN/{5750 + submapIndex}` (a 5-file MRG: id, sample data, indices, **SSHD**, soundbank — M6-02 §4) into the submap `SoundFile` at sequence volume **0x7f**; the audio bit **0x2** (`loadingAudioFiles_800bcf78`) marks the load and gates both the media pipeline (§3.2) and transitions (§8.1). The submap SFX (footsteps, door creaks, chest clanks) are played by the scripts through the SFX sequencer (M6-02) — the decompiled `sssqResetStuff()` calls at submap `init`/`destroy` (:451/:459) reset the SFX sequencer state on entry/exit.

---

## 12. The modding surface

### 12.1 Events (J32)

Seven submap events (M8-02 family table; `legend/game/modding/events/submap/`):

| Event | Posted | Mutable payload |
|---|---|---|
| `SubmapLoadEvent` | `FINALIZE_SUBMAP_LOADING_7` (:3299) | **the submap controller script** (`loadedEvent.submapScript` — a mod can swap the whole map script), gameState, submap, objects |
| `SubmapWarpEvent` | `mapTransition` submap-warp branch (:4041) | target cut |
| `SubmapEncounterEvent` | `prepareEncounter(encounter, …)` (:528) | **encounter + battle stage id** (the mod can redirect any encounter) |
| `SubmapEncounterAccumulatorEvent` | `canEncounter` per tick (:3694) | the accumulated step + limit (the mod can scale/suppress accumulation) |
| `SubmapEncounterRateEvent` | `getEncounterRate` (:515) | the base rate |
| `SubmapEnvironmentTextureEvent` | `prepareEnv`-side env texture prep | environment texture overrides (analogous to the object one) |
| `SubmapObjectTextureEvent` | `prepareSobjs` → `loadTextureOverrides` (:628–630) | per-sobj texture overrides (PNG uvAdjustments, §4.2) |

### 12.2 Config entries

`ENCOUNTER_RATE_CONFIG` (the §7.2 modes), `INDICATOR_MODE_CONFIG` (OFF/MOMENTARY/ON + the live toggle input), `REDUCE_MOTION_FLASHING_CONFIG` (:5524), `SAVE_ANYWHERE_CONFIG` + `AUTO_SAVE_AFTER_BATTLE_CONFIG` (§10.2), `SubmapWidescreenMode` (§5.3), and the global `Config.combatStage` debug override of the battle stage (:531–532). All are M7-02 registry entries (the 88-entry census, M8-03 re-verified).

### 12.3 Registries

The `encounters` registry (M8-03: 512 `lod` entries) is the content seam for encounter composition (§7.3); the `shop` registry for shops (:4069/:5272); the `engineStateTypes` registry for the submap state itself (`LodEngineStateTypes.SUBMAP`). Character/addition functions (12–19) are the seams into M9-06; the item/textbox functions into M9-07.

---

## 13. Classification

### 13.1 Submap rows (SM1–SM20)

| # | Finding | Layer | Confidence |
|---|---|---|---|
| SM1 | 60 Hz (`vsyncMode = 1`) + 2 script frames/tick; all decompiled timing constants normalized by `2/vsyncMode` and `3 − vsyncMode`; display basis 384×240 | 3 (decompiled) | source-verified |
| SM2 | `srand((int)System.nanoTime())` at submap entry: decompiled call site, layer-4 host-clock argument; the only `srand` game call site; re-seeds the gameplay LCG (LCG @ 0x8002d260, initial seed **`0x24040001`**, `Scus94491BpeSegment.java:585` — M9-04 §16 correction) per entry → non-reproducible encounter/battle streams | 3 call / 4 argument | source-verified (Q-70) |
| SM3 | `NEWROOT.RDT`: `SubmapCutInfo[0x400]` @ 0 (two 16-bit packed records = 13-bit file index + 3-bit disc group) + `collisionAndTransitions[0x63c]` @ 0x2000; `getDrgnFile` @ 0x800e6504 early/late selection by chapter; file group = `idx×3 + 4` base, +0 env / +1 sobj assets / +2 scripts; all paths resolve to the current chapter disc (DRGN21–24) | 3 (data) | source-verified (3-bit semantics beyond selection Inferred) |
| SM4 | Per-cut layout: 33-entry sobj stride (model + 32 anims), virtual-model sharing via `realFileIndex()/33`, the DRGN24.932 PXL-animation retail bug workaround (GH#292), sobj 16 = overlay texture, the DRGN0-based 3D overlay model with the camera-cancelled matrix, cut 673 "The End" special case | 3 (data) | source-verified |
| SM5 | `SUBMAP/savepoint` = the 0x904-byte SMAP.OV_ slice at 0x10694, **expanded at unpack time by the generic Unmrg transformation into a 6+-entry MRG directory** (2 indicator ANMs, savepoint TMD, savepoint anim, dust TMD, dust anim); consumed by `SMap.LOAD_NEWROOT_1` (:4103–4110); the 13 OVL TIMs = 11-table misc textures + alert + darker-shadow | 3 (assets) | source-verified (byte-level MRG magic asset-validated-pending); **closes the Q-11/Q-50 savepoint leg** |
| SM6 | Environment = composite background quad at max OT z + foreground/background cutouts with the three documented depth modes (stored z / sobj-relative dynamic / 14-bit zFlags) + the camera-cancelled 3D cut model + TMD sobjs with shadow blobs; widescreen via `SubmapWidescreenMode` (368-expected-width renderer path) | 3 (decompiled) | source-verified |
| SM7 | Camera = `GsRVIEW2` built with the integer-precision `GsSetSmapRefView2L` (Q-20/Q-67(c)); initialized to the scene primitive's center; pinned to sobj 0 (or an attached sobj); script-moved via pos/offset/lerp surfaces; `shouldRestoreCameraPosition` across battles | 3 (decompiled) | source-verified (hardware identity asset-validated-pending) |
| SM8 | Player movement = script `MovePlayer` (123) with screen-space deltas → world transform → once-per-tick collision → walkability (bit 0x8) → 2-tick lerp; run threshold 8.0 units/tick (64.0 squared); facing via `atan2(dx, cos(rotX)·dz)` (GH#2316) | 3 (decompiled) | source-verified |
| SM9 | Unstuck budget: 60.0 world units of post-save-load movement allowance so a save loaded inside a model can be walked out of; decremented by `|dx|+|dz|` per successful move | 3 (decompiled) + 4 (the constant's tuning) | source-verified |
| SM10 | Encounter gate: 30-tick grace, no file loads, indicators not disabled, ≥60-tick re-enable window after cutscenes, collided primitive info == 0, controller script loaded, player moved this tick | 3 (decompiled) | source-verified |
| SM11 | Accumulator: limit 0x1400, step = `rate × walk/run × vsyncMode / 2.0`, walk/run split at 9.0 distance units; `EncounterRateMode.RETAIL` preserves the documented retail no-abs bug ("running up/right gave 1/4 the intended encounters"); AVERAGE/INTENDED/NONE are layer-4 | 3 (decompiled + tables) / 4 (modes) | source-verified |
| SM12 | Selection: cut → `{scene, rate, stage}` (ROM table @ 0x800f64c4) → 300×4 id pools (@ 0x800f74c4) → weighted index 35/35/20/10 (0x2ccc/0x5999/0x7333 over 15 bits) → 512-entry registry; `SubmapEncounterEvent` mutability; the S_BTLD `encounters` blob is the original table with no runtime consumer (Q-50 leg) | 3 (decompiled + data) | source-verified |
| SM13 | `mapTransition` dispatch: FMV (cut > 0x7ff, after-state 2/5/6/7/8), wmap (cut 0–1), submap warp (cut ≥ 2), battle (scene < 0x200), menus/specials (0x3fa–0x3ff); 10-tick fades; audio-loading gate (bits 0x82); transition lock; `MapTransitionData4c` choreography | 3 (decompiled) | source-verified |
| SM14 | Collision-driven transitions: NEWROOT bit 0x10 + scene bits 16–21 + cut bits 22+, fired only on script-ticked frames (GH#1195); wmap-exit boundary latching (`MapTransitionData4c`) | 3 (decompiled + data) | source-verified |
| SM15 | 144 native script functions (slot map §9.1); controller script = file 0, per-sobj scripts = files 1..N; 20-sobj capacity, sobj 0 = player; sobj-sobj collision flags; attached effects (dust/smoke/snow/footprints/Law Pod) | 3 (decompiled table + scripts) | source-verified (patch corpus M8-03) |
| SM16 | Script-patch surface: 353 entries (351 diff + 2 replacement), submap scripts under `SECT/DRGN21.BIN`; the patch pipeline is M8-03 §3 (CRC-gated, restore-on-removal) | 4 (tooling) over 3 (scripts) | source-verified |
| SM17 | Save contract: `scene/cut/indicatorsDisabled/primaryPartyBackup/pathIndex/dotIndex/dotOffset/facing/directionalPathIndex` tags; unstuck budget reset on load; `canSave` = ALWAYS (savepoint) / NEVER (blacklist cuts, retail-bug-justified) / SAVE_ANYWHERE (config); post-battle auto-save | 3 (decompiled) + 4 (blacklist, autosave) | source-verified |
| SM18 | Music: 3-tier resolution (flag-gated `_8004fb00` with per-entry story bits in `gameState._1a4`, unflagged `_8004fa98`, base `submapMusic_80050068` [64 ids] with Moon's `moonMusic_8004ff10` and the submap-57 intro special case); `FUN_8001c84c` change detection (−1 stop / −2 retrigger / −3 keep); per-submap SFX from DRGN0/{5750+id} at volume 0x7f, gated by audio bit 0x2 | 3 (decompiled + tables) | source-verified |
| SM19 | `_800bd7b0` transition-mode global: 2/9/−4/3 written by `FUN_80020ed8` on state change (+ one-shot `_800bd7ac` title flag re-asserting 9 via `FUN_800218f0`), 1 written on `unloadSmap`; the sole runtime read is game var 67 (script-visible) — **closes Q-03** | 3 (decompiled) | source-verified (retail script usage of var 67 asset-validated-pending) |
| SM20 | Savepoint system: script-placed (772), engine-rendered (TMD + 2 ring quads + 8 rotating petals + scrolling VRAM strip), `standingInSavePoint_8005a368` (game var 124) → `canSave = ALWAYS`; triangle indicators: 21 slots, player indicator CLUT-colored by accumulator (≤0xa00 / ≤0xf00 / else), indicator-mode config with live toggle, reduce-motion gate | 3 (decompiled) | source-verified |

### 13.2 Java-mechanism rows (J28–J33)

| # | Mechanism | Notes |
|---|---|---|
| J28 | **Async media pipeline**: `CompletableFuture` fan-out over the file loader's pool for the NEWROOT pair, the background MRG, the sobj/script/texture groups, and the overlay MRG; stage machine tick-polled (yield-until-condition) with one callback exception (`loadAssets.onLoaded`); global `Loader.getLoadingFileCount()` as the cross-stage gate; `onFileLoadingException` swallows per-future failures into logged nulls (a corrupt file degrades, e.g. `prepareSobjs` `exceptionally → null`, rather than crashes the load) | A Rust port needs the same *staging contract* (what must be resident before each stage) without the Java future idioms (Research Principle 2) |
| J29 | **Per-sobj script states**: one `ScriptState<SubmapObject210>` per sobj (20 max) with registered ticker/renderer/destructor, plus the controller state; sobj models are allocated per cut from the 33-stride asset MRG and deallocated in `UNLOAD_3` (positions persisted to the 24-slot `sobjPositions_800bd818` static for the re-entry path) | The sobj count is data-driven (`scripts.size() − 2`), the array is fixed at 20 |
| J30 | **Latches**: `screenOffsetLatch_800cbd38`/`geomOffsetLatch_800cbd3c` are per-frame deferred-target handles (ticked in `resetLatches`, cleared when the lerp completes) so a script-set camera target animates without per-frame re-assertion | `addLatch`/`clearLatches` (:4693–4706) |
| J31 | **`MapTransitionData4c`**: per-warp choreography record (16 wmap-exit primitive indices, the collided-index latch, and the per-tick setter callback run on the player sobj) — the mechanism that keeps the outgoing map's script authoritative over the player index during the fade, and latches it off at the wmap boundary | Source-commented in `MapTransitionData4c.java` |
| J32 | **Event posting at the content seams**: `SubmapLoadEvent` (script swap) at finalize, `SubmapWarpEvent` at warp, the three encounter events at their computation points, the two texture-override events at prep — exact-class dispatch, NORMAL priority (M8-02) | The encounter events make the entire encounter pipeline mod-mutable without patching |
| J33 | **`Submap` abstraction**: `SMap` holds a `Submap` (abstract) — `RetailSubmap` is the only in-repo implementation; the interface (loadEnv/loadAssets/loadMusicAndSounds/startMusic/loadMapTransitionData/prepareEnv/prepareSobjModel/finishLoading/preDraw/draw/drawEnv/unload/calcGoodScreenOffset/getEncounterRate/prepareEncounter/storeStateBeforeBattle/isReturningToSameMapAfterBattle/canSave/getCamera) is the modding seam for alternate submap renderings (e.g. a fully-modded map) | No second implementation exists in-repo; the seam is exercised only by `RetailSubmap` |

---

## 14. Register updates

- **Q-03 — closed (→ this document §3.5, SM19).** The consumer side is fully enumerated: `_800bd7b0` is written by `FUN_80020ed8` (2/9/−4/3 on state change) and by `SMap.unloadSmap` (= 1); its only runtime read is game var 67 (`GameVarParam.java:155/:303`) — it is a script-visible transition-origin signal, not a Java-consumed mode. The one-shot title flag `_800bd7ac` → `FUN_800218f0` re-asserts 9. No other consumers exist in the reference.
- **Q-11 — advanced (SMAP.OV_ leg resolved, §4.3, SM5).** The `SUBMAP/savepoint` 0x904 slice is the 6+-entry MRG directory consumed by `SMap.LOAD_NEWROOT_1`; all 13 TIMs' consumers were already documented (M2-04 §2.1) and re-verified here. The lod_engine exhaustiveness question (the Q-11 title) is unchanged — still explicitly unestablished (M2-04 §4).
- **Q-50 — advanced (savepoint leg closed, encounters leg open, §4.3/§7.3, SM5/SM12).** `SUBMAP/savepoint` now has a documented unpack-time expansion and a runtime consumer. The `encounters` blob (S_BTLD 0x68d8+0x7000) remains consumer-free at runtime — the 512-entry `LodEncounters.LEGACY` registry is the live source of encounter content (M8-03); the blob is retained in the tree for tooling. Whether the original read the same table from `S_BTLD.OV_` at battle start (vs. it being a decompilation-time extraction aid) is asset-validated-pending.
- **Q-70 — advanced (§2.2, SM2).** M9-03 documents the call site's context (submap `INIT_0`, the only `srand` game call site, the 15-bit LCG, the per-entry re-seed consequence for the 50 `rand()` consumers, and the fact that battle return does *not* re-seed). The seed-source design decision for a deterministic reimplementation remains Proposed/M10 as the register states; no original-behavior evidence for the seed argument exists in-repo.
- **M2-04 correction (in place):** `research/assets/04-original-executable-overlay.md` §2.1 — the `SUBMAP/savepoint` row's "no decompiled consumer found" note is corrected to point at the Unmrg expansion + `SMap.java:4103–4110` consumer (this document §4.3). The row's size/offset data are unchanged.
- **No new questions opened.** The items deliberately left open (the byte-level MRG magic of the savepoint slice; retail script usage of game var 67; the S_BTLD blob's original reader; the 3-bit disc-group field's finer semantics) are all asset-validated-pending legs of existing entries, not new questions.

---

## 15. Cross-references

- M1-02 §5 (vsyncMode census — submap 60 Hz), §2–3 (per-frame ordering the §3.4 orchestration plugs into).
- M2-01 §6 (DRGN addressing, the 402/3 example, `drgnBinIndex_800bc058`), §6.2 (virtual files — the sobj model sharing).
- M2-04 (SMAP.OV_ inventory — §4.3 corrects §2.1 in place; S_BTLD — §7.3).
- M2-05 (CContainer), M2-06 (TIM), M2-07 (TmdAnimationFile), M2-09 (FMV index space for the §8.1 FMV branch).
- M3-05 (script VM — `PAUSE_AND_REWIND` in §8.4, `FlowControl`), M3-06 (native function table — the 144-slot install, game vars 67/124/25).
- M4-01 (ordering table 0x4000/14 bits — the background max-z and sobj screen-z scaling), M4-02 (GTE — `GsF_LIGHT` flat lights, `psxDegToRad` petal table, the integer-precision camera twin).
- M5-01 (LEGACY render mode + the 368 expansion, the VRAM TPage grid at (576,256)/(1008,256)), M5-03 (textbox system — function 198), M5-04 (FMV takeover — §8.1).
- M6-01 (audio bit domain 0x2/0x80, `loadMusicPackage`), M6-02 (SSHD/soundbank — §11.2, SFX sequencer reset).
- M7-01 (save tags and the SMap legacy blob 0x3256 — §10), M7-02 (the config entries of §12.2).
- M8-02 (event contract — §12.1), M8-03 (core-mod censuses: 512 encounters, 41 shops; the patch corpus — §9.2/§12.3).
- M9-02 (title — the 0x3fb return path, `resetSubmapToNewGame`, GAME_OVER), M9-04 (world map — the cut 0/1 exits, the sailing save fields, the wmap encounter-rate modes' world-map fields), M9-05 (battle — SBtld, the encounter stage, post-combat return, the `rand()` consumers), M9-06 (characters — functions 12–19, the char-swap menu 0x3fa), M9-07 (inventory — 0x3fe/0x3ff, the shops registry, textboxes).
