# M7-02 — The Configuration System

**Milestone:** M7 (Persistence) — deliverable 2 of 2.
**Scope:** typed/registry-registered configuration entries; global vs per-campaign vs per-save storage; the `.dcnf` binary format; config presets (`.dpre`); the legacy `config.conf` properties file; runtime events and consumers.
**Primary sources (all `Severed-Chains`, reference revision `0c08c679b`):**
- `legend/game/saves/`: `ConfigEntry` (+ `BoolConfigEntry`, `IntConfigEntry`, `FloatConfigEntry`, `StringConfigEntry`, `EnumConfigEntry`, `ColourConfigEntry`, `CampaignNameConfigEntry`), `ConfigCollection`, `ConfigStorage`, `ConfigStorageLocation`, `ConfigCategory`, `ConfigRegistryEvent`, `ConfigPreset`, `ConfigPresetEntry`, `ConfigPresetManager`, `ConfigPresetState`, `RawTag`.
- `legend/game/modding/events/config/`: `ConfigEvent`, `ConfigUpdatedEvent`, `ConfigLoadedEvent`, `ConfigDefaultPresetsEvent`.
- `legend/core/Config.java` (legacy properties store, 249 lines).
- `legend/core/GameEngine.java:102` (`CONFIG`), `:126-132` (static boot), `:248` (GLOBAL config load).
- `legend/game/modding/coremod/CoreMod.java` (80 entries), `legend/lodmod/LodConfig.java` (7), `legend/turnorder/TurnOrderConfigs.java` (1); representative custom entries in `legend/game/modding/coremod/config/`.
- UI: `legend/game/inventory/screens/OptionsScreen.java`, `OptionsPresetsScreen.java`, `OptionsCategoryScreen.java`, `MainMenuScreen.java:543-549`, `NewCampaignScreen.java`, `legend/game/combat/ui/BattleHud.java:1498-1499`, `CampaignSelectionScreen.java:105`.
- Consumers: `legend/game/debugger/DebuggerController.java:186-196`; audio consumers per M6-01 §5.1/§5.3/§8 (A4, A11).

**Layering:** the entire configuration system is **layer 4** (reference implementation) — the retail game has no moddable config; the closest retail analog is the per-slot save data already documented in M7-01. The registry/event machinery it builds on (`Registrar`, `MutableRegistry`, `RegistryEntry`, `RegistryEvent.Register`, `ModContainer`) is documented under M8 (T-MOD); here only its use from the config side is recorded.

**Evidence classification:** all format claims are **source-verified** (from the reference serializer code); no `asset-validated` claims (no retail config/preset files are available; the formats are reference-defined regardless).

---

## 1. Two coexisting config systems

Two independent configuration stores coexist in the reference:

| | Legacy `config.conf` | Registry `.dcnf` system |
|---|---|---|
| Class | `legend.core.Config` (static) | `GameEngine.CONFIG` : `ConfigCollection` + `ConfigStorage` |
| File | `./config.conf` (Java `Properties`, text) | `config.dcnf` / `campaign_config.dcnf` / save-file tail (binary, §3.2) |
| Keys | fixed string keys (§1.1) | `RegistryId`s registered by mods (§2.1) |
| Moddable | No | Yes — entries carry pluggable serializers/deserializers/edit controls |
| Role | host-level tool settings (window, dev flags, overlays, speed multiplier) | all game/mod options (controls, gameplay, audio, UI, mod enablement) |
| Written when | `GameEngine` static block (create-if-missing or load, `GameEngine.java:126-132`), shutdown (`RenderEngine.java:1241-1244`), low-memory fallback (`Unpacker.java:319-324`) | the save/campaign/preset flows (§5) |

The naming trap is real: `legend.core.Config` (legacy, static) vs `GameEngine.CONFIG` (the live collection, `GameEngine.java:102`). They do not interact except via `Config.switchFullScreen()` (`Config.java:181-186`), which toggles the new-system `CoreMod.FULLSCREEN_CONFIG` entry through `CONFIG` and immediately re-saves `config.dcnf` — the only bridge between the two.

### 1.1 Legacy `config.conf` keys (`Config.java`)

Defaults are set in the class initializer; the file is written **sorted** (`SortedStoreProperties` wraps `Properties.store` with a `TreeSet`-sorted view, `Config.java:228-249`):

