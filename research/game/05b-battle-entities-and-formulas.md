# 05b — Battle: Entities, Stats, and Formulas

M9 deliverable 05, part b of c. Covers: the battle-entity and stat data models (byte-level field maps), entity initialization and per-turn lifecycle, the physical and magical damage pipelines end-to-end, accuracy/evasion, on-hit special effects and status ailments, healing/resources/revival/guard, the element system, player/monster asymmetries, the script/native/data boundary for every significant formula (RQ7), RNG consumption, and the retail-vs-reference differences on the formula surface. Entry/exit, the loading-stage machine, the turn-gauge simulation, the 362-slot script-function table, battle audio, and the `SEffe` overview are **05a** (cross-referenced, not repeated). Additions in depth, battle UI/targeting screens, and post-battle screens are **05c**.

Sources (all read this session):

- Severed-Chains @ `0c08c679b` (pinned, clean): `legend/game/combat/Battle.java` (9,392 lines), `legend/game/combat/bent/BattleEntity27c.java` (833 lines), `legend/game/combat/bent/PlayerBattleEntity.java` (609 lines), `legend/game/combat/bent/MonsterBattleEntity.java` (223 lines), `legend/game/combat/formula/` (`Formula`, `PhysicalDamageFormula`, `State`, `Side`, `Step`, `FormulaBuilder`, `Operation`), `legend/game/modding/coremod/CoreMod.java` (`:241–251` — `PHYSICAL_DAMAGE_FORMULA`), `legend/game/combat/Monsters.java` (1,332 lines — 400 monster stat/reward blocks, 512 names), `legend/game/combat/types/` (`AttackType`, `MonsterStats1c`, `CombatantStruct1a8` + nested `ItemDrop` record `:87`, `EnemyRewards08`, `battlestate/Status04`, `battlestate/StatusConditions20`), `legend/game/inventory/SpellStats0c.java`, `legend/game/characters/Element.java`, `legend/game/modding/coremod/elements/` (9 element classes + `NoElement`), `legend/game/characters/` (`Stat`, `UnaryStat`, `FractionalStat`, `VitalsStat`, `StatCollection`, `StatMod`, `StatModType`), `legend/game/combat/encounters/Encounter.java`, `legend/game/combat/environment/BattlePreloadedEntities_18cb0.java`, `legend/game/additions/AdditionHitProperties10.java`, `legend/game/scripting/GameVarParam.java`, `legend/game/scripting/GameVarArrayParam.java`, `legend/game/scripting/ScriptState.java` (`:245–262`), `patches/scripts/player_combat_script.txt` (6,802 lines — layer-4 replacement, key formula routines cited by label), `patches/libs/lookup_tables.txt` (`STATUS_AFFLICTION_FLAGS`)
- Reference-repo history: commit `850c43d7e` "fix simplerand spell status chances (closes #2440) (#2684)" — the retail status-effect roll preserved in the pre-fix code (§11, F12)
- Prior research: M4-05 §2.3 (the two retail LCGs), M3-05 (Fate runtime — the `rand` op = `bound × simpleRand() >>> 16`), M3-06 (592-slot native-function catalog), M8-02/M8-03 (registries/events; 131 spells, 35 additions, 512 encounters), M9-05a (B1–B20; the 362-slot table, turn system, rewards model, exit path)
- No retail assets are available: no `asset-validated` claims in this document. The retail player combat script content is unknown; the L4 replacement script is cited for the script-side mechanics that the decompiled native functions clearly interlock with (guard, shields, regen, revive, escape), flagged as such where the retail script may differ.

## 1. Scope and layer model

The battle formula surface splits into three layers of truth:

- **Native (decompiled retail, L3)**: the damage pipelines, accuracy, special-effect determination, death/loot, stat aggregation, and the formula step chain. These run identically for player and monster combatants and are exposed to scripts through the 05a §7 function table.
- **Script (L3 retail originally; L4 replacement in the reference)**: the turn flow calls the native functions, but *incoming* damage modifiers (fear ×2, dragoon dragon-block ×10, guard halving, shield nullification, the 999,999,999 cap), regen application, revival, escape, shield turn bookkeeping, and status-ailment ticking live in the player combat script (`patches/scripts/player_combat_script.txt` in the reference). 05a B7 established that the reference ships a layer-4 replacement for the retail player combat script; the script-side mechanics below are therefore **source-verified as the reference behavior** and *inferred* as retail behavior where the decompiled native side interlocks with them unambiguously (the native `GUARD` stat, the `GUARD_HEAL` stat, the shield word, the `runAwayFlags_2b4`/`dragoonTurnsRemaining_294` battle vars all exist in the decompiled retail `BattleStateEf4`, so the mechanics are retail-anchored even where the retail script text is unavailable).
- **Data (registries + ROM tables)**: per-combatant base numbers come from the stat system (characters: registry stat types + save data; monsters: `MonsterStats1c[400]`), spell data (`SpellStats0c` via the 131-entry spell registry), addition hit properties (`AdditionHitProperties10`), equipment (148-entry equipment registry), encounter parameters (`Encounter` registry, 512 entries), and enemy rewards (`EnemyRewards08[400]`).

Layer tags: **L3** LoD-specific (decompiled retail), **L4** reference-implementation-specific.

## 2. Entity and stat data models

### 2.1 Storage model

A combatant ("bent") is a `ScriptState<BattleEntity27c>` (the per-bent script state, 05a §5) wrapping an `innerStruct_00` entity of size 0x27c, created per type via `RegisterBattleEntityStatsEvent` (M8-02). Two structural facts (F1, source-verified):

1. **The entity flag word lives in the script state, not the struct.** `BattleEntity27c.FLAG_*` (`BattleEntity27c.java:64–94`) are applied via `ScriptState.setFlag/clearFlag/hasFlag` (`ScriptState.java:245–262`), which operate on `storage_44[7]` — storage slot 7 of each bent's script state (the retail per-combatant flag word; scripts read/write it directly). The 14 flags: `0x1` unknown, `0x2` DRAGOON, `0x4` MONSTER, `0x8` CURRENT_TURN, `0x10` HIDE, `0x20` TAKE_FORCED_TURN, `0x40` DEAD, `0x80` ANIMATE_ONCE, `0x100` unknown, `0x200` MONSTER_SUB_PART, `0x400` unknown, `0x800` NO_SCRIPT, `0x1000` RELOAD_BATTLE_ACTIONS, `0x2000` NO_LOOT, `0x4000` CANT_TARGET, `0x8000` MONSTER_MAIN_PART.
2. **The reference re-lays out parts of the struct.** Blocks the decompiler left as equipment words (`equipmentType_16`, `equipment_02_18`, … `equipmentIcon_30`, `attackHit_3c`, `magicHit_3e`, `_118`, `additionSpMultiplier_11a`, `additionDamageMultiplier_11c`) are **commented out** in `BattleEntity27c`/`PlayerBattleEntity` and replaced by Java-managed objects (`Map<EquipmentSlot, Equipment> equipment_11e`, the `Addition` object, the `spells` list). The remaining offsets are preserved. A Rust reimplementation is free to restructure (§2.2 comment), but the offsets that survive are the decompiled retail layout.

### 2.2 `BattleEntity27c` field map (base, both combatant kinds)

