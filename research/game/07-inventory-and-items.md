# 07 — Inventory and Items

M9 deliverable 07 (the final M9 checkpoint). Covers: the three inventory containers in `GameState52c` (the stack-based item `Inventory`, the unbounded equipment `List`, the unique-goods `GoodsInventory`) and the gold counter, the `InventoryEntry`/`ItemStack`/`Item`/`Equipment`/`Good` data models (including the retail byte-offset field layout and the retail ID→registry-ID mapping), inventory operations (give/take/merge/overflow, the drop pipeline, the mod-event surface), the complete retail item catalog (54 registrations) and the item-effect families (attack, recovery, purification, buff/shield, random-ball, utility), the battle item-use pipeline (the DEFF load + `throw_item` script-frame injection, the entrypoint table, the stor parameter words), equipment eligibility (the `CanEquip` + `EquipmentTypes` tag model, the per-character type sets) and equipment effects (the `onEquip` stat mods, the battle bridge, the four special equipment classes), the goods inventory (the dragoon spirits, the retail bitset pack/unpack), the field and battle UI state machines (the menu-stack model, `UseItemScreen`, `ItemListScreen`, `EquipmentScreen`, `TooManyItemsScreen`, `GoodsScreen`, the battle `ItemListMenu`, the message-box family), the shop system (data model, the retail shop catalog, the screen state machine, buy/sell transactions, the post-battle MERCHANT path), persistence (cross-reference to M7-01), and the script/native/data boundary for the whole surface (RQ7).

