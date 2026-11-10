# M9-04 — The World Map: Engine State, Path Network, Navigation, Encounters, and Special Travel

**Milestone:** M9 (Game Systems: Content and Rules), deliverable 4 of 7 (01 Dabas, 02 title, 03 submap, 04 world map, 05 battle, 06 characters & progression, 07 inventory & items). **Date:** 2026-11-09. **Approved starting point:** `5cbdd65`.

**Scope.** The world map ("wmap") is the continent-scale engine state the player enters from submaps: a 3D continent model with an MCQ background, on which the player (Dart) travels along a fixed network of geometric paths between named locations, entering submaps (towns, dungeons), ships the Coolon between destinations, rides the teleport sphere, and crosses the Barrier Stations between regions. This document covers: the timing model (§2); the entry/exit hand-off contract with the submap (§3); the complete geography data model with table censuses and invariants (§4); the per-location availability-gating model built on `wmapFlags` and the 49 destination markers (§5); the state machines and per-tick orchestration (§6); navigation — dot-walk movement, the location prompt, labels, and the path-start validation model (§7); world-map encounters end-to-end (§8); special travel: Coolon warp, teleport sphere, Queen Fury, Barrier Stations (§9); camera and rendering, including weather effects (§10); music and sound (§11); the save contract (§12); the script-vs-hardcode boundary (RQ7, §13); the modding surface (§14); and the classification/mechanism tables (§15).

**Primary sources.**
- Severed-Chains @ `0c08c679b` (pinned, clean): `legend/game/wmap/WMap.java` (6,245 lines — the world-map engine state; decompiled retail anchors throughout), `legend/game/wmap/WmapStatics.java` (1,883 lines — the ROM world-map tables @ `0x800ef000`–`0x800f7f74`), `legend/game/wmap/MapState100.java`, `WMapCameraAndLights19c0.java`, `WMapModelAndAnimData258.java`, `Location14.java`, `Place0c.java`, `DirectionalPathSegmentData08.java`, `WMapDestinationMarker2c.java`, `CoolonWarpDestination20.java`, `TeleportationLocation0c.java`, `WaterAnimation20.java`, `Continent.java`, `WmapState.java`, `MapMarker.java`, `legend/game/submap/RetailSubmap.java` (:367–417 world-map-exit detection, :1959–1975 `submapWorldMapExits_800f7f74`), `legend/game/submap/SMap.java` (:3990–4010 `mapTransition` world-map branch), `legend/game/submap/EncounterRateMode.java`, `legend/game/EngineStates.java` (:44–75 `loadGameStateOverlay`, :96–139 `FUN_80020ed8`), `legend/game/EngineState.java` (:117–119 default `RenderMode.PERSPECTIVE`), `legend/game/Scus94491BpeSegment.java` (:284–320 `scriptSetGlobalFlag1` @ 0x80017390, :585 `randSeed = 0x24040001`), `legend/game/Scus94491BpeSegment_8004.java` (:58 `simpleRandSeed_8004dd44 = 3`), `legend/game/Scus94491BpeSegment_800b.java` (`submapCut_80052c30`, `collidedPrimitiveIndex_80052c38`), `legend/game/types/GameState52c.java` (:50–53 the flag arrays, :26 hero-ticket comment), `legend/game/types/Flags.java` (the 32-bits-per-word packed model), `legend/game/sound/Audio.java` (:590–695 `unloadSoundFile`, :775–790 `loadWmapMusic` @ 0x8001f708), `legend/game/combat/SBtld.java` (:63–73 `startEncounter`), `legend/lodmod/LodEncounters.java` (512 `LEGACY` entries), `legend/game/modding/events/worldmap/` (world-map events), `legend/game/modding/events/gamestate/EncounterEvent.java`, `legend/game/scripting/GameVarParam.java` (:187/:335 game var 112), `legend/game/scripting/GameVarArrayParam.java` (:48/:96), `legend/game/debugger/GameStateViewerController.java` (:355–362 wmapFlags display), `patches/scripts.csv` (zero wmap entries).
- Cross-referenced documents: M1-02 §5 (timing), M2-01 §6 (DRGN addressing), M3-05/M3-06 (script VM, functions 2/3 = set/read global flag 1), M6-01 (music packages, `unloadSoundFile` slots), M7-01 (save tags, `lastSavableEngineState`), M7-02 (config entries: `EncounterRateMode`, `RUN_BY_DEFAULT`), M8-02 (event contract), M8-03 (core mods: encounter census, `scripts.csv`), M9-03 §2.2 (submap RNG — corrected here), §7 (submap encounters), §8 (transition dispatch).

**Validation.** All findings are **source-verified** (reference code) unless noted `Inferred`/`Unresolved`. The `@Method` ROM anchors on `WMap` and the literal ROM tables in `WmapStatics`/`RetailSubmap` establish the decompiled-retail provenance of the system; exact retail rendering identity (continent TMDs, MCQ backgrounds, Coolon fly animation) is **asset-validated-pending** (no disc images, no retail bytes). The retail *script* policies that drive location gating (which script flag unlocks which destination) are not in-repo (disc content); the *mechanism* is source-verified, the specific flag→destination assignments are asset-validated-pending. No `asset-validated` claims.

**Numbering.** M9's J series continues from M9-01 (J20–J24), M9-02 (J25–J27), and M9-03 (J28–J33): this document adds **W1–W16** (world-map classification rows, §15.1) and **J34–J38** (Java-mechanism rows, §15.2).

---

## 1. System overview and layers

The world map is `LodEngineStateTypes.WORLD_MAP`, implemented by `WMap` (`WMap.java:201`, `class WMap extends EngineState<WMap>`). It renders in **`RenderMode.PERSPECTIVE`** — `WMap` does not override `EngineState.getRenderMode()` (`EngineState.java:117–119`), in contrast to the submap's `RenderMode.LEGACY` (M9-03 §1). It runs at the reference's 60 fps display with **3 logic ticks per rendered frame** (`tickMultiplier() = 3`, `WMap.java:464–467`) and `vsyncMode_8007a3b8 = 1` while playing (§2). Discord rich presence reports the state `"Exploring"` (`WMap.java:6236–6239`).

The world model is a **static path graph over a 3D continent landscape**:

- **66 geometric paths** (`pathSegmentLengths_800f5810`, 67 entries = 66 lengths + terminal `-1`) define **794 position "dots"** (`pathDotPosArr_800f591c`, 66 arrays of `Vector3f`).
- **132 directed segments** (`directionalPathSegmentData_800f2248`, 133 entries = 132 + a terminal `0`-terminator whose scan defines `directionalPathCount_0c` at entry, `WMap.java:4690–4696`) reference those paths by `|pathSegmentIndexAndDirection| - 1`, with sign = travel direction.
- **256 location nodes** (`locations_800f0e34`) sit at path endpoints (or are pathless); each carries a place reference, submap cut/scene both directions, continent, and effect flags.
- One **continent TMD + MCQ background** is loaded per continent (8 continents, `Continent.java`), plus the player avatar (Dart, ship, Coolon, teleport sphere — 4 models, `playerAvatarVramSlots_800ef694 = {15, 16, 20, 21}`).

