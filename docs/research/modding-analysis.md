# Modding Analysis — Severed Chains (Java) Modding Architecture

Reference commit: `ab5dfe5ba6d4c07464a2f0e67985b9e77e2f7bdb`
Paths relative to `~/Documents/GitHub/Severed-Chains`.

## 1. Mod Model (verified)

- Mods are **Java JARs** in `mods/`, discovered by the external
  `org.legendofdragoon:mod-loader:4.3.5` library (`ModManager.findMods(dir, version)`).
- A mod declares `@Mod(id = "...", version = "^x.y.z")` (semver range; the engine
  matches against `Version.VERSION` — wrong versions reported via
  `MODS.getWrongVersions()` and shown as overlay notifications).
- `@EventListener`-annotated methods subscribe to the event bus (`EventManager`).
- Class-loading: **one shared `URLClassLoader` over all mod JARs** (no per-mod
  isolation — mods can see each other's classes; verified in mod-loader.md §2).
  Failed loads tracked in `MODS.getFailedToLoad()` (modId → error), surfaced in
  the title screen and crash logs.
- **Retail content is a mod**: `legend.lodmod.LodMod` (`@Mod(id = "lod")`)
  registers all game content — characters, stats, elements, additions, items,
  equipment, spells, shops, encounters, battle actions, campaign/engine-state
  types, input actions, configs, fonts/icons. This "content as a mod" pattern is
  the template for the native game layer.
- Bundled example mod: `legend.turnorder.TurnOrderMod` (`@Mod(id = "turn_order")`)
  — subscribes to `RenderEvent` + battle events, registers a config entry
  (`ShowTurnOrderConfig`), renders a turn-order HUD, and its on-screen bounds are
  exposed to the post shader via `turn_order_bounds` (verified in
  `RenderEngine.onDraw`).

## 2. Core Extensibility Machinery (verified)

### 2.1 Registries (external mod-loader + in-repo `Registries`)

> mod-loader internals now **verified from source** (`~/Documents/GitHub/Mod-Loader`,
> commit `c692134`) — see `docs/research/mod-loader.md` for exact semantics.

- `Registries` (core, `legend.core.Registries`) holds the engine's registry set:
  `config`, `campaignTypes`, `engineStateTypes`, `inputActions`,
  `characterTemplates`, `statTypes`, `statModTypes`, `goods`, plus mod-added
  registries discovered via `AddRegistryEvent` (mods can define **new registry
  kinds**).
- `RegistryId` = `{modId, entryId}` (grammar `[a-z][a-z0-9_-]*`, full id ≤ 255
  chars — Tag-safe; verified mod-loader.md §4).
- `MutableRegistry`/`Registry`/`RegistryEntry`/`RegistryDelegate`/`Registrar`:
  typed registries; **lazy entry resolution** — `RegistryDelegate` proxies are
  created on any `getEntry` (even missing ones) and throw with mod attribution
  at *use* time; `Registrar` registers **lazy suppliers** per (registry, mod);
  duplicate entry id → fatal `DuplicateRegistryIdException` (verified).
  `Registrar` in a mod class declares entries lazily
  (`CONFIG_REGISTRAR.register("addition_mode", ...)` — see
  `legend.game.modding.coremod.CoreMod`, 378 lines of core config entries).
- Boot order (`GameEngine.bootMods`): load mod classes → init event bus →
  post `AddRegistryEvent` (mods register their registries) → initialize core
  registries in dependency order (config, campaignTypes, engineStateTypes,
  inputActions, characterTemplates, statTypes, statModTypes, goods) →
  `loadingComplete` (fires registry events so consumers can read entries) →
  input bindings → lang.

### 2.2 Event bus

> Event semantics now **verified from source** (mod-loader.md §3).

- `EventManager.postEvent(TypedEvent)` — synchronous on the calling thread,
  **exact-class dispatch** (an event subclass does not trigger base-class
  listeners), **priority-ordered** (HIGHEST→LOWEST; ties = registration
  order); `stopPropagation`/`cancel` semantics; **critical events re-throw
  handler exceptions, non-critical errors go to `errorCallback(modId, e)`**
  (→ `onModError` → overlay notification, mod failure doesn't crash).
- In-repo event catalog (`legend.game.modding.events.*`) — representative set:
  - battle: `BattleStartedEvent`, `BattleEndedEvent`, `BattleEntityTurnEvent`,
    `MonsterStatsEvent`, `EnemyRewardsEvent`, `SpellStatsEvent`,
    `StatDisplayEvent`, `LoadDeffEvent`, `LoadEnemyEvent`, `ActiveItemEvent`,
    `ActiveSpellEvent`, `ArcherSpEvent`, `BattleMusicEvent`,
    `RegisterBattleEntityStatsEvent`, `DragonBlockStaffOn/OffEvent`
  - characters: character create/unlock events
  - config: `ConfigRegistryEvent`
  - engine: `EngineStateChangeEvent`
  - gamestate: `GameLoadedEvent`, `GameSavedEvent`, ...
  - input: `InputPressedEvent`, `InputReleasedEvent` (mod-only; the game reads
    input directly — verified: no in-game subscribers)
  - inventory: item/goods/shop/equipment/spell registry + gather events
  - scripting: `ReadGlobalFlagsEvent` (mods can read story flag state)
  - submap/worldmap: enter/exit + object events
  - `RenderEvent` (per-frame render hook — how mods draw)
- Content registration events (one per content type): `RegisterCharacterTemplatesEvent`,
  `AdditionRegistryEvent`, `ItemRegistryEvent`, `GoodsRegistryEvent`,
  `ShopRegistryEvent`, `SpellRegistryEvent`, `EquipmentRegistryEvent`,
  `EncounterRegistryEvent`, `RegisterDeffsEvent`, `RegisterPostBattleActionsEvent`,
  `RegisterBattleActionsEvent`, `GatherBattleActionsEvent`,
  `RegisterCampaignTypesEvent`, `RegisterEngineStateTypesEvent`,
  `InputActionRegistryEvent`, `RegisterAtlasTexturesEvent`, `IconMapEvent`,
  `RegisterLevelUpActionsEvent`, `ElementRegistryEvent`, `StatTypeRegistryEvent`,
  `StatModTypeRegistryEvent`, `BattleEntityTypeRegistryEvent`,
  `ElementIconRegistryEvent`, `ConfigRegistryEvent`, ...

### 2.3 Configuration

- `ConfigCollection` + typed entries (`Bool/Int/Float/Enum/String/Color/
  CampaignName`), each with a `ConfigCategory`, storage location
  (`GLOBAL` / `CAMPAIGN` / per-save), and presets (`ConfigPresetManager`).
- Entries are registry-driven: mods register config entries; the options menu is
  generated from the registry (`ControllerKeybindsConfigEntry`,
  `LanguageConfigEntry` — including **per-action keybinding editing**).
- Global config `config.dcnf` (Tag-serialized); per-campaign/per-save configs
  stored inside saves.

### 2.4 Input actions

- `InputAction` registry (`InputActionRegistry`): semantic actions
  (`INPUT_ACTION_BTTL_ATTACK`, `_BTTL_COUNTER`, `_BTTL_TRANSFORM`, `_MENU_*`,
  `_GENERAL_*`, `_FREECAM_*`, `_DEBUG_*`...), each with default bindings
  (`InputBindings.initBindings`), user-editable bindings (keycode/scancode/button/
  axis activations with mods + deadzones), and `InputCodepoints` for on-screen
  button glyphs.
- **Battle addition timing input** (verified, `AdditionOverlaysEffect44`):
  per hit, `right` = `BTTL_ATTACK` (or `BTTL_COUNTER` for counter hits),
  `wrong` = the other; success if pressed within
  `[frameSuccessLowerBound, frameSuccessUpperBound]` (derived from
  `AdditionHitProperties10.overlayHitFrameOffset_02` +
  `totalSuccessFrames_03`, scaled by `ADDITION_TIMING_WINDOW_CONFIG`);
  early/late/wrong → fail; `autoCompleteType` covers tutorial/auto modes.
  **The "which action is correct" logic is hardcoded — there is no mod hook for
  alternative input evaluators today.** Third-party mods like Playable Lloyd
  (face-button sequences) require engine-level seams the native project must add
  (see `docs/architecture/luau-api.md` §Combat).

### 2.5 Story script patching

> Pipeline now **verified from source** (`~/Documents/GitHub/Script-Recompiler`,
> commit `3433e6f`) — see `docs/research/script-recompiler.md`. Summary:

- External `script-recompiler:0.7.11` provides the **tooling half** of the
  script VM: `Disassembler` (raw LE u32 word array → token tree with
  register-type tracking), `Assembler` (token tree → words; op encoding
  `opcode | paramCount<<8 | headerParam<<16`), `Translator`/`Tokenizer`
  (canonical text form), `Patcher` (unified diff over stripped disassembly,
  with `call <index>`↔`call <name>` normalization), and a **fate compiler**
  (ANTLR grammar; high-level language → opcodes). The **runtime VM**
  (`legend.game.scripting.ScriptManager` + `ScriptState`) lives in the
  reference repo and reuses the same library at runtime (`GameEngine.SCRIPTS`).
- **Patch application** (`legend/game/unpacker/scripts/ScriptPatcher`, run from
  `GameEngine.start` right after unpack): a build-shipped `./patches/` dir
  (built from the repo's `patches/` via Gradle) with:
  - `scripts.csv` — patch list: `<diff|replacement>,<scriptPath>,<patchFile>,<desc>`;
  - `scripts/*.diff` — unified diffs in canonical text form; `scripts/*.txt` —
    full **fate-language replacement scripts** (notably
    `player_combat_script.txt` — "Major rewrites to support modding" — and
    `throw_item.txt`);
  - `libs/` (includes), `meta/` (**local** meta CSVs — the game does NOT fetch
    the remote scmeta; only the CLI does), `replacements/` (binary files, e.g.
    `chester.tim`).
  - Apply = back up unpatched original → disassemble(backup) → translate →
    apply unified diff (or compile fate for replacements) → assemble → write;
    **CRC32-of-patch-and-includes cache** (`files/patches/cache/`) with
    **restore-on-deletion** (deleted patch → backup moved back).
- `patches/scripts/DRGN0|DRGN1|DRGN21/...` — the per-script patch corpus
  (hundreds of retail bug fixes: finisher crashes, dart checks, hardcoded
  char counts...).
- This is the "fix original game bugs + tweak story" layer; native ports the
  fate pipeline into `psx::script` (dis/asm/text codec) + an in-repo unified-
  diff component + a native fate compiler (open question Q-SCR-1) + the `fate`
  CLI (roadmap M12).

### 2.6 i18n

> Verified from source (mod-loader.md §5).

- `LangManager` (external mod-loader): 4-level specificity
  **OVERRIDE > LANGUAGE_COUNTRY > LANGUAGE > FALLBACK**; `.lang` files are
  Java `.properties` (UTF-8) at `<modId>/lang/<xx>[_<XX>].lang` in each mod JAR
  + a global user-override directory (loaded before mod boot); mod load order
  = override order; key-as-fallback (missing key returns the key itself).
  In-repo bundles: `src/main/resources/lod*/lang/en.lang`. `I18n.translate(key)`.

## 3. What a Java Mod Can Do Today (capability list)

1. Register/replace content entries (characters, items, equipment, spells,
   additions, elements, stats, shops, encounters, campaign types, engine states,
   input actions, configs, icons, textures).
2. Subscribe to any engine event (frame render, battle lifecycle, state changes,
   input, inventory, saves).
3. Add/modify config entries and keybinds.
4. Register new registry kinds.
5. Patch story scripts (via the fate pipeline, at unpack time).
6. Render overlays (`RenderEvent` + render API).
7. Extend saves (`modData` ListTag via Read/WriteSaveData events; DABA Save60).
8. Provide language overrides.
9. Replace assets (unpack-time replacements in `patches/replacements`).
10. Battle content: DEFFs (monster behaviour CMB), post-battle actions,
    battle UI actions, entity stat registration.

**Not possible today (Java):** alternative addition input evaluators (e.g.
button sequences), per-addition custom combat resolution, arbitrary
script-in-game (no Luau), runtime asset replacement without re-unpack,
non-Java native extensions.

## 4. Migration to Native (proposal)

| Java concept | Native equivalent |
|---|---|
| Mod JAR + `@Mod` | Mod **package** (zip dir): `mod.toml`/`mod.json` manifest (id, version range, deps, assets, optional `.so/.dll/.dylib` + `.luau` scripts) |
| `@EventListener` methods | Engine event bus with typed events; Luau `Engine.Event:Subscribe` + native plugin subscriptions |
| Registries + `RegistryId` | Same model in C++ (`RegistryId{modId, entryId}`), exposed to Luau as tables and to native plugins as string IDs |
| `LodMod` (retail content) | Built-in **`lod` game module** (C++ or Luau): registers all retail content — same structure, first-class mod API |
| fate script patches | `psx::script` dis/asm + in-repo unified-diff + native fate compiler; the fate CLI tool (roadmap M12) — verified pipeline in script-recompiler.md |
| Keybinding config | Input actions + bindings storage (port `InputBindings` model) |
| Save `modData` | Port Tag `ListTag` mod-data slot; versioned |
| Asset replacements | Runtime override directories (priority chain) — replaces unpack-time replacements (more flexible) |
| JavaFX debugger | Qt 6 debugger tooling (separate process, shared memory/IPC or socket) |

**Key decisions for the native design** (detailed in architecture docs):
1. Registry/event model is **data-adjacent** (IDs, params), so both Luau and
   native plugins can participate without a JVM.
2. Additions get a pluggable **input evaluator + hit resolver** interface
   (seam missing in Java — a native advantage for the Playable-Lloyd class of
   mods).
3. Mod isolation: each mod's scripts run in a sandboxed Luau VM namespace;
   native plugins are dlopen'd against the **C++23 plugin API** with a
   documented failure contract (native-api.md — a crashing plugin takes down
   the process; that is the documented limitation, with contract tests + fuzz
   + an out-of-process mode as future work).

## 5. Open Questions

- ~~External mod-loader/modding-repo source not in the reference repo~~ —
  **RESOLVED**: `Mod-Loader` (`c692134`) and `Script-Recompiler` (`3433e6f`)
  repositories inspected read-only; semantics verified in
  `docs/research/mod-loader.md` and `docs/research/script-recompiler.md`.
- **Q-MOD-1..3 / Q-SCR-1..3**: see the respective research docs (mod error
  callback behavior, i18n retail-vs-mod precedence, fate compiler approach,
  multi-mod patch merging, entrypoint count source).
- Mod license diversity: mods are third-party; the native mod package format
  must carry per-mod license metadata (don't assume AGPL).
- Whether any released mods rely on Java-specific reflection/annotation tricks
  that have no native analogue (unknown — no mod source available locally).
  Note: the *shared classloader* in Java means cross-mod class references are
  technically possible today; the native design deliberately disallows them
  (documented in the migration guide).