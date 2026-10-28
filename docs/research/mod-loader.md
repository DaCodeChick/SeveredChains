# Mod-Loader Repository Research

**Repository:** `~/Documents/GitHub/Mod-Loader`
**Commit:** `c692134907a070a8b2c7db7b2b779010bc16deb9` (branch `main`, 2026-10-11, "Improve mod id tracking")
**Artifact:** `org.legendofdragoon:mod-loader:4.3.5` (matches the version consumed by
Severed-Chains `build.gradle`) — verified.
**License:** AGPL-3.0 (verified, same FSF text as Severed-Chains).
**Read-only reference — do not modify.**

This document resolves the Phase 0 open question *"external mod-loader
internals not in the reference repo; API reconstructed from usage."* All
behavior below is **verified** from source unless marked otherwise.

## 1. What it is

A small (~1,390 LOC, 30 files) dependency-free-ish library providing the mod
platform primitives the Severed Chains game uses: **mod discovery/loading
(`ModManager`)**, **event bus (`EventManager`)**, **registries
(`Registry`/`Registrar`/`RegistryId`)**, and **i18n (`LangManager`)**. It is
Java-reflection-based (annotations, classpath scanning) — in the native port
these become explicit APIs (see §7).

Dependencies (its own build.gradle): commons-collections4 4.4 (only for
`ReferenceMap`), semver4j 3.1.0 (NPM-semver ranges), log4j, reflections
0.10.2 (classpath scanning), jsr305 (annotations). Java 21 target.

## 2. Mod model (verified)

- **`@Mod(id, version)`** annotation (runtime retention) on a mod's main
  class. `version` is an **NPM-style semver range** (semver4j
  `Semver.SemverType.NPM`) matched against the *game's* version string —
  i.e. a mod declares which game versions it supports, e.g. `^1.0.0`.
  Mismatch → mod reported in `wrongVersion` map, **skipped** (not fatal).
- **Discovery = `ModManager.Access.findMods(modsDir, gameVersion)`:**
  - All `*.jar` files in `mods/` go into **one shared `URLClassLoader`**
    (parent = app classloader). **There is no per-mod classloader
    isolation** — all mods see each other's classes. (This is a Java
    limitation the native design removes; see §7.)
  - `reflections` scans the app classpath (the `legend` package — finds
    `CoreMod` and `LodMod` built-in mods) **plus** all mod JARs for
    `@Mod`-annotated classes. Built-in mods and JAR mods are first-class
    citizens in the same system.
  - **Duplicate mod id** → error logged, mod in `failedToLoad`, skipped.
  - `IncompleteAnnotationException` (old mod without `version`) → treated as
    wrong version.
  - **`requiredModIds`** (constructor arg; Severed-Chains passes its core
    built-ins, e.g. `lod`) must be found or `MissingRequiredModException`
    (fatal at boot).
- **Loading = `loadMods([modIds])`:** requested set ∪ required set;
  instantiate via no-arg constructor (`ModContainer` wraps `modId` + instance
  + classloader). Constructor failure → mod in `failedToLoad`, **game
  continues** (the failure is reported in UI). `loadingComplete()` flips all
  containers to `ModState.READY`.
- **`pathToModId` tracking:** maps code-source URL → set of mod ids in that
  JAR (the 2026-10-11 "Improve mod id tracking" commit). This is how event
  error attribution maps a listener class → its mod. **Quirk:** all mods in
  one JAR share the code source, so a failing handler attributes to *all*
  mods in that JAR (documented limitation to preserve/fix consciously in
  native — the native design can attribute precisely per-mod, §7).
- `setActiveMod(cls)` sets the "currently executing mod" (used for
  `ModContainer.isModActive(modId)` checks inside event handlers — e.g. a
  mod checking whether *its* handler is the one running).

## 3. Event system (verified)

- **`Event`** (base): mutable `propagate` flag; `stopPropagation()` stops
  further *handlers* (loop still runs for stale-binding cleanup);
  `isCritical()` (protected, default false) — **critical events re-throw
  handler exceptions** as `EventHandlerException` (fatal); non-critical
  handler exceptions are caught → logged + `errorCallback(modId, throwable)`
  (Severed-Chains uses this to disable the offending mod and notify the
  user).