| Part | Layer | Basis |
|---|---|---|
| `WMap` engine state: timing, state machines, navigation, encounters, travel, camera, weather | **3, decompiled retail** | `@Method` anchors throughout (e.g. `initFlagsPathsCutsAndPlaces` @ 0x800e78c0, `handleEncounters` @ 0x800e3704, `transitionToEngineState` @ 0x800cce1c) |
| `WmapStatics` geography tables (locations, places, segments, dots, markers, exits) | **3, decompiled retail** (literal ROM data) | `WmapStatics.java:55–1883`; `@Method`-anchored consumers |
| `submapWorldMapExits_800f7f74` (104-entry submap→wmap exit table) | **3, decompiled retail** (ROM table mirrored in `RetailSubmap`) | M9-03 §8.2; §3.2 here |
| Async media pipeline (futures over the file loader, tick-polled stage gates, `Loader.getLoadingFileCount`) | **4 — reference-specific mechanism** (the staging structure is decompiled, the future/gating machinery is the reference's async execution model) | J36 |
| Config-driven overrides: `EncounterRateMode` (world-map walk/run modifiers), `RUN_BY_DEFAULT` | **4 — reference-specific** (the rate fields are reference config; retail had fixed rates) | §8, W7 |
| Location gating via `wmapFlags` + 49 destination markers (mechanism) | **3, decompiled retail** | §5; the *retail script assignments* (which flag unlocks which destination) are disc content — asset-validated-pending |
| Mod events (`WorldMapEncounterEvent`), encounter registry, config entries | **4 — reference modding surface** (M8-02) | §14 |
| Weather effects (snow, smoke, clouds), marker/label VRAM art, input action bindings | **3 constants / 4 bindings** (art is retail VRAM data; bindings are the reference input registry) | §10, §7.3 |

The world map is the M9 system **least touched by script**: it has no per-map Fate script, ticks no script frames of its own (§2), and `patches/scripts.csv` contains **zero** wmap entries (M8-03 §3). All of its *mechanism* is hardcode over ROM data; its *policy* (which locations are unlocked, when the Coolon arrives, when the Queen Fury becomes enterable) is driven through the global flag arrays that retail scripts set (§5, §13).

---

## 2. Timing model (W1)

**3 ticks per rendered frame; vsyncMode 1 in play.** `WMap.tickMultiplier()` returns **3** (`WMap.java:464–467`) — the engine state's `tick()` runs 3 times per rendered frame (M1-02 §5). `vsyncMode_8007a3b8` is set to **1** in `initWmap` (`:681`) and `restoreMapOnExitMenu_` (`:707`), and to **2** only on the hand-off frames: `transitionToEngineState` (`:797`) and `transitionToTitle` (`:830`).

**The speed-constant idiom.** Tick thresholds and per-tick increments are written as retail constants normalized by the tick rate, in two forms seen in the decompilation:

- `constant / (3.0f / vsyncMode_8007a3b8)` — e.g. the teleport animation's 96-tick duration (`:3242–3246`), the scale ramp `tick * 0.015625f / (3.0f / vsyncMode)` (`:3250`), the menu fade's 45-tick duration (`:665`);
- `constant / vsyncMode_8007a3b8` — e.g. the location-entry delay `9.0f / vsyncMode` (`WAIT_7` case, :3385–3390).

With `vsyncMode = 1` both forms reduce to the retail tick counts; the `(3.0f / vsyncMode)` form expresses "retail ticks per frame" so a 2×-speed state (if `vsyncMode` were 3, as in the submap's script framing, M9-03 §2.1) would scale correctly. `WMap` never sets `vsyncMode > 1` during play.

**The entry wait gate (`loadWait`).** `WmapStatics.loadWait` (a *mutable static*, `WmapStatics.java:1879`, initial `60 / vsyncMode`) is reset to `60 / vsyncMode` when the `WorldMapState` machine is `UNINITIALIZED_0` (`:934`) and to `60` in the deallocator path (`:869`). The `WAIT_FOR_MUSIC_TO_LOAD` state proceeds only when **both** the music package has finished loading (`(getLoadedAudioFiles() & 0x80) == 0`, `:713–717`) **and** `loadWait-- > 30 / vsyncMode` has fallen (`:944`) — i.e. a minimum of 30 ticks (0.5 s at 60 Hz) elapses even if the music loads instantly.

**No script frames.** `EngineStates.loadGameStateOverlay` sets `SCRIPTS.setFramesPerTick(1)` on every state change (`EngineStates.java:56`), and `WMap.java` contains **zero** `SCRIPTS.` calls (verified by census) — the world map runs no Fate script at all. (§13.)

---

## 3. Entry and exit: the hand-off contract (W2)

### 3.1 Entry

The world map is entered through exactly one engine path: `SMap.mapTransition` with target cut in `[0, 1]` — `engineStateToTransitionTo = WORLD_MAP` (M9-03 §8.1, `SMap.java:4005–4009`). The transition carries **no explicit payload**: the two global variables `submapCut_80052c30` and `collidedPrimitiveIndex_80052c38` (`Scus94491BpeSegment_800b`) retain the *exiting submap's* cut and scene/primitive, and the world map resolves the player's spawn from them. Producers of the (cut, scene) pair:

1. **Collision-driven world-map exits** (the common case): `RetailSubmap` caches, per cut, the exit primitive indices from the 104-entry `submapWorldMapExits_800f7f74` table (§4.7); when the player's collided primitive matches one, the transition fires with `collidedPrimitiveIndex_80052c38` = the primitive walked into (M9-03 §8.2).
2. **Fast-travel station exits** (named (cut, scene) pairs recognized at wmap entry, `:4833–4850`): the Wingly station submaps exit with direction-encoding scenes — (528, 13) "Zenebatos to Mayfil (first time)", (528, 14) "Zenebatos to Aglis", (528, 15) "Zenebatos to Mayfil", (540, 19) "Mayfil to Zenebatos", (572, 23) "Aglis to Zenebatos" — which set `fastTravelTransitionMode = TELEPORT_1`; and the Zenebatos Coolon port submap (529, 41) sets `OPEN_COOLON_MAP_2` + `usingCoolonFromZenebatos_254` and marks the location visited.
3. **Barrier-station region travel** (§9.4): *wmap-internal* — `SET_DEST_9` writes the pseudo-cut 999 and the chosen region into the two globals and the world map re-initializes in place (no submap is entered); the `(999, R)` pair is resolved by the next `initFlagsPathsCutsAndPlaces`.

At entry, after the spawn resolution below, the same code region recognizes the **Queen Fury Donau departure** context (submapCutFrom 242, sceneFrom 3): `scriptFlags2[0x8f]` (143) → first-time departure forced movement (`queenFuryForceMovementMode = WALK_1`, input disabled), `scriptFlags2[0x90]` (144) → later departures (`shortForceMovementMode = WALK_1`, input disabled) (`:4808–4822`, in-source comments).

`WMap.init()` is minimal — `super.init()` plus `lastSavableEngineState = this.type` (`:433–436`, M7-01) — after which the `WmapState` machine (INIT → WAIT_FOR_MUSIC_TO_LOAD → INIT2 → LOAD_BACKGROUND_OBJ → PLAY, §6) runs. During INIT2, `initFlagsPathsCutsAndPlaces` (`:4630`, @ 0x800e78c0) performs the spawn resolution:

1. **Destination-flag expansion** (§5.2): for each of the 49 real destination markers, if `scriptFlags2_bc[marker.packedFlag]` is set, the marker's 8-word (256-bit) bitmap is written into `wmapFlags_15c`.
2. **Source resolution**: `submapCutFrom_c4 = submapCut_80052c30`, `submapSceneFrom_c6 = collidedPrimitiveIndex_80052c38`. The **debug room** (cut 0, scene 0) is remapped to Hellena (cut 13, scene 17) by an explicit source comment "If Debug Room Floor 1" (`:4657–4661`).
3. **Location lookup**: a linear scan of the 256 locations for `(submapCutFrom, submapSceneFrom)` equality (`:4663–4674`). (For Barrier re-entries the pair is the pseudo-cut 999 + region, §9.4 — the barrier locations 100/101/102 carry `(submapCutFrom, submapSceneFrom) = (999, R)` for R = 0/1/2, so the scan resolves them directly.)
4. **Fallback**: if no location matches, **or** the matched location's `wmapFlags` bit is clear, the source is replaced with Hellena (13, 17) and `locationIndex = 5` (location 5 = the Hellena spawn location, `:4677–4682`). (Short-circuit evaluation keeps the out-of-range `locationIndex = 0x100` case safe: when `locationExists` is false the flag bit is never read.)

**Spawn placement** (tail of `initFlagsPathsCutsAndPlaces`, `:4700–4760+`):

- `transitionFromCombatOrShip = (previousEngineState == BATTLE && submapCutFrom != 999) || (submapSceneFrom == 31 && submapCutFrom == 279)` — the second disjunct is the **Queen Fury disembarkation** (cut 279, scene 31; §9.3).
- If `(!transitionFromCombatOrShip && !loadingNewGameState_800bdc34) || reinitializingWmap_80052c6c` (the **submap-exit or wmap re-initialization** case): `shortForceMovementMode_d4 = WALK_1` and `disableInput_d0 = true` — the player is dropped at the path start and walks in under forced movement (§7.1).
- Otherwise (the **title/save-load/combat-return/sailing** case): the path state is restored directly from the `GameState52c` fields `directionalPathIndex_4de / pathIndex_4d8 / dotIndex_4da / dotOffset_4dc / facing_4dd` (§12), input is enabled, and a linear scan finds the location on the restored `directionalPathIndex` whose `checkLocationIsValidAndOptionallySetPathStart(i, -1, null) == 0` (`:4724–4736`).

### 3.2 Exit

`WMap` exposes one internal setter, `transitionToEngineState(EngineStateType)` (`:805–808`, sets `engineStateToTransitionTo` + `wmapState = TRANSITION_TO_ENGINE_STATE`), reached from:

| Exit | Trigger | Target | Payload written |
|---|---|---|---|
| Location entry (§7.2) | location prompt "Enter" confirmed | SUBMAP | `SET_DEST_9`: `submapCut = location.submapCutTo`, `submapScene = location.submapSceneTo` — except the Barrier branch (§9.4), which re-enters the wmap in place |
| Barrier region travel (§9.4) | Barrier prompt region confirmed | **WORLD_MAP (re-init)** | `submapCut = 999` (pseudo-cut), `collidedPrimitiveIndex = chosen region` → `TRANSITION_TO_WORLD_MAP` |
| Coolon (§9.1) | flight completes / Zenebatos map closes | SUBMAP (Zenebatos town, dest 8; Zenebatos departures) or **WORLD_MAP (re-init)** (other arrivals) | `INIT_DEST_7`: dest 8 → destination's `cutTo/sceneTo`; else → destination's `cutFrom/sceneFrom` + `COOLON_ARRIVAL_3`; Zenebatos close → current location's `cutTo/sceneTo` + `OPEN_COOLON_MAP_2` |
| Teleport (§9.2) | sphere flight completes | SUBMAP (destination capital: 528 Zenebatos / 539 Mayfil / 572 Aglis) | `INIT_FADE_2`: `submapCut = target.cutTo`, `submapScene = target.sceneTo` |
| Queen Fury boarding (§9.3) | Coolon button at the QF dock (flag-gated) | SUBMAP (cut 279, scene 31) | `locations_800f0e34[93].cutTo/sceneTo` (`:5015–5018`) |
| Battle (§8) | encounter accumulator fires | BATTLE | none (the wmap's own fade machine is not used; battle return re-enters the wmap via the §3.1 combat-return branch) |
| Quit | main-menu QUIT | TITLE | `resetSubmapToNewGame()` called before the hand-off (`:823–831`) — same reset as the GAME_OVER path (M9-02 T10) |

**The fade-end funnel.** Most of these exits run the `tickFadeOutTransition` machine first (§6.3): fade to black (`startFadeEffect(1, 30)`), the map rotation to `INIT_SUBMAP_ZOOM_1`/`SUBMAP_ZOOM_2`, the MCQ colour fade (`mcqColour_800c6794 -= 0.125f / (3.0f / vsyncMode)`, 90/`vsyncMode` ticks), then the map texture brightness 1→0 (`0.5f / (3.0f / vsyncMode)` per tick, 6 ticks). When the brightness reaches 0, the funnel decides (`:3979–4006`): `submapCut == 999` (Barrier) → `TRANSITION_TO_WORLD_MAP`; otherwise → `engineStateToTransitionTo = SUBMAP` + `TRANSITION_TO_ENGINE_STATE` — with the fast-travel overrides `OPEN_COOLON_MAP_2` → SUBMAP and `COOLON_ARRIVAL_3` → `TRANSITION_TO_WORLD_MAP`.

`transitionToEngineState()` (`:782–798`, @ 0x800cce1c) is the shared epilogue: it copies `mapState`'s path fields into the `GameState52c` save fields (`:783–787`), renders one last frame, `deallocate()s`, and queues `engineStateOnceLoaded_8004dd24 = engineStateToTransitionTo` with `engineStateData` (this state's `writeSaveData` tag, §12) — consumed by `EngineStates.loadQueuedOverlay` → `loadGameStateOverlay` on the next frame (M9-02 T3). `transitionToWorldMap()` (`:800–810`, @ 0x800cce9c) is the **same-state re-initialization** path (deallocate, `reinitializingWmap = true`, back to INIT) used for the Barrier region travel and Coolon arrivals that land on the world map.

---

## 4. Geography data model (W4)

All tables are `WmapStatics` statics mirroring ROM data (ROM addresses in the field names). Censuses below were computed by parsing the table literals (verification script run against `WmapStatics.java` @ `0c08c679b`).

### 4.1 Continents (`Continent.java`)

8 real continents + `NONE_8`: `SOUTH_SERDIO_0`, `NORTH_SERDIO_1`, `TIBEROA_2`, `ILLISA_BAY_3`, `MILLE_SESEAU_4`, `GLORIANO_5`, `DEATH_FRONTIER_6`, `ENDINESS_7`. Each carries a `continentNum` used for `continentIndex_800bf0b0` (camera/model selection, `:4701`). The location table's continent census over all 256 entries: SOUTH_SERDIO 37, NORTH_SERDIO 22, TIBEROA 15, ILLISA_BAY 16, MILLE_SESEAU 18, GLORIANO 4, DEATH_FRONTIER 5, ENDINESS 38, `NONE_8` 101.

### 4.2 Locations (`locations_800f0e34`, `Location14`, 256 entries)

Fields: `directionalPathIndex_00` (−1 = pathless), `placeIndex_02`, `submapCutFrom_04`, `submapSceneFrom_06`, `submapCutTo_08`, `submapSceneTo_0a`, `unknownIndex_0c`, `continent_0e`, `thumbnailShouldUseFullBrightness_10` (bool), `effectFlags` (int).

Census (256 entries):

- **155 have a directional path** (`directionalPathIndex ≥ 0`, max 131); **101 are pathless** (−1) — including all `NONE_8`-continent entries (101; the last non-`NONE_8` entry is index 158).
- `thumbnailShouldUseFullBrightness`: **62 true / 194 false** (§7.2 brightness rule).
- `effectFlags` census: `{0: 200, 4: 2, 16: 30, 32: 14, 64: 4, 128: 3, 136: 2, 192: 1}`.
- **Special locations:**
  - **5** — the Hellena spawn/fallback: `(dp 2, place 3, cutFrom 696/13, cutTo 696/13)`; the §3.1 fallback target.
  - **85** — Queen Fury island (ENDINESS): `(dp 105, place 78, cutFrom 994/0, cutTo 994/50)`; arrival from the QF-island submap 994 (exit `(994, 0)`, §4.7).
  - **93** — Queen Fury boarding: `(dp −1, place 86, cut 279/31 both directions, NONE_8)`; the §9.3 boarding target.
  - **100/101/102** — the **Barrier Stations**: `(31, 93, 999/0, 999/18)`, `(45, 94, 999/1, 999/2)`, `(50, 95, 999/2, 999/1)` — `cutFrom == cutTo == 999` on all three (bidirectional endpoints, §9.4), all full-brightness, `effectFlags 0`.
  - **72–77** (`0x48`–`0x4d`) — the **teleport sphere** locations (§4.6).

### 4.3 Places (`places_800f0234`, `Place0c`, 249 entries)

Fields: `name_00` (String or null), `fileIndex_03`, `servicesFlag_04` (5-bit service mask), `soundIndices_08[4]`. Census: **78 named** (indices 0–95), **171 null-named**; 54 of the named places carry a nonzero `servicesFlag`. Names are multi-line (`\n`-separated) display strings, e.g. `"Commercial\nTown of Lohan"`. Places **70** ("Mille\nSeineau\nCastle") and **74** ("The\nBarrier") are named but **referenced by no location** (dead name entries — the 241 distinct `placeIndex` values referenced by locations span 0–248). The barrier stations all use place 93–95 named `"Barrier\nStation"` with `fileIndex 35` and no services. The 5 service names are `services_800f01cc = {"Save Point", "Hotel", "Clinic", "Weapon Shop", "Item Shop"}` (§7.2).

### 4.4 Directional path segments (`directionalPathSegmentData_800f2248`, `DirectionalPathSegmentData08`, 133 entries)

Fields (5): `pathSegmentIndexAndDirection_00`, `encounterRate_03`, `battleStage_04`, `encounterIndex_05`, `modelIndex_06`. The table holds **132 nonzero entries + one terminal entry with `pathSegmentIndexAndDirection_00 == 0` at index 132**; the entry scan in `initFlagsPathsCutsAndPlaces` (`:4690–4696`) counts up to that terminator and stores `directionalPathCount_0c = 132`.

The javadoc fixes the index encoding: *"Path segment index (actually path index + 1 because 0 can't have a sign). The index can be positive or negative depending on Dart's direction."* — hence the consumer idiom `pathIndex = |indexAndDirection| - 1` (e.g. `initDataForReversePath`, `:5458`), which is why 132 directed segments map onto 66 geometric paths (max `|index|` = 66).

Census (133 entries):

- `modelIndex`: **0 = 91** (Dart/foot), **1 = 8** (ship), **2 = 30** (Coolon), **3 = 4** (teleport sphere).
- `encounterRate`: **1 = 98, 0 = 35**. Every `modelIndex 2/3` segment is rate 0; the single rate-0 `modelIndex 0` segment is an exception among the 91 foot segments.
- `battleStage`: `{0: 35, 6: 6, 36: 12, 46: 4, 86: 34, 87: 22, 88: 8, 89: 12}` — all 35 zero entries are exactly the rate-0 segments; **no entry is −1**, so the `battleStage == -1 → 1` default in `handleEncounters` (§8.2) is dead code for the current table.
- `encounterIndex`: never −1; max **127** overall, max **97** among the 98 rate-1 segments.
- **AIOOBE latent invariant (source-verified):** `encounterIds_800ef364` has **100 rows** (§4.8), so `encounterIndex` values 98–127 (present only on rate-0 segments) point at nonexistent rows. The index is dereferenced only when the accumulator fires (§8.2), which requires `encounterRate ≥ 1`; all rate-1 indices are ≤ 97. The out-of-range rows are unreachable — the same hazard class as the submap's 64-wide music base table (M9-03 §11.3).

### 4.5 Path geometry

- `pathSegmentLengths_800f5810`: **67 entries** = 66 dot-counts + terminal `−1`; max path length **46 dots**.
- `pathDotPosArr_800f591c`: **66 arrays**, **794 `Vector3f` dots total** — exactly matching the 66 lengths.
- `tmdUvAdjustmentMetrics_800eee48`: 22 entries (TMD UV adjustment metrics for the continent models).
- `placePositionVectors_800c74b8`: 257 `Vector3i` (one per place + one extra; world positions used for label/selection).

### 4.6 Teleport sphere tables

- `teleportationEndpointIndices_800ef698`: **6 pairs** — `{72,76}, {73,74}, {76,72}, {77,75}, {74,73}, {75,73}` — teleporter endpoint location pairs over locations 72–77, all ENDINESS capitals: Zenebatos (72, 73, 77), Mayfil (74, 75), Aglis (76) — the network is Zenebatos↔Aglis and Zenebatos↔Mayfil (§9.2).
- `teleportationLocations_800ef6c8`: **6 entries** for locations `0x48`–`0x4d` (72–77) with world `Vector3i` translations — the sphere positions.

### 4.7 World-map exit table (`submapWorldMapExits_800f7f74`, `RetailSubmap.java:1959–1975`, 104 entries)

Pairs of (submap cut, boundary primitive index) — the primitives that, when collided with inside that cut, exit to the world map (M9-03 §8.2). Census: **104 entries, 83 distinct cuts**, 6 `(0, 0)` tombstones, plus the special-submap exits `(994, 0)`, `(996, 0)`, `(993, 0)`, `(992, 0)` ×2, `(990, 0)`, `(991, 0)`, `(285, 0)`, `(279, 31)` (the Queen Fury dock), and the three entries **`(999, 0)`, `(999, 1)`, `(999, 2)`** — Barrier-pair exits for cut 999. **The wmap never enters submap 999** (the fade-end funnel routes `submapCut == 999` to a wmap re-initialization instead, §3.2/§9.4), so these three entries are **dead in the reference** — consistent with a vestigial Barrier submap in the retail ROM (whether any submap's NEWROOT data still targets cut 999 is asset-validated-pending). The source comment notes the table "seems to be missing one element at the end, there are 792 cuts" (a decompilation artifact of the retail table's length field).

### 4.8 Encounter table (`encounterIds_800ef364`, 100 rows × 4)

`int[100][4]` — the four encounter IDs per segment's encounter group, selected at 35/35/20/10 (§8.2). Max encounter ID **459** (within the 512-entry `LodEncounters.LEGACY` registry, M8-03 §1.1).

### 4.9 Destination markers (`wmapDestinationMarkers_800f01ec`… table at `WmapStatics` 0x800f5a6c region, `WMapDestinationMarker2c`, 64 entries)

Fields: `packedFlag_00` (bit index into `scriptFlags2_bc`), `flags_04[8]` (8-word = 256-bit location bitmap), `x_20`, `y_21`, `placeIndex_24`. Census: **49 real markers, each with a unique `packedFlag`**, then **15 `0xffff` tombstones**. The 49 markers are the world map's "discovered destination" set: §5.2 expands their bitmaps on entry; §7.3's label system renders their names.

### 4.10 Coolon warp table (`coolonWarpDest_800ef228`, `CoolonWarpDestination20`, 9 entries)

One entry **per Coolon origin** (the table is indexed by `coolonOriginIndex_221`, detected from the current location at warp start, defaulting to 8 = Zenebatos, `:2610–2635`), each: `destPosition_00` (12-bit fixed-point `Vector3f`, all at **z = 4040** — the warp-map label plane), `locationIndex_10` (the origin's own location), `defaultDestLocationIndex_14` (the pre-selected destination, 0–8), label `x_18`/`y_1a` offsets, and a two-line `placeName_1c`. The nine port locations (the origin `locationIndex` set): **25, 13, 28, 35, 48, 51, 60, 70, 83** (Lohan, Indels Castle, Fletz, Donau, Fueno, Furni, Deningrad, Ulara, Zenebatos).

### 4.11 Other tables

- `waterClutYs_800ef348`: 14 CLUT-Y values (485–492 mirrored) driving the 2-frame water animation (§10.2).
- `regions_800f01ec`: `{"South of Serdio", "North of Serdio", "Tiberoa"}` — the 3 region names of the Barrier prompt (§9.4).
- `playerAvatarVramSlots_800ef694 = {15, 16, 20, 21}` — VRAM slots for the 4 player avatar models.
- `positiveDirectionMovementMask_800f0204`: 8 input masks (4 cardinal + 4 diagonal, counter-clockwise from north) — the direction→input mapping for intersection selection (§7.1).
- `loadWait` (mutable static, §2).

---

## 5. Location availability gating: `wmapFlags` and destination markers (W3)

### 5.1 The flag arrays

`GameState52c` (:50–53) holds `Flags` objects, where `Flags(count)` is an `int[count]` word array and **packed bit `i` = word `i >>> 5`, bit `i & 0x1f`** (`Flags.java`) — i.e. 32 bits per word:

- `scriptFlags2_bc = new Flags(32)` → 1024-bit "global flag 2" array (the retail scripts' destination-award flags live here).
- `scriptFlags1_13c = new Flags(8)` → 256-bit "global flag 1" array.
- **`wmapFlags_15c = new Flags(8)` → 256 bits: one availability bit per world-map location index 0–255.**
- `visitedLocations_17c = new Flags(8)` → 256 bits: one visited bit per location.

`wmapFlags` is the *location-gate* array: `checkLocationIsValidAndOptionallySetPathStart` returns **1 ("Destination flags for location not set on gameState")** when the bit is clear (`:5518`, javadoc `:5486–5499` listing return codes −3 path-not-found, −2 not-on-current-continent, −1 location-does-not-exist, 0 valid, 1 locked), and the §3.1 entry fallback fires when the spawn location's bit is clear. The debugger renders it as 8 hex words (`GameStateViewerController.java:355–362`).

### 5.2 The two write mechanisms (both decompiled retail)

1. **Direct script access — global flag 1, bits 256–511.** `scriptSetGlobalFlag1` (@ 0x80017390, `Scus94491BpeSegment.java:284–315`) routes packed indices `8..15` (words) — i.e. **flag-1 bits 256–511 — to `wmapFlags_15c`** (`index % 8` word select, `:298/:310`), with a `ScriptFlags1ChangedEvent` posted only for the `scriptFlags1_13c` half (mod surface, M8-02). `scriptReadGlobalFlag1` (@ 0x800173fc) reads the same space (including the −1 out-of-bounds read guard documented in-source, `:324–328`). Game var **112** additionally exposes `wmapFlags` raw-word read (`GameVarParam.java:187/:335`) and per-word array access (`GameVarArrayParam.java:48/:96`). So retail scripts unlock a location by setting global-flag-1 bit `256 + locationIndex`.
2. **Destination-marker expansion at wmap entry.** In `initFlagsPathsCutsAndPlaces` (`:4635–4648`), for each of the 49 real markers: if `scriptFlags2_bc[marker.packedFlag]` is set, **all 8 words** are written with `wmapFlags_15c.setRaw(flagIndex, marker.flags_04[flagIndex])` — a *word-granular overwrite* (not a per-bit OR) applied in table order 0..48, so a later marker's word replaces an earlier one's entirely. The mechanism is source-verified; which retail script flag awards which destination (the actual `packedFlag`→region assignments) is disc content (asset-validated-pending).

### 5.3 Visited bits

`visitedLocations_17c` is set (256-bit packed `set(locationIndex, true)`) at: Coolon arrival (`:2775`, the arrived destination's `locationIndex`), teleport arrival (`:3258`), path-completion location arrival (`:4416`, `:4848`), and `SET_DEST_9` (location entry, `:4416`). It is read for the thumbnail brightness rule (§7.2) and, with `thumbnailShouldUseFullBrightness`, never gates movement.

### 5.4 Flag lifecycle at wmap entry

`initWmap2` (@ 0x800ccc64, `:723–736`) runs on **every** world-map entry (the INIT2 state, §6.1): `setProjectionPlaneDistance(1100)`; **clears all 8 words of `scriptFlags1_13c`** (`setRaw(i, 0)`, i = 0..7 — the entire 256-bit lower half of the retail "global flag 1" space is zeroed on entry, decompiled retail); `initWmapAudioVisuals()`; resets `tickMainMenuOpenTransition_800c6690`; → `LOAD_BACKGROUND_OBJ`; Discord update. The contrast is the gating model's persistence story: the script-settable **`scriptFlags1_13c` is volatile across wmap entries** (retail scripts must re-establish those flags per submap visit), while **`wmapFlags_15c` persists** (never cleared by the wmap; only expanded at entry, §5.2, and written by scripts via the upper flag-1 bits / game var 112) — so a location unlocked via the upper flag-1 bits stays unlocked across entries, while one set via the lower bits does not survive the next world-map visit. The same `initWmap2` is the documented origin of the menu-open-transition tick counter used by §6.4's fade gate.

---

## 6. State machines and per-tick orchestration (J34)

### 6.1 The `WmapState` machine (14 states, runnable table)

`wmapStates_800ef000` (`:336–351`) is a 14-entry `Runnable[]` indexed by `WmapState.state`, dispatched once per tick (`:592`):

| # | State | Runnable | Role |
|---|---|---|---|
| 0 | `INIT` | `initWmap` | resize 320×240, vsyncMode 1, `unloadSoundFile(9)` (WMAP SFX), `loadWmapMusic(chapter)`, → 1 |
| 1 | `WAIT_FOR_MUSIC_TO_LOAD` | `waitForWmapMusicToLoad` | polls music-load flag 0x80 **and** the `loadWait` minimum-wait gate (§2) → 2 |
| 2 | `INIT2` | `initWmap2` | file loads (continent model, avatar, overlays); `initFlagsPathsCutsAndPlaces` + spawn (§3.1) → 13 |
| 13 | `LOAD_BACKGROUND_OBJ` | `loadBackgroundObj` | MCQ background load → 3 (PLAY) |
| 3 | `PLAY` | `handleAndRenderWmap` | the per-tick workhorse (§6.2) |
| 4 | `TRANSITION_TO_SCREENS` | `transitionToScreens` | menu entry: VRAM readback of the effects region + 64×512 strip (`:673–675`), → 5 |
| 5 | `RENDER_SCREENS` | `renderWmapScreens` | main menu / inventory / save screen under the wmap (whichMenu dispatch; NONE_0 → 11, QUIT → 12, `:631–645`) |
| 6 | `EXIT_SCREENS` | `restoreMapOnExitMainMenu` | fade back in, VRAM re-upload, → 3 |
| 11 | `PRE_EXIT_SCREENS` | `setWmapStateToExitScreens` | 1-tick staging state before 6 |
| 7 | `TRANSITION_TO_ENGINE_STATE` | `transitionToEngineState` | epilogue for submap/battle/title targets (§3.2) |
| 8 | `TRANSITION_TO_WORLD_MAP` | `transitionToWorldMap` | **wmap-internal re-initialization**: Barrier region travel (`submapCut == 999`) and Coolon arrivals (`COOLON_ARRIVAL_3`) — deallocate → INIT with `reinitializingWmap = true` (§3.2, §9) |
| 9 | `UNUSED_DEALLOCATOR` | `unusedDeallocator` | deallocate → 10 |
| 10 | `NOOP` | `noOpWmapState11Runnable` | terminal idle |
| 12 | `TRANSITION_TO_TITLE` | `transitionToTitle` | quit epilogue incl. `resetSubmapToNewGame()` |

(The entry path is INIT → 1 → 2 → 13 → 3 in state-value order; the remaining states are the exit/menu/travel transitions. The runnable table is indexed by the enum's `state` field, so the rows above are grouped by flow, not by index.)

### 6.2 Per-tick orchestration in PLAY

`handleAndRenderWmap` (@ 0x800cccbc, `:756–759`) is exactly two calls: `handleAndRenderMapAndPlayer()` (the map/player/foreground render pass, which internally runs the two lifecycle machines and the camera/weather systems) and `handleInventoryTransition()` (the triangle-key menu gate, §6.4). The two lifecycle machines are 8-state enums (`WMap.java:204–231`):

- `WorldMapState`: `UNINITIALIZED_0, UNUSED_1, LOAD_MODEL_2, INIT_MAP_ANIM_3, NOOP_4, RENDER_5, NOOP_6, UNUSED_DEALLOC_7`.
- `PlayerState`: `UNINITIALIZED_0, UNUSED_1, LOAD_MODEL_2, INIT_PLAYER_MODEL_3, WAIT_FOR_MODEL_TO_LOAD_4, RENDER_5, NOOP_6, UNUSED_DEALLOC_7`.

Both must reach `RENDER_5` for gameplay input and encounters to be live (the `handleEncounters` gate, §8.2; the `processInput` gate, `:5045–5060`).

### 6.3 Nested travel sub-machines

`WMapModelAndAnimData258` carries the travel sub-state machines (enum values source-verified, `WMapModelAndAnimData258.java`):

- `ZoomState` — **5 reference states** (`LOCAL_0, CONTINENT_1, TRANSITION_MODEL_OUT_2, WORLD_3, TRANSITION_MODEL_IN_4`) with an in-source javadoc giving the **retail-equivalent 7-state sequence** (LOCAL, CONTINENT, TRANSITION_MODEL_OUT ×2, WORLD, TRANSITION_MODEL_IN, CONTINENT) — the reference merges the retail's duplicated transition states.
- `FadeState` (3: START_FADE/FADE/END_FADE) + `FadeAnimationType` (3: NONE/FADE_IN/FADE_OUT) — the global fade for location entry, menu transitions, and battle entry.
- `CoolonWarpState` — **12 values** incl. the gap: `NONE_0, ASCENT_1, INIT_WORLD_MAP_2, MAIN_LOOP_3, INIT_PROMPT_4, PROMPT_LOOP_5, FLY_ANIM_6, INIT_DEST_7, INIT_DESCENT_10, PAN_MAP_11, DESCENT_12, RESTORE_DART_NEG_1(−1)` (states 8/9 absent in the decompilation; −1 is a special "restore Dart" negative state).
- `TeleportAnimationState` — 4: `INIT_ANIM_0, RENDER_ANIM_1, INIT_FADE_2, FADE_OUT_3`.
- `FastTravelTransitionMode` — 4: `NONE_0, TELEPORT_1, OPEN_COOLON_MAP_2, COOLON_ARRIVAL_3`.
- `MapTransitionState` (in `MapState100`, 10 states `INIT_0`…`SET_DEST_9`) — the location-entry choreography (prompt confirm → forced walk-in → `SET_DEST_9`), §7.2 — distinct from the travel fades below.
- **Travel fade machines** (`fadeTransitionTickers_800f01fc` dispatched by `tickTransitionAnimation` @ 0x800e3ff0, indexed by `FadeAnimationType`): `tickFadeInTransition` (@ 0x800e406c) — wmap entry fade: fade-from-black, and (except `TELEPORT_1`) a camera drop-in (viewpoint y −9000 → +9000, zoom steps = player position ÷ 30), 45/`vsyncMode` ticks, 6-tick forced-movement release, ending at `AWAIT_INPUT` + no forced movement (input re-enabled); `tickFadeOutTransition` (@ 0x800e469c) — the exit fade and the fade-end funnel (§3.2).
- Camera sub-machines in `WMapCameraAndLights19c0`: `ProjectionDistanceState` (5: `SELECT_0, INIT_VIEW_NEAR_1, MAIN_LOOP_NEAR_2, INIT_VIEW_FAR_3, MAIN_LOOP_FAR_4`), `CameraUpdateState` (3: incl. `AWAIT_INPUT_0`), `MapRotationState` (3: `MAIN_LOOP_0` + two transition states), `LightsUpdateState` (4).

`MapState100` (the navigation state, `@Method`-anchored fields) holds: `continent_00`, `locationCount_08`, `directionalPathCount_0c`, `locationIndex_10`, `directionalPathIndex_12`, `pathIndex_14`, `dotIndex_16`, `dotOffset_18` (float), `facing_1c` (int, sign = direction), `prevDotPos_20`/`nextDotPos_28` (`Vector3f`), `pathSegmentEndpointTypeCrossed_fc` (`PathSegmentEndpointType`), `submapCutTo_c8`/`submapSceneTo_ca`, `submapCutFrom_c4`/`submapSceneFrom_c6`, `forcedMovementMode_d8` (`queenFuryForceMovementMode`), `shortForceMovementMode_d4`, `pathSegmentEntering_f8` (`PathSegmentEntering`), `disableInput_d0`, `locationDistances_154` (the camera's per-location distance table).

### 6.4 Menu integration under the world map (J35)

The wmap supports the main menu, inventory, and save screens *in place*: `handleInventoryTransition` (@ 0x800cc83c, triangle press) fades `mapTextureBrightness_20` down at `0.5f / (3.0f / vsyncMode)` per tick and, after `45 / vsyncMode` ticks, enters `TRANSITION_TO_SCREENS` → `RENDER_SCREENS` with the menu; before switching, the effects VRAM region (`storedEffectsRect_800c8700`) and a 64×512 strip are read back to RAM (`GPU.downloadData15`, `:673–675`) so the menu can render without the map's effects live. `restoreMapOnExitMenu_` (@ 0x800cca74) reverses it: vsyncMode 1, `startFadeEffect(2, 15)`, re-upload, `initLighting()`, `setProjectionPlaneDistance(1100)`, → PLAY. Menu QUIT routes to `TRANSITION_TO_TITLE` (`:643–645`).

---

## 7. Navigation: movement, location prompts, labels (W6)

### 7.1 Dot-walk movement

The player never free-walks the landscape: movement is **parametric travel along a path's dot array**. `updatePlayer` (`:5351–5400` region) computes the current position as the linear interpolation between `prevDotPos` and `nextDotPos` weighted by the float `dotOffset` (with a short y-arc for ship bobbing, `:3320–3325`), advances `dotOffset` by the tick speed (walk vs run, §8.1), and steps `dotIndex` at integer crossings. Facing is the int `facing_1c` (sign = travel direction along the segment); rotation is smoothed per tick (`previousPlayerRotation_c2` lerp).

**Path start/continuation:**

- `initDataForReversePath` (@ 0x800eab94, `:5448`): for a departure location — `pathIndex = |seg.indexAndDirection| - 1`; if `facing > 0`, spawn at the **last dot** (`dotIndex = length − 2`, `dotOffset = 3.0`, destination angle 0 — the player is placed just past the endpoint and walks *out* of the location); if `facing ≤ 0`, spawn at dot 0 with `dotOffset = 0`, destination angle π. Y is offset −2 (sunk into the terrain). Initial rotation = `atan2(dx, dz)` of the end/first segment.
- `setNewPathSegmentStateInfo` (`:5450` region): on segment entry, sets `locationIndex`, `directionalPathIndex`, and the segment data; `identifyNewPathSegmentAndSetStateInfo` (`:5400` region) linearly scans locations for a matching `directionalPathIndex` when crossing a segment boundary.
- At a **path intersection** (a location shared by multiple segments), direction is chosen from the held movement input using `positiveDirectionMovementMask_800f0204` (8 masks, §4.11) — `checkLocationIsValidAndOptionallySetPathStart(i, 1, intersectionPoint) == 0` filters candidates (`:1106`, `:2048`).

**`checkLocationIsValidAndOptionallySetPathStart(locationIndex, mode, dotPos)`** (`:5505`, @ 0x800eb09c) is the single validity gate, with documented return codes (javadoc `:5486–5499`): −3 `directionalPathIndex == −1` (pathless location); −2 (mode ≠ −1) location on a different continent; **1 `wmapFlags` bit clear (locked)**; 0 valid. Mode −1 = arrival (no continent check), 0 = departure, 1 = intersection candidate.

### 7.2 The location prompt

Reaching a path endpoint (or the arrival flow) opens `wmapLocationPromptPopup` (a `WmapPromptPopup` over the location's place):

- **Options:** normally a single `"Enter"` option; for the **Barrier Stations** (`submapCutTo == 999`) **two region options** — `regions_800f01ec[submapSceneTo >>> 4]` and `regions_800f01ec[submapSceneTo & 0xf]` (`:4147–4152`) — the two *other* regions (location 100 sceneTo 0x12 → regions 1,2; 101 → 0,2; 102 → 0,1; §9.4).
- **Alt text (services):** `INPUT_ACTION_WMAP_SERVICES` toggles; the place's `servicesFlag` bits (0–4) select from the 5 `services_800f01cc` names; zero bits → `"No facilities"` (`:4154–4163`). The services alt-text is suppressed for Barrier stations.
- **Thumbnail brightness** (`:4232–4245`): full brightness if `visitedLocations[locationIndex]` **or** `thumbnailShouldUseFullBrightness`; otherwise **× 0.375**.
- **Selector geometry:** option offset 8.0 (Barrier, 2 options) vs 14.0 (1 option); thumbnail blit at VRAM (768, 508) → screen (offsetX+20, offsetY−95), 120×90 (`:4247–4258`).
- **Confirm** (`:4291`): option 0 → clears label textboxes 6/7, `MapTransitionState = INIT_MOVEMENT_6`, `playMenuSound(3)`, then the 10-state `MapTransitionState` sequence (`INIT_0`…`SET_DEST_9`) runs the forced-movement walk into the location, a 9-tick `WAIT_7` delay, popup deallocation, and finally **`SET_DEST_9`** (`:4414–4440`): sets the visited bit, writes `submapCut_80052c30 = submapCutTo` / `submapScene_80052c34 = submapSceneTo` (or the Barrier special case, §9.4), re-enables input, and deallocates the popup — after which the engine transitions to the submap via the queued state.

### 7.3 Destination labels (the start-button labels)

While holding `INPUT_ACTION_WMAP_DESTINATIONS` in local zoom, up to **7 location labels** (`startLabelNames/Xs/Ys`, `startLocationLabelsActive_800c68a8`) are built from the destination markers' place names: per-label 3-stage fade-in via `startButtonLabelStages_800c86d4` (0 = clear+init textbox at (label.x+160, label.y+104), 1 = advance, 2 = render; `:4560–4615`), with text measured by the modern `measureText` (@ 0x800e7624 — "Completely replaced retail with modern String version", `:4617–4624`) and textbox width `chars × 9 / 2`, height `lines × 6 + 4`. Slots beyond the live count are reset each tick (`:4608–4614`).

### 7.4 Input surface

Census of input actions in `WMap.java`: `WMAP_ZOOM_OUT` (5), `WMAP_ZOOM_IN` (4), `WMAP_QUEEN_FURY_COOLON` (4), `WMAP_DESTINATIONS` (4), `MENU_UP/DOWN/CONFIRM/BACK` (4 each), `WMAP_SERVICES` (3), `GENERAL_RUN` (3), `GENERAL_OPEN_INVENTORY` (3), `GENERAL_MOVE_UP/RIGHT/LEFT/DOWN` (3 each), `WMAP_ROTATE_RIGHT/LEFT` (2), `MENU_RIGHT/LEFT` (2). Input gating is layered: `processInput` requires no files loading, no active fade, `modelIndex < 2` (no input while the Coolon/teleport sphere is the player model), `WorldMapState.RENDER_5` **and** `PlayerState.RENDER_5`, and no forced-movement mode (`:5045–5060`); the zoom/rotation/coolon checks add state-specific gates (`:471–925` input region).

---

## 8. World-map encounters (W7)

### 8.1 Movement-driven accumulation

`updateEncounterAndMovementAnimation` (@ 0x800e10a0, `:3327–3372`) runs every tick the player's position actually changed:

- **Run detection:** `mag = getAnalogueMagnitude()`; running when `mag == 0.0f && autoRun != runHeld` (digital, with the `RUN_BY_DEFAULT` config inverting the stick-free state) **or** `mag ≥ 0.75f` (analog over threshold); otherwise walking. Animation index 4 = run, 3 = walk.
- **Encounter call:** running → `handleEncounters(worldMapRunModifier × mag)` (analog) or `handleEncounters(worldMapRunModifier)` (digital); walking → `handleEncounters(worldMapRunModifier × mag)` (analog — the *run* modifier, scaled) or `handleEncounters(worldMapWalkModifier)` (digital). `EncounterRateMode` (`legend/game/submap/EncounterRateMode.java`) is the shared config type (M7-02) carrying the four rate fields; the world-map fields are wmap-specific.
- **Ship ambience:** `modelIndex == 1` (ship) plays water SFX (sound file 0) every `4 × tickMultiplier()` ticks (`:3366–3370`).

### 8.2 `handleEncounters(multiplier)` (`:3663–3700`, @ 0x800e3704)

**Gates (all must pass):** no files loading; `WorldMapState == RENDER_5` **and** `PlayerState == RENDER_5`; `modelIndex < 2` (no encounters on the Coolon or teleport sphere — consistent with the §4.4 rate-0 census); no active `FadeAnimationType`; no forced movement (neither `shortForceMovementMode` nor `queenFuryForceMovementMode`).

**Accumulation:**

```
encounterAccumulator_800c6ae8 += round(segment.encounterRate × multiplier × 70 / (3.0f / vsyncMode));
if (encounterAccumulator ≥ 5120) { encounterAccumulator = 0; …trigger… }
```

At the defaults (rate 1, digital walk, multiplier = `worldMapWalkModifier`), the accumulator advances `round(mod × 70/3)` per tick; the 5120 threshold therefore takes ≈ 219 ticks (≈3.7 s of continuous walking at modifier 1.0). The accumulator is an instance field zeroed at construction (`:490`) — since `EngineStates.loadGameStateOverlay` constructs a **fresh `WMap` per entry** (§3.1, J37), it starts at 0 on every world-map entry.

**Trigger:**

- `battleStageId = segment.battleStage` (the `−1 → 1` fallback is dead for the current table, §4.4).
- Encounter selection: `encounterIndex == −1 → encounterId 0`; otherwise **`rand = simpleRand() % 100`** and `rand < 35 → row[0]`, `< 70 → row[1]`, `< 90 → row[2]`, else `row[3]` from `encounterIds_800ef364[encounterIndex]` — the **35/35/20/10** weighting, matching the submap's ratios (M9-03 §7.3, which uses the equivalent 15-bit fixed-point constants 0x2ccc/0x5999/0x7333) but implemented here over the 16-bit `simpleRand()` (M4-05 §2.3: `seed = seed×9 + 0x3711`, output `seed/2 & 0xffff`, initial seed **3**). The `% 100` on a 16-bit output is mildly biased (65536 mod 100 = 36): IDs at offsets 0–35 of the output range occur one extra time per 65536 draws.
- `Encounter` resolved via the registry: `REGISTRIES.encounters.getEntry(LodMod.MOD_ID, LodEncounters.LEGACY[encounterId])` (M8-03: the 512-entry retail encounter surface).
- **Mod event:** `WorldMapEncounterEvent` (extends `EncounterEvent<WMap>`, carries the `DirectionalPathSegmentData08`) is posted before the transition — mods may swap `encounter`/`battleStageId` (the "event as RPC" idiom, M8-02).
- `startEncounter(event.encounter, event.battleStageId)` (`SBtld.java:65` region — sets `encounterId_800bb0f8`, `battleStage_800bb0f4`, the `encounter` reference, M9-03 §7.4), then `engineStateToTransitionTo = BATTLE` and `wmapState = TRANSITION_TO_ENGINE_STATE`.

**RNG consumption (Q-70, wmap leg).** `WMap` is a consumer of *both* gameplay RNGs: **22 `rand()` call sites** (19 lines; the 15-bit LCG @ 0x8002d260, initial seed **`0x24040001`**, `Scus94491BpeSegment.java:585`) — all cosmetic: the TMD angle init (`rand() % 4095` through `psxDegToRad`, `:2168`), smoke instances (`:5657–5662`), cloud placement/sizes (`:5695–5712`), snowflakes (`:5865–5869`, `:5915–5917`) — and **1 `simpleRand() % 100`** at the encounter selection above. `WMap` makes **no `srand` call**: it inherits the stream seeded at the last submap entry (M9-03 §2.2, as corrected in §16 here). The encounter *trigger* is fully deterministic (movement-quantized accumulation); only the 4-way encounter ID choice is random.

---

## 9. Special travel (W8, W9)

### 9.1 Coolon warp

The Coolon (the flying ship) connects the nine port locations (§4.10). Entry: `INPUT_ACTION_WMAP_QUEEN_FURY_COOLON` (the square button, shared with Queen Fury — disambiguated by context) at a Coolon location with the model in local zoom; from Zenebatos the warp map is reached differently — the Zenebatos port submap (529, scene 41) exits to the wmap with `OPEN_COOLON_MAP_2` + `usingCoolonFromZenebatos_254` (§3.1 path 2). The `CoolonWarpState` machine (§6.3) runs: `ASCENT_1` (Dart boards, ship rises) → `INIT_WORLD_MAP_2` (the origin's warp-map view: `coolonDestIndex` seeded from `defaultDestLocationIndex`, player position set to the origin's `destPosition`) → `MAIN_LOOP_3` / `INIT_PROMPT_4` / `PROMPT_LOOP_5` (destination selection over the 9 port labels, selector `y = index × 0x10`) → `FLY_ANIM_6` (a 36/`vsyncMode`-tick flight: `currPlayerPos` lerped origin→destination `destPosition` while the Coolon model scales out by ~1/24 per tick to 0) → `INIT_DEST_7` → (for non-Zenebatos origins) `INIT_DESCENT_10` / `PAN_MAP_11` / `DESCENT_12` (descent onto the destination). `INIT_DEST_7` (`:2781–2800`) stops the flight SFX and splits:

- **Destination 8 (Zenebatos, the home port)**: `visitedLocations.set(dest.locationIndex)`; `submapCut/submapScene` = the destination location's **`cutTo/sceneTo`** → the Zenebatos town submap via the fade-end funnel.
- **All other destinations**: `submapCut/submapScene` = the destination location's **`cutFrom/sceneFrom`** (the pair the wmap's entry lookup resolves back to that location), `fastTravelTransitionMode = COOLON_ARRIVAL_3`, `previousEngineState = null` → the fade-end funnel takes the `COOLON_ARRIVAL_3` branch → **`TRANSITION_TO_WORLD_MAP`**: the wmap re-initializes and the player walks in at the destination location (the `reinitializingWmap` branch of §3.1).
- **Zenebatos departures** (`usingCoolonFromZenebatos`, closing the warp map in `MAIN_LOOP_3`): `submapCut/submapScene` = the current (Zenebatos) location's `cutTo/sceneTo` → SUBMAP via the `OPEN_COOLON_MAP_2` branch.

`RESTORE_DART_NEG_1` is the negative-state that restores the Dart model when the Coolon flow is aborted.

### 9.2 Teleport sphere (Wingly)

The teleport sphere (in-source comment at `:3652`: "the Wingly teleportation between Aglis and Zenebatos") is a **station-submap → world-map → capital-submap** round trip over the ENDINESS map:

1. **Departure**: the player exits a Wingly station submap with a direction-encoding scene (the 5 named pairs, §3.1 path 2: Zenebatos 528 scenes 13/14/15, Mayfil 540 scene 19, Aglis 572 scene 23). Wmap entry resolves the corresponding origin teleporter location (72/73/77, 75, 76) and sets `fastTravelTransitionMode = TELEPORT_1` (the no-camera-drop entry fade, §6.3).
2. **Flight**: `handlePlayerMovement` (`:3204–3310`, "Handles player movement for both travel on paths and teleportation sphere") detects the player's `locationIndex` as `teleportationEndpointIndices[i][0]` and targets `teleportationEndpointIndices[i][1]` (the 6 pairs, §4.6). The `TeleportAnimationState` machine runs `INIT_ANIM_0` → `RENDER_ANIM_1` — a 96/`vsyncMode`-tick `arcLerp` of the player position ("lerp, but it arcs in the y direction", `:3310`) with a pulsating sphere scale `tick × 0.015625f / (3/vsync) + sin(tick × π/4 / (3/vsync)) / 16` (`:3242–3252`) and the full-screen fast-travel distortion effect (`renderFastTravelScreenDistortionEffect`) — → `INIT_FADE_2` / `FADE_OUT_3` (the sphere scales out at `0.25f / (3/vsync)` per tick).
3. **Arrival** (`INIT_FADE_2`, `:3258–3266`): `visitedLocations.set(target)`; `submapCut/submapScene` = the target location's **`cutTo/sceneTo`** — the destination capital's submap (**528 Zenebatos**, **539 Mayfil**, **572 Aglis**) — and `FADE_OUT` begins; the fade-end funnel (§3.2, `submapCut != 999`) completes the transition into that submap.

### 9.3 Queen Fury boarding

`initEnterQueenFuryTransition` (@ 0x800e8e94, `:4991–5027`): when **`scriptFlags2_bc` bit `0x97` (223)** is set (the retail script's "Queen Fury available" flag — asset-validated-pending which script moment sets it), pressing the Coolon button while `modelIndex == 1` (at sea) with no forced movement, no fade, the map unrotated, the camera in `AWAIT_INPUT_0`, local zoom, files loaded, and not at a terminal path endpoint → board the Queen Fury: `submapCutTo/SceneTo = locations[93]` (cut **279**, scene **31**), globals written, `initTransitionAnimation(FADE_OUT_2)`. A second QF-related script surface exists at entry: the **Donau departure** recognition (§3.1, submap 242 scene 3) gated by `scriptFlags2[0x8f]`/`[0x90]` (the first-time and later Queen-Fury-leaves-Donau forced-movement cutscenes). Disembarkation back to the world map is recognized at entry by the `submapSceneFrom == 31 && submapCutFrom == 279` combat/ship bypass (§3.1) — the player re-enters the world map *in sailing position* (path state restored from `GameState52c`, not path-walk-in); the Queen Fury dock's world-map exit is the `(279, 31)` table entry (§4.7). `queenFuryForceMovementMode_d8` suppresses normal input/encounters while aboard the QF's scripted movement.

### 9.4 Barrier Stations (region travel, pseudo-cut 999)

The three Barrier Stations (locations 100/101/102, §4.2) are the only inter-region travel. **Cut 999 is a pseudo-cut**: no submap 999 is ever entered from the world map — the barrier crossing is a **wmap-internal transition** (fade out, wmap re-initialization, fade in on the destination region):

1. **Departure.** The player walks the Barrier path segment (dp 31/45/50 — the segments whose endpoints *are* the Barrier locations) to the endpoint; the prompt opens with the **two other regions** as options (§7.2). On confirm, `SET_DEST_9` takes the `submapCutTo == 999` branch (`:4424–4436`): `submapCut_80052c30 = locations[i].submapCutFrom` (= 999) and **`collidedPrimitiveIndex_80052c38 = continentIndex`**, where `continentIndex = (optionIndex == 1) ? (submapSceneTo >>> 4) : (submapSceneTo & 0xf)` — the **chosen region 0/1/2** stored in the scene-hand-off global. The `MapTransitionState` sequence completes, the `tickFadeOutTransition` machine runs (90-tick MCQ fade + brightness fade, §3.2), and at the fade end the funnel sees `submapCut == 999` → **`TRANSITION_TO_WORLD_MAP`** (`:3993–3996`) — the wmap deallocates and re-initializes.
2. **Resolution.** On re-initialization, `initFlagsPathsCutsAndPlaces` reads the globals back (`submapCutFrom = 999`, `submapSceneFrom = R`), and the location scan matches the Barrier location whose `submapSceneFrom == R` — 100→R0 (South of Serdio side), 101→R1 (North of Serdio side), 102→R2 (Tiberoa side). Because `reinitializingWmap_80052c6c` is true, the spawn takes the **walk-in branch** (§3.1): `shortForceMovementMode = WALK_1`, input disabled — the player walks out of the Barrier endpoint on the destination continent; `tickFadeInTransition` fades the new continent in with the camera drop-in.
3. **Return.** The destination-side Barrier location has `cutTo == 999` with its own two region options, so the round trip back is the same flow in reverse.

The mechanism is fully source-verified (tables + the fade funnel + the entry resolution). The `(999, 0/1/2)` entries in `submapWorldMapExits_800f7f74` (§4.7) are **dead in the reference** under this flow — they imply a Barrier submap 999 with three exit doors that the decompiled wmap never loads (a vestigial retail structure; asset-validated-pending).

---

## 10. Camera and rendering (W10, W11)

### 10.1 Camera (`WMapCameraAndLights19c0`, 0x800c66b0)

- **Projection distance:** `ProjectionDistanceState` (5 states, §6.3) interpolates the projection plane between **near 600** and **far 800.0** over tick counts (the menu-restore and initWmap2 value is the wider **1100** plane, `:695`/`:724`); near ↔ far is chosen by zoom level (local vs world), with the INIT/MAIN_LOOP state pair per range.
- **Zoom:** `zoomState_1f8` (the 5-state `ZoomState`, retail-equivalent 7, §6.3) driven by `WMAP_ZOOM_IN/OUT` — LOCAL (player-relative) ↔ WORLD (continent overview); `zoomStateIsLocal_c4` latches the current regime.
- **Rotation:** `MapRotationState` (3 states) — `WMAP_ROTATE_LEFT/RIGHT` rotate the map in 45° steps through a transition state back to `MAIN_LOOP_0`.
- **Lights:** `LightsUpdateState` (4 states) — per-continent light setup re-initialized by `initLighting()` on menu restore (`:714`).
- `getCamera()` returns `currRview2_00` directly to the render engine (`:6241–6244`).

### 10.2 Rendering

- **Continents:** one TMD per continent, loaded per `continentNum` during INIT2/LOAD_BACKGROUND_OBJ (the `filesLoadedFlags` bit-0 gate, `:739`); the background is built by `loadBackgroundObj` as an `McqBuilder` ("World Map Background MCQ", `B_PLUS_F` translucency, VRAM offset (320, 0), `mcqBrightness_800ef1a4` starting at 0.0, `:738–748`); `loadMapMcqToVram` (`:1792` region) uploads the continent's MCQ background.
- **Water:** `WaterAnimation20` regions (x, y, w, h, two `FileData` frames, `currTick`) scroll 2-frame water over the MCQ; the 14 CLUT-Y values `waterClutYs_800ef348` (485–492 mirrored) drive the palette cycling.
- **Player shadows/wakes:** `shadowRenderers_800ef684` — 4-slot per-model table: Dart shadow, **Queen Fury wake** (the trailing wake quads, `:3501–3600` region: per-segment quads with UV 0–64, z scaled × 4.0), no-op (Coolon), no-op (teleport); plus the ship's port-side wake (6-vertex fan, `:3600–3650` region) and the wake segment tick management (`wakeSegmentCount`/`wakeSegmentStride`/`shipPositions` bookkeeping, `:3650–3700` region).
- **Weather (retail constants, all `rand()`-driven, §8.2):** snowfall (flake field, per-flake 12-tick phase, `:5865–5917`), smoke instances (`smokeInstances_800c86f8`, `:5657–5662`), clouds (`:5695–5712`) — all layer-3 rendering over layer-4 JOML math.
- **Markers:** `MapMarker` (16×16 arrow sprites, 8-frame sequence, 3 colors — red `(85/128, 0, 0)`, blue `(0, 0, 85/128)`, white `(1, 1, 2)`) rendered ortho from VRAM (640, 256) with CLUT (640, 496), 4-bpp (`MapMarker.java`); the Coolon/Queen-Fury prompt overlays (`CoolonQueenFuryOverlay`) and the zoom-level pyramid (`ZoomOverlay`) are the other wmap ortho elements.

---

## 11. Music and sound (W12)

- **World-map music:** `loadWmapMusic(chapterIndex)` (@ 0x8001f708, `Audio.java:775–790`) loads the 5-file DRGN music package at directory **`5850 + chapterIndex × 5`** (M6-01's 5-file music package model) after checking the no-op condition (`getSongId() × 5 + 5815 == fileIndex` — already playing), calling `unloadSoundFile(8)` (music slot) and setting the `0x80` "wmap music loading" bit of `loadingAudioFiles_800bcf78` (the `WAIT_FOR_MUSIC_TO_LOAD` gate, §2). Called at entry (`initWmap`, `:635`) and on menu restore (`:709`); `restoreMusicAfterMenu` unloads slot 8 before the reload when the menu had taken over.
- **World-map SFX:** the "WMAP SFX" `SoundFile` (instance field, `:426` — per-entry, since each wmap entry constructs a fresh `WMap`) is loaded per continent from DRGN directory **`5740 + continentIndex`** (`:542`); unloaded at entry by `unloadSoundFile(9)` (`initWmap`, `:682`) and by the menu path. Slot semantics (`Audio.java:590–695`): **8 = BGM (music thread unload)**, **9 = WMAP SFX** (SSHD playable sound), 0 = menu (5739), 4 = submap SFX, etc.
- **Ship ambience:** the periodic water SFX tick (§8.1).

---

## 12. Save contract (W13)

`WMap.writeSaveData` (`:439–447`) emits a `MapTag` with exactly 5 fields — **`pathIndex`, `dotIndex`, `dotOffset` (float), `facing`, `directionalPathIndex`** — read from the `GameState52c` fields `pathIndex_4d8` / `dotIndex_4da` / `dotOffset_4dc` / `facing_4dd` / `directionalPathIndex_4de`. `readSaveData` (`:450–462`) restores them, **gated on the presence of `pathIndex`** (older saves lacking the tag skip silently). The `mapState` → `GameState52c` copy runs at exactly two points: `transitionToScreens` (`:759–763`, when a menu opens under the wmap) and `transitionToEngineState` (`:784–788`, on exit) — so the save fields mirror the live path state at menu-open and engine-exit time, which covers every in-wmap save path (saving requires the menu) and the crash-recovery path (the last menu-open/entry copy). `lastSavableEngineState` is set in `init()` (`:435`, M7-01), making the wmap a savable engine state (crash-recovery saves capture it).

The world-map path state is thus **the only wmap state persisted** — continent, location, zoom, and travel progress are all *derived* from `(directionalPathIndex, pathIndex, dotIndex, dotOffset, facing)` at re-entry (§3.1 restore branch). The `GameState52c` flag arrays (`wmapFlags`, `visitedLocations`, `scriptFlags*` — M7-01's V1–V10 save fields, incl. the `RetailSavedGame`/`V10Serializer` leg) persist the gating and discovery state. A save made *in a submap* also carries the wmap path fields (the SMap save tag's wmap block, M9-03 §10) — the two states' path state is one shared `GameState52c` slice.

---

## 13. The script-vs-hardcode boundary (RQ7, W14)

The world map settles RQ7 (M9 cross-cutting priority) as the **most hardcode-dominant M9 system** — one of only three M9 systems with *zero* runtime script involvement (the others: the title, M9-02 T15, and the Dabas machine, M9-01 D12):

| Behavior | Script involvement | Hardcode involvement | Boundary |
|---|---|---|---|
| Path network, movement, intersection selection | none | engine over ROM tables (§7) | **pure hardcode over data** |
| Location prompt, entry sequence, thumbnails | none | engine (`MapTransitionState`, `:4140–4440`) | pure hardcode over data |
| **Which locations are unlocked** | **retail scripts set the flags** (global flag 1 bits 256–511 → `wmapFlags`; global flag 2 bits → destination markers; game var 112 raw access) | the *gate check* and marker expansion are decompiled retail (§5) | **script-policy / hardcode-mechanism** — the mechanism is source-verified; the flag→destination assignments are disc content (asset-validated-pending) |
| **Queen Fury availability** | `scriptFlags2[0x97]` set by retail script | the boarding gate (`:4991–5027`) is decompiled retail | script-policy / hardcode-mechanism |
| Encounters (trigger) | none | deterministic accumulation (§8.2) | pure hardcode |
| Encounters (ID choice) | none | `simpleRand() % 100` 35/35/20/10 (§8.2) | pure hardcode (RNG — Q-70) |
| Coolon/teleport/Barrier mechanics | none | engine state machines + ROM tables (§9) | pure hardcode over data |
| Weather, camera, rendering | none | decompiled retail rendering (§10) | pure hardcode |
| Save/load | none (script flags ride in the flag arrays) | 5-field path tag (§12) | pure hardcode |

Evidence for the zero-runtime-script claim: (a) `WMap.java` contains **zero** `SCRIPTS.` calls (census); (b) `patches/scripts.csv` (353 entries, M8-03 §3) contains **zero** wmap entries; (c) there is no wmap "controller" script slot in the loader (the submap's per-cut script model, M9-03 §9, has no wmap counterpart). The only script *surface* facing the world map is the flag/variable access in §5.2 (functions 2/3, M3-06; game var 112) — and `scriptData_08[27]` (the "hero tickets" variable, `GameState52c.java:26` comment) is a pure script-data slot with no in-repo wmap consumer (only the debugger viewer reads it, `:298`).

---

## 14. Modding surface (W15)

- **Events:** `legend/game/modding/events/worldmap/` — the world-map event family; the populated in-repo class is `WorldMapEncounterEvent` (extends `EncounterEvent<WMap>`, `implements WorldMapEvent`; payload + `DirectionalPathSegmentData08`; posted at encounter trigger, §8.2 — mods may rewrite `encounter`/`battleStageId` before `startEncounter` consumes the event). The encounter registry itself (512 retail entries, M8-03) is the primary mod lever for wmap encounters; `battleStageId` selects the battle background stage (M9-05 scope).
- **Config:** `EncounterRateMode` (the shared M7-02 entry carrying `worldMapWalkModifier`/`worldMapRunModifier`) and `RUN_BY_DEFAULT` (§8.1) — the only wmap-behavior config entries.
- **Debugger:** `GameStateViewerController`/`GameStateEditorController` expose the 8 `wmapFlags` words and the path state (`:355–362`, editor `:386–391`).

---

## 15. Classification and mechanism tables

### 15.1 World-map classification rows (W1–W16)

| ID | Claim | Layer | Confidence |
|---|---|---|---|
| W1 | Timing: `tickMultiplier() = 3`, `vsyncMode = 1` in play (2 only on hand-off frames), the two speed-constant idioms, the `loadWait` entry gate (30-tick minimum under the music-load flag 0x80) | 3 | source-verified |
| W2 | Entry/exit hand-off: the two global variables (`submapCut_80052c30`, `collidedPrimitiveIndex_80052c38`) are the sole transition payload; the (cut, scene) → location lookup with the debug-room remap and Hellena (13/17, location 5) fallback; spawn mode split (path-walk-in vs direct restore); the 5 exit routes incl. quit→`resetSubmapToNewGame` | 3 | source-verified |
| W3 | `wmapFlags_15c` = 256-bit per-location gate (`Flags(8)` word model); two write mechanisms: global-flag-1 bits 256–511 (direct) and the 49 destination markers' word-granular `setRaw` expansion from `scriptFlags2` at entry; `checkLocationIsValid` return code 1 = locked; **`scriptFlags1_13c` (the lower flag-1 half) is cleared on every wmap entry by `initWmap2`** while `wmapFlags` persists — the flag-1 halves have different volatilities | 3 (mechanism) | source-verified (retail flag assignments asset-validated-pending) |
| W4 | Geography census: 256 locations (155 pathed / 101 pathless; continent + effectFlags + brightness censuses), 249 places (78 named, 171 null; dead names 70/74), 133 segment entries (132 + 0-terminator; model 0/1/2/3 = 91/8/30/4; rate 1/0 = 98/35; battleStage census; no −1), 67 length entries (66 + −1, max 46), 794 dots, 104 exit entries (83 cuts, `(999, 0/1/2)`, `(0,0)` ×6), 64 markers (49 real, unique flags, 15 tombstones), 9 Coolon destinations (z=4040), 6 teleport pairs/locations (72–77), 100×4 encounter rows (max ID 459), 14 water CLUT-Ys, 3 regions | 3 | source-verified (tables are ROM literals) |
| W4a | **AIOOBE latent invariant:** `encounterIds` is 100 rows; only rate-1 segments dereference it; all rate-1 `encounterIndex` ≤ 97; the 28 zero-rate segments referencing rows 98–127 are unreachable (rate 0 ⇒ no accumulation ⇒ no dereference) — the hazard is preserved, not fixed | 3 | source-verified |
| W5 | Barrier flow: cut 999 is a **pseudo-cut** — the crossing is wmap-internal (fade out → `TRANSITION_TO_WORLD_MAP` re-init → `(999, R)` entry resolution → walk-in on the destination region); locations 100–102 are bidirectional endpoints (`cutFrom == cutTo == 999`); the 2-option region prompt from the sceneTo nibbles; `SET_DEST_9` stores the chosen region in `collidedPrimitiveIndex`; the `(999, 0/1/2)` exit-table entries are **dead in the reference** (submap 999 never entered from the wmap) | 3 | source-verified (any retail submap-999 usage asset-validated-pending) |
| W6 | Navigation: dot-walk interpolation with `dotOffset`, the spawn placements (last-dot/first-dot, y−2, atan2 rotation), the 8-mask intersection selection, `checkLocationIsValid` (−3/−2/1/0), the prompt (Enter/region/services, brightness ×0.375, option offset 8/14), the 10-state entry sequence ending in `SET_DEST_9`, the 7-slot label system with 3-stage fade and modern `measureText` | 3 (+4 for `measureText`) | source-verified |
| W7 | Encounters: movement-gated accumulation `round(rate × mult × 70 / (3/vsync))`, threshold 5120, run/walk × (analog mag) multiplier model, `simpleRand() % 100` 35/35/20/10 (16-bit bias noted), `battleStage −1 → 1` dead for the current table, `WorldMapEncounterEvent` before `startEncounter`, fresh accumulator per entry (fresh instance) | 3 (+4 for the config multipliers) | source-verified |
| W8 | Coolon warp: the 12-state machine (8/9 absent, −1 restore state); per-origin 9-entry table (z=4040 label plane); 36-tick flight lerp + ~1/24-per-tick scale-out; `INIT_DEST_7` split — destination 8 (Zenebatos) → town submap via `cutTo/sceneTo`, other arrivals → `COOLON_ARRIVAL_3` wmap re-init via `cutFrom/sceneFrom`, Zenebatos departures → submap via `OPEN_COOLON_MAP_2` | 3 | source-verified |
| W9 | Teleport sphere (Wingly): station-submap → wmap → capital-submap round trip; the 5 named departure pairs set `TELEPORT_1` at entry; 6 endpoint pairs over locations 72–77 (Zenebatos↔Aglis, Zenebatos↔Mayfil); the 96-tick `arcLerp` + pulsating scale + distortion effect; arrival writes the destination capital's `cutTo/sceneTo` (528/539/572) | 3 | source-verified |
| W9a | Queen Fury boarding: `scriptFlags2[0x97]` gate + the 7-condition input gate; `locations[93]` = cut 279/scene 31; disembarkation recognition at entry (scene 31 + cut 279) restores sailing position | 3 | source-verified (flag 0x97's retail setter asset-validated-pending) |
| W10 | Camera: projection 600↔800 (1100 menu-restore) over 5 states, zoom (5 reference / 7 retail-equivalent states), 45° rotation, 4-state lights | 3 | source-verified |
| W11 | Rendering: per-continent TMD + MCQ, 2-frame water + 14 CLUT-Ys, 4-slot shadow/wake table (Dart shadow, QF wake quads UV 0–64 z×4, ship port fan), weather (snow/smoke/clouds) on `rand()`, `MapMarker` ortho sprites (3 colors, VRAM 640/256, CLUT 640/496) | 3 | source-verified |
| W12 | Music/sound: wmap music package `5850 + chapter × 5` (5 files; 0x80 load flag; slot 8), per-continent WMAP SFX `5740 + continent` (slot 9, per-entry SoundFile), menu SFX 5739 (slot 0), ship water SFX every `4 × tickMultiplier` ticks | 3 | source-verified |
| W13 | Save: the 5-field path tag (`pathIndex/dotIndex/dotOffset/facing/directionalPathIndex`) over the shared `GameState52c` slice, `pathIndex`-gated read, the two-point mapState→gameState copy (menu-open + engine-exit), `lastSavableEngineState` in `init`; the flag arrays ride in the standard save fields | 3 | source-verified |
| W14 | RQ7 boundary: zero runtime script involvement (no `SCRIPTS.` calls, zero `scripts.csv` entries, no controller-script slot); script influence limited to the flag/variable surface (functions 2/3, game var 112, `scriptData[27]` unconsumed in-repo) | 3/4 | source-verified |
| W15 | Mod surface: `WorldMapEncounterEvent` (the only in-repo world-map event payload class), encounter registry as the primary lever, `EncounterRateMode` + `RUN_BY_DEFAULT` config, debugger flag editor | 4 | source-verified |
| W16 | Quit from wmap resets the submap to new-game state (`resetSubmapToNewGame()` before the title hand-off) — the same reset as GAME_OVER (M9-02 T10) | 3 call, 4 context | source-verified |

### 15.2 Java-mechanism rows (J34–J38)

| ID | Mechanism | Notes |
|---|---|---|
| J34 | Per-tick orchestration: the 14-entry `wmapStates_800ef000` runnable table dispatched once per tick; the 8-state `WorldMapState`/`PlayerState` lifecycle machines both gating `RENDER_5`; the nested travel sub-machines on `WMapModelAndAnimData258`/`WMapCameraAndLights19c0` | §6 |
| J35 | In-place menu integration: main menu / inventory / save screens render over the wmap; effects VRAM readback before the switch (`GPU.downloadData15`), re-upload + `initLighting` + `setProjectionPlaneDistance(1100)` on restore; QUIT → `TRANSITION_TO_TITLE` | §6.4 |
| J36 | Async load gating: all file loads are futures over the loader, polled per tick via `Loader.getLoadingFileCount()` and the `filesLoadedFlags`/`loadingAudioFiles_800bcf78` bit gates (music 0x80; the `loadWait` minimum-wait floor) | §2, §6.1 |
| J37 | Fresh-instance-per-entry: `EngineStates.loadGameStateOverlay` constructs a new `WMap` on every state change (`constructor_00.get()`, `EngineStates.java:71`) — all instance state (accumulator, travel machines, prompts, VRAM handles) is per-entry by construction; only the `GameState52c` slice and the ROM tables persist | §3, §8.2, §12 |
| J38 | Weak/instance object lifecycle: prompt popups, markers, and overlays are instance-allocated and `deallocate()`d at state exit (`:1022–1030`); the TMD renderer slots are released via the `Obj`/`Texture` clear in `loadGameStateOverlay` (M1-04) | §6 |

---

## 16. Register updates and in-place corrections

**Q-70 — advanced (wmap leg).** M9-04 documents the world map's consumption of the gameplay RNGs: 22 `rand()` (15-bit LCG) call sites (19 lines), all cosmetic (TMD angles, smoke, clouds, snow — §8.2), and the single `simpleRand() % 100` at encounter selection (the only gameplay-significant wmap draw). `WMap` makes no `srand` call — the wmap inherits the submap-seeded stream and re-encounters after battle return continue it un-reseeded, exactly as the submap leg (M9-03 §2.2) established for the submap→battle direction. The encounter *trigger* is deterministic (movement-quantized, §8.2); the non-reproducibility is confined to the 4-way ID choice. The seed-source design decision remains **Proposed/M10** as the register states.

**In-place corrections (committed with this document):**

1. **M9-03 §2.2 (SM2) and the register's Q-70 M9-03-advance note** stated the `rand()` LCG's initial seed is **3**. Corrected to **`0x24040001`** (`Scus94491BpeSegment.java:585`, `private static int randSeed = 0x24040001;`). The value **3** is the initial seed of the *other* LCG, `simpleRand` (`simpleRandSeed_8004dd44 = 3`, `Scus94491BpeSegment_8004.java:58`, M4-05 §2.3) — the two 15-bit/16-bit LCGs were conflated. The M9-03 SM2 row's "(LCG @ 0x8002d260, initial seed 3)" is corrected to "initial seed 0x24040001"; the register's Q-70 (b) clause ("reachability from initial seed 3") is unchanged in substance — it correctly refers to `simpleRand`'s seed — while the Q-70 advance note's "(initial seed 3)" referring to `rand` is corrected.
2. No M4/M5/M6/M7/M8 document changes were required: M4-05 §2.3/§2.4 already documented both LCGs with the correct seeds; M9-03's exit-table usage (M9-03 §8.2) contains no census claim that conflicts with the 104-entry/83-cut census re-verified here (the `(999, 0/1/2)` tail entries are documented as dead-in-reference Barrier pairs, §4.7/§9.4).

**Register status after M9-04:** Q-70 **advanced** (wmap leg added; seed-value correction applied in place). **No new questions opened** — the Barrier flow (wmap-internal, pseudo-cut 999), the gating model, the fast-travel network (Wingly/ Coolon/ Queen Fury), and the encounter invariants all resolved from in-repo source; the only asset-validated-pending items (retail flag→destination assignments, flags 0x8f/0x90/0x97's script setters, any retail submap-999 usage, retail rendering identity) are the standing no-disc-image limitation, not open research questions.

**Files created/modified by M9-04:** created `research/game/04-worldmap.md`; modified `research/game/03-submap.md` (§2.2 seed correction), `research/04-unresolved-questions.md` (Q-70 advance + seed correction), `research/README.md` (phase status), `AGENTS.md` (M9-04 delivery note).