| Offset | Field | Meaning (verified) |
|---|---|---|
| — | `type` | `BattleEntityType` (registry, 2 in-repo types) |
| 0x0e | `status_0e` | Ailment bitset 0x1–0x80 (petrified/bewitched/confused/fearful/stunned/weapon-blocked/dispirited/poisoned) + 0x800 "no elemental effects for this attack" (transient, HP%-spell marker, cleared after damage) + 0x2000 can-become-dragoon + 0x4000 divine dragoon |
| 0x14 | `specialEffectFlag_14` | 0x8 attack-all display, 0x40 "chance to kill" (executioner), 0x80 resistance (cancels incoming special effects) |
| 0x16–0x1e, 0x26, 0x30, 0x3c–0x3e | — | decompiled equipment words, **commented out** in the reference (Java-managed equipment, F1) |
| 0x20 | `equipmentElementalResistance_20` | `ElementSet` — half-damage on these elements (player) |
| 0x22 | `equipmentElementalImmunity_22` | `ElementSet` — zero damage on these elements (both kinds) |
| 0x24 | `equipmentStatusResist_24` | 8-bit status-resist mask (same 0x1–0x80 order) |
| 0x28 | `equipmentAttack1_28` | Σ equipment `attack_10` (informational; the live value is the ATTACK stat) |
| 0x44 | `onHitStatusChance_44` | weapon status-infliction % (player); also used when executioner is active |
| 0x4a | `equipmentOnHitStatus_4a` | weapon status-infliction bitset (player) |
| 0x4c | `turnValue_4c` | turn-gauge value (05a §6) |
| 0x4e, 0x52 | — | `spellId_4e`/`itemId_52` commented out (replaced by `spell_94`/`item_d4` objects) |
| 0x54 | `guard_54` | the `GUARD` stat word (bit 0 = currently guarding; script-set) |
| 0x78 | `targetArrowPos_78` | 3×float — target-arrow position for archer/item-throw/ailment FX (the decompiler's `_80`/`_82` int fields alias this vector; do not trust them) |
| 0x7e | `hitCounterFrameThreshold_7e` | counter-attack aggression (0 = cannot counter) |
| 0x84/0x86 | `middleOffsetX_84`/`middleOffsetY_86` | FX anchor offsets, 1/100 scale (shorts) |
| 0x94 | `spell_94` | `SpellStats0c` — the *active* spell (or null) |
| 0xb4–0xbb | `powerAttack_b4`/`powerAttackTurns_b5`, `powerMagicAttack_b6/b7`, `powerDefence_b8/b9`, `powerMagicDefence_ba/bb` | buff/debuff magnitudes (±50) + remaining turns |
| 0xbc–0xc7 | `tempAttackHit_bc/bd`, `tempMagicHit_be/bf`, `tempAttackAvoid_c0/c1`, `tempMagicAvoid_c2/c3`, `tempPhysicalImmunity_c4/c5`, `tempMagicalImmunity_c6/c7` | temporary stat modifiers (% or 0/1) + remaining turns |
| 0xd4 | `item_d4` | `ItemStack` — the active item (or null) |
| 0x110–0x116 | `physicalImmunity_110`, `magicalImmunity_112`, `physicalResistance_114`, `magicalResistance_116` | booleans: immunity → 0 damage; resistance → half |
| 0x142 | `_142` | OR'd with `equipment.onHitStatus_1b` during init (purpose unresolved) |
| 0x144 | `combatant_144` | `CombatantStruct1a8` — asset/reward record (§2.6) |
| 0x148 | `model_148` | `Model124` — the model + transform block (M5-02) |
| 0x26c–0x278 | `combatantIndex_26c`, `loadingAnimIndex_26e`, `currentAnimIndex_270`, `charId_272` (also monster ID), `allBentSlot_274`, `typeBentSlot_276` (−1 until loaded), `_278` | bookkeeping; `charId_272` is the character slot for players and the monster index for monsters |
| 0xc8–0xf7 | `movementParent_c8`, `movementTicks_cc`, `movementRemaining_d0`, `movementStep_dc`, `movementDestination_e8`, `movementStepYAcceleration_f4` | scripted movement (pulled from `ScriptState` for bents) |
| — | `extendedConditions` | `StatusConditions20` (moved from `BattleStateEf4.statusConditions_00`): `menuBlockFlag_18`, `shieldsSigStoneCharmTurns_1c` (the 8-bit shield word, script `var[128+slot][7]`), `pandemoniumTurnsDiedAsDragoon_1d`, `chargingSpirit_1e`, `stolenItem_1f` |
| — | `status_384` | `Status04` (moved from `BattleStateEf4`): `statusEffect_00` (0–8 + unknown 0x40/0x80), `statusTurns_01` ("does not decrease on all statuses"), `unknown_02`; script-visible as packed int `var[129+slot]` (§8.3) |

The transform (position/rotation/scale) lives in `Model124` (0x124 bytes), not in the entity head — `Model124` is the M5-02 model block.

### 2.3 `PlayerBattleEntity` fields (delta)

| Offset | Field | Meaning (verified) |
|---|---|---|
| — | `character` | the bound `CharacterData2c` (game state char slot) |
| 0x04/0x06 | `level_04`/`dlevel_06` | character level / dragoon level (the level in the damage formulas is `level_04`) |
| 0x1c | `equipmentAttackElements_1c` | `ElementSet` — the player's *attack* elements (equipment-derived; the only source — `getAttackElements()` returns this) |
| 0x56/0x58 | `additionHits_56`/`selectedAddition_58` | hit count / selected-addition `RegistryId` |
| 0xac–0xb2 | — | decompiled `dragoonAttack_ac`…`dragoonMagicDefence_b2` **commented out** — replaced by the `DRAGOON_*_STAT` stat types (L4 re-model; the same numbers flow through the stat system) |
| 0xcc–0xd3 | `tempSpPerPhysicalHit_cc/cd`, `tempMpPerPhysicalHit_ce/cf`, `tempSpPerMagicalHit_d0/d1`, `tempMpPerMagicalHit_d2/d3` | temporary on-hit SP/MP gains + turns |
| 0x118 | — | `_118` commented out |
| — | `addition` | the resolved `Addition` object (replaces retail `dragoonSpells_800c6960`) |
| 0x11e | `equipment_11e` | `EnumMap<EquipmentSlot, Equipment>` (WEAPON/HELMET/ARMOUR/BOOTS/ACCESSORY) |
| 0x128 | `spMultiplier_128` | Σ equipment `spMultiplier` |
| — | `originalSpPerPhysicalHit`, `originalMpPerPhysicalHit`, `originalSpPerMagicalHit`, `originalMpPerMagicalHit` | baseline on-hit gains (character stats) |
| 0x12a–0x130 | `spPerPhysicalHit_12a`, `mpPerPhysicalHit_12c`, `spPerMagicalHit_12e`, `mpPerMagicalHit_130` | effective on-hit gains (baseline + equipment + active temp) |
| 0x132 | `escapeBonus_132` | "extra percent chance to run away" |
| 0x134–0x13a | `hpRegen_134`, `mpRegen_136`, `spRegen_138`, `revive_13a` | Σ equipment + character regen/revive |
| 0x13c/0x13e | `hpMulti_13c`/`mpMulti_13e` | Σ equipment HP/MP multipliers |
| — | `spells` | `List<SpellStats0c>` — the character's unlocked spells (registry-resolved at init) |

Player-specific stat overrides: `getEffectiveDefence()`/`getEffectiveMagicDefence()` multiply by `DRAGOON_DEFENSE_STAT`/`DRAGOON_MAGIC_DEFENSE_STAT` (percent of 100) **while in dragoon form** (`PlayerBattleEntity.java:164–178`).

### 2.4 `MonsterBattleEntity` fields (delta)

| Offset | Field | Meaning (verified) |
|---|---|---|
| 0x6e | `damageReductionFlags_6e` | = `specialEffectFlag_14` at load; 0x1 magical min-1, 0x2 physical min-1, 0x4 magical immunity, 0x8 physical immunity (F11) |
| 0x74 | `monsterElementalImmunity_74` | `ElementSet` (copy of `equipmentElementalImmunity_22` at load) |
| 0x76 | `monsterStatusResistFlag_76` | 8-bit status-resist mask (copy of `equipmentStatusResist_24` at load) |

Monster script-visible stats (`getStat` overrides, `MonsterBattleEntity.java:195+`): `MONSTER_ELEMENT` = `element.flag`, `MONSTER_DAMAGE_REDUCTION` = `damageReductionFlags_6e`, `MONSTER_ELEMENTAL_IMMUNITY` = packed set, `MONSTER_STATUS_RESIST_FLAGS`.

### 2.5 The stat system

Stats are registry-registered (`statTypes`, 17 in-repo types + 2 stat mods per M8-03): `Stat` (base — current + optional max, per-`StatMod` `turnFinished` hook) → `UnaryStat` (single value + `StatMod`s that add/multiply) → `FractionalStat` (percent-of-unary) and `VitalsStat` (current/max pair — HP/MP/SP). A bent's `StatCollection` is built at construction from the type's `RegisterBattleEntityStatsEvent` registration set (M8-02); `StatCollection.turnFinished(bent)` (called from the base `BattleEntity27c.turnFinished()`) propagates the turn countdown to every stat mod — this is where stat-registered power/temp turn timers tick, alongside the entity-field `temp*Turns`/`power*Turns` counters the base `turnFinished()` (`BattleEntity27c.java:~400–444`) decrements directly (attack/magic hit/avoid, physical/magical immunity, then the player override decrements the four on-hit temp pairs, `PlayerBattleEntity.java:275–296`, and `recalculateSpeedAndPerHitStats()` re-derives the effective on-hit gains from the `original*` baselines + active temps, `:299–318`).

### 2.6 Supporting records

- **`CombatantStruct1a8`** (0x1a8): per-combatant asset/reward record — `mrg_00`, `tmd_04`, `script_10`, `assets_14[32]`, `xp_194`, `gold_196`, `_19a` (copied from the reward's `_06`, unresolved), `drops` (list), `charSlot_19c`, `flags_19e`, `vramSlot_1a0`, `charIndex_1a2`; nested **`record ItemDrop(int chance, InventoryEntry<?> item)`** (`CombatantStruct1a8.java:87`).
- **`MonsterStats1c`** (0x1c): `hp_00`, `mp_02` (unused in battle — monster MP is never read), `attack_04`, `magicAttack_06`, `speed_08`, `defence_0a`, `magicDefence_0c`, `attackAvoid_0e`, `magicAvoid_10`, `specialEffectFlag_12` (the 0x1/0x2/0x4/0x8 word, *not* the 0x8/0x40/0x80 entity word), `elementFlag`, `elementalImmunityFlag` (ElementSet), `statusResistFlag`, `targetArrowX_12/Y_13/Z_14`, `hitCounterFrameThreshold_15`, `_16`, `_17`, `middleOffsetX_18/Y_19`.
- **`EnemyRewards08`**: `xp_00`, `gold_02`, `itemChance_04`, `itemDrop_05` (`Supplier<? extends InventoryEntry>` or null), `_06`.
- **`Encounter`** (registry, 512 entries, M8-03): `monsters` (≥1, ≤3 unique species — constructor-enforced, 05a B17), `musicIndex`, `escapeChance`, `playerOpeningCamera`, `monsterOpeningCamera`, `cameraPosIndices[4]`, `postCombatSubmapCut`/`postCombatSubmapScene`, `introWaitTicks`. Script-visible elements via `var[46][n]` (`GameVarArrayParam`: `encounter.get(n)`).
- **`SpellStats0c`** (0xc, 131-entry spell registry, M8-03): `targetType_00` (0x8 = attack all), `flags_01` (0x4 = "either buff spell or always hit (or both)"), `specialEffect_02`, `damageMultiplier_03` (the ×8…×0.5 selector, §5.3), `multi_04` (added to magic attack, or to a monster's attack), `accuracy_05`, `mp_06`, `statusChance_07`, `element_08` (`RegistryDelegate<Element>`), `statusType_09` (ailment bitset 0x1–0x80), `buffType_0a` (0x80 def up / 0x40 mdef up / 0x20 atk up / 0x10 matk up / 0x08 def down / 0x04 mdef down / 0x02 atk down / 0x01 matk down), `_0b` (unresolved).
- **Census** (source-verified): `Monsters.java` declares **400** `MonsterStats1c` blocks, **400** `EnemyRewards08` blocks, and **512** `monsterNames_80112068` strings (names 395–397 are the Melbu names, 05a §10).

### 2.7 Addition hit properties (`AdditionHitProperties10`)

The 16-byte per-hit record (`AdditionHitProperties10.java`): `flags_00` (0xc0, 0xe0 for ultimate's first hit; "appears unused"), `totalFrames_01`, `overlayHitFrameOffset_02`, `totalSuccessFrames_03`, `damageMultiplier_04`, `sp_05`, `audioFile_06`, `isFinalHit_07`, `cameraMovementX_08/Z_09/Ticks_0a`, `hitDistanceFromTarget_0b`, `framesToHitPosition_0c`, `_0d` ("always 32, except a few for Haschel, not used in PCS"), `additionFailAnimationIndex_0e`, `overlayStartingFrameOffset_0f`; plus `animationScale` (default 100) and a per-hit sound table. The script accessor index map (`get(index)`, `:63–83`): 0–15 → the byte fields, 100 → `animationScale`, 16+ → sound `(index−16)/2`, even = `soundIndex`, odd = `initialDelay`. Formula-relevant indices: **4 = damage multiplier** (summed across hits in the player physical formula, §4.2), 5 = SP, 11 = hit distance, 12 = frames-to-hit, 14 = fail animation. `BattlePreloadedEntities_18cb0.getHit/getHitCount(charSlot, hitNum)` (`:33–53`) routes dragoon-form players to the preloaded `dragoonAdditionHits_38` table and everyone else to the registry `Addition.getHit(char, additionInfo, hitNum)`.

## 3. Initialization and lifecycle

### 3.1 Monster load (`Battle.loadMonster`, @ 0x800f8670, `Battle.java:8867`)

Per enemy index: the display name comes from `monsterNames_80112068[charId_272]`; `monsterStats_8010ba98[charId_272]` feeds a **`MonsterStatsEvent`** (mutable: hp, maxHp, attack, magicAttack, speed, defence, magicDefence, attackAvoid, magicAvoid, specialEffectFlag, elementFlag, elementalImmunityFlag, statusResistFlag) and then the stat collection is populated: HP set current/max, `specialEffectFlag_14`, `element`, `equipmentElementalImmunity_22`, `equipmentStatusResist_24`, SPEED/ATTACK/MAGIC_ATTACK/DEFENSE/MAGIC_DEFENSE/ATTACK_AVOID/MAGIC_AVOID via `setRaw`, `onHitStatusChance_44 = 0`, `equipmentOnHitStatus_4a = 0`, `targetArrowPos_78`, `hitCounterFrameThreshold_7e`, `_80`/`_82` (aliased vector words), `middleOffsetX_84`/`middleOffsetY_86`. Then the monster-specific copies: `damageReductionFlags_6e = specialEffectFlag_14`, `monsterElementalImmunity_74`, `monsterStatusResistFlag_76`, and the two immunity booleans (`0x8 → physicalImmunity_110`, `0x4 → magicalImmunity_112`). Monsters have **no** `physicalResistance_114`/`magicalResistance_116` set at load (the /2 resistance path is player-equipment and mod-event territory).

### 3.2 Player stat init (`Battle.initPlayerBattleEntityStats`, @ 0x800ef28c, `Battle.java:8399`)

`characterStatsLoaded_800be5d0 = true`, then per player slot: `spells` = the character's unlocked spells (registry); `player.stats.set(character.stats)` (copies every stat instance — the stat system *is* the character's battle stat state); `level_04`/`dlevel_06` from the character; `status_0e = character.getStatusAndFlags()` (the 0x2000 can-become-dragoon bit is set at character creation, M9-06 scope); `selectedAddition_58`/`addition`; then **the equipment aggregation** over the 5 slots (source-verified table — this is the retail stat-derivation contract):

| Equipment source | Aggregation into the player |
|---|---|
| `flags_00` | `specialEffectFlag_14 |=` |
| `attackElement_04` | `equipmentAttackElements_1c addAll` |
| `elementalResistance_06` | `equipmentElementalResistance_20 addAll` |
| `elementalImmunity_07` | `equipmentElementalImmunity_22 addAll` |
| `statusResist_08` | `equipmentStatusResist_24 |=` |
| `attack_10` | `equipmentAttack1_28 +=` **and** ATTACK stat `setRaw(raw + v)` |
| `magicAttack_11`/`defence_12`/`magicDefence_13`/`attackHit_14`/`magicHit_15`/`attackAvoid_16`/`magicAvoid_17` | corresponding stat `setRaw(raw + v)` |
| `getGuardHealBonus()` (base `Equipment` returns 0 — no in-repo override exists; the base `GUARD_HEAL` stat value is character-derived, M9-06) | GUARD_HEAL stat `setRaw(raw + v)` |
| `onHitStatusChance_18` | `onHitStatusChance_44 +=` |
| `onHitStatus_1b` | `equipmentOnHitStatus_4a |=` **and** `_142 |=` |
| `physicalImmunity`/`magicalImmunity`/`physicalResistance`/`magicalResistance` | booleans `|=` |
| `spMultiplier` | `spMultiplier_128 +=` |
| `spPerPhysicalHit`/`mpPerPhysicalHit`/`spPerMagicalHit`/`mpPerMagicalHit` | fields `+=` |
| `escapeBonus` | `escapeBonus_132 +=` |
| `hpRegen`/`mpRegen`/`spRegen`/`revive` | fields `+=` |
| `hpMultiplier`/`mpMultiplier` | `hpMulti_13c`/`mpMulti_13e +=` |

Finally `originalSpPerPhysicalHit += spPerPhysicalHit_12a` (and the three other `original*`) — the baselines the per-hit temp system restores from (§2.5).

### 3.3 Enemy drops and scripts (`Battle.loadEnemyDropsAndScript`, @ 0x80109808, `Battle.java:9326`)

`rewards = enemyRewards_80112868[enemyId]`; if `rewards.itemDrop_05 != null` the combatant gets exactly **one retail drop** (`ItemDrop(itemChance_04, itemDrop_05.get())`); then the **`EnemyRewardsEvent`** (mutable xp, gold, and the whole drops list — mods can rewrite any of it, 05a B18) finalizes `combatant.xp_194`/`gold_196`/`_19a`; then a **`LoadEnemyEvent`** (`SECT/DRGN1.BIN/<enemyId+1>`, mutable script future) loads the per-enemy script asynchronously into `combatant.scriptPtr_10` (the script slot of the 05a §7 table).

The opening camera position index is drawn separately, in `loadStageDataAndControllerScripts` (@ 0x80109050, `Battle.java:9300`, called from the stage-6 preload `:2031`): `hud.currentCameraPositionIndicesIndex_800c66b0 = simpleRand() & 3` (`:9320`), `currentCameraIndex_800c6780 = encounter.cameraPosIndices[index]` (`:9321`) — one of the 05a B4 call sites.

### 3.4 Active spell / item

- `setActiveItem` (@ 0x800f7a74, `BattleEntity27c.java:822`): `item_d4 = item`.
- **Base** `setActiveSpell(spellId)` (@ 0x800f9e50, `:827`): `REGISTRIES.spells.getEntry(LodMod.id(LodMod.SPELL_IDS[spellId]))` → **`ActiveSpellEvent(this, spellId, spell)`** → `spell_94 = event.spell`.
- **Player override** `setActiveSpell` (`PlayerBattleEntity.java:598`): "Spell ID > 127 is a retail bug, happens with Shiranda's d-attack and virulent arrow" — `spellId == -1 || spellId > 127` falls back to `LodSpells.SPELL127`, then posts **`SpellStatsEvent(charData, spell)`** (character-keyed, the moddable path) → `spell_94`. The two different events are the mod surface for spell-stat overrides (M8-02 family table).
- `clearTempWeaponAndSpellStats` (@ 0x800f9e10, `:817`): `item_d4 = null; spell_94 = null` — called before every spell/item attack (the L4 script does it via `scriptDragoonMagicStatusItemAttack` internally).

### 3.5 Per-turn cleanup

The base `turnFinished()` (called by the turn system after a bent's turn, 05a §6) decrements the entity-field turn counters (`tempAttackHitTurns_bd` … `tempMagicalImmunityTurns_c7`, each zeroing its value at 0), then `stats.turnFinished(this)` ticks the stat-registered timers. The player override additionally decrements the four on-hit temp pairs and re-derives the effective on-hit gains. Script-level per-turn work (guard off, shield/sig/charm turns, status ailments, regen) runs in the player combat script's `START_TURN` (§7, §8).

### 3.6 Death and loot (`scriptSetBentDeadAndDropLoot`, @ 0x800ccf2c, `Battle.java:4316`)

On `dead == true` and not already `FLAG_DEAD`: set `FLAG_DEAD`; if `FLAG_MONSTER`: `goldGainedFromCombat_800bc920 += combatant.gold_196`, `totalXpFromCombat_800bc95c += combatant.xp_194`; if not `FLAG_NO_LOOT`: for each `ItemDrop` — **drop roll `simpleRand() * 100 >> 16 < drop.chance()`** (roll ∈ [0, 99]; P(success) = chance/100 for chance ≤ 100, so any chance ≥ 100 is certain — F13) — then `Equipment → itemsDroppedByEnemies_800bc928` (perform = `giveEquipment`, overflow → `equipmentOverflow`) or `ItemStack → gameState.items_2e9.give(item)` (perform = "fit in inventory", overflow → `itemOverflow`); set `FLAG_NO_LOOT` (multi-part monsters drop once). `dead == false` clears the flag. Both paths end with `battleState.cacheLivingBents()`. The loot-less variant is `scriptSetBentDead` (@ 0x800cd078, `:4361`). The L4 script's death handler (after the death animation 0xe, `CLEAR_MY_STATUS`, menu-block clear `var[128+slot][6] = 0`) calls `TRY_TO_REVIVE` first; only on failure does it set `CURRENT_HP = 0` and call the loot function (§7.4).

### 3.7 Battle end accumulators

`spGained_800bc950` (map `CharacterData2c → int`) is merged by `scriptGiveSp` (@ 0x800f43dc, `:8727`): `sp.setCurrent(sp + amount); spGained.mergeInt(player.character, amount, sum)` — the per-character SP report for the post-battle screen (05c). `scriptConsumeSp` (@ 0x800f4518, `:8749`): `sp.setCurrent(sp − amount)` (param 2), **no** `spGained` adjustment. HP/MP have no dedicated give/consume script functions; the script uses `scriptSetBentStat`/`scriptSetBentRawStat` with `CURRENT_HP`/`CURRENT_MP`/`MAX_HP`/`MAX_MP` (e.g. the revive path sets `CURRENT_HP` raw). The post-battle action is set by `scriptSetPostBattleAction` (@ 0x800ccf0c, `:4292`): registry ID or legacy int **1 = victory, 2 = game over, 3 = boss kill, 4 = FMV, 5 = merchant** (the `MERCHANT` action is what escape triggers, §7.5).

## 4. Physical damage formulas

### 4.1 The pipeline (F2)

`scriptPhysicalAttack` (@ 0x800f2500, `Battle.java:8640`) computes damage as `EVENTS.postEvent(new AttackEvent(battle, attacker, defender, AttackType.PHYSICAL, CoreMod.PHYSICAL_DAMAGE_FORMULA.calculate(attacker, defender))).damage` — the formula is a mod-mutable `Formula` object (`Formula.make` step chain, `Formula.java`) and the result is a mutable `AttackEvent` (M8-02). The **10-step chain** (`CoreMod.java:241–251`, source-verified order):

| # | Step | Source | Effect |
|---|---|---|---|
| 1 | `calculatePhysicalDamage` | entity (abstract) | base damage (§4.2/§4.3) |
| 2 | `applyElementalInteractions` | `PhysicalDamageFormula.java:20–32` | per attack element: attacker-side ×150/×50 then defender-side (identity, F6) |
| 3 | `applyPower` | `adjustDamageForPower` (§4.5) | power-buff float multiplier |
| 4 | `applyDragoonSpace` | `PhysicalDamageFormula.java:34–47` | dragoon-space element boost (§4.6) |
| 5 | `minimum(0)` | — | clamp ≥ 0 |
| 6 | `applyDamageMultipliers` | `attacker.applyPhysicalDamageMultipliers` | player: identity; **monster: spell multiplier table (§4.7)** |
| 7 | `applyAttackEffects` | `attacker.applyAttackEffects()` | player: no-op; **monster: self-buff (§4.7)** |
| 8 | `minimum(1)` | — | clamp ≥ 1 (the "min-1" of monster damage comes from *here*, not the formula) |
| 9 | `applyResistanceAndImmunity` | `defender.applyDamageResistanceAndImmunity(dmg, PHYSICAL)` | §4.8 |
| 10 | `applyElementalResistanceAndImmunity` | per attack element: `defender.applyElementalResistanceAndImmunity(dmg, element)` | §4.8 |

Order matters: elemental interactions apply *before* power and the multiplier; the ≥1 clamp applies *before* resistance (so a halved minimum-1 hit can still be 1, but immunity zeroes it). The script function then fills `specialEffects` via `determineAttackSpecialEffects` (§6.6) and the HUD element icons (physical + the player's attack elements).

### 4.2 Player base formula (F3, @ 0x800f2af4, `PlayerBattleEntity.java:193`)

```
attack = ATTACK stat
attackMultiplier = 100
if selectedAddition == null:            // no addition equipped
    if isDragoon(): attackMultiplier = DRAGOON_ATTACK_STAT   // percent
else if additionHits > 0:
    additionMultiplier = Σ getHitProperty(typeBentSlot, i, 4) over all hits   // property 4 = damageMultiplier, summed
    damageMultiplier = isDragoon() ? DRAGOON_ATTACK_STAT : getAdditionDamageMultiplier()  // percent (addition.getDamageMultiplier × 100, rounded)
    attackMultiplier = additionMultiplier × damageMultiplier / 100
attack = attack × attackMultiplier / 100
return round( (level + 5) × attack × 5 / target.getEffectiveDefence() )   // Math.round, float division
```

Notes: the `round` is floating-point rounding of the *whole* expression (the only rounded step in the physical pipeline; every other division truncates); `getEffectiveDefence()` is the dragoon-adjusted defence (§2.3); with no addition and not a dragoon the multiplier is exactly 100, so plain player damage = `round((L+5)·atk·5/def)`; the dragoon path replaces the addition multiplier with the `DRAGOON_ATTACK_STAT` percent (the commented-out retail `dragoonAttack_ac` word, F1 re-model).

### 4.3 Monster base formula (F4, @ 0x800f2d48, `MonsterBattleEntity.java:87`)

```
atk = ATTACK stat + spell_94.multi_04          // the *active* spell's multi also feeds physical damage
return atk × atk × 5 / target.getEffectiveDefence()   // truncated integer, quadratic in attack
```

No `level + 5` term, no rounding, no minimum here (the ≥1 clamp is pipeline step 8). `spell_94` is assigned by the monster's own script before it attacks (the L4 enemy controller scripts always assign the monster's base "attack spell" before the attack); the formula dereferences `spell_94.multi_04` unconditionally — at load time `spell_94` is null until the script assigns it (the decompiled retail path has no null guard either).

### 4.4 Elemental interactions (F6)

`applyElementalInteractions` loops over `attacker.getAttackElements()` (player: equipment elements — typically one, the weapon's; monster: the *active spell's* element as a singleton) and for each applies **both directions**: `attackElement.adjustAttackingElementalDamage(PHYSICAL, dmg, defElement)` then `defElement.adjustDefendingElementalDamage(PHYSICAL, dmg, attackElement)`. From `Element.java` (source-verified): attacking-side strong → `dmg × 150 / 100`, weak → `dmg × 50 / 100`, else unchanged; **the defending-side method is the identity in all 10 registered elements — no element overrides it, and the `attackType` parameter is unused in all of them** (it is a mod extension point; retail has no defensive elemental adjustment). With multiple attack elements the two adjustments compose per element (a 2-element attack with one strong and one weak multiplies ×15/20 = ×0.75 overall — the retail composition is sequential, not chosen).

The element strong/weak table (registry `elements`, 9 + NoElement, M8-03; `SimpleElement` = strong against `opposed`, weak against *itself*, boosts *itself*):

| Element | Flag | Strong vs | Weak vs | Boosts |
|---|---|---|---|---|
| NoElement | 0 | — | — | — (in-repo: none) |
| Water | 0x1 | Fire | Water | Water |
| Earth | 0x2 | Wind | Earth | Earth |
| Dark | 0x4 | Light | Dark | Dark |
| Divine | 0x8 | — (none) | — (none) | Divine |
| Thunder | 0x10 | — (none) | **Thunder** | Thunder |
| Wind | 0x40 | Earth | Wind | Wind |
| Light | 0x20 | Dark | Light | Light |
| Fire | 0x80 | Water | Fire | Fire |

So each directional pair (Water↔Fire, Earth↔Wind, Dark↔Light) is symmetric: attacking with element E against a defender of element T deals ×150% when T = E's opposed and ×50% when T = E. Divine and Thunder have no strong target; Thunder is uniquely weak against itself. `Element.flag` is `@Deprecated` (mods may add colliding flags; the registry id is the identity).

### 4.5 Power adjustment (F7)

`adjustDamageForPower(damage, attacker.powerAttack, defender.powerDefence)` (`Battle.java:8544`, static): float multiplier `base = 1.0`; `attacker < 0 → base /= 2`; `defender > 0 → base /= 2`; `attacker > 0 → base += 0.5`; `defender < 0 → base += 0.5`; return `(int)(damage × base)` (truncating float cast). The multiplier table over (attacker sign, defender sign): (+,−) 1.5, (+,0) 1.5, (0,−) 1.5, (0,0) 1.0, (+,+) 1.0, (−,−) 1.0, (−,0) 0.5, (0,+) 0.5, (−,+) **0.25**. (Power magnitudes are only ever ±50 or 0 — §5.5 — so the sign is the whole story.)

### 4.6 Dragoon space (F8-partial)

`applyDragoonSpace` (physical step 4; the magic path has the same step inline): when `Battle.dragoonSpaceElement_800c6b64 != null` (set when a player transforms, `scriptSetDragoonSpaceElementIndex(charId)` — the L4 script's ACTION_SPECIAL does this after loading the dragoon deff), each attack element gets `spaceElement.adjustDragoonSpaceDamage(PHYSICAL, dmg, attackElement)`: `boostsElement(attackElement)` → ×150% (the space boosts its own element), `attackElement.isStrongAgainst(spaceElement)` → ×50% (attacking *against* the space's weakness is halved), else unchanged.

### 4.7 Monster-only multipliers and self-buffs (F5)

- `MonsterBattleEntity.applyPhysicalDamageMultipliers` → `applyMagicDamageMultiplier(this, target, dmg, 0)` — the **spell** `damageMultiplier_03` table (§5.3) applies to monster *physical* attacks too (×8/×6/×5/×4/×3/×2/1.5/0.5/1). The player base is the identity.
- `MonsterBattleEntity.applyAttackEffects` → `applyBuffOrDebuff(this, this)` — a monster whose active spell has `buffType_0a` bits self-buffs for 4 turns (same `charId`, §5.5). The player base is a no-op (player buffs come from the HP%-spell branch, §5.1).

### 4.8 Resistance and immunity (F11)

`applyDamageResistanceAndImmunity(damage, attackType)` (base @ 0x800f29d4, `BattleEntity27c.java:327`): physical — `physicalImmunity_110 → 0`, else `physicalResistance_114 → dmg/2`; magical — `magicalImmunity_112 → 0`, else `magicalResistance_116 → dmg/2`. **Monster override** (`MonsterBattleEntity.java:73`): before the base, physical + `damageReductionFlags_6e & 0x2 → return 1`, magical + `& 0x1 → return 1` — the monster "min-1 damage" flags short-circuit *before* the immunity booleans, so a 0x2-monster takes exactly 1 physical damage even if it also had the 0x8 immunity bit (the bits coexist in the same word; retail data uses them disjointly). `loadMonster` maps 0x8 → `physicalImmunity_110` and 0x4 → `magicalImmunity_112` (F11). `applyElementalResistanceAndImmunity(dmg, element)` (base `:353`): `defender.equipmentElementalImmunity_22.contains(element) → 0`; **player override** (`PlayerBattleEntity.java:253`): first `equipmentElementalResistance_20.contains(element) → dmg/2`, then super. (The monster's `monsterElementalImmunity_74` set is the *same object* as `equipmentElementalImmunity_22` at load — one set, two names.)

## 5. Magical damage formulas

### 5.1 The battle-level pipeline (`Battle.calculateMagicDamage`, @ 0x800f204c, `Battle.java:8568`)

`magicType`: 0 = item, 1 = spell. Three branches:

1. **Stat-mod items** (`magicType == 0 && item.isStatMod()`): `item.calculateStatMod(attacker, defender)` — direct (heal/buff items are not damage at all; item damage proper is the branch-2 path with the item's own element/multiplier).
2. **HP%-spell branch** (spell, `spell_94.flags_01 & 0x4` — the "either buff spell or always hit" flag *doubles as* the HP%-damage marker on spells): `damage = defender.MAX_HP × spell_94.multi_04 / 100`; if `targetType_00 & 0x8` (attack all) the target set is all player bents (attacker is a player) or all *alive* monsters (attacker is a monster) — each gets `applyBuffOrDebuff(attacker, target)`; the attacker gets `status_0e |= 0x800` (the "no elemental effects" marker, consumed in the script entry points below to *skip* resistance/immunity).
3. **Normal branch**: `attackElement = spell.element_08 (spells) | item.getAttackElement() (items)`; `attackType = DRAGOON_MAGIC_STATUS_ITEMS (spells) | ITEM_MAGIC (items)`; `damage = attacker.calculateMagicDamage(defender, magicType)` (§5.2) → `attackElement.adjustAttackingElementalDamage` → `defender.getElement().adjustDefendingElementalDamage` (identity, F6) → `adjustDamageForPower(damage, attacker.powerMagicAttack_b6, defender.powerMagicDefence_ba)` → dragoon-space step (same as physical §4.6). Then `damage < 0 → 0`.

### 5.2 Entity base formulas

- **Player** (@ 0x800f2e98, `PlayerBattleEntity.java:236`): `matk = MAGIC_ATTACK stat (+ spell_94.multi_04 if spell)`; if dragoon: `matk ×= DRAGOON_MAGIC_ATTACK_STAT / 100`; `return (level + 5) × matk × 5 / target.getEffectiveMagicDefence()` (truncated; the dragoon-adjusted magic defence, §2.3).
- **Monster** (@ 0x800f8768, `MonsterBattleEntity.java:100`): `matk = MAGIC_ATTACK stat (+ spell_94.multi_04 if spell)`; `return matk × matk × 5 / target.getEffectiveMagicDefence()` (quadratic, truncated — the same shape as the monster physical formula with the active spell's `multi_04`).

### 5.3 The damage-multiplier table (F9)

`applyMagicDamageMultiplier(attacker, defender, damage, magicType)` (static, @ 0x800f946c, `Battle.java:8962`; magicType 0 = spell, 1 = item): `damageMultiplier = spell_94.damageMultiplier_03 (spells) | item.getAttackDamageMultiplier(attacker, defender) (items)`:

| `damageMultiplier_03` | Result |
|---|---|
| 0x01 | `damage × 8` |
| 0x02 | `damage × 6` |
| 0x04 | `damage × 5` |
| 0x08 | `damage × 4` |
| 0x10 | `damage × 3` |
| 0x20 | `damage × 2` |
| 0x40 | `damage + damage / 2` (×1.5, integer) |
| 0x80 | `damage / 2` |
| anything else (incl. 0) | `damage` |

### 5.4 Script entry points

- **`scriptDragoonMagicStatusItemAttack`** (@ 0x800f2694, `:8660`): `clearTempWeaponAndSpellStats()` + `setActiveSpell(spellId)` (the player-override path, §3.4) → `calculateMagicDamage(att, def, 1)` → `applyMagicDamageMultiplier(att, def, dmg, 0)` → **`dmg = max(1, dmg)`** → if `attacker.status_0e & 0x800` (HP%-spell): clear the bit and post **`AttackEvent` only** (resistance/immunity *skipped* — HP% damage and buff spells are not resisted); else `applyDamageResistanceAndImmunity` → `applyElementalResistanceAndImmunity(spell.element_08)` → `AttackEvent` + HUD element icon. Then `specialEffects = determineAttackSpecialEffects(att, def, DRAGOON_MAGIC_STATUS_ITEMS)`.
- **`scriptItemMagicAttack`** (@ 0x800f2838, `:8694`): `spell_94 = null` → `calculateMagicDamage(att, def, 0)` → `applyMagicDamageMultiplier(att, def, dmg, 1)` → `max(1, dmg)` → same 0x800-branch structure (item element) → `AttackEvent` → `specialEffects = determineAttackSpecialEffects(att, def, ITEM_MAGIC)` → **`applyItemSpecialEffects`** (@ 0x800f8854, `:8914`: `item.applyBuffs(att, def)` + `defender.recalculateSpeedAndPerHitStats()`).

The ≥1 minimum for magic is applied in the *script entry points* (unlike physical, where it is pipeline step 8) — so a spell that computes 0 damage still does 1 (pre-resistance); resistance can still halve it to 0 only via the immunity path (0) — a resisted min-1 spell deals 0 (the halving of 1 is 0), an immunity deals 0.

### 5.5 Buff/debuff (`applyBuffOrDebuff`, static, @ 0x800f9380, `Battle.java:8926`)

For each bit `i` of `attacker.spell_94.buffType_0a` (0x80 >> i, i = 0..7): `turnCount = attacker.charId_272 != defender.charId_272 ? 3 : 4`; `amount = i < 4 ? +50 : −50`; `switch(i % 4)`: 0 = `powerDefence_b8/b9`, 1 = `powerMagicDefence_ba/bb`, 2 = `powerAttack_b4/b5`, 3 = `powerMagicAttack_b6/b7`. So: `buffType_0a` bits 0x80/0x40/0x20/0x10 = def/mdef/atk/matk **up** +50, bits 0x08/0x04/0x02/0x01 = the same **down** −50; **3 turns on other species, 4 turns on the same `charId`** (self-buff for players — same character slot — and for a monster buffing its own kind). Source comment (L4): "This has been intentionally changed to attacker.buffType. Defender.buffType was always set to attacker.buffType anyway." — i.e. the retail code copied the attacker's buff type onto the defender first and read it back; the reference reads the attacker's directly (no behavior change). The power values then feed `adjustDamageForPower` (§4.5) for the rest of the battle — buffs are *damage multipliers*, not stat changes (the stat itself is untouched; `powerAttack` is the signed magnitude the float table consumes).

## 6. Accuracy, evasion, and targeting

### 6.1 The hit check (`Battle.checkHit`, @ 0x800f1aa8, `Battle.java:8490`)

```
isMonster = attacker instanceof MonsterBattleEntity
effectAccuracy:
    PHYSICAL:  isMonster ? attacker.spell_94.accuracy_05 : attacker.ATTACK_HIT stat
    DRAGOON_MAGIC_STATUS_ITEMS: attacker.spell_94.accuracy_05
    ITEM_MAGIC: 100
hitStat = attackType.tempHitStat.applyAsInt(attacker)          // signed percent (temp stat)
effectAccuracy = effectAccuracy × (hitStat + 100) / 100
avoidChance = (PHYSICAL ? defender.ATTACK_AVOID : defender.MAGIC_AVOID) stat
modifiedAvoidChance = avoidChance × (attackType.tempAvoidStat.applyAsInt(attacker) + 100) / 100
hit = (modifiedAvoidChance < effectAccuracy)
      && (effectAccuracy − modifiedAvoidChance ≥ simpleRand() × 101 >> 16)
if ITEM_MAGIC && attacker.item_d4.alwaysHits(): hit = true
else if (attackType.alwaysHitStat.applyAsInt(attacker) & attackType.alwaysHitMask) != 0: hit = true
```

The roll is `simpleRand() × 101 >> 16` ∈ [0, 100] (16-bit LCG word × 101, 16-bit right shift — max = 65535 × 101 >> 16 = 100). With `d = effectAccuracy − modifiedAvoidChance`: `d ≤ 0` → never hits (first conjunct); `d ≥ 100` → always hits (roll ≤ 100 ≤ d); `1 ≤ d ≤ 99` → hits iff `roll ≤ d`, i.e. **P(hit) = (d + 1) / 101** — a 101-step window (the roll includes 0). The attacker's *temp* hit stat modifies the attacker's accuracy as a signed percent, and the attacker's *temp avoid* stat modifies the **defender's** evasion (it is the attacker's `tempAttackAvoid_c0`/`tempMagicAvoid_c2` that scale the defender's avoid — the "debuff the target's evasion" buff works through the attacker's own temp slot).

### 6.2 `AttackType` table (source-verified, `AttackType.java`)

| | `tempHitStat` | `tempAvoidStat` | `alwaysHitStat` | `alwaysHitMask` |
|---|---|---|---|---|
| `PHYSICAL` | `tempAttackHit_bc` | `tempAttackAvoid_c0` | `spell_94.flags_01` | 0x4 |
| `DRAGOON_MAGIC_STATUS_ITEMS` | `tempMagicHit_be` | `tempMagicAvoid_c2` | `spell_94.flags_01` | 0x4 |
| `ITEM_MAGIC` | `tempMagicHit_be` | `tempMagicAvoid_c2` | `→ 0` | 0xe0 (unused — the item's `alwaysHits()` is checked instead) |

So "always hit" for spells = the active spell's `flags_01 & 0x4` (the same bit that marks HP% spells and buff spells — a spell is *either* always-hit *or* HP%-damage/buff in practice, per the `SpellStats0c` javadoc "either buff spell or always hit (or both)"); for items = the item's `alwaysHits()` (a per-item property).

### 6.3 Script entry points

`scriptCheckPhysicalHit` (@ 0x800f95d0, `:9022`), `scriptCheckSpellOrStatusHit` (@ 0x800f9618, `:9031`), `scriptCheckItemHit` (@ 0x800f9660, `:9040`) — each calls `checkHit` with its `AttackType` and writes the bool. The L4 script calls them before every damage application (misses skip the damage pipeline and play a miss animation; misses still consume the turn).

### 6.4 Targeting

`scriptGetItemOrSpellTargetingInfo` (@ 0x800f4600, `:8773`) returns the HUD list-menu's targeting state (0 = nothing selected, 1 = item/spell selected, −1 = menu unloading; the target bent index, −1 for attack-all; the item/spell registry ID). `scriptGetItemOrSpellAttackTarget` resolves the actual target(s). The L4 script's **`RANDOMIZE_TARGET_ARROW_IF_FLAG_SET`** (`player_combat_script.txt:6681`): when `var[45][244] & 0x2` (battle var `fieldFlags_550`) is set, the target arrow is randomized — `rand var[53]` (alive-monster count) draws a monster index and forces the HUD target to it (attack-all/random-target presentation). Position anchors: `targetArrowPos_78` (the 3-float arrow position, §2.2) and `middleOffsetX_84/Y_86` (1/100-scale FX anchor for archer/item-throw/ailment effects) come from `MonsterStats1c` for monsters. The on-screen targeting UI (arrow cursor, selection grid) is 05c scope.

### 6.5 `scriptGetHitProperty` (@ 0x800cd0ec, `Battle.java:4384`)

`bent → battlePreloadedEntities.getHitProperty(bent.typeBentSlot_276, hitNum, propertyIndex)` — the §2.7 accessor; the script uses it for addition animation scales (index 100), hit distances, and the damage-multiplier sum (§4.2 uses the same data natively).

### 6.6 On-hit special effects (`determineAttackSpecialEffects`, `Battle.java:8815`)

- `ITEM_MAGIC`: returns `attacker.item_d4.getSpecialEffect(attacker, defender)` directly (per-item logic; the `CauseStatusItem` retail roll is documented in §12/F12b).
- Otherwise: `effectChance = attacker.getStatusEffectChance(attackType)`:

| Attacker | PHYSICAL | DRAGOON_MAGIC_STATUS_ITEMS | ITEM_MAGIC |
|---|---|---|---|
| Player | `onHitStatusChance_44` (weapon %) | `spell_94.statusChance_07` | (n/a — item path) |
| Monster | `spell_94.statusChance_07` | `spell_94.statusChance_07` | `onHitStatusChance_44` |

- **The roll (F12)**: *retail* `simpleRand() × 101 >> 16 < effectChance` (roll ∈ [0, 100] → P = chance/101 for chance ≤ 101); *reference* (GH#2440, commit `850c43d7e`): `seed_800fa754.nextInt(100) < effectChance` (roll ∈ [0, 100) → P = chance/100, non-deterministic `java.util.Random` seeded once at `Battle` construction, `Battle.java:505`). The retail probability semantics are therefore **source-verified from the pre-fix reference code** (the formula existed verbatim in the reference repo until the fix; the roll line is `Battle.java:8824` in the current code).
- On success: `statusType = attacker.getStatusEffectStatus(attackType)` (player PHYSICAL → `equipmentOnHitStatus_4a` weapon bitset; both kinds DRAGOON → `spell_94.statusType_09`; player/monster ITEM → unimplemented — unreachable, the item path returned above). If `statusType & 0xff != 0`: find the **first set bit from 0x80 down to 0x1** (index 0..7) → `effect = 0x80 >> index` — i.e. the ailment applied is the *highest* bit set (poison (0x80) wins over petrify (0x1) if both are set — the retail registry data observed in-repo sets a single status bit per weapon/spell, but the engine consumes the full bitset).
- Then `specialEffects = attacker.getSpecialEffectStat(attackType) & attacker.getSpecialEffectMask(attackType)`:

| | PHYSICAL | DRAGOON_MAGIC_STATUS_ITEMS | ITEM_MAGIC |
|---|---|---|---|
| Player stat | `specialEffectFlag_14` | `spell_94.flags_01` | (unimplemented — item path) |
| Player mask | **0x40** | **0xf0** | 0x80 |
| Monster stat | `spell_94.flags_01` | `spell_94.flags_01` | (unimplemented) |
| Monster mask | **0xf0** | **0xf0** | 0x80 |

  (Player physical checks only the executioner bit 0x40 of the *entity* word; spell and monster attacks check the full 0xf0 of the *spell* word — an asymmetry, F12c.) If `specialEffects != 0`: `(!isAttackerMonster && attackType != DRAGOON_MAGIC_STATUS_ITEMS) || specialEffects == 0x80 → effect = 0` — i.e. a *player* non-spell attack with the executioner bit replaces the ailment with `effect = 0` (the "one-hit-kill chance" behavior, the L4 script applies the executioner check on `effect == 0` + hit), and any attacker with the 0x80 special-effect bit also yields `effect = 0`. Finally: `defender.specialEffectFlag_14 & 0x80` (defender "resistance") → `effect = −1` (cancelled).
- Return semantics: **−1 = none, 0x1–0x80 = the ailment bit to apply, 0 = special-effect substitution (executioner path)**.

## 7. Healing, resources, and revival

### 7.1 Vitals access and clamping

HP/MP/SP are `VitalsStat`s (current/max) in the stat collection, script-visible via `scriptGetBentStat2`/`scriptSetBentStat`/`scriptSetBentRawStat` with `CURRENT_HP`/`MAX_HP`/`CURRENT_MP`/`MAX_MP`/`SP`. The L4 script's recovery routines (`RECOVER_HP` `:259`, `RECOVER_MP` `:282`, `RECOVER_SP` `:303`): skip if the target is `FLAG_DEAD` (0x40); `new = current + amount`; if `new ≥ max` the routine's clamp path renders `max − current` and writes `max` (otherwise it renders/writes the amount/new value — the stat is effectively capped at max either way, and `FractionalStat.getCurrent()` additionally clamps on read); `scriptRenderRecover(bent, amount, colourIndex)` (colour 0x7 HP / 0xc MP / 0xd SP — the regen path's `ALLOCATE_REGEN_EFFECT` type 0/1/2 is a separate argument). Healing is additive-only; damage (including negative regen) goes through the damage pipeline (§7.3).

### 7.2 SP give/consume

`scriptGiveSp` (@ 0x800f43dc): `SP += amount`; **`spGained_800bc950.mergeInt(character, amount)`** (the post-battle SP report, §3.7); returns the new total. `scriptConsumeSp` (@ 0x800f4518): `SP −= amount` (param 2; param 1 is "unused"). SP is the dragoon resource: the dragoon-turn SP economy is in §7.7.

### 7.3 Regen (`HANDLE_REGEN`, `player_combat_script.txt:6502`)

Runs at the **start** of each player's turn (inside the `START_TURN` block `:6136` — "disables guard, ticks turn-limited effects, applies regen" — after the guard off + shield decrements, before the action):
- **Gate**: if the bent `IS_DRAGOON` (storage flag 0x2 — `stor[31]` set by the main loop's `IS_DRAGOON`) → skip regen entirely (no config check in `HANDLE_REGEN` itself; the `lod_core:equip_effects_in_dragoon` config gates a different dragoon path — the on-hit vitals grant `GIVE_ON_HIT_VITALS` `:6588`, which skips dragoons only when the config is off).
- **HP regen**: `HP_REGEN` stat; `> 0` → `RECOVER_HP(MAX_HP × regen / 100)` — **a percentage of max HP** (integer division), regen effect (type 0) + 10-frame wait; `== 0` → skip; `< 0` → **damage over time**: reaction sound, `amount = MAX_HP × |regen| / 100`, set the bent's own bit in `var[45][188]` (the "taking damage" slot word — the same word that gates shield consumption, §7.6), then run the full **`APPLY_DAMAGE`** pipeline (guard halving, shield nullification, cap, incoming multipliers all apply to DoT), exit if `FLAG_DEAD`.
- **MP regen**: `MP_REGEN` stat → `RECOVER_MP(regen)` — **an absolute amount** (not a percentage), regen effect type 1 + 10-frame wait.
- **SP regen**: `SP_REGEN` stat → gated by `CAN_BECOME_DRAGOON` (`status_0e & 0x2000`, `:712`) → `RECOVER_SP(regen)` — absolute, regen effect type 2 + 10-frame wait.

The asymmetry is exact: HP regen is percent-of-max (and can be negative = DoT), MP/SP regen are absolute. (The `hpMulti_13c`/`mpMulti_13e` equipment multipliers and the character-base HP/MP come from the stat system — M9-06 scope.)

### 7.4 Revival (`TRY_TO_REVIVE`, `:6640`)

Called from the death handler *before* the death is committed (`:5855–5880`): `REVIVE` stat; `rand 100` (roll = `100 × simpleRand() >>> 16` ∈ [0, 99]); **success iff `REVIVE > roll`** → P = REVIVE/100 for REVIVE ≤ 100, certain for REVIVE ≥ 100; **blocked if `STATUS stat & 0x1`** (petrified cannot auto-revive); on success `PROC_REVIVE` (`:6655`): allocate the revival effect, 40-frame wait, if the current animation is 0xe (death) switch to animation 9 (revival) and wait, then **`RECOVER_HALF_HP`** (`:5471`: `MAX_HP >> 1` → `RECOVER_HP` — revives at exactly half max HP) and set idle. Only if revival fails does the script set `CURRENT_HP = 0` and call `scriptSetBentDeadAndDropLoot(dead = true)`.

### 7.5 Escape (`ACTION_RUN_AWAY`, `:4961`)

- Pre-checks: `var[45][77] & 0x8` (bit 3 of `runAwayFlags_2b4` — escape disabled) or `escapeChance == 0` (`var[46][2]`) → immediate failure.
- Roll: `scriptGetBentStat(bent, ESCAPE_BONUS_151)` → `total = escapeChance + escapeBonus`; `rand 0x64` (= `rand 100`; roll = `100 × simpleRand() >>> 16` ∈ [0, 99]); **success iff `total > roll`** → P = total/100 for total ≤ 100, certain for total ≥ 100; `var[45][77] & 0x1` (bit 0) forces success — the "no real fight" encounters.
- Success → `scriptSetPostBattleAction(5)` = **MERCHANT** (the post-battle merchant screen stands in for the retail "ran away → back to town" flow) + `yield`. Failure → the turn is consumed (the script falls through to the normal turn actions).
- The `ESCAPE_BONUS_132` stat = character base + Σ equipment `escapeBonus` (§3.2) — it is a *flat percent added to the encounter's base chance*, not a multiplier.

### 7.6 Guard and shields

- **Guard action** (`ACTION_GUARD`, `:4770`): allocate guard-or-on-damage-recover effect; `GUARD stat = 1`; guard animation 5 (skipped while a dragoon); then the **guard heal**: `newHP = min(MAX_HP, CURRENT_HP + MAX_HP × GUARD_HEAL / 100)` — the `GUARD_HEAL` stat (character base + Σ `equipment.getGuardHealBonus()`, base `Equipment` = 0, §3.2) is **a percentage of max HP**, rendered as a heal (colour 7) and written via `scriptSetBentRawStat(CURRENT_HP)`.
- **Guarding halves incoming damage** (L4 script `APPLY_DAMAGE`): if `GUARD stat != 0` → guard reaction sound, guard effect, (guard *animation* only if not incapacitated, not a dragoon, and no shield active) → `damage >>= 1` (`shr 0x1`), then `damage = min(damage, 999999999)` (the global damage cap).
- **Shields** (the word `StatusConditions20.shieldsSigStoneCharmTurns_1c`, script `var[128+slot][7]`): `CHECK_MATERIAL_MAGICAL_SHIELD` (`:5735`) — first gate: if the bent's own bit is set in `var[45][188]` (battle var = `battleStateEf4._470`, the "already taking damage" word — set by the DoT path and the attack pipeline) → no shield; else read the shield word `& 0xf`: bits 0x1/0x2 (material shield), 0x4/0x8/0xC (magical shield — values are multiples of 4); returns 1 = material, 2 = magical, 0 = none. The caller (`APPLY_DAMAGE`) matches the shield kind to the attack kind (physical ↔ material, magical ↔ magical) and **a matching shield nullifies the hit — damage is set to 0** (`mov 0x0, stor[32]`). `START_TURN` (`:6136`) decrements the word per kind each turn: material bits (`& 0x3` set) → `word − 1`; magical bits (`& 0xc` set) → `word − 4`; sigil-stone bits (`& 0xc0` set) → `word − 0x40`; charm-potion bits (`& 0x300` set) → `word − 0x100`. Bit `0x400` is a separate "died as dragoon" latch in the same word: `HANDLE_DEATH` (`:5837`) sets it when a dragoon-form entity dies, and if the latch is already set (a second death event) it clears it and runs `TRANSFORM_OUT_OF_DRAGOON` (death detransforms). While bewitched (`STATUS & 0x2`), `START_TURN` also writes `var[128+slot][4] = −1` and `var[45][184+slot] = −1` (battle var = `battleStateEf4._460[slot]`, offset 0x460, field unresolved) every turn.
- **`IS_INCAPACITATED`** (`:689`): `var[129+slot] & 0xf == 4` (stunned — the status-effect index, §8.3) **or** `FLAG_DEAD` → incapacitated (cannot act; the incapacitation check is consulted for the guard animation and the action menu — 05c).

### 7.7 Dragoon SP economy (source-verified; the L4 script is a replacement — 05a B7)

The dragoon system is budgeted in units of **100 SP = 1 turn**, and the L4 script manipulates **two independent per-character (slots 0–2) battle-var words** in `BattleStateEf4` (the script's `var[45][N]` = `readBattleVar(N)`, `GameVarArrayParam`):

| Word | Offset | Script var | Reference writers / readers (L4 script) |
|---|---|---|---|
| `dragoonTurnsRemaining_294[3]` | 0x294–0x2a0 | `var[45][69+slot]` | `INIT_PLAYER` (zeroed at battle entry, `:6004`); the main-loop per-turn tick (decremented + read, `:4598`); `RECOVER_SP` (extended by regen/100, `:311`) |
| `battleStateEf4._234/_238/_23c` (unresolved fields, a 3-slot word at 0x234) | 0x234–0x23c | `var[45][45+slot]` | `ACTION_TRANSFORM` (`:5118`) / `ACTION_SPECIAL` (party loop `:5003`) (written = SP/100 at the dragoon action); `DETRANSFORM_FROM_DRAGOON_AFTER_TURN` (`:5176`, written `= 1`); retail story scripts `DRGN0/5056`, `DRGN0/5278` (read/written via retail function 371 — not registered in the reference's 1024-entry table, a no-op there) |

- **Dragoon action (transform / special)**: `ACTION_TRANSFORM` (the L4 "transform" menu action, `:5107`) and `ACTION_SPECIAL` (the "special" action, `:4992`, which runs the budget loop over **all** party slots `var[34][0..var[35]−1]`) both do, per slot: `var[45][45+slot] = SP / 100` (integer division — **the budget is initialized from SP here**), then `consume = SP − (SP/100) × 100` (= SP mod 100) → `scriptConsumeSp(bent, 1, consume)` — **SP is truncated to a multiple of 100** at the start of the dragoon form (`scriptGiveSp(bent, 0, out)` is used as an unmodified current-SP read; `scriptConsumeSp` is an unclamped `sp −= amount`, `Battle.java:8749` — a negative consume would raise SP, but here `consume ≥ 0` always). `ACTION_TRANSFORM` then loads the dragoon deff (`scriptLoadDragoonDeffSync`, `DRAGOON_TRANSFORM_DEFF` or the short-transform flag `0x21`), stage, and sounds; `ACTION_SPECIAL` additionally attaches the foot-model shadow part.
- **Per-turn tick** (main loop `:4598`; runs on the dragoon's own turns when `IS_DRAGOON` (storage flag 0x2) and `var[45][242] != 2`): `dragoonTurnsRemaining_294[slot]` is **decremented** and re-read; then if `budget ≠ SP / 100` the entity **consumes exactly 100 SP** (`scriptConsumeSp(bent, 1, 100)` — the `budget == SP/100` comparison is a consistency guard so externally-spent SP is not double-charged); then if the decremented budget `== 0` → **auto-detransform** (`TRANSFORM_OUT_OF_DRAGOON` + clear `var[45][245]` bit 0, the in-dragoon-form flag).
- **The two words do not interlock in the reference**: the tick's budget word (`dragoonTurnsRemaining_294`, var[45][69+slot]) is zeroed by `INIT_PLAYER` and is **never initialized from SP** — the dragoon action writes the *other* word (var[45][45+slot]). Consequences in the reference (all source-verified): the tick budget goes 0 → −1 → −2 … on successive dragoon turns, so the `budget == 0` auto-detransform is never taken, the `budget == SP/100` guard is never true, and the entity simply **loses 100 SP per dragoon turn until SP is exhausted** (SP clamps at 0 via `VitalsStat`'s `Math.max(0, ·)` validator); the dragoon then persists at SP 0 until it is detransformed manually (selecting "transform" again runs `ACTION_DETRANSFORM` — a flat 100 SP consume, `:5164`), by death (`HANDLE_DEATH` → `TRANSFORM_OUT_OF_DRAGOON`), or when the battle ends. Whether the *retail* tick read the same word (the decompiled state field's name "turns remaining" and the tick's arithmetic strongly suggest a retail SP/100 initialization exists somewhere the reference does not reproduce) is outside in-repo evidence — asset-validated-pending (F-dragooninit, §15 item 9).
- **SP regen extends the tick budget**: `RECOVER_SP` (`:303`) — SP recovery applies to the dragoon as usual (`scriptGiveSp(bent, regen, out)` + render, colour 0xd), and *additionally* when the entity's `storage_44[7] & 0x2` (dragoon flag) is set, `dragoonTurnsRemaining_294[slot] += regen / 100` (`:311`) — in the reference this extends a negative word (see above), but the mechanism is source-verified.
- **Immediate detransform / detransform-after-turn**: `ACTION_DETRANSFORM` (`:5160`) — `DETRANSFORMATION_MODE != 1` → flat `scriptConsumeSp(bent, 1, 100)` + `TRANSFORM_OUT_OF_DRAGOON` + clear `var[45][245]` bit 0; `DETRANSFORMATION_MODE == 1` (the `DETRANSFORM_FROM_DRAGOON_AFTER_TURN` path, `:5173` — mode 1 comes from `GET_TRANSFORM_MODE` `:5280`: first-ever transform or `STATUS & 0x4000`) → writes `var[45][45+slot] = 1` (the other budget word).
- **`spGained_800bc950`** (§3.7): `scriptGiveSp` (`Battle.java:8729`) accumulates every SP *gain* amount per character (`mergeInt`) — the post-battle report's "SP gained" figure (05c). The dragoon action's `scriptGiveSp(bent, 0, out)` calls add 0, so the truncation itself does not pollute the counter.
- `getArcherSp()` (`PlayerBattleEntity.java:380`) — the archer dragoon's base SP — is the dlevel table 0 → 0, 1 → 35, 2 → 50, 3 → 70, ≥ 4 → `100 + (dlevel − 4) × 50`, then the mutable **`ArcherSpEvent`** (M8-02). The dragoon transform itself (deff load, `dragoonDeffsWithExtraTims_800fb040` 24-entry table, `dragoonDeffFlags_800fafec` 84-entry flag table, `dragoonSpaceElement` set, stage change via `CHANGE_BATTLE_STAGE`) is 05c/05a scope (05a §10 stage system); the SP side is fully source-verified here.

## 8. Elements and status effects

### 8.1 Element registry

Nine elements + NoElement (M8-03 census: 9 element entries + 10 element icons), registry `elements`, `Element` base (`Element.java`) + `SimpleElement` (opposed-element constructor) + the four specials (Divine/Thunder/NoElement override the predicates). Flags (deprecated, §4.4 table): 0x0/0x1/0x2/0x4/0x8/0x10/0x20/0x40/0x80. `ElementSet` is a `Set<Element>` with a packed-int codec (the `monsterElementalImmunity_74`/`equipmentElementalImmunity_22` wire form for saves, M9-06). The `attackType` parameter on all three adjust methods is unused by every in-repo element (extension point).

### 8.2 Adjustment semantics (source-verified, §4.4/§4.6)

Attacking: strong ×150/100, weak ×50/100 (integer). Defending: identity (all 10). Dragoon space: space boosts its own element ×150%; an attack strong *against* the space element ×50%. No other elemental math exists in the reference (no elemental drain, no triple-weakness, no resistance per element beyond the set-based immunity/half).

### 8.3 Status ailments — storage and lifecycle

Three storage sites (all script-visible):
1. **`status_0e` (the `STATUS` stat)** — the live ailment *bitset* (0x1–0x80) + 0x800/0x2000/0x4000. The script reads/writes it via `scriptGetBentStat(STATUS)` / `scriptSetBentStat(STATUS)`; the ailment bits are what `APPLY_INCOMING_DAMAGE_MULTIPLIERS` (fear 0x8), `TRY_TO_REVIVE` (petrify 0x1), `CAN_BECOME_DRAGOON` (0x2000), and the death handler test.
2. **`status_384` = script `var[129+slot]`** (`Status04`, packed `effect | turns<<8 | unknown<<16`) — the *currently-ticking* ailment: one index 0–7 + a turn counter. `GameVarArrayParam` cases 129–136 map `allBents[n].status_384.pack()/unpack()` (the reference *moved* this from `BattleStateEf4` into the entity, same script view).
3. **The ailment bitset → index mapping** (the `STATUS_AFFLICTION_FLAGS` lookup, `patches/libs/lookup_tables.txt:1–10`, and `Status04.statusEffect_00` 0–8): 0 = petrified (0x1), 1 = bewitched (0x2), 2 = confused (0x4), 3 = fearful (0x8), 4 = stunned (0x10), 5 = weapon blocked (0x20), 6 = dispirited (0x40), 7 = poisoned (0x80).

Lifecycle (L4 script, `HANDLE_STATUS_CONDITIONS` `:6218`): at the start of a player's turn — skip in tutorial fights (monster IDs 0xfd–0xff, `IS_IN_TUTORIAL` `:6330` — with a source comment: "fixed retail bug here, < and > were flipped" — the reference *corrects* a retail comparison bug, F14); if `STATUS & 0xff != 0`, find the **first** set flag (iterate 0..7, first match — the *lowest* bit, unlike `determineAttackSpecialEffects` which takes the *highest*) and `APPLY_STATUS_CONDITION(idx)` (`:6244`): `var[129+slot] = 0x300 | idx` (**status turns = 3**, the effect index), fork a new effect-manager child script (`SEffe::scriptLoadSameScriptAndJump` to `FORK_ENTRYPOINT_RENDER_TICK_STATUS` with p2 = idx), set the bent's bit in `var[45][183]` (the "status effect processing" word) and wait until it clears. The per-ailment behavior (petrified/stunned → incapacitated turn skip — `IS_INCAPACITATED`; fearful → the ×2 incoming multiplier — §7.6/APPLY_INCOMING_DAMAGE_MULTIPLIERS; confused → action/targeting disruption; poisoned → the DoT linkage — the negative `HP_REGEN` path of §7.3 is the only DoT mechanism and is stat-driven, i.e. the poison ailment's script leg is expected to interact with it) executes in that forked child (entrypoint 11, indexed by p2 — the fork's child flow is 05c script-flow scope; the *effects* each ailment has on the verified native/script mechanics above are source-verified as listed). **No native code ticks `statusTurns_01`** (grep-verified: only `pack`/`unpack` touch it) — the 3-turn countdown is entirely script-side.
- **Persistence out of battle**: the end-of-battle write-back (05a §8) writes `status_0e` back to the character with the mask `& 0xc8` — **only bewitched (0x40) and poisoned (0x80) persist past the battle** (the retail status-continuation rule); all other ailments and the 0x800/0x2000/0x4000 bits are cleared.
- **Status resist**: `equipmentStatusResist_24` (player, §3.2) / `monsterStatusResistFlag_76` (monster, §3.1) — the 8-bit resist mask is consumed by the script's ailment-application path (the L4 script checks the resisted bits before applying; the native side only stores the mask).

### 8.4 The special-effect flag words

- **Entity `specialEffectFlag_14`** (both kinds): 0x8 = "Attack All" display, 0x40 = "chance to kill" (executioner — `onHitStatusChance_44` doubles as its chance, per the field javadoc), 0x80 = resistance (cancels incoming special effects, §6.6). Players accumulate it from equipment `flags_00` (§3.2); monsters take it from `MonsterStats1c.specialEffectFlag_12` (0x1/0x2/0x4/0x8 damage-reduction word — the *same* word copied into `damageReductionFlags_6e`, §3.1; the 0x8/0x40/0x80 entity-word meanings are the player/equipment-side interpretation, while the monster-side 0x1/0x2/0x4/0x8 meanings are the damage-reduction ones — the word is polysemous by owner, F15).
- **`spell_94.flags_01`**: 0x4 = always hit / buff / HP%-damage (context-dependent, §5.1/§6.2), 0x8 = attack-all on the HP%-branch target set (§5.1).

## 9. Player/monster asymmetries

| Aspect | Player | Monster |
|---|---|---|
| Base physical formula | `round((L+5)·atk·mult·5/def)` — linear, rounded | `(atk + spell.multi)²·5/def` — quadratic, truncated |
| Base magic formula | `(L+5)·matk·5/mDef` (matk + spell.multi; dragoon % mult) | `matk²·5/mDef` (matk + spell.multi) |
| Physical damage multiplier | identity | the spell `damageMultiplier_03` table (×8…×0.5) |
| Attack effects step | no-op | self-buff (`applyBuffOrDebuff(self, self)`) |
| Attack elements | equipment elements (`equipmentAttackElements_1c`) | the active spell's element (singleton) |
| Accuracy source | ATTACK_HIT stat (PHYSICAL), spell accuracy (spells) | **spell accuracy for both** PHYSICAL and spells |
| Status chance | weapon % (PHYSICAL/ITEM), spell % (spells) | spell % (PHYSICAL/spells), `onHitStatusChance_44` (items) |
| Special-effect stat | PHYSICAL: entity word & 0x40; spells: spell word & 0xf0 | spell word & 0xf0 for both |
| Resistance (/2) | equipment sets (elemental) + equipment booleans (phys/magic) | booleans only if equipment-equivalent bits set (none at load); elemental-immunity set |
| Min-1 damage | never (0 possible pre-attack-event) | `damageReductionFlags` 0x2/0x1 → exactly 1 |
| Immunity (0) | equipment booleans | 0x8/0x4 flags → booleans at load |
| Guard/shields/regen/revive/escape | the full L4-script surface (§7) | not applicable (monsters don't guard/escape; monster "regeneration" is data-driven per-enemy-script) |
| Dragoon economy | SP/100 turns, `dragoonTurnsRemaining_294`, `dragoonSpaceElement` | n/a |
| Death | revive roll (REVIVE, half-HP, petrify-blocked) | no revival; loot on death |
| HP cap on heal | `maxHP` clamp in `RECOVER_*` | same native routines (used by enemy-heal scripts where present) |
| Turn cleanup | entity `turnFinished` + player on-hit temps | entity `turnFinished` only |

The quadratic monster formula is the single largest mechanical asymmetry: monster damage scales with the *square* of (attack + spell multi) divided by defence, while player damage scales linearly with (level + 5) × attack × 5 — the level term is the player's only progression lever inside the formula (the stats themselves are M9-06).

## 10. Script/native/data boundary (RQ7, formula surface)

Per 05a §7 the turn flow is scripted; this section fixes, for every formula-surface behavior, *where the number comes from*:

| Behavior | Source of the rule | Source of the numbers |
|---|---|---|
| Physical/magic damage pipelines | **Native** (`CoreMod.PHYSICAL_DAMAGE_FORMULA` chain, `calculateMagicDamage`) | Data: stats (character registry stats + equipment aggregation; `MonsterStats1c[400]`), spell `multi_04`/`damageMultiplier_03`/`element_08`/`targetType_00`, addition `damageMultiplier_04` sum, equipment elements/defence, `level_04` |
| Elemental ×1.5/×0.5 | **Native** (`Element.adjustAttackingElementalDamage`) | Data: element registry predicates (opposed pairs) |
| Power ±25…+50% | **Native** (`adjustDamageForPower`) | Buff/debuff ±50 set by `applyBuffOrDebuff` from spell `buffType_0a` (data) |
| Dragoon-space ×1.5/×0.5 | **Native** | Data: the transformed dragoon's element |
| Min-0/min-1 clamps, ≥1 magic min | **Native** (pipeline steps 5/8; script entry points for magic) | — |
| Resistance/immunity (0, /2, min-1) | **Native** (`applyDamageResistanceAndImmunity` + monster override) | Data: equipment booleans; `MonsterStats1c` 0x1/0x2/0x4/0x8 word |
| Accuracy (the 101-window roll) | **Native** (`checkHit`) | Data: ATTACK_HIT/MAGIC_HIT/avoid stats, spell `accuracy_05`, temp stats (buffs) |
| Always-hit | **Native** | Data: spell `flags_01 & 0x4`, item `alwaysHits()` |
| Status-infliction roll + ailment choice | **Native** (`determineAttackSpecialEffects`); retail roll = `simpleRand()×101>>16` (L4 changed, §11) | Data: weapon `onHitStatusChance_18`/`onHitStatus_1b`, spell `statusChance_07`/`statusType_09` |
| Executioner / resistance bits | **Native** (the mask logic) | Data: equipment `flags_00` (0x40/0x80), spell `flags_01` |
| Buff/debuff ±50, 3/4 turns | **Native** (`applyBuffOrDebuff`) | Data: spell `buffType_0a` |
| **Guard halving, shield nullification, fear ×2, dragoon dragon-block ×10, 999,999,999 cap** | **Script** (L4 `APPLY_DAMAGE`/`APPLY_INCOMING_DAMAGE_MULTIPLIERS`/`CHECK_MATERIAL_MAGICAL_SHIELD`; retail-anchored via the decompiled `GUARD` stat, shield word, `fieldFlags_550` (`var[45][244]`), `battleStateEf4._470` (`var[45][188]`)) | GUARD_HEAL stat (data), shield word turns (item/data-driven) |
| Regen (HP %, MP/SP absolute, DoT) | **Script** (`HANDLE_REGEN`; retail-anchored via the `HP_REGEN`/`MP_REGEN`/`SP_REGEN`/`revive` stats in the decompiled `Battle` stat enum + equipment fields) | Data: character + equipment regen values |
| Revive (half-HP, petrify-blocked) | **Script** (`TRY_TO_REVIVE`; retail-anchored via the `REVIVE` stat) | Data: `revive` stat |
| Escape (chance + bonus, roll ∈ [0, 99] window) | **Script** (`ACTION_RUN_AWAY`; retail-anchored via `escapeChance`, `ESCAPE_BONUS_132`, `runAwayFlags_2b4`) | Data: `Encounter.escapeChance`, equipment `escapeBonus` |
| Ailment ticking (3 turns, first-bit, fork) | **Script** (`HANDLE_STATUS_CONDITIONS`; retail-anchored via `Status04` + `status_0e`) | Data: the applied ailment index |
| Death/loot (gold, XP, drop roll, overflow) | **Native** (`scriptSetBentDeadAndDropLoot`) | Data: `EnemyRewards08[400]` (mutable via `EnemyRewardsEvent`), `ItemDrop.chance` |
| Dragoon SP (100/turn, budget words) | **Script** (main-loop tick `:4598`; retail-anchored via `dragoonTurnsRemaining_294` + the `_234/_238/_23c` words — §7.7) | Data: SP; the two budget words (no in-repo SP-derived initialization — §15 item 9) |
| SP/XP/gold accumulators, post-battle action | **Native** | — |
| Targeting (arrow randomization, selection) | **Script** + native HUD state (05c) | Data: `MonsterStats1c.targetArrowXYZ` |

The RQ7 conclusion for the formula surface: **the engine is the law, the script is the courtroom**. Every *magnitude* is native or data; every *interaction* around the damage number (halves, zeroes, caps, DoT timing, revive, escape) is script-side but retail-anchored by the decompiled battle-state fields it manipulates. A Rust reimplementation must reproduce the native pipelines exactly (they are deterministic given stats) and must choose a script substrate with the same control-flow semantics for the script-side rules — the retail script text itself remains unavailable (asset-validated-pending, 05a B7).

## 11. RNG and determinism (Q-70 battle leg, advanced)

Roll inventory on the formula surface (source-verified; retail semantics from the decompiled/native code + the pre-fix reference history):

| Roll | Site | Distribution | Used for |
|---|---|---|---|
| `simpleRand() × 101 >> 16` | `checkHit` (`Battle.java:8490`) | [0, 100] uniform | hit check (§6.1) |
| `simpleRand() × 100 >> 16` | `scriptSetBentDeadAndDropLoot` (`:4330`) | [0, 99] uniform | per-drop chance (§3.6) |
| `simpleRand() & 3` | `loadStageDataAndControllerScripts` (`:9300`, draw `:9320`) | [0, 3] | opening camera position index |
| `100 × simpleRand() >>> 16` (via Fate `rand 0x64`) | L4 script `ACTION_RUN_AWAY` | [0, 99] uniform | escape roll (§7.5) |
| `100 × simpleRand() >>> 16` (via Fate `rand 100`) | L4 script `TRY_TO_REVIVE` | [0, 99] uniform | revive roll (§7.4) |
| `seed_800fa754.nextInt(100)` (L4, GH#2440) | `determineAttackSpecialEffects` (roll line `:8824`) | [0, 100) | status-infliction roll — **retail was `simpleRand() × 101 >> 16` ∈ [0, 100]** (pre-fix code, commit `850c43d7e`) |
| `seed_800fa754` (L4) | effect/item selection (`:505`, 05a B4) | `java.util.Random` | presentation |

(`simpleRand` is the **16-bit** LCG — M4-05 §2.3 — so `N × simpleRand() >> 16` spans [0, N−1] for N ≤ 65536; the `rand 101` hit roll reaches 100 because 65535 × 101 >> 16 = 100.)

Consequences (advancing Q-70): (a) every *native* battle roll and the *script* `rand` op (M3-05: `bound × simpleRand() >>> 16`) consume the **same 16-bit `simpleRand` LCG** (seed 3, no re-seed in the battle path — 05a B4), so in retail the hit rolls, drop rolls, camera choice, escape roll, and revive roll are one interleaved stream (the retail player script's other `rand` uses — if any — join the same stream); (b) the retail status-infliction probability is `chance/101` (roll window [0, 100]) while the reference's is `chance/100` over a non-deterministic generator — a *behavioral* L4 change, not just a stream change (F12); (c) the LCG's modulo bias is negligible at these windows but the window *granularity* matters: a 100% drop (chance = 100) always passes (roll ≤ 99 < 100), a 50% chance passes exactly 50 of 100 rolls (roll < 50), and **any chance ≥ 100 is certain** — the same granularity makes `REVIVE ≥ 100` and an escape `total ≥ 100` certain. The turn-jitter L4 replacement (GH#2481) and the `turnOrderRng`/`seed_800fa754` statics are 05a B4 scope. `Battle` makes no `srand` call — the stream inherits the submap/wmap-seeded state (M9-03 SM2, M9-04 W7).

## 12. Retail/reference differences and preserved bugs (formula surface)

| # | Item | Layer | Status |
|---|---|---|---|
| F12 | Status-infliction roll: retail `simpleRand()×101>>16 < chance` (P = chance/101) → reference `seed_800fa754.nextInt(100) < chance` (P = chance/100, non-deterministic). Commit `850c43d7e` "fix simplerand spell status chances (closes #2440) (#2684)" | L4 change (retail semantics preserved in pre-fix code) | source-verified |
| F12b | `CauseStatusItem` (items that inflict statuses): the pre-fix code wrapped the effect in `if(simpleRand()×101>>16 < 101)` — a roll that *always* passes (roll ∈ [0, 100] < 101; dead code); the reference removed it. No behavior change | L4 cleanup | source-verified |
| F14 | `IS_IN_TUTORIAL` monster-range comparison: source comment "fixed retail bug here, < and > were flipped" — the L4 script *corrects* the retail tutorial-detection comparison | L4 fix (retail bug documented) | source-verified (L4 script) |
| F-bug-1 | Active-spell ID > 127: "retail bug, happens with Shiranda's d-attack and virulent arrow" — the player `setActiveSpell` override falls back to `SPELL127` (preserved, `PlayerBattleEntity.java:598`) | retail bug preserved | source-verified |
| F-bug-2 | `scriptSetTempSpellStats` (@ 0x800f96a8, `:9048`): "Caches selected spell's stats on a battle entity (no longer does anything)" — the body is commented out in the reference | L4 no-op (retail cached temp spell stats; the L4 script sets temps directly) | source-verified |
| F1 | Equipment/dragoon-stat words commented out and re-modelled (Java equipment map, `DRAGOON_*_STAT` stat types, `Addition` object, `spells` list); the surviving offsets are the decompiled retail layout | L4 re-model, same numbers | source-verified |
| F5c | `applyBuffOrDebuff`: "intentionally changed to attacker.buffType. Defender.buffType was always set to attacker.buffType anyway" | L4 simplification, no behavior change | source-verified |
| F-enemyhp | `ENEMY_HP_BARS_CONFIG` (L4 config): screen-space HP bars over monsters (26×4 quad, blue/yellow/red by fraction, `z*4` depth, −400 depth offset) rendered in `MonsterBattleEntity.bentRenderer` — pure layer-4 presentation | L4 addition | source-verified |
| F-dragoongate | Dragoons and L4: `HANDLE_REGEN` skips transformed dragoons **unconditionally** (no config); the `equip_effects_in_dragoon` (L4 config) gates the on-hit vitals grant (`GIVE_ON_HIT_VITALS`) for dragoons instead | L4 addition | source-verified |
| F-dragooninit | Dragoon SP budget words do not interlock in the reference: the dragoon action initializes `var[45][45+slot]` (the `_234/_238/_23c` words) to SP/100 and truncates SP to a multiple of 100, but the per-turn tick decrements `dragoonTurnsRemaining_294` (var[45][69+slot]), which `INIT_PLAYER` zeroes and nothing ever initializes from SP — so in the reference the tick budget goes negative, the `budget == 0` auto-detransform is never taken, and SP simply drains 100/turn to 0 (clamped) with no auto-detransform. The retail tick word/initialization is unknown (the story-script path uses unregistered retail function 371; asset-validated-pending). §7.7 / §15 item 9 | L4 script divergence (retail unresolved) | source-verified (the reference behavior) |
| F-swap | Character swap (GH#2882): game vars 48/50/52 (`GameVarParam.java:98–102`/`276–280`) = first alive bent / first alive player / first alive monster index; *writing* them does `aliveBents_e78.set(0, …)` etc. — the L4 swap mechanic swaps the *head of the alive lists* in place; `charSwapBentCache` (`Battle.java:609`, `Map<CharacterData2c, PlayerBattleEntity>`) is declared but **never referenced** (dead field in the reference) | L4 feature | source-verified |
| F-tutorial | Tutorial fights (monster IDs 0xfd–0xff) skip status-ailment processing entirely | retail-anchored (monster ID range in the ROM table) / L4 script | source-verified (L4 script) |
| — | The turn-jitter RNG (GH#2481) and the `turnOrderRng`/`seed_800fa754` additions: 05a B4 scope, not repeated | L4 | source-verified |

## 13. Findings and compatibility classification (F1–F20)

| ID | Finding | Layer | Classification |
|---|---|---|---|
| F1 | Entity storage: flags in `ScriptState.storage_44[7]`; partial struct re-layout in the reference (commented-out equipment words → Java objects); surviving offsets = decompiled retail layout | L4 re-model / L3 layout | source-verified |
| F2 | Physical pipeline = the 10-step `CoreMod.PHYSICAL_DAMAGE_FORMULA` chain (order: base → elemental → power → dragoon space → ≥0 → multipliers → attack effects → ≥1 → resistance → elemental resistance), mod-mutable `Formula` + mutable `AttackEvent` | L3 (mechanism L4-moddable) | source-verified |
| F3 | Player physical = `round((level+5)·atk·mult/100·5/def)`; mult = addition-hit property-4 sum × (dragoon or addition damage %) / 100, or dragoon % / 100 with no addition | L3 | source-verified |
| F4 | Monster physical = `(atk + activeSpell.multi)²·5/def` (quadratic, truncated); the ≥1 clamp is pipeline step 8, not the formula | L3 | source-verified |
| F5 | Monster-only: the spell `damageMultiplier_03` table (×8/6/5/4/3/2/1.5/0.5/1) applies to *physical* attacks; `applyAttackEffects` = self-buff (4 turns) | L3 | source-verified |
| F6 | Elemental math is attacker-side only (×150/×50, integer); `adjustDefendingElementalDamage` is identity in all 10 elements and `attackType` is unused — retail has no defensive elemental adjustment; the opposed-pair table (Water↔Fire, Earth↔Wind, Dark↔Light) is symmetric; Thunder weak against itself; Divine/Thunder have no strong target; multi-element attacks compose sequentially | L3 | source-verified |
| F7 | `adjustDamageForPower` = float multiplier {0.25, 0.5, 1.0, 1.5} over (attacker sign, defender sign), truncating cast; magnitudes only ever ±50 (buff source) | L3 | source-verified |
| F8 | Magic: HP%-spell branch (`flags_01 & 0x4`) = `MAX_HP × multi/100`, `targetType_00 & 0x8` = all-players/all-alive-monsters, applies `applyBuffOrDebuff` per target, sets `status_0e |= 0x800` → the script entry points then *skip* resistance/immunity; normal branch = entity formula → element → power → dragoon space → multiplier table → ≥1 (entry-point clamp) → resistance/immunity | L3 | source-verified |
| F9 | Player magic = `(level+5)·matk·5/mDef` (matk + spell.multi; dragoon matk % and dragoon mdef %); monster magic = `matk²·5/mDef`; the ≥1 min for magic lives in the script entry points (a resisted min-1 spell can still deal 0 via halving) | L3 | source-verified |
| F10 | `checkHit` (`Battle.java:8490`): P(hit) = (d+1)/101 for 1 ≤ d ≤ 99, d ≥ 100 always hits, where d = acc − avoid after signed-% temp adjustments (the attacker's temp hit scales accuracy; the attacker's temp avoid scales the *defender's* avoid); roll = `simpleRand()×101>>16` ∈ [0, 100] (16-bit LCG word); always-hit = spell `flags_01 & 0x4` (mask 0x4) or `item.alwaysHits()` (ITEM_MAGIC, base accuracy 100); monster physical accuracy = spell `accuracy_05` (players use the ATTACK_HIT stat) | L3 | source-verified |
| F11 | Monster `damageReductionFlags_6e` (= `MonsterStats1c` word): 0x2 → physical min-1 (short-circuits before immunity), 0x1 → magical min-1, 0x8 → physical immunity, 0x4 → magical immunity (mapped to the 0x110/0x112 booleans at load) | L3 | source-verified |
| F12 | Special effects: chance table (weapon %/spell % per kind); the ailment = the **highest** set bit of `statusType`; the retail roll = `simpleRand()×101>>16` ∈ [0, 100] (P = chance/101) — the L4 replacement is `seed_800fa754.nextInt(100)` (P = chance/100, non-deterministic, GH#2440); specialEffect masks: player physical = entity word & 0x40, player/monster spells = spell word & 0xf0; `effect = 0` = executioner substitution (player non-spell, or any 0x80 bit); defender entity word & 0x80 cancels | L3 + L4 (roll) | source-verified |
| F13 | Death/loot: `goldGainedFromCombat`/`totalXpFromCombat` accumulation; one retail drop per monster; roll `simpleRand()×100>>16 ∈ [0, 99] < chance` (P = chance/100; chance ≥ 100 certain); `FLAG_NO_LOOT` once-per-monster; `EnemyDrop` perform/overflow → post-battle inventory overflow lists | L3 | source-verified |
| F14 | Ailment lifecycle: 3 turns (`var[129+slot] = 0x300 | idx`), first-bit (lowest) selection per turn, forked status-effect script, `var[45][183]` processing word, tutorial-fight skip (monster IDs 0xfd–0xff, with the L4-corrected retail comparison), no native turn countdown (script-owned), end-of-battle persistence mask `0xc8` (bewitched + poisoned only) | L3 storage / script lifecycle (L4 replacement) | source-verified (native) / source-verified (L4 script, retail-anchored) |
| F15 | The `specialEffectFlag_14` word is polysemous by owner: player/equipment 0x8/0x40/0x80 (attack-all/executioner/resistance) vs monster 0x1/0x2/0x4/0x8 (damage reduction) — one decompiled word, two semantic tables | L3 | source-verified |
| F16 | Guard: `GUARD` stat bit; halving `damage >>= 1` (script); guard heal = `GUARD_HEAL` % of **max HP** (clamped, rendered); shield word `var[128+slot][7]` (0x1/0x2 material, 0x4–0xC magical in units of 4, 0xC0 sigil, 0x300 charm, 0x400 died-as-dragoon latch) — matching shield **nullifies** the hit (damage = 0); `battleStateEf4._470` (`var[45][188]`) "taking damage" word gates shield use; `START_TURN` decrements (−1 material, −4 magical, −0x40 sigil, −0x100 charm); bewitched (`STATUS & 0x2`) writes `var[128+slot][4] = −1` and `var[45][184+slot] = −1` (`battleStateEf4._460[slot]`) every turn; dead/petrified skip the `START_TURN` regen block | L3 fields / L4 script (retail-anchored) | source-verified |
| F17 | Incoming-damage modifiers (script): fear (STATUS & 0x8) ×2; dragoon (flag 0x2) + `var[45][244] & 0x1` (dragon-block active) ×10; cap 999,999,999 | L4 script (retail-anchored) | source-verified (L4) |
| F18 | Dragoon SP: 100 SP = 1 turn budget model over two per-character battle-var words (no interlock in the reference — F-dragooninit): the dragoon action writes `var[45][45+slot]` (`_234` word) = SP/100 and truncates SP to a multiple of 100; the per-turn tick (main loop `:4598`) decrements `dragoonTurnsRemaining_294[slot]` (var[45][69+slot]), consumes 100 SP iff budget ≠ SP/100, and would auto-detransform at budget 0 (never reached in the reference — the word is never initialized from SP); SP regen while dragoon extends the tick budget by regen/100; immediate detransform = flat 100 SP; `DETRANSFORM_FROM_DRAGOON_AFTER_TURN` writes the `_234` word `= 1`. `spGained_800bc950` tracks SP gained for the post-battle report; `getArcherSp` dlevel table (0/35/50/70/100+50·(dl−4)) + mutable `ArcherSpEvent` | L3 fields / L4 script (retail-anchored; tick-word initialization unresolved) | source-verified |
| F19 | Escape: `runAwayFlags_2b4` bit 3 = disabled, bit 0 = forced; `escapeChance == 0` → failure; roll `100×simpleRand()>>>16` ∈ [0, 99], success iff `escapeChance + ESCAPE_BONUS_132 > roll` (P = total/100, total ≥ 100 certain); success → post-battle action 5 (MERCHANT) | L3 fields / L4 script (retail-anchored) | source-verified |
| F20 | Regen (at turn start, `START_TURN`): HP = % of max (negative = DoT through the full damage pipeline — guard/shield/cap apply to DoT); MP/SP = absolute; dragoons skip `HANDLE_REGEN` unconditionally (L4), SP gated by `canBecomeDragoon` (0x2000); L4 `equip_effects_in_dragoon` gates the on-hit vitals grant for dragoons; revive = REVIVE/100 chance (roll ∈ [0, 99] < REVIVE; REVIVE ≥ 100 certain), petrify-blocked, half-max-HP | L3 stats / L4 script (retail-anchored) | source-verified |

All F-findings are `source-verified` against Severed-Chains @ `0c08c679b` (and, for F12's retail roll, against the pre-fix reference code at `850c43d7e^`). Script-side items marked "L4 script (retail-anchored)" are source-verified as the reference behavior; their *retail* identity rests on the decompiled `BattleStateEf4` fields they manipulate (the `GUARD` stat, shield word, `dragoonTurnsRemaining_294`, `runAwayFlags_2b4`, `HP_REGEN`/`REVIVE`/`ESCAPE_BONUS` stat enum entries all exist in the decompiled retail code) — the retail script text itself remains asset-validated-pending (05a B7). No `asset-validated` claims.

## 14. Cross-document dependencies

- **M9-05a (game/05a)**: the 362-slot script-function table (§7 — every function cited here is an entry in it), the turn system (§6 — `turnFinished` callers, forced turns), the loading-stage machine (stage 6 preload → `loadMonster`/`initPlayerBattleEntityStats` ordering), the data model (§5 — `BattleStateEf4` battle vars read here: 64 `numCompleteAdditionHits_280`, 66 `counterAttackStage_288`, 69–71 `dragoonTurnsRemaining_294[0..2]`, 72 `battleStateEf4._2a0` (unresolved field), 76 `specialFlag_2b0`, 77 `runAwayFlags_2b4`, 82–95 addition spinner words, 183 status-processing word, 188 taking-damage word, 244/245 modifier flags), the rewards model (B18), the exit path (§12 — the `status &= 0xc8` write-back), the `SEffe` overview (B16 — the effect-manager child forks of §8.3).
- **M9-05c (pending)**: additions in depth (the addition spinner, `AdditionHitProperties10` fields 0–3/6–15 beyond the formula-relevant ones, the `Addition` registry's `getHit`/`getSpMultiplier`/`getDamageMultiplier` implementations), battle UI/targeting screens (`scriptGetItemOrSpellTargetingInfo` consumers, the list menu), post-battle screens (consumers of `spGained_800bc950`/`goldGainedFromCombat_800bc920`/`totalXpFromCombat_800bc95c`/`livingChars_800bc968`, the MERCHANT post-battle action screen = the escape destination), the dragoon transform deff flow, the forked status-effect child flows (per-ailment behavior in full).
- **M9-06 (pending)**: the stat system's character side — base stat values, level-up curves, the 17 stat types' semantics beyond battle consumption, `CharacterData2c` (level_12/dlevel_13/status_10), equipment registry details (the 148 entries' actual `getGuardHealBonus` overrides, `alwaysHits()`, `getSpecialEffect` implementations), the `canBecomeDragoon` (0x2000) grant.
- **M3-05/06**: the Fate runtime (`rand` op semantics, `fork_reenter`, storage slots) and the 592-slot native catalog (the 362 battle slots this document cites).
- **M4-05 §2.3**: the two retail LCGs (`simpleRand` — 16-bit, seed 3; `rand` — 15-bit, seed 0x24040001) — the roll distributions here.
- **M7-01**: the battle-save absence (battle is not savable) and the `status &= 0xc8` persistence into saves.
- **M8-02/03**: the mutable events (`AttackEvent`, `MonsterStatsEvent`, `EnemyRewardsEvent`, `LoadEnemyEvent`, `ActiveSpellEvent`, `SpellStatsEvent`, `ArcherSpEvent`), the registries (spells 131, additions 35, encounters 512, elements 9, stat types 17).

## 15. Open items and evidence gaps

1. **Q-70 (advanced, battle leg)**: the formula-surface roll inventory (§11) — the native rolls (checkHit, drops, camera) and the script `rand` rolls (escape, revive) all draw the shared 16-bit `simpleRand` stream (seed 3, no in-battle re-seed); the retail status-infliction roll (chance/101 over [0, 100]) is established from the pre-fix reference code (`850c43d7e^`); the L4 changes (GH#2440 roll, GH#2481 turn jitter) are documented. The seed-source design decision remains M10 scope.
2. **`Status04.unknown_02`** ("possibly bool has status") and the `statusEffect_00` 0x40/0x80 unknown bits: no in-repo consumer beyond `pack`/`unpack`; the script writes only `0x300 | idx`. Unresolved — evidence gap (no write observed).
3. **`SpellStats0c._0b`**, **`EnemyRewards08._06`** (copied to `CombatantStruct1a8._19a`, never read in-repo), **`BattleEntity27c._142`** (OR'd with `equipment.onHitStatus_1b` at init, no other consumer): unresolved fields, decompiled-unknown.
4. **`charSwapBentCache`** (`Battle.java:609`): declared, never referenced — dead field in the reference (the GH#2882 swap works purely through the game-var 48/50/52 list-head writes). Recorded as an L4 remnant; no register item (no behavioral consequence).
5. **Retail player combat script**: all script-side mechanics (§7, §8.3) are source-verified as the *reference* (L4 replacement) behavior and retail-anchored via the decompiled battle-state fields; the retail script text is unavailable (asset-validated-pending, 05a B7). Where the L4 script *fixes* a retail bug (F14 tutorial comparison) or *adds* behavior (F-dragoongate, F-enemyhp), the retail side is documented as the pre-fix/absent behavior.
6. **Monster "no active spell" physical damage**: `MonsterBattleEntity.calculatePhysicalDamage` reads `this.spell_94.multi_04` unconditionally; the L4 per-enemy scripts always set the active spell before the first attack (the DRGN1/401 controller convention, 05a §7). If a retail monster script attacked before setting a spell, the retail binary's behavior is outside in-repo evidence (the decompiled Java would NPE; a retail 0-read is possible but unverified — asset-validated-pending).
7. **`hpMulti_13c`/`mpMulti_13e`**: accumulated at init (§3.2) but their consumer is the character stat side (M9-06) — no battle-side read observed in `Battle.java`. Recorded for M9-06.
8. **Executioner `effect = 0` consumer**: the native side returns 0 for the executioner path; the *application* of the executioner check (HP → 1 or instant kill, using `onHitStatusChance_44` as its chance) lives in the L4 script's damage handler (05c script-flow scope). The native contract (return 0 vs −1 vs an ailment bit) is fully source-verified.
9. **Dragoon SP budget word split (F-dragooninit)**: the reference's dragoon SP bookkeeping uses two per-character words that do not interlock — the dragoon action initializes `var[45][45+slot]` (the `_234/_238/_23c` fields, offset 0x234) to SP/100 and truncates SP to a multiple of 100, while the per-turn tick decrements `dragoonTurnsRemaining_294[slot]` (var[45][69+slot], offset 0x294), which `INIT_PLAYER` zeroes and nothing in the reference ever initializes from SP. Net reference behavior: 100 SP per dragoon turn drained to 0 (clamped by `VitalsStat`), no auto-detransform via the tick. Whether the retail tick consumed the *initialized* word (i.e. whether the retail `var[45][69]` slot mapped to the same memory the retail script's transform wrote) is outside in-repo evidence — the retail story scripts touch the `_234` word through retail function 371, which the reference does not register — **asset-validated-pending**. Recorded here and in F-dragooninit; not a register-level question (it does not block M10: the Rust implementation can reproduce the reference behavior verbatim or restore the presumably-retail word unification as a documented deviation).

No new register questions opened by this document (Q-70 advanced in place; items 2–4, 7, 8 are evidence gaps recorded here rather than register-level open questions — none blocks M10 design beyond the already-registered determinism decision). No `asset-validated` claims.