| Key | Default | Meaning / accessor |
|---|---|---|
| `low_memory_unpacker` | `false` | `Config.lowMemoryUnpacker()`; set at runtime by `enableLowMemoryUnpacker()` (unpacker fallback path, `Unpacker.java:319-324`) |
| `last_config_preset` | `""` | slug of the last selected preset; pre-selection in `OptionsPresetsScreen.java:64-70` and `NewCampaignScreen.java:116-124` |
| `window_width` / `window_height` | `960` / `720` | window size; note the **read fallbacks differ from the stored defaults** (`windowWidth()` reads with fallback 640, `Config.java:72-74`) |
| `battle_ui_colour_change`, `battle_ui_r/g/b` | `false`, `0/41/159` | battle UI recolouring (RGB 0..255) |
| `addition_overlay_colour_change`, `addition_overlay_r/g/b` | `false`, `72/96/255` | addition-mode overlay colour (`getAdditionOverlayRgb` composites R,G,B,0x00 little-endian into an `int`) |
| `counter_overlay_r/g/b` | `216/96/32` | addition-counter overlay colour; `setCounterOverlayRgb` has a **source bug**: it writes the `addition_overlay_colour_change` key instead of a counter-change key (`Config.java:167-179`) |
| `game_speed_multiplier` | `1` | 1..16 (`load()`, `Config.java:215-222`); dual-state: `gameSpeedMultiplier` (current) vs `loadedGameSpeedMultiplier` (the loaded value — allows restoring the pre-session value) |
| `combat_stage`, `combat_stage_id` | `false`, `0` | dev feature: jump to a specific battle stage (id clamped 1..127) |

`Config.load()` clamps the speed multiplier and captures `loadedGameSpeedMultiplier`; `Config.save()` (`:224-226`) truncates and rewrites. `readInt`/`readBool` (`:188-213`) clamp / fall back silently on malformed values.

---

## 2. The registry-based entry model

### 2.1 The registry and registration