- **`CancelableEvent`**: `cancel()` = `stopPropagation()` + `canceled` flag.
- **`@EventListener(priority = Priority.NORMAL)`** on methods:
  - Listener discovery = reflection scan (same classpaths as mod discovery),
    method must have **exactly one parameter** which must extend `Event`,
    must be **static** (instance listeners exist via `register(Object)` —
    held by `WeakReference`; collected instances mark the binding *invalid*
    → lazily removed via `clearStaleRefs()`, which the game calls
    periodically).
  - **Priority enum: LOWEST < LOW < NORMAL < HIGH < HIGHEST; higher runs
    first; equal priority = undefined order** (in practice registration
    order). Javadoc: mods should only use high/low priority when they need
    to.
  - Dispatch: `postEvent(event)` under `synchronized(listeners)`; iterates
    that event *class's* bindings (exact class match, no inheritance
    dispatch — an event subclass does NOT trigger base-class listeners);
    pushes the listener class on an `eventHandlerStack` (used to restore the
    active mod after dispatch) and sets the active mod for the handler's
    duration.
  - **Reentrancy:** dispatch is fully synchronous on the calling (game)
    thread — no worker threads, no async delivery. A handler may post other
    events (the stack handles it).
- **`RegistryEvent`** (events/registries): a small bridge event whose
  `register(id, entry)` method is called by `Registrar` during boot so mods
  can supply entries to core registries (boot ordering mechanism — see
  modding-analysis.md for the catalog of registered events).

## 4. Registries (verified)

- **`RegistryId`** = immutable `modId:entryId`:
  - Both parts must match `[a-z][a-z0-9_-]*` (lowercase, digits, `_`, `-`).
    *Note:* the error message claims "at least 3 characters" but the pattern
    enforces 1+ — a Java bug/quirk; the native port should pick one behavior
    (recommend: enforce 1+, match the pattern, fix the message).
  - **Full id ≤ 255 chars** (Tag string-length safe — saves store registry
    ids).
  - `equals`/`hashCode` value-based.
- **`Registry<Type extends RegistryEntry>`**:
  - `entries: Map<RegistryId, Type>` (materialized) +
    `delegates: Map<RegistryId, RegistryDelegate<Type>>` as a **weak
    `ReferenceMap`** — delegates are created lazily on *any* `getEntry`
    (even for missing entries) and garbage-collected when unused; the
    delegate dereferences the supplier → on access, if absent, throws
    `ModNotLoadedException`/`RegistryLockedException` depending on phase.
  - This lazy-delegate design is what lets the game reference
    `registry.getEntry(id).get()` anywhere (e.g. battle code resolving an
    addition id at runtime) while deferring the failure to use-time with
    full mod attribution.
- **`Registrar<Type, EventType>`**: per-(registry, mod) registration buffer:
  - `register(entryId, Supplier<T>)` — **lazy supplier**, not the value;
    duplicate entry id for the same mod → `DuplicateRegistryIdException`
    (fatal — a mod bug).
  - `registryEvent(eventType)` — at boot, feeds each supplier's value into
    the `RegistryEvent` (the core listener then `entries.put(...)`).
  - Registration happens **during mod boot**, before the registry locks
    (registry lock phase = after all mods loaded — `RegistryLockedException`
    on late registration).
- **`Registries`** (the holder): a class where the game declares all core
  registries (the catalog in modding-analysis.md) — it's just a bundle; the
  interesting semantics are per-`Registry`/`Registrar`.

## 5. i18n (verified)

- **`LocaleSpecificity`** priority order (highest → lowest):
  **OVERRIDE > LANGUAGE_COUNTRY > LANGUAGE > FALLBACK**.
- **Lang file format:** Java `.properties` files, UTF-8, named
  `lang/<xx>[_<XX>].lang` (pattern `([a-z]+)(?:_([A-Z]+))?\.lang`), inside
  each mod's JAR under `<modId>/lang/`; also from a global override
  directory (user-provided overrides — `loadLangOverrides`, called from
  `GameEngine.start` with `ORIGINAL_LOCALE` before mod boot).
