# 06 — Characters and Progression

M9 deliverable 06. Covers: the character data model (`CharacterData2c`, the 0x2c retail struct, the `GameState52c` campaign/party model), the character template system (the 9 retail templates, the registry, the shared `RetailCharacterTemplate` progression logic, template inheritance and the Albert/Miranda mirroring), the stat system (the 17 registered stat types, the `Stat`/`UnaryStat`/`FractionalStat`/`VitalsStat` model, stat mods, the character→battle-entity stat bridge), level progression (the per-character XP tables, the level-up pipeline, the post-battle XP distribution and grant, extended levels), dragoon (spirit) progression (the per-character DXP tables, the dragoon-level-up pipeline, the DXP sources, the transform gates, the d-attack data, the divine-dragoon variant), elements (the 9-element registry, the strong/weak/boosts model, the three damage-adjustment paths, character and attack element resolution), addition and spell progression (the unlock criteria model, the mastery economy, the retail per-character addition/spell tables, the spell struct and dragoon-spell DEFF loading), the party model (flags, main/secondary split, the script party API, the char-swap rules), and the new-game/save/load lifecycle.

This document is the **consumer side** of the XP/SP/mastery grant paths that 05c documented from the battle side (the `scriptLevelUpAddition`/`scriptGiveSp` producers, the addition driver). Battle mechanics that are already settled — the damage pipelines, the bent flag/status words, the menu and action execution, the spinner engines, the status-ailment lifecycle, the post-battle *action* dispatch — live in **05a**/**05b**/**05c** and are cross-referenced, not repeated. Save-file byte formats (card layout, versioning, the retail save card header) live in **M7-01**; this document covers the character *state* those formats persist.

Sources (all read this session unless noted):

- Severed-Chains @ `0c08c679b` (pinned, clean): `legend/game/characters/` (`CharacterData2c` full, `CharacterTemplate` full, `Stat`, `StatType`, `UnaryStat`, `FractionalStat`, `VitalsStat`, `StatCollection`, `StatMod`, `UnaryStatModType`, `FractionalStatModType`, `CharacterAdditionInfo`, `CharacterSpellInfo`, `LevelUpActions`, `Element`, `ElementSet`, `CharacterTemplateRegistry`, `AdditionUnlockCriterion` + the four criterion classes), `legend/lodmod/LodMod.java` (stat registrations `:162–179`, element registrations `:186–194`, the battle-action menu builder `:700–837` incl. `canSpecialTransform`, the `DRAGOON_SPIRIT_ICONS` list `:880–888`), `legend/lodmod/LodCharacterTemplates.java`, `legend/lodmod/characters/` (all 9 templates + `RetailCharacterTemplate` full + `DartCharacterData` full), `legend/lodmod/LodSpells.java` (full), `legend/lodmod/spells/` (`RetailSpell`, `DragonSpell`), `legend/game/inventory/SpellStats0c.java`, `legend/lodmod/Legacy.java` (full), `legend/lodmod/LodGoods.java` (spirit entries), `legend/lodmod/RetailCampaignType.java`, `legend/lodmod/LodLevelUpActions.java` + the four `*LevelUpAction` classes, `legend/game/types/GameState52c.java`, `legend/game/SItem.java` (`addSp` `:487–515` @ 0x80022cd0, `cacheCharacterSlots` `:968–983` @ 0x80103b10, the status-screen dragoon-stat rendering `:1493–1532`), `legend/game/combat/Battle.java` (`initPlayerBattleEntityStats` @ ~LAB_800ef31c, the equipment transfer block `:8430–8460`, `scriptGiveSp` @ 0x800f43dc `:8730–8741`, `scriptLevelUpAddition` @ 0x800cd200 `:4394–4411`, `scriptSetDragoonSpaceElementIndex` @ 0x800f9b78 `:9208–9216`, the elemental-application block `:8595–8620`, `totalXpFromCombat` reset `:1972` and enemy-kill accumulation `:4315–4332`, `livingChars_800bc968` fill `:2435–2444`, `spGained_800bc950.clear` `:8323`), `legend/game/combat/bent/PlayerBattleEntity.java` (field block `:57–90`, `isDragoon`/`canBecomeDragoon` `:155–160`, the `HAS_TRANSFORMED_BEFORE` write `:591`, `getAttackElements`/`getElement` `:182–189`), `legend/game/combat/formula/PhysicalDamageFormula.java`, `legend/game/inventory/screens/PostBattleScreen.java` (report start `:129–176`, `CHECK_NEXT_THING_TO_GIVE` `:196–230`, `givePendingXp` `:521–558`, `givePendingDxp` `:560–592`, `checkForUnlocks`/`applyLevelUpActions` `:594–625`), `legend/game/saves/RetailSavedGame.java` (`createGameState` `:96–252`, `updateAddition` `:254–270`), `legend/game/saves/ConfigPresetManager.java` (the vanilla preset `:208–225`), `legend/game/modding/coremod/` (`UnlockPartyConfig`, `SecondaryCharacterXpMultiplierConfigEntry`, `CoreMod.UNLOCK_PARTY_CONFIG` `:184`), `legend/lodmod/config/` (`MaxLevelConfig`, `MaxDragoonLevelConfig`, `ExtendedDragoonActionsConfig`), `legend/game/scripting/GameVarParam.java` (case 61 `:139/297`, cases 115–123 `:338–346`, `setPartyFlags` `:368–372`), `legend/game/combat/ui/CharSwapListMenu.java` (`canUseCharSwap`), `legend/game/inventory/screens/CharSwapScreen.java` (the `UNLOCK_PARTY` gates), `legend/game/Scus94491BpeSegment_8004.java` (`additionOffsets_8004f5ac` `:171`, `CHARACTER_ADDITIONS` `:174–183`)
- `patches/scripts/player_combat_script.txt` (6,802 lines — layer-4 replacement; the DXP/SP and mastery producers cited here are the ones documented in 05c §3/§4)
- Prior research: M9-05a (B1–B20: the 362-slot table, the battle-stage model, the script-var surface), M9-05b (F1–F20: the bent models, the power-stat derivation, the elemental damage stages, the guard model), M9-05c (U1–U35: the addition driver, the SP/mastery grants, the d-attack HUD, the tutorial words, the equipment-flag mechanism), M7-01 (save formats: the retail char array, the V2 character tag, the config storage), M8-02/M8-03 (the registries and the core-mod census: 9 character templates, 35 additions, the event surface)
- No retail assets are available: no `asset-validated` claims in this document. The per-character tables (XP/growth/DXP/addition/spell/spirit data) are L4 constants extracted by the reference author from the retail binary; they are **source-verified as reference data** and retail-byte-pending for the values themselves.

## 1. Scope and layer model

Four layers apply to this surface (05b §1):