This document is the **producer side** of the equipment-stats transfer that 05b/05c documented from the battle side (`initPlayerBattleEntityStats`'s per-equipment bake, the `additionExtra_474` special-effect flags) and the **consumer side** of the post-battle report (05c's `PostBattleScreen` drop grant, M9-06's `givePendingXp`/SP/DXP flows) and the Dabas minigame (M9-01's reward flow). Battle mechanics already settled — the damage pipelines, the bent stat words (the `0xb4–0xd3` power/temp words the buff items write), the DEFF effect model, the post-battle *action* dispatch, the status-ailment bits — live in **05a**/**05b**/**05c** and are cross-referenced, not repeated. Save-file byte formats (the retail struct's `0x19C`/`0x1E8`/`0x2E9` fields, the per-character `0x14` equipment bytes, the V1/V2 tags) live in **M7-01**; this document covers the runtime *state* and *operations* those formats persist.

Sources (all read this session):

- Severed-Chains @ **`71ce130`** (updated baseline; the historical pinned baseline was `0c08c679b` — see §12.1 for the three-commit delta this document was checked against): `legend/game/inventory/` (`Inventory` full, `InventoryEntry` full, `ItemStack` full, `Item` full, `Equipment` full, `Goods` full, `GoodsInventory` full, `GoodsSource`, `EquipItemResult`, `EquipmentTypes` full, `EquipmentSlot`, `ItemIcon`, `WhichMenu`, `GatherAttackItemsEvent`/`GatherRecoveryItemsEvent`), `legend/game/SItem.java` (the item/equipment script functions `:249–420`, `addHp`/`addMp`/`addSp` `:421–517`, `takeItemFromSlot` `:518–543`, `takeEquipment`/`giveEquipment`/`giveItems`/`addGold` `:544–629`, the display/sort helpers `:655–770`, `equipItem`/`cacheCharacterSlots` `:987–1005`, `loadItemsAndEquipmentForDisplay` `:1089–1140`), `legend/game/inventory/screens/` (`UseItemScreen` full, `ItemListScreen` full, `EquipmentScreen` full, `TooManyItemsScreen` full, `GoodsScreen` full, `MessageBoxScreen`, `ChoiceMessageBoxScreen`, `CycleMessageBoxScreen`, `QuantityMessageBoxScreen`, `MenuStack`, `MenuScreen` (structure), `PostBattleScreen` (the drop/overflow states `:420–520`), `DabasScreen` (the reward path `:400–445`)), `legend/game/types/Shop.java`, `legend/game/combat/types/EnemyDrop.java`, `legend/game/inventory/ShopRegistry.java`/`ShopRegistryEvent.java`, `legend/game/modding/coremod/shops/` (`ShopExtension`, `ItemShopExtension`, `EquipmentShopExtension`, `GoodShopExtension`), `legend/game/combat/Battle.java` (`scriptUseItem`/`scriptApplyEquipmentEffect` table slots 1010/1011 `:1123–1124`/`:1239–1252`, `seed_800fa754` `:505`, `scriptRand` @ 0x800cee50 `:4802–4815`, `setMtSeed`/`scriptSetMtSeed` @ 0x800cfed0/0x800cff24 `:4959–4969`, the battle-end repeat-item return `:8386–8393`, the overflow clear `:1976–1977`, the drop-`EnemyDrop` construction `:4333–4335`, `scriptGiveItem` @ 0x800f99ec `:9150–9161`), `legend/game/combat/ui/ItemListMenu.java` (full), `legend/game/Scus94491BpeSegment.java` (`simpleRand` @ 0x800133ac `:224–235`, `rand()` `:588–592`, `srand` @ 0x8002d270, `scriptGiveChestContents` @ 0x800244c4 / `scriptGiveGold` @ 0x80024480 / `scriptTakeItem` @ 0x80024590 `:434–488`), `legend/game/Scus94491BpeSegment_800b.java` (`itemOverflow`/`equipmentOverflow` `:42–43`), `legend/game/scripting/GameVarParam.java` (the item/equipment slot accessors, case 125 shop ID `:200`/`:348`, the retail-ID inventory entry `:262`), `legend/game/modding/coremod/CoreMod.java` (`INVENTORY_SIZE_CONFIG` `:166`, `ITEM_GROUP_SORT_MODE` `:187`, the menu input actions `:195–214`), `legend/game/modding/coremod/config/InventorySizeConfigEntry.java` (default 32), `legend/lodmod/config/ItemStackSizeConfig.java` (default 1, 1–50), `legend/lodmod/LodItems.java` (full — 54 registrations), `legend/lodmod/items/` (all 31 classes), `legend/lodmod/LodEquipment.java` (full — 149 registrations incl. the four special subclasses), `legend/lodmod/equipment/` (`WargodCallingEquipment`, `UltimateWargodEquipment`, `DestroyerMaceEquipment`, `DetonateArrowEquipment`), `legend/lodmod/LodGoods.java`, `legend/lodmod/LodShops.java` (full), `legend/lodmod/LodMod.java` (`ITEM_IDS` `:212–222`, `EQUIPMENT_IDS` `:223–249`, `GOODS_IDS` `:250–263`, `SHOP_IDS` `:281–295`, `gatherEquipmentTypes` `:527+`, `gatherCharacterEquipmentTypes` `:679–688`, `gatherAttackItems` `:840–866`, `gatherRecoveryItems` `:869–877`), `legend/game/characters/CharacterData2c.java` (`canEquip`/`equip`/`getEquipment` `:122–164`), `legend/game/submap/SMap.java` (the shop-screen entry points `:4050`/`:5272`), `legend/game/saves/serializers/RetailSerializer.java` (the retail-ID consumers `:100–162`), `legend/game/saves/RetailSavedGame.java`/`SeveredSavedGame.java` (the `force`-give on load `:130`/`:110`), `patches/scripts/throw_item.txt` (the item-use entrypoints `:1–45`), `legend/game/combat/postbattleactions/MerchantPostBattleAction.java`
- Prior research: M9-05a (the 362-slot script-function table, the `@Method` convention), M9-05b (F1–F20: the bent stat words incl. the `0xb4–0xd3` temp words, the power-stat derivation, the elemental stages, the `scriptSetBentDeadAndDropLoot` drop roll), M9-05c (U1–U35: the equipment special-effect flag `specialEffectFlag_14`, the `additionExtra_474` words, the post-battle MERCHANT action value 5, the `PostBattleScreen` report), M9-06 (C1–C40: the `CharacterData2c` model, `cacheCharacterSlots`, the goods/spirit eligibility, the post-battle report state machine), M9-01 (the Dabas reward flow), M7-01 (the retail save struct: `0x19C` goods bitset, `0x1E8` equipment list, `0x2E9` item list, the `0x14+i` per-character equipment bytes), M8-02/M8-03 (the registries, the event surface, the core-mod config census)
- No retail assets are available: no `asset-validated` claims in this document. The item/equipment/shop price and stat values are L4 constants extracted by the reference author from the retail binary; they are **source-verified as reference data** and retail-byte-pending for the values themselves. The `throw_item` script is an L4 replacement script (the retail item-use scripts are not in the repo).

## 1. Scope and layer model

Four layers apply to this surface (05b §1):

- **Native (decompiled retail, L3)**: the `GameState52c` inventory fields (`items_2e9`, `equipment_1e8`, `goods_19c`, `gold_94`), the retail `@Method`-addressed routines preserved by the reference (`SItem.takeEquipment` @ 0x800233d8, `giveEquipment` @ 0x80023484, `giveItems` @ 0x80023544, `addGold` @ 0x8002363c, `takeItemFromSlot` @ 0x800232dc, `setInventoryFromDisplay` @ 0x800239e0, `sortItems` @ 0x80023a2c, `sortItems()` @ 0x80023a88, `equipItem` @ 0x80103a5c, `cacheCharacterSlots` @ 0x80103b10, `loadItemsAndEquipmentForDisplay` @ 0x80024480-adjacent, `Item.useInMenu` @ 0x80022d88, `ItemListMenu.cacheItems` @ 0x800f83c8, `UseItemScreen` portrait/slot geometry @ 0x800fc8c0/0x800fc8dc, `EquipmentScreen` geometry @ 0x800fc824, `SMap`/`Battle` item-script functions incl. `scriptGiveChestContents` @ 0x800244c4, `scriptTakeItem` @ 0x80024590, `scriptGiveItem` @ 0x800f99ec, `scriptUseItem`/`scriptApplyEquipmentEffect` table slots 1010/1011, `scriptSetMtSeed` @ 0x800cff24, `scriptRand` @ 0x800cee50, `simpleRand` @ 0x800133ac), the `itemOverflow`/`equipmentOverflow` statics, and the retail-ID→registry-ID tables (`ITEM_IDS`, `EQUIPMENT_IDS`, `GOODS_IDS`, `SHOP_IDS`).
- **L4 core-mod data (layer-4, modeling retail)**: the 54 item registrations with prices, the 149 equipment registrations with the full stat layout, the 37 goods, the 41 registered shops (39 named retail-ID shops), the `EquipmentTypes` assignments (equipment tag sets, the 9 character tag sets), the random-ball pools (`gatherAttackItems`/`gatherRecoveryItems`), the `RetailCampaignType` starting items (06 C34), and the configs (`INVENTORY_SIZE_CONFIG`, `ITEM_STACK_SIZE`, `ITEM_GROUP_SORT_MODE`).
- **L4 engine re-architecture**: the `Stat`/object inventory model (retail's item list was a flat `u8` array with no stacking — M7-01; the reference's `Inventory` of `ItemStack`s with merge/overflow is the L4 model that must round-trip the no-stacking retail format), the menu screens as Java classes (retail's menus were native; the reference re-implements them, preserving retail screen behavior and `@Method` addresses where identifiable), the registry/event mod surface, the message-box family, the equipment sort/filter/discard UI, and the vanilla config preset (M7-02).
- **Script (L3 retail originally; L4 replacement in the reference)**: the item-use scripts (`throw_item` entrypoints 32–44 — L4 replacement; the retail item-use scripts are not in the repo), the shop-ID write (game var 125, the MERCHANT battle script), the chest/give/take calls from plot scripts, and the MT seed write (`scriptSetMtSeed`). Item *rules* (stacking, overflow, price, eligibility, effect magnitude) are native + data; the *presentation* of an item effect (its animation/DEFF) is script/DEFF.

Layer tags: **L3** LoD-specific (decompiled retail), **L4** reference-implementation-specific.

## 2. The data model

### 2.1 The containers (I1)

`GameState52c` holds four independent inventory counters:

| field | type | capacity | semantics |
|---|---|---|---|
| `items_2e9` @0x2e9 | `Inventory` (stack list) | `maxSize` = `INVENTORY_SIZE_CONFIG` (default **32**, 1–9999, CAMPAIGN config) | stackable consumables; `ItemStack` entries |
| `equipment_1e8` @0x1e8 | `List<Equipment>` | hard cap **255** (`scriptGetMaxEquipmentCount` returns 255; `EquipmentShopExtension` and `Equipment` capacity checks use 255) | one entry per equipment object, no stacking |
| `goods_19c` @0x19c | `GoodsInventory` (set) | unlimited, unique entries | story goods + dragoon spirits; no quantity, no price |
| `gold_94` @0x94 | int | clamped at **99,999,999** (`SItem.addGold` @ 0x8002363c: `gold_94 > 99999999 → 99999999`) | shop currency |

The two overflow buffers are **static** (battle-scoped): `itemOverflow` / `equipmentOverflow` (`Scus94491BpeSegment_800b.java:42–43`), cleared at battle start (`Battle.java:1976–1977`) and consumed only by `TooManyItemsScreen` (§7.6).

### 2.2 `InventoryEntry` and the price model (I2)

`InventoryEntry<T>` (the interface all three entry kinds implement) defines: `getRegistryId()`, `getIcon()`, `getNameTranslationKey()`/`getDescriptionTranslationKey()`, `getSize()`/`getMaxSize()`/`isEmpty()`, `copy()`, `renderIcon()`, and the price defaults: `getPrice()` (deprecated, default 0), **`getBuyPrice() = getSellPrice() * 2`**, `getSellPrice() = getPrice()`. Both `Item` and `Equipment` override the pair to `buy = price × 2`, `sell = price` (the retail buy/sell asymmetry); `Good` keeps the defaults (both 0 — goods are never bought or sold).

### 2.3 `Inventory` — the item container (I3)

`Inventory` (`inventory/Inventory.java`) is an `ArrayList<ItemStack>` with a `maxSize` (0 = unlimited) and an event switch:

- `give(Item)`/`give(ItemStack)`: copies the stack, then `give(stack, force=false)`.
- `give(stack, force)`: posts `GiveItemEvent(this, stack, force ? OVERFLOW : FAIL)`; a canceled event or empty `givenItems` returns the original stack (nothing given); otherwise it **simulates** on a temp copy of itself (events disabled) to detect overflow; with `OverflowMode.FAIL` an overflow returns the stack untouched (the give fails atomically); otherwise it applies `giveImpl` for each item (with the `OVERFLOW` force bit) and returns `EMPTY`.
- `giveImpl(stack, force)`: (1) merge into existing same-item stacks via `current.merge(remaining)`, (2) place into the first empty slot, (3) append if `force || maxSize == 0 || size < maxSize`, else return the remainder.
- `take(Item)`/`take(stack)`: posts `TakeItemEvent` (the stack may be rewritten — a different item or smaller size); walks the list, on `isSameItem` shrinks both sides by the overlap, removes emptied slots; returns the unremoved portion.
- `takeFromSlot(slot, amount)`: clamps `amount` to `[0, stack.size]`; posts `TakeItemEvent`; if the event changed the item's identity it falls back to a regular `take`; after shrinking the slot, any shortfall is pulled from *other* stacks of the same item (`take(remaining)`); removes the slot when empty.
- `removeIfEmpty(slot)` / `removeIfEmpty(stack)`, `hasRoom(stack)` (merge capacity first, then slot count), `setSize`-style copy constructor, `disableEvents()`/`enableEvents()` (used by the temp-inventory in `give` and by `TooManyItemsScreen`'s stack-merging).

The mod-mutable points are the `GiveItemEvent`/`TakeItemEvent` pairs (the `givenItems`/`stack` lists are rewriteable; `OverflowMode` is chooseable).

### 2.4 `ItemStack` (I4)

`ItemStack` wraps `item` + `size` + `durability` + an optional `Tag extraData`:

- `EMPTY` is a singleton over `CoreMod.NOTHING`; `isEmpty()` = `size < 1 || durability < 1` (a fully-worn stack is empty).
- `durability`: `-1` at construction resolves to the item's `getMaxDurability`; `damage(amount)` decrements and **wraps across items** (`while (durability < 0) { durability += max; shrink(1); }` — the wear of a merged stack is distributed one item at a time).
- `merge(other)`: only same-item; transfers `min(remainingCapacity, other.size)`; **also propagates wear**: `this.damage(other.getMaxDurability() - other.getCurrentDurability())` — merging a worn stack into a fresh one costs durability on the receiver.
- `take(amount)` produces a new stack (clamped to `size`) and shrinks the original.
- Battle-attack surface (consumed by the 05b item-attack pipeline): `isStatMod()`, `calculateStatMod(user, target)`, `applyBuffs(user, target)`, `alwaysHits()`, `getSpecialEffect(user, target)`, `getAttackElement()`, `getAttackDamageMultiplier(user, target)`.
- Usage: `canBeUsed(UsageLocation)` / `canBeUsedNow(UsageLocation)` / `canTarget(TargetType)`; `useInMenu(response, charId)`; `useInBattle(user, targetBentIndex)`.
- Item flags: `isRepeat()` (returned after battle — the ∞ items, §4.8) and `isProtected()` (cannot be stolen by enemies; the discard UI also gates on the protected/non-discardable state).

### 2.5 `Item` — the abstract consumable (I5)

`Item` (icon + base price) provides the defaults: `getMaxStackSize = 1` (all retail stackable items override it with the `ITEM_STACK_SIZE` config, default 1, 1–50, CAMPAIGN — i.e. vanilla retail behavior is no stacking, matching M7-01's no-stacking save format), `hasDurability = false`, `isSame = item identity`, `canBeUsedNow = canBeUsed`, `applyBuffs`/no-op, `alwaysHits = false`, `getSpecialEffect = 0`, `getAttackDamageMultiplier = 0`, `getAttackElement`/`useInBattle`/`useInMenu` throw ("yet to be implemented") — every concrete item overrides what it supports. The enums: `UsageLocation { MENU, BATTLE }`, `TargetType { ALLIES, ENEMIES, ALL, INSTANT }` (INSTANT = no target selection). `Item.useInMenu` carries the retail address @ 0x80022d88.

### 2.6 `Equipment` — the retail 0x1c layout (I6)

`Equipment` (`inventory/Equipment.java`) is an immutable `RegistryEntry` + `InventoryEntry` + `ScriptReadable` whose fields carry the retail byte offsets as name suffixes (the decompiled retail layout, 0x1c per item):

| offset | field | meaning |
|---|---|---|
| 0x00 | `price` | base price (buy = 2×, sell = 1×) |
| 0x01 | `flags_00` | `0x4` = can't be discarded; `0x40` = instakill; `0x80` = resist instakill (javadoc; the consumer is the retail DEFF/battle layer, not native in the reference — see §15.3) |
| 0x02 | `slot` | `EquipmentSlot` enum: WEAPON, HELMET, ARMOUR, BOOTS, ACCESSORY |
| 0x03 | *(retail `_02`/`equipableFlags_03` — commented out in the reference)* | the `equipableFlags_03` word is undecoded (open item 7) |
| 0x04 | `attackElement_04` | `ElementSet` (the weapon's attack element(s)) |
| 0x05–0x07 | `elementalResistance_06`, `elementalImmunity_07` | `ElementSet`s |
| 0x08 | `statusResist_08` | status-ailment resist bits (the 05b status bit table) |
| 0x0a–0x0f | `mpPerPhysicalHit`, `spPerPhysicalHit`, `mpPerMagicalHit`, `spPerMagicalHit`, `hpMultiplier`, `mpMultiplier`, `spMultiplier` | per-hit resource generation (the 05b `0xcc–0xd3` temp words' equipment source) and max-stat percentage multipliers |
| — | `magicalResistance`, `physicalResistance`, `magicalImmunity`, `physicalImmunity` | booleans (the `canBeUsedNow`-style combat flags, 05b) |
| — | `revive`, `hpRegen`, `mpRegen`, `spRegen`, `escapeBonus` | revive flag + percentage regens + escape bonus |
| 0x0e | `icon_0e` | `ItemIcon` |
| 0x0f | `speed_0f` | flat speed |
| 0x10–0x17 | `attack_10`, `magicAttack_11`, `defence_12`, `magicDefence_13`, `attackHit_14`, `magicHit_15`, `attackAvoid_16`, `magicAvoid_17` | flat combat stats |
| 0x18 | `onHitStatusChance_18` | percentage |
| 0x1b | `onHitStatus_1b` | status type applied on hit |

`canBeDiscarded()` = `(flags_00 & 0x4) == 0`; `getSize()`/`getMaxSize()` = 1 (equipment never stacks). The battle-side consumption (the additive stat bake, the `specialEffectFlag_14 |= flags_00` merge, the element/resist accumulation) is 05b C22 — cross-referenced, not repeated.

### 2.7 `Good` and `GoodsInventory` (I7)

`Good` is a `RegistryEntry` + `InventoryEntry` with a `sortingIndex`; size is always 1, both prices are always 0. `GoodsInventory` is a `HashSet<Good>` (unique, unlimited):

- `give(good, source)`: posts `GiveGoodsEvent(this, good, source)` (cancellable; the `givenGoods` list rewriteable); sources: `GoodsSource { INITIALIZATION, GAMEPLAY, DEBUGGER, EXTERNAL }`.
- `take(good)`: posts `TakeGoodsEvent` (cancellable; the `takenGoods` list may remove *multiple* goods at once).
- `has(good)`, `clear()`.
- Retail round-trip: `unpack(index, packed)` / `pack(index)` operate on the retail 32-bit words (2 words × 32 bits = 64 slots, 37 used) — **mod goods are left in place on unpack and ignored on pack** (the retail save format is 37-slot only; M7-01 §4.2 `0x19C`).

The 37 `GOODS_IDS` (`LodMod.java:250–259`): indices 0–7 are the eight dragoon spirits (`red/blue/jade/gold/violet/silver/dark/divine_dragoon_spirit`), indices 8–32 are the story goods (`war_bulletin`, `fathers_stone`, `prison_key`, …, `law_output`), index 33 is `gold_dragoon_spirit_2` (a second gold-spirit slot), 34 = `magic_shiny_bag`, 35 = `vanishing_stone`, 36 = `lavitzs_picture`. The spirits' item behavior (transform eligibility, the `hasDragoon()` check, the divine-spirit Dart flip) is M9-06 C17/C39 — cross-referenced.

### 2.8 The retail ID mapping (I8)

The retail save format and the retail script API address entries by **integer ID**; the reference maps them to registry IDs through four string tables (`LodMod.java`):

- `itemId < 0xc0` (192) → **equipment**: `EQUIPMENT_IDS[itemId]` (192 entries; blank strings = unused retail slots).
- `itemId >= 0xc0` → **item**: `ITEM_IDS[itemId − 192]` (64 entries; retail item IDs 192–255; blank strings = unused retail slots — the 06 starting item "burn_out" is index 3, i.e. retail ID 195).
- goods: bit `b` of word `w` → `GOODS_IDS[w*32 + b]` (37 entries).
- shops: `SHOP_IDS[0–65]` (66 entries: 39 named — 38 real shops + `unknown_shop_01` — and 27 `empty_shop` placeholders).

Consumers: the retail save serializers (`RetailSerializer.java:100/120/137/162`), the plot-script functions `scriptGiveChestContents` @ 0x800244c4 and `scriptTakeItem` @ 0x80024590 (`Scus94491BpeSegment.java:434–488`), and the script-var inventory read `GameVarParam.java:262`. The mapping is the **L4 compatibility layer** — the retail binary addressed these tables directly.

## 3. Operations, drops, and events

### 3.1 The give/take primitives (I9)

The `SItem`-level primitives (all with retail addresses, §1):

- `takeItemFromSlot(slot)`/`takeItemFromSlot(slot, amount)` @ 0x800232dc — the single/quantity item removal (the menu discard, the shop sell, the `UseItemScreen` consume).
- `takeEquipmentId(equipment)` / `takeEquipment(index)` @ 0x800233d8 — `TakeEquipmentEvent` (cancellable), list removal.
- `giveEquipment(equipment)` @ 0x80023484 — `GiveEquipmentEvent(equipment, unmodifiable list, maxInventorySize=255)`; modes: `FAIL` (reject), `TRUNCATE` (add until full), default (add all — the list is effectively uncapped at 255 in the default mode).
- `giveItems(List<EnemyDrop>)` @ 0x80023544 — the drop pump: `performDrop()` per entry; on failure → `overflow()` (the entry's overflow callback) + count; returns the overflow count.
- `addGold(amount)` @ 0x8002363c — `gold_94 += amount`, clamped at 99,999,999, returns 0.
- `addHp`/`addMp`/`addSp` @ 0x80022b50/0x80022c08/0x80022cd0 — the vitals writers (M9-06 for the SP→DXP queueing side).

### 3.2 The drop and overflow pipeline (I10)

1. **Battle**: when the L4 combat script marks a monster dead, `scriptSetBentDeadAndDropLoot` (05b C12) reads the per-enemy drop from the `enemyRewards_80112868` table (`loadEnemyDropsAndScript` @ 0x80109808, `EnemyRewardsEvent`-mutable) and appends `EnemyDrop` records to `itemsDroppedByEnemies_800bc928` (`Battle.java:4333–4335`): equipment drops carry `performDrop = () -> giveEquipment(equipment)`, item drops `performDrop = () -> items_2e9.give(item).isEmpty()`; the overflow callbacks push into `equipmentOverflow`/`itemOverflow`.
2. **Battle end**: repeatable items used in battle are returned to the inventory *before* the report (`Battle.java:8386–8393`: `items_2e9.give(stack)`, remainder → `itemOverflow`), then `usedRepeatItems_800c6c3c` is cleared.
3. **Report**: `PostBattleScreen.WAIT_FOR_INPUT_14` (`:456–472`) calls `giveItems(itemsDroppedByEnemies_800bc928)`; if drops remain (`giveItems` count > 0) **or** `itemOverflow` is non-empty → `TRANSITION_TO_TOO_MANY_ITEMS` (resize 384×240, pop the report, push `TooManyItemsScreen`); else → `UNLOAD_18` (`WhichMenu.UNLOAD_POST_COMBAT_REPORT_30`).
4. **Field**: chest contents and script gives go through `scriptGiveChestContents` @ 0x800244c4 (gold shortcuts `0xfb–0xfe` = 20/50/100/200 G, `0xff` = nothing; equipment `itemId < 0xc0` via `giveEquipment`; items ≥ 0xc0 via `items_2e9.give`; the given entry is recorded in the static `lastChestContents`) and `scriptGiveItem`/`scriptGiveEquipment` (SItem) — **a field give that doesn't fit fails atomically and the item is lost** (no field overflow buffer; the overflow machinery is battle-only).
5. **Dabas**: the minigame reward (`DabasScreen.giveReward`, `:422–429`) dispatches on entry type — `ItemStack` → `items_2e9.give`, `Equipment` → `giveEquipment`, `Good` → `goods_19c.give(good, GAMEPLAY)` (M9-01 for the minigame itself).

### 3.3 The mod-event surface (I11)

All L4, registered in M8-02's census: `GiveItemEvent`/`TakeItemEvent` (per-container, mutable item lists, overflow mode), `GiveEquipmentEvent`/`TakeEquipmentEvent`, `GiveGoodsEvent`/`TakeGoodsEvent` (source-tagged), `RepeatItemReturnEvent` (the ∞-item consume gate — `returnItem` default = `stack.isRepeat()`), `GatherAttackItemsEvent`/`GatherRecoveryItemsEvent` (the random-ball pools), `GatherShopExtensionsEvent`/`ShopContentsEvent`/`ShopBuyEvent`/`ShopSellEvent`/`ShopSellPriceEvent` (the shop surface), `GatherEquipmentTypesEvent`/`GatherCharacterEquipmentTypesEvent` (the eligibility tag sets).

## 4. Items: the catalog and the effect families

### 4.1 The catalog (I12)

`LodItems` registers **54** items (54 `ITEM_REGISTRAR.register` calls). By family (price = retail base price; buy = 2×):

**Recovery — HP** (`RecoverHpItem`): healing_potion (5, 50%, single), healing_fog (15, 100%, single), healing_breeze (25, 50%, all), healing_rain (60, 100%, all), enemy_healing_potion (5, 30%, enemy — `targetAll=false`, `HealingPotionItem` subclass with the enemy-targeting override).
**Recovery — MP** (`RecoverMpItem`): sun_rhapsody (25, 100%, single), moon_serenade (100, 100%, all).
**Recovery — SP** (`RecoverSpItem`): spirit_potion (10, 100%, single).
**Recovery — status** (`RecoverStatusItem`): body_purifier (5, bit `0xb0`), mind_purifier (10, bit `0x4e`), depetrifier (15, bit `0x1`).
**Battle-only utility**: angels_prayer (15 — the resurrection; `isStatMod`/`calculateStatMod = 0`, `alwaysHits`, the effect is in the DEFF/script), charm_potion (2 — the encounter-accumulator reset, §4.9), magic_sig_stone (the `MAGIC_SIG_STONE` registration, `SignetStoneItem`), pandemonium (200 — the all-allies DEFF effect, repeat+protected, entrypoint 34 with the `0xffffff` colour word), smoke_ball (200 — the no-target escape, repeat+protected, entrypoint 41, `loadDeff` no-op), sachet (200 — the single-enemy DEFF, entrypoint 33), total_vanishing (10 — the single-enemy erase; `getSpecialEffect` returns `−1` when the target has the `0x80` resist bit, else `0`; entrypoint 42, `loadDeff` no-op). The NO_ELEMENT attack items psyche_bomb (`0x8`) / psyche_bomb_x (the X variant) are counted in the 24 attack items above (§4.3).
**Attack** (`AttackItem`, 24 — §4.3): the elemental attack items incl. detonate_rock (the chain-attack component, targetAll, NO_ELEMENT, multiplier 0).
**Random balls** (50): attack_ball (enemies), recovery_ball (allies) — §4.7.
**Buff** (`BuffItem`, 200): power_up (+50 def/mdef/atk/matk, allies), power_down (−50, enemies), speed_up (+100% speed, allies), speed_down (−50% speed, enemies) — repeat+protected.
**Shield** (`ShieldItem`, 200): magic_shield (magical immunity), material_shield (physical immunity) — repeat+protected, no DEFF.
**Cause-status** (`CauseStatusItem`, 10): panic_bell (bit `0x4`), stunning_hammer (bit `0x10`), poison_needle (bit `0x80`), midnight_terror (bit `0x8`) — the negative first constructor arg (−11/−13/−16/−12) is the item's *power* (damage), the fourth arg the status bit; entrypoint 44.

### 4.2 The battle item-use pipeline (I13)

The battle-side entry is the native `scriptUseItem` (Battle table **slot 1010**, `Battle.java:1123`/`:1239–1244`: item registry ID in, target bent index in) → `ItemStack.useInBattle(user, targetBentIndex)`. For `BattleItem` subclasses the effect is a **three-stage async pipeline** (`lodmod/items/BattleItem.java`):

1. **Stage 0**: `loadDeff(user, deffEntrypoint, deffParam)` — looks up the DEFF registry entry named by `getDeffId()` — the **default is the item's own registry ID** (`REGISTRIES.deff.getEntry(getDeffId())`; the single exception is `HealingFogItem`, which shares the `HEALING_POTION` DEFF) — calls `Battle.allocateDeffEffectManager(user, 0, user.index, param, entrypoint, new ScriptDeffManualLoadingEffect())` and `deff.get().load()`, and sets `deffManager_800c693c.flags_20 |= 0x40_0000` (the manual item-DEFF flag); in parallel `injectScript(user, "throw_item", entrypoint)` loads the `throw_item` patch script and builds a `ScriptStackFrame` at the item's entrypoint; yields `FlowControl.PAUSE_AND_REWIND`.
2. **Stage 1**: waits (`PAUSE_AND_REWIND`) until the async load completes.
3. **Stage 2**: restores the user's frame offset, calls the item's `useItemScriptLoaded(user, targetBentIndex)` (the parameter-word setup), `user.pushFrame(stackFrame)` — **the item script runs as a frame pushed onto the acting bent's script stack** (when it finishes, control returns to the bent's normal script), resets the stage, yields `CONTINUE`.

The parameter words (set by `useItemScriptLoaded`, consumed by the `throw_item` entrypoint): `stor[28]` = target bent index (or −1 for ALL), `stor[30]` = user bent index, `stor[8]` = the DEFF colour word (e.g. `0xd7fff5` healing green, `0x780078` MP yellow, `0xffffff` white).

The `throw_item.txt` entrypoint table (the L4 replacement script; `patches/scripts/throw_item.txt:14–45`) — the PCS entrypoints 0–31 are re-declared so that forked bent scripts keep their expected entrypoints; the item entrypoints are:

| entrypoint | name | consumers |
|---|---|---|
| 32 | `DO_NOTHING` | `BattleItem` default |
| 33 | `USE_ATTACK_ITEM` | all 24 attack items, sachet |
| 34 | `USE_HEALING_POTION` | the HP/MP/SP recovery items, angels_prayer, charm_potion, pandemonium |
| 35 | `USE_DEPETRIFIER` | depetrifier |
| 36 | `USE_BODY_PURIFIER` | body_purifier |
| 37 | `USE_MIND_PURIFIER` | mind_purifier |
| 38 | `USE_POWER_SPEED_UP` | the 4 buff items |
| 39 | `USE_MAGIC_SHIELD` | magic_shield |
| 40 | `USE_MATERIAL_SHIELD` | material_shield |
| 41 | `USE_SMOKE_BALL` | smoke_ball |
| 42 | `USE_TOTAL_VANISHING` | total_vanishing |
| 43 | *(re-declared `ENTRYPOINT_11_RENDER_TICK_STATUS` — no item uses it)* | — |
| 44 | `USE_CAUSE_STATUS` | the 4 cause-status items |

Items that are MENU-only (the purifiers, the charm potion, the recovery items' menu use) still carry entrypoints — the recovery items' *battle* use goes through the same `scriptUseItem` path; the DEFF-less items (shields, smoke ball, total vanishing) override `loadDeff` to a no-op so only the script frame runs.

### 4.3 Attack items (I14)

`AttackItem(icon, price, targetAll, element, damageMultiplier)`: BATTLE-only; `canTarget(ENEMIES)` and `canTarget(ALL)` iff `targetAll`; `getAttackElement()` = the element (the 05b elemental pipeline consumes it); `getAttackDamageMultiplier()` = the multiplier (`0` = normal, `0x8` = 8, `0x10` = 16, `0x40` = 64 — the DEFF-side effect magnitude); `getSpecialEffect()` = −1 (none); entrypoint 33. The retail assignments (L4 data): the ×1 items (normal) are thunderbolt, meteor_fall, gushing_magma, fatal_blizzard, rave_twister, dancing_ray, detonate_rock, black_rain? — no: black_rain is ×1 (0); the ×8 (`0x8`) items: psyche_bomb, flash_hall; the ×16 (`0x10`): burning_wave, down_burst, frozen_jet, gravity_grabber, night_raid, spectral_flash; the ×64 (`0x40`): burn_out, dark_mist, pellet, spark_net, spear_frost, spinning_gale, trans_light. (Transcription from `LodItems.java:53–76`; the multiplier's *application* is the DEFF — retail binary, open item 1.)

### 4.4 Recovery items (I15)

`RecoverHpItem(icon, price, targetAll, percentage)` (the MP/SP variants are symmetric): usable in **both** MENU and BATTLE; `canBeUsedNow(MENU)` = some party member's HP is not full (the dimmed-row gate, §7.3); menu use (`useInMenu`, the shared retail address @ 0x80022d88) computes `amount = (percentage == 100) ? −1 (full) : maxHP × percentage / 100` (integer) and calls `addHp(charId, amount)`. Battle use: `isStatMod() = true`, `alwaysHits() = true`, and `calculateStatMod(user, target)` sets `user.status_0e |= 0x800` (the "recovery item in progress" marker — the DEFF consumes it; the bit's semantics are the retail DEFF's, open item 2) and returns `target.maxHP × percentage / 100` — the item's DEFF script (entrypoint 34) reads this as the healing amount. The `user` is the actor, the `target` the recipient — the heal is **percentage of the target's max**, not the user's.

### 4.5 Purifiers (I16)

`RecoverStatusItem(price, statusBit)` (all `GREEN_POTION`, price 5/10/15): usable in both locations; `canBeUsedNow(MENU)` = some party member has *any* of the item's status bits; menu use requires the target to have one of the bits (else `failure()`) and then **clears the target's entire `status_10`** (`character.status_10 &= ~status` with `status = character.status_10` — i.e. `status_10 = 0`; a purifier cures *all* of the target's ailments, not just the matched one). Battle: entrypoints 35/36/37, the DEFF handles the heal display; the retail status-bit meaning (05b's bit table) is cross-referenced.

### 4.6 Buffs and shields (I17)

`BuffItem` (20 constructor args): BATTLE-only, `isRepeat()` + `isProtected()` (the ∞ power/speed shields — never consumed in battle, never stolen). `applyBuffs(user, target)` (called by the item DEFF's script) writes the bent's power/temp words with `turnCount = (user != target) ? 3 : 4` (3 turns on other-party targets, 4 on self): `powerAttack_b4`/`powerAttackTurns_b5`, `powerMagicAttack_b6`/`_b7`, `powerDefence_b8`/`_b9`, `powerMagicDefence_ba`/`_bb`, `tempAttackHit_bc`/`_bd`, `tempMagicHit_be`/`_bf`, `tempAttackAvoid_c0`/`_c1`, `tempMagicAvoid_c2`/`_c3`, `tempPhysicalImmunity_c4`/`_c5`, `tempMagicalImmunity_c6`/`_c7` (the word model is 05b's); speed is a **percentage `UNARY` stat mod** (`percent(speedUp|speedDown)`, `turns(turnCount)`, registered under the `speed_up`/`speed_down` IDs); the per-hit SP/MP words (`tempSpPerPhysicalHit_cc`…`tempMpPerMagicalHit_d3`, `PlayerBattleEntity` only) are the 05b `0xcc–0xd3` economy's item source. The power values can be negative (power_down writes −50 into the same words — debuff via the same channel). `ShieldItem` is the `BuffItem` with only the immunity booleans set and a **no-DEFF** `loadDeff` (script-frame only): magic_shield (entrypoint 39, magical immunity), material_shield (entrypoint 40, physical immunity).

### 4.7 The random balls (I18)

`AttackBallItem` (enemies) / `RecoveryBallItem` (allies) (both price 50, stackable, BATTLE-only) are **randomizers**, not effects: `useInBattle` posts `GatherAttackItemsEvent`/`GatherRecoveryItemsEvent` (the L4 pools — 25 attack items: spark_net, burn_out, pellet, spear_frost, spinning_gale, trans_light, dark_mist, panic_bell, stunning_hammer, poison_needle, midnight_terror, thunderbolt, meteor_fall, gushing_magma, dancing_ray, fatal_blizzard, black_rain, rave_twister, burning_wave, frozen_jet, down_burst, gravity_grabber, spectral_flash, night_raid, flash_hall; 7 recovery items: spirit_potion, sun_rhapsody, healing_potion, healing_fog, moon_serenade, healing_rain, healing_breeze), picks `seed_800fa754.nextInt(pool.length)` (**the battle Mersenne Twister** — §11), sets `user.item_d4 = selected` and `user.registryIds[0]` to the selected item's ID, and delegates to the selected item's own `useInBattle` (the selected item's DEFF + entrypoint run). The script-facing twins are `scriptGenerateAttackItem`/`scriptGenerateRecoveryItem` (SItem `:376–393`) — same event + MT pick, returning the chosen registry ID.

### 4.8 Repeatable and protected items (I19)

`isRepeat()` items (the 4 buffs, the 2 shields, pandemonium, smoke_ball) are shown with the **∞ glyph** in the battle `ItemListMenu` (`renderText(I18n.translate("lod_core.ui.infinity"), x + 130, y)` — the ∞ font glyph added in the Phase-A update, §12.1) and the consumption is gated by `RepeatItemReturnEvent(stack, stack.isRepeat())`:

- **Battle** (`ItemListMenu.onUse`): the stack is taken 1 from `player.item_d4`; if the event returns `returnItem` (default true for repeats), the unit goes to `Battle.usedRepeatItems_800c6c3c` instead of being consumed; at battle end the units are given back (`Battle.java:8386–8393`, overflow → `itemOverflow`).
- **Menu** (`UseItemScreen.menuStage3Select`): the same event; if `!returnItem` the item is consumed via `takeItemFromSlot(slot, 1)`.

`isProtected()` (buffs, shields, pandemonium, smoke_ball, signet-stone-class items) marks items enemies cannot steal; the discard UI separately gates on the entry flags (§7.4).

### 4.9 The utility items (I20)

- **Charm potion** (2): MENU+BATTLE; `canBeUsedNow` = the current engine state's encounter accumulator is non-zero (`WMap.encounterAccumulator_800c6ae8` or `SMap.encounterAccumulator_800c6ae8` when the submap has encounters); menu use **resets the accumulator to 0** (the cross-system encounter effect — M9-03/M9-04 for the accumulator model) and shows the `use` text; battle use is the entrypoint-34 white-flash no-op.
- **Angels' prayer** (15): BATTLE-only, allies, `isStatMod` with `calculateStatMod = 0`, `alwaysHits` — the resurrection effect is entirely in the DEFF/script (the 05b status/revive model).
- **Pandemonium** (200): BATTLE-only, **all allies** (`canTarget(ALL)`), repeat+protected, entrypoint 34 with `stor[8] = 0xffffff` and the target/user words — the all-party DEFF effect.
- **Smoke ball** (200): BATTLE-only, no target (`canTarget` always false), repeat+protected, entrypoint 41, no DEFF — the battle-escape script.
- **Sachet** (200): BATTLE-only, single enemy, NO_ELEMENT, entrypoint 33.
- **Total vanishing** (10): BATTLE-only, single enemy, entrypoint 42, no DEFF; `getSpecialEffect` returns −1 if `target.specialEffectFlag_14 & 0x80` (resist), else 0 — the resist check is on the *equipment/attack-flag* word (05b).
- **Cause-status items** (10 each): BATTLE-only, single enemy, entrypoint 44; `getSpecialEffect` = the status bit (panic `0x4`, stun `0x10`, poison `0x80`, terror/fear `0x8`); the first constructor arg is the item's damage power (−11/−13/−16/−12).
- **Detonate rock** (5): the chain-attack *component* (BATTLE-only, targetAll, NO_ELEMENT, multiplier 0) — normally produced by the Detonate Arrow equipment (§5.5), not the shop.
- **Enemy healing potion** (5): the `HealingPotionItem` 30% variant targeting the enemy side.

## 5. Equipment: eligibility and effects

### 5.1 The slots and the type tags (I21)

`EquipmentSlot` = WEAPON, HELMET, ARMOUR, BOOTS, ACCESSORY (the retail 5-slot model; `CharacterData2c.equipment_14` is the per-character `EnumMap`). Eligibility is a two-stage check in `CharacterData2c.canEquip(slot, equipment)` (`:122–139`):

1. **`CanEquip`**: `template.canEquip(character, slot, equipment)` (the character side) and `equipment.canEquip(character, slot)` (the equipment side) each return `DENY`/`FORCE`/`NORMAL`; either `DENY` → false, either `FORCE` → true.
2. **Type-tag intersection**: `EquipmentTypes.getEquipmentTypesForCharacter(template)` ∩ `getEquipmentTypesForEquipment(equipment)` must be non-disjoint.

`EquipmentTypes` (`inventory/EquipmentTypes.java`) is a string-tag system built at boot from two L4 gather events (`loadEquipmentTypes`): 23 constants — weapon classes `LONGSWORD`, `POLEARM`, `BOW`, `SHORTSWORD`, `HAND`, `HAMMER`, `AXE`; character classes `DART`, `LAVITZ`, `SHANA`, `ROSE`, `HASCHEL`, `ALBERT`, `MERU`, `KONGOL`, `MIRANDA`; generic `NEUTRAL`, `MALE`, `FEMALE`, `ADDITIONS`, `HEAVY`, `MEDIUM`, `LIGHT`; the special `ARMOR_OF_YORE`. The per-equipment assignments (`LodMod.gatherEquipmentTypes`, `:527+`) give each of the 149 equipment entries its tag set (e.g. `bastard_sword` → LONGSWORD, `blue_dg_armor` → MERU, `armor_of_yore` → ARMOR_OF_YORE, most accessories → NEUTRAL or MALE/FEMALE/MEDIUM/LIGHT).

The 9 character tag sets (`gatherCharacterEquipmentTypes`, `LodMod.java:679–688`):

| character | weapon class | tags |
|---|---|---|
| Dart | LONGSWORD | DART, NEUTRAL, MALE, ADDITIONS, HEAVY, ARMOR_OF_YORE |
| Lavitz | POLEARM | LAVITZ, NEUTRAL, MALE, ADDITIONS, HEAVY, ARMOR_OF_YORE |
| Shana | BOW | SHANA, NEUTRAL, FEMALE, MEDIUM, LIGHT |
| Rose | SHORTSWORD | ROSE, NEUTRAL, FEMALE, ADDITIONS, MEDIUM |
| Haschel | HAND | HASCHEL, NEUTRAL, MALE, ADDITIONS |
| Albert | POLEARM | ALBERT, NEUTRAL, MALE, ADDITIONS, HEAVY, ARMOR_OF_YORE |
| Meru | HAMMER | MERU, NEUTRAL, FEMALE, ADDITIONS, LIGHT |
| Kongol | AXE | KONGOL, NEUTRAL, MALE, ADDITIONS, ARMOR_OF_YORE (no HEAVY) |
| Miranda | BOW | MIRANDA, NEUTRAL, FEMALE, MEDIUM, LIGHT |

### 5.2 The on-equip stat mods (I22)

`Equipment.onEquip(character)` / `onUnequip(character)` (called by `CharacterData2c.equip` — the single write path for equip/unequip/shop-purchase/new-game/load): for non-zero `hpMultiplier`/`mpMultiplier` a **permanent `FRACTIONAL` stat mod** (percentage, keyed by the equipment's registry ID) is added/removed on the character's HP/MP vitals (the mod applies to the *max* — M9-06 C20); for non-zero `speed_0f` a **permanent `UNARY` flat mod** (`contributesToOtherMods(true)`) on SPEED. This is the *field-side* effect (the character's displayed max HP/MP/speed); the *battle-side* effect is the separate additive bake at battle start (05b C22) — the two are independent paths over the same data.

### 5.3 The battle bridge (cross-reference)

The per-equipment battle aggregation (flat stat additions into the bent raw words, `specialEffectFlag_14 |= flags_00`, the attack-element/resist/immunity/status-resist accumulation, `equipmentAttack1_28 += attack_10`) is 05b C22; the `additionExtra_474` special-effect flag consumers (the Wargod/Destroyer bits) are 05c — both cross-referenced.

### 5.4 The four special equipment classes (I23)

All four are `Equipment` subclasses overriding the battle hooks (the corrected Wargod behavior, per the checkpoint constraint):

- **Wargod Calling** (500, ACCESSORY): `applyEffect(wearer)` → `battleState.additionExtra_474[wearer.allBentSlot_274].flag_00 |= 0x2` (the SP-halving flag — 05c).
- **Ultimate Wargod** (5000, ACCESSORY): `applyEffect` → `flag_00 |= 0x6` (0x2 SP-halving **plus** 0x4 mastery suppression — 05c).
- **Destroyer Mace** (250, WEAPON, ATK 55): `applyEffect` → `flag_00 |= 0x1` (the destroyer flag — 05c).
- **Detonate Arrow** (200, WEAPON bow, ATK 50, `statusResist_08 = 0x8`): `prepareAttack(player)` → resets `battleState._560`/`_564` to 0, sets `player.item_d4 = new ItemStack(DETONATE_ROCK)`, and loads DEFF **0xc1** via `Battle.loadSpellItemDeff(player, 0xc1, 0, player.index, −1, 1, new ScriptDeffManualLoadingEffect())`; `attack()` returns `EquipmentAttackType.DEFF` — the weapon replaces its normal attack with the Detonate Rock item DEFF (the chain attack).

`applyEffect` is called by the battle equipment-effect path (`scriptApplyEquipmentEffect`, table slot 1011, `Battle.java:1124`/`:1249–1252`).

### 5.5 The equipment catalog (I24)

`LodEquipment` registers **149** equipment entries with the full §2.6 layout (L4 data; retail-byte-pending). Notable flag assignments (from the registrations): `dragon_buster` (0x4 can't-discard), `brass_knuckle`/`gladius`/`indoras_axe` (0x40 instakill), `roses_hair_band`/`talisman`/`poison_guard` (0x80 resist-instakill), `wargod_calling`/`ultimate_wargod`/`destroyer_mace`/`detonate_arrow` (the special subclasses). The five dragoon-armor entries (red/jade/gold/violet/silver/dark/blue `*_dg_armor`, price 400) each carry one element in `elementalImmunity_07`; the six element stones (`red_eye/jade/silver/darkness/blue_sea/violet/golden_stone`, price 250) each carry one element in `attackElement_04`; `armor_of_legend` (5000) and `phantom_shield` (5000) are the dual-immunity accessories; `legend_casque` (5000) is the DEF-50/SPD-127 helmet. The `LEGEND_CASQUE`/`ARMOR_OF_LEGEND`/`PHANTOM_SHIELD`/`DRAGON_SHIELD` (2500)/`ANGEL_SCARF` (2500) top-price tier is the late-game set.

## 6. UI state machines

### 6.1 The menu-stack model and `WhichMenu` (I25)

`MenuStack` (`SItem.menuStack`, a `Deque<MenuScreen>`): `pushScreen`/`popScreen` (pop calls `screen.delete()`); input handlers register on the first push and unregister on the last pop; **render propagates bottom→top**, **input propagates top→bottom** and stops at `HANDLED`; wheel input is accumulated to one step per render frame. `MenuScreen` (abstract, extends `ControlHost`) provides the input overrides (`mouseMove/Press/Release/Click`, `keyPress/Release`, `buttonPress/Release`, `axis`, `inputActionPressed/Released`, `inputClassChanged`), the hotkey system (`addHotkey(label, inputAction, handler)` — the L4 prompt-row), and the numeric/glyph render helpers. The unload contract: each screen's terminal state sets `Menus.whichMenu_800bdc38` to a `WhichMenu` value and calls the `unload` runnable — `WhichMenu { NONE_0, RENDER_NEW_MENU, RENDER_OLD_MENU, UNLOAD, UNLOAD_QUIETLY, RENDER_SAVE_GAME_MENU_19, UNLOAD_SAVE_GAME_MENU_20, RENDER_CHAR_SWAP_MENU_24, UNLOAD_POST_COMBAT_REPORT_30, QUIT }` (M7-01/M9-02 for the menu lifecycle). Menu assets: `SItem.loadMenuAssets()` loads DRGN files 6665/6666 (the menu textures) and directory 5815 (the menu music).

The message-box family (all L4): `MessageBoxScreen` (the retail-style confirm/alert with a result callback), `CycleMessageBoxScreen` (generic cycling value box: min/max, arrow navigation, wrap-around), `ChoiceMessageBoxScreen` (a cycling yes/no box), `QuantityMessageBoxScreen` (a cycling quantity box, 1..max — the Phase-A addition, §12.1). All push onto `menuStack` and resolve through a `MessageBoxResult` callback.

### 6.2 `UseItemScreen` — field item use (I26)

Stages: `0` (fade in, init) → `1` (allocate renderables, build the item list) → `2` (item list, 5 visible rows at y = 18 + 17i, left pane x=16/33; up/down/page/top/bottom/home/end navigation with input-edge-only wrapping; mouse + wheel) → `3` (character select, up to 7 portrait slots at x = 21 + 50i, HP/MP readouts per portrait, left/right wrap) → `100` (fade out, `unload`). The item list is built from `items_2e9` filtered by `stack.canBeUsed(MENU)`, sorted by `menuItemIconComparator(ITEM_IDS order)` (the RETAIL vs GROUP sort mode, §7.4 of 06's config census), with `flags_02 |= 0x4000` on entries whose `canBeUsedNow(MENU)` is false (dimmed; selecting them plays the deny sound). On select: `itemTargetAll = stack.canTarget(ALL)` (ALL-target items highlight **all** character portraits and apply to every party member in a loop, success = any success); single-target items pick one character. On confirm: `useInMenu(response, charId)`; on success the `RepeatItemReturnEvent` gate (§4.8) decides consumption; a non-null `response.text` pushes a `MessageBoxScreen` (the item's use text). The portrait/slot geometry carries the retail addresses @ 0x800fc8c0/0x800fc8dc.

### 6.3 `ItemListScreen` — the item list (I27)

Two panes (items left at x=8, equipment right at x=188; 7 rows; description pane below): the items pane caps at `INVENTORY_SIZE_CONFIG`, the equipment pane at 255; left/right (or mouse) switches focus, preserving the row index. The content is `loadItemsAndEquipmentForDisplay(equipment, items, 0)` (`SItem.java:1089–1140`): all item stacks; the loose equipment list **plus the currently-equipped items inlined** (per party character, per slot) flagged `0x3000 | charId` (the 0x1000 "skip on write-back" bit + the 0x2000 "can't discard" bit + the owner charId in the low byte). `setInventoryFromDisplay` (the write-back, @ 0x800239e0) skips entries with the 0x1000 bit — equipped items stay on their character, loose items are force-given back. L4 additions: the **sort** hotkey (items via `menuItemIconComparator`, equipment via `menuEquipmentSlotComparator` — slot ordinal then name — then write-back) and the **discard** hotkey (items → `QuantityMessageBoxScreen` 1..stack-size then shrink/remove; equipment → single confirm; the 0x2000-flagged entries are refused with the deny sound).

### 6.4 `EquipmentScreen` — equipping (I28)

Stages: `0` (fade) → `1` (set the character card, silent re-sort) → `2` (build the list, 4 visible rows at y = 99 + 17i) → `3` (navigation; character comparison stats via `characterCard.setEquipmentComparison` — the selected-vs-equipped stat delta; the 5-slot grid via `renderCharacterEquipment`) → `100` (fade, unload). The list is `getEquippableItemsForCharacter`: loose equipment where `character.canEquip(equipment.slot, equipment)` **and** the item is not already the character's in that slot, further filtered by the slot filter (§below); left/right moves between characters (rebuild at stage 1, wrap only on a fresh input edge). On confirm (`menuSelect`): `equipItem(equipment, charId)` (the §5.2 path, @ 0x80103a5c) → `takeEquipment(itemSlot)` (remove from the loose list) → `giveEquipment(previousEquipment)` (return the displaced item) → **`addHp(charId, 0)` + `addMp(charId, 0)`** (re-clamp current HP/MP to the new maxes after the stat change) → re-sort if a sort mode is active → rebuild at stage 2. L4 hotkeys: **unequip** (every slot → `character.equip(slot, null)`, displaced items to the loose list), **sort** (cycles 0/1/2 → `sortEquipmentInventory`: 0 = alpha (slot ordinal → name), 1 = power (slot ordinal → weapon: `attack_10 + magicAttack_11 + attackHit_14 + magicHit_15 + speed_0f` descending, else `defence_12 + magicDefence_13 + attackAvoid_16 + magicAvoid_17 + speed_0f` descending → name), 2 = icon (icon index → name)), **filter** (cycles null → WEAPON → HELMET → ARMOUR → BOOTS → ACCESSORY → null).

### 6.5 `TooManyItemsScreen` — the overflow screen (I29)

States: `LOAD_ITEMS_1` (consume the static `itemOverflow`/`equipmentOverflow` — item stacks are merged into a temp event-disabled `Inventory` first; equipment appended) → `INIT_2` → `INIT_3` (fade in) → `RENDER_4` (the "too many items" confirm: YES = keep/swap, NO = discard) → `RENDER_6`/`REPLACE_5` → `DROPPED_8` (select an overflowed entry, 5 rows) → `INVENTORY_9` (select the inventory slot to swap with, 7 rows; the right pane shows the item list or the equipment list depending on the selected entry's kind; the 0x6000-flagged (protected/non-discardable) slots are refused) → back to `DROPPED_8` (the loop; each swap exchanges the two entries and writes both lists back via `setInventoryFromDisplay`) → `DISCARD_10` (the second confirm; if **any** overflowed entry is a non-discardable equipment the discard is refused with "cannot be thrown away" and returns to `RENDER_6`) → `UNLOAD_12` (fade, pop, `WhichMenu.UNLOAD`). The sort hotkey re-sorts the *inventory* pane (with the correct ID table — items use `EQUIPMENT_IDS`-order and equipment `ITEM_IDS`-order as passed at the call site, `:487–494`). Only `PostBattleScreen.TRANSITION_TO_TOO_MANY_ITEMS` pushes this screen (§3.2) — the field has no overflow UI.

### 6.6 `GoodsScreen` (I30)

Two `ItemList<Good>` panes (left x=8, right x=188; the goods sorted by `sortingIndex` and dealt alternately left/right), left/right (or mouse) switches focus, the description pane tracks the hovered entry; the goods icons come from the `GoodsIcon` "Goods Icons" atlas built on first render; BACK = unload. Purely a viewer — give/take of goods is script/Dabas/shop only.

### 6.7 The battle `ItemListMenu` (I31)

The in-battle item submenu (a `ListMenu` in the battle HUD's action-menu chain — 05c for the menu framework): `cacheItems()` @ 0x800f83c8 walks `items_2e9`, keeps `canBeUsed(BATTLE)` stacks, and **merges same-item stacks into `CombatItem02` entries** (one row per item kind, summed size); rows render `name × size` with the ∞ glyph for repeats (§4.8); `onSelection` sets `player.setActiveItem(stack)`; `handleTargeting`: `INSTANT` → 2 (no target), else `hud.handleTargeting(ALLIES ? 0 : 1, canTarget(ALL))`; `getTargetingInfo` publishes `params[0]` = selection state, `params[1]` = target bent index (**−1 for ALL-target items**), `params[2]` = the item's registry ID (the battle script's `scriptUseItem` call arguments); `onUse` consumes 1 (or queues the repeat, §4.8).

## 7. The shop

### 7.1 Data model and registrations (I32)

`Shop` (`types/Shop.java`): `shopType_00` (0 = equipment, 1 = item, 3 = the Helena-script shops' type — the type is a *hint*; the actual per-entry behavior comes from the extension selection, §7.2) + an array of `Supplier<InventoryEntry<?>>` (`getItem(i)` returns a fresh `copy()`). `ShopScreen.ShopEntry<T>`: `item` + mutable `price` (initialized to `entry.getBuyPrice()` — i.e. price×2 — and rewriteable by `ShopContentsEvent`). Registry `lod_core:shops` (`ShopRegistry`, the mod-mutable `MutableRegistry` + `ShopRegistryEvent`). The retail catalog (`LodShops`): **41 registered shops** — the 39 named retail-ID shops: BALE (equipment), SERDIO (item), LOHAN (equipment incl. wargod_calling/ultimate_wargod/phantom_shield/dragon_shield — the endgame set — + item), KAZAS (equipment + fort item + fort equipment), FLETZ (equipment + item + accessory), DONAU (equipment + item), QUEEN_FURY (equipment + item), FUENO (equipment + item; the burn_out sale), FURNI (equipment + item), DENINGRAD (equipment + item), WINGLY_FOREST (equipment + item), VELLWEB (equipment + item), ULARA (equipment + item; the attack_ball/recovery_ball sale), ROUGE (equipment + item), MOON (equipment + item; the 7 dragoon armors + destroyer_mace), HELLENA_01/02 (type 3 item shops), KASHUA (equipment + item), FOREST (item), KAZAS_FORT (equipment), VOLCANO (item), ZENEBATOS (equipment + item), UNKNOWN_SHOP_01 (type 0 with *item* entries — a retail data quirk) — plus `black_castle_item_shop` (**registered but with no `SHOP_IDS` entry**: unreachable via the retail-ID path; no other references found in the repo) and the `empty_shop` placeholder. The retail ID space is `SHOP_IDS` (66 entries: 39 named + 27 `empty_shop`, `LodMod.java:281–295`); the current shop is `SItem.shopId_8007a3b4`, set by plot scripts via game-var case 125 (`GameVarParam.java:348`) and read by the submap (`SMap.java:4050/5272` → `new ShopScreen(REGISTRIES.shop.getEntry(shopId_8007a3b4).get())`).

### 7.2 The screen state machine (I33)

`ShopScreen` states: `INIT_0` (`cacheCharacterSlots`, post `GatherShopExtensionsEvent` — the extensions map (extension → priority int) is filled by the core mod: `ItemShopExtension`, `EquipmentShopExtension`, `GoodShopExtension`, plus mod extensions; each `attach`ed) → `LOAD_ITEMS_1` (fade; build the `ShopEntry` list from `shop.getItem(i)` with `getBuyPrice()`; post `ShopContentsEvent` — the contents are rewriteable) → `INIT_2` (allocate) → `RENDER_3` (the main menu: **Buy / Sell / Carried / Leave**) → `BUY_4` (the shop list, 6 rows; the active extension is chosen **per entry** — the first extension (by priority) whose `accepts(entry)` matches the entry's kind: `ItemStack` → item extension, `Equipment` → equipment extension, `Good` → good extension) → `EXTENSION_5` (the extension's sub-flow — the equipment character select) → `SELL_10` (the player's inventory, 6 rows) → `START_FADE_16`/`FINISH_FADE_17` → `UNLOAD_19` (`WhichMenu.UNLOAD`). The header shows the gold and the carried-count fraction (items vs `INVENTORY_SIZE_CONFIG`, equipment vs 255); "Carried" pushes `ItemListScreen` (pop-back on unload); "Leave" fades out.

### 7.3 Buying (I34)

- **Items** (`ItemShopExtension.selectEntry`): `gold < price` → deferred "not enough gold" alert; else `maxQuantity = min(INVENTORY_SIZE_CONFIG − items.size, gold / price)`; `maxQuantity ≤ 0` → "inventory full" alert; else `QuantityMessageBoxScreen` (1..maxQuantity) → on YES: `ShopBuyEvent` + a loop giving **one unit at a time** (`items_2e9.give(entry.item)`; per unit `gold_94 -= price`; on the first fit failure → "inventory full" alert + stop). (The Phase-A quantity box — §12.1; the per-unit gold debit is the retail-correct total.)
- **Equipment** (`EquipmentShopExtension.selectEntry`): `equipment_1e8.size() >= 255` → "inventory full"; `gold < price` → "not enough gold"; else the **character-portrait sub-flow** (the portraits of the party, visible only for characters with `canEquip(slot, item)`, left/right + mouse selection, the stat-preview comparing current vs would-be equipment totals): on confirm → a second "equip?" confirm → `equipItem(item, charId)`; on failure → the item is unequipped + "failed to equip" alert; on success with the displaced item **not** fitting back into the loose list (`giveEquipment` false) → the displaced item is re-equipped + "inventory full" alert (no gold spent); otherwise `gold_94 -= price`. If no character can equip (or the equip confirm is declined without a character), the item is bought **unequipped** (`giveUnequipped` → `giveEquipment`).
- **Goods** (`GoodShopExtension.selectEntry`): already-owned → "already have" alert; `gold < price` → "not enough gold"; else single confirm → `ShopBuyEvent`, `goods_19c.give(good, GAMEPLAY)`, `gold_94 -= price`.

### 7.4 Selling (I35)

`Sell` first asks "what do you want to sell?" (Equipment = YES / Items = NO); either empty → "you have nothing to sell" alert. In `SELL_10`, non-discardable equipment is refused on select (deny sound). On confirm: **items** → `QuantityMessageBoxScreen` (1..stack-size) → on YES `takeItemFromSlot(slot, quantity)` (the full quantity is removed), then `ShopSellEvent` + `ShopSellPriceEvent(shop, inv, inv.getSellPrice())` + **`addGold(price)` exactly once** — the reference pays the single-unit sell price regardless of the quantity sold (source-verified at `ShopScreen.java:732–787`; retail's sell was one-item-at-a-time, so this is an L4 quirk of the quantity extension, §12.2); **equipment** → the quantity box is trivially 1 (equipment size 1), `takeEquipment(slot)`, the same single `addGold`. When the inventory becomes empty the "nothing to sell" alert shows and the list exits.

### 7.5 The post-battle MERCHANT (I36)

The MERCHANT post-battle action (value 5, 05c) is `MerchantPostBattleAction` (total duration 15 frames, fade 30; `performAction` sets `whichMenu_800bdc38 = WhichMenu.NONE_0`). The *shop itself* opens in the submap: the MERCHANT battle script writes the shop ID (game var 125 → `SItem.shopId_8007a3b4`), and the submap's menu transition (`SMap.java:4050`) / direct menu entry (`:5272`) pushes the `ShopScreen` for that shop — the 03-submap menu model. The shop is thus a *submap* screen (not a battle screen): the battle ends, the submap loads, and the shop is the destination the script pointed at.

## 8. Persistence (cross-reference)

The save-side is M7-01: the retail struct fields `0x19C` (goods, 2×i32 bitset over `GOODS_IDS`), `0x1E8` (equipment, `u8` retail IDs, 0xFF-terminated, ≤255), `0x2E9` (items, `u8` retail IDs 192–255, 0xFF-terminated, ≤64 — **no stacking in the retail format**: each saved item becomes a size-1 stack), and the per-character `0x14+i` equipment bytes (M7-01 §4.3). Load paths: `RetailSavedGame.createGameState`/the `GoodsInventory.unpack`/the `give(stack, force=true)` calls (`RetailSavedGame.java:130`, `SeveredSavedGame.java:110` — the force-give bypasses the size cap because the save is trusted); the L4 `SeveredSavedGame` V1/V2 tag formats carry the same entries as registry IDs (M7-01). The `INVENTORY_SIZE_CONFIG` change re-caps the live `items_2e9` via `onChange` (`InventorySizeConfigEntry`) without touching the save format. The equipment `canBeDiscarded`/flags and the item repeat/protected behavior are runtime-only (no save backing).

## 9. RQ7 — the script/native/data boundary

| behavior | native (L3) | script | data (registry/ROM) |
|---|---|---|---|
| Container model (3 inventories + gold, the caps) | ✓ (the fields, `addGold` clamp, the 255/32 caps) | — | `INVENTORY_SIZE_CONFIG` (L4), `ITEM_STACK_SIZE` (L4) |
| Give/take/merge/overflow | ✓ (`Inventory`, `SItem` primitives, the overflow statics) | the field give/take script functions (the landing pads for plot scripts) | — |
| Drop pipeline | ✓ (`scriptSetBentDeadAndDropLoot` accumulation — 05b, `giveItems`, the `EnemyDrop` overflow callbacks, the battle-end repeat return) | the L4 death handler triggers the accumulation; the MERCHANT action script | the `enemyRewards_80112868` table (`EnemyRewardsEvent`-mutable) |
| Item effects (the rule: how much, whom, what) | the menu-side arithmetic (the percentage heals, the purifier clear, the buff turn words, the charm reset) | the battle-side presentation (the `throw_item` entrypoints 32–44 — L4 replacement; the item DEFFs — retail binaries) | the per-item parameters (element, multiplier, percentage, status bit, the pool membership) |
| Random balls | the pick (`seed_800fa754.nextInt`) + the script twin | `scriptGenerateAttackItem`/`scriptGenerateRecoveryItem` are the script-facing picks | the 25/7-item pools (L4 gather events) |
| Repeat/protected items | the `usedRepeatItems` return, the `RepeatItemReturnEvent` gate | the battle script's `scriptUseItem` call | the per-item flags |
| Equipment eligibility | ✓ (`canEquip`, the type intersection, `onEquip` stat mods) | — | the tag sets (L4 data) |
| Equipping (the swap, the clamp) | ✓ (`equipItem`, `takeEquipment`, `giveEquipment`, the HP/MP re-clamp) | — | — |
| UI (all screens) | the retail screen behavior/addresses (where preserved) | — | the sort/filter/discard/quantity UI (L4) |
| Shop | the transaction rules (gold checks, the per-unit debit, the equip-swap) | the shop-ID write (var 125, the MERCHANT script); `ShopContentsEvent`/buy/sell events (mod) | the shop catalogs (L4 data) |
| Retail IDs | the mapping tables | the retail-ID script functions (`scriptGiveChestContents`, `scriptTakeItem` @ 0x80024590) | `ITEM_IDS`/`EQUIPMENT_IDS`/`GOODS_IDS`/`SHOP_IDS` |

**Conclusion (RQ7, this surface)**: inventory and items are **native + data-driven with a script-thin edge** — like 06's progression system, the opposite of battle's script-courtroom. The engine owns every rule (stacking, overflow, price, eligibility, the heal arithmetic, the shop transactions); the data owns every value (the 54/149/37/41 catalogs, the tag sets, the pools); the script is a thin *caller* (give/take/chest/shop-ID/MERCHANT) and a *presenter* (the item DEFFs and the `throw_item` replacement scripts carry the battle-side effect presentation). The one genuinely script-owned value on this surface is the **MT seed** (`scriptSetMtSeed`) — which the retail battle script sets per battle and which the item randomizers and `scriptRand` consume.

## 10. RNG and timing determinism

Three distinct random generators exist (do not conflate — the checkpoint constraint):

1. **`simpleRand()`** @ 0x800133ac (`Scus94491BpeSegment.java:224–235`): global 16-bit LCG — `seed = seed * 9 + 0x3711`, output `seed / 2 & 0xffff`; re-seeds from the vsync counter (`× 9`) when the low 12 bits of the seed are 0. Used by the battle layer for effect randomness (05a/05b/05c) and by the drop roll (05b C12).
2. **`rand()`** (`Scus94491BpeSegment.java:588–592`): global 15-bit LCG — `seed = seed * 0x41c6_4e6d + 0x3039`, output `seed >>> 16 & 0x7fff`; seeded by `srand` @ 0x8002d270.
3. **`seed_800fa754`** (`Battle.java:505`): a **static** `java.util.Random` (Mersenne Twister) shared by all battles; re-seeded by `setMtSeed(seed)` @ 0x800cfed0 / `scriptSetMtSeed` @ 0x800cff24 (`seed ^ 0x75b_d924`); consumed by `scriptRand` @ 0x800cee50 (the battle-script random, min/max inclusive with the negative-range branch), the item randomizers (§4.7 — `nextInt(pool.length)`), `scriptGenerateAttack/RecoveryItem`, and the battle effect particles/rain (SEffe).

Inventory determinism: given (a) the MT seed the retail battle script sets at battle start, (b) the `simpleRand` seed (vsync-derived at boot), and (c) the deterministic drop table, **every item outcome is deterministic**: the drop roll (05b C12), the ball picks (MT), the shop transactions (pure arithmetic), the overflow order (the insertion-ordered drop list), the menu sort orders (comparators over the retail ID tables / names / icon indices). The only non-determinism in the surface is cosmetic: the Dabas reward text uses `ThreadLocalRandom` (L4, display only — M9-01) and the menu input timing (which is user-driven, not engine-driven). No item value, price, or eligibility depends on RNG.

## 11. Retail vs reference (the layer-4 surface)

1. **The stack model**: the retail item list was a flat no-stacking `u8` array (M7-01); the reference's `Inventory`/`ItemStack` (stacking, durability, merge-with-wear, overflow) is the L4 model that round-trips the flat format (the save loader builds size-1 stacks; `ITEM_STACK_SIZE` defaults to 1 — vanilla parity).
2. **The quantity UI (the Phase-A update)**: the retail shop sold and bought one item at a time; the reference adds the `QuantityMessageBoxScreen` buy/sell/discard dialogs, the equipment sort/filter/unequip hotkeys, and the ∞ glyph + font entry for repeatables (§12.1). The **sell-N-pays-once** behavior is a byproduct of that extension (I35) — preserved as reference behavior, flagged here.
3. **The menu re-architecture**: the screens are L4 Java classes preserving the retail screen flow, the `@Method` addresses where identifiable, and the retail geometry (the 5/6/7-row lists, the portrait grids); the hotkey prompt rows and the mouse/wheel support are L4.
4. **The registry/event surface**: every give/take/gather/shop event (§3.3) and the `MutableRegistry` shops — L4.
5. **The L4 data**: the 54/149/37/41 catalogs with prices and stats, the `EquipmentTypes` assignments, the random-ball pools, the retail ID tables — the reference author's extraction of the retail values (source-verified as L4 data, retail-byte-pending).
6. **The special equipment and the Wargod bits**: the four subclasses and their `additionExtra_474` flags (0x1/0x2/0x6) model the retail behavior (05c) — the corrected Wargod Calling/Ultimate Wargod/Destroyer Mace behavior is preserved per the checkpoint constraint.
7. **The `throw_item` replacement script**: the retail item-use scripts are not in the repo; the L4 `throw_item.txt` (entrypoints 32–44, the re-declared PCS entrypoints 0–31) is the reference's reconstruction — item *rules* are native (the Java item classes), the *presentation* is this script + the retail DEFFs.
8. **The config surface**: `INVENTORY_SIZE_CONFIG` (32), `ITEM_STACK_SIZE` (1), `ITEM_GROUP_SORT_MODE` (RETAIL), `REDUCE_MOTION_FLASHING_CONFIG` (the EquipmentScreen arrow variants) — the vanilla preset (M7-02) models the retail defaults.

## 12. The reference-repository update (Phase A)

### 12.1 The three-commit delta (I37)

The historical pinned baseline for M9 was `0c08c679b`; the updated baseline checked for this checkpoint is **`71ce130`** (3 commits ahead, 0 behind; clean, on `main`). The census (18 files changed, +614/−32; 5 new files) is entirely **L4 UI extension** — no `@Method` address changes, no retail-data table changes, no changes to previously documented call paths:

- **New message-box primitives**: `CycleMessageBoxScreen` (generic min/max cycling box with wrap), `ChoiceMessageBoxScreen` (cycling yes/no), `QuantityMessageBoxScreen` (cycling quantity 1..max) — the building blocks for the quantity flows.
- **Quantity flows**: shop buy (per-unit gold debit), shop sell (quantity removal, single-price payout), item-list discard (quantity shrink).
- **Equipment sort/filter**: `sortEquipmentByAlpha/Power/Icon` + `sortEquipmentInventory`, the `ITEM_GROUP_SORT_MODE`-aware `menuItemIconComparator`, the `INPUT_ACTION_MENU_SORT`/`FILTER`/`UNEQUIP`/`DELETE` input actions (CoreMod `:207–214`: delete/sort/advanced/help/mods editable, filter/unequip hidden).
- **The ∞ glyph**: the infinity font entry for the repeatable-item indicator (the battle `ItemListMenu`'s `lod_core.ui.infinity` text).
- **`CoreMod` additions**: `INPUT_ACTION_MENU_DELETE`, the `INVENTORY_SIZE_CONFIG` registry delegate (the 32-default cap config), the `INVENTORY_SIZE_CONFIG.onChange` → `items_2e9.setMaxSize` wiring.

All findings in this document were verified against the updated baseline; no previously documented finding (05a/05b/05c/06, M7, M8) is invalidated by the delta.

### 12.2 Known reference-specific behaviors (preserved, not normalized)

- Sell-N-pays-once (I35).
- `UNKNOWN_SHOP_01` (type 0 with item entries) and the Helena type-3 shops (I32) — the retail data quirks as extracted.
- The purifier's whole-`status_10` clear (I16) — the retail behavior (one purifier cures all ailments of the target).
- The instakill equipment flags (0x40/0x80) with no native consumer in the reference (I6) — the consumer is the retail DEFF/battle layer.
- The `0x800` recovery marker bit (I15) — set natively, consumed by the retail DEFF.
- `poison_guard`/`talisman`/`roses_hair_band` carrying the 0x80 resist-instakill flag (I24) — the retail data as extracted.

## 13. Findings register

| ID | Finding | Layer | Confidence |
|---|---|---|---|
| I1 | Four containers: `items_2e9` (`Inventory`, cap = `INVENTORY_SIZE_CONFIG` default 32, 1–9999, CAMPAIGN), `equipment_1e8` (`List`, hard cap 255), `goods_19c` (`GoodsInventory` set, unlimited/unique), `gold_94` (clamp 99,999,999 @ `SItem.addGold` 0x8002363c); the static battle-scoped `itemOverflow`/`equipmentOverflow` (cleared at `Battle:1976–1977`) | L3 fields / L4 model | source-verified |
| I2 | Price model: `InventoryEntry` defaults `buy = sell×2`, `sell = getPrice()` (0); `Item`/`Equipment` override to `buy = price×2`, `sell = price`; `Good` keeps the defaults (0/0) | L3/L4 | source-verified |
| I3 | `Inventory.give`: `GiveItemEvent` (FAIL vs OVERFLOW), temp-copy overflow simulation, atomic FAIL (returns the stack), `giveImpl` merge→empty-slot→append; `take` with `TakeItemEvent` rewrite; `takeFromSlot(slot, amount)` (clamped, identity-change fallback, cross-stack shortfall); `hasRoom`; event-disable switch | L4 model (retail ops) | source-verified |
| I4 | `ItemStack`: size+durability+extraData; `EMPTY` singleton; `isEmpty = size<1 ∥ durability<1`; `damage` wraps wear across items; `merge` transfers wear (`damage(other.max − other.current)`); battle-attack surface (`calculateStatMod`, `applyBuffs`, `alwaysHits`, `getSpecialEffect`, `getAttackElement`, `getAttackDamageMultiplier`); `isRepeat`/`isProtected` | L4 model | source-verified |
| I5 | `Item` defaults: `maxStackSize = 1` (the stackable items use `ITEM_STACK_SIZE`, default 1, 1–50, CAMPAIGN — vanilla = no stacking, matching the no-stacking retail save format), `hasDurability = false`, `canBeUsedNow = canBeUsed`; `UsageLocation {MENU, BATTLE}`; `TargetType {ALLIES, ENEMIES, ALL, INSTANT}`; `useInMenu` @ 0x80022d88 | L3 address / L4 model | source-verified |
| I6 | `Equipment` retail layout 0x00–0x1b (§2.6): `flags_00` (0x4 no-discard, 0x40 instakill, 0x80 resist-instakill — javadoc; no native consumer in the reference), `slot`, the hidden `equipableFlags_03` (commented out), the element sets, the per-hit MP/SP ×4, the HP/MP/SP multipliers, the resist/immunity booleans, `revive`/regens/`escapeBonus`, `speed_0f`…`magicAvoid_17`, `onHitStatusChance_18`/`onHitStatus_1b`; size always 1; `canBeDiscarded = !(flags & 0x4)` | L3 decompilation | source-verified (the layout) / flag semantics Inferred (javadoc-only) |
| I7 | `Good` (sortingIndex, size 1, price 0) + `GoodsInventory` (set; `GiveGoodsEvent`/`TakeGoodsEvent` with the `GoodsSource {INITIALIZATION, GAMEPLAY, DEBUGGER, EXTERNAL}` tag; the `TakeGoodsEvent` list may remove several goods; the retail `unpack`/`pack` 32-bit bitset leaves/ignores mod goods) | L3/L4 | source-verified |
| I8 | Retail ID mapping: `itemId < 0xc0` → `EQUIPMENT_IDS[itemId]` (192 entries, blanks = unused), `≥ 0xc0` → `ITEM_IDS[itemId−192]` (64 entries, retail 192–255, blanks = unused), goods bit `b` word `w` → `GOODS_IDS[w*32+b]` (37 entries, `LodMod.java:250–259`), shops `SHOP_IDS[0–65]` (66 entries: 39 named + 27 `empty_shop`, `LodMod.java:281–295`); consumers = the retail serializers, `scriptGiveChestContents`/`scriptTakeItem` @ 0x80024590, `GameVarParam:262` | L3/L4 compat | source-verified |
| I9 | The `SItem` primitives with retail addresses: `takeItemFromSlot` @ 0x800232dc, `takeEquipment` @ 0x800233d8, `giveEquipment` @ 0x80023484 (FAIL/TRUNCATE/default modes, maxInventorySize 255), `giveItems(List<EnemyDrop>)` @ 0x80023544 (overflow callback + count), `addGold` @ 0x8002363c, `setInventoryFromDisplay` @ 0x800239e0 (0x1000 skip), `sortItems` @ 0x80023a2c/0x80023a88, `equipItem` @ 0x80103a5c, `cacheCharacterSlots` @ 0x80103b10 | L3 | source-verified |
| I10 | Drop pipeline: `scriptSetBentDeadAndDropLoot` (05b C12) → `EnemyDrop` (item + performDrop + overflow callback) into `itemsDroppedByEnemies_800bc928` (`Battle:4333–4335`); battle-end repeat return → `itemOverflow` fallback (`Battle:8386–8393`); `PostBattleScreen.WAIT_FOR_INPUT_14` → `giveItems` → overflow → `TRANSITION_TO_TOO_MANY_ITEMS` (384×240, pop, push) or `UNLOAD_18`; **field gives that don't fit are lost** (no field overflow buffer); Dabas `giveReward` dispatch (item/equipment/good) | L3 | source-verified |
| I11 | The mod-event surface (all L4): `GiveItem/TakeItem`, `GiveEquipment/TakeEquipment`, `GiveGoods/TakeGoods`, `RepeatItemReturnEvent`, `GatherAttackItems/GatherRecoveryItems`, `GatherShopExtensions/ShopContents/ShopBuy/ShopSell/ShopSellPrice`, `GatherEquipmentTypes/GatherCharacterEquipmentTypes` | L4 | source-verified |
| I12 | The item catalog: 54 registrations — 12 recovery (healing_potion 5/50%, healing_fog 15/100%, healing_breeze 25/50%-all, healing_rain 60/100%-all, enemy_healing_potion 5/30%-enemy, sun_rhapsody 25/100%-MP, moon_serenade 100/100%-MP-all, spirit_potion 10/100%-SP, body_purifier 5, mind_purifier 10, depetrifier 15, angels_prayer 15), 24 attack (incl. psyche_bomb `0x8` / psyche_bomb_x), 2 balls (50), 4 buffs (200), 2 shields (200), 10 utility/special (charm_potion 2, pandemonium 200, smoke_ball 200, sachet 200, total_vanishing 10, the 4 cause-status 10, magic_sig_stone), the retail price values are L4 data | L4 data | source-verified (L4) / retail-byte source Unresolved |
| I13 | The battle item pipeline: `scriptUseItem` (table slot 1010) → `BattleItem.useInBattle` 3-stage async (DEFF registry-by-item-ID + `allocateDeffEffectManager` + `deffManager.flags_20 |= 0x40_0000`; `throw_item` frame injection; `pushFrame` onto the acting bent's script stack); the parameter words `stor[28]` = target (−1 ALL), `stor[30]` = user, `stor[8]` = colour; the `throw_item.txt` entrypoints 32–44 (43 = re-declared PCS entrypoint, unused by items) — the L4 replacement script | L3 native / L4 script | source-verified |
| I14 | Attack items: element + `damageMultiplier` (0/0x8/0x10/0x40 — the per-item assignments in §4.3) + `targetAll`; entrypoint 33; `getSpecialEffect = −1`; the multiplier's application is the DEFF (retail binary) | L4 data / L3 native | source-verified (the values) / application Unresolved |
| I15 | Recovery items: `canBeUsedNow(MENU)` = some party HP not full; menu `useInMenu` = `max × pct / 100` integer (100% → −1 full); battle `calculateStatMod` sets `user.status_0e |= 0x800` and returns `target.max × pct / 100` (the DEFF reads it); `alwaysHits`; entrypoint 34; the heal is percentage of the *target's* max | L3/L4 | source-verified (the 0x800 consumer is the retail DEFF — Unresolved) |
| I16 | Purifiers: the status bits body 0xb0 / mind 0x4e / depetrify 0x1; `canBeUsedNow(MENU)` = some party member has a matching bit; menu use requires a matching bit then **clears the target's entire `status_10`** (one purifier cures all ailments of the target); entrypoints 35/36/37 | L3/L4 | source-verified |
| I17 | Buffs/shields: `applyBuffs` writes the `0xb4–0xc7` power/temp words with `turns = 3` (other party) / `4` (self); speed is a percentage UNARY stat mod (`turns`); the per-hit SP/MP words `0xcc–0xd3` (player targets); negative power values = debuffs (power_down −50); shields = immunity booleans, **no DEFF** (script-frame only); all four are `isRepeat` + `isProtected`; entrypoints 38/39/40 | L3/L4 | source-verified |
| I18 | Random balls: the pools (25 attack / 7 recovery items, the L4 gather events) + `seed_800fa754.nextInt` (the battle MT); the ball sets `user.item_d4`/`registryIds[0]` and delegates to the selected item's `useInBattle`; the script twins `scriptGenerateAttackItem`/`scriptGenerateRecoveryItem` | L3 native pick / L4 data pools | source-verified |
| I19 | Repeat items: the ∞ glyph (the Phase-A font entry); the `RepeatItemReturnEvent` gate (default `returnItem = isRepeat()`); battle consumption → `usedRepeatItems_800c6c3c` → battle-end return (overflow → `itemOverflow`); menu consumption → `takeItemFromSlot`; `isProtected` = not stealable | L3/L4 | source-verified |
| I20 | The utility items: charm_potion resets the current engine state's `encounterAccumulator_800c6ae8` to 0 (WMap/SMap-with-encounters) — the cross-system effect; angels_prayer = the statMod-0 resurrection (DEFF-side); pandemonium = the all-allies DEFF; smoke_ball = the no-target escape (no DEFF); sachet = the single-enemy DEFF; total_vanishing = the erase with the `specialEffectFlag_14 & 0x80` resist check (returns −1/0); the cause-status items = the status-bit specials with damage powers −11/−13/−16/−12, entrypoint 44 | L3/L4 | source-verified |
| I21 | Equipment eligibility: `CanEquip` (DENY/FORCE/NORMAL, both sides) then the `EquipmentTypes` string-tag intersection; 23 tag constants; the 9 character tag sets (§5.1 table); the per-equipment assignments (L4 gather) | L3/L4 data | source-verified (L4 data) / the retail tag source Unresolved |
| I22 | `onEquip`/`onUnequip`: permanent FRACTIONAL mods for `hpMultiplier`/`mpMultiplier` (on the max), permanent UNARY flat mod (`contributesToOtherMods`) for `speed_0f`; the single write path is `CharacterData2c.equip`; the battle-side bake is separate (05b C22) | L3/L4 | source-verified |
| I23 | The four special equipment classes: Wargod Calling → `additionExtra_474.flag_00 |= 0x2`; Ultimate Wargod → `\|= 0x6` (0x2 + 0x4); Destroyer Mace → `\|= 0x1`; Detonate Arrow → `prepareAttack` (reset `_560`/`_564`, `item_d4 = DETONATE_ROCK`, DEFF 0xc1 via `loadSpellItemDeff`) + `attack() = DEFF`; `applyEffect` via `scriptApplyEquipmentEffect` (table slot 1011) | L4 (modeling retail) | source-verified (the 05c bits are the consumers) |
| I24 | The equipment catalog: 149 registrations, full §2.6 layout; the flag assignments (dragon_buster 0x4; brass_knuckle/gladius/indoras_axe 0x40; roses_hair_band/talisman/poison_guard 0x80); the dragoon armors (7, one immunity element each, 400), the element stones (7, one attack element each, 250), the 5000-tier (armor_of_legend, phantom_shield, legend_casque, ultimate_wargod) | L4 data | source-verified (L4) / retail-byte source Unresolved |
| I25 | The menu model: `MenuStack` (render bottom→top, input top→bottom, stop at HANDLED, wheel accumulation), `MenuScreen` (the input overrides, the hotkey prompt rows), `WhichMenu` (10 values, the unload contract), the message-box family (MessageBox/Choice/Cycle/Quantity — the latter three new in the Phase-A update), the menu assets (DRGN 6665/6666, dir 5815) | L4 (retail flow preserved) | source-verified |
| I26 | `UseItemScreen`: stages 0/1/2/3/100; 5-row item list (`canBeUsed(MENU)`, the 0x4000 dim flag for `!canBeUsedNow`, the icon comparator sort); ALL-target items apply to every party member (success = any); the `RepeatItemReturnEvent` consume gate; `response.text` → messagebox; the retail geometry @ 0x800fc8c0/0x800fc8dc | L3 retail screen / L4 class | source-verified |
| I27 | `ItemListScreen`: the two panes (items cap = config, equipment 255); `loadItemsAndEquipmentForDisplay(…, 0)` inlines the equipped items flagged `0x3000 \| charId` (0x1000 skip-on-write-back + 0x2000 no-discard); the L4 sort (icon comparator / slot comparator + write-back) and discard (quantity box for items, single for equipment, 0x2000 refused) | L3/L4 | source-verified |
| I28 | `EquipmentScreen`: stages 0–3/100; `getEquippableItemsForCharacter` (`canEquip` ∧ not-equipped ∧ slot filter); confirm = `equipItem` → `takeEquipment` → `giveEquipment(previous)` → `addHp/addMp(charId, 0)` re-clamp → re-sort; the L4 hotkeys unequip (all slots) / sort (alpha/power/icon cycle, the power formula in §6.4) / filter (slot cycle) | L3/L4 | source-verified |
| I29 | `TooManyItemsScreen`: consumes the static overflow lists (temp-inventory merge); states LOAD_ITEMS_1…UNLOAD_12; the keep/swap loop (5 overflow rows ↔ 7 inventory rows, the 0x6000 refused slots) and the discard path (refused when any overflowed entry is non-discardable equipment — "cannot be thrown away"); pushed **only** from `PostBattleScreen.TRANSITION_TO_TOO_MANY_ITEMS` | L3 retail screen / L4 class | source-verified |
| I30 | `GoodsScreen`: the two-pane viewer (sorted by `sortingIndex`, dealt alternately), the "Goods Icons" atlas, BACK = unload; give/take of goods is script/Dabas/shop only | L4 | source-verified |
| I31 | The battle `ItemListMenu`: `cacheItems` @ 0x800f83c8 (the `CombatItem02` same-item merge); rows `name × size` + ∞; `handleTargeting` (INSTANT→2, ALLIES→0, ENEMIES→1, the ALL flag); `getTargetingInfo` (`params[1]` = target or −1, `params[2]` = the item registry ID — the `scriptUseItem` arguments); `onUse` consume/repeat-queue | L3/L4 | source-verified |
| I32 | The shop model: `Shop {shopType_00 (0/1/3 in the retail data), items_00}` + `ShopEntry {item, price = getBuyPrice()}`; the `lod_core:shops` mutable registry; 41 registered shops (the 39 named retail-ID shops + `black_castle_item_shop` — registered but with no `SHOP_IDS` entry, no other references found in the repo — + `empty_shop`; `LodShops`); `SHOP_IDS` (66 entries: 39 named + 27 `empty_shop`, var 125 → `SItem.shopId_8007a3b4`); the per-entry extension selection (priority-ordered `accepts`); the Helena type-3 and `unknown_shop_01` type-0-with-items quirks | L3/L4 data | source-verified (the data) / the type-3/quirk semantics Inferred |
| I33 | The `ShopScreen` state machine: INIT_0 (the extensions event + attach) / LOAD_ITEMS_1 (the contents event) / INIT_2 / RENDER_3 (Buy/Sell/Carried/Leave) / BUY_4 / EXTENSION_5 / SELL_10 / START_FADE_16 / FINISH_FADE_17 / UNLOAD_19; the header gold + carried fraction; Carried → `ItemListScreen` | L3 retail screen / L4 class | source-verified |
| I34 | Buy transactions: items (gold check, `maxQty = min(cap − size, gold/price)`, the quantity box, per-unit give + per-unit debit, inventory-full alert); equipment (the 255 cap, the gold check, the character-portrait sub-flow with `canEquip` visibility + stat preview, the equip confirm, the displaced-item re-equip on overflow, no-gold-spend on failure, the unequipped buy for no-character); goods (already-have, single, the GAMEPLAY source) | L3/L4 | source-verified |
| I35 | Sell transactions: the equipment/items choice; the non-discardable refusal; items → the quantity box removes N but **`addGold(priceEvent.price)` once** (the single-unit sell price — the Phase-A extension quirk, retail sold one at a time); equipment → the trivial-1 box + single `addGold`; the "nothing to sell" empty-inventory alert; the `ShopSellEvent`/`ShopSellPriceEvent` (the price is mod-mutable) | L3/L4 | source-verified (the quirk) |
| I36 | The post-battle MERCHANT: action value 5 (05c) → `MerchantPostBattleAction` (15/30 frames, `whichMenu = NONE_0`); the shop opens in the submap via the script-written shop ID (var 125) and `SMap:4050/5272` — the shop is a submap screen, not a battle screen | L3/L4 | source-verified |
| I37 | The Phase-A reference update (`0c08c679b` → `71ce130`, 3 commits, 18 files, +614/−32, 5 new): the cycle/choice/quantity message boxes, the quantity buy/sell/discard flows, the equipment sort/filter/unequip, the ∞ glyph, the `INPUT_ACTION_MENU_DELETE/SORT/FILTER/UNEQUIP` actions, `INVENTORY_SIZE_CONFIG` — all L4 UI; no `@Method` address or retail-data changes; no prior finding invalidated | L4 | source-verified (the census) |
| I38 | RNG: `simpleRand()` @ 0x800133ac (16-bit LCG, `×9 + 0x3711`, `seed/2 & 0xffff`, vsync reseed on low-12-bits-zero), `rand()` @ `:588` (15-bit, `×0x41c6_4e6d + 0x3039`, `>>>16 & 0x7fff`, `srand` @ 0x8002d270), `seed_800fa754` (static battle MT, `java.util.Random`, seeded by `scriptSetMtSeed` @ 0x800cff24 = `seed ^ 0x75b_d924`, `scriptRand` @ 0x800cee50); the item randomizers consume the MT; the surface is fully deterministic given the seeds (the Dabas text `ThreadLocalRandom` is cosmetic) | L3/L4 | source-verified |

## 14. Cross-document dependencies

- **05a**: the 362-slot script-function table (slots 1010/1011 = `scriptUseItem`/`scriptApplyEquipmentEffect`; the `scriptGiveItem`/`scriptTakeItem` retail-ID functions; `scriptSetMtSeed` @ 0x800cff24; `scriptRand` @ 0x800cee50; game var 125 = the shop ID; var 61's neighbor cases), the `@Method` convention, the battle-stage model.
- **05b**: the bent models (the `0xb4–0xd3` power/temp words the buff items write, the `specialEffectFlag_14` the total-vanishing resist check and the equipment-flags merge read, the `item_d4` word, the `registryIds` array), the power-stat derivation (the equipment/buff stat consumers), the elemental damage stages (the attack items' elements, the equipment attack elements), the status-ailment model (the purifier's `status_10` clear, the cause-status bits, the 05b status bit table), the drop roll (`simpleRand` in `scriptSetBentDeadAndDropLoot`), the guard/per-hit economy (the per-hit MP/SP equipment+buff words `0xcc–0xd3`).
- **05c**: the equipment special-effect flag (`specialEffectFlag_14` save/clear/restore) and the `additionExtra_474` words (the Wargod/Destroyer `applyEffect` bits — I23), the post-battle MERCHANT action (value 5, I36), the `PostBattleScreen` report (the drop/overflow hand-off — I10), the battle menu framework (the `ItemListMenu`'s parent chain), the tutorial/counterattack item (`burn_out` — the 05c retail item 0xc3 = retail ID 195 = `ITEM_IDS[3]`, the catalog entry here).
- **06 (characters and progression)**: the `CharacterData2c` model (the `equipment_14` map, the `status_10` the purifiers clear, the vitals the `onEquip` mods and the heal items write), `cacheCharacterSlots` (the party cache the menus use), the goods/spirit eligibility (the `hasDragoon()` goods check), the post-battle report state machine (the drop grant's context), the stat-mod model (the `FRACTIONAL`/`UNARY` mod types the equipment and buff items use).
- **03-submap / 04-worldmap**: the menu lifecycle (the `WhichMenu` contract, the submap menu transition), the encounter accumulators (the charm potion's cross-system reset), the chest-open flow (`scriptGiveChestContents`, `lastChestContents`).
- **M9-01 (Dabas)**: the reward flow (`giveReward`'s three-way dispatch), the Dabas discard (the L4 messagebox path).
- **M7**: the save formats (the `0x19C`/`0x1E8`/`0x2E9` fields, the per-character `0x14+i` equipment bytes, the no-stacking retail item list, the force-give on load), the config storage (`INVENTORY_SIZE_CONFIG`/`ITEM_STACK_SIZE`/`ITEM_GROUP_SORT_MODE` locations and the vanilla preset).
- **M8**: the registries (items, equipment, goods, shops, elements, the input actions) and the event census (the §3.3 surface).

## 15. Open items and evidence gaps

1. **The attack-item damage-multiplier application** (I14) — the values (0/0x8/0x10/0x40) and their per-item assignments are source-verified L4 data; the *application* (how the DEFF scales the item hit by the multiplier — percent, flat add, or a damage-table index) is in the retail item DEFFs (not in the repo) → Unresolved (register).
2. **The `0x800` recovery marker bit** (I15) — set natively in `calculateStatMod` (`user.status_0e |= 0x800`); the consumer (the recovery DEFF's script) is retail → the bit's exact meaning (recovery-in-progress? the heal-color key?) → Inferred (register).
3. **The instakill equipment flags** (I6, I24) — the 0x40/0x80 javadoc meanings and the per-item assignments (brass_knuckle/gladius/indoras_axe; roses_hair_band/talisman/poison_guard) are source-verified; the *consumer* (the retail instakill battle logic — the "Three Executioners instakill" comments at `Battle:5397/5436` confirm instakill is DEFF/script-driven) is not native in the reference → the flag→behavior mapping is Inferred, the assignments retail-byte-pending (register).
4. **The retail shop prices** (I32/I34) — the reference prices every shop entry at the item's `getBuyPrice()` (price×2); retail shops may have had per-shop price overrides in the binary (the `ShopEntry.price` is mutable and the `ShopContentsEvent` can rewrite it — the mod surface anticipates it) → no evidence of retail per-shop prices in the repo → Inferred (register).
5. **The retail multi-item sell** (I35) — the reference's quantity sell removes N and pays once; whether retail's shop UI supported quantity sells at all (the retail shop screen is native and not in the repo) → the one-at-a-time assumption is Inferred (register).
6. **The unused retail ID slots** (I8) — the blank `ITEM_IDS` entries (indices 0, 4, 21, 27, 34, 59–63 — 10 blanks) and `EQUIPMENT_IDS` blanks (44 gaps: indices 75, 87, 92, 102, 129, 132, 141, 145, 147, 155, 158–191) — unused retail slots or items the reference didn't model (e.g. the `burn_out`-class retail-only items) → the blanks are source-verified gaps; what retail had there → Unresolved (register). Note: every *used* ID (the shop catalogs, the new-game setup, the drop tables) resolves — the blanks are only reachable via a hand-edited save or a retail script call.
7. **The `equipableFlags_03` word** (I6) — commented out in the reference decompilation (`_02`/`equipableFlags_03`); the retail 0x02–0x03 bytes between `slot` and `attackElement_04` are undecoded → Unresolved (register).
8. **The Helena type-3 shops and `unknown_shop_01`** (I32) — the type-3 `shopType_00` (two item shops in the Helena script flow) and the type-0 shop with item entries are source-verified data quirks; the type's original retail role (a third shop UI variant? a script-flagged behavior?) → Unresolved (register; the per-entry extension selection makes the type behaviorally inert in the reference).
9. **The `GoodsSource` semantics beyond logging** (I7) — the source tag is carried on the `GiveGoodsEvent`; no native consumer branches on it (the mod surface can) → the retail counterpart of the source distinction → Unresolved (register).
10. **The retail item-use scripts** (I13) — the `throw_item.txt` entrypoint names and the PCS re-declarations are the L4 reconstruction; the retail item-use scripts (the original DEFF-driven item effects, the exact stor-word contracts) are not in the repo → the entrypoint *numbers* and the stor words are source-verified (the L4 classes and script agree); the retail byte-level script → asset-validated-pending (register).
