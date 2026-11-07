# M7-01 — Save Data Formats, Campaigns, and Save/Load Lifecycle

Milestone M7 (Persistence), deliverable 1 of 2. Companion: `research/saves/02-config.md` (M7-02).

Primary sources (all `Severed-Chains` @ `0c08c679b` unless noted): `legend/game/saves/*`, `legend/core/tags/*`, `legend/game/saves/serializers/*`, `legend/game/EngineState(Type)`, `legend/game/wmap/WMap`, `legend/game/submap/SMap`, `legend/game/combat/Battle`, `legend/game/title/{Ttle,GameOver}`, `legend/game/credits/{Credits,FinalFmv}`, `legend/game/types/{GameState52c,Flags}`, `legend/game/characters/{StatCollection,StatType,UnaryStatType,VitalsStatType,UnaryStatModType,RetailCharacterTemplate}`, `legend/game/Main`, `legend/lodmod/{LodMod,RetailCampaignType,Legacy}`, `legend/core/{GameEngine,IoHelper}`.

Layering note: **RETAIL** is original PSX save behavior (layer 3). **V1–V10** are reference-implementation save formats (layer 4) that exist so previously-shipped Severed Chains saves keep loading. Memcard import is the reference's handling of original PSX memory-card content (layer 3 data in a layer 4 container). Nothing here is `asset-validated` (no retail save bytes or memcard images exist in this environment).

---

## 1. Big picture

Persistence is organized in three tiers:

1. **Campaigns** — a directory under `saves/` (`SaveManager.dir = Paths.get("saves")`, `SaveManager.java:79`). One directory per play-through; holds zero or more save files plus one `campaign_config.dcnf` (per-campaign config, M7-02). Campaign name comes from the `campaign_name` config entry (a CAMPAIGN-location `StringConfigEntry`, `Campaign.java:40-46`); the directory name is the slugified campaign name (`IoHelper.slugName`).
2. **Save files** — `<campaign>/<slugified-save-name>.dsav`, matched by the glob `*.dsav` (`SaveManager.SAVE_MATCHER`, `SaveManager.java:80`). Each file self-identifies its version by a 4-byte magic at offset 0 (no external metadata, no checksum).
3. **The in-memory save model** — `SavedGame` subclasses: `RetailSavedGame` (RETAIL + V1–V7), `MemcardSavedGame` (a `RetailSavedGame` with memcard location fields), `SeveredSavedGame` (V8–V10), `InvalidSavedGame` (unparseable files).

```
SavedGame (abstract; campaign, version, fileName, saveName, campaignType, config,
             engineState: RegistryId, engineStateData: Tag, modData: ListTag)
├── RetailSavedGame        — full campaign-state mirror of the ROM GameState52c fields
│   └── MemcardSavedGame   — version "OG"; + locationType/locationIndex (memcard slot origin)
├── SeveredSavedGame       — V8+ model; + portrait atlas (FileData, w×h), charPortraits,
│                            characters: List<SavedCharacter> (V1- or V2-shaped), activeParty
└── InvalidSavedGame       — placeholder; createGameState() throws InvalidSaveException
```

`SavedGame` fields (`SavedGame.java:10-19`): `campaign`, `version` (the `SaveVersion.name` string, e.g. "OG"/"V1"/"V10"), `fileName` (slug, no extension), `saveName` (display name — may differ from the filename in V2+ where the name is embedded), `campaignType` (RegistryId — which `CampaignType` built/loads this save), `config` (per-save `ConfigCollection`, M7-02), `engineState` (RegistryId of the engine state the save was written under), `engineStateData` (per-state Tag, §10), `modData` (ListTag of mod payloads, V10 only).

Every `SavedGame` answers three questions: `isValid()`, `createSaveCard()` (the save-menu UI card — retail layout `RetailSaveCard` vs modern `SeveredSaveCard`), and `createGameState()` — the load path's core, which materializes a live `GameState52c` from the parsed model.

### The save model mirrors the ROM state struct

`GameState52c` (`GameState52c.java`) is the live game-state struct whose field names carry their ROM offsets (`scriptData_08`, `gold_94`, … `indicatorsDisabled_4e3`; total ≈ 0x52C bytes — the "52c" in the class name). Both save models (`RetailSavedGame` fields at `RetailSavedGame.java:48-88`, `SeveredSavedGame` at `SeveredSavedGame.java:30-66`) mirror exactly the subset of that struct that is persistent, and `createGameState()` copies field-by-field back into a fresh `GameState52c` (see §4.4/§6.2). The decompiled ROM globals (`gameState_800babc8`, `campaignType` at `Scus94491BpeSegment_800b`, `whichMenu_800bdc38`, `loadingNewGameState_800bdc34`, …) are what the save/load machinery touches.

### No checksums, anywhere

No save version carries a CRC/checksum/signature beyond the 4-byte magic. Integrity = magic match + a deserializer running to completion. Any `Throwable` mid-deserialize is wrapped in `InvalidSaveException` (`SaveManager.loadData`, `SaveManager.java:382-396`); files that match no magic become `InvalidSavedGame` placeholders (still listed in the UI, un-loadable). Truncated or wrong-length sub-fields throw (`FileData.checkBounds`), the same way.

---

## 2. The Tag data model

The reference's modern save format (V10), the per-state save data (V8+), the per-save config tail (V2+, M7-02 §4), and mod data are all built on one self-describing tagged type system: `legend/core/tags/*` — **10 tag types** (the plan's "12 tag types" counts the 11 source files plus the interface; the type system itself has 10 type codes and 10 concrete classes).

### 2.1 Type codes (`Tag.java:8-21`)

| Code | Constant | Class | Wire encoding (all little-endian) |
|---|---|---|---|
| 0 | `TAG_TYPE_MAP` | `MapTag` | varint count, then per entry: varint type, string key, payload |
| 1 | `TAG_TYPE_LIST` | `ListTag` | varint count, then per element: varint type, payload |
| 10 | `TAG_TYPE_STRING` | `StringTag` | varint length + UTF-8 bytes |
| 11 | `TAG_TYPE_INT` | `IntTag` | unsigned LEB127 varint |
| 12 | `TAG_TYPE_LONG` | `LongTag` | unsigned LEB127 varlong (64-bit) |
| 13 | `TAG_TYPE_FLOAT` | `FloatTag` | 4-byte IEEE-754 float |
| 14 | `TAG_TYPE_BOOL` | `BoolTag` | 1 byte, non-zero = true |
| 15 | `TAG_TYPE_RAW` | `RawTag` | varint length + raw bytes |
| 30 | `TAG_TYPE_REGISTRY_ID` | `RegistryIdTag` | u24 length + US-ASCII (`RegistryId.toString()`); empty string reads as `null` |
| 31 | `TAG_TYPE_ENUM` | `EnumTag` | varint length + UTF-8 of the enum `name()` (class recovered on the consumer side) |

### 2.2 Primitives (from `FileData`)

- **Varint** (`FileData.readVarInt:283`, `writeVarInt:435`): unsigned LEB127, 7 bits/byte, MSB = continue. `writeVarInt` of a negative value encodes the two's-complement form (up to 10 bytes for `int`). The `IntRef` overloads advance a shared cursor.
- **String** (`FileData.writeString:556` / `readString:566`): varint byte-length + UTF-8.
- **Ascii** (`FileData.writeAscii:502-520` / `readAscii:522-540`): **3-byte (u24) length prefix + US-ASCII**. This is the default `writeRegistryId`/`readRegistryId` encoding (`FileData.java:577-600`). A 2-byte variant exists and is used by `FileData.writeEnum/readEnum` (u16 length + ASCII) — *not* by `EnumTag` (which uses the varint/UTF-8 STRING encoding).
- **RegistryId legacy alias**: the int-offset `readRegistryId` overload (`FileData.java:593-596`) rewrites `"lod-core"` → `"lod_core"` ("old core mod ID" rename; in-source comment "Dunno how long we'll keep this. Maybe forever."). The `IntRef` overload does **not** do this rewrite (source-verified asymmetry).
- **Float** (`FileData.java:262-281`): 4-byte IEEE-754 LE.