- **Native (decompiled retail, L3)**: the `CharacterData2c` 0x2c struct and `GameState52c` 0x52c campaign struct, the party model (flags, `charIds_88`, `cacheCharacterSlots`), the `SItem.addSp`/`cacheCharacterSlots` routines (retail `@Method` addresses preserved), the character→bent stat transfer (`initPlayerBattleEntityStats`), the `spGained_800bc950`/`totalXpFromCombat_800bc95c`/`livingChars_800bc968` battle globals, the post-battle progression report (`PostBattleScreen` — retail screen logic with retail addresses), the dragoon-spell DEFF loading, and the `hasTransformed` script-flag bits (retail `scriptFlags2` bits `0x1b8–0x1c0`).
- **L4 core-mod data (layer-4, modeling retail)**: the 9 `CharacterTemplate` registrations with their per-character tables, the 17 stat-type registrations, the 9 element registrations, the 63-entry spell registry, the 35-entry addition registry (M8-03), the goods (spirits), the 4 no-op level-up actions, the `Legacy` tables (`CHAR_IDS`, `CHARACTER_SPELLS`, `RETAIL_HP`), the `additionOffsets_8004f5ac`/`CHARACTER_ADDITIONS` retail tables, the `RetailCampaignType` new-game setup, the config entries (`MAX_LEVEL`, `MAX_DRAGOON_LEVEL`, `SECONDARY_CHARACTER_XP_MULTIPLIER`, `UNLOCK_PARTY`, `EXTENDED_DRAGOON_ACTIONS`), and the mod-event hooks.
- **L4 engine re-architecture**: the `Stat`/`StatCollection` object model (retail stored HP/MP/SP and the addition level/XP arrays *inside* the 0x2c struct — the decompiled source shows them commented out), the template abstraction (retail had per-character C tables/functions), the registry-based additions/spells (retail had fixed per-character arrays), the extended-level machinery (past level 60), and the vanilla config preset (the reference's retail-equivalence model, M7-02).
- **Script (L3 retail originally; L4 replacement in the reference)**: minimal on this surface. The script *producers* are the in-battle SP grants (`scriptGiveSp` — the L4 combat script calls it), the mastery ticks (`scriptLevelUpAddition` — called by the 05c addition driver), the party-membership writes (the retail `char_utils` script wrote the per-character party-flag variables `115–123`; the retail script is not in the repo), and the dragoon-space element set (`scriptSetDragoonSpaceElementIndex`, called by the d-attack flow). Progression *rules* are native + data.

Layer tags: **L3** LoD-specific (decompiled retail), **L4** reference-implementation-specific.

## 2. The character data model

### 2.1 `CharacterData2c` — the 0x2c retail character record (C1)

`CharacterData2c` (`characters/CharacterData2c.java`) is the per-character persistent record. The decompiled field names keep the retail byte offsets as suffixes; the layout as reconstructed in the reference:

| offset | field | meaning |
|---|---|---|
| 0x00 | `xp_00` | XP toward the next level (see §5.2 for the threshold semantics) |
| 0x04 | `partyFlags_04` | party-membership flags (C2) |
| 0x08–0x0c | *(retail `hp_08`/`mp_0a`/`sp_0c` — commented out in the reference)* | retail stored current HP/MP/SP here; the reference moves them into the `VitalsStat` values in `stats` (C1, §14) |
| 0x0e | `dlevelXp_0e` | DXP toward the next dragoon level |
| 0x10 | `status_10` | status-ailment bits (C3) |
| 0x12 | `level_12` | level (1-based in play) |
| 0x13 | `dlevel_13` | dragoon level |
| 0x14 | `equipment_14` | `EnumMap<EquipmentSlot, Equipment>` — retail stored per-slot item IDs; the reference stores equipment objects (5 slots: WEAPON/HELMET/ARMOUR/BOOTS/ACCESSORY) |
| 0x19 | `selectedAddition_19` | registry ID of the currently selected addition (`null` for Shana/Miranda — they have no additions; the 05b comment "No addition (Shana/???)") |
| 0x1a / 0x22 | *(retail `additionLevels_1a[8]` / `additionXp_22[8]` — commented out)* | retail stored addition levels and mastery XP as 8-entry arrays; the reference stores a `LinkedHashMap<RegistryId, CharacterAdditionInfo>` (C1) |
| — | `stats` | `StatCollection` (the reference's stat storage; §4) |
| — | `additions`, `spells` | `LinkedHashMap` of per-character addition/spell info (creation order = template order) |
| — | `hasTransformed` | boolean (L4 addition; retail backed it with `scriptFlags2` bits — C19) |
| — | `gameState`, `template`, `name` | back-references; the name is an `I18nText` keyed by the template registry ID |

`set(other)` copies xp, partyFlags, stats, dxp, status, level, dlevel, equipment, selectedAddition, and the addition/spell maps (via copy-constructors), then calls `template.copy(other, this)` — the template-specific mirroring hook (§3.5).

### 2.2 Party flags (C2)

`partyFlags_04` bits (constants on `CharacterData2c`):

| bit | constant | meaning |
|---|---|---|
| 0x1 | `IN_PARTY` | in the overall party (retail "in party") |
| 0x2 | `CAN_BE_IN_PARTY` | eligible for the *main* party (without it, the character can only appear as secondary) |
| 0x4 | `TEMPORARILY_REMOVED_FROM_PARTY` | assigned by the retail `char_utils` script (in-source comment) for characters temporarily removed from the party, to be restored later; "when temporarily removed from the party, these flags will be shifted up by 8 bits to back them up" (in-source comment — the backup mechanism is visible only in the comment; the L4 writer is the script var API, C5) |
| 0x20 | `CANT_REMOVE` | "don't select, can't be taken out of main party" (Dart at new game) |
| 0x40 | `HAS_ULTIMATE_ADDITION` | "no longer used" (in-source comment) |

### 2.3 Character status bits (C3)

`status_10` (the character-side mirror of the battle `status_0e`; 05b documents the battle-side consumption): `0x1` Petrified, `0x2` Bewitched, `0x4` Confused, `0x8` Fearful, `0x10` Stunned, `0x20` Weapon blocked, `0x40` Dispirited, `0x80` Poison. `getStatusAndFlags()` returns `status_10 | (hasDragoon() ? 0x2000 : 0)` — the 0x2000 bit is the "has a dragoon spirit" marker that becomes `PlayerBattleEntity.canBecomeDragoon()` in battle (`(status_0e & 0x2000) != 0`, `PlayerBattleEntity.java:158–160`). `DartCharacterData.getStatusAndFlags()` additionally ORs `0x4000` while the divine spirit is in goods (§6.6) — the 05c U28 "STATUS & 0x4000" divine marker.

### 2.4 The campaign/party container (C4)

`GameState52c` (the 0x52c retail campaign struct, M7-01 for the save mapping) holds:

- `charData_32c` @0x32c: `List<CharacterData2c>` — **all 9 characters**, created in the fixed retail slot order (C4).
- `charIds_88` @0x88: `IntList` — the **main party**: indices into `charData_32c`, capacity 3 in retail (the char-swap UI fills `3 - charIds_88.size()` slots, `CharSwapScreen.java:107`).
- `getCharacterBySlot(slot)` = `charData_32c.get(charIds_88.getInt(slot))`.
- Everything else (script data `0x08`, gold `0x94`, flags, goods `0x19c`, equipment `0x1e8`, items `0x2e9`, wmap fields `0x4d8+`) — see M7-01 / 03 / 04.

`SItem.cacheCharacterSlots()` @ 0x80103b10 (`SItem.java:968–983`) rebuilds the two party caches used by the menus and the post-battle report:

- `characterIndices_800bdbb8`: every character index with `IN_PARTY` set.
- `secondaryCharIds_800bdbf8`: characters with `IN_PARTY` set **but not present in `charIds_88`** — the bench ("secondary") party.

So the retail party model is two-tiered: *overall party* (`IN_PARTY`) and *main party* (`charIds_88`, 3 slots). A character is `IN_PARTY` but not in `charIds_88` → secondary. (05c §8's post-battle report consumes exactly this split, §10.2 here.)

### 2.5 The script party API (C5)

Party membership is script-writable: game-var cases **115–123** (`GameVarParam.java:338–346`) map to per-character (0–8) party-flag writes; `setPartyFlags(index, flags)` posts a `PartyFlagsChangeEvent` (a mod hook that may rewrite the index/flags) and then assigns `charData_32c[index].partyFlags_04`. This is the native landing pad for the retail `char_utils` script (the retail script itself is not in the repo — the in-source comment on `TEMPORARILY_REMOVED_FROM_PARTY` is the only direct evidence of its existence). In the reference, no L4 script writes these variables; mid-story party changes therefore flow through whatever L4 script layer the campaign scripts provide, and the party UI (§2.6) is the player-facing surface.

### 2.6 The char-swap rules and `UNLOCK_PARTY` (C4)

- In-battle (the CHARACTERS menu action, 05c §2): `CharSwapListMenu.canUseCharSwap(player)` — denied if the player has `CANT_REMOVE` and `UNLOCK_PARTY` is off; otherwise requires a non-empty usable list (characters with `IN_PARTY | CAN_BE_IN_PARTY` not currently in the battle party).
- Out-of-battle (`CharSwapScreen`): swapping a `CANT_REMOVE` character out of the main party is blocked unless `UNLOCK_PARTY` is on; a character can enter the main party only if it has `CAN_BE_IN_PARTY`, unless `UNLOCK_PARTY` is on.
- `UNLOCK_PARTY` (L4 bool config, **default true**) is the reference's relaxation of the retail party rules; the **vanilla preset** (`ConfigPresetManager.java:208–225`) sets it to **false** to model retail.

## 3. The character template system

### 3.1 The abstract surface (C6)

`CharacterTemplate` (`characters/CharacterTemplate.java`, a `RegistryEntry`) — the per-character "class". `make(gameState)` calls `makeCharacter(gameState)` then posts `CharacterCreatedEvent`. The abstract/overridable surface (grouped):

- **Data**: `makeCharacter` (abstract), `copy(from, to)` (default no-op; §3.5), `deserialize(FileData, IntRef)` / `serialize(character, MapTag)` / `deserialize(MapTag)` (the L4 save path, C35).
- **Progression**: `applyLevelUp(character, actions[, source])`, `applyDragoonLevelUp(character, actions)`, `checkUnlocks(character, actions)`, `getXpToNextLevel(character)`, `getDxpToNextLevel(character)`; the growth getters (`getHpToAdd(level)`, `getMpToAdd`, `getSpToAdd`, `getSpeedToAdd`, `getAttackToAdd`, `getDefenseToAdd`, `getMagicAttackToAdd`, `getMagicDefenseToAdd`, `getDragoonAttackToAdd(dlevel)`, `getDragoonMagicAttackToAdd`, `getDragoonDefenseToAdd`, `getDragoonMagicDefenseToAdd` — the ones not abstract have retail defaults, §5.3/§6.3), and `getExtendedXp(character)` (protected, default `Σ_{i=0}^{level-60} (23000 + i×1000)`).
- **Identity**: `getElement(character)` / `getElement(character, bent)` (default: the bent-aware delegates to the plain one), `hasDragoon(character)`, `canUseSpells(character)`, `getDragoonAddition(character)` (the d-attack data, §6.5), `isArcher(character)` (Shana/Miranda: true — the `IS_ARCHER` script-readable, `PlayerBattleEntity.java:534`), `canEquip(character, slot, equipment)`.
- **Presentation/assets**: `loadPortrait`, `renderTransformIcon`, battle model/texture/sound paths, attack/dragoon-attack/transform animation loaders, `getDragoonTransformDeff` / `getDragoonAttackDeff`, weapon-trail and spell-ring colours, particle colour, model-part indices (hands/feet/weapon), shadow size, `getWeaponTrailVertexComponent`, `getSpBarColours(index)` (5 palettes × 6 colours on `RetailCharacterTemplate`), `loadWorldMapModel` (the wmap character models — 04).

### 3.2 The registry and the 9 retail templates (C6)

`CharacterTemplateRegistry` (registry ID `lod_core:character_templates`); `LodCharacterTemplates` registers the 9 retail templates: `dart`, `lavitz`, `shana`, `rose`, `haschel`, `albert`, `meru`, `kongol`, `miranda` (via `RegisterCharacterTemplatesEvent`, M8-02). The retail character slot order (the `CHAR_IDS` table, §3.6/§11.3) matches this set.

### 3.3 `RetailCharacterTemplate` — the shared retail base (C7, C8, C29)

All 9 retail templates extend `RetailCharacterTemplate` (directly or via the Lavitz/Shana parents). Its shared logic:

- **`makeCharacter` (base)**: builds the 17-stat `StatCollection` in the fixed retail order (HP, MP, SP, SPEED, ATTACK, MAGIC_ATTACK, DEFENSE, MAGIC_DEFENSE, ATTACK_HIT, MAGIC_HIT, ATTACK_AVOID, MAGIC_AVOID, DRAGOON_ATTACK, DRAGOON_MAGIC_ATTACK, DRAGOON_DEFENSE, DRAGOON_MAGIC_DEFENSE, GUARD_HEAL); sets raw ATTACK_HIT = 100, MAGIC_HIT = 100, GUARD_HEAL = 10; then calls `applyLevelUp(INITIALIZATION)` and `applyDragoonLevelUp(null)`, then restores HP and MP to max. **Consequence (C7a)**: every character is created at **level 1** (table row 0 applied) and **dlevel 1** — the first dragoon-level-up's stat increments (MP +20, SP +100, the dlevel-0 dragoon-stat row, C8) are part of every character's starting stats even before any spirit is owned. SP starts at 0 (only HP/MP are restored).
- **`applyLevelUp(character, actions, source)`**: posts `PreCharacterLevelUpEvent(character, source)` pre-filled with `statsToAdd` = {HP: `getHpToAdd(level)`, ATTACK: `getAttackToAdd(level)`, DEFENSE, MAGIC_ATTACK, MAGIC_DEFENSE, SPEED} — **all indexed by the character's current level** (row L is the increment applied going from level L to L+1); mods may alter `statsToAdd` and `levelsToAdd` (default 1); the increments are added to the raw stats; `level_12 += levelsToAdd`; `checkUnlocks(character, actions)`; posts `PostCharacterLevelUpEvent`. **XP is not touched here** — the XP→level loop lives in the consumer (the post-battle report, §10.2, and the initialization replays).
- **`applyDragoonLevelUp(character, actions)`**: posts `PreCharacterDragoonLevelUpEvent` pre-filled with `statsToAdd` = {MP: `getMpToAdd(dlevel)` (default **20**), SP: `getSpToAdd(dlevel)` (default **100**), DRAGOON_ATTACK: `getDragoonAttackToAdd(dlevel)` (abstract), DRAGOON_DEFENSE: `getDragoonDefenseToAdd` (default: dlevel 0 → **200**, dlevel 4 → **20**, else **10**), DRAGOON_MAGIC_ATTACK (abstract), DRAGOON_MAGIC_DEFENSE (same default as defense)}; apply; `dlevel_13 += levelsToAdd`; `checkUnlocks`; posts `PostCharacterDragoonLevelUpEvent`.
- **`checkUnlocks(character, actions)`**: for every addition and spell on the character, `info.checkUnlock(character)` (C29) → `info.unlock(gameState.timestamp_a0)` and, if `actions != null`, records `UNLOCK_ADDITION`/`UNLOCK_SPELL` with the options (§5.4).
- **`hasDragoon(character)`**: `gameState.goods_19c.has(getDragoonSpirit())` — **dragoon eligibility is the spirit item in the goods inventory, not a character flag** (C17).
- **`swapAddition`/`swapSpell`**: template-clone helpers used by `copy` (§3.5).
- **`getMpToAdd`/`getSpToAdd`/`getDragoonDefenseToAdd`/`getDragoonMagicDefenseToAdd`**: the retail defaults above; the per-character overrides are the abstract ones plus a few (Shana/Miranda override the dragoon attack/magic-attack rows: 200/150 at dlevel 0, 5 otherwise).
- **`getExtendedXp`** (default on `CharacterTemplate`): the past-level-60 XP series 23000, 24000, 25000, … (C10).
- **L4 save** (`serialize`/`deserialize`): the per-character tag fields are `flags, xp, dxp, level, dlevel, hasTransformed, status, stats, equipment (slot→id), selectedAdditionId, additions (id, unlockState, unlockTimestamp, level, xp), spells (id, unlockState, unlockTimestamp)` — M7-01 documents the enclosing save versions.

### 3.4 The per-character template data (C9, C14, C17, C27, C32, C38)

| template | parent | element | spirit (good ID) | d-attack | additions (unlock levels) | spells (dlevel gates) | start addition |
|---|---|---|---|---|---|---|---|
| Dart | Retail | FIRE (DIVINE in dragoon form w/ divine spirit) | RED (10); DIVINE (80) qualifies too (C39) | yes (divine variant) | 7: DOUBLE_SLASH (init), VOLCANO 2, BURNING_RUSH 8, CRUSH_DANCE 15, MADNESS_HERO 22, MOON_STRIKE 29, BLAZING_DYNAMO (mastery) | 4: FLAMESHOT (spirit), EXPLOSION 2, FINAL_BURST 3, RED_EYED_DRAGON 5 | DOUBLE_SLASH |
| Lavitz | Retail | WIND | JADE (30) | yes | 5: HARPOON (init), SPINNING_CANE 5, ROD_TYPHOON 7, GUST_OF_WIND_DANCE 11, FLOWER_STORM (mastery) | 4: WING_BLASTER (spirit), BLOSSOM_STORM 2, GASPLESS 3, JADE_DRAGON 5 | HARPOON |
| Shana | Retail | LIGHT | SILVER (60) | **null** (C38) | **none** | 4: MOON_LIGHT (spirit), STAR_CHILDREN 2, GATES_OF_HEAVEN 3, WHITE_SILVER_DRAGON 5 | — (none) |
| Rose | Retail | DARK | DARK (70) | yes | 4: WHIP_SMACK (init), MORE_MORE 14, HARD_BLADE 19, DEMONS_DANCE (mastery) | 4: ASTRAL_DRAIN (spirit), DEATH_DIMENSION 2, DEMONS_GATE 3, DARK_DRAGON 5 | WHIP_SMACK |
| Haschel | Retail | THUNDER | VIOLET (50) | yes | 6: DOUBLE_PUNCH (init), FERRY_OF_STYX 14, SUMMON_4_GODS 18, FIVE_RING_SHATTERING 22, HEX_HAMMER 26, OMNI_SWEEP (mastery) | 4: ATOMIC_MIND (spirit), THUNDER_KID 2, THUNDER_GOD 3, VIOLET_DRAGON 5 | DOUBLE_PUNCH |
| Albert | Lavitz | WIND (inherited) | JADE (inherited) | yes | 5: ALBERT_HARPOON (init), ALBERT_SPINNING_CANE 5, ALBERT_ROD_TYPHOON 7, ALBERT_GUST_OF_WIND_DANCE 11, ALBERT_FLOWER_STORM (mastery) | 4: ALBERT_WING_BLASTER (spirit), ROSE_STORM 2, ALBERT_GASPLESS 3, JADE_DRAGON 5 | ALBERT_HARPOON |
| Meru | Retail | WATER | BLUE (20) | yes | 5: DOUBLE_SMACK (init), HAMMER_SPIN 21, COOL_BOOGIE 26, CATS_CRADLE 30, PERKY_STEP (mastery) | 4: FREEZING_RING (spirit), RAINBOW_BREATH 2, DIAMOND_DUST 3, BLUE_SEA_DRAGON 5 | DOUBLE_SMACK |
| Kongol | Retail | EARTH | GOLD (40) | yes | 3: PURSUIT (init), INFERNO 23, BONE_CRUSH (mastery) | **3**: GRAND_STREAM (spirit), METEOR_STRIKE **3**, GOLDEN_DRAGON 5 | PURSUIT |
| Miranda | Shana | LIGHT (inherited) | SILVER (inherited) | **null** (inherited) | **none** | 4: MIRANDA_MOON_LIGHT (spirit), MIRANDA_STAR_CHILDREN 2, MIRANDA_GATES_OF_HEAVEN 3, WHITE_SILVER_DRAGON 5 | — (none) |

Notes:

- The first (spirit-gated) spell of every dragoon-capable character is **pre-unlocked at creation** (`.unlock(gameState.timestamp_a0)` inside `makeCharacter`) — the spirit criterion on it is therefore only meaningful for re-unlock after a lock.
- The "mastery" final additions use `AdditionMasteryUnlockCriterion` and have `countsTowardsMastery = false` in the addition registry (M8-03), so the criterion is satisfiable (C27).
- Kongol's spell gates skip dlevel 2 (1/3/5) — the only character with a non-standard gate pattern; his 3-addition roster matches the 3-segment d-attack HUD documented in 05c §6.
- Shana and Miranda have **no additions at all** — the `scriptLevelUpAddition` guard (`selectedAddition_19 == null` → no-op, §8.3) exists precisely for them (and the 05b "No addition (Shana/???)" comment in `calculatePhysicalDamage`).
- Albert's spell list reuses retail spell 26 (ROSE_STORM) and 8 (JADE_DRAGON); Rose's dlevel-2 spell is DEATH_DIMENSION (16). The per-character spell *assignments* are the L4 extraction of the retail character→spell mapping (C32).
- Every template's `getXpToNextLevel` is `XP[level−1]` (60-entry table) with the extended path past 60; every `getDxpToNextLevel` is a 5-branch switch on dlevel (C14):
  - **Dart, Rose**: 1→1200, 2→6000, 3→12000, 4→20000, n≥5→20000+(n−4)×10000.
  - **Lavitz, Haschel, Shana, Miranda** (inherited): 1→1000, 2→6000, 3→12000, 4→20000, then the same series.
  - **Meru, Kongol**: 1→1000, 2→**2000**, 3→12000, 4→20000, then the same series.
- The 60-entry growth tables (XP/HP/ATK/DEF/MATK/MDEF, 9 × 6 columns of 60 values) are the templates' `static final int[]` constants (e.g. `ShanaTemplate.java:30–35`). They are retail values in the reference; the first/last XP entries per character: Dart 20 … 382000, Lavitz 35 … 387730, Shana 30 … 389640, Rose 20 … 390786, Haschel 20 … 385820, Meru 20 … 386584, Kongol 20 … 388494 (Albert = Lavitz's tables via inheritance; Miranda = Shana's). The tables are source-verified as reference data; the retail-byte source of each value is not verifiable from the repo (C9).
- The HP tables have visible step jumps at levels 11, 21, 41, 51 (e.g. Rose: 21×10, then 58, 78, 83, **181** at level 41, then 112) — a retail data shape, not interpreted here (C9).
- Past level 60 (L4, C10): each template's growth getters switch from the table to a closed form. Speed is granted once at level 0 (Shana 65, Lavitz 40; the others per their table row 0). The extended per-stat increments are `floor(level·k) − floor((level−1)·k)` with per-character coefficients `k` (attack/defense/magic-attack/magic-defense): Haschel 3.76/—/2.96/2.96, Kongol 5.08/5.08/1.5/1.6, Lavitz (Albert) 4.5/3.98/2.16/1.94, Meru 2.94/2.4/4.5/3.96, Rose —/2.84/2.84/—, Shana (Miranda) 2.08/2.98/5.1/5.08 (a "—" marks a getter whose extended form is a constant or absent in that template; exact per-getter forms are in the template sources, cited lines in §3.4's source list). Extended HP is a flat 385/level for the non-Dart templates; Dart's HP getter is piecewise (30 up to level 10, then rising — source-cited).
- The d-attack data (`getDragoonAddition`) is §6.5.

### 3.5 Template inheritance and the retail mirroring (C37)

Inheritance tree: `RetailCharacterTemplate` ← {`DartTemplate`, `LavitzTemplate` ← `AlbertTemplate`, `ShanaTemplate` ← `MirandaTemplate`, `RoseTemplate`, `HaschelTemplate`, `MeruTemplate`, `KongolTemplate`}.

`copy(from, to)` (default no-op) is the per-template mirror applied when a `CharacterData2c` is copied (`CharacterData2c.set` ends with `template.copy(other, this)`):

- **`AlbertTemplate.copy`**: swaps the five Lavitz additions for the `ALBERT_`-prefixed registry entries and three spells (WING_BLASTER→ALBERT_WING_BLASTER, BLOSSOM_STORM→ROSE_STORM, GASPLESS→ALBERT_GASPLESS). This models the retail fact that **Albert and Lavitz share one retail addition block** (`additionOffsets_8004f5ac` gives both the offset 8, C30) and nearly one spell set; the reference disambiguates with distinct registry IDs.
- **`MirandaTemplate.copy`**: swaps the three Shana light spells for the `MIRANDA_`-prefixed entries (10→65, 11→66, 12→67) — retail spells 10–13 are shared between Shana and Miranda in the retail table; the reference registers 65–67 as distinct entries (C32).
- **`DartTemplate.makeCharacter`** builds a base character via `super.makeCharacter`, then wraps it in a `DartCharacterData` and calls `character.set(original)` — the only non-identity `set` in normal play (the retail base class's `copy` is a no-op, so the wrap is pure reparenting plus the divine-spell list, C39).

### 3.6 `Legacy.CHAR_IDS` and the retail slot order (C4)

`Legacy.CHAR_IDS = [dart, lavitz, shana, rose, haschel, albert, meru, kongol, miranda]` — the retail character-slot → template mapping, used by the retail save loader to instantiate each of the 9 saved character records (C35). The new-game creation order is the same (C34). The `RETAIL_HP` table (9 × 60 cumulative max-HP values) is used only for the **save card** preview: `maxHp = RETAIL_HP[charIndices[0]][level−1]` (char 0 = Dart, `RetailSerializer.java:203`, `LegacySerializer.java:260`) (C36). Note rows 5 and 8 are byte-identical to rows 1 and 2 (Albert = Lavitz's HP curve, Miranda = Shana's) — the retail data sharing made explicit.

## 4. The stat system

### 4.1 The 17 registered stat types (C20)

`LodMod.java:162–179` registers the stat types (registry `lod_core:stat_types`; the registrations are also added to a `RegisterStatTypesEvent` for mods, `LodMod.java:489+`):

| registry entry | class | role |
|---|---|---|
| `hp`, `mp`, `sp` | `VitalsStat` (via `VitalsStatType`) | current + max, the vitals |
| `speed`, `attack`, `magic_attack`, `defense`, `magic_defense`, `attack_hit`, `magic_hit`, `attack_avoid`, `magic_avoid` | `UnaryStat` | single values (05b's power-stat sources) |
| `dragoon_attack`, `dragoon_magic_attack`, `dragoon_defense`, `dragoon_magic_defense` | `UnaryStat` | the dragoon-form stat multipliers (05b: applied as `effective × stat / 100` while `isDragoon()`, `PlayerBattleEntity.java:161–178`) |
| `guard_heal` | `UnaryStat` | the guard HP-recovery value (05b §7) |

### 4.2 The stat object model (C20, C21)

- **`Stat`** (abstract): `type` + back-reference to the owning `StatCollection` + `mods` — a `LinkedHashMap<RegistryId, StatMod>` keyed by the *mod's registry ID* (each distinct mod is a registry entry, M8-02). `turnFinished(bent)` iterates the mods calling `mod.turnFinished(...)` and then removes mods for which `isFinished(...)` — **the per-turn mod expiry is driven by the battle turn loop** (the buffs/debuffs documented in 05b/05c are these mods).
- **`UnaryStat`**: a raw `int value` + an `Int2IntFunction` validator. `get() = getRaw() + Σ mods.apply(...)` (additive). `getRawWithContributingMods()` = `value + Σ mods.apply` over only the mods with `contributesToOtherMods` — the base for chained (percentile-of-modified) mods.
- **`FractionalStat`**: `value` (current) + `max`; validator; `getCurrent()` lazily clamps current to the computed max (the clamp is on read so the max can be raised first); `getMax() = maxRaw + Σ mods.apply(...)` — **for vitals, stat mods apply to the *maximum***, while for unary stats they apply to the value; `restore(value)`/`restore()`/`isFull()`.
- **`VitalsStat`**: `FractionalStat` with validator `v → max(0, v)` (current can go negative in battle — 05c U35's negative-SP drain — and `getCurrent` clamps at 0 only through the validator at set time; the lazy max-clamp on read is the other bound).
- **`StatMod`** (interface): `getType()`, `copy()`, `apply(stats, type)` (the additive amount), `turnFinished`, `isFinished`, `contributesToOtherMods`. The two retail mod types (`UnaryStatModType`, `FractionalStatModType`) share the same config shape: **`amount`, `percentile`, `turns`, `contributesToOtherMods`**, serializable (MapTag/FileData) and **script-configurable** (`readConfigFromScript`: params 0–3 = amount / percentile / turns / contributesToOtherMods — the script-side `stat mod` API, M8-02's event surface).
- **`StatCollection`**: an insertion-ordered `LinkedHashMap<RegistryId, Stat>`; constructed from a `StatType...` varargs (the retail order, §3.3); `set(other)` copies stat-by-stat (`stat.copy(this)` — a *copy*, not a reference); `turnFinished(bent)` iterates all stats; `deserialize` (count + per-stat type ID + data). The collection is part of the character (§2.1) and is copied into the battle entity (§4.3).

### 4.3 The character→battle stat bridge (C22)

`Battle.initPlayerBattleEntityStats(player)` (per main-party player, at battle start, the LAB_800ef31c region):

1. `player.spells` = the character's **unlocked** spells (registry entries — the in-battle spell list).
2. **`player.stats.set(player.character.stats)`** — the battle entity gets a *copy* of the character's stat collection (stats and mods); battle-only stat changes (mods expiring, raw mutations) do not write back to the character. (Retail kept separate per-bent stat words in the 0x27c bent, 05b; the reference unifies them on the stat model — §14.)
3. `player.level_04 = character.level_12`, `player.dlevel_06 = character.dlevel_13`, `player.status_0e = character.getStatusAndFlags()` (the 0x2000 spirit bit, C3), `player.selectedAddition_58 = character.selectedAddition_19`, `player.addition` = the selected addition's registry entry.
4. Per equipped slot (`Battle.java:8436–8460`): copy the equipment into `player.equipment_11e`; `specialEffectFlag_14 |= equipment.flags_00` (the 05c equipment special-effect flag — the d-attack/addition gating); `equipmentAttackElements_1c += equipment.attackElement_04` (the equipment *attack elements*, §7.3); `equipmentElementalResistance_20`/`equipmentElementalImmunity_22` += the equipment's sets; `equipmentStatusResist_24 |= equipment.statusResist_08`; and **flat additive raw increments** for attack, magic attack, defense, magic defense, attack hit, magic hit, attack avoid, magic avoid, guard heal (each `stat.setRaw(raw + equipment.<field>)`), plus `equipmentAttack1_28 += equipment.attack_10`. **Equipment stats are baked additively at battle start — they are not live `StatMod`s** (C22).
5. The power stats (`powerAttack_b4`, `powerMagicAttack_b6`, `powerDefence_b8`, `powerMagicDefence_ba`, the hit/avoid powers) are then derived from these — 05b §3 documents that derivation; the per-hit SP/MP gain fields (`PlayerBattleEntity` 0xcc–0xd3 and the `original`/current pairs at 0x128–0x130) are the retail attack SP/MP economy (05b/05c).

## 5. Level progression (XP → level)

### 5.1 The threshold semantics (C9)

`getXpToNextLevel(character) = XP[character.level_12 − 1]` (60-entry table): the value is the **absolute XP required to advance from the current level** (retail-style: `xp_00` is compared, not decremented — the level-up loop §10.2 tests `xp_00 >= getXpToNextLevel()`). Table row 0 is applied at creation (level 0→1, C7a); the table covers levels 1–60 (row L−1: the requirement to leave level L).

### 5.2 The level-up pipeline (C7)

`applyLevelUp` (§3.3) is the whole level-up: the six stat increments (table-indexed by the level being left), the level increment, and the unlock check. It does **not** consume XP. `LevelUpSource` distinguishes `INITIALIZATION` (creation, new-game level-up, save-load replay), `GAMEPLAY` (the post-battle report), and `DEBUGGER` (the character editor) — the source is carried on the Pre/Post events so mods can distinguish contexts; no reference behavior currently keys off it beyond the event payload.

### 5.3 The level-up actions (C13)

`LevelUpActions` is an ordered list of `(LevelUpAction, options)` entries; `run(character)` calls each `action.apply(character, options)` and returns the list for display. The four registered actions (`LodLevelUpActions`: `level_up`, `dragoon_level_up`, `unlock_addition`, `unlock_spell`) have **no-op `apply`** (e.g. `LevelUpLevelUpAction.apply` is `// no-op`) — they are *display records* of what happened, consumed only by `PostBattleScreen.applyLevelUpActions` (which filters the entries by `instanceof UnlockAdditionLevelUpAction`/`UnlockSpellLevelUpAction` and fills the `additionsUnlocked_8011e1b8`/`spellsUnlocked_8011e1a8` display maps). The options carry the unlocked registry ID (`UnlockAdditionLevelUpActionOptions.additionId`, `UnlockSpellLevelUpActionOptions.spellId`).

### 5.4 Where `checkUnlocks` runs (C29)

- At every `applyLevelUp`/`applyDragoonLevelUp` (§3.3).
- At report start, for **every** character, via `PostBattleScreen.checkForUnlocks()` (so a spirit gained in battle unlocks the first spell at report time even with no level-up).
- At `addAddition` (a newly added addition that already meets its criteria unlocks immediately, timestamped `timestamp_a0 + additions.size()`).
- At save-load (C35).

### 5.5 Extended levels (L4) (C10)

The retail max level is 60. The reference extends it: `MAX_LEVEL` config (default 60, range 1–250, `CAMPAIGN`/`GAMEPLAY` storage) caps the level-up loops; past the table, `getXpToNextLevel = XP[59] + getExtendedXp` with `getExtendedXp = Σ (23000 + i×1000)` (C10), and the growth getters switch to their closed forms (§3.4). All of this is L4 (no retail counterpart).

## 6. Dragoon (spirit) progression

### 6.1 Spirits are goods (C17)

`LodGoods` registers the eight dragoon spirits as `Good`s with retail item IDs: RED 10, BLUE 20, JADE 30, GOLD 40, VIOLET 50, SILVER 60, DARK 70, DIVINE 80 (plus `GOOD_SPIRITS` 130 and `GOLD_DRAGOON_SPIRIT_2` 340). `hasDragoon()` = the character's `getDragoonSpirit()` present in `gameState.goods_19c` (Dart: RED, with the DIVINE spirit additionally qualifying, §6.6). There is no per-character "has dragoon" flag — the goods inventory is the single source of truth, and `goods` is a campaign-level inventory (M7-01).

### 6.2 The DXP tables (C14)

`getDxpToNextLevel` per template (§3.4 table): the threshold **to leave** the current dlevel. dlevel starts at 1 at creation (C7a), so `dlevelXp_0e` is compared against the dlevel-1 row (1200 for Dart/Rose, 1000 for the others).

### 6.3 The dragoon-level-up pipeline (C8)

`applyDragoonLevelUp` (§3.3): MP +20, SP +100, and the four DRAGOON_* stat increments (table-indexed by the dlevel being left), then `dlevel_13 += 1`, then `checkUnlocks` (the dlevel-gated spells unlock here — that is the *only* unlock path for the dlevel-2/3/5 spells in normal play).

### 6.4 The DXP sources (C15, C16)

DXP has exactly two sources in the reference, both native routines:

1. **In-battle SP grants** — `Battle.scriptGiveSp(bentIndex, amount)` @ 0x800f43dc: `sp.setCurrent(current + amount)` **and** `spGained_800bc950.mergeInt(character, amount)` (a battle-global `Object2IntMap<CharacterData2c>`, cleared at battle start `Battle.java:8323`). Every scripted SP grant to a player queues the same amount as pending DXP. (The L4 combat script's SP-grant paths — the 05c addition/attack grants — are the callers.)
2. **SP items** — `SItem.addSp(charIndex, amount)` @ 0x80022cd0: if SP is full → −2; `amount == −1` fills to max; otherwise `spToAdd = min(amount, max − current)` while **`dxpToAdd = amount` (the full requested amount, even when the SP gain is clamped)**; `sp.restore(spToAdd)`, `dlevelXp_0e += dxpToAdd`, then the dragoon level-up loop `while (dxp >= getDxpToNextLevel() && dlevel < MAX_DRAGOON_LEVEL) applyDragoonLevelUp(null)`.

Post-battle, the queued DXP is drained by `PostBattleScreen.givePendingDxp()` (10/frame, CONFIRM to skip) with the same level-up loop and the `dragoonLevelsGained` tracking (C16). `MAX_DRAGOON_LEVEL` (default **5**, range 1–50, `ScriptReadable` — the script can read the cap) bounds both loops.

### 6.5 The d-attack data (C38)

Each dragoon-capable template's `getDragoonAddition(character)` returns an `AdditionHits80` of **8 hits**: 6 active (power pattern 100/10/20/30/40/0 in `damageMultiplier_04`, `flags_00 = 0xc0`, with per-character `_01`/`_02`/`_06`/`_09`/`_0a`/`_0b`/`_0c`/`_0d`/`_0f` property words and per-hit sound tables) + 2 silent tail hits (`flags_00 = 0x0`, all-zero data; the tail carries only sounds where present). Kongol's is sparse (no per-hit sounds on most hits). This is the data 05c §6's "daddy" HUD and the d-attack script consume; the per-hit *damage* arithmetic lives in the retail d-attack DEFF (05c open item 1). **Dart's variant (C39)**: with the divine spirit, the 6 active hits become plain 200/20/40/60/80 with no property words. Shana/Miranda return `null` (no d-attack).

### 6.6 The divine-dragoon variant (C39)

`DartCharacterData` (Dart's `CharacterData2c` subclass) flips behavior when `goods_19c.has(DIVINE_DRAGOON_SPIRIT)`:

- `getStatusAndFlags()` adds `0x4000` (the 05c U28 divine marker).
- `getUnlockedSpells()`/`getAllSpells()`/`getSpellInfo()` switch to the **divine spell list** — `DIVINE_DG_BALL` (retail 9) and `DIVINE_DG_CANNON` (retail 4), both DIVINE-element, added **pre-unlocked at construction** (the red list stays in the base `spells` map).
- `hasTransformed()` reports false while the divine spirit is present (the divine form is not a "transform" in the flag's sense).
- `DartTemplate.getElement(character, bent)` returns DIVINE instead of FIRE when `bent.isDragoon()` and the divine spirit is present — the divine element is *dragoon-form-only* (the bent-aware override is the only one among the 9 templates).
- The d-attack data and the transform icon follow the same switch (§3.4, `renderTransformIcon` picks the DIVINE icon set when the divine spirit is present).

The retail 10th pseudo-character slot (the "DD" row of `CHARACTER_ADDITIONS`/`additionOffsets_8004f5ac`, C30) and the d-attack HUD's charId-9 special case (05c U28) are consistent with a retail divine-dragoon entity; the reference models it as a *runtime flip of Dart* rather than a 10th character (C39; the charId-9 identification remains Inferred, 05c U28).

### 6.7 The transform gates (C18)

The battle menu builder (`LodMod.java:700–837` — the L4 core mod's re-implementation of the retail menu, 05c §2 for the menu surface):

- The **TRANSFORM** (transform) action is offered when `!player.isDragoon() && SP ≥ 100 && player.canBecomeDragoon()` (the 0x2000 spirit bit).
- The **SPECIAL** action (all-party transform) is additionally offered when `canSpecialTransform()`: ≥ 3 players in the battle party; every player alive; **every player has `dlevel ≥ 1` and `SP ≥ dlevel × 100`**; no player currently in dragoon form; every player `canBecomeDragoon()`. (A dlevel-5 dragoon therefore needs 500 SP for the all-party transform.)
- In dragoon form the menu offers SPELLS (always) and TRANSFORM-as-detransform only under `EXTENDED_DRAGOON_ACTIONS` (default true; vanilla preset false); GUARD/ITEMS/ADDITIONS/ESCAPE in dragoon form are likewise `EXTENDED_DRAGOON_ACTIONS`-gated.
- The retail forced-action gating for the tutorial fights (the `battleState._548`/`_54c` words, 05c U31) is implemented natively in this same builder (`LodMod.java:759–787`, "This seems to be for the tutorial fights") — it disables all actions and re-enables one per tutorial step (ATTACK/ITEMS at `_548` 0/1; TRANSFORM at `_54c` 5/9/16; D_ATTACK at 12; SPELLS otherwise), setting `FLAG_TAKE_FORCED_TURN` on the tutorial enemy. In the reference the words are never set (05c U31), so this native handler is dormant.

### 6.8 `hasTransformed` (C19)

A `CharacterData2c` boolean (L4 storage). Writers: the bent script-enum case `HAS_TRANSFORMED_BEFORE` (`PlayerBattleEntity.java:591` — the L4 combat script sets the character's flag during the transform flow) and the save loaders. **Retail backing**: on retail-save load, `hasTransformed` for characters 0–8 is read from `scriptFlags2` bits `0x1b8, 0x1ba, 0x1b9, 0x1bb, 0x1bc, 0x1bd, 0x1be, 0x1bf, 0x1c0` respectively (the non-contiguous bit assignment is the retail one, `RetailSavedGame.java:240–248`). The consumer is the 05b `HAS_TRANSFORMED_BEFORE` bent word and the story-script checks.

## 7. Elements

### 7.1 The registry (C23)

9 elements (registry `lod_core:elements`, `LodMod.java:186–194`):

| entry | flag | colour (RGB) | class | opposition model |
|---|---|---|---|---|
| `none` | 0x0 | white | `NoElement` | no strong, no weak, no boost |
| `water` | 0x1 | (0x4c,0xb7,0xe1) | `SimpleElement` | strong vs FIRE; weak vs self |
| `earth` | 0x2 | (0xb6,0x70,0x00) | `SimpleElement` | strong vs WIND; weak vs self |
| `dark` | 0x4 | (0x19,0x0f,0x80) | `SimpleElement` | strong vs LIGHT; weak vs self |
| `divine` | 0x8 | (128,128,128) | `DivineElement` | no strong/weak; boosts self only |
| `thunder` | 0x10 | (0x81,0x09,0xec) | `ThunderElement` | **no strong**; weak vs self; boosts self |
| `light` | 0x20 | (0xd5,0xc5,0x3a) | `SimpleElement` | strong vs DARK; weak vs self |
| `wind` | 0x40 | (0x48,0xff,0x9f) | `SimpleElement` | strong vs EARTH; weak vs self |
| `fire` | 0x80 | (0xee,0x09,0x09) | `SimpleElement` | strong vs WATER; weak vs self |

`SimpleElement(opposed)`: `isStrongAgainst` = other is the opposed element, `isWeakAgainst` = other is self, `boostsElement` = other is self. The flag integers are the retail packed-element bits (the deprecated `Element.flag`/`ElementSet.pack`/`unpack` API keeps the legacy single-word form). Because each pair's elements name each other as opposed (water's opposed is fire *and* fire's opposed is water, likewise earth↔wind and dark↔light), all three `SimpleElement` pairs are **mutually strong** (each deals ×150/100 to the other); only thunder has no strong target at all (and only divine has no strong/weak). The effective retail chart (C23): an attack deals **×150/100** if the attacker's element is strong against the defender's, **×50/100** if the defender's element equals the attacker's (self-weakness), else ×100/100.

### 7.2 The three damage-adjustment paths (C23, C24)

`Element` implements the three adjustments, consumed by the two damage pipelines (05b §4–§6):

- `adjustAttackingElementalDamage(attackType, damage, targetElement)`: strong → ×150/100; weak → ×50/100; else identity. (The attacker's element vs the defender's element.)
- `adjustDefendingElementalDamage(...)`: **identity** (retail fact — no defender-side elemental interaction).
- `adjustDragoonSpaceDamage(attackType, damage, attackingElement)`: `boostsElement(attacking)` (i.e. attacker element == the space element) → ×150/100; `attacking.isStrongAgainst(this)` → ×50/100; else identity. **Reversed direction from the normal path**: in dragoon space the matching element is boosted, and the element *strong against* the space is halved.

Consumers: the magic branch of `Battle`'s damage routine (`Battle.java:8605–8614`: `adjustAttacking…`, `adjustDefending…`, then `if (dragoonSpaceElement != null) adjustDragoonSpaceDamage(…)`), and `PhysicalDamageFormula.applyElementalInteractions`/`applyDragoonSpace` (the formula pipeline stages — the loops apply the adjustment **per attack element**, §7.3).

**Dragoon space**: `Battle.dragoonSpaceElement_800c6b64` (nulled at battle init `:8319`) is set exclusively by `scriptSetDragoonSpaceElementIndex(charId)` @ 0x800f9b78 — `charId != −1` → the element of that player (`battleState.getPlayerById(charId).getElement()`), `−1` → clear. The L4 d-attack/transform scripts are the callers; the retail scripts are not in the repo (C24).

### 7.3 Character and attack elements (C25, C26)

- **Character element**: `CharacterData2c.getElement([bent])` posts `ResolveCharacterElementEvent` (a mod hook; default `event.element` = `template.getElement(...)`). The template element is the per-character constant (§3.4) except Dart's bent-aware divine switch (C39).
- **Attack elements**: `PlayerBattleEntity.getAttackElements()` = `equipmentAttackElements_1c` — the union of the character's element (the bent's `getElement()`) plus each equipped item's `attackElement_04` set, accumulated at battle init (§4.3). The elemental adjustment loops therefore apply once per attack element (a weapon can add an element). Monsters' attack elements are their current spell's `element_08` (`MonsterBattleEntity.getAttackElements`).
- **Script-readable elements**: `Element.read(index, out)` — 0 = colour as `0xBBGGRR`, 1 = is-strong-against(out element), 2 = is-weak-against(out element), 3 = boosts(out element) (the out param must hold another element's registry ID). The L4 script can query the whole chart.
- The element-icon order for the dragoon-spirit UI: fire, wind, light, dark, thunder, water, earth, divine (`LodMod.DRAGOON_SPIRIT_ICONS`).

## 8. Addition progression

### 8.1 The per-character rosters and criteria (C27)

The rosters/unlock levels are §3.4's table. The criteria classes:

- `AdditionLevelUnlockCriterion(level)`: met when `character.level_12 >= level`.
- `AdditionMasteryUnlockCriterion` (no args): met when **every addition on the character with `countsTowardsMastery` is `isComplete`** (level ≥ max). Vacuously true for characters with no additions (Shana/Miranda — they have none to begin with).

The final (ultimate) additions of each character have `countsTowardsMastery = false` in the registry (M8-03) — otherwise the criterion that unlocks them could never be met. All retail additions have exactly 5 `LevelMultipliers` (max level 5) and 2–8 hits (the 05c U32 census: DOUBLE_SLASH 2 … MOON_STRIKE 7, BLAZING_DYNAMO 8).

### 8.2 The unlock model (C29)

`CharacterAdditionInfo`: a criterion list; `unlockState` ∈ `UnlockState { UNLOCKABLE (auto-unlock when criteria met), UNLOCKED (usable), LOCKED (never) }` (default UNLOCKABLE); `unlockTimestamp` (−1 until usable); `level` (**starts at 1** — an addition is level 1 the moment it is added; its XP/mastery starts at 0); `xp` (mastery counter). `checkUnlock` = state is unlockable **and** all criteria met (never re-unlocks). `unlock(timestamp)` sets UNLOCKED + timestamp. `addAddition` auto-unlocks an already-satisfied criterion set (timestamp `timestamp_a0 + additions.size()`).

### 8.3 The mastery economy (C28)

- **Producer (in battle)**: `Battle.scriptLevelUpAddition(bentIndex, enable)` @ 0x800cd200 — with `enable != 0`, `charData.getAdditionInfo(selectedAddition_19).xp++` (guarded: `selectedAddition_19 == null` → no-op — Shana/Miranda). The 05c addition driver calls it per resolved hit (the "mastery tick" of 05c §4.4).
- **Level-up (at report start)**: `PostBattleScreen` `:167–176` — for every character, every addition: `while (info.level < addition.getMaxLevel(char, info) && info.xp >= addition.getXpToNextLevel(char, info)) { info.level++; post PostAdditionLevelUpEvent(char, additionId, info) }`.
- **The formula** (`SimpleAddition`): `getXpToNextLevel = info.level × 20` (20, 40, 60, 80 for levels 1–4); `getMaxLevel = levelMultipliers.length` (5); `isComplete = level >= 5`. **XP is cumulative, never consumed** — the writer census is `xp++` (script) and save-load assignment; nothing decrements it.
- The level affects battle data through the addition registry entry: `getDamageMultiplier`/`getSpMultiplier` = `levelMultipliers[level−1].damage/sp` (05c §4: the per-level damage/SP multipliers), and `getDamage`/`getSp` = the sum of the hits' `damageMultiplier_04`/`sp_05` times that multiplier.
- The mastery *state* gates only the ultimate addition's unlock (§8.1) — there is no separate "mastery" flag beyond the per-addition levels.

### 8.4 The retail addition table and offsets (C30)

`Scus94491BpeSegment_8004.CHARACTER_ADDITIONS` (10 rows, comment "Dart, Lavitz, Shana, Rose, Haschel, Albert, Meru, Kongol, Miranda, DD"): the per-character retail addition lists — Dart 7, Lavitz 5, Shana 0, Rose 4, Haschel 6, Albert 5 (the `ALBERT_` IDs), Meru 5, Kongol 3, Miranda 0, **DD (the 10th pseudo-character) 0** — and `additionOffsets_8004f5ac = {0, 8, −1, 14, 29, 8, 23, 19, −1, 0}`: each character's offset into the retail 35-entry addition table (Shana/Miranda −1 = none; **Albert reuses Lavitz's block at offset 8** — the retail sharing behind the §3.5 mirroring). The 35-entry table's implied layout is [Dart 0–6, Lavitz/Albert 8–12, Rose 14–17, Kongol 19–21, Meru 23–27, Haschel 29–34] with five reserved slots (7, 13, 18, 22, 28 — Inferred from the offset gaps; their retail meaning is unresolved). The table is used by the retail-save merge (C35) and the `SeveredSavedCharacterV1` loader.

## 9. Spell progression

### 9.1 The retail spell struct (C31)

`SpellStats0c` (abstract, a `RegistryEntry` + `ScriptReadable`) — the 12-byte retail spell record:

| field | meaning |
|---|---|
| `targetType_00` | observed values 0x40/0x48/0x80/0x88/0xc0/0xc8; documented bit: **0x8 = attack all** (in-source javadoc); the other bits are not fully decoded (C31) |
| `flags_01` | **0x4 = either buff spell or always hit (or both)** (in-source javadoc) |
| `specialEffect_02` | per-spell effect word (values 0/2/0x10/0x20 — semantics not decoded) |
| `damageMultiplier_03` | damage scaling word (0/0x8/0x10/0x20 — the dragoon spells carry 0x10–0x20 patterns) |
| `multi_04` | 0 for most damage spells; 100/50 on the buff spells (MOON_LIGHT 100, GATES_OF_HEAVEN 50, RAINBOW_BREATH 50) — read as the buff magnitude (Inferred, C31) |
| `accuracy_05` | 100 for all retail spells |
| `mp_06` | MP cost (10/20/30 for the minor spells, 80 for the dragon spells, 50 for the divine spells, 0 for the unassigned placeholders) |
| `statusChance_07` | status-inflict chance (100 on DEATH_DIMENSION/DEMONS_GATE/THUNDER_KID; 0 elsewhere) |
| `element_08` | the spell element (registry delegate) |
| `statusType_09` | status type (0x8 on DEATH_DIMENSION, 0x10 on THUNDER_KID, 0 otherwise — the 05b status-ailment model) |
| `buffType_0a` | buff stat bits (in-source javadoc: 0x80 def up, 0x40 mdef up, 0x20 atk up, 0x10 matk up, 0x8 def down, 0x4 mdef down, 0x2 atk down, 0x1 matk down; observed 0/0x8/0x10/0xc0) |
| `_0b` | unresolved (0/0x2/0x8/0x20/0x80/0xc0 in the data) |

`ScriptReadable.read(0, out)` = `getBattleStage()` (−1 if the spell doesn't change the battle stage). `RetailSpell` adds `loadDeff` (§9.4); `DragonSpell` (the dlevel-5 spells) adds `battleStage`.

### 9.2 The retail spell registry (C32)

63 registered entries: retail indices 0–59 plus 65–67 (60–64 unregistered). Named assignments:

| index | spell | element | MP | assigned to |
|---|---|---|---|---|
| 0–3 | FLAMESHOT, EXPLOSION, FINAL_BURST, RED_EYED_DRAGON | FIRE | 10/20/30/80 | Dart |
| 4 | DIVINE_DG_CANNON | DIVINE | 50 | Dart (divine list, pre-unlocked) |
| 5–8 | WING_BLASTER, GASPLESS, BLOSSOM_STORM, JADE_DRAGON | WIND | 20/30/20/80 | Lavitz (Albert via 14/17/26/8) |
| 9 | DIVINE_DG_BALL | DIVINE | 50 | Dart (divine list, pre-unlocked) |
| 10–13 | STAR_CHILDREN, MOON_LIGHT, GATES_OF_HEAVEN, WHITE_SILVER_DRAGON | LIGHT | 20/10/30/80 | Shana (Miranda via 65–67 + 13) |
| 14–19 | ALBERT_WING_BLASTER, ASTRAL_DRAIN, DEATH_DIMENSION, ALBERT_GASPLESS, DEMONS_GATE, DARK_DRAGON | WIND/DARK/DARK/WIND/DARK/DARK | 20/10/20/30/30/80 | Albert (shares 15/16/18/19 with Rose's list) |
| 20–23 | ATOMIC_MIND, THUNDER_KID, THUNDER_GOD, VIOLET_DRAGON | THUNDER | 10/20/30/80 | Haschel |
| 24–28 | FREEZING_RING, RAINBOW_BREATH, ROSE_STORM, DIAMOND_DUST, BLUE_SEA_DRAGON | WATER/WATER/WIND/WATER/WATER | 10/20/20/30/80 | Meru (26 is Albert's dlevel-2 spell) |
| 29–31 | GRAND_STREAM, METEOR_STRIKE, GOLDEN_DRAGON | EARTH | 20/30/80 | Kongol |
| 32–37 | (unassigned placeholders, NO_ELEMENT, power 0) | — | 0 | — |
| 38–59 | (empty retail slots, NO_ELEMENT, power 0) | — | 0 | — |
| 65–67 | MIRANDA_STAR_CHILDREN, MIRANDA_MOON_LIGHT, MIRANDA_GATES_OF_HEAVEN | LIGHT | 20/10/30 | Miranda (duplicates of 10–12) |

`Legacy.CHARACTER_SPELLS` (the save-merge table) fixes the per-character retail spell order: Dart {0,1,2,3}, Lavitz {5,7,6,8}, Shana {11,10,12,13}, Rose {15,16,18,19}, Haschel {20,21,22,23}, Albert {14,26,17,8}, Meru {24,25,27,28}, Kongol {29,30,31}, Miranda {66,65,67,13}. Unlock criteria are §3.4's table: all four slots are spirit-gated; the first is pre-unlocked at creation; the others gate on dlevel 2/3/5 (Kongol 3/5).

### 9.3 The dragoon spells and battle stages (C33)

The dlevel-5 spells are `DragonSpell`s with a `battleStage`: RED_EYED_DRAGON 71, JADE_DRAGON 72, BLUE_SEA_DRAGON 73, DARK_DRAGON 74, WHITE_SILVER_DRAGON 75, GOLDEN_DRAGON 76, VIOLET_DRAGON 77 — the stage the battle switches to for the dragon attack (05a §5's battle-stage model).

### 9.4 Dragoon-spell DEFF loading (C33)

`RetailSpell.loadDeff` (the `ScriptDeffEffect` path, 05b/05c for the DEFF machinery): `deffManager.flags_20 |= dragoonDeffFlags_800fafec[index] << 16` (per-spell static flag table); `allocateDeffEffectManager(...)`; if `flags_20 & 0x40000` → `loadDeffSounds` (with the retail special case `index != 46 || entrypoint != 0 ? 0 : 2` — index 46 is one of the unregistered retail slots); for each index in `dragoonDeffsWithExtraTims_800fb040` → extra TIMs from `SECT/DRGN0.BIN/(4115 + i)`; then `loadDeff(SECT/DRGN0.BIN/(4139 + index×2), SECT/DRGN0.BIN/(4140 + index×2))` — **the retail dragoon-spell DEFF pair is addressed as 4139/4140 + 2×retail-index** (source-verified; the DEFF contents are retail binaries, M2-04/05a B18).

## 10. The post-battle progression report (the consumer)

`PostBattleScreen` (05c §8 for the surrounding action dispatch and screen lifecycle) runs the progression grant. Report start (`:129–176`): clears the display maps, builds the portraits, then computes the grant pools (§10.2); state `CHECK_NEXT_THING_TO_GIVE` runs `checkForUnlocks()` (§5.4) and then sequences the display: gold → XP/DXP tick → level-ups → (the remaining states: dragoon levels, addition unlocks, spell unlocks, per the state enum — retail state numbers 3/4/5/8 visible in the source).

### 10.1 Determinism note

The report is a frame-driven animation (10 units/frame, button-skip) over deterministic pools — no RNG (C11, §13).

### 10.2 The XP pool (C12, C11)

- The per-enemy reward values (`xp_194`, `gold_196`, drops) are loaded per combatant from the `enemyRewards_80112868` table by `loadEnemyDropsAndScript` @ 0x80109808 (`Battle.java:9319–9345`), which posts `EnemyRewardsEvent` (M8-02) — mods may rewrite the xp/gold/drops before they land on the combatant.
- `totalXpFromCombat_800bc95c` (battle global, reset at battle start `Battle.java:1972`) is accumulated in `scriptSetBentDeadAndDropLoot` @ 0x800ccf2c (`Battle.java:4315–4332`) when the L4 combat script marks a monster dead: `FLAG_DEAD` set, and if `FLAG_MONSTER` — `goldGainedFromCombat_800bc920 += gold_196`, `totalXpFromCombat_800bc95c += xp_194`, then the item drops are rolled with `simpleRand()` unless `FLAG_NO_LOOT`. It is also a **script variable** (game-var case 61, read+write — a script can adjust the pool).
- `livingChars_800bc968` (filled at the post-combat camera-fade start — frame 0 of the post-combat action, `Battle.java:2435–2444`, from `battleState.alivePlayerBents_eac`) is the living-main-party list.
- `xpDivisor` = |living main party|; each living main-party character gets `totalXpFromCombat / xpDivisor` (integer division); each **secondary** character gets `(int)(safeDiv(totalXpFromCombat, xpDivisor) × SECONDARY_CHARACTER_XP_MULTIPLIER)` (config, default **1.0**; the vanilla preset models retail at **0.5** — C12; the 0.5 is a reference modeling choice, not a verified retail constant).
- `givePendingXp()` (10/frame, CONFIRM-skips): `xp_00 += chunk`; then `while (xp_00 >= getXpToNextLevel() && level < MAX_LEVEL) { applyLevelUp(actions, GAMEPLAY); levelsGained++ }`; `applyLevelUpActions` (C13).

### 10.3 The DXP pool (C16)

`givePendingDxp()` drains `spGained_800bc950` (10/frame, CONFIRM-skips): `dlevelXp_0e += chunk`, `spGained.mergeInt(char, −chunk)` (the SP-bar drain visual), then the dragoon level-up loop (≤ `MAX_DRAGOON_LEVEL`) with `dragoonLevelsGained` tracking.

### 10.4 Addition levels and unlock display

At report start (before the ticks): the addition level-up loop (§8.3) + `PostAdditionLevelUpEvent`. At `CHECK_NEXT_THING_TO_GIVE`: `checkForUnlocks()` for every character; `applyLevelUpActions` collects the `UNLOCK_ADDITION`/`UNLOCK_SPELL` entries into the `additionsUnlocked_8011e1b8`/`spellsUnlocked_8011e1a8` maps for the unlock-display states.

## 11. Character lifecycle: new game, equipment, saves

### 11.1 New game (C34)

`RetailCampaignType.setUpNewCampaign` (the retail campaign type, registered in the campaign registry — M7-01):

- Creates the 9 characters in `CHAR_IDS` order (`charIds_88.add(0)` — the main party starts with Dart alone).
- Starting levels via the `levelUp(char, level)` helper: Lavitz 3, Shana 4, Rose 8, Haschel 13, Albert 15, Meru 17, Kongol 19, Miranda 23 (Dart stays 1). The helper: `while (level < target−1) applyLevelUp(INITIALIZATION)`; then `xp_00 = getXpToNextLevel()` (at the level *below* the target — so a new-game character at level L carries `xp = XP[L−2]`, i.e. partial progress into level L); then one final `applyLevelUp`; then HP/MP restore. (Source-verified L4; the retail starting levels/xp are its data.)
- `dart.partyFlags_04 = IN_PARTY | CAN_BE_IN_PARTY | CANT_REMOVE` (0x23). (The other characters' initial party flags are set by the campaign scripts — the retail `char_utils` path, C5.)
- Starting items: BURN_OUT ×1, HEALING_POTION ×2 (the "burn_out" item of 05c §4.6 — the d-attack SP item, retail ID 4313/0xc3).
- Starting equipment (per character, 5 slots): Dart BROAD_SWORD/BANDANA/LEATHER_ARMOR/LEATHER_BOOTS/BRACELET; Lavitz SPEAR/SALLET/SCALE_ARMOR/LEATHER_BOOTS/BRACELET; Shana SHORT_BOW/FELT_HAT/CLOTHES/LEATHER_SHOES/BRACELET; Rose RAPIER/FELT_HAT/LEATHER_JACKET/LEATHER_SHOES/BRACELET; Haschel IRON_KNUCKLE/ARMET/DISCIPLE_VEST/IRON_KNEEPIECE/BRACELET; Albert SPEAR/SALLET/SCALE_ARMOR/LEATHER_BOOTS/BRACELET; Meru MACE/TIARA/SILVER_VEST/SOFT_BOOTS/BRACELET; Kongol AXE/ARMET/LION_FUR/IRON_KNEEPIECE/BRACELET; Miranda SHORT_BOW/FELT_HAT/CLOTHES/LEATHER_SHOES/BRACELET.
- `gold_94 = 20`; `transitionToNewCampaign` plays FMV 2 into SUBMAP (02-title).

### 11.2 Equipment (C22)

`CharacterData2c.equip(slot, equipment)`: the outgoing item's `onUnequip(character)`, the incoming item's `onEquip(character)` (the equipment's character-side hooks — M9-07 for the item/equipment data and the `EQUIP_EFFECTS_IN_DRAGOON` config). Compatibility: `canEquip(slot, equipment)` = the template's `canEquip` (retail: all 9 return `CanEquip.NORMAL`) AND the equipment's own `canEquip` — `DENY` wins, `FORCE` wins, otherwise the `EquipmentTypes` sets (character types ∩ equipment types non-empty) decide (M9-07 for the type tables). The battle-side effect of equipment is §4.3's flat additive transfer (no live mods).

### 11.3 Saves (C35)

- **L4 (Severed V2)**: per-character tag fields (§3.3); the stats collection serializes its mods (amount/percentile/turns/contributesToOtherMods) — M7-01 for the container.
- **Retail save load** (`RetailSavedGame.createGameState`): per character slot 0–8, `template = characterTemplates[CHAR_IDS[slot]]`, `template.make(gameState)`, then the **replay**: `while (level < saved.level) applyLevelUp(INITIALIZATION)` and `while (dlevel < saved.dlevel) applyDragoonLevelUp(null)` (rebuilding the derived stats/unlocks), then the direct overwrites (`xp_00`, `partyFlags_04`, HP/MP/SP current, `dlevelXp_0e`, `status_10`), per-slot equipment (unknown retail IDs skipped with a warning), and the **addition/spell merge**: retail entries first (in the `CHARACTER_ADDITIONS`/`CHARACTER_SPELLS` order, `unlock(timestamp_a0 + i)` when the criteria are met after the replay — deterministic ordering), then any non-retail (mod) entries. `hasTransformed` from the `scriptFlags2` bits (§6.8). The save card `maxHp` from `RETAIL_HP` (§3.6).
- **Legacy V1** (`SeveredSavedCharacterV1`): the same replay pattern (its `:82` calls `applyLevelUp(INITIALIZATION)` in the level loop; its `:105` uses `CHARACTER_ADDITIONS[index]`).

## 12. RQ7 — the script/native/data boundary for this surface

| behavior | native (L3) | script | data (registry/ROM) |
|---|---|---|---|
| Character/stat model, the 0x2c struct | ✓ (the struct, the flags, the status bits) | party writes via var 115–123 (`PartyFlagsChangeEvent`) | — |
| Level-up pipeline (stat increments, unlock check) | ✓ (`applyLevelUp` — L4 re-architecture of the retail per-character tables) | — | the 60-entry XP/growth tables (per template) |
| XP distribution and grant | ✓ (`PostBattleScreen`, `totalXpFromCombat`, `livingChars`, `scriptSetBentDeadAndDropLoot` kill accumulation) | the L4 death handler drives the kill accumulation; var case 61 can read/write the pool; `EnemyRewardsEvent` (mod) | the `enemyRewards_80112868` per-enemy table (05a) |
| Dragoon-level-up pipeline | ✓ (`applyDragoonLevelUp`) | — | the DXP tables (per template) |
| DXP sources | ✓ (`scriptGiveSp`, `SItem.addSp`) | the L4 combat script's SP-grant calls are the producers | the spirit goods (IDs 10–80) |
| Transform/special-transform gates | ✓ (the menu builder, `canSpecialTransform`) | — | `EXTENDED_DRAGOON_ACTIONS` (L4) |
| Element resolution and damage adjustments | ✓ (the adjustments, the dragoon-space word) | `scriptSetDragoonSpaceElementIndex`; element `ScriptReadable` queries | the template elements; the element registry (opposition/colour/flags) |
| Addition unlock + mastery | ✓ (`checkUnlocks`, the report loop, `scriptLevelUpAddition`) | the addition driver's mastery ticks (05c §4.4) | the per-character rosters + criteria; the addition registry (multipliers, hits, `countsTowardsMastery`) |
| Spell unlock | ✓ (`checkUnlocks` — the dlevel path) | — | the per-character spell lists + dlevel gates; the spell registry (12-byte records, DEFF addresses) |
| Party membership (story-driven) | the flags + the var-115–123 landing pad | the retail `char_utils` script (not in the repo) writes them | the `CANT_REMOVE`/`CAN_BE_IN_PARTY` constants; the char-swap rules |
| `hasTransformed` | the `scriptFlags2` bits on save | the L4 combat script (`HAS_TRANSFORMED_BEFORE`) sets it in play | the bit→character map (0x1b8–0x1c0) |
| New-game state | — | — | `RetailCampaignType` (levels, equipment, items, gold — L4 data modeling retail) |

**Conclusion (RQ7, this surface)**: characters and progression are the most **native + data-driven** system in the M9 surface — the opposite of battle's "the script is the courtroom" (05b). The engine (the templates' tables + `PostBattleScreen` + the level-up pipelines) owns every rule; the data (the per-character tables and the registries) owns every value; the script is only a thin producer (SP grants, mastery ticks, the party-flag writes, the dragoon-space set) and never a rule. The L4 layer's contributions are architectural (the stat model, the template/registry abstractions, the events, the extended levels, the party/level configs) — none of the retail *rules* are script-resident on this surface.

## 13. RNG and timing determinism

Progression on this surface is fully deterministic given the battle outcome: the XP split is integer division over a deterministic living-party list; the grant animation is a 10-per-frame counter with button-skip (no timing-dependent value — the total granted is fixed); the DXP pool is the sum of the in-battle `scriptGiveSp` amounts (the 05c RNG-free spinners); the unlock checks are pure functions of (level, dlevel, goods, addition levels); the unlock timestamps derive from `timestamp_a0` (the campaign clock, M7-01), not RNG; the save-load replay is a deterministic while-loop; the retail-first merge ordering fixes display order. No random source appears anywhere in the progression path (verified by inspection of every routine above).

## 14. Retail vs reference (the layer-4 surface)

The L4 deviations/re-architecture on this surface (each preserved, none silently normalized):

1. **Stat storage**: retail's in-struct `hp_08`/`mp_0a`/`sp_0c` and `additionLevels_1a[8]`/`additionXp_22[8]` (visible as commented-out decompiled fields) are replaced by the `StatCollection` (17 stats, vitals with max) and the registry-keyed `CharacterAdditionInfo` maps (§2.1, §4).
2. **The battle stat bridge is a copy**: retail kept per-bent stat words in the 0x27c bent (05b); the reference copies the character's `StatCollection` into the bent at battle start and bakes equipment additively (§4.3).
3. **The template abstraction**: retail's per-character C tables/functions become 9 registry entries behind `CharacterTemplate` — the mod surface (M8-02) and the L4-only machinery (events, the abstract growth getters).
4. **Extended levels**: past level 60 (and the `MAX_LEVEL` config, the `getExtendedXp` series, the per-stat floor-difference coefficients) — no retail counterpart (§5.5).
5. **Configs modeling retail knobs**: `MAX_DRAGOON_LEVEL` (default 5, ScriptReadable), `SECONDARY_CHARACTER_XP_MULTIPLIER` (default 1.0; vanilla preset 0.5 — the reference's retail-equivalence model), `UNLOCK_PARTY` (default true; vanilla false), `EXTENDED_DRAGOON_ACTIONS` (default true; vanilla false) (§2.6, §5.5, §6.4, §6.7; the vanilla preset, `ConfigPresetManager.java:208–225`).
6. **The mod-event surface**: `Pre/PostCharacterLevelUpEvent` (mutable `statsToAdd`/`levelsToAdd`), `Pre/PostCharacterDragoonLevelUpEvent`, `CharacterCreatedEvent`, `ResolveCharacterElementEvent`, `PostAdditionLevelUpEvent`, `PartyFlagsChangeEvent`, `RegisterCharacterTemplatesEvent`, `RegisterLevelUpActionsEvent`, the stat-mod-type script configs — all L4.
7. **The registry duplicates**: the `ALBERT_` additions/spells (sharing retail block 8 with Lavitz) and the `MIRANDA_` spells (retail 65–67 duplicating 10–12) — the reference's way of giving the shared retail tables distinct identities (§3.5, §8.4, §9.2).
8. **The 10th pseudo-character ("DD")**: `CHARACTER_ADDITIONS`/`additionOffsets_8004f5ac` have 10 rows (the last, "DD", empty, offset 0) — the retail divine-dragoon slot; the reference models the divine form as a runtime flip of Dart instead of a 10th character (§6.6, C39).
9. **`hasTransformed` as a boolean** backed by the retail `scriptFlags2` bits on save (§6.8).
10. **The new-game setup as data** (`RetailCampaignType`) — the starting levels/equipment/items/gold are L4 constants extracted to model retail (C34).

## 15. Findings register

| ID | Finding | Layer | Confidence |
|---|---|---|---|
| C1 | `CharacterData2c` is the 0x2c retail character record: `xp_00`, `partyFlags_04`, (retail `hp_08`/`mp_0a`/`sp_0c` — commented out in the reference), `dlevelXp_0e`, `status_10`, `level_12`, `dlevel_13`, `equipment_14` (slot map), `selectedAddition_19`, (retail `additionLevels_1a[8]`/`additionXp_22[8]` — commented out); the reference stores stats in a `StatCollection` and additions/spells in registry-keyed maps | L3 decompilation / L4 | source-verified |
| C2 | `partyFlags_04` bits: 0x1 IN_PARTY, 0x2 CAN_BE_IN_PARTY (main-party eligibility), 0x4 TEMPORARILY_REMOVED_FROM_PARTY (retail `char_utils`; flags "shifted up by 8 bits to back them up" per in-source comment), 0x20 CANT_REMOVE, 0x40 HAS_ULTIMATE_ADDITION ("no longer used") | L3 | source-verified (the bit meanings) / the 8-bit backup mechanism Inferred (comment-only) |
| C3 | `status_10` bits 0x1–0x80 (petrified/bewitched/confused/fearful/stunned/weapon-blocked/dispirited/poison); `getStatusAndFlags()` ORs 0x2000 when `hasDragoon()` — the `canBecomeDragoon` bit; `DartCharacterData` additionally ORs 0x4000 while the divine spirit is present (the 05c U28 marker) | L3/L4 | source-verified |
| C4 | Party model: `charData_32c` (all 9, retail slot order), `charIds_88` (main party, 3 slots), `getCharacterBySlot`; `cacheCharacterSlots` @ 0x80103b10 builds the IN_PARTY index list and the secondary list (IN_PARTY ∉ charIds_88); the char-swap rules gate on CANT_REMOVE/CAN_BE_IN_PARTY and `UNLOCK_PARTY` | L3 | source-verified |
| C5 | Script party API: game-var cases 115–123 write per-character `partyFlags_04` through `PartyFlagsChangeEvent` (mod-rewritable) — the landing pad of the retail `char_utils` script (not in the repo) | L3 var / L4 event | source-verified |
| C6 | `CharacterTemplate` (registry `lod_core:character_templates`) is the per-character class: data (make/copy/serialize), progression (level-up/dragoon-level-up/checkUnlocks/XP+DXP thresholds/growth getters/`getExtendedXp`), identity (element ×2/hasDragoon/canUseSpells/getDragoonAddition/isArcher/canEquip), and the full asset surface (paths, DEFFs, colours, model parts, SP-bar palettes); 9 retail registrations; `CharacterCreatedEvent` on `make` | L4 | source-verified |
| C7 | `applyLevelUp` = Pre-event with `statsToAdd` {HP/ATK/DEF/MATK/MDEF/SPD} indexed by the **current** level → mods → apply → `level += levelsToAdd` (default 1) → `checkUnlocks` → Post-event; XP is not touched; creation applies one level-up (level 0→1, table row 0) so every character starts at level 1 with `xp` = 0 | L4 (modeling retail) | source-verified (L4) / retail correspondence Inferred |
| C7a | Creation also applies one `applyDragoonLevelUp` (dlevel 0→1): MP +20, SP +100, the dlevel-0 dragoon-stat row (e.g. DRG_DEF +200, DRG_MDEF +200, template-specific DRG_ATK/DRG_MAG_ATK) — every character's starting stats include the first dragoon-level-up's increments even before owning a spirit; SP starts 0 (only HP/MP restored) | L4 | source-verified |
| C8 | `applyDragoonLevelUp` = Pre-event with `statsToAdd` {MP: `getMpToAdd` (default 20), SP: `getSpToAdd` (default 100), DRG_ATK/DRG_MAG_ATK (abstract per template), DRG_DEF (default 200@0/20@4/10 else), DRG_MDEF (same)} → apply → `dlevel += 1` → `checkUnlocks` → Post-event | L4 | source-verified |
| C9 | Per-character 60-entry tables (XP + 5 growth columns) are the templates' `static final int[]` constants; `XP[level−1]` = the absolute XP to leave level L; table heads: Dart 20, Lavitz 35, Shana 30, others 20; the HP tables step-jump at levels 11/21/41/51 (retail shape, uninterpreted); Albert = Lavitz's tables, Miranda = Shana's | L4 data | source-verified (L4) / retail-byte source Unresolved |
| C10 | Extended levels (L4): `MAX_LEVEL` (default 60, 1–250) caps the loops; past the table `XP[59] + Σ(23000 + i×1000)`; the growth getters switch to closed forms (`floor(level·k) − floor((level−1)·k)` per stat, per-character k; extended HP flat 385 except Dart's piecewise form) | L4 | source-verified |
| C11 | The post-battle report is frame-driven (10 units/frame, CONFIRM-skip) over fixed pools; `givePendingXp` level loop capped at `MAX_LEVEL`; `applyLevelUpActions` records the no-op actions for display | L3 (retail screen) | source-verified |
| C12 | XP pool: the per-enemy `xp_194`/`gold_196`/drops come from the `enemyRewards_80112868` table via `loadEnemyDropsAndScript` @ 0x80109808 (Battle:9319–9345, `EnemyRewardsEvent` — the xp/gold/drops are mod-mutable); accumulation happens in `scriptSetBentDeadAndDropLoot` @ 0x800ccf2c (Battle:4315–4332) when the L4 combat script marks a monster dead (`FLAG_DEAD` + `FLAG_MONSTER` → gold/xp added, `simpleRand()`-rolled drops unless `FLAG_NO_LOOT`); `totalXpFromCombat_800bc95c` resets at battle start (1972) and is script-var case 61 (read/write); split over `livingChars_800bc968` (filled at the post-combat camera-fade start — frame 0 of the post-combat action, 2435–2444): main = total / livingCount (raw integer division), secondary = safeDiv(total, livingCount) × `SECONDARY_CHARACTER_XP_MULTIPLIER` (default 1.0; vanilla preset 0.5) | L3 | source-verified (the 0.5 is an L4 modeling value) |
| C13 | The 4 registered level-up actions (`level_up`, `dragoon_level_up`, `unlock_addition`, `unlock_spell`) have no-op `apply` — display records; their options carry the unlocked registry IDs, consumed only by the report's display maps | L4 | source-verified |
| C14 | Per-character DXP tables (threshold to leave dlevel): Dart/Rose 1200/6000/12000/20000/20k+series; Lavitz/Haschel/Shana(Miranda) 1000/6000/12000/20000/20k+series; Meru/Kongol 1000/2000/12000/20000/20k+series; dlevel starts at 1 (C7a) | L4 data | source-verified (L4) / retail-byte source Unresolved |
| C15 | DXP sources (exactly two, native): `scriptGiveSp` @ 0x800f43dc (every scripted SP grant also queues `spGained_800bc950[character] += amount`) and `SItem.addSp` @ 0x80022cd0 (SP items: `dxp += requested amount` even when the SP gain is clamped; immediate dragoon level-up loop ≤ `MAX_DRAGOON_LEVEL`) | L3 | source-verified |
| C16 | Post-battle DXP drain: `givePendingDxp` (10/frame) → `dlevelXp_0e += chunk`, `spGained −= chunk` (the SP-bar visual), dragoon level-up loop ≤ `MAX_DRAGOON_LEVEL` (default 5, ScriptReadable), `dragoonLevelsGained` tracking | L3 | source-verified |
| C17 | Dragoon eligibility = the character's `getDragoonSpirit()` present in `goods_19c` (spirits are goods with retail IDs RED 10, BLUE 20, JADE 30, GOLD 40, VIOLET 50, SILVER 60, DARK 70, DIVINE 80); no per-character dragoon flag; Dart qualifies with RED **or** DIVINE | L3/L4 | source-verified |
| C18 | Transform gate: TRANSFORM offered when `!isDragoon && SP ≥ 100 && canBecomeDragoon` (0x2000); SPECIAL (all-party) when ≥3 players, all alive, all `dlevel ≥ 1 && SP ≥ dlevel×100`, none already dragoon, all canBecomeDragoon; the tutorial forced-action gating is implemented natively in the same builder (dormant in the reference — 05c U31) | L4 core mod (retail menu) | source-verified |
| C19 | `hasTransformed`: L4 boolean; writers = the bent script-enum `HAS_TRANSFORMED_BEFORE` (PlayerBattleEntity:591, the L4 combat script) + the save loaders; retail backing = `scriptFlags2` bits 0x1b8/0x1ba/0x1b9/0x1bb/0x1bc/0x1bd/0x1be/0x1bf/0x1c0 → characters 0–8 | L3/L4 | source-verified |
| C20 | 17 stat types (hp/mp/sp = VitalsStat; the other 14 = UnaryStat, incl. the 4 DRAGOON_* multipliers and GUARD_HEAL); `Stat.mods` keyed by mod registry ID; `UnaryStat.get = raw + Σmods`; `FractionalStat.getMax = maxRaw + Σmods` (vital mods apply to the **max**); `turnFinished` per-turn mod expiry driven by the battle loop | L4 | source-verified |
| C21 | Stat-mod config: `amount`, `percentile`, `turns`, `contributesToOtherMods` — serializable and script-configurable (params 0–3); `getRawWithContributingMods` provides the chained-mod base | L4 | source-verified |
| C22 | Battle bridge (`initPlayerBattleEntityStats`): bent spells = unlocked spell list; `player.stats.set(character.stats)` (a **copy**); level/dlevel/status(±0x2000)/selectedAddition transferred; per-equipment: `specialEffectFlag_14 |= flags_00`, attack-element/resist/immunity/status-resist accumulation, flat additive raw increments (attack…guardHeal), `equipmentAttack1_28 += attack_10` — equipment stats are baked at battle start, not live mods | L3/L4 | source-verified |
| C23 | Elements: 9 registered (flags 0x0/0x1/0x2/0x4/0x8/0x10/0x20/0x40/0x80, colours; the three `SimpleElement` pairs water↔fire, earth↔wind, dark↔light are mutually strong + self-weak; custom: NoElement none, Thunder no-strong, Divine boost-self-only); damage: attacking 1.5×/0.5×, defending identity, dragoon-space 1.5× (attacker == space) / 0.5× (attacker strong vs space); applied per attack element (equipment elements included) | L3/L4 | source-verified (L4 data) / retail-byte source Unresolved |
| C24 | Dragoon space: `Battle.dragoonSpaceElement_800c6b64` set only by `scriptSetDragoonSpaceElementIndex(charId)` @ 0x800f9b78 (charId or −1 = clear); consumed in the magic branch (Battle:8608–8614) and `PhysicalDamageFormula.applyDragoonSpace` | L3 | source-verified |
| C25 | Character element: template constant (Dart's bent-aware divine switch is the only bent-aware override); `ResolveCharacterElementEvent` mod hook on both the plain and bent-aware resolutions | L4 | source-verified |
| C26 | Elements are ScriptReadable: 0 = colour 0xBBGGRR, 1 = strong-vs(out), 2 = weak-vs(out), 3 = boosts(out) | L4 | source-verified |
| C27 | Per-character addition rosters + unlock levels (§3.4 table); `AdditionLevelUnlockCriterion` (level ≥ n) and `AdditionMasteryUnlockCriterion` (all mastery-counting additions complete; vacuously true with none); the ultimate additions have `countsTowardsMastery = false`, max level 5, 2–8 hits | L4 data | source-verified |
| C28 | Mastery: `scriptLevelUpAddition` @ 0x800cd200 does `selectedAddition.xp++` (null-guarded for Shana/Miranda); the report loop `while (level < 5 && xp ≥ level×20) level++` (cumulative, never consumed) + `PostAdditionLevelUpEvent`; the level indexes the addition's `LevelMultipliers` (damage/SP multipliers) | L3/L4 | source-verified |
| C29 | Unlock model: `UnlockState` UNLOCKABLE/UNLOCKED/LOCKED; `checkUnlock` = unlockable ∧ all criteria; `unlock(timestamp_a0)`; check points = `addAddition`, every level-up/dragoon-level-up, the report's `checkForUnlocks` (all characters), save-load | L4 | source-verified |
| C30 | Retail addition table: `CHARACTER_ADDITIONS` (10 rows incl. the empty "DD" row) + `additionOffsets_8004f5ac = {0,8,−1,14,29,8,23,19,−1,0}` into the retail 35-entry table; Albert shares Lavitz's block (offset 8); implied reserved slots 7/13/18/22/28 | L3 data | source-verified (the table) / reserved-slot semantics Inferred |
| C31 | `SpellStats0c` (12 bytes): `targetType_00` (0x8 = attack all; other bits undecoded), `flags_01` (0x4 = buff-or-always-hit), `specialEffect_02`, `damageMultiplier_03`, `multi_04` (100/50 on the buff spells — read as buff magnitude, Inferred), `accuracy_05` (100), `mp_06`, `statusChance_07`, `element_08`, `statusType_09`, `buffType_0a` (bit table in javadoc), `_0b`; ScriptReadable 0 = battle stage | L3 | source-verified (fields + javadoc) / several field semantics Inferred |
| C32 | 63 registered spells (retail 0–59 + 65–67); per-character assignments + dlevel gates (§9.2): first spell pre-unlocked at creation; Kongol 1/3/5 (the only non-standard); Albert's list (14/26/17/8) and Rose's (15/16/18/19) both draw on the retail 14–19 dark block without overlapping entries; Miranda uses the 65–67 duplicates of 10–12 (retail sharing) | L4 data | source-verified (L4) / retail-byte source Unresolved |
| C33 | The dlevel-5 spells are `DragonSpell`s with battle stages 71–77 (red/jade/blue/dark/white-silver/golden/violet); DEFF load: `deffManager.flags_20 |= dragoonDeffFlags_800fafec[index] << 16`, sound special case for index 46 (an empty retail slot, placeholder-registered in L4), extra-TIM table `dragoonDeffsWithExtraTims_800fb040` → dir 4115+i, DEFF pair `SECT/DRGN0.BIN/(4139 + 2×index)` + `(4140 + 2×index)` | L3 | source-verified |
| C34 | New game: 9 characters in CHAR_IDS order; main party = {Dart}; starting levels Lavitz 3 / Shana 4 / Rose 8 / Haschel 13 / Albert 15 / Meru 17 / Kongol 19 / Miranda 23 (Dart 1); the `levelUp` helper leaves `xp = XP[target−2]` (partial progress); Dart flags 0x23; items BURN_OUT ×1 + HEALING_POTION ×2; the 9×5 starting-equipment table (§11.1); gold 20; FMV 2 → SUBMAP | L4 (modeling retail) | source-verified (L4) / retail values Unresolved |
| C35 | Retail save load: per slot, `CHAR_IDS` → template → `make` → replay loops (`applyLevelUp`/`applyDragoonLevelUp` to the saved level/dlevel) → direct field overwrites → per-slot equipment (unknown IDs skipped) → addition/spell merge (retail-first, `unlock(timestamp_a0 + i)`, then mod entries); `hasTransformed` from the scriptFlags2 bits; save-card `maxHp` from `RETAIL_HP[char0][level−1]` | L3/L4 | source-verified |
| C36 | `RETAIL_HP` (9 × 60 cumulative max-HP; rows 5/8 = rows 1/2) is used only for the save-card preview (Dart's max HP at his level) | L3 | source-verified |
| C37 | Template mirroring: `CharacterData2c.set` ends with `template.copy(other, this)`; Albert swaps Lavitz's 5 additions + 3 spells to the `ALBERT_`/retail-26/14 IDs; Miranda swaps Shana's 3 light spells to 65–67; the retail root cause = the shared retail addition block (offset 8) and shared spell IDs (10–13) | L4 | source-verified |
| C38 | D-attack data: 8 hits (6 active, power 100/10/20/30/40/0, `flags_00 = 0xc0`, per-character property/sound words + 2 silent `0x0` tail hits); Kongol sparse; Shana/Miranda `null`; the per-hit damage arithmetic is the retail DEFF (05c open item 1) | L4 data | source-verified (L4) / retail DEFF Unresolved |
| C39 | Divine dart: with the DIVINE spirit in goods — `0x4000` flag, the divine spell list (DIVINE_DG_BALL 9 / DIVINE_DG_CANNON 4, pre-unlocked at construction), the divine element in dragoon form (the only bent-aware element override), `hasTransformed` suppressed, the divine d-attack variant (200/20/40/60/80); the retail "DD" 10th slot and the d-attack HUD's charId-9 case (05c U28) are consistent with a retail divine entity — the reference models it as a runtime flip of Dart | L4 | source-verified (the flip) / the charId-9 identity Inferred (05c U28) |
| C40 | The vanilla config preset (the reference's retail-equivalence model): `UNLOCK_PARTY = false`, `EXTENDED_DRAGOON_ACTIONS = false`, `SECONDARY_CHARACTER_XP_MULTIPLIER = 0.5`, plus the retail icon set/item-sort/etc. — L4 configuration, not a verified retail constant set | L4 | source-verified (as L4 config) |

## 16. Cross-document dependencies

- **05a**: the 362-slot script-function table (slot 176 `scriptLevelUpAddition`, the `scriptGiveSp` pair 487/488, `scriptSetDragoonSpaceElementIndex`, the `HAS_TRANSFORMED_BEFORE` bent enum), the battle-stage model (the DragonSpell stages 71–77), the encounter/combatant data model (`xp_194`), the script-var surface (case 61, cases 115–123).
- **05b**: the bent models (the `status_0e` transfer and the 0x2000 bit, the `level_04`/`dlevel_06` words, the `selectedAddition_58` word), the power-stat derivation (the consumers of §4.3's transfer), the elemental damage stages (the consumers of §7.2), the DRAGOON_* multipliers' application (`effective × stat / 100`), the guard model (GUARD_HEAL), the per-hit SP/MP economy (the 0xcc–0xd3/0x128–0x130 fields), the status-ailment model (the `status_10` mirror, the `statusType_09` spell field).
- **05c**: the addition system (the consumer of §8 — the driver's mastery ticks, the per-level multipliers, the hit counts, the 0xe0 ultimate-hit flag), the SP/mastery grants (§3.3/§3.4 there = the producers of C15/C28), the d-attack HUD and data (§6.5 here feeds it; 05c U28 is corroborated by C39), the equipment special-effect flag (§4.3's `specialEffectFlag_14` — 05c §3.5's save/clear/restore), the tutorial words (C18's native handler = 05c U31's native side), the dragoon SP model (05c U35 vs C18's transform gate: the gate requires SP ≥ 100 to *enter*; the in-form drain is 05c's pooled-SP model).
- **02-title / 03-submap / 04-worldmap**: the new-game transition (FMV 2 → SUBMAP), the character world-map models (`loadWorldMapModel` paths), the `characterInitialized_4e6` discarded bit (M7-01).
- **M7**: the save formats (the retail 9-char array + `CHAR_IDS`, the V2 character tag fields, the save card), the config storage (the five progression configs' locations/categories, the vanilla preset), `timestamp_a0` (the unlock-timestamp source), the menu stack (the report as a screen).
- **M8**: the registries (9 character templates, 17 stat types, the stat-mod types, 9 elements, 63 spells, 35 additions, goods, 4 level-up actions, campaign types) and the event surface (the 7 events of C6/C25/C29/C5/C13).
- **M9-07 (inventory and items, upcoming)**: the equipment data model (`attack_10`…`statusResist_08`, `attackElement_04`, `canEquip`, `EquipmentTypes`, `onEquip`/`onUnequip`), the item data model (the BURN_OUT/HEALING_POTION starting items, the SP items that reach `SItem.addSp`, the attack-item list `gatherAttackItems`), the goods inventory (the spirits' item behavior, the retail IDs 10–80/130/340).

## 17. Open items and evidence gaps

1. **The charId-9 / "DD" identity** (C39, 05c U28) — the divine-dragoon runtime flip is source-verified; whether the retail charId 9 (the d-attack HUD's special case, the 10th `CHARACTER_ADDITIONS` row) is the same retail entity in all contexts → Inferred (register).
2. **The retail addition table's reserved slots** (7, 13, 18, 22, 28 — C30) — the offset table is source-verified; what the five unused 35-entry-table slots held in retail (Shana/Miranda additions? the DD? placeholder padding?) → Unresolved (register).
3. **`SpellStats0c` field semantics** (C31) — `targetType_00` beyond the 0x8 attack-all bit, `specialEffect_02`, `damageMultiplier_03`, `multi_04` (buff magnitude is Inferred), `_0b` → partially Inferred / Unresolved (register; the consumers are the 05b spell pipeline and the DEFFs — retail binaries).
4. **Retail DXP sources beyond `scriptGiveSp`** — in the reference, `spGained_800bc950` has exactly one writer (source-verified absence of others); whether retail queued DXP from additional in-battle sources (e.g. the d-attack itself) cannot be checked without the retail scripts/binary → Unresolved (register).
5. **The retail value of the secondary-character XP share** — the reference default is 1.0 and the vanilla preset models 0.5 (C12/C40); the actual retail constant is not verifiable from the repo → Inferred (the 0.5 modeling is the reference's, preserved as such).
6. **The SPELL32–37 placeholder entries** (C32) — six NO_ELEMENT power-0 slots (0xc0/0x48 flag shapes) assigned to no character; whether they are retail Kongol spell candidates, reserved, or padding → Unresolved (register).
7. **The 8-bit flag backup for temporarily-removed characters** (C2) — documented only by the in-source comment on `partyFlags_04`; no L4 writer implements the shift (the retail `char_utils` script presumably did) → Inferred (register).
8. **The per-template dragoon-stat increment tables** (the abstract `getDragoonAttackToAdd`/`getDragoonMagicAttackToAdd` rows, §6.3) — Shana/Miranda's (200/150 at 0, 5 otherwise) are cited; the other seven templates' rows are in the template sources (cited) but were not transcribed here (data, not behavior) → no gap for implementation; recorded for completeness.
9. **The `xpDivisor == 0` edge** — the main-party XP split (`PostBattleScreen.java:151`) is a raw integer division by the living-main-party count, while the secondary split (`:161`) uses `MathHelper.safeDiv` (zero-safe). If the report ever ran with zero living main-party members the main loop would throw; in normal play the report only runs on victory (≥1 survivor), so the edge is unreachable (Inferred; preserved, not "fixed").
