# M8-02 — Registries and Events

**Milestone:** M8 (Mods and Modding), deliverable 2 of 3. **Date:** 2026-11-07. **Approved starting point:** `fc0881d` (research repo HEAD before M8 activation `305d851`; M8-01 committed at `4606af5`).

**Sources.** Mod-Loader `3433e6f`: `registries/Registry.java`, `MutableRegistry.java`, `Registrar.java`, `RegistryDelegate.java`, `RegistryEntry.java`, `Registries.java`, `RegistryId.java`, `RegistryLockedException.java`, `DuplicateRegistryIdException.java`, `IncorrectModIdException.java`, `events/EventManager.java`, `Event.java`, `CancelableEvent.java`, `EventBinding.java`, `EventListener.java`, `Priority.java`, `EventHandlerException.java`, `events/registries/RegistryEvent.java`, `Latch.java`, `ModContainer.java`, `ModManager.java`. Severed-Chains `0c08c67`: `core/Registries.java`, `core/GameEngine.java`, `core/AddRegistryEvent.java`, `core/platform/input/InputBindings.java`, `core/platform/SdlPlatformManager.java`, `game/modding/events/**` (10 packages, 88 files), `game/combat/bent/BattleEvent.java`, `game/combat/Battle.java`, `game/characters/**`, `game/saves/**`, `game/scripting/ScriptState.java`, `game/Scus94491BpeSegment.java`, `game/EngineStates.java`, `game/inventory/**`, `game/submap/**`, `game/wmap/**`, `game/dabas/DabasScreen.java`, `game/debugger/**`, `lodmod/**`, `turnorder/**`, `game/modding/coremod/**`.

**Validation.** All findings are `source-verified` (reference code) unless noted. No `asset-validated` claims (no retail assets). Continues the J/D numbering from `01-mod-loading.md` (J1–J8, D1–D9).

---

## 1. Registry identity and naming

### 1.1 The two-level id model

Two distinct identifiers are in play, and conflating them is the single most likely source of confusion in this subsystem:

- **Registry id** — identifies a *registry* (a category of content). One per `MutableRegistry`, passed to its constructor. E.g. `lod_core:items`.
- **Entry id** — identifies one *entry* inside a registry. Every entry id is a `RegistryId` whose `modId` component names the **registering mod**, and whose `entryId` component is the entry's own name. E.g. `lod:item_01`, `lod_core:nothing`, `turn_order:show_turn_order`.

`RegistryId` (Mod-Loader `registries/RegistryId.java`) is a two-part value type:

| Contract rule | Source | Detail |
|---|---|---|
| Grammar | `RegistryId.java:7` | Each part must match `[a-z][a-z0-9_-]*` — lowercase, starts with a letter, then letters/digits/underscore/hyphen. Violation → `IllegalArgumentException` (`:36-42`). |
| Length | `RegistryId.java:13,22` | Full id (both parts plus colon) must be **≤ 255 characters** (the message says "less than 255"; the check is `> 255` → throws). |
| String form | `RegistryId.java:21-34` | Single-string constructor splits on `:` and requires **exactly two** parts — no nested colons, no 1- or 3-part forms. This is how save files (M7-01), script inline params (`ScriptInlineRegistryIdParam`), and the i18n key parser round-trip ids as `modId:entryId` text. |
| Textual form | `RegistryId.java:44-47` | `toString()` = `modId + ":" + entryId`. |
| Equality | `RegistryId.java:57-74` | Value equality on both parts; `hashCode` = `modId.hashCode()*31 + entryId.hashCode()`. |

**Ownership semantics (the key contract):** the registry id's `modId` names the mod that *owns the category* (a namespace convention, not an enforcement point — nothing checks it at runtime). Entry ids carry the *registering* mod's id, and **that** is what is enforced: `MutableRegistry.register` (Mod-Loader `MutableRegistry.java:17-19`) requires `ModContainer.isModActive(id.modId())` — the executing handler's mod (see §2.2) must be the mod named in the entry id. Consequently a registry whose id is `lod_core:items` can hold entries from any number of mods (`lod_core:nothing`, `lod:item_01`, `somemod:mystery_item`), and the registry id's own mod component does not constrain which mods may contribute.

The 21 in-repo registry ids split 19/2 along this convention (§1.3): 19 are `lod_core:*` (categories defined by the engine/content baseline), 2 are `lod:*` (`encounters`, `battle_actions`).

### 1.2 Translation key format

Registry entries double as i18n anchors. `RegistryEntry.getTranslationKey(type)` (Mod-Loader `registries/RegistryEntry.java:21-27`) composes:

```
<entryId.modId>.<registryId.entryId>.<entryId.entryId>.<type>
```