### 2.3 Structural notes

- `Tag.makeTag(type)` (`Tag.java:68-96`) is the deserialization dispatch; unknown codes throw `IllegalStateException`.
- `MapTag` stores entries in a `HashMap` (`MapTag.java:10`) — **serialization order of map entries is nondeterministic across runs** (JVM hash order). A V10 file is not byte-stable for identical logical content; any byte-comparison tooling must parse, not diff.
- Every tag is `clone()`-able (deep) — used for save-card preview state.
- Deserialization is streaming (cursor-based, `IntRef`); a short/corrupt file throws out of `FileData.checkBounds`.

---

## 3. Save versions

`SaveVersion` (`SaveVersion.java:10-22`) — the complete registry of magics, all read as little-endian u32 at file offset 0:

| Version | Magic (LE u32) | Bytes at 0..3 | Name | Deserializer | Era (reference git history) |
|---|---|---|---|---|---|
| RETAIL | `0x01114353` | `53 43 11 01` ("SC" + 2 ctrl) | "OG" | `RetailSerializer.fromRetail` | original PSX (SCUS94491), layer 3 |
| V1 | `0x76615344` | `DSav` | "V1" | `RetailSerializer.fromV1` | first reimpl save format (2022-12, commits `0955cfc06`/`3ceb7095a`) |
| V2 | `0x32615344` | `DSa2` | "V2" | `LegacySerializer.fromV2To7` | 2023-03-03 `45a7e7f2f` "Dememulate gamestate" |
| V3 | `0x33615344` | `DSa3` | "V3" | "V2To7" | V2-era line; adds name + maxHp/maxMp (§5.2) |
| V4 | `0x34615344` | `DSa4` | "V4" | "V2To7" | V2-era line; RegistryIds for equipment/items |
| V5 | `0x35615344` | `DSa5` | "V5" | "V2To7" | 2025-09-05 `77c39e235` "Stacks" (item size/durability) |
| V6 | `0x36615344` | `DSa6` | "V6" | "V2To7" | 2025-11-23 `2e2eba44c` "Add goods registry" |
| V7 | `0x37615344` | `DSa7` | "V7" | "V2To7" | V6-era line; per-addition entries + RegistryId selectedAddition |
| V8 | `0x38615344` | `DSa8` | "V8" | `V8Serializer.fromV8` | 2026-01-06 `24b62ff6b` "Engine states registry" |
| V9 | `0x39615344` | `DSa9` | "V9" | `V9Serializer.fromV9` | 2026-05-02 `f9517449e` "Character Registry (#2661)" |
| V10 | `0x61615344` | `DSaa` | "V10" | `V10Serializer.fromV10` | 2026-08-30 `786a3cc5e` "New tagged save format (#2797)" — **current** |

(V3/V4/V7 have no single identifiable introduction commit in the pinned history; their feature placement is source-verified in `LegacySerializer`, the era is `Inferred` from adjacent commits.)

- **Matching** (`SaveManager.loadData:382-396`): versions are tried in enum order (RETAIL first); `defaultMatcher` (`SaveVersion.java:44-50`) checks `data.readInt(0) == version.code` and returns `data.slice(0x4)` — the deserializer always receives the file **minus** the 4-byte magic.
- **Writing** uses only one version: `GameEngine.SAVES = new SaveManager(SaveVersion.V10, V10Serializer::toV10)` (`GameEngine.java:103`). All other versions are read-only history.
- "Migration" is **not** an in-place chain — there is no V_n → V_{n+1} converter. Each version has its own deserializer that directly produces the current in-memory model (`RetailSavedGame` or `SeveredSavedGame`). The only true *data-level* migration is `SaveMigrator.upgradeEngineStateData` (§10.3), which upgrades the legacy per-state blobs.

---

## 4. RETAIL and V1 — fixed-layout saves

Both share one game-state struct decoder, `RetailSerializer.deserializeRetailGameState` (`RetailSerializer.java:60-206`), differing only in where the struct sits in the file and in the surrounding bytes.

### 4.1 File wrappers

| | RETAIL (PSX) | V1 |
|---|---|---|
| total min. size | **0x6E4** (1764 B) | **0x518** (1304 B) |
| 0x000–(state−1) | 0x1FC bytes — save-card icon image (not parsed) + location fields below | 0x30 bytes — header (not parsed; the display name is taken from the *filename*, not the file) |
| locationIndex (u8) | @ 0x1A8 | @ 0x2C |
| locationType (u8) | @ 0x1A9 | @ 0x2D |
| game-state struct | @ **0x1FC** (absolute) | @ **0x30** (absolute) |

(Offsets for the RETAIL location bytes are read from the *original* file, not the sliced struct — `RetailSerializer.java:36-37` / `:48-49`. `FileData.slice(offset)` = from `offset` to end, `FileData.java:62-65`.)