- **Precedence assembly** (`LangManager.Access.initialize(mods, locale)`):
  1. For **each mod in load order**: its `lang_COUNTRY`, `lang`, and
     `lang/en` fallback URLs are merged into that specificity's map — **a
     later-loaded mod overrides an earlier mod** (mod load order = i18n
     override order).
  2. Then the **original game's** language strings (the `loadLang(specificity,
     map)` overload taking a specificity — the retail text, resolved by the
     game, per specificity) are merged in *below* the mods (putAll into the
     same specificity maps — note: this merges into the same map *after* the
     mods; the ordering between retail and mod strings within one
     specificity is "retail wins" — a Java quirk worth documenting and
     consciously preserving or fixing; verify with a targeted test if a mod
     shadows retail text — **open question Q-MOD-3**).
- **Lookup:** first specificity with a hit; no hit → **returns the key
  itself** (the key-as-fallback pattern used throughout the game).
- **Formatting:** `getTranslation(key)` with no args runs
  `String.formatted()` **only if the value contains `%`** (argless formats
  like `%s` would throw at that point — the game instead uses `I18nText`
  which resolves format args at construction; verified usage pattern, not a
  loader feature).
- `findAvailableOverrideLocales(dir)` — scans a directory for the locales
  of user overrides (used to offer override languages in the options menu).

## 6. Misc (verified)

- `Latch` — a static boolean latch (used to gate one-shot init).
- `IoHelper` — `findJarResources(cls, path)` (enumerate resource entries in
  a JAR dir) + generic IO.
- All state is per-`Manager` instance, `reset()` supported (the game resets
  between mod re-bots, e.g. on language change / `--reset-mods`).

## 7. Implications for the native port

1. **No reflection, no annotations, no classloaders.** The native mod system
   (luau-api.md §package format) is the C++23 analog:
   - `@Mod(id, version)` → `mod.toml` manifest (`id`, `version` semver
     range, parsed by the engine — the *native* semver implementation must
     match NPM-semver range semantics of semver4j for compat: `^`, `~`,
     comparators; golden-test against a semver4j fixture set).
   - Classpath scan → package-directory scan (`mods/*.scmod` archives +
     `game/` built-in modules compiled in).
   - **Per-mod isolation improves on Java:** one Luau VM per mod (already
     designed) and precise per-mod error attribution (fixing the
     `pathToModId` JAR-grouping quirk).
   - Shared classloader quirk ⇒ Java mods *can* accidentally reference each
     other's classes; native mods cannot. Flag in the migration guide
     (luau-api.md): cross-mod class references are unsupported by design.
2. **Event semantics to preserve 1:1** (already in engine-design.md):
   exact-class dispatch, priority order (higher first),
   stopPropagation/cancel, critical vs non-critical error handling,
   synchronous delivery, weak instance-listener semantics (native: explicit
   `unsubscribe` + `SharedRef` listener handles; no weak-ref needed —
   document the behavior difference as an improvement).
3. **Registry semantics to preserve:** lazy entry resolution with
   use-time failure + mod attribution; ≤255 char ids; lowercase id grammar;
   duplicate-id fatal at registration; registry lock after boot.
4. **i18n:** properties file format is trivial to parse natively (subset:
   `key=value`, `\n` escapes, UTF-8); keep the 4-level specificity +
   load-order override; keep key-as-fallback.
5. **Mod states:** INITIALIZED → READY (2 states). The native lifecycle
   (luau-api.md) has more states (ERROR, etc.) — a superset, fine.

## 8. Open questions

- **Q-MOD-1:** When Severed-Chains calls `errorCallback` on a non-critical
  handler failure, does it disable the mod permanently for the session or
  just for the event? (Check `GameEngine` errorCallback impl — verify during
  M5 port; behavior must be replicated.)
- **Q-MOD-2:** `Reflexions` scan of the `legend` package for built-in mods —
  are there mods *other than* `CoreMod`/`LodMod` in the app classpath (e.g.
  in the bundled `turnorder` example JAR layout)? (Low impact.)
- **Q-MOD-3:** Retail-vs-mod i18n precedence within one specificity (§5.2)
  — verify by test whether a mod can override a retail string of the same
  key in the same locale; document the resulting rule for the native i18n.