# M8-01 — Mod Discovery and Loading

**Milestone:** M8 (Modding and Extensibility) — deliverable 1 of 3.
**Scope:** mod discovery (jar scanning, `@Mod` metadata, semver gating), instantiation and lifecycle states, the re-entrant `bootMods` sequence, resource namespacing and locale detection, failure/wrong-version reporting surfaces, mod selection and the campaign↔mod binding, and the observable contract of the Java-specific mechanisms (plan research question 7, audit F-04).
**Primary sources:**
- `Mod-Loader` (reference revision `c692134`, 33 files): `ModManager`, `Mod`, `ModContainer`, `ModState`, `Latch`, `IoHelper`, `MissingRequiredModException`, `ModNotLoadedException`, `LocaleSpecificity`.
- `Severed-Chains` (reference revision `0c08c679b`): `legend/core/GameEngine.java` (`MODS` at `:94`, discovery at `:245-246`, `bootMods` at `:306-353`, `onModError` at `:354-357`, `loadLang` at `:359-363`), `legend/core/Version.java`, `legend/game/Main.java:60-110` (crash catch), `legend/game/inventory/screens/ModsScreen.java`, `CampaignSelectionScreen.java`, `NewCampaignScreen.java`, `legend/game/title/Ttle.java:558-576`, `legend/game/Scus94491BpeSegment.java:573-583`, `legend/game/saves/SaveManager.java:232`, `legend/game/modding/coremod/config/EnabledModsConfigEntry.java`, `legend/game/ui/GameOverlay.java:37-41`.
- M1 precedent: `research/platform/01-application-lifecycle.md` §3–§4 (startup order and the `bootMods` call-site census — this document deepens §4).

**Layering:** the modding framework is entirely **layer 4** (reference implementation) — the retail game has no mod system. The closest retail analogs are the data tables the core mods later encode (M8-03) and the PSX memory-card slot model (M7-01 §11). Where the framework interacts with layer-3 facts (retail content shapes), those are documented under M8-03.

**Evidence classification:** all claims are **source-verified** from the pinned reference code. No `asset-validated` claims (no mod jars or campaign files are available in this environment).

---

## 1. Mod identity and discovery

### 1.1 Inputs

- **Directory:** `./mods` in the working directory — created if missing (`Files.createDirectories`, `ModManager.Access.findMods`).
- **Unit:** every `*.jar` file directly in that directory (`DirectoryStream` glob; no subdirectories, no non-jar packages). There is no per-mod packaging requirement beyond a jar; a single jar may contain multiple `@Mod`-annotated classes, and the same class location may be associated with several mod ids (`pathToModId` maps a code-source location to a `Set<String>`, `ModManager.java:37`).
- **Game version:** `Version.VERSION` = `"3.0.0"` (`Version.java:11-18` — `MAJOR.MINOR.REVISION` strings; `BUILD`/`HASH`/`CHANNEL` are build-injected placeholders `"SNAPSHOT"`/`"COMMIT"`/`"CHANNEL"` in source).

### 1.2 Classpath construction

`findMods` builds **one** `URLClassLoader` over **all** discovered jars, parented to the `ModManager`'s own class loader (`ModManager.java:105`). There is no per-mod class loader: every mod class is visible to every other mod and to the engine (no class-loading sandbox — §7). The framework's recent history moved mod attribution off class loaders: commit `3a897a7` "Use protection domain for mod tracking instead of individual classloaders" and the pinned `c692134` "Improve mod id tracking" — the tracking key is `modClass.getProtectionDomain().getCodeSource().getLocation().toString()` (`ModManager.java:36-37`).

### 1.3 Annotation scan

Discovery uses the `org.reflections` library (`Reflections.getTypesAnnotatedWith(Mod.class)`), configured over **both** worlds (`ModManager.java:112-116`, plus the shared helper `ModManager.addModsToReflectionsConfig`, `:219-224`):

- the engine project's own classpath root + the `legend` package (so `CoreMod`/`LodMod` are found when running inside the Severed-Chains project),
- the mod `URLClassLoader` + the jar URLs (so mods in `./mods` are found).

The in-source comment records the dual deployment: *"Find mods in the current project (finds CoreMod in SC, mod in SCDK)"* — the Mod-Loader is also embedded in the mod-development kit, where the "project" classpath is the mod author's workspace and `findJarResources` resolves resources from a sibling `resources/` directory instead of a jar (§4.3).

### 1.4 The `@Mod` annotation and per-mod gates

`@Mod` is a RUNTIME-retention annotation with exactly two elements (`Mod.java:6-10`):

```java
@Retention(RetentionPolicy.RUNTIME)
public @interface Mod {
  String id();      // lowercase slug, e.g. "lod_core"
  String version(); // semver *range* the game version must satisfy
}
```

Per candidate class (`ModManager.java:123-153`), in order:

1. **Semver gate** — `new Semver(gameVersion, Semver.SemverType.NPM).satisfies(modAnnotation.version())` (`semver4j`). A mod's `version` is a range over the *game* version; the in-repo core mods all declare `^3.0.0` (caret range, NPM semantics: `>=3.0.0 <4.0.0`) — `CoreMod.java:94`, `LodMod.java:113`, `TurnOrderMod.java:42`. Mismatch → `wrongVersion.put(id, version)` + skip (warning log "is for another version!").
2. **Duplicate mod id** — already in `allModClasses` → error log + `failedToLoad.put(id, "Duplicate mod ID")` + skip.
3. **Registration** — `allModClasses[id] = class`, `allModUrls[id] = code-source URL`, `pathToModId[url].add(id)`.
4. **`IncompleteAnnotationException`** (an annotation element missing at read time — e.g. a jar built against an older `@Mod` shape) → treated as a version problem: `wrongVersion.put(classSimpleName, "Unknown")`.
5. **Any other `Throwable`** while reading/registering → `failedToLoad.put(classSimpleName, "Failed to load")`. Note the failure maps key by **class simple name** here, but by **mod id** in the instantiation phase (§2.2) — the two keys are not uniformly comparable.