The 0x1FC/0x30 prefixes and the 0x54 (RETAIL) trailing padding are never parsed — only the game-state struct and the two location bytes are (source-verified). The retail file is exactly what sits in one PSX memcard slot: `SaveManager.splitMemcard` reads `memcard.slice(i * 0x2000, 0x720)` (0x720 = 1824 B slot image; the save's last read ends at 0x6E4) — see §11.

### 4.2 Game-state struct (relative to struct start; identical for RETAIL and V1)

All integers little-endian. "ROM-off" = the `GameState52c` field whose name carries the same offset (the struct is a straight dump of the retail ROM state).

| Off | Size | Field (ROM-off) | Notes |
|---|---|---|---|
| 0x008 | 128 B | `scriptData_08` — 32 × i32 | ROM script variables (script op 249 family). Known meanings from in-source comments: idx 18 "stardust turned in", 20/21 Zy marshland/boat, 27 "hero tickets" (`GameState52c.java:21-29`) |
| 0x088 | 12 B | `charIds_88` — 3 × i32 | active party; −1 = empty slot; ids ≥ 9 dropped on load (`removeIf(i >= 9)`) |
| 0x094 | 4 | `gold_94` | |
| 0x098 | 4 | `chapterIndex_98` | |
| 0x09C | 4 | `stardust_9c` | |
| 0x0A0 | 4 | `timestamp_a0` | game-clock ticks; drives unlock-timestamps on load |
| 0x0A4 | 4 | `submapScene_a4` | submap primitive index |
| 0x0A8 | 4 | `submapCut_a8` | submap cut |
| 0x0B0 | 4 | `_b0` | "Used by the script engine" (comment) |
| 0x0B4 | 4 | `battleCount_b4` | total battles |
| 0x0B8 | 4 | `turnCount_b8` | total player turns |
| 0x0BC | 128 B | `scriptFlags2_bc` — 32 × i32 | **1024-bit** flag space (packed index `p` → int `p>>>5`, bit `p&0x1f`, `Flags.java:33-39`). Known: bit 0x4_0000 "has psych bomb X", bit 0x800_0000 "died in arena fight" (comments); transform bits per character §6.1 |
| 0x13C | 32 B | `scriptFlags1_13c` — 8 × i32 | 256-bit space |
| 0x15C | 32 B | `wmapFlags_15c` — 8 × i32 | 256-bit space |
| 0x17C | 32 B | `visitedLocations_17c` — 8 × i32 | 256-bit space |
| 0x19C | 8 B | goods — 2 × i32 bitmask | 64 retail goods; bit b of word w → `LodMod.GOODS_IDS[w*32+b]` (retail compact index → registry id) |
| 0x1A4 | 32 B | `_1a4` — 8 × i32 | "at least 3, related to submap music" (comment; length 8 unconfirmed) |
| 0x1C4 | 32 B | `chestFlags_1c4` — 8 × i32 | "not 100% sure this is only chest flags" (comment) |
| 0x1E8 | ≤255 B | equipment list | u8 ids, **0xFF-terminated**; id → `LodMod.EQUIPMENT_IDS[id]` (retail index → registry id; blank entry = warn + skip) |
| 0x2E9 | ≤64 B | item list | u8 ids, 0xFF-terminated; id → `LodMod.ITEM_IDS[id − 192]` (retail item ids 192..255); each becomes `InventoryEntry(id, size=1, durability=1)` — **retail saves have no stacking** |
| 0x32C | 9 × 0x2C | character records | see §4.3 |
| 0x4D8 | 2 | `pathIndex_4d8` (u16) | world-map path |
| 0x4DA | 2 | `dotIndex_4da` (u16) | |
| 0x4DC | 1 | `dotOffset_4dc` (u8) | read as byte, stored in a `FloatTag`/float field (0..255) |
| 0x4DD | 1 | `facing_4dd` (i8) | |
| 0x4DE | 2 | `directionalPathIndex_4de` (u16) | |
| 0x4E3 | 1 | `indicatorsDisabled_4e3` (u8 ≠ 0) | save-config flag (M7-02 territory: indicator mode) |
| 0x4E4 | 1 | (u8 ≠ 0) `isOnWorldMap` | determines `engineState` = WORLD_MAP / SUBMAP |
| 0x4E6 | 2 | (u16) `characterInitialized` | **read and discarded** (`/*savedGame.characterInitialized = */`, `RetailSerializer.java:197`) |

Derived on load: `engineStateData` = empty `MapTag` (retail carries no per-state Tag — the path/dot fields above *are* the state data); `maxHp` = `Legacy.RETAIL_HP[firstPartyCharId][level−1]`, `maxMp` = `firstPartyChar.dlevel × 20` (save-card display heuristics, `RetailSerializer.java:203-204`); `locationName` = `LodMod.getLocationName(locationType, locationIndex)` — type 1 → ROM `worldMapNames_8011c1ec`, type 3 → ROM `chapterNames_80114248`, else ROM `submapNames_8011c108`, out-of-range → "Unknown location" (`LodMod.getLocationName`, in-source ROM anchors `LAB_80108b5c`…).

### 4.3 Retail character record (0x2C = 44 bytes, ×9 fixed slots)

| Off | Size | Field | Notes |
|---|---|---|---|
| 0x00 | 4 | xp (i32) | |
| 0x04 | 4 | flags (i32) | party flags incl. `IN_PARTY` |
| 0x08 | 2 | hp (u16) | current |
| 0x0A | 2 | mp (u16) | |
| 0x0C | 2 | sp (u16) | |
| 0x0E | 2 | dlevelXp (u16) | |
| 0x10 | 2 | status (u16) | status-effects bits |
| 0x12 | 1 | level (u8) | |
| 0x13 | 1 | dlevel (u8) | |
| 0x14–0x18 | 5 × 1 | equipment (u8 × 5) | slot `i` → `EquipmentSlot.fromLegacy(i)`; id → `LodMod.EQUIPMENT_IDS` |
| 0x19 | 1 | selected addition (i8) | **retail global index** — converted per slot: `idx − additionOffsets_8004f5ac[slot]` (`RetailSerializer.java:165`); negative/invalid → first retail addition for that character; slot with no additions → ignored |
| 0x1A–0x21 | 8 × 1 | addition levels (u8 × 8) | stored value kept as `max(1, value)` (`AdditionInfo` floor); only positions `< CHARACTER_ADDITIONS[slot].length` are mapped to addition ids (positional — **retail additions only**) |
| 0x22–0x29 | 8 × 1 | addition xps (u8 × 8) | |

**Retail-save compatibility: `Verified` from the `RetailSerializer` source.** Every retail field has an explicit reader with a cited offset; the ID mapping tables (`EQUIPMENT_IDS`, `ITEM_IDS`, `GOODS_IDS`, `CHAR_IDS`, `CHARACTER_ADDITIONS`, `additionOffsets_8004f5ac`, `RETAIL_HP`) are in-repo content in `legend.lodmod.Legacy`/`LodMod` (registry-backed). The retail save loads **without modification** of the file bytes — the reference re-parses them directly. What retail *cannot* express (and silently loses on a retail→modern re-save): item stacks/durability/extraData, mod-added goods, mod-added equipment/items (retail id tables are closed), per-addition unlock state (replayed conditionally, §6.3), mod characters (slots are positional retail ids), and any engine state other than WMAP/SMAP (the 0x4E4 byte is binary).

### 4.4 `createGameState()` — RetailSavedGame

`RetailSavedGame.createGameState()` (`RetailSavedGame.java:96-250`) reconstructs the live world:

- Copy state fields into a fresh `GameState52c` (arraycopies for the fixed fields; registry lookups + `give(..., GoodsSource.INITIALIZATION)` for goods; `ItemStack` rebuild for items, skipping unknown item ids with a warning).
- **Characters are rebuilt by replay, not by stat copy**: for each of the 9 retail slots, `template.make(gameState)` then `while(level_12 < saved.level) applyLevelUp(null, INITIALIZATION)` and `while(dlevel_13 < saved.dlevel) applyDragoonLevelUp(null)` — base stats are *recomputed* from the character templates, then `xp`, `partyFlags`, current HP/MP/SP (stat `setCurrent`), `dlevelXp`, `status` are applied.
- Per-character equipment via `EquipmentSlot` → registry lookup (unknown → warn + skip).
- **Additions**: retail additions in `CHARACTER_ADDITIONS[slot]` order first, then any extra saved entries; each gets `level`/`xp` copied and then `checkUnlock(character)` → `unlock(timestamp_a0 + i)` — **unlock state is re-derived from game conditions at load time**, not read from the save.
- **Spells**: same retail-first-then-extras pattern; `checkUnlock` → `unlock` (spells store no per-spell state in retail/V1).
- `selectedAddition`: kept if the id still resolves, else first unlocked addition.
- Transform flags: `charData[i].hasTransformed = scriptFlags2.get(bit)`, bits `0x1b8, 0x1ba, 0x1b9, 0x1bb, 0x1bc, 0x1bd, 0x1be, 0x1bf, 0x1c0` for slots 0..8 (`RetailSavedGame.java:240-248`) — the per-character transform state lives *inside the script-flag space*, not in the character record.

---

## 5. V2–V7 — the legacy streaming line

One deserializer, `LegacySerializer.fromV2To7` (`LegacySerializer.java:32-296`), streams fields with an `IntRef` cursor; version-gated by `SaveVersion.ordinal()`.

### 5.1 Common flow (after the 4-byte magic)

1. **Name**: V2 → the *filename* (no embedded name); V3+ → ascii (u24-len).
2. **maxHp/maxMp**: V3+ → two i32s (written values); V2 → derived later from `RETAIL_HP`/`dlevel×20` (same as retail).
3. u8 locationType, **u16** locationIndex (wider than retail's u8).
4. i32 `_04` — read and discarded.
5. 32 × i32 scriptData.
6. **i8 charSlotCount** + that many **i16** charIds (−1 skipped) — the party is now variable-length.
7. i32 ×6: gold, chapterIndex, stardust, timestamp, submapScene, submapCut.
8. i32 ×3: `_b0`, battleCount, turnCount.
9. 32 + 8 + 8 + 8 × i32: scriptFlags2, scriptFlags1, wmapFlags, visitedLocations.
10. **≤V5 only**: 2 × i32 goods bitmask (retail 64-goods table).
11. 8 × i32 `_1a4`; 8 × i32 chestFlags.
12. u16 equipmentCount, u16 itemCount, **(V6+ only) u16 goodsCount**.
13. Equipment × equipmentCount: **V4+** `RegistryId`; **V2/V3** u8 retail index.
14. Items × itemCount: id (V4+ RegistryId / V2/V3 u8 retail index); **V5+** i32 size, i32 durability, i32 extraDataSize + skip raw bytes ("no one was using extraData at this point, just gonna remove the reader" — in-source comment); <V5 → size = durability = 1.
15. **(V6+)** goods × goodsCount: RegistryId.
16. u16 charDataCount — **read and discarded** ("Not yet used"); **always 9 character records follow**.
17. 9 × character records (§5.2).
18. `charIndices.removeIf(i >= 9)`.
19. **≤V4**: 8 × i32 `_4b8` discarded (this is the ROM `0x4B8` region, later dropped from the state struct).
20. u16 pathIndex, u16 dotIndex, u8 dotOffset, i8 facing, u16 directionalPathIndex.
21. i32 characterInitialized (discarded).
22. u8 isOnWorldMap → `engineState`.
23. **≤V4**: u8 mono, u8 vibrationEnabled, u8 morphMode (all discarded — decompiled-removed settings).
24. u8 indicatorsDisabled.
25. **File tail = per-save config** — `ConfigStorage.loadConfig(config, SAVE, data.slice(offset.get()))` (M7-02 §4). The remainder of the file *is* the SAVE-location `.dcnf` payload.
26. Derived: locationName, `engineStateData` = empty `MapTag`, maxHp/maxMp.

### 5.2 Per-character record (V2–V7, all i32-based vs retail's packed u16s)

- i32 ×7: xp, flags, hp, mp, sp, dlevelXp, status.
- **u16 level, u16 dlevel** (retail: u8/u8).
- Equipment: **V4+** i8 equipmentSlotCount, then count × {ascii slot *name*, RegistryId}; **<V4** fixed 5 × u8 retail index (slot = `fromLegacy(i)`).
- selectedAddition: **V7+** RegistryId; **<V7** i16 retail global index (converted via `additionOffsets_8004f5ac[slot]` as in retail).
- i16 additionCount. **V7+**: count × {RegistryId, i16 level **stored as level−1** (reader does `+1`), i32 xp} — mod additions supported. **<V7**: fixed 8 × {i16 level (raw, no +1), i32 xp} positional to retail additions.
  - *(The level-encoding inconsistency between V1 (raw byte, floored at 1), V2–V6 (raw i16), and V7+ (level−1) is source-verified as-is.)*

### 5.3 Version feature matrix (what each version *added*)

| Version | Additions over previous |
|---|---|
| V2 | streaming format; variable-length party (i16 ids); embedded file tail = SAVE config; u16 locationIndex |
| V3 | embedded save name; stored maxHp/maxMp (i32 ×2) |
| V4 | RegistryIds for equipment/items (mod equipment); per-char equipment as (slotName, RegistryId) pairs with count |
| V5 | item size/durability/extraData (stacks); |
| V6 | goods as RegistryId list (goods registry) replacing the 64-bit retail bitmask |
| V7 | per-addition {id, level, xp} entries; selectedAddition as RegistryId (mod additions) |

---

## 6. V8 — engine-state-registry era

`V8Serializer.fromV8` (`V8Serializer.java:39-179`) → `SeveredSavedGame` + `SeveredSavedCharacterV1` records. Streaming layout:

1. ascii name; **RegistryId campaignTypeId** (campaigns become mod-registered `CampaignType`s — `LodMod.RETAIL_CAMPAIGN_TYPE` registers `"retail"`, `LodMod.java:122-124`); ascii locationName (free text — the retail type/index tables are gone).
2. Portrait atlas: **not in the file** — `SAVES.getRetailAtlas()` (`Loader.loadFileSync("retail_atlas.png")`, a bundled resource, lazily loaded via `Latch`), fixed **512×64**; per-character portrait rect = `(charIndex*48, 0, 48, 48)`.
3. 32 × i32 scriptData.
4. i8 charSlotCount + × i16 activeParty (no −1 skip on read — the writer controls the count; `removeIf(i >= characters.size())` afterwards).
5. i32 ×4 gold/chapterIndex/stardust/timestamp; i32 ×3 `_b0`/battleCount/turnCount.
6. 32+8+8+8 × i32 flag spaces. (No goods bitmask — goods are registry-ids only from V6.)
7. 8 × i32 `_1a4`; 8 × i32 chestFlags.
8. u16 ×3: equipmentCount, itemCount, goodsCount → RegistryIds; items {RegistryId, i32 size, i32 durability, i32 extraDataSize + skip}.
9. u16 charDataCount; × characters (retail-bound):
   - `templateId = CHAR_IDS[charIndex]` — **charIndex is the retail slot index** (mod characters not possible in V8);
   - i32 maxHp, i32 maxMp (stored on the V1 character for save-card rendering);
   - i32 ×7 xp/flags/hp/mp/sp/dlevelXp/status; **u16 level, u16 dlevel**;
   - **`hasTransformed` derived** from `scriptFlags2.get(transformFlags[charIndex])` with `transformFlags = {0x1b8, 0x1ba, 0x1b9, 0x1bb, 0x1bc, 0x1bd, 0x1be, 0x1bf, 0x1c0, 0x1c1}` (`V8Serializer.java:26-37`) — note the **10th entry (0x1c1) is latent**: `CHAR_IDS` has 9 retail characters (`Legacy.java:47-57`), so `charDataCount` cannot reach 10 without an out-of-bounds throw;
   - i8 equipmentSlotCount × {ascii slot, RegistryId};
   - RegistryId selectedAddition;
   - i16 additionCount × {RegistryId, i16 level (+1 decode), i32 xp}.
10. i32 characterInitialized (discarded).
11. **RegistryId engineStateId** — the save's engine state is now *any registered* `EngineStateType` (mod states possible, though retail writes only WMAP/SMAP, §10).
12. **i32 engineStateDataLength + raw blob** — negative length → `InvalidSaveException`; the blob runs through `SaveMigrator.upgradeEngineStateData(engineStateId, blob, fallbackScene=4, fallbackCut=675)` (§10.3) to produce the per-state `MapTag`.
13. File tail = SAVE config (as V2–V7).

Load (`SeveredSavedGame.createGameState`, `SeveredSavedGame.java:78-128`): same state-field copy as retail; characters via `savedCharacter.make(gameState)` — the **V1 character model replays level-ups** exactly like retail (`SeveredSavedCharacterV1.make`, `SeveredSavedCharacterV1.java:67-165`), with retail-first-then-extras addition/spell unlock *replay* and the same `CHARACTER_ADDITIONS[this.index]` / `CHARACTER_SPELLS[this.index]` retail tables. V1 characters therefore still require the retail index (their `index` field) and conditional-unlock replay.

---

## 7. V9 — character-registry era

`V9Serializer.fromV9` (`V9Serializer.java:24-136`) → `SeveredSavedGame` with **moddable characters**:

- Steps 1–3 of V8, except the **atlas is in the file**: i32 width, i32 height, i32 byte-size, then raw atlas bytes.
- Steps 5–8 of V8 (state fields, flags, inventories) unchanged.
- Characters: u16 charDataCount × {
  - **RegistryId templateId** — any registered `CharacterTemplate` (mod characters supported);
  - **`template.deserialize(data, offset)`** — the template knows its own character's byte layout (abstract `CharacterTemplate.deserialize(FileData, IntRef)`, `CharacterTemplate.java:39`); for retail characters this is `RetailCharacterTemplate.deserialize` → a **`SeveredSavedCharacterV2`**;
  - 4 × i16 portrait rect (x, y, w, h) — explicit per-character portrait, no longer derived.
- Steps 10–13 of V8 (characterInitialized, engineStateId, length-prefixed state blob + migrator, config tail) unchanged.

### 7.1 V2 character record (retail template byte layout, `RetailCharacterTemplate.java:95-143`)

| Field | Encoding |
|---|---|
| flags | i32 |
| xp | i32 |
| dxp (dlevelXp) | i32 |
| level | **i32** (V8: u16) |
| dlevel | **i32** |
| hasTransformed | **1-byte bool, explicit** (V8: derived from scriptFlags2) |
| status | i32 |
| stats | `StatCollection` byte layout (§7.2) |
| equipmentSlotCount | i32 (V8: i8) |
| equipment × count | ascii slot name + RegistryId (null id → skip) |
| selectedAddition | RegistryId |
| additionInfoCount | u16 |
| additions × count | RegistryId + **enum `UnlockState`** (u16-len ascii) + i32 unlockTimestamp + i16 level + i32 xp |
| spellInfoCount | u16 |
| spells × count | RegistryId + enum `UnlockState` + i32 unlockTimestamp |

### 7.2 `StatCollection` and stat-mod layouts

`StatCollection.deserialize` (`StatCollection.java:50-64`): i32 count × {RegistryId statTypeId, `statType.deserialize(stat, data, offset)`}. Stat types are registry entries (`StatType` subclasses; retail: `VitalsStatType`, `UnaryStatType`):

- **VitalsStat** (`VitalsStatType.java:16-20`): i32 current, i32 maxRaw, then mods.
- **UnaryStat** (`UnaryStatType.java:16-19`): i32 raw, then mods.
- **Mods** (`StatType.deserializeMods:20-31`): i32 modCount × {RegistryId modId, RegistryId modTypeId, `statModType.deserialize(...)`}. Retail mod type `UnaryStatModType` (byte: i32 amount, 1-byte percentile bool, i32 turns — `contributesToOtherMods` is hardcoded `false` on the byte path, preserved only via the tag path).

**V2 character load semantics** (`SeveredSavedCharacterV2.make`, `SeveredSavedCharacterV2.java:52-122`): **no level replay** — `stats` is copied wholesale (`character.stats.set(this.stats)`), `level`/`dlevel` set directly, `hasTransformed` explicit, additions/spells restored via explicit `UnlockState` + `unlockTimestamp` (`setUnlockState`). This is the mod-safe model: unlock conditions are never re-evaluated, so mod-added characters/additions/spells with custom unlock logic survive load exactly as saved.

---

## 8. V10 — the tagged format (current)

`V10Serializer.fromV10` (`V10Serializer.java:44-158`) / `toV10` (`:160-297`). The file is **one top-level `MapTag`** (self-describing, extensible) followed by the SAVE config tail — no magic-checked fixed offsets beyond the 4-byte `DSaa`.

### 8.1 Top-level tag keys (write side = `toV10`; read side = `fromV10`)

| Key | Tag | Content |
|---|---|---|
| `saveName` | String | display name |
| `campaignTypeId` | RegistryId | |
| `locationName` | String | `engineState.getLocation(gameState)` |
| `atlas` | Map | `width` Int (=512), `height` Int (=512), `data` Raw = **PNG-compressed** 512×512 portrait atlas (`TexturePacker.packToBytes(512,512)` → `PngWriter.compress`) |
| `scriptData` | List(32 × Int) | |
| `activeParty` | List(Int) | |
| `gold`, `chapterIndex`, `stardust`, `timestamp` | Int | |
| `_b0`, `battleCount`, `turnCount` | Int | |
| `scriptFlags2` (32), `scriptFlags1` (8), `wmapFlags` (8), `visitedLocations` (8) | List(Int) | raw ints per flag word |
| `_1a4` (8), `chestFlags` (8) | List(Int) | |
| `equipment` | List(Map) | entries: `equipmentId` RegistryId |
| `items` | List(Map) | entries: `itemId` RegistryId, `size` Int, `durability` Int, `extraData` Tag (any tag, optional) — **V10 is the first format that stores item extraData** (V5–V9 skipped it) |
| `goods` | List(Map) | entries: `goodId` RegistryId |
| `characters` | List(Map) | entries: `templateId` RegistryId + `template.serialize(character, characterTag)` (the tag character model, §8.2) + `portraitX/Y/W/H` Int (from the packer's rect) |
| `engineStateId` | RegistryId | `engineState.type` |
| `engineStateData` | Tag | `engineState.writeSaveData(gameState)` — the per-state Tag directly (no raw blob, no migrator — migrator is a V8/V9 concern) |
| `modData` | List(Map) | entries: `id` RegistryId, `data` Tag — filled by the `WriteSaveDataEvent` (§12.1) |
| *(file tail)* | — | `ConfigStorage.saveConfig(CONFIG, SAVE, …)` (§M7-02) |

Read side: `tag.get(...)` lookups by key (order-independent); `engineStateData` is **not** run through `SaveMigrator` (a V10 writer produces already-migrated tags); `modData` → `ReadSaveDataEvent` at load.

### 8.2 V10 character tag model (retail template, `RetailCharacterTemplate.serialize` `:145-203` / `deserialize(MapTag)` `:205-252`)

Map keys: `flags`, `xp`, `dxp`, `level`, `dlevel` (Int), `hasTransformed` (Bool), `status` (Int); `stats` (List of Map: `statTypeId` RegistryId + type-specific keys — vitals: `current`/`max` Int + `mods`; unary: `val` Int + `mods`; mod entries: `modId`/`typeId` RegistryId + type-specific); `equipment` (List of Map: `slot` **EnumTag** (EquipmentSlot), `equipmentId` RegistryId); `selectedAdditionId` (RegistryId, **omitted when null**); `additions` (List of Map: `additionId`, `unlockState` EnumTag, `unlockTimestamp` Int, `level` Int, `xp` Int); `spells` (List of Map: `spellId`, `unlockState` EnumTag, `unlockTimestamp` Int).

### 8.3 Writer-side notes

- `toV10` is the only live writer (`GameEngine.SAVES`, §3). It builds the portrait atlas from each character template's `loadPortrait()`, packs at 512×512, PNG-compresses — the atlas in a V10 file is **lossy-compressed** (V8: raw retail atlas; V9: raw file atlas).
- `tag.serialize(data, offset)` + config tail (map order nondeterministic, §2.3).
- Items with `extraData != null` store it — item extraData is a free Tag (e.g. quest-item state).

---

## 9. Character model comparison (across versions)

| | Retail/V1 | V2–V7 | V8 | V9+ | V10 |
|---|---|---|---|---|---|
| encoding | 0x2C packed record ×9 | streaming i32 record ×9 | inline V1 record (retail slot bound) | template `deserialize` (bytes) | template `serialize` (tags) |
| character identity | fixed slot 0..8 | fixed slot 0..8 | `CHAR_IDS[slot]` | **RegistryId** (moddable) | **RegistryId** |
| stats | hp/mp/sp u16 + level replay | i32 + level replay | V1 model + level replay | **full `StatCollection`** | full `StatCollection` (tags) |
| transform | scriptFlags2 bits | scriptFlags2 bits | scriptFlags2 bits (derived) | explicit bool | explicit bool |
| additions | 8 × positional (level u8, xp u8) | 8 positional (V<7) / per-entry (V7+) | per-entry {id, level, xp} | per-entry **+ UnlockState + timestamp** | per-entry **+ UnlockState + timestamp** (tags) |
| spells | none (replay) | none (replay) | none (replay) | per-entry **UnlockState + timestamp** | per-entry (tags) |
| load strategy | replay level-ups; conditional unlock re-derivation | same | same (V1 model) | **copy stats; explicit unlock state** | same (V2 model) |
| portrait | — (retail card art) | — | 48×48 at slot×48 on 512×64 retail atlas | rect ×4 i16, raw atlas in file | rect ×4 Int, **PNG** atlas in file |

---

## 10. Engine-state Tag flow

`EngineState.writeSaveData(GameState52c) → Tag` / `readSaveData(GameState52c, Tag)` are abstract on `EngineState` (`EngineState.java:48-49`). Implementors (7): **WMap, SMap** (real data) and **Battle, Ttle, GameOver, FinalFmv, Credits** (all `writeSaveData → null`, `readSaveData → no-op` — these states are *not savable*: the save menu doesn't appear in them, and a save written under them carries `engineStateData = null`).

### 10.1 WMap (`WMap.java:435, 439-462`)

- `init()` sets `EngineStates.lastSavableEngineState = this.type` (`WMap.java:435`).
- write: `MapTag { pathIndex Int, dotIndex Int, dotOffset Float, facing Int, directionalPathIndex Int }` (from `GameState52c` fields 0x4D8..0x4DE).
- read: **returns early if `pathIndex` absent** (legacy empty data); else copies all five into the state.

### 10.2 SMap (`SMap.java:434-451, 464-525`)

- `init()` also sets `lastSavableEngineState` (`SMap.java:451`).
- write: first syncs `gameState.submapScene_a4 = collidedPrimitiveIndex_80052c38` and `submapCut_a8 = submapCut_80052c30` (the *live* engine globals), then `MapTag { scene Int, cut Int, indicatorsDisabled Bool, primaryPartyBackup List(Int), + pathIndex/dotIndex/dotOffset/facing/directionalPathIndex }` — the world-map path data is duplicated into submap saves *"for when we're sailing the Queen Fury, go inside, and save"* (in-source comment, `SMap.java:478-484`). `primaryPartyBackup` is the SMap's party-swap backup list (`SMap.java:434`).
- read: sets `unstuckMovementBudget = UNSTUCK_SAVE_LOADED_BUDGET` (**60.0f** — an anti-stuck movement grace period granted on every load, `SMap.java:436,491`); if `scene` absent (legacy) → falls back to the state's own submap fields (no data was saved); else restores scene/cut/indicatorsDisabled into state *and* the engine globals (`submapScene_80052c34`, `submapCut_80052c30`, `collidedPrimitiveIndex_80052c38`), then `primaryPartyBackup` and the wmap path fields if present.

### 10.3 Legacy blob migration (`SaveMigrator.upgradeEngineStateData`, `SaveMigrator.java:23-24, 26-85`)

V8/V9 store the engine-state data as a **raw length-prefixed byte blob** with a 2-byte sub-version; `SaveMigrator` upgrades the two known retail shapes to the modern Tag map (fallback scene = 4, cut = 675):

- **WMAP v1** (version u16 `0x3156` = `'V' | '1'<<8`): u16 pathIndex, u16 dotIndex, **u8 dotOffset** (read as byte into a FloatTag), i8 facing, u16 directionalPathIndex. Empty blob → empty map ("legacy saves").
- **SMAP** (version u16; v2 = `0x3256`): i32 scene, i32 cut, bool indicatorsDisabled; **v2 only**: i32 count + × i32 primaryPartyBackup. Empty blob → `{scene: fallback, cut: fallback}`.
- Unknown version → logged warning, empty map. States other than WMAP/SMAP pass the raw blob through untouched (V10) — so mod engine states can round-trip arbitrary bytes.

### 10.4 How the load path consumes the Tag

`SaveManager.loadGameState(save, fullBoot)` (`SaveManager.java:406-431`):

```
CONFIG.clearConfig(SAVE); CONFIG.copyConfigFrom(save.config)     // per-save config wins (M7-02)
if fullBoot: bootRegistries(); InputBindings.initBindings(); InputBindings.loadBindings(CONFIG)
gameState = save.createGameState()                                // §4.4 / §6.2
if currentEngineState.is(save.engineState):
    currentEngineState.readSaveData(gameState, save.engineStateData)   // apply in place (e.g. submap→submap)
else:
    EngineStates.engineStateData = save.engineStateData            // deferred: consumed when that state initializes
if save.modData != null: EVENTS.postEvent(new ReadSaveDataEvent(save.modData))
campaignType = REGISTRIES.campaignTypes.getEntry(save.campaignType); campaignType.setUpLoadedGame(gameState)
```

then `loadGameState(state)` (`SaveManager.java:433-440`): posts `GameLoadedEvent(state)` — **mods may replace the entire state** (`event.gameState` is what gets installed) — installs `gameState_800babc8`, `EquipmentTypes.loadEquipmentTypes()`, `loadingNewGameState_800bdc34 = true`, `whichMenu_800bdc38 = UNLOAD`.

`CampaignType` (`CampaignType.java:11-16`) is a registry entry (registry id `lod_core:campaign_type`, `CampaignTypeRegistry.java:11`) with four hooks: `setUpNewCampaign`, `transitionToNewCampaign`, `setUpLoadedGame`, `transitionToLoadedGame`. The retail one (`RetailCampaignType`): new campaign = 9 characters created (Dart + the 8 joinable), starting levels Lavitz 3 / Shana 4 / Rose 8 / Haschel 13 / Albert 15 / Meru 17 / Kongol 19 / Miranda 23 (level-up replay with `xp = getXpToNextLevel()` after the final level — the "XP bar full" starting state), Dart as sole active party member (`charIds_88 = [0]`, flags `IN_PARTY|CAN_BE_IN_PARTY|CANT_REMOVE`), starting items (Burn Out + 2× Healing Potion), full starting equipment loadout per character, gold 20; `transitionToNewCampaign` = `Fmv.playCurrentFmv(2, SUBMAP)` (the prologue FMV → beach). Both load hooks are no-ops.

---

## 11. Campaigns and memcards on disk

### 11.1 Campaign directory model

- `saves/<slugName(campaignName)>/` — created on demand (`Campaign.create` → `manager.resolve(IoHelper.slugName(name))`, `Campaign.java:32-34, 50-58`); a campaign's *name* is stored in `campaign_config.dcnf` under `campaign_name` and read back on load (fallback: directory name, `Campaign.java:36-48`).
- Contents: `*.dsav` save files + `campaign_config.dcnf` (CAMPAIGN-location config: campaign name, enabled/known mods, campaign-level settings — M7-02).
- **`Campaign.load`** constructs `latestSave` = `loadFirstValidSave()` (`Campaign.java:82-100`): children matching `*.dsav` sorted by **mtime descending** (`IoHelper.childrenSortedByDate`, `IoHelper.java:60-64`), first one that deserializes; none → `InvalidSavedGame` placeholder. `loadAllSaves()` (`:108-126`) loads everything **asynchronously** (`CompletableFuture.completeAsync` — common pool) for the save-menu UI.
- Campaign listing order: directories (containing ≥1 `.dsav`), sorted by their **latest save's mtime, descending** (`SaveManager.getCampaignPaths`, `SaveManager.java:306-351`).
- Save naming: `generateSaveName` (`SaveManager.java:119-149`) = `<location> N` — location from the *current context*: world map → ROM `worldMapNames_8011c1ec[continentIndex_800bf0b0]`; while the save menu is up (`WhichMenu.RENDER_SAVE_GAME_MENU_19`) → ROM `chapterNames_80114248[chapterIndex]`; else ROM `submapNames_8011c108[submapId_800bd808]`; N = (highest existing save whose name matches `^(.+?)\s(\d+)$` with the same location prefix) + 1. Campaign names: `New Campaign`, `New Campaign 2`, … (`generateCampaignName`, `SaveManager.java:96-106`).
- Filesystem hygiene: save file names are slugified on write (`newSave` → `IoHelper.slugName(saveName)`), but `deleteSave` resolves `saveName + ".dsav"` **without slugifying** (`Campaign.java:70-76` — source-verified inconsistency; safe only because filenames were slugified at creation).

### 11.2 Pre-campaign artifacts and legacy adoption

Two kinds of legacy on-disk data exist, both discovered on the title screen (`Ttle.java:300-301`):

1. **Uncategorized saves** — `*.dsav` files *directly* under `saves/` (saved before the campaign model existed): `findUncategorizedSaves()` (`SaveManager.java:157-165`). Title-screen flow (`Ttle.java:527-546`): prompt for a campaign name → `moveCategorizedSaves(saves, name)` — a plain file move into the new campaign directory (contents untouched, `SaveManager.java:215-226`).
2. **PSX memcards** — `findMemcards()` (`SaveManager.java:169-180`) scans `saves/` for **memcard image files**: exactly **0x20000 (128 KiB)** bytes, u16@0 = `0x434D` ("MC"), and ≥1 of 16 directory entries (at offsets `(i+1) × 0x80`, i = 0..15) with type byte **0x51 ('Q')** and a 16-char product code at entry+0xA matching one of the seven retail LoD codes (`SaveManager.REGIONS`, `SaveManager.java:69-77`): `BASCUS-94491drgn` (US), `BESCESP03044drgn` (FR), `BISCPSP10119drgn` (JP), `BESCESP03045drgn` (DE), `BESCESP03046drgn` (IT), `BESCESP03043drgn` (EU), `BESCESP03047drgn` (SP).

   **Memcard file layout (source-verified as the reference's import contract):**

   | Off | Size | Field |
   |---|---|---|
   | 0x0000 | 2 | `0x434D` "MC" magic |
   | 0x0080 + i×0x80 (i = 0..15) | 0x80 | directory entry: [0] type (0x51 = save slot), [+0xA..+0x1A) 16-char product code (US-ASCII) |
   | (i−1)×0x2000 (i = 1..15) | 0x720 | slot data = one retail save image (RETAIL magic at slot start; save ends at 0x6E4, 0x54 padding) |

   Fifteen usable save slots (i = 1..15) — the retail LoD memcard select screen layout. **Layering note:** the 'Q' entry type and product-code fields match PSX memory-card directory conventions, but this 128 KiB image is a 16-screen *subset* file (a raw PSX MCr is 1 MiB); whether this exact file shape is a raw tool dump or a repack is `Unresolved` (no memcard images available) — registered as Q-80.

   **Memcard → campaign conversion** (`SaveManager.splitMemcards`, `SaveManager.java:228-288`; user-confirmed on the title screen, `Ttle.java:548-576`):
   - `bootMods(Set.of())` + `bootRegistries()` — a **registry re-boot with no mods** (retail-ids-only conversion context);
   - `splitMemcard` (`SaveManager.java:290-304`): for slots 1..15 with type 0x51 + matching product code → `loadData(campaign, "", memcard.slice(i*0x2000, 0x720))` (the RETAIL deserializer) → `MemcardSavedGame`;
   - all extracted saves are **sorted by in-save `timestamp`** and re-written as *current-format* saves named `<locationName> <per-location counter>` (e.g. "Laguna 1", "Laguna 2") with **mtimes backdated** (`now − (n−i) seconds`) to preserve their relative ordering in the UI;
   - per-save engine-state context is reconstructed for the write: `locationType == 1` → world map (`continentIndex = locationIndex`, WORLD_MAP); `locationType == 3` → chapter/save-menu context (`whichMenu = RENDER_SAVE_GAME_MENU_19`, submap scene/cut from the save, SUBMAP); else → submap (`submapId = locationIndex`, scene/cut from the save, SUBMAP);
   - campaign config gets `campaign_name` and is written to `campaign_config.dcnf`; the original memcard file is deleted only if the user confirmed; finally `bootMods(all)` re-runs on the title-screen caller side.

---

## 12. Save/load lifecycle

### 12.1 Save (write) path

| Call site | Context |
|---|---|
| `NewCampaignScreen.render` (`NewCampaignScreen.java:231-253`) | new campaign: fresh registries → `Campaign.create` → `campaignType.setUpNewCampaign(state)` → `NewGameEvent` (mods may mutate) → `SAVES.loadGameState(state)` → campaign config written (`campaign_config.dcnf` incl. `enabled_mods`/`known_mods`) → **initial save** named `chapterNames[0]` under a fresh SUBMAP state |
| `SMap.createNewSave` (`SMap.java:576-584`) | in-submap quick save; gated by `canCreateNewSave()` (`:567-574`): submap stage `RENDER_SUBMAP_12`, no menu open, indicators not disabled, no transition, no full-screen effect, `canSave()`; auto-generated name |
| `SaveGameScreen` (`SaveGameScreen.java:165,177`) | the save UI: user-named **new** save (`newSave`) or **overwrite** of an existing slot (`overwriteSave(fileName, saveName, ...)`) |
| `Main.main` catch (`Main.java:66-92`) | **crash recovery** (§12.3) |

All writes go through `SaveManager.overwriteSave` (`SaveManager.java:353-370`): `ExpandableFileData` → write u32 magic (`serializerVersion.code`) → `serializer.serialize(saveName, data, offset, campaignType, engineState, gameState)` (for V10: the Tag tree + `WriteSaveDataEvent`-collected `modData` + SAVE config tail, §8) → `Files.write` (CREATE/TRUNCATE/WRITE) to `<campaign>/<slug>.dsav`.

**Mod save-data contract** (`WriteSaveDataEvent`/`ReadSaveDataEvent`, `saves/`): on write, mods receive a `ListTag` and call `add(RegistryId, Tag)` — entries are `{id: RegistryIdTag, data: Tag}`; the javadoc contract: the registry id **must use the mod's own mod id** or the mod won't get it back. On load, `ReadSaveDataEvent.get(RegistryId)` returns the first matching entry's `data`. This is the only mod-persistence channel in the save file (V10).

### 12.2 Load path

`Campaign.loadGame(filename)` → read file → `SaveManager.loadData(campaign, filename, data)` (version dispatch, §3) → `SavedGame`. Then `loadGameState(save[, fullBoot])` per §10.4. Ordering guarantees that matter for the rewrite:

1. **Config before registries** (full boot): the per-save config is installed into `CONFIG` *before* `bootRegistries()` and input-binding reload — mods reading config during (re-)boot see the loaded save's values.
2. **State construction, then state-specific data**: `createGameState()` runs before `readSaveData` — per-state tags can assume the campaign state already exists.
3. **In-place vs deferred state data**: same-state reload applies the Tag immediately; cross-state load stashes it in `EngineStates.engineStateData` for the destination state to consume at init.
4. **Mod data after state, before campaign hooks**: `ReadSaveDataEvent` → `campaignType.setUpLoadedGame`.
5. **`GameLoadedEvent` can replace the whole state** — the final arbiter before `gameState_800babc8` is installed.
6. Load ends with `whichMenu = UNLOAD` + `loadingNewGameState_800bdc34 = true` (the overlay/scene load machinery then brings up the deferred `engineStateOnceLoaded_8004dd24` transition, M1 `platform/04`).

### 12.3 Crash-recovery saves

`Main.main` wraps `GameEngine.start()` in a try/catch (`Main.java:62-135`). On any `Throwable`, if a game state is live (`gameState_800babc8 != null`) **and** the `create_crash_save` config entry (a CoreMod `CreateCrashSaveConfigEntry`, default `true` — entry registered at `CoreMod.java:132`) is enabled:

1. `name = generateSaveName(existingSaves, "Crash Recovery")` → "Crash Recovery N";
2. sync submap scene/cut from the live globals into the state;
3. **if `lastSavableEngineState != null`** (set by WMap/SMap `init()`, §10), swap `currentEngineState` for a fresh instance of that type — so a crash in a menu/FMV/battle still serializes under the last WMAP/SMAP context (with real state data); (`Main.java:76-82`)
4. `SAVES.newSave(name, RETAIL_CAMPAIGN_TYPE, currentEngineState, gameState)` — the recovery save lands in the *current* campaign.

Any failure during recovery is logged and swallowed (the crash report still prints: version/hash, loaded+failed mods, the "attempts to generate a recovery save" note, full stack trace; `debug.log` is copied to `CRASHLOG-<timestamp>.log`; `System.exit(1)`).

**Failure inventory** (all observed in source): unknown magic → `InvalidSaveException` → `InvalidSavedGame` (listed, un-loadable); mid-deserialize `Throwable` → same; `InvalidSavedGame.createGameState()` throws `InvalidSaveException("Attempted to load invalid save …")`; unknown registry ids at load time are **skipped with a warning** (items/equipment/goods — the save survives with holes); unknown stat/mod/addition/spell registry ids throw (no skip — `REGISTRIES.…getEntry(id).get()` is unconditional in the character paths) — so a save referencing a *removed mod's* characters fails the whole load, while one referencing removed items degrades. No retry/corruption-recovery beyond "don't crash the menu" exists.

---

## 13. Compatibility and migration summary

- **Loadable without modification**: all 11 magics (RETAIL, V1–V10) — each deserializes directly to the current model. Retail compatibility is `Verified` from the `RetailSerializer` source (§4).
- **No lossless round-trip for old formats**: loading a V2–V9 save and re-saving writes **V10** — the re-save is canonical and *adds* capability (extraData, tag extensibility, mod data), but anything the old format couldn't express is what it is; V2's unknown-4-byte `_04`, the `_4b8`/mono/vibration/morph bytes, `charDataCount`, `characterInitialized`, and the V5–V9 item extraData blobs are dropped by design (in-source comments).
- **Mod forward compatibility**: V4+ ids, V6+ goods, V7+ additions, V9+ characters/stats are registry-id-based — a save survives mod *additions*; it degrades (warn + skip) on removed items/equipment/goods and fails on removed characters/stat types (§12.3). The `lod-core`→`lod_core` alias is the one in-format id migration (read side only).
- **Migration chain**: `SaveMigrator` covers exactly the two legacy engine-state blob shapes (WMAP v1 / SMAP v1–v2) inside V8/V9 files; V10 stores the migrated form. There is no V_n→V_{n+1} file converter — the chain is "each old version understands itself".

---

## 14. Layering and classification (D-table)

| # | Behavior | Layer | Class | Notes |
|---|---|---|---|---|
| D1 | RETAIL save layout (0x6E4 file, struct dump, positional characters, u8 retail id tables, goods bitmask, transform bits in scriptFlags2) | 3 | Format-faithful (source-verified from the reference's retail parser; retail bytes themselves asset-validated-pending) | the compatibility floor for the Rust rewrite |
| D2 | V1 layout (0x518, struct @ 0x30, name from filename) | 4 | Reference format | first reimpl save |
| D3 | V2–V7 streaming line (feature matrix §5.3, per-save config tail) | 4 | Reference format | backward-compat readers |
| D4 | V8/V9 streaming + engine-state raw blob + `SaveMigrator` | 4 | Reference format | |
| D5 | V10 tagged format (self-describing MapTag, PNG atlas, `modData`, SAVE config tail) | 4 | Reference format — **the current writer** | the Rust implementation's natural floor |
| D6 | Campaigns (`saves/<dir>/`, `*.dsav`, `campaign_config.dcnf`, naming rules, mtime ordering) | 4 | Reference behavior | replaces the PSX memcard slot model |
| D7 | Memcard import (0x20000 image, 16×0x80 directory, 15×0x720 slots, product-code table, timestamp-ordered re-serialization) | 3 data / 4 container | Reference handling of original content | Q-80 (file-shape vs raw MCr) |
| D8 | Savable states = WMAP + SMAP only; per-state Tag shapes (§10) | 3 (states/fields) + 4 (Tag encoding) | decompiled-contract fields, layer-4 tagging | `lastSavableEngineState` mechanism is layer 4 |
| D9 | Crash-recovery saves ("Crash Recovery N", last-savable-state swap, config-gated) | 4 | Reference enhancement | no original analog |
| D10 | Character load semantics: level-up replay + conditional unlock re-derivation (V1 model) vs stats-copy + explicit unlock state (V2 model) | 4 | Reference behavior (V1 mirrors the retail *effect*: stats recompute from templates) | the two strategies are both load-time, neither is a format field per se |
| D11 | No checksums; magic + parse success = validity; warn+skip vs fail policy split (§12.3) | 4 | Reference behavior | |
| D12 | MapTag nondeterministic ordering; PNG (lossy) atlas in V10 | 4 | Reference behavior | byte-stability not expected |

---

## 15. Register and cross-document impact

- **Q-80 (new)** — memcard image file shape: is the reference's 0x20000 "MC" file a raw PSX MCr screen-subset dump or a tool repack? The 0x51 entry type and 16-char product code match PSX memory-card conventions, but no memcard image exists to confirm against, and raw-MCr layout (1 MiB, directory geometry) cannot be cross-checked offline. — `Open` — M10 + assets — needs: one real PSX memcard dump (asset-validated-pending).
- Carried into **M7-02** (`research/saves/02-config.md`): the `.dcnf` binary format (i32 count + {RegistryId, i32 len, bytes} — fully specified here for the save tail, `ConfigStorage` shared), the three storage locations (SAVE/CAMPAIGN/GLOBAL) and their files (save tail, `campaign_config.dcnf`, `config.dcnf`), the preset `.dpre` format, and the config entry registry (`lod_core:config`) — all read in M7-02 with their own checkpoints.
- Carried to **M8** (modding): the config-entry registration event (`ConfigRegistryEvent`), `ConfigDefaultPresetsEvent`, `NewGameEvent`, `GameLoadedEvent`, `ReadSaveDataEvent`/`WriteSaveDataEvent` contracts as mod-facing APIs (the registry machinery itself is M8's subject); CoreMod's 80 config entries, LodConfig's 8, TurnOrderConfigs' 2.
- Cross-doc consistency check: M1 `platform/04-engine-states.md` documents the engine-state machine; this document's savable-state set (WMap/SMap via `lastSavableEngineState`) is consistent with it. M2 asset docs do not cover saves. No earlier document claimed save-format facts requiring correction.
- **No `asset-validated` claims** in this document; retail/PSX byte-level facts are source-verified from the reference's own parsers with the retail-bytes confirmation recorded as asset-validated-pending.
