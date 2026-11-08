# M8-03 — Core Mods, Debugger, Script-Patch Modding, and i18n

**Milestone:** M8 (Mods and Modding), deliverable 3 of 3. **Date:** 2026-11-07. **Approved starting point:** `fc0881d` (M8-01 `4606af5`, M8-02 `ca2ee9a`).

**Sources.** Severed-Chains `0c08c67`: `game/modding/coremod/**` (CoreMod, CorePostBattleActions, CoreEngineStateTypes, config/LanguageConfigEntry), `lodmod/**` (all 21 registrar files), `turnorder/**`, `game/debugger/**` (17 files), `core/renderer/RenderEngine.java`, `game/scripting/ScriptManager.java`, `game/scripting/ScriptState.java`, `game/scripting/GameVarParam.java`, `game/scripting/ScriptStorageParam.java`, `game/Scus94491BpeSegment.java`, `game/unpacker/Unpacker.java`, `game/unpacker/scripts/ScriptPatcher.java`, `ScriptPatchList.java`, `ScriptPatch.java`, `PatchType.java`, `patches/` (scripts.csv, scripts/**, libs/**, replacements/**, meta/**), `lang/**`, `core/lang/I18nText.java`, `game/i18n/I18n.java`, `core/GameEngine.java`. Mod-Loader `3433e6f`: `i18n/LangManager.java`, `LocaleSpecificity.java`, `ModContainer.java`. Script-Recompiler `3433e6f` (via in-repo use): `Patcher`, `Translator`, `MetaManager`. M3 (`research/scripting/03-toolchain.md`, `04-meta-system.md`, `06-native-script-functions.md`), M5-03 (Queen Fury overlay), M7-02 (config entry census).

**Validation.** All findings `source-verified` unless noted. No `asset-validated` claims. Continues J/D numbering from M8-01/M8-02 (J1–J15, D1–D19).

---

## 1. Core-mod content inventory (plan RQ4)

### 1.1 The verified entry census

Per-registrar counts (each `REGISTRAR.register(...)` call = one entry; the 14 extra `.register(` hits in `LodMod.java` are handler delegations, not entries):

| Registry | lod_core | lod | turn_order | Total | Notes (examples) |
|---|---|---|---|---|---|
| `config` | 80 | 7 | 1 | **88** | M7-02 census (e.g. `lod_core:language`, `lod:max_level`, `turn_order:show_turn_order`) |
| `inputActions` | 40 | 29 | — | 69 | core: menu navigation, `general_*` (fullscreen/speed/turbo), freecam, debug controls; lod: gameplay actions |
| `items` | 1 | 54 | — | 55 | `lod_core:nothing` (identity item) + retail items |
| `campaignTypes` | — | 1 | — | 1 | `lod:retail` |
| `engineStateTypes` | 2 | 5 | — | 7 | core: `title`, `credits`; lod: `submap`, `world_map`, `battle`, `final_fmv`, `game_over` |
| `characterTemplates` | — | 9 | — | 9 | one per retail character |
| `statTypes` | — | 17 | — | 17 | |
| `statModTypes` | — | 2 | — | 2 | |
| `elements` | — | 9 | — | 9 | |
| `elementIcons` | — | 10 | — | 10 | e.g. `earth`, `dark`, `divine`, `thunder`, `light`, `wind`, `fire` (PNG-backed) |
| `battleEntityTypes` | — | 2 | — | 2 | `player`, `monster` |
| `levelUpActions` | — | 4 | — | 4 | `level_up`, `dragoon_level_up`, `unlock_addition`, `unlock_spell` |
| `postBattleActions` | 3 | 3 | — | 6 | core: `victory`, `game_over`, `play_fmv` (generic machinery); lod: the retail-specific actions |
| `equipment` | — | 148 | — | 148 | |
| `goods` | — | 37 | — | 37 | |
| `shops` | — | 41 | — | 41 | e.g. the Queen Fury equipment/item shops (M5-03) |
| `spells` | — | 131 | — | 131 | |
| `deff` | — | 44 | — | 44 | |
| `additions` | — | 35 | — | 35 | |
| `encounters` | — | 512 | — | 512 | the largest category by far |
| `battleActions` | — | 10 | — | 10 | |
| **Total** | **126** | **1110** | **1** | **1237** | |

Source: the 27 registrar files censused in M8-02 §5 (per-file counts: `CoreMod.java` 80+1+40, `CorePostBattleActions.java` 3, `CoreEngineStateTypes.java` 2; `LodMod.java` 1+29+17+2+9+10+2, `LodEngineStateTypes.java` 5, `LodEquipment.java` 148, `LodEncounters.java` 512, `LodCharacterTemplates.java` 9, `LodLevelUpActions.java` 4, `LodPostBattleActions.java` 3, `LodShops.java` 41, `LodGoods.java` 37, `LodSpells.java` 131, `LodDeffs.java` 44, `LodConfig.java` 7, `LodAdditions.java` 35, `LodBattleActions.java` 10, `LodItems.java` 54; `TurnOrderConfigs.java` 1).

### 1.2 The engine/content boundary (analysis)

What the census shows about where "engine" ends and "content" begins:

1. **The split is mechanism vs. configured behavior, not code vs. data.** The game class (non-mod) contains all *mechanism* — the state machine, render/audio/input, the script VM, the save system — while **all 1237 registered entries, including things that are behaviorally part of the "engine", live in mods**: `lod_core` registers the title/credits engine states, the victory/game-over/FMV post-battle actions, and 40 of the 69 input actions. A mod can in principle replace any of them (they are ordinary registry entries resolved through delegates), even though the reference ships them as core content.
2. **`lod_core` is the UI/system baseline** (126 entries): 80 config entries (the entire user-facing option set), 40 engine input actions (menu navigation, fullscreen/speed/turbo, freecam, the six `debug_*` actions), the `nothing` identity item, the two non-gameplay engine states, and the three generic post-battle actions.
3. **`lod` is all LoD gameplay content** (1110 entries) — every character, stat, element, item, spell, shop, encounter, and the five gameplay engine states. Content scale is dominated by encounters (512), equipment (148), and spells (131).
4. **`turn_order` is the shape of a pure behavioral mod**: zero content entries beyond one config entry (`show_turn_order`) — its substance is Java code (the turn-order UI, M8-02 §4.3's five event handlers).
5. **Implication for the Rust project (analysis only, no design decision):** the mod boundary already separates *every piece of retail configuration* from the mechanism; a Rust reimplementation that hardcodes retail content inside the engine would change the observable mod surface (a mod that overrides e.g. `lod:retail` campaign type or a post-battle action relies on those being swappable entries, and the save format stores their `RegistryId`s — M7-01). The 1237-entry census is the scale of the content surface any content-loading mechanism must support.

---

## 2. The debugger (modding-adjacent tool)

### 2.1 Architecture

The debugger is an in-process **JavaFX developer tool** (22 files under `game/debugger/`: the `Debugger` application + controller and FXML/CSS, nine subsystem panel pairs — CharacterEditor, CombatDebugger, GameStateEditor, GameStateViewer, RegistrySelector, ScriptDebugger, ScriptLiveDebugger, SmapDebugger, SmapModelDebugger — plus `RegistryEntryConverter`). It is not a mod (no `@Mod` annotation, no jar) and has no registry entries of its own; it is the reference's built-in developer surface.

- **Launch:** the input action `debug_open_debugger` (a `lod_core` input action, `CoreMod.java:234`, user-rebindable like any other) is handled in the render engine's key processing (`RenderEngine.java:1404-1412`): first press starts the JavaFX toolkit on a **dedicated thread** (`Platform.setImplicitExit(false)` + `new Thread(() -> Application.launch(Debugger.class))`); later presses just bring the window forward (`Platform.runLater(Debugger::show)`). Failure is logged ("Failed to start debugger") and non-fatal.
- **Coupling:** same JVM — the FXML controllers read and write live game state directly (e.g. the GameState editor binds to the script-data array, the character editor to live character structs).
- **Event participation:** the four controller classes are the in-repo *instance-registered* event consumers censused in M8-02 §4.3 (6 weak handlers: config load/updated, script lifecycle ×2, render ×3) — the debugger is the primary real-world user of the weak-instance listener mechanism (J11).
- **Attributed mutation:** the debugger applies its changes **through the same event paths as gameplay**, tagged with dedicated sources — `GoodsSource.DEBUGGER` (see `GiveGoodsEvent` javadoc) and `LevelUpSource.DEBUGGER` (see the level-up event javadocs) — so debugger edits flow through the normal take/give/level-up events and their handlers.
- **Live script stepping:** `ScriptLiveDebuggerController` embeds a Script-Recompiler `Translator` and steps a running script state token by token (the `ScriptLifecycleEvent` hook, M8-02 §4.2, is what feeds it).

### 2.2 Contract classification

The debugger itself is layer-4 tooling (JavaFX, a Java-specific UI kit — a Rust reimplementation would not preserve it). What belongs to the modding contract: (a) the `debug_*` input actions exist and are registered by core (rebindable, M8-02 §1.3); (b) the weak-instance event registration pattern must work for long-lived-but-transient objects; (c) the `*Source.DEBUGGER` enums are part of the inventory/level-up event payloads — mods observing those events see debugger-originated mutations as a distinct source.

---

## 3. Script-patch modding (plan RQ5)

### 3.1 The `./patches/` layout

```
patches/
  scripts.csv          353 entries: 2 replacement + 351 diff (no header row)
  scripts/             replacement sources + diff trees
    player_combat_script.txt, throw_item.txt        (the 2 replacements, assembly language)
    DRGN0/<id>/<variant>.diff ...                   (351 diffs; DRGN0 keyed by SECT/DRGN0.BIN/<id>/<variant>)
    DRGN1/20.diff, ...  DRGN21..24/...              (DRGN1 flat; DRGN21–24 nested)
    *.config.csv        (24 patch-config files, named <diff>.config.csv)
  libs/                shared assembly snippets + Fate sources
    char_utils.fate / char_utils.txt   (paired: "// If you make any changes to this file,
                                       it has to be manually recompiled to char_utils.txt")
    queen_fury.fate, lookup_tables.txt, shadow.txt,
    throw_item_effects.txt, widescreen.txt, widescreen.full.txt
  replacements/        file-level asset replacements (chester.tim)
  meta/                runtime meta CSVs (descriptions.csv, enums.csv, params.csv,
                       legend.game.combat.bent.BattleEntityStat.csv,
                       legend.game.combat.types.BattleSoundUsage.csv)
```

`scripts.csv` rows are `<type>,<key>,<path>[,description]` with `type ∈ {replacement, diff}` (`PatchType.valueOf(uppercase)`, `ScriptPatchList.java:33-41`); a legacy two-column form `<scriptPath>,<diffPath>` is still accepted and treated as a diff (M3-03 §8.1). The key is the script's identity: the section path within the DRGN bank for diffs (e.g. `SECT/DRGN1.BIN/20`) or the script name for replacements (e.g. `player_combat_script`). The 351 in-repo diffs are overwhelmingly retail-behavior fixes (hardcoded character counts, finisher crashes, registry-ification of steal/item tables) plus the widescreen overlay diffs (M5-03/04).

### 3.2 Patch semantics (unpack-time, `ScriptPatcher`)

Patching happens **at unpack time** (M2 pipeline), never at runtime; the game loads already-patched scripts from the unpacked file tree. Per patch (`ScriptPatcher.java:196-231`):

1. **Backup:** the original file is copied to the backups dir once (replacements of non-existent files need no backup — a replacement can *create* a script the game did not ship).
2. **DIFF:** decompile the **backup** bytes (`SCRIPTS.disassemble` with extra branch/table-length corrections from the sibling `<diff>.config.csv`, `:269-276`) → translate to assembly text (`Translator`) → apply the unified diff (`Patcher.applyPatch`, the Script-Recompiler patcher — M3-03) → reassemble (`SCRIPTS.assemble`) → write over the files-dir copy. Because the diff always applies to the pristine backup, re-unpacking is idempotent.
3. **REPLACEMENT:** read the replacement source → `SCRIPTS.assemble` (assembly tokenize with the meta — the replacement `.txt` files are assembly language, e.g. the `entrypoint :ENTRYPOINT_*` declarations in `player_combat_script.txt`) → write (create-or-truncate).
4. **Incremental cache** (`:75-186`): a cache dir mirrors `patches/` plus a cache `scripts.csv`; a patch (re)applies only when the CRC32 of the patch file, its `include`d files (recursively, matched by `^\s*\+?#?include\s+(.+?)\s*;?\s*$` — both `+include` and `#include` spellings, resolved against the include dirs `libs` + `scripts`), or its config name changed. Patches removed from the csv are **restored** (backup moved back, empty dirs pruned).

The toolchain instance is the **runtime `ScriptManager`** (`GameEngine.SCRIPTS`): the unpacker calls `SCRIPTS.disassemble/assemble/meta` directly. `ScriptManager`'s lazy `meta()` loads the meta CSVs from `patches/meta` (`MetaManager.loadMeta("meta")`) and constructs the `FateCompiler`/`Tokenizer`/`Disassembler` — the same meta that drives the runtime debugger (M3-04). `SCRIPTS.compile(name, source)` (Fate source → assembled bytes) has **no in-repo callers** — it is the exposed API surface for mods and tooling.

### 3.3 Asset-side replacements (Unpacker)

Parallel to script patches, the unpacker runs code-registered file transformations over the unpacked tree (`Unpacker.java:120-131, 273-283`): branch transformations (the submap PXL→TIM converter, the "Claire model fixer") and **replacements** — currently exactly one, "Replace Chester texture" (drgn21_260 textures, reading the replacement asset from `patches/replacements/`). This is the file-level analogue of script replacements: a code-registered hook + a `patches/replacements/` file supply.

### 3.4 Runtime script-facing modding surface

What a mod can touch *at runtime* around scripts (M3 covers the language/VM; this is the mod hook surface):

| Surface | Mechanism | Anchor |
|---|---|---|
| Script lifecycle events | `ScriptLifecycleEvent` — 9 lifecycle points per script state (ALLOCATED, PRE/POST_SCRIPT_VM_TICK, PRE/POST_TICK_CALLBACK, PRE/POST_RENDER_CALLBACK, PRE/POST_DEALLOCATE), one reused instance per state, posted at each point | `ScriptState.java:268-269`; M8-02 §4.2 |
| Global-flag read interception | `ReadGlobalFlagsEvent` — two singleton events (FLAGS1/FLAGS2) posted on every script global-flag read; a handler may rewrite the value via the `IntRef` before the script sees it | `Scus94491BpeSegment.java:133-134` |
| Script→game-state access | `GameVarParam` (the script-visible game-variable switch), `ScriptStorageParam` (per-script-state storage), `GameVarArrayParam` (array variables; also posts the `DragonBlockStaff*`/`PartyFlagsChange`/`PrimaryPartyChange` events) | M3-06; `game/scripting/*` |
| Script→registry access | `ScriptInlineRegistryIdParam` — script literals of the form `modId:entryId` parsed to a `RegistryId` (M8-02 §1.1's two-part string form is exactly this format); entries resolve through delegates at use time | `ScriptInlineRegistryIdParam.java:43`; `Text.java:1460` |
| Script persistence | `WriteSaveDataEvent`/`ReadSaveDataEvent` — mod-tagged save data keyed by the mod's own registry ids (M8-02 §4.2) | M7-01 |
| Fate compilation at runtime | `SCRIPTS.compile(name, source)` — Fate source → script bytes (unused in-repo; the API exists for mods/tooling) | `ScriptManager.java:441-449` |

**Engine↔shipped-script interlock:** game code explicitly depends on shipped patch libraries — `SMap.java:433` ("Used as a FIFO queue by char_utils script lib"), `CharacterData2c.java:32` ("Used by char_utils.txt…") — i.e. the retail experience is engine + its own patch scripts as a coupled pair. A replacement script (e.g. `player_combat_script`) must honor these interlock points or the coupled engine behavior breaks. This is a factual boundary of the modding surface, not a design recommendation.

---

## 4. The i18n override model (plan RQ6)

### 4.1 Precedence

`LocaleSpecificity` (Mod-Loader) is the precedence order, highest first:

```
OVERRIDE > LANGUAGE_COUNTRY > LANGUAGE > FALLBACK
```

Lookup is **per-key**: `LangManager.findTranslation(key)` walks the specificity buckets in order and returns the first bucket containing the key (`LangManager.java:46-56`); a missing key returns **the key itself** (the observable "missing translation" behavior — the key renders on screen).

### 4.2 Sources

1. **In-jar mod files** — `<modId>/lang/<lang>[_<COUNTRY>].lang`, discovered at container construction (M8-01). At `LangManager.Access.initialize(mods, locale)` (`LangManager.java:94-118`), each loaded mod's `getLangResource(locale)` (Mod-Loader `ModContainer.java:74-100`) yields up to three URLs — `<lang>_<COUNTRY>.lang` → LANGUAGE_COUNTRY, `<lang>.lang` → LANGUAGE, `en.lang` → FALLBACK — and each is loaded (UTF-8 `java.util.Properties`) into its bucket. A mod may ship several languages; the bucket filled for a given key depends on the *requested* locale (e.g. locale `fr`: only the FALLBACK `en.lang` bucket is filled unless the mod ships `fr.lang`). Between mods, later-loaded properties overwrite earlier ones for the same key+bucket (mod iteration order is the `ModManager` instance map order — unspecified).
2. **File overrides** — `GameEngine.loadLang()` → `LANG_ACCESS.loadLangOverrides(Path.of("lang"), locale)` (`GameEngine.java:285-290`): the working-dir `lang/` tree is walked (following links); file names match `^(?:\w+\.)?([a-z]+)(?:_([A-Z]+))?\.lang$` — the optional `<name>.` prefix (e.g. `core.pl.lang`) is **organizational only and ignored**; classification is by the locale part: exact language+country match → LANGUAGE_COUNTRY, language-only match → LANGUAGE, and any `en.lang` → FALLBACK regardless of the active locale (`LangManager.java:164-187`). All matched files load into the single OVERRIDE bucket, later files overwriting earlier ones in walk order.
3. **Programmatic overrides** — `LangManager.addLangOverrides(map)` / `GameEngine.addLangOverrides(map)` merge a map straight into OVERRIDE (`LangManager.java:156-158`).

The in-repo `lang/` directory ships five override files: `core.pl.lang`, `lod.pl.lang`, `turnorder.pl.lang` (a Polish translation of the three in-repo mods' strings) and `unpacker.en.lang`, `unpacker.pl.lang` (the unpacker tool's strings).

### 4.3 Value semantics and lookup failures

- Values are **`String.formatted` templates**: `getTranslation(key)` runs `formatted()` when the value contains `%`; `getTranslation(key, args)` formats with args (`LangManager.java:58-71`).
- The game facade `I18n.translate` (all overloads: raw key / `RegistryEntry` / `RegistryDelegate`, ± args) wraps lookup in a catch for `IllegalFormatException` — a malformed template is logged and **the key itself is returned** (`game/i18n/I18n.java`). `I18nText` (deferred text component, `core/lang/I18nText.java`) holds key + args and resolves at render time, so translation changes apply to already-built UI.

### 4.4 Key namespaces and the language UI

- **Registry entries** (M8-02 §1.2): `<entryMod>.<registryEntryId>.<entryId>.<type>` — e.g. `lod.campaign_type.retail.name`, `lod.items.item_01.name`; config entries use the `label` type for their UI labels (evidence: `lod.config.max_level.label` in `lang/lod.pl.lang`).
- **Mod names:** `<modId>.name` (e.g. `lod.name=Legend of Dragoon`) — this is what the ModsScreen and error toasts display (M8-01 §5).
- **UI keys:** arbitrary `<modId>.ui.*`-style keys (e.g. `lod_core.ui.mods.error`).
- **Language selection:** the `lod_core:language` config entry (`LanguageConfigEntry`) — a `Locale` value, GLOBAL location, default = the locale in the *union of available locales* (all loaded mods' supported locales + the `lang/` override locales) closest to the JVM default locale, else `Locale.ENGLISH` (`LanguageConfigEntry.java:56-90`, `Main.ORIGINAL_LOCALE = Locale.getDefault()`). The options screen shows a dropdown of the same union sorted by display name; **`onChange` calls `GameEngine.loadLang()`** — reset + re-initialize + reload overrides — so switching language re-translates the running game live, with no mod reboot.

---

## 5. Java mechanisms (continuation of the J table)

| # | Mechanism | Observable contract (must preserve) | Implementation detail (may differ) |
|---|---|---|---|
| J16 | Unpack-time patch pipeline (backup → decompile → diff/replace → reassemble → write; CRC32-gated incremental; restore on removal) | Patched scripts are the ones the game runs; re-unpack is idempotent; removing a patch restores the original; only changed patches re-apply | CRC32 over patch + include closure + config name; cache dir mirroring; parallel file writes |
| J17 | Shared runtime toolchain (one `ScriptManager`/meta instance serves unpacker, runtime, and debugger) | Meta/enums/param names are identical across unpack-time patching and runtime disassembly; the Fate compiler is reachable at runtime | Lazy meta load from `patches/meta`; `compile()` has no in-repo callers |
| J18 | JavaFX-on-a-thread debugger | The debug UI is launchable from a bound key at runtime and can be dismissed without killing the game; failure to launch is non-fatal | `Platform.setImplicitExit(false)` + dedicated `Application.launch` thread; FXML/CSS |
| J19 | Properties-based lang files with per-key specificity buckets | The four-level per-key precedence (OVERRIDE > LANGUAGE_COUNTRY > LANGUAGE > FALLBACK), key-itself fallback, and live language switching without reboot | `java.util.Properties` (UTF-8 via reader); EnumMap buckets; `String.formatted` templates; directory walk for overrides |

---

## 6. Classification (continuation of the D table)

| # | Finding | Class | Note |
|---|---|---|---|
| D20 | The engine/content boundary is mechanism vs. configured behavior: all 1237 retail entries (incl. title/credits states, generic post-battle actions, 40 of 69 input actions) are mod-registered and delegate-resolved | **Contract** | Swappability of core content is observable (save files store entry ids — M7-01); hardcoding retail content in the engine would shrink the mod surface |
| D21 | Patch application is unpack-time only; the game never sees patch files at runtime | **Contract** | The observable unit of script modding is the *unpacked* script tree; a Rust pipeline must produce equivalent bytes, not replicate this patching machinery |
| D22 | Diffs apply to the pristine backup (idempotent re-unpack); removal restores the original | **Contract** | Unidirectional original→patched with full rollback is the observable behavior |
| D23 | Patch language is assembly (the disassembler's textual form), not Fate; Fate sources are manually compiled to the committed `.txt` outputs | **Contract + observation** | The char_utils.fate header comment is explicit; a runtime Fate-compile path exists (`SCRIPTS.compile`) but is unused in-repo |
| D24 | The 24 `<diff>.config.csv` files correct disassembler branch/table detection per patch site | **Contract** | A patch whose config is missing/lost re-applies (CRC includes the config name) — config is part of the patch identity |
| D25 | Debugger-originated inventory/level-up mutations carry distinct `*Source.DEBUGGER` tags through the normal events | **Contract** | Mods observing take/give/level-up events can distinguish debugger edits |
| D26 | i18n: per-key four-level precedence, key-itself fallback, malformed-template fallback to the key, live re-translation on language change without reboot | **Contract** | All four behaviors are user-observable; the `lang/` override dir and per-mod in-jar files are the two extension points |
| D27 | Override file-name prefix (`core.pl.lang`) is ignored; only the locale suffix classifies the file | **Observation** | Organizational convention only — two files with the same locale suffix merge into one bucket in walk order |
| D28 | Engine↔shipped-script interlock: game code documents dependencies on shipped patch libs (char_utils) | **Contract** | Replacements of retail scripts must preserve the documented interlock points (SMap:433 FIFO queue, CharacterData2c:32 removal flag) |

---

## 7. Verification criteria mapping (plan M8)

| Criterion | Where answered |
|---|---|
| Core-mod content inventory (what lod_core vs lod each register, with counts and examples) | §1.1 (full census: 126 / 1110 / 1 = 1237, per-registry with examples) |
| What the engine/content boundary implies (analysis only) | §1.2 (five analysis points; D20) |
| The debugger as a modding-adjacent tool | §2 (architecture, coupling, contract classification) |
| Modding extension points for scripts (Fate libs, replacements, diffs, registry read/write from scripts, script event hooks) | §3 (layout, patch semantics, asset replacements, the runtime hook table, engine↔script interlock) |
| The i18n override model (locale specificity, file formats, precedence) | §4 (precedence, the three sources, value semantics, key namespaces, language UI) |

**Open-question register:** no new items from this deliverable. All findings are source-verified; the Fate-compile API having no in-repo callers and the override-file-name prefix being ignored are recorded as observations (D23/D27), not open questions. The `queen_fury.fate` source has no committed compiled output in `patches/` (unlike the char_utils pair) — its consumption goes through the Queen Fury overlay flow documented in M5-03; this is cross-referenced, not re-investigated here. No `asset-validated` claims.