After the scan: **required-mod check** — `allModIds` (the successfully registered set, so wrong-version mods do *not* count) must contain `requiredModIds`; otherwise `MissingRequiredModException("Missing required mods (…)")` is thrown (`ModManager.java:156-160`). Severed-Chains' required set is **`"lod"` and `"lod_core"`** (`GameEngine.java:94`). Because discovery runs once at `GameEngine.start()` (`:245`), a missing required mod surfaces in `Main.main`'s catch (`Main.java:66`) as a **hard boot failure**: crash log (with the mod census — "Loaded mods"/"All mods"/"Failed to load mods"), `CRASHLOG` copy, `exit(1)` — no title screen, no UI (M7-01 §12 documents the catch's tail).

### 1.5 Re-scan hazard (source-verified, latent)

`findMods` clears `allModUrls`, `wrongVersion`, `failedToLoad`, `pathToModId` but **not** `allModClasses` (`ModManager.java:107-110`). A second `findMods` call would therefore flag every previously found mod as "Duplicate mod ID". This is latent only because Severed-Chains calls `findMods` exactly once per process (at `GameEngine.start()`, `:245`); all later reboots go through `loadMods` against the cached `allModClasses` (§2). `reset()` (`ModManager.java:168-171`) likewise leaves `allModClasses` intact. **Observation, no determination made** — a replacement design must decide whether discovery is a one-shot or a repeatable operation.

---

## 2. Loading and instantiation

### 2.1 `loadMods(modIds)`

`Access.loadMods()` (no-arg) loads **all** discovered mods; `loadMods(Set<String>)` loads the requested set **plus the required ids unconditionally** (`modsToLoad = modIds ∪ requiredModIds`, `ModManager.java:173-190`). For each id: present in `allModClasses` → `instantiateMod`; absent → added to the returned **`missingModIds`** set. The caller is responsible for the missing-mods UX (§5.3); the framework itself does not throw for missing *optional* mods.

### 2.2 `instantiateMod`

```java
new ModContainer(modId, allModClasses.get(modId).getDeclaredConstructor().newInstance())
```
(`ModManager.java:191-198`) — a **no-arg constructor**, called with no lifecycle hook: the mod class's static initializers (which run the registrar `register(...)` calls, M8-02 §3) execute at class-load, and the instance carries no `init()` method contract. On any `Throwable` (`ModManager.java:197-205`): warning log "FAILED TO LOAD MOD", `failedToLoad.put(id, "Failed to create mod instance")`, and **removal from the discovered caches** — `allModClasses.remove(id)`, `allModUrls.remove(id)`, and `pathToModId.remove(id)`. The last call is a **no-op** (the map is keyed by path, not mod id — `ModManager.java:37`), so the path→ids set retains the failed id; source-verified observation. After removal, the mod is gone from `getAllModIds()` — a failed mod cannot be retried in a later `loadMods` without a process restart (the class is still loaded in the shared class loader, but the framework no longer tracks it).

### 2.3 `ModContainer` and lifecycle states

Fields (`ModContainer.java:19-25`): `modId`, `mod` (the instance), `classLoader` (the *shared* URLClassLoader — identical for every mod in `./mods`), `state`.

- `ModState` is a two-value enum (`ModState.java:3-10`): `INITIALIZED` (set by the `ModContainer` ctor) → `READY` (set by `loadingComplete()`, `ModManager.java:207-211`, after all instantiations). `isReady()` is the only query. There is no `FAILED` state — failures are removed from the container map entirely (§2.2) and tracked in the `failedToLoad` map.
- **Resource namespacing:** `getResource(path)` = `classLoader.getResource(modId + '/' + path)` (`ModContainer.java:61-63`) — a mod's resources live under `<modId>/` inside its jar. The engine never accesses another mod's namespace directly; cross-mod access is by registry id (M8-02).
- **Supported locales:** computed **once at construction** (`ModContainer.java:27-41, 43-58`): `IoHelper.findJarResources(mod.getClass(), modId + "/lang")` + the pattern `^([a-z]+)(?:_([A-Z]+))?\.lang$` → unmodifiable `List<Locale>` (`en.lang` → `en`; `en_CA.lang` → `en_CA`).
- `getLangResource(locale)` returns the resolution chain as a `Map<LocaleSpecificity, URL>`: `LANGUAGE_COUNTRY` → `LANGUAGE` → `FALLBACK` (always `lang/en.lang`), each present if found (`ModContainer.java:74-96`; the Javadoc documents the exact 4-way order incl. the "null if en isn't found" case). The consumer is `LangManager` (M8-03 §5).
- **Active-mod statics:** `activeManager` + `activeModClass` are **process-wide statics** on `ModContainer` (`ModContainer.java:24-25`, set by `ModManager.setActiveMod`, `ModManager.java:86-88`); `isModActive(modId)` tests whether the active handler class belongs to that mod via `pathToModId` (`ModContainer.java:103-106`). This is the mechanism behind registry namespace ownership (M8-02 §4.2) and mod attribution for config locks (M7-02 §3.4) and error reporting (M8-02 §6). It is a plain mutable static — **thread affinity assumed (main thread)**, no synchronization, and it survives reboots until the next dispatch (audit F-04's "process-wide static active mod", §7).

---

## 3. The re-entrant boot: `bootMods`

`GameEngine.bootMods(Set<String> modIds)` (`GameEngine.java:306-356`) is the single boot routine, invoked at startup and on every mod-set change. (M1 `platform/01` §4 documents the 9-step sequence; the line anchor there, "GameEngine.java:289-335", drifts from the pinned source, where the method spans `:306-356` — corrected in the M8 reconciliation.) The steps, with the M8-level detail:

1. `MOD_ACCESS.reset()` — clear `loadedModUrls` + `loadedModInstances` (discovered classes kept, §1.5).
2. `EVENT_ACCESS.reset()` — clear all event bindings + stale set (M8-02 §6.4).
3. `REGISTRY_ACCESS.reset()` — unlock all registries, clear delegate latches, clear entries (M8-02 §4.4).
4. `missingMods = MOD_ACCESS.loadMods(modIds)` — instantiation (§2); per-mod failures recorded, not thrown.
5. `EVENT_ACCESS.initialize(MODS)` — reflectively re-discover **all** `@EventListener` classes over the engine classpath *and* the loaded mods (`EventManager.initialize`, M8-02 §6.1).
6. `EVENTS.postEvent(new AddRegistryEvent(REGISTRIES))` — mods register **custom** registries (M8-02 §4.5); the event is *critical* (M8-02 §6.5).
7. `REGISTRY_ACCESS.initialize(...)` for the **eight registries the title screen needs**: `config`, `campaignTypes`, `engineStateTypes`, `inputActions`, `characterTemplates`, `statTypes`, `statModTypes`, `goods` — the in-source comment for `goods` is explicit: *"We need to boot the goods registry for save cards on the title screen"* (`GameEngine.java:325-334`). The remaining registries are initialized later by `bootRegistries()` (`GameEngine.java:365-370` — `REGISTRY_ACCESS.initializeRemaining()` in insertion order + icon-map/atlas work) at new-campaign start and other full-boot points.
8. `MOD_ACCESS.loadingComplete()` — all containers → `READY`.
9. `InputBindings.initBindings()` + `InputBindings.loadBindings(CONFIG)` (M1-05; re-runs on every boot, so mod-contributed default bindings are rebuilt per mod set).
10. `loadLang()` (`GameEngine.java:362-366`) — reset + reinitialize i18n for the configured locale + reload `lang/` overrides.
11. **User-visible failure reporting** — for every `failedToLoad` id: `GameOverlay.addNotification(7, I18n "lod_core.ui.mods.error" [modId])`; for every `wrongVersion` id: `… "lod_core.ui.mods.wrong_version" [modId]` (`GameEngine.java:344-349`). `addNotification(seconds, text)` takes a **duration in seconds** (`GameOverlay.java:37-41`) — these are 7-second toast notifications.
12. Returns `missingMods` to the caller.

**Re-entrancy is a first-class contract, not a startup phase.** Call sites (12, source-verified):

| Call site | Mod set | Purpose |
|---|---|---|
| `GameEngine.java:246` (start) | all | boot for the title screen |
| `Scus94491BpeSegment.java:582` (decompiled `main`, `@Method(0x800e5d44)`) | all | the retail game-entry path (new game from the campaign flow) |
| `Ttle.java:570` (title, `finally` after memcard split) | all | restore the full mod set after the memcard flow (§5.4) |
| `SaveManager.java:232` (memcard split) | **`Set.of()`** — required mods only | the split operates without optional mods |
| `CampaignSelectionScreen.java:133` | campaign's `ENABLED_MODS` | per-campaign mod set on campaign entry |
| `CampaignSelectionScreen.java:136` | all (fallback) | old campaigns without the config key — the fallback also **records** all-mods as the campaign's enabled set (migration) |
| `CampaignSelectionScreen.java:169, 255` | all | return to title / save-list exit restores the global set |
| `NewCampaignScreen.java:130/134, 160/163, 260` | all → selected, selected → all | the "Edit Presets" / "Mods" buttons reboot with all mods, then with the chosen set; screen unload reboots all |

The observable consequences: events, registries, input bindings, and language are **rebuilt from scratch on every boot**; different campaigns can run with different mod sets; the title screen always runs with all mods.

---

## 4. Failure, version, and missing-mod reporting

### 4.1 Framework-recorded state

| Map | Key | Values (source literals) | Set when |
|---|---|---|---|
| `wrongVersion` | mod id (or class simple name for `IncompleteAnnotationException`) | the mod's declared version range, or `"Unknown"` | discovery (§1.4) |
| `failedToLoad` | class simple name (scan) / mod id (instantiation) | `"Duplicate mod ID"`, `"Failed to load"`, `"Failed to create mod instance"` | discovery / instantiation |

Both are exposed read-only (`getWrongVersions`, `getFailedToLoad`) and are **cleared only by a re-`findMods`** — i.e. once per process.

### 4.2 User-visible surfaces

1. **Boot toasts** — 7-second `GameOverlay` notifications per failed/wrong-version mod (§3 step 11).
2. **ModsScreen** (`ModsScreen.java:33-80`) — the mod-selection screen lists `getAllModIds()` sorted by **translated name** (`I18n.translate(modId + ".name")`); required mods render without a checkbox plus a "required" tooltip; failed and wrong-version mods render as **red rows with the recorded reason** (i18n keys `lod_core.ui.mods.error` / `…wrong_version`).
3. **Crash log** — the `Main.main` catch dumps "Loaded mods" / "All mods" / "Failed to load mods: - id: reason" (`Main.java:95-104`) — the mod census is part of every crash report.
4. **Missing required mod** — no UI: `MissingRequiredModException` propagates out of `GameEngine.start()` into the same crash catch (§1.4).

---

## 5. Mod selection and the campaign↔mod binding

### 5.1 ModsScreen

Constructed with the mutable `enabledMods` set (`ModsScreen.java:24-28`): each non-required mod gets a checkbox bound to that set (`onChecked`/`onUnchecked` mutate it); the caller reboots with the resulting set (NewCampaignScreen §3 table). The screen is also where the §4.2(2) error rows are shown.

### 5.2 Campaign entry (`CampaignSelectionScreen`, `:108-160`)

- **Known-mods diff:** `knownMods` = the campaign's `KNOWN_MODS` config, else its `ENABLED_MODS`, else the currently loaded set. If the current mod set ≠ known mods → confirmation box `lod_core.ui.campaign_selection.installed_mods_changed` (YES → `changeMods` flow, which offers the ModsScreen; NO → proceed with the current set).
- **`bootModsAndLoadSaves`:** campaign has `ENABLED_MODS` → `bootMods(Set.of(...))`; **fallback for old saves from before the key existed** (in-source comment) → `bootMods(all)` and **write all-mods into the campaign's `ENABLED_MODS`** (in-place migration).
- **Missing mods:** non-empty return → confirmation box `lod_core.ui.campaign_selection.missing_mods_confirm` (YES → load the saves anyway; NO → back to selection). Loading with missing mods is *allowed* with user consent.
- **`loadSaves`:** refreshes the campaign's `KNOWN_MODS` to the current full set and **saves the CAMPAIGN section to `campaign_config.dcnf`** (`CampaignSelectionScreen.java:149-157`) — mod-set knowledge is persisted with the campaign (M7-02 §5).

### 5.3 The binding entries

`ENABLED_MODS_CONFIG` and `KNOWN_MODS_CONFIG` (`CoreMod.java:163-164`) are both **`EnabledModsConfigEntry`** — `String[]`, **CAMPAIGN** location, `OTHER` category (`EnabledModsConfigEntry.java:8-11`). Codec: **u16 count + NUL-terminated ASCII ids** (each id with a 1-byte length prefix, `:13-45`); the deserializer applies **`replace("lod-core", "lod_core")`** per id (`:43`) — a read-side alias for the historical mod-id spelling, the same pattern as the registry-id alias documented in M7-01 §2. The new-campaign flow records both entries at campaign creation (M7-02 §5 step 7).

### 5.4 The memcard flow's reduced boot

`SaveManager.splitMemcards` reboots with **only the required mods** (`SaveManager.java:232` — `bootMods(Set.of())`), and the title screen's `finally` clause restores all mods afterwards (`Ttle.java:570`) — a deliberate, self-contained reduced-mod window for the PSX-import path (M7-01 §11.2).

---

## 6. Resource namespacing and locale detection (summary)

- Mod resources are namespaced by **directory prefix `<modId>/`** inside the jar (`ModContainer.getResource`); the engine's own resources are at the jar/project root.
- Lang files: `<modId>/lang/<lang>.lang` / `<lang_CC>.lang`; supported locales are detected **at container construction** by directory listing (`IoHelper.findJarResources`, `IoHelper.java:18-57` — directory mode for the SCDK's sibling `resources/` tree, jar-entry mode for jars; entries matched by `startsWith(path)`, file name returned).
- Resolution order at read time: `LANGUAGE_COUNTRY` → `LANGUAGE` → `FALLBACK` (`en`) → absent (`ModContainer.getLangResource`). The consumer (`LangManager`) and the file format are M8-03 §5.
- Mod display names and error strings come from i18n keys `<modId>.name` / `lod_core.ui.mods.*` — i.e. a mod's own identity strings are **localizable by the mod itself** (M8-03 §5).

---

## 7. Java-specific mechanisms: observable contract vs implementation (plan Q7; audit F-04)

| # | Mechanism (Java implementation) | Observable contract (a replacement must preserve) | Implementation detail (avoidable) |
|---|---|---|---|
| J1 | Single `URLClassLoader` over all `./mods/*.jar`, parented to the engine loader | All mods are discovered and visible to each other and to the engine; **there is no isolation** — a mod can reference any engine or other-mod class at compile time and run with full JVM privileges | The loader object, class-file parsing, hot-swap behavior |
| J2 | `org.reflections` annotation scan over project classpath + jars | `@Mod(id, version)` classes are found by annotation, not by manifest/config; mods are declared in code, not in a directory manifest file | The Reflections library, classpath duality (SC vs SCDK) |
| J3 | `getDeclaredConstructor().newInstance()` — no-arg constructor, no init hook | A mod is a *class* instantiated exactly once per boot; state lives in statics + the instance; there is no explicit init/shutdown API | Constructor reflection; static-initializer ordering at class load |
| J4 | Process-wide static active mod (`ModContainer.activeManager/activeModClass`) set around each event-handler dispatch (nested via a handler stack) | (a) registry registration is only legal while the owning mod's handler is executing (namespace ownership, M8-02 §4.2); (b) errors and config locks are attributable to a mod by class→path→mod-id lookup; (c) the attribution survives nested dispatches (stack, not overwrite) | The static itself (thread-affinity assumption; no synchronization), the `pathToModId` map, protection-domain keying |
| J5 | Weak references: `RegistryDelegate` storage in a commons-collections `ReferenceMap`, instance event listeners in `WeakReference` (`EventBinding.java:12`), delegate value memoized in a `Latch` | Invalidated references degrade gracefully: an absent entry → `ModNotLoadedException` with id + registry; a GC'd listener instance → binding marked invalid, deferred removal (`staleListeners` + `clearStaleRefs()`, called from the render loop at `RenderEngine.java:603`) | The specific reference types; the GC-timing dependency |
| J6 | `ModManager` caches (`allModClasses` never cleared after discovery; `reset()` keeps them) | Within a process: discovery is one-shot; reboots re-instantiate the same classes with **fresh instances but persistent mod static state** (a mod's static fields survive reboots — source-verified consequence, no in-repo mitigation) | The map layout; the latent duplicate-id hazard of a second `findMods` (§1.5) |
| J7 | `RuntimeException` taxonomy (`MissingRequiredModException`, `ModNotLoadedException`, `DuplicateRegistryIdException`, `IncorrectModIdException`, `RegistryLockedException`, `EventHandlerException`) | Failure *categories* with the documented user surfaces (§4): hard-fail (missing required, critical-event handler throw, registry init), toast+continue (instantiate failure, non-critical handler throw), row-in-list (wrong version, duplicate) | The exception classes themselves |
| J8 | No unloading | "Unloading" a mod = rebooting without it; its classes remain in the shared class loader for the process life; registries/events are rebuilt, but mod *static* state is not reset (J6) | — |

**Security boundary:** there is none. The framework provides mod *attribution* (J4) and *namespace ownership* (M8-02 §4.2) but no sandbox, no capability set, no permission model — a mod's failure modes are managed (§4, M8-02 §6.5), not prevented. Any Rust/C-ABI design decision about plugin trust is a **future design choice**, not an inherited requirement (recorded here per §7 of the M8 authorization; no conclusion drawn).

---

## 8. The language-neutral mod contract (verification criterion)

**Discovery inputs:** a directory of jars; per-jar, zero or more `@Mod`-annotated classes; a game version string.
**Lifecycle states (observable):** *discovered* (id in `getAllModIds`) → *loaded* (instantiated, state INITIALIZED) → *ready* (state READY, after `loadingComplete`) — plus the side-channel states *wrong-version* (never loaded, in `wrongVersion`), *failed* (in `failedToLoad`; removed from discovered), *missing* (requested but undiscovered; returned by `loadMods`). There is no per-mod *unloaded* state (J8).
**Registration semantics:** a mod records entry suppliers in statics at class-load; entries are materialized and namespace-checked during its registry's register-event dispatch; registries then lock (M8-02 §4).
**Event semantics:** class-level `@EventListener` classes with one-arg `@EventListener` methods (static, or instance-registered); priority-ordered delivery (higher first, equal-priority order undefined); per-handler error isolation with mod attribution, except critical events which rethrow (M8-02 §6).
**Failure modes:** the §4.1 taxonomy with the §4.2 surfaces; required-mod absence is the only pre-UI hard failure.
**Persistence coupling:** the campaign stores `enabled_mods`/`known_mods` string arrays (CAMPAIGN config location, M7-02 §5); save files carry per-entry registry ids of whatever mod set created them (M7-01 §2/§6).

---

## 9. Classification table (D1–D9)

| ID | Claim | Layer | Category |
|---|---|---|---|
| D1 | Discovery = `./mods/*.jar`, one shared URLClassLoader, Reflections `@Mod` scan over project + jar classpaths; dual SC/SCDK deployment recorded in-source | 4 | `source-verified` |
| D2 | `@Mod{id, version}` with NPM-semver gate over game version 3.0.0; in-repo core mods declare `^3.0.0`; wrong-version → skip + map | 4 | `source-verified` |
| D3 | Required mods = `lod` + `lod_core`; missing → `MissingRequiredModException` → hard boot failure (crash log + exit, no UI) | 4 | `source-verified` |
| D4 | Instantiation = no-arg ctor, no init hook; failure → `failedToLoad` + removal from discovered caches (incl. the no-op `pathToModId.remove`); two-state lifecycle INITIALIZED/READY; no unloading | 4 | `source-verified` |
| D5 | `bootMods` is a re-entrant 12-step rebuild (framework reset → load → event init → AddRegistryEvent → 8 menu registries → READY → bindings → lang → toasts → missing return); 12 call sites incl. the required-only memcard boot and per-campaign sets | 4 | `source-verified` |
| D6 | Resource namespacing by `<modId>/` jar prefix; locale detection at container construction; `LANGUAGE_COUNTRY → LANGUAGE → FALLBACK(en)` resolution chain | 4 | `source-verified` |
| D7 | Reporting surfaces: 7-s boot toasts, ModsScreen red rows (sorted by translated name, required locked), crash-log mod census, consent boxes for installed-mods-changed / missing-mods on campaign entry; `enabled_mods`/`known_mods` CAMPAIGN string-array entries with the `lod-core`→`lod_core` read alias | 4 | `source-verified` |
| D8 | The J1–J8 mechanism table: observable contract vs implementation detail; no sandbox/capability model exists; attribution via the process-wide static active mod (thread-affinity assumed) | 4 | `source-verified` (the "no determination" items marked as observations) |
| D9 | Mod static state persists across reboots (J6) and discovery is one-shot per process (J6, §1.5 latent hazard) | 4 | `source-verified` consequence |

**Register impact:** none opened by this deliverable (the latent re-scan hazard §1.5 and the static-state persistence D9 are documented observations about reference behavior, not open questions — they bind any replacement design directly).

**Cross-document notes:** M1 `platform/01` §4 (this document deepens it; the `bootMods` line anchor there is corrected in the M8 reconciliation), M7-02 §3.4/§5 (advisory mod locks and the campaign config flows use the attribution + binding mechanisms documented here), M7-01 §2 (the `lod-core`→`lod_core` alias appears for both registry ids and mod ids), M8-02 (registry/event contract), M8-03 (core mods, i18n, debugger, script-patch modding).

**No `asset-validated` claims** — no mod jars or campaign files are available; all claims above are `source-verified` from the pinned reference code.