with `type` defaulting to `"name"`. Example: the item `lod:item_01` in registry `lod_core:items` → translation key `lod.items.item_01.name`. (The key uses the *registry id's* `entryId()` as its middle segment — note the registry id's mod component is not in the key — and the *entry's* mod as the first segment.) The LangManager override model that consumes these keys is M8-03 scope; the key *shape* is part of the registry naming contract.

### 1.3 The 21-registry inventory

`Severed-Chains core/Registries.java` constructs all 21 registries (the `Registries` subclass adds each via `addRegistry`, which records registry → Register-event factory, Mod-Loader `Registries.java:25-31`, in a `LinkedHashMap` — insertion order is the `initializeRemaining` order). Element types are the decompiled game types the category holds.

| # | Field (`REGISTRIES.*`) | Registry id | Element type | Registrar mods (entry `modId`s) | Initialized in | Register event |
|---|---|---|---|---|---|---|
| 1 | `config` | `lod_core:config` | `ConfigEntry<?>` | lod_core, lod, turn_order | bootMods (explicit, 1st) | `ConfigRegistryEvent` |
| 2 | `campaignTypes` | `lod_core:campaign_type` | `CampaignType` | lod | bootMods (explicit, 2nd) | `RegisterCampaignTypesEvent` |
| 3 | `engineStateTypes` | `lod_core:engine_state_types` | `EngineStateType<?>` | lod_core, lod | bootMods (explicit, 3rd) | `RegisterEngineStateTypesEvent` |
| 4 | `inputActions` | `lod_core:input_actions` | `InputAction` | lod_core, lod | bootMods (explicit, 4th) | `InputActionRegistryEvent` |
| 5 | `characterTemplates` | `lod_core:character_templates` | `CharacterTemplate` | lod | bootMods (explicit, 5th) | `RegisterCharacterTemplatesEvent` |
| 6 | `statTypes` | `lod_core:stats` | `StatType<?>` | lod | bootMods (explicit, 6th) | `StatTypeRegistryEvent` |
| 7 | `statModTypes` | `lod_core:stat_mods` | `StatModType<?,?,?>` | lod | bootMods (explicit, 7th) | `StatModTypeRegistryEvent` |
| 8 | `goods` | `lod_core:goods` | `Good` | lod | bootMods (explicit, 8th — "needed for save cards on the title screen", `GameEngine.java:333`) | `GoodsRegistryEvent` |
| 9 | `elements` | `lod_core:elements` | `Element` | lod | bootRegistries (remaining, in order) | `ElementRegistryEvent` |
| 10 | `battleEntityTypes` | `lod_core:battle_entity_types` | `BattleEntityType` | lod | bootRegistries | `BattleEntityTypeRegistryEvent` |
| 11 | `elementIcons` | `lod_core:element_icons` | `ElementIcon` | lod | bootRegistries | `ElementIconRegistryEvent` |
| 12 | `deff` | `lod_core:deffs` | `DeffPackage` | lod | bootRegistries | `RegisterDeffsEvent` |
| 13 | `encounters` | `lod:encounters` | `Encounter` | lod | bootRegistries | `EncounterRegistryEvent` |
| 14 | `postBattleActions` | `lod_core:post_battle_actions` | `PostBattleAction<?,?>` | lod_core, lod | bootRegistries | `RegisterPostBattleActionsEvent` |
| 15 | `battleActions` | `lod:battle_actions` | `BattleAction` | lod | bootRegistries | `RegisterBattleActionsEvent` |
| 16 | `additions` | `lod_core:additions` | `Addition` | lod | bootRegistries | `AdditionRegistryEvent` |
| 17 | `shops` | `lod_core:shops` | `Shop` | lod | bootRegistries | `ShopRegistryEvent` |
| 18 | `items` | `lod_core:items` | `Item` | lod_core, lod | bootRegistries | `ItemRegistryEvent` |
| 19 | `equipment` | `lod_core:equipment` | `Equipment` | lod | bootRegistries | `EquipmentRegistryEvent` |
| 20 | `spells` | `lod_core:spells` | `SpellStats0c` | lod | bootRegistries | `SpellRegistryEvent` |
| 21 | `levelUpActions` | `lod_core:level_up_actions` | `LevelUpAction<?>` | lod | bootRegistries | `RegisterLevelUpActionsEvent` |

Registry ids were read from each registry class's constructor (e.g. `ItemRegistry.java:8` `super(new RegistryId("lod_core", "items"))`; `EncounterRegistry.java:8` and `BattleActionRegistry.java:8` use `"lod"`). The two `lod`-owned categories are the only exception to the `lod_core` convention. "Registrar mods" is the census in §5.

External mods may add *new* registries at boot via `AddRegistryEvent` (see §2.3) — the 21 are the in-repo census, not a cap.

Registrar coverage per registry is counted in §5; five registries have more than one contributing mod (config: 3; engineStateTypes, inputActions, postBattleActions, items: 2 each).

---

## 2. The registry contract

### 2.1 Registration mechanics

Entry insertion happens through exactly one path: a `Registrar` collects `Supplier`s, and when the registry's `Register` event is posted, each supplier is invoked and the result pushed through `MutableRegistry.register` (Mod-Loader `Registrar.java:32-34` → `MutableRegistry.java:12-28`). The register method performs three checks in order, then stores:

1. **Lock check** (`MutableRegistry.java:13-15`): if the registry is locked → `RegistryLockedException`.
2. **Active-mod check** (`MutableRegistry.java:17-19`): the entry id's mod must be "active" (the executing handler's code source must belong to that mod — §2.2) → else `IncorrectModIdException("Mod tried to register <registry> entry <entryId> for incorrect mod ID <modId>")`.
3. **Duplicate check** (`MutableRegistry.java:21-23`): id already present → `DuplicateRegistryIdException("Registry ID <id> already registered")`.

On success the entry receives a back-reference (`entry.setRegistry(this, id)`, `RegistryEntry.java:7-10`) and is stored. Note the duplicate check is *per registrar* at declaration time as well: `Registrar.register` (`Registrar.java:21-30`) throws `DuplicateRegistryIdException` if the same registrar has already declared that entry id (class-initialization-time failure).

**There are no add/remove events.** The only event coupling between registries and the event bus is the one-shot `Register` event fired at initialization (`Registries.Access.initialize`, Mod-Loader `Registries.java:62`). Nothing is posted when an entry is added or removed (entries are never individually removed at runtime — only wholesale on reset, §2.5). The "event firing on add/remove" research question resolves to: *registration is the only mutability window, and it is itself the event*.

### 2.2 The active-mod check (enforcement semantics)

`ModContainer.isModActive(modId)` (Mod-Loader `ModContainer.java:103-105`) consults two **process-wide statics**: `activeManager` (the `ModManager`) and `activeModClass` (the class currently "running"). The check is: `activeManager.getModsByClass(activeModClass)` (the set of mod ids whose code source matches the active class) contains `modId`.

`getModsByClass` (Mod-Loader `ModManager.java:82-84`) maps a class to its **code source location** (jar file or classes directory) and looks that up in `pathToModId` (populated at discovery, `ModManager.java:150`). Consequences:

- **Game-internal classes** (compiled into the main game jar/classes) are the code source for **all three in-repo mods** simultaneously: `pathToModId[<game classes>] = {lod, lod_core, turn_order}` (discovery scans the main classpath — `ModManager.java:111-118`, M8-01 §1). A handler in game code can therefore register entries under any of the three ids; conversely, an external mod jar's classes map only to their own id.
- The active class is set by the event dispatcher **per handler** (EventManager pushes the handler's class before invoking it — `EventManager.java:113-114`, §3.2), so during a `Register` event dispatch, each handler runs "as" the mod that owns it.
- Because the statics are global and the dispatcher is the only writer in practice, the contract assumes single-threaded boot (J13, §6; audit F-04 in M8-01).

**Error attribution** reuses the same mapping: when a handler throws, the EventManager resolves `getModsByClass(handlerClass)` and joins the id set with `,` (`EventManager.java:118-122`) — for a game-internal handler failure this yields the compound string `lod,lod_core,turn_order` in the user-facing error toast, since all three mods share the game code source. (External mod failures attribute to a single id.)

### 2.3 Mutability windows and initialization order

Registries are **open only during their own `Register` event dispatch**. `Registries.Access.initialize(registry)` (Mod-Loader `Registries.java:51-65`) does, in order: guard against double-init (`IllegalStateException`), guard against unknown registries (`IllegalArgumentException`), **post the registry's `Register` event**, then `lock()`. After `lock()`, `register` throws `RegistryLockedException` forever until the next `reset()`.

The in-repo boot sequence (from M8-01 §4, re-anchored here for registry purposes) is:

```
bootMods(modIds):                                   (GameEngine.java:307-353)
  MOD_ACCESS.reset(); EVENT_ACCESS.reset(); REGISTRY_ACCESS.reset()
  MOD_ACCESS.loadMods(modIds)                       -- instantiate mod classes
  EVENT_ACCESS.initialize(MODS)                     -- scan + register event handlers
  EVENTS.postEvent(new AddRegistryEvent(REGISTRIES))-- mods may ADD registries (GameEngine.java:322)
  REGISTRY_ACCESS.initialize(REGISTRIES.config)          -- window 1:  config entries
  REGISTRY_ACCESS.initialize(REGISTRIES.campaignTypes)   -- window 2:  campaign types
  REGISTRY_ACCESS.initialize(REGISTRIES.engineStateTypes)-- window 3:
  REGISTRY_ACCESS.initialize(REGISTRIES.inputActions)    -- window 4:
  REGISTRY_ACCESS.initialize(REGISTRIES.characterTemplates)-- window 5:
  REGISTRY_ACCESS.initialize(REGISTRIES.statTypes)       -- window 6:
  REGISTRY_ACCESS.initialize(REGISTRIES.statModTypes)    -- window 7:
  REGISTRY_ACCESS.initialize(REGISTRIES.goods)           -- window 8:  (title-screen save cards)
  MOD_ACCESS.loadingComplete()                      -- all mod instances -> READY
  InputBindings.initBindings() / loadBindings(CONFIG)-- posts RegisterDefaultInputBindingsEvent (InputBindings.java:31)
  loadLang()
  (toasts for failed/wrong-version mods)
bootRegistries():                                   (GameEngine.java:366-...)
  REGISTRY_ACCESS.initializeRemaining()             -- windows 9-21: the other 13 registries, in LinkedHashMap insertion order
  ItemIcon.loadIconMap()                            -- posts IconMapEvent
  EVENTS.postEvent(new RegisterAtlasTexturesEvent(images)) -- GameEngine.java:378
  (texture atlas packing)
```

Observations:

- **The 8 explicit windows exist because the title/menu UI needs those categories before the game-world categories are required.** The 8th (`goods`) is explicitly justified in a source comment ("save cards on the title screen").
- `AddRegistryEvent` is posted **before any registry initializes**, so a mod can contribute a new registry that then gets registered-and-locked like the built-ins (its `initializeRemaining` call covers it — added registries go into the same `LinkedHashMap`). No in-repo mod uses it (only external-mod extension point). It is critical (§3.5).
- `bootRegistries()` is called from exactly three sites: the memcard-conversion flow (`SaveManager.java:233`), save loading (`SaveManager.java:411`, only when `fullBoot`), and new-campaign creation (`NewCampaignScreen.java:233`). Each such call is preceded by a `bootMods` re-run (M8-01 §4's 12 call sites), so the window ordering is always `reset → re-register → windows 1-8 → ... → windows 9-21`.
- Because all `Register` events are critical (§3.5), **any registrar failure (duplicate id, wrong mod id, thrown supplier) during a boot window crashes the app through the M1 crash path** (platform/01 §7: `Main.main` catch → crash-recovery save + `CRASHLOG-<ts>.log` + `System.exit(1)`). There is no per-registry failure tolerance.

### 2.4 Delegates: deferred, weakly-held entry references

Code references entries through `RegistryDelegate<Type>` (`Mod-Loader registries/RegistryDelegate.java`), never through the registry map directly. The contract-relevant properties:

- **Delegates are valid before entries exist.** `Registrar.register` returns `registry.getEntry(id)` immediately (`Registrar.java:29`), which is a `computeIfAbsent` on a `ReferenceMap`-backed `delegates` map (`Registry.java:22-24`) — so a mod's `public static final RegistryDelegate<Item> MY_ITEM = ITEM_REGISTRAR.register("my_item", () -> new MyItem())` compiles and holds a delegate at class-load time, and only calls `.get()` later, by which time registration has happened.
- **Deferred resolution with caching.** The delegate wraps a `Latch` (Mod-Loader `Latch.java`): the supplier (which reads `registry.entries.get(id)`) runs on first `get()` and the result is cached until `clear()`. `get()` on a delegate whose id has no entry throws `ModNotLoadedException("<id> not found in <registry>")` (`RegistryDelegate.java:29-35`) — the reference's way of saying "you dereferenced a registry entry whose mod is not enabled for this campaign" (see §2.6).
- **Weak reference map.** `delegates` is an Apache Commons `ReferenceMap` (weakly-keyed), `Registry.java:12`. Delegates kept alive by mod static fields survive; transiently-created ones can be collected. This is an implementation detail (D12) but the observable edge case is that two independently obtained delegates for the same id are *equal* (`equals` on class + id, `RegistryDelegate.java:45-52`) yet distinct objects with independent latches.
- **Registry iteration** (`Registry.java:38-41`) iterates entry ids (the `entries` key set) — this is how `InputBindings.saveBindings` walks `REGISTRIES.inputActions` (`InputBindings.java:44`) and how config serialization walks the config registry (M7-02).
- `size()` counts entries, not delegates (`Registry.java:34-36`).

### 2.5 Reset semantics (the reboot cycle)

`bootMods` re-entrance (campaign switch, mod-set change, new campaign) runs three resets at the top (`GameEngine.java:312-314`):

- `MOD_ACCESS.reset()` — mod instances back to a clean slate (M8-01 §3).
- `EVENT_ACCESS.reset()` — clears the listener map and stale set (`EventManager.java:64-68`); handlers are re-discovered by the subsequent `EVENT_ACCESS.initialize` scan. (Note: the dispatcher's `eventHandlerStack` is *not* cleared on reset — a hazard only if a reboot were ever triggered from inside a dispatch, which no in-repo call site does; recorded as a constraint, not a defect, since reboots are driven from UI flows.)
- `REGISTRY_ACCESS.reset()` — for every registry: `lock=false`, clear all known delegates' latches, clear all entries (`MutableRegistry.java:34-42`, `Registries.java:75-81`), and clear the initialized set.

Because entry objects are re-created on each boot (suppliers re-run in each window), **any code that dereferenced a delegate into a local/static before a reboot holds a stale entry object** — the registry contract relies on going through delegates (whose latches are cleared) at use time, not caching the entry. Mods that hold raw entries across reboots would silently use pre-reboot data; no in-repo mod does this (they all hold delegates — §5).

### 2.6 What "missing mod" means at the registry layer

A campaign enables a subset of mods (`enabled_mods`, M8-01 §7). On `bootMods(enabledMods)`, the non-enabled mods' classes still exist (one-shot discovery, M8-01 §2) but their registrars' handlers are still scanned — their entries are *registered* (the handler runs under its own active-mod context). The "missing" manifestation is at **dereference time**: `getEntry(id).get()` on an id whose supplier was never executed for this boot... in practice the reference registers *all* discovered mods' entries regardless of enablement (the Register event handlers are all re-registered on every boot by the Reflections scan — enablement affects instantiation/`loadMods` membership, M8-01 §4); the user-visible missing-mod path is the `missingMods` return from `loadMods` and the consent flow (M8-01 §4). `ModNotLoadedException` is the guard against dereferencing an id that was never registered (e.g. an id typed by hand in a script inline param — `ScriptInlineRegistryIdParam` — for a mod/entry that does not exist).

---

## 3. The event contract

### 3.1 The manager model

`EventManager` (Mod-Loader `events/EventManager.java`) is a synchronous, single-lock, **exact-class** event bus:

| Contract rule | Source | Detail |
|---|---|---|
| Dispatch is by exact class | `EventManager.java:105` | Handlers are looked up with `event.getClass()` — **no supertype dispatch**. A handler registered for `BattleEvent` receives none of its 14+ subclasses; a handler for `InventoryEvent` receives none of the 8 `Give*/Take*` events. The class hierarchy in the event tree is organizational (shared payload fields), not a dispatch mechanism. |
| Priority ordering | `EventManager.java:60, 90-95`; `@EventListener` javadoc | Five levels `LOWEST < LOW < NORMAL < HIGH < HIGHEST` (`Priority.java`); higher first; **equal-priority order is registration order** (stable sort after each insertion) and the annotation javadoc explicitly disclaims any guarantee ("the execution order is undefined"). In-repo, every handler uses the default `NORMAL` (census below). |
| Single lock, re-entrant | `EventManager.java:104-148` | The whole dispatch is inside `synchronized(listeners)`. Nested `postEvent` from a handler is legal (re-entrant on the same thread) — used implicitly by handlers that post follow-up events. |
| Active-mod bookkeeping | `EventManager.java:113-114, 147` | Before each handler: push the handler's class onto `eventHandlerStack` and set it as the active mod; after the loop: restore the outer active mod (top of stack, or null). This is what makes the registry active-mod check (§2.2) work per-handler and is also what error attribution uses. |
| Returns the event | `EventManager.java:150` | `postEvent` returns the event — the reference's "event as RPC" idiom: the engine mutates the event before posting, posts, then reads mutated fields back (e.g. `SubmapWarpEvent.newCut`, `GatherItemsEvent` result arrays, `PreCharacterLevelUpEvent.statsToAdd`). 99 `postEvent` call sites in the in-repo census; the pattern dominates the gameplay-facing events. |

### 3.2 Handler registration

Two registration forms:

1. **Class-level, static** — the `@EventListener` annotation (class or method; the scan is class-level, `EventManager.java:53`). `EventManager.Access.initialize` (`EventManager.java:44-61`) runs a Reflections scan over the game's `legend` package plus all mod jar URLs (`addModsToReflectionsConfig`, M8-01 §1.2), collects classes annotated `@EventListener`, and registers each class with a null instance.
2. **Instance registration** — `EVENTS.register(this)` (`EventManager.java:99-101`), which binds the object's own `@EventListener` methods. The instance is held via `WeakReference` (`EventBinding.java:12,19`). In-repo users: `TurnOrderMod`'s constructor (`TurnOrderMod.java:61`) and the four debugger controllers (`ScriptDebuggerController`, `SmapModelDebuggerController`, `DebuggerController`, `ScriptLiveDebuggerController`) — all transient UI features that must not pin memory.

Per-method validation at registration (`EventManager.java:70-98`) — violations are **logged and skipped**, not fatal:

- Non-static method with no instance → "must be static" (checked via `method.canAccess(instance)`).
- Not exactly one parameter → "must have one parameter".
- Parameter type not assignable to `Event` → "must have event parameter".
- Only **declared** methods of the exact class are considered (`getDeclaredMethods`) — inherited `@EventListener` methods are *not* picked up (D17).

Priority comes from the method's `@EventListener(priority=...)` (default `NORMAL`). After initialize, each event's binding list is sorted priority-descending (`EventManager.java:60`); instance registrations sort on insertion.

**Stale-reference handling:** an instance binding whose weak referent has been GC'd is marked invalid on dispatch (`EventBinding.execute`, `EventBinding.java:28-35`) and queued in `staleListeners`; `clearStaleRefs()` (`EventManager.java:153-159`) physically removes them. The reference calls `clearStaleRefs()` **every frame** from the render loop (`RenderEngine.java:603`) — so a dead debugger/turn-order instance stops receiving events within one frame, and stale bindings are even detected during a dispatch where propagation was stopped (the loop deliberately continues: "Don't exit immediately so that stale listeners still get cleared", `EventManager.java:111`).

### 3.3 Handler invocation and error isolation

Per binding, per event (`EventManager.java:106-145`):

```
if (event.shouldPropagate()) {
    push(listenerClass); setActiveMod(listenerClass)
    try { binding.execute(event) }
    catch (IllegalAccessException | InvocationTargetException e) {
        modId = getModsByClass(listenerClass) joined with ","  (or "Unknown mod")
        if (event.isCritical())
            throw new EventHandlerException(modId + " event handler for " + event + " threw an exception", cause-unwrapped)
        else
            LOGGER.error("Failed to deliver event", e)
            errorCallback.accept(modId, e)
    } finally { pop() }
}
if (binding.isInvalid()) staleListeners.add(binding)
```

- **Non-critical events (the default):** a handler exception is **isolated** — the error is logged, the `errorCallback` fires, and **the remaining handlers still run** (the loop continues; `finally` pops the stack). In the reference game, `errorCallback` is `GameEngine::onModError` (`GameEngine.java:96, 355-357`), which shows a **7-second toast** `I18nText("lod_core.ui.mods.error", modId)`. The user-visible consequence of a broken mod handler on a non-critical event is therefore: one toast per failed dispatch, and the game continues. There is no mod disablement, no rate limiting (every failing dispatch toasts), and no crash-log entry beyond the `log4j2` error line.
- **Critical events:** the exception is rethrown as `EventHandlerException` (cause unwrapped from `InvocationTargetException`). No in-repo catch exists between the post site and the top-level crash handler — the failure propagates through `bootMods`/`bootRegistries`/the calling screen into the M1 crash path (platform/01 §7): crash-recovery save (if `CREATE_CRASH_SAVE_CONFIG` is on), `CRASHLOG-<timestamp>.log` copy, `System.exit(1)`. **A critical event failure is a game crash by design.**
- The catch covers only reflection failure modes (`IllegalAccessException`, `InvocationTargetException`); any other exception type (e.g. `Error`) escapes the dispatcher entirely.

### 3.4 Control flow: stopPropagation and cancellation

- `stopPropagation()` (`Event.java:10-12`) sets a flag; subsequent bindings are skipped for *this event* (the stale-detection loop still runs). **Zero in-repo call sites** — documented "play nice" contract, unused by the reference content.
- `CancelableEvent` (`CancelableEvent.java`) adds `cancel()` (= `stopPropagation()` + `canceled=true`) and `isCanceled()`. Five in-repo event classes extend it: `InventoryEvent` (base of 8 take/give/flag events), `AdditionUnlockEvent`, `CloneCharacterEvent`, `PreCharacterDragoonLevelUpEvent`, `PreCharacterLevelUpEvent`.
- In-repo **cancellation is checked for 7 behaviors** — the take/give inventory family and character cloning: `TakeEquipmentEvent` (`SItem.java:553`), `GiveEquipmentEvent` (`SItem.java:565`), `CloneCharacterEvent` (`SMap.java:1213`), `GiveItemEvent` (`Inventory.java:102`), `TakeItemEvent` (`Inventory.java:189,244`), `TakeGoodsEvent` (`GoodsInventory.java:30`), `GiveGoodsEvent` (`GoodsInventory.java:47`).
- The level-up/advance `Pre*` cancelable events (`PreCharacterLevelUpEvent`, `PreCharacterDragoonLevelUpEvent`, posted at `RetailCharacterTemplate.java:272/295`) are **posted and read back for their mutated fields, but their cancellation is never checked** — cancelling them in-repo has no effect. (Their contract value is the *mutation* payload — `statsToAdd`, `levelsToAdd` — which the engine does apply.) `AdditionUnlockEvent`'s cancellation is likewise unchecked in-repo. Documented as contract-available-but-unenforced.

### 3.5 The critical-event set

Exactly three `isCritical()` overrides exist:

| Event | Source | Effect of handler failure |
|---|---|---|
| All 21 `RegistryEvent.Register` subclasses | `RegistryEvent.java:24-27` | Boot crash (registry window interrupted) |
| `AddRegistryEvent` | `core/AddRegistryEvent.java:23` | Boot crash (registry-add phase) |
| `RegisterAtlasTexturesEvent` | `game/textures/RegisterAtlasTexturesEvent.java:20` | Crash at save-load/new-campaign (posts from `bootRegistries`, `GameEngine.java:378`) |

Everything else (the ~80 gameplay/notification events) is non-critical → toast + continue.

---

## 4. Event family inventory

### 4.1 Family structure

The in-repo event tree is 113 classes/interfaces: 77 of them in 88 files under `game/modding/events/**` (the 10 package directories, each with a `package-info`, plus the root `package-info` and the root-level `RenderEvent`), and 36 more declared in domain packages, mostly the 21 `Register*` events co-located with what they register (`core/`, `game/characters|combat|inventory|saves|additions`, `core/platform/input`) plus gameplay events in `combat/bent`, `combat/ui`, `dabas`, `scripting`, and `textures`. The ten `modding/events` packages match the plan's "10 event families": **battle, characters, config, engine, gamestate, input, inventory, scripting, submap, worldmap**.

The hierarchy (source-verified from the class declarations):

```
Event (Mod-Loader)
├── CancelableEvent
│   ├── InventoryEvent (empty cancelable base for the take/give family)  [cancellable]
│   ├── AdditionUnlockEvent  [cancellable]
│   ├── CloneCharacterEvent  [cancellable]
│   ├── PreCharacterDragoonLevelUpEvent  [cancellable]
│   └── PreCharacterLevelUpEvent  [cancellable]
├── BattleEvent (game/combat/bent: `battle`)
│   └── BattleStateEvent (adds `encounter`)
│       ├── BattleStartedEvent / BattleEndedEvent
│       └── BattleEntityTurnEvent<T> (adds `state`, `bent`)
├── ConfigEvent
│   ├── ConfigUpdatedEvent / ConfigLoadedEvent / ConfigDefaultPresetsEvent
├── EngineEvent
│   └── EngineStateChangeEvent (old type + new state)
├── GameStateEvent (holds `GameState52c`)
│   ├── GameLoadedEvent / NewGameEvent / PartyFlagsChangeEvent / PrimaryPartyChangeEvent
├── InGameEvent<T extends EngineState> (implements EngineStateEvent<T>; holds state + GameState52c)
│   ├── EncounterEvent<T> → SubmapEncounterEvent, WorldMapEncounterEvent
│   └── (6 Submap* events implementing LoadedSubmapEvent)
├── InputEvent → InputPressedEvent / InputReleasedEvent
├── ShopEvent → ShopBuyEvent / ShopContentsEvent / ShopSellEvent / ShopSellPriceEvent
├── ScriptEvent (scriptIndex) → ScriptLifecycleEvent (9-value lifecycle enum)
└── GatherItemsEvent → GatherAttackItemsEvent / GatherRecoveryItemsEvent
```

plus flat `Event` subclasses (the flat tail carries most of the gameplay surface): `ActiveItemEvent`, `ActiveSpellEvent`, `ArcherSpEvent`, `AttackEvent`, `BattleMusicEvent`, `CharacterCreatedEvent`, `CombatantModelLoadedEvent`, `DabasRewardsEvent`, `DivineDragoonEvent`, `DragonBlockStaffOff/OnEvent`, `EnemyRewardsEvent`, `GatherBattleActionsEvent`, `GatherCharacterEquipmentTypesEvent`, `GatherEquipmentTypesEvent`, `GatherShopExtensionsEvent`, `IconMapEvent`, `LoadDeffEvent`, `LoadEnemyEvent`, `MonsterStatsEvent`, `PostAdditionLevelUpEvent`, `PostCharacterDragoonLevelUpEvent`, `PostCharacterLevelUpEvent`, `ReadGlobalFlagsEvent`, `RegisterBattleEntityStatsEvent`, `RegisterDefaultInputBindingsEvent`, `RenderEvent`, `RepeatItemReturnEvent`, `ResolveCharacterElementEvent`, `SetBattleEntityStatEvent`, `SpellStatsEvent`, `StatDisplayEvent`, `ReadSaveDataEvent`, `WriteSaveDataEvent`, `RegisterAtlasTexturesEvent`, `AddRegistryEvent`, `EngineStateEvent<T>`/`SubmapEvent`/`LoadedSubmapEvent`/`WorldMapEvent` (interfaces).

Because dispatch is exact-class (§3.1), a mod handler must target the **concrete** class the engine posts; the hierarchy is for payload sharing only.

### 4.2 Family table (payload, cancellation, post sites, in-repo consumers)

Payloads are the fields a handler may read/mutate. "Post sites" are the in-repo sources; many take a single representative (the engine posts most events from exactly one place). Consumers: only the 7 in-repo handler classes (§4.3) — every other event is an **extension point with no in-repo consumer**.

| Family (primary package — see §4.1 for co-location) | Events (payload summary) | Cancellable | Representative post sites |
|---|---|---|---|
| **registry/infra** (core + saves + textures) | `AddRegistryEvent` (Registries; critical), the 21 `Register*` events (MutableRegistry), `RegisterAtlasTexturesEvent` (map of id→Image, additive `add()`; critical), `IconMapEvent` (item/goods/equipment icon map), `RegisterDefaultInputBindingsEvent` (default-binding map, additive) | no | `GameEngine.java:322`; each registry window (§2.3); `GameEngine.java:378`; `ItemIcon.java:83`; `InputBindings.java:31` |
| **config** | `ConfigUpdatedEvent` (the `ConfigEntry<?>` that changed), `ConfigLoadedEvent` (collection + storage location), `ConfigDefaultPresetsEvent` (preset map) | no | `ConfigCollection.java:47`; `ConfigStorage.java:119,185`; `ConfigPresetManager.java:202` |
| **engine** | `EngineStateChangeEvent` (old `EngineStateType`, new `EngineState`) | no | `EngineStates.java:61` (state-machine transition) |
| **gamestate** | `GameLoadedEvent`, `NewGameEvent` (both hold `GameState52c`; order: New before Loaded — javadoc), `PartyFlagsChangeEvent` (index+flags), `PrimaryPartyChangeEvent` (index+value) | no | `SaveManager.java:434`; `NewCampaignScreen.java:238`; `GameVarParam.java:369`; `GameVarArrayParam.java:73` |
| **input** | `InputPressedEvent` (action, repeat flag), `InputReleasedEvent` (action) | no | `SdlPlatformManager.java:408,417,640,657,688,694,793,803` (8 SDL poll sites) |
| **battle** (20 concrete: 13 in `combat/bent`, 1 in `combat/ui`, 6 flat) | `BattleStarted/Ended` (encounter), `BattleEntityTurnEvent` (script state + bent), `AttackEvent` (attacker/defender/type/damage — damage mutated by handler), `ArcherSpEvent`, `BattleMusicEvent` (victoryType, musicIndex), `CombatantModelLoadedEvent`, `DragonBlockStaffOn/Off`, `GatherBattleActionsEvent`, `LoadDeffEvent` (paths mutated), `LoadEnemyEvent`, `MonsterStatsEvent` (hp mutated), `SetBattleEntityStatEvent`, `SpellStatsEvent`, `StatDisplayEvent`, `ActiveItemEvent`/`ActiveSpellEvent`/`EnemyRewardsEvent`/`RegisterBattleEntityStatsEvent` (flat) | no | `Battle.java:2332,2375,2385,2012,2997,4244,2453,6729,8645,8673,8677,8708,8712,8879,9335`; `GameVarArrayParam.java:640-642`; `CharacterData2c.java:207,212`; `BattleEntity27c.java:276` |
| **characters** (10) | `CharacterCreatedEvent`, `CloneCharacterEvent` (char0→char1; cancellable), `DivineDragoonEvent`, `Pre/PostCharacterLevelUpEvent` (statsToAdd map, levelsToAdd; Pre cancellable), `Pre/PostCharacterDragoonLevelUpEvent`, `PostAdditionLevelUpEvent`, `ResolveCharacterElementEvent` (resolved element mutated), `AdditionUnlockEvent` (cancellable) | 4 of 10 | `CharacterTemplate.java:28`, `SMap.java:1211`, `SMap.java:1245`, `RetailCharacterTemplate.java:272-304`, `PostBattleScreen.java:174`, `CharacterData2c.java:207,212` |
| **inventory** (18 concrete + 2 bases) | `GiveItem/TakeItem` (stack mutated; cancellable), `GiveGoods/TakeGoods` (cancellable), `GiveEquipment/TakeEquipment` (cancellable), `ScriptFlags1/2ChangedEvent` (packed index + value; cancellable via base), `GatherItemsEvent` + `GatherAttack/RecoveryItemsEvent` (additive `stacks` list via `add()`), `GatherEquipmentTypesEvent`, `GatherCharacterEquipmentTypesEvent`, `GatherShopExtensionsEvent`, `IconMapEvent` (also listed under infra), `RepeatItemReturnEvent` (stack + flag; result read back), `ShopBuy/Sell/Contents/SellPriceEvent` (price mutated) | 8 (all InventoryEvent subclasses) | `Inventory.java:100,187,242`; `GoodsInventory.java:28,45`; `SItem.java:376,385,551,563`; `Scus94491BpeSegment.java:295,337`; `EquipmentTypes.java:52,53`; `UseItemScreen.java:486`; `ShopScreen.java:127,144,327,341,759,760`; `ItemShopExtension.java:59`; `GoodShopExtension.java:48` |
| **submap** (7) | `SubmapLoadEvent` (submap + script file, script mutated), `SubmapWarpEvent` (newCut mutated), `SubmapEncounterRateEvent` (rate mutated), `SubmapEncounterAccumulatorEvent` (accumulator + step + multiplier mutated), `SubmapEncounterEvent` (encounter/stage/cut/scene), `SubmapEnvironmentTextureEvent` / `SubmapObjectTextureEvent` (disk index; overrides map mutated) | no (cancellation n/a — `InGameEvent` base) | `SMap.java:3259,4015,3701`; `RetailSubmap.java:515,526,659,831` |
| **worldmap** (1) | `WorldMapEncounterEvent` (encounter, stage, path segment) | no | `WMap.java:3727` |
| **scripting** (3) | `ScriptEvent` (scriptIndex), `ScriptLifecycleEvent` (9 lifecycle values: ALLOCATED, PRE/POST_SCRIPT_VM_TICK, PRE/POST_TICK_CALLBACK, PRE/POST_RENDER_CALLBACK, PRE/POST_DEALLOCATE — one reusable instance per script state), `ReadGlobalFlagsEvent` (flag index ref, mutated) | no | `ScriptState.java:269` (all 9 lifecycle posts), `Scus94491BpeSegment.java` (global flags) |
| **saves** (2) | `ReadSaveDataEvent` / `WriteSaveDataEvent` (a `ListTag` of `{id, data}` pairs; **contract: ids must use the handler's own mod id or the mod will not receive them back** — javadoc on `WriteSaveDataEvent.add`) | no | `V10Serializer.java:294` (write, during save serialization); `SaveManager.java:426` (read, during save load) |
| **dabas** (1) | `DabasRewardsEvent` (gold, reward list, special reward — all mutated) | no | `DabasScreen.java:228` |
| **render** (1) | `RenderEvent` (empty payload; **a single static instance posted every frame** — `Scus94491BpeSegment.java:130,162` — handlers must not retain it) | no | `Scus94491BpeSegment.java:162` |

Total in-repo `postEvent` call sites: **99**.

### 4.3 In-repo consumer census

Exactly **7 classes, 39 handler methods** consume events in the reference — everything else is extension surface:

| Class | Kind | Handlers (event → method) |
|---|---|---|
| `CoreMod` (`game/modding/coremod/CoreMod.java:95` class-level) | static | 9: RegisterEngineStateTypesEvent, ConfigRegistryEvent, InputActionRegistryEvent, ItemRegistryEvent, RegisterDefaultInputBindingsEvent, RegisterPostBattleActionsEvent, GatherShopExtensionsEvent, GameLoadedEvent (inventory-size override), RegisterAtlasTexturesEvent |
| `LodMod` (`lodmod/LodMod.java:114` class-level) | static | 19: one per registry window (campaignTypes, engineStates, inputActions, config, characterTemplates, levelUpActions, items, equipment, goods, shops, encounters, spells, statTypes, statModTypes, elements, elementIcons, bentTypes) + RegisterDefaultInputBindingsEvent + RegisterBattleEntityStatsEvent |
| `TurnOrderMod` (`turnorder/TurnOrderMod.java:42` class-level; instance-registered in ctor `:61`) | weak instance | 5: ConfigRegistryEvent, GameLoadedEvent, BattleEndedEvent, BattleEntityTurnEvent, RenderEvent |
| `DebuggerController` | weak instance | 2: ConfigLoadedEvent, ConfigUpdatedEvent |
| `ScriptDebuggerController` | weak instance | 2: ScriptLifecycleEvent, RenderEvent |
| `SmapModelDebuggerController` | weak instance | 1: RenderEvent |
| `ScriptLiveDebuggerController` | weak instance | 1: ScriptLifecycleEvent |

**Priority used: none.** No in-repo handler sets `@EventListener(priority=...)` — all 39 are `NORMAL`. The five-level priority mechanism is part of the contract but dormant in the reference content (D11).

---

## 5. In-repo registrar census

27 `Registrar` statics across the three in-repo mods (entry `modId` = the mod's own):

| Mod | Registrars (registry → file) |
|---|---|
| **lod_core** (5) | items → `CoreMod.java:99` (registers `lod_core:nothing`); config → `CoreMod.java:103` (80 entries, M7-02); inputActions → `CoreMod.java:193`; postBattleActions → `CorePostBattleActions.java:16`; engineStateTypes → `CoreEngineStateTypes.java:14` |
| **lod** (21) | campaignTypes, inputActions, statTypes, statModTypes, elements, elementIcons, battleEntityTypes → `LodMod.java:122,126,161,181,185,196,208`; engineStateTypes → `LodEngineStateTypes.java:17`; equipment → `LodEquipment.java:19`; encounters → `LodEncounters.java:16`; characterTemplates → `LodCharacterTemplates.java:21`; levelUpActions → `LodLevelUpActions.java:18`; postBattleActions → `LodPostBattleActions.java:16`; shops → `LodShops.java:13`; goods → `LodGoods.java:13`; spells → `LodSpells.java:15`; deff → `LodDeffs.java:13`; config → `LodConfig.java:20`; additions → `LodAdditions.java:16`; battleActions → `LodBattleActions.java:20`; items → `LodItems.java:37` |
| **turn_order** (1) | config → `TurnOrderConfigs.java:13` (1 entry: `show_turn_order`) |

Coverage: all 21 registries have ≥1 registrar; the entry-count census per registry is M8-03 scope (core-mod content inventory). The config registry's three registrars are the concrete instance of the multi-mod contribution rule (§1.1) — the verified M7-02 census is 88 entries (lod_core 80, lod 7, turn_order 1).

---

## 6. Java mechanisms (continuation of the J table)

Observable-contract vs implementation detail, per M8-01 §8 conventions:

| # | Mechanism | Observable contract (must preserve) | Implementation detail (may differ) |
|---|---|---|---|
| J9 | Annotation-scan handler discovery | Every handler of every *loaded* mod is active after each boot; a class can contribute handlers with no registration call; discovery covers game code and all mod jars. | Reflections over the `legend` package + mod URLs; class-level `@EventListener`; `getDeclaredMethods` (J14). |
| J10 | Single-lock re-entrant dispatch | Events are dispatched synchronously on the caller thread; handlers may post further events (nesting works); no handler runs concurrently with another. | One `synchronized` block per dispatch on the listener map. |
| J11 | Weak instance listeners | Instance-registered listeners stop receiving events once the owning object is reclaimable; cleanup is deferred (frame-batched), never a failure. | `WeakReference` + `staleListeners` set + per-frame `clearStaleRefs`. |
| J12 | Deferred entry resolution | Delegates can be held before registration; first use after registration succeeds; use of an unknown id is a distinct, catchable error naming the id and registry. | `Latch`-cached supplier; `ReferenceMap` (weak) for delegates. |
| J13 | Process-wide active mod | Registry registration succeeds iff the registering code "belongs to" the entry's mod; per-handler attribution of errors to mod id(s). | Two statics (`activeManager`, `activeModClass`); code-source → mod-id-set lookup. Thread-affinity assumed (audit F-04, M8-01). |
| J14 | Declared-methods-only binding | Inherited handler methods are not active; a subclass must redeclare. | `Class.getDeclaredMethods`. |
| J15 | One-shot discovery + static registrar state | Registrars and delegates survive reboots; each boot re-runs the suppliers and re-fires the Register windows; per-boot entry objects. | Static final fields in mod classes; `reset()` clears registries/listeners, not the statics. |

---

## 7. Classification (continuation of the D table)

| # | Finding | Class | Note |
|---|---|---|---|
| D10 | Exact-class event dispatch (no supertype fan-out) | **Contract** | A replacement bus that fans out hierarchically would change observable behavior for any mod registering on a base class (currently dead code). Preserve exact-class semantics or document the divergence. |
| D11 | Five-level priority, stable order, all in-repo handlers NORMAL | **Contract + observation** | The mechanism is part of the API (mods may use it); the reference content exercises none of it. |
| D12 | Weak reference map for delegates | **Implementation** | No observable effect while delegates are statically held (always the in-repo pattern); relevant only to transient delegates. |
| D13 | Active mod as process-wide statics, code-source mod sets | **Implementation** (contract in J13) | A per-dispatch-context active mod (e.g. task-local) would be observationally identical in the reference's single-threaded boot. |
| D14 | No add/remove events; the Register window is the only mutability event | **Contract** | "Event firing on add/remove" research question (plan M8 RQ2): answered — there are none; registration *is* the event. |
| D15 | Per-handler error isolation with mod-attributed callback; game shows a 7-s toast per failing dispatch | **Contract** | Observable user surface; the toast text is the `lod_core.ui.mods.error` i18n key with the mod id argument (compound id for game-internal handlers, §2.2). |
| D16 | Critical-event failure = app crash (crash-recovery save + CRASHLOG + exit) | **Contract** | Critical set is exactly: the 21 Register events + AddRegistryEvent + RegisterAtlasTexturesEvent (§3.5). |
| D17 | Only declared (non-inherited) `@EventListener` methods bind | **Implementation** | Observable consequence recorded in J14; a Rust re-binding that walks supertraits would silently differ for handler subclasses. |
| D18 | Cancellation is checked for 7 take/give/clone behaviors only; `Pre*`-level-up and `AdditionUnlockEvent` cancellation is unenforced in-repo | **Contract + observation** | The cancelable *type* is API surface (mods may cancel); which cancellations the engine honors is the in-repo census (4.2) — a replacement honoring all of them is the faithful choice. |
| D19 | `stopPropagation` unused in-repo; `RenderEvent` is a reused static instance posted per frame | **Contract + observation** | The former is API surface only; the latter means render handlers must be side-effect-only per call (no state accumulation on the event object). |

---

## 8. Verification criteria mapping (plan M8)

| Criterion | Where answered |
|---|---|
| Registry contract: naming, ownership, mutability windows, locking, delegation, event firing on add/remove | §1.1 (naming + ownership), §2.3 (windows + locking), §2.4 (delegation), §2.1/D14 (event firing — none beyond the Register window) |
| 21 registries' element types and purposes | §1.3 inventory table |
| Event contract: families, payloads, priorities, cancelable vs not, error isolation + user-visible consequences | §4.1/§4.2 (families + payloads), §3.1/§4.3 (priorities — mechanism + census), §3.4/D18 (cancelable), §3.3/D15/D16 (error isolation: toast vs crash) |
| Full registry inventory table (id, element type, registrar mod, lock behavior, key events) | §1.3 + §5 (registrar census) + §2.1 (lock behavior) |
| Event family table (event, payload, cancelable, priorities used, error behavior) | §4.2 (+ §3.5 for the critical subset; all non-listed events are non-critical → D15 toast behavior) |

**Open-question register:** no new items from this deliverable. The "10 event families" plan estimate is confirmed exactly (10 packages, §4.1); the "event firing on add/remove" question resolves to D14; the cancelable-but-unenforced behaviors are recorded as contract facts (D18), not unresolved questions, because the source is unambiguous about both the API and the in-repo checks. No `asset-validated` claims.