- Registry: `GameEngine.REGISTRIES.config` — `MutableRegistry<RegistryId, ConfigEntry<?>>`; iteration `for(RegistryId id : REGISTRIES.config)`, lookup `REGISTRIES.config.getEntry(id).get()` (e.g. `OptionsScreen.java:57-60`).
- Registration uses the standard Mod-Loader registrar pattern (M8 territory): `private static final Registrar<ConfigEntry<?>, ConfigRegistryEvent> CONFIG_REGISTRAR = new Registrar<>(GameEngine.REGISTRIES.config, MOD_ID)` (`CoreMod.java:100`), then `CONFIG_REGISTRAR.register("menu_inner_deadzone", () -> new DeadzoneConfigEntry(0.5f))` → `RegistryDelegate` field. `ConfigRegistryEvent extends RegistryEvent.Register<ConfigEntry<?>>` (`saves/ConfigRegistryEvent.java:7-11`) is the registrar's register-event type.
- **Verified entry census (this revision):** `CoreMod` 80, `LodConfig` 7 (`max_level`, `max_dragoon_level`, `item_stack_size`, `extended_dragoon_actions`, `ui_background_colour`, `ui_selection_colour`, `dabas_save_data`), `TurnOrderConfigs` 1 (`show_turn_order`) — **88 total**; no other module defines a `ConfigEntry` registrar (source-verified by repo-wide grep). *(Supersedes the plan's approximate 80+8+2=90 figure.)*

### 2.2 `ConfigEntry<T>` fields (`ConfigEntry.java:12-27`)

`ConfigEntry extends RegistryEntry` (M8 machinery). Fields: `defaultValue` (private, `:13`), `storageLocation` (`:14`), `category` (`:15`), `serializer`/`deserializer` — `Function<T,byte[]>` / `Function<byte[],T>` (`:16-17`), `editControl` — `BiFunction<T, ConfigCollection, Control>` (private, `:19`).

Behavior hooks:
- `onChange(config, oldValue, newValue)` (`:103-105`) — empty base hook; called on every *notifying* `setConfig` (§4.1) **before** the `ConfigUpdatedEvent` is posted; custom entries override for runtime side effects (e.g. `LanguageConfigEntry.onChange` → `GameEngine.loadLang()`, an i18n reload at runtime).
- `getPresetValueType()` (`:38-62`) — derives the preset schema tag from the *runtime class of the default value*: `boolean`, `int8`/`int16`/`int32`/`int64` (`Byte`/`Short`/`Integer`/`Long`), `float32`/`float64`, `char`, `string`, `enum`, `bytes`, `string_array`, `list`/`set`/`map` (collection interfaces), else **`opaque`**. "This describes compatibility; it does not validate the serialized bytes."
- `getPresetSchemaVersion()` (`:64-66`) — default **1**, overridable: "Positive version of this entry's preset encoding… Readers require an exact match. Legacy presets without metadata cannot be checked."
- i18n: `getLabelTranslationKey()`/`getHelpTranslationKey()` (`:80-86`) delegate to `RegistryEntry.getTranslationKey("label")`/`("help")` (the `<modId>.config.<entryId>.…` key scheme — M8); `hasHelp()`/`isAdvanced()` default false; `availableInBattle()` (`:99-101`) defaults true and gates the in-battle options screen.
- `makeEditControl(value, config)` (`:76-78`) builds the UI widget; `hasEditControl()` is the options-screen visibility gate (entries without one — e.g. `StringConfigEntry` — never appear in options).

### 2.3 Built-in typed entries and their byte codecs

Payloads are **opaque bytes** whose meaning is defined per entry (§3.2); verified codecs:

| Entry (class) | Value | Wire format (entry payload) | Edit control |
|---|---|---|---|
| `BoolConfigEntry` | `boolean` | 1 byte `0`/`1`; deserializer is lenient — any other byte → default (`BoolConfigEntry.java:32-49`) | `Checkbox` (optional via the `editable` ctor flag) |
| `IntConfigEntry` | `int` | 4 bytes, `ByteOrder.nativeOrder()` (LE on x86/ARM64), via `MathHelper.setInt` (`IntConfigEntry.java:30-43`); min/max are **UI-only** spinner bounds, not serialized | `NumberSpinner.intSpinner` (optional) |
| `FloatConfigEntry` | `float` | 4 bytes, `nativeOrder` bits of `Float.floatToIntBits` (`FloatConfigEntry.java:30-43`); step/bigStep/min/max are UI-only | `NumberSpinner.floatSpinner` (optional) |
| `StringConfigEntry` | `String` | **N-byte little-endian length prefix (N = the entry's `lengthSize` ctor parameter) + UTF-8**, via `IoHelper.stringToBytes/stringFromBytes` (`IoHelper.java:75-96`); malformed → default | **none** (hidden from options) |
| `EnumConfigEntry<E>` | enum | 1-byte length prefix + UTF-8 of `name()` (`IoHelper.enumToBytes`, `IoHelper.java:99-101`); unknown name → default | `Dropdown` — option i18n keys are `<modId>.config.<entryId>.<CONSTANT>` |
| `ColourConfigEntry` | `Vector3f` (0..1 RGB) | 3 bytes R,G,B truncated from `v*255` (`ColourConfigEntry.java:30-45`); alpha discarded | `ColourPicker` |
| `CampaignNameConfigEntry` | campaign name, min 1 | `StringConfigEntry` codec with `lengthSize = 1` → **1-byte length + UTF-8**; **CAMPAIGN** location, GAMEPLAY category (`CampaignNameConfigEntry.java:4-8`) | none (set programmatically) |

Representative **custom entry** (`LanguageConfigEntry`, GLOBAL/USER_INTERFACE, value `Locale`): default = original locale (resolved among loaded mods' supported locales + a `lang/` override dir, `LanguageConfigEntry.java:58-71`); edit control = dropdown of all available locales; `onChange` → `GameEngine.loadLang()`; custom codec = NUL-terminated ASCII language + optional NUL-terminated country (`ja\0JP\0`, `:97-133`). This pattern (custom serializer + edit control + `onChange` hook) is the extension point mods use for non-primitive config values (keybind maps, mod lists, controller profiles, …).

---

## 3. Storage: the `.dcnf` format and the three locations

### 3.1 Locations and paths (`ConfigStorageLocation`, `ConfigStorage.java:37-150`)

| Location | File | Lifecycle |
|---|---|---|
| `GLOBAL` | `config.dcnf` (working dir) | boot load after mod boot (`GameEngine.java:248`); saved by options flows (§5.6) |
| `CAMPAIGN` | `saves/<campaign dir>/campaign_config.dcnf` | written at campaign creation (`NewCampaignScreen.java:246`), on in-game options close (`MainMenuScreen.java:546-547`, `BattleHud.java:1498-1499`), and in the memcard flow (`SaveManager.java:280`); loaded on campaign load (`Campaign.java:36-48`) |
| `SAVE` | **tail of each `.dsav` file** (M7-01 §6.3/§8.3/§9.3: `u32 count + {RegistryId, u32 len, bytes}*` after the version-specific content) | written/loaded atomically with the save; copied into the live `CONFIG` on load (§5.5) |

**In practice only the `CAMPAIGN` section ever appears in `campaign_config.dcnf`** — every call site passes `ConfigStorageLocation.CAMPAIGN` for that file; the `SAVE` section exists only in save-file tails. (The `loadConfig`/`saveConfig` APIs themselves are location-generic.)

### 3.2 Binary format and semantics (GLOBAL/CAMPAIGN files and the save tail)

```text
+0x00  u32 LE  entryCount          (entries for this location)
per entry (entryCount times):
        u24 LE  idLen  + idLen ASCII bytes   RegistryId of the entry
        u32 LE  dataLen
        dataLen bytes  opaque payload (entry's own codec, §2.3)
```

Write — `ConfigStorage.saveConfig(configs, location, data, offset)` (`:123-150`): iterates the **whole registry**, keeps entries whose `storageLocation` matches, serializes `configs.getConfig(entry)` for each (so **values equal to their defaults are written too** — files are self-contained snapshots; a `null` value — practically impossible, `getConfig` falls back to the default — would be skipped with a warning). The `Path` variant (`:56-77`) builds into a **pre-allocated 100 KiB `FileData`** (an implicit size ceiling) and writes the used slice; parent directories are created on demand.

Read — `ConfigStorage.loadConfig(configs, location, data)` (`:79-121`):
1. `configs.clearConfig(location)` (drops the location's values *and* any values for ids no longer in the registry, `ConfigCollection.java:91-97`);
2. per entry: read the `RegistryId`; `getEntry` — if the id/mod is **unknown**, read the length and **skip the payload** (forward-safe: an old file referencing a since-removed entry does not desynchronize the stream; a warning is logged);
3. known id: read length, slice the payload, and **only if `entry.storageLocation == location`** apply it via `entry.deserializer` + `configs.setConfigQuietly` (an entry recorded under the wrong location is decoded and silently discarded);
4. after all entries: `EVENTS.postEvent(new ConfigLoadedEvent(configs, location))` **and** `RENDERER.setFrameSkipOption(CONFIG.getConfig(CoreMod.FRAME_SKIP_CONFIG.get()))` — the frame-skip option is re-applied after **every** location load, not just GLOBAL (`:118-119`).

The `Path` load variant (`:37-54`): **missing file → `clearConfig(location)` and return (no event, no frame-skip re-apply)**; **corrupt file → the `Throwable` is caught and logged as a warning** (the load is treated as "no file"; it does not propagate).

### 3.3 The Tag-based variant (presets)

`ConfigStorage` also reads/writes locations as **Tag** structures (M7-01 §2 model) for preset files (§6):
- `saveConfig(configs, location, MapTag)` (`:203-228`): `tag[location.name()]` = `ListTag` of per-entry `MapTag`s: `configId` (`RegistryIdTag`), `valueType` (`StringTag`, `getPresetValueType()`), `schemaVersion` (`IntTag`, `getPresetSchemaVersion()`), `data` (`RawTag`, the §2.3 bytes). `saveConfigSchema` (`:230-239`) throws if the entry has no valid value type/schema (a preset-incompatible entry breaks preset saving for that location).
- `loadConfig(configs, location, MapTag)` (`:152-201`): per entry — unknown id, wrong location, or (when schema metadata is present) **`valueType`/`schemaVersion` mismatch vs the entry's current values** → the entry is **skipped and counted**; after the location, if any were skipped, a user-visible `GameOverlay` notification (`lod_core.ui.options_presets.skipped_settings`, count) is shown. Schema is **required when the preset's top-level tag has a `formatVersion`** key; legacy presets without `formatVersion` check the per-entry keys only if present (`loadConfigValue`, `:189-201`). Ends with the same `ConfigLoadedEvent` + frame-skip re-apply as the binary variant.

### 3.4 Locking (advisory)

`ConfigCollection.lockConfig(entry)` (`ConfigCollection.java:117-122`): `StackWalker(RETAIN_CLASS_REFERENCE).getCallerClass()` → `MODS.getModsByClass(caller)` → the **i18n-translated mod display names** (sorted) are recorded per entry. Locks are **advisory, not enforced**: `setConfig` never checks them — the options UI simply disables the control and shows a "locked" label (`OptionsScreen.java:106-115`). Locks are memory-only (never persisted).

---

## 4. `ConfigCollection` semantics

Fields (`ConfigCollection.java:21-27`): `notifyChanges` (ctor flag), `configValues` (`Map<RegistryId,Object>` — *explicitly set* values only), `locked` (§3.4), `presetState` (`@Nullable ConfigPresetState`), `revision` (monotonic counter).

- **Constructors** (`:28-35`): `ConfigCollection()` = `this(true)`; `ConfigCollection(false)` for "detached edits that must not change runtime state or post events" (the preset editor's scratch collection). The live `CONFIG` is `new ConfigCollection()` (`GameEngine.java:102`).
- **`getConfig(entry)`** (`:37-40`): `configValues.getOrDefault(id, entry.getDefaultValue())` — the default fallback; values are never absent-throwing.
- **`setConfig(entry, value)`** (`:42-50`): captures `oldValue`; `setConfigQuietly` (put + `revision++`); **if `notifyChanges`**: `entry.onChange(this, oldValue, value)` *then* `EVENTS.postEvent(new ConfigUpdatedEvent(entry))`. There is **no default-dedup** — a value equal to the default is still stored (the write-side dedup happens instead in `saveConfig`, which serializes whatever `getConfig` returns, §3.2).
- **`setConfigQuietly`** (`:52-55`): package-private; put + `revision++`, no callbacks — used by the storage loaders.
- **`hasConfig`** (`:82-84`), **`clearConfig()`** (`:86-90`), **`clearConfig(location)`** (`:91-97`, also drops ids missing from the registry).
- **`copyConfigFrom(other)` / `copyConfigFrom(other, locations)`** (`:99-115`): bulk copy (all / per-location, resolving each id against the registry); both null out `presetState`.
- **Preset identity** (`:58-80`): `setPreset(name)` attaches a `ConfigPresetState(name, this, revision)`; `getPresetName()`, `isPresetModified()` (→ `presetState.isModified(this, revision)`, §6.2), `getPresetDisplayName()` (i18n: *"custom"* / *"modified <name>"* / plain name); `refreshPreset()` (`:78-80`) bumps `revision` — called after mod reboots / editor returns so the modified-check re-runs.

---

## 5. The config lifecycle (boot → runtime → persistence)

```text
BOOT
  GameEngine static block (GameEngine.java:126-140)
    1. legacy config.conf: exists? Config.load() : Config.save()   (§1)
    2. GPU/SPU/AudioThread built with compile-time defaults
  mod boot (M8)
    3. config entries registered (Registrar + ConfigRegistryEvent)
  GameEngine.init (GameEngine.java:248)
    4. CONFIG already constructed (:102, all-defaults reads)
    5. ConfigStorage.loadConfig(CONFIG, GLOBAL, "config.dcnf")
       -> clearConfig(GLOBAL) + per-entry setConfigQuietly
          + ConfigLoadedEvent + RENDERER frame-skip re-apply
       (no file -> location cleared silently, no event)

NEW CAMPAIGN (NewCampaignScreen)
    6. preset dropdown: preselected via legacy last_config_preset;
       onPresetSelected/applyPreset (:200-210) ->
         CONFIG.copyConfigFrom(preset.config, {CAMPAIGN, SAVE})
         CONFIG.setPreset(preset.name)
       "Options" button (:148-154): live CONFIG, all locations; on close
         CONFIG.refreshPreset() + saveConfig(GLOBAL, config.dcnf) only
         ("don't save campaign config until an actual save file is made
          so we don't end up with orphan campaigns" — comment at :152)
    7. render-unload (:231-250): bootRegistries ->
         campaign = Campaign.create(SAVES, name)        (Campaign.java:50-55:
           config.setConfig(CAMPAIGN_NAME, name) only)
         campaignType.setUpNewCampaign + NewGameEvent + SAVES.loadGameState
         campaign.loadConfigInto(CONFIG)                (campaign name -> CONFIG)
         CONFIG.setConfig(KNOWN_MODS_CONFIG, allModIds)
         CONFIG.setConfig(ENABLED_MODS_CONFIG, enabledMods)
         saveConfig(CONFIG, CAMPAIGN, campaign_config.dcnf)   (:246)
         SAVES.newSave(...)                            (first .dsav; SAVE tail
                                                       carries the SAVE values)

LOAD (campaign selection; save load)
    8. Campaign.load (Campaign.java:36-48): campaign.config loads the
         CAMPAIGN section of campaign_config.dcnf (missing -> cleared);
         campaign name = CAMPAIGN_NAME value else directory name
       CampaignSelectionScreen:105 -> campaign.loadConfigInto(CONFIG)
    9. SaveManager.loadGameState(save, fullBoot) (SaveManager.java:407-431):
         CONFIG.clearConfig(SAVE)
         CONFIG.copyConfigFrom(save.config)        (the save's own
            ConfigCollection, deserialized from the .dsav tail by
            ConfigStorage.loadConfig(..., SAVE, FileData) at save load)
         fullBoot -> bootRegistries + InputBindings.loadBindings(CONFIG)
         ... createGameState / state data / modData / campaignType (M7-01 §11.3)

RUNTIME
   10. CONFIG.setConfig(entry, value)  ->  put + revision++ ->
        entry.onChange (if notifying) -> ConfigUpdatedEvent
       (consumers: DebuggerController.java:186-196; audio entries' onChange
        re-application per M6-01 §5.1/§5.3/§8 (A4, A11); LanguageConfigEntry i18n reload;
        options widgets call setConfig on user change)
   11. mods may lockConfig entries (advisory UI lock, §3.4)

SAVE (saveConfig call sites, GLOBAL unless noted)
   12. Config.switchFullScreen (Config.java:184-185)
       OptionsPresetsScreen "Edit Global" close (OptionsPresetsScreen.java:86-92)
       MainMenuScreen options close (:546-547: GLOBAL + CAMPAIGN)
       BattleHud battle-options close (:1498-1499: GLOBAL + CAMPAIGN)
       NewCampaignScreen options close (:153: GLOBAL only)
       NewCampaignScreen unload (:246: CAMPAIGN)
       SaveManager memcard flow (:280: CAMPAIGN)
       SAVE location: embedded in every .dsav write (M7-01 §12)
```

Notes:
- **Defaults travel with the file** (§3.2), so a `config.dcnf` written by one mod set is a self-describing snapshot for that set; a later run with fewer mods still skips unknown ids safely (§3.2 read step 2).
- The **`SAVE` location is per-save**, not per-campaign: two saves in one campaign can carry different SAVE-location values; editing per-save options in the main menu affects the currently loaded save only (the next `overwriteSave` rewrites its tail).
- `last_config_preset` (legacy `config.conf`) is the only UI meeting point of the two systems: preset pre-selection in both preset screens.
- **Mod reboots** (new-campaign mod editing, `NewCampaignScreen.java:132-141`) call `CONFIG.refreshPreset()` on return — the preset baseline re-check (§6.2) because registry defaults may have changed.

---

## 6. Presets (`.dpre`)

### 6.1 File format and location

- Directory: **`config/`** in the working dir (`ConfigPresetManager.configPath`, `ConfigPresetManager.java:59`); files matched by glob `*.dpre` (`:60`). (Not `presets/` — the i18n/plan wording is a misnomer; the on-disk name is authoritative.)
- `FORMAT_VERSION = 1` (`:57`); a preset **without** a `formatVersion` tag is still accepted as legacy (`validatePresetFormat`, `:170-175`).
- File contents: a single **`MapTag`** (the Tag model of M7-01 §2) with:
  ```text
  MapTag:
    "name"          StringTag   preset display name
    "formatVersion" IntTag      1  (new-format files only)
    "GLOBAL"        ListTag  --+  per location (all three, keys =
    "CAMPAIGN"      ListTag  --|  ConfigStorageLocation.name()), each
    "SAVE"          ListTag  --+  element a MapTag:
                                  "configId"      RegistryIdTag
                                  "valueType"     StringTag (getPresetValueType)
                                  "schemaVersion" IntTag   (getPresetSchemaVersion)
                                  "data"          RawTag   (the §2.3 bytes)
  ```
  (§3.3 describes the generic `saveConfig`/`loadConfig` MapTag pair used here.)
- **Write** — `savePreset(name, config)` (`:143-168`): builds the MapTag (all three locations of the passed collection), serializes into an `ExpandableFileData(256)`, and writes via `writePreset` (`:177-189`): **temp file + `ATOMIC_MOVE` (REPLACE_EXISTING), falling back to a plain move if the filesystem lacks atomic moves, `deleteIfExists` of the temp in `finally`** — preset writes are crash-safe (contrast §7.2).
- **Read** — `loadPresetList()` (`:96-113`): lists `config/`, filters `*.dpre` regular files, maps each to a `ConfigPresetEntry` with `temporaryName` = filename minus `.dpre`, resolved **asynchronously** (`CompletableFuture.supplyAsync`, common pool); `loadPreset` (`:120-141`): `MapTag.deserialize` → format validation → `name` tag → `new ConfigCollection()` + `ConfigStorage.loadConfig(config, location, tag)` for each location → `ConfigPreset(name, config)`. Failure → notification + `null` (the entry then shows as an invalid preset, `OptionsPresetsScreen.java:100-108`).

### 6.2 Built-in presets and `ConfigPresetState`

- `loadDefaultPresets()` (`:192-204`) — **posts `ConfigDefaultPresetsEvent` on every call** (the preset screens call it at construction) — and returns three **non-editable** (`editable=false`, `path=null`) built-ins:
  - `severed_chains` — plain all-defaults collection (`:206-208`);
  - `vanilla` — defaults **+ 12 explicit retail-behavior overrides** (`:210-226`): `extended_dragoon_actions=false`, `equip_effects_in_dragoon=false`, `save_anywhere=false`, `unlock_party=false`, `secondary_character_xp_multiplier=0.5`, `display_element_icon=false`, `icon_set=RETAIL`, `item_group_sort_mode=RETAIL`, `show_turn_order=false`, `quick_text=HOLD`, `run_by_default=false`, `encounter_rate=RETAIL`;
  - `speedrunner` — defaults **+ 5 overrides** (`:228-235`): `battle_transition_mode=INSTANT`, `transformation_mode=SHORT`, `auto_text=true`, `auto_text_delay=0.0`, `quick_text=INSTANT`.
  - *(There are no bundled `.dpre` resource files — the built-ins are code-defined collections, source-verified: none under `src/main/resources`.)*
- User presets: `config/*.dpre`; `presetExists(name)` (`:62-81`) is false for built-in names; `deletePreset` (`:83-94`) only for editable entries with a path (confirmation-gated in the UI).
- **`ConfigPresetState`** (`ConfigPresetState.java:18-70`) — "transient preset identity and immutable value baseline; never written to config files." Attached to a `ConfigCollection` via `setPreset(name)`:
  - the ctor snapshots the **serialized bytes of every registered entry** (`entry.serializer.apply(config.getConfig(entry))`, `:23-39`); any entry that fails to serialize stores `null` and marks the state **modified** (fail-closed: "an unreadable setting cannot establish an exact preset match");
  - `isModified(config, revision)` (`:41-48`) recomputes only when the collection's `revision` changed (lazy + cached);
  - `matches` (`:50-65`) requires the registry size to be unchanged and every entry's current serialized bytes to be equal (fail-closed on size drift or serialization error).
  - This is what powers the options flow's "modified?" handling (`ConfigCollection.isPresetModified()`, `:67-69`).

### 6.3 Options and new-campaign UI flows (layer 4)

- **`OptionsScreen`** (`OptionsScreen.java:36+`): one screen per `ConfigCategory`; keeps entries whose `category` matches, whose `storageLocation` is in the screen's valid-location set, that have an edit control, and (in battle) are `availableInBattle`. Each row: i18n label + `makeEditControl(config.getConfig(entry), config)` widget + optional help tooltip + lock indicator (§3.4). The advanced-options toggle is itself a config entry (`OptionsScreen.java:159`).
- **`OptionsPresetsScreen`** (`OptionsPresetsScreen.java:42+`): preset dropdown (built-ins + `loadPresetList()`), preselected via `Config.getLastConfigPreset()` (`:64-70`); "Edit Global" pushes an `OptionsCategoryScreen` over the **live `CONFIG`** restricted to `GLOBAL`, saving `config.dcnf` on close (`:86-92`); "Edit"/"Add" push an `OptionsCategoryScreen` over a **detached** `new ConfigCollection(false)` seeded from the preset's config (`copyConfigFrom`, `:107-111` / `:146-151`) restricted to `CAMPAIGN + SAVE`; on close a "save changes?" confirmation → `ConfigPresetManager.savePreset(name, config)` (new) or copy-back into the original preset's collection (edit, `:166-186`). Delete is confirmation-gated. The screen's `unload` callback hands the surviving preset list to the caller (used to rebuild the new-campaign dropdown after a mod reboot).
- **`NewCampaignScreen`** (§5 steps 6–7): preset dropdown with the same preselection rule; `applyPreset` copies the preset's `CAMPAIGN + SAVE` values into the live `CONFIG` and calls `CONFIG.setPreset(name)` (so the display name reflects the applied preset, including the "modified" suffix, `:107-109`); "Edit Presets" reboots all mods, opens `OptionsPresetsScreen`, then reboots with the chosen mod set and `CONFIG.refreshPreset()` (`:131-142`); the "Mods" button does the same for mod enablement (`:156-168`).

---

## 7. Cross-cutting notes

1. **No checksums** of `.dcnf`/`.dpre` content beyond the entry count / Tag structure; forward-compat is the skip-unknown-id rule (binary, §3.2) and the valueType/schemaVersion match with user-visible skip notifications (Tag, §3.3). *(Layer 4.)*
2. **Crash-safety is split**: `.dpre` writes use temp-file + atomic move (§6.1); `.dcnf` writes (`saveConfig(Path)`) and legacy `config.conf` writes are plain truncating `Files.write` — a power loss mid-write corrupts that file (a corrupt `config.dcnf` then loads as *empty* for its location, §3.2). The SAVE-tail shares the save file's non-atomicity (M7-01 §12). *(Layer 4.)*
3. **Event contract**: `ConfigUpdatedEvent` carries the **entry** (the consumer reads the value via `CONFIG`); `ConfigLoadedEvent` carries the **collection + location**; `ConfigDefaultPresetsEvent` carries the built-in preset list and fires on every `loadDefaultPresets()` call. `RENDERER.setFrameSkipOption` re-applies after every location load (§3.2 step 4).
4. **`availableInBattle`** gates the in-battle options screen — the only place the config system is aware of game state. *(Layer 4 UX.)*
5. **Implicit `.dcnf` size ceiling**: the Path save variant builds into a fixed 100 KiB buffer (§3.2).
6. **Mod-Loader boundary**: `Registrar`/`RegistryDelegate`/`RegistryEntry`/`MutableRegistry`/`RegistryEvent.Register`/`ModContainer` semantics are M8 (T-MOD) deliverables; M7-02 records only their config-side usage (§2.1, §3.4).
7. **Audio config consumers** (master/music/SFX/FMV volumes — M6-01 §5.1/§5.3, A11; music interpolation/pitch/granularity quality configs — M6-01 §8, A4) are ordinary `ConfigEntry`s of this system with `onChange` re-application; nothing audio-specific exists in the config machinery itself.

---

## 8. Classification table (D1–D10)

| ID | Claim | Layer | Category |
|---|---|---|---|
| D1 | Two coexisting config stores (legacy `config.conf` properties vs registry `.dcnf`); the only bridge is `Config.switchFullScreen` | 4 | `source-verified` |
| D2 | Legacy `config.conf` key table, sorted-store writer, 1..16 speed clamp with dual current/loaded state, the `setCounterOverlayRgb` wrong-key bug, the window-default mismatch (960/720 stored vs 640 read fallback) | 4 | `source-verified` |
| D3 | Entry model: pluggable serializer/deserializer/editControl/onChange/availableInBattle, preset value-type + schema-version model; 88 registered entries (CoreMod 80, LodConfig 7, TurnOrderConfigs 1) at reference revision `0c08c679b`; `ConfigRegistryEvent extends RegistryEvent.Register<ConfigEntry<?>>` | 4 | `source-verified` (supersedes the plan's 90 estimate) |
| D4 | Built-in codecs: bool 1B (lenient), int/float 4B native-order, string N-byte LE length + UTF-8 (N = `lengthSize`; no edit control), enum 1-byte length + UTF-8 name, colour 3B (Vector3f 0..1 → 0..255), campaign name 1-byte length + UTF-8 (CAMPAIGN) | 4 | `source-verified` |
| D5 | `.dcnf` layout (u32 count + {RegistryId u24+ASCII, u32 len, opaque bytes}*) with defaults written too; read: clear-then-load, skip-unknown-id, wrong-location entries discarded, `ConfigLoadedEvent` + frame-skip re-apply after **every** location load; missing file = silent clear; corrupt file = swallowed warning; 100 KiB write buffer | 4 | `source-verified` |
| D6 | Three locations: GLOBAL (`config.dcnf`, boot), CAMPAIGN (`campaign_config.dcnf` — CAMPAIGN section only in practice; written at creation/options-close/memcard flow), SAVE (per-save `.dsav` tail); the SAVE location is per-save, not per-campaign | 4 | `source-verified` |
| D7 | `ConfigCollection` semantics: `notifyChanges` flag, explicit-values map with default fallback, revision counter, `setConfig` = put + onChange + ConfigUpdatedEvent (event after hook), advisory mod locks (StackWalker caller → translated mod names; UI-disabled, never thrown), embedded preset identity with `refreshPreset` | 4 | `source-verified` |
| D8 | Presets: `config/*.dpre` Tag files (MapTag {name, formatVersion?, 3 location ListTags of {configId, valueType, schemaVersion, data}}); FORMAT_VERSION 1 with legacy acceptance; schema-validated load with per-entry skips + user notification; built-ins are code-defined (vanilla = 12 retail overrides, speedrunner = 5) and non-editable; async list loading; temp-file + atomic-move writes; `ConfigPresetState` = serialized-byte baseline with lazy revision-gated fail-closed match | 4 | `source-verified` |
| D9 | Options UI: per-category screens, edit-control binding with help tooltips and advisory lock indicators, in-battle availability gate, preset edit on detached (`notifyChanges=false`) collections with save-changes confirmation, new-campaign preset apply (`CAMPAIGN+SAVE` copy + `setPreset`) with mod-reboot refresh | 4 | `source-verified` |
| D10 | Crash-safety split: `.dpre` atomic (temp + ATOMIC_MOVE, fallback) vs `.dcnf`/`config.conf` truncating writes (non-atomic); corrupt `config.dcnf` loads as empty | 4 | `source-verified` |

**Register impact:** none opened/closed by this deliverable. The plan's config-entry count (90) is superseded by the verified 88 (D3) — recorded here rather than in the register (a plan correction, not an open question).

**Cross-document notes:** M7-01 §6.3/§8.3/§9.3 (the SAVE-location tail is part of every save version's file; `SavedGame.config` is the deserialized tail collection copied into `CONFIG` at `SaveManager.loadGameState:408-409`), M6-01 §5.1/§5.3/§8 (A4, A11; audio config consumers), M8 (T-MOD — `Registrar`/registry/event machinery, `RegistryEntry` translation keys), M10 (evidence worklist — none; all layer 4).

**No `asset-validated` claims** — no retail config/preset files exist for this system (it is reference-defined end to end); all claims above are `source-verified` from the reference serializers and consumers.
