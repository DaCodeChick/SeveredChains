# Native Extension API — C++23 Plugin Interface

Design for first-class native plugins (`.dll` / `.so` / `.dylib`).
Requirements: **C++23 at the boundary (not a C ABI)** — a C-linkage
*discovery* entry point is the only C-isms allowed; stable ABI; version
negotiation; explicit ownership (RAII); no-exceptions; no RTTI; no EnTT
registry exposure; no `psx::` type exposure; not Qt's plugin system.

## 1. Core Principles

1. **The engine exports; plugins consume.** `scengine` builds as a shared
   library; the player-facing executable is a thin host; standalone tools
   (Qt debugger, fate CLI) link the same engine. *(Unchanged from Phase 0.)*
2. **C++23 at the boundary.** Plugins are C++23 shared libraries built
   against the engine **SDK** (headers + import library + pinned toolchain,
   §9). The only C-linkage artifact is the *discovery* entry symbol
   (§2) — everything a plugin implements and everything it calls is C++:
   abstract interface classes, RAII handles, `std::string`,
   `std::expected`/`Status`. A C ABI is *not* used anywhere in the actual
   interface.
3. **Binary compatibility = frozen-layout discipline + toolchain parity**
   (§9.2): existing interface classes never gain virtuals or data members;
   new capability = new interface class; plugins are built with the same
   compiler family + standard library as the engine (toolchain hash
   checked at load). This is the standard C++ shared-library ABI contract
   (same class of policy as LLVM/Chromium component ABIs) — and it is
   *stricter and more auditable* than a hand-rolled C ABI.
4. **One-way versioning.** Plugins declare a **minimum** engine API major
   version; the engine exposes versioned interface classes, discovered
   through one stable query on `EngineAPI` (§3). New engine versions add
   interfaces; they never remove or change existing ones.
5. **Explicit ownership (RAII).** Engine-created objects handed to plugins
   are borrowed (const or engine-owned `Shared<T>` — §9.3); plugin-created
   objects handed to the engine transfer ownership via
   `std::unique_ptr` at the documented transfer points only. No raw
   delete across the boundary. Handle-leak verification at unload (§4).
6. **Errors, not exceptions.** `lod::Status` (enum class) or
   `std::expected<T, lod::Status>` for every fallible operation; no
   `throw`/`try`/`catch` anywhere (compile-enforced on both sides; the SDK
   toolchain file sets `-fno-exceptions`/`/EHs-c-` — error-handling.md).
   This is a deliberate **narrowing** of the project-wide
   "permitted but discouraged" policy (AGENTS.md §4): the plugin ABI stays
   exception-free whatever the engine's own build settings are, because a
   throw across a third-party-compiled boundary cannot be unwound safely.
   RTTI is likewise disabled (`-fno-rtti` / `/GR-`) — the API never uses
   `dynamic_cast`; interface identity is carried by id + version constants.
7. **Thread safety.** Default contract: **all API calls are
   main-thread (sim/render) unless the method is documented
   `[audio-thread]` or `[thread-safe]`.** Same as Phase 0.
8. **Lifecycle is a state machine** (§4) — the engine refuses (with a
   clear error) calls in the wrong state.
9. **Not Qt's plugin system.** The engine implements its own loader
   (dlopen/LoadLibrary/dyld + the contracts below). Qt 6 is used only by
   the separate-process desktop tools (qt-integration.md) and has no role
   in plugin loading.

## 2. Discovery Entry Point (the only C linkage)

A plugin exports exactly one C-linkage symbol per supported API major:

```cpp
// SDK: lod/plugin.hpp
namespace lod {

struct PluginInfo;
class EngineAPI;

// Implemented by the plugin (one instance per plugin, plugin-owned
// lifetime — typically a static object). Returned pointer is valid until
// the plugin is unloaded; the engine never deletes it.
class Plugin {
public:
  virtual ~Plugin() = default;
  virtual const PluginInfo& Info() const = 0;
  // Called after load, before READY. `api` outlives the plugin's READY
  // period. Returning a non-OK Status aborts activation (plugin reported
  // failed — parity with Java's failed-mod UX).
  virtual Status OnLoad(EngineAPI& api) = 0;
  virtual void OnReady() = 0;
  // Called before unload. The plugin must release every engine resource
  // it holds (the engine verifies — handle-leak check, §4).
  virtual void OnShutdown() = 0;
};

// The discovery symbol (C linkage, versioned by API major).
extern "C" LOD_API const Plugin* LoDPluginCreateV1();
}  // namespace lod
```

The engine looks up `LoDPluginCreateV<max>` down to `V1` (first hit wins);
a plugin compiled against SDK v1 exports only `LoDPluginCreateV1` and loads
on any engine ≥ v1. (If a future v2 changes the `Plugin` vtable layout,
both symbols can coexist; v1 symbols are never removed while supported.)

**`PluginInfo`** (value type; `std::string`s — see §9.2 toolchain rule):

```cpp
struct PluginInfo {
  std::string ID;            // scoped id: "example.dragoon_fury"
  std::string Version;       // semver "1.2.0"
  std::string Name;
  std::string Author;
  std::string License;       // SPDX id (per-mod license metadata — not assumed AGPL)
  uint32_t MinAPIVersion;    // engine API major the plugin was built against
  std::vector<std::string> Depends;  // mod/plugin ids (dependency closure checked by engine)
  uint64_t Flags;            // LOD_PLUGIN_FLAG_* (GAMEPLAY | ASSETS | COMBAT | ...)
  std::string Toolchain;     // compiler+stdlib hash (checked at load — §9.2)
};
```

Public data members of a project-owned aggregate are PascalCase and the acronym
rule applies to them (AGENTS.md naming section, cpp.md §7): this struct is a
frozen *layout*, not a C header translation, so it is spelled the project way.
"Frozen" constrains which members exist and their types, never their casing. The
`id` values mentioned below are string ids, which stay lowercase — a different
namespace from C++ identifiers.

## 3. Engine Interface (engine → plugin)

`EngineAPI` is the **frozen root** (v1): after v1 it never gains virtuals
or members. Capability discovery is one stable method; each capability is
a separate **frozen interface class** with typed constants:

```cpp
// SDK: lod/engine_api.hpp
namespace lod {

class EngineAPI {
public:
  virtual ~EngineAPI() = default;
  virtual const EngineVersion& Version() const = 0;      // "0.1.0+commit", channel
  // Stable discovery entry. Returns nullptr if the running engine does
  // not provide `id` at `version` (the plugin degrades gracefully,
  // logged). `id` values are the LOD_API_*_ID string constants.
  virtual void* QueryTable(const char* id, uint32_t version) = 0;

  template<typename T>
  std::optional<T*> Table(uint32_t version = T::Version) const {
    return static_cast<T*>(QueryTable(T::ID, version));
  }
  // Engine-side diagnostics available to the plugin.
  virtual void Log(Verbosity level, const std::string& tag,
                   const std::string& message) = 0;
};

// Example capability interface (frozen at v1):
class GameplayAPI {
public:
  static constexpr const char* ID = "lod.gameplay";
  static constexpr uint32_t Version = 1;
  virtual ~GameplayAPI() = default;
  virtual uint32_t EntityCount(SceneHandle scene) const = 0;
  virtual std::expected<uint32_t, Status>
      EntitySpawn(SceneHandle scene, const SpawnDesc& desc) = 0;
  virtual void EntityDestroy(SceneHandle scene, EntityHandle e) = 0;
  virtual std::expected<Transform, Status>
      EntityTransformGet(SceneHandle scene, EntityHandle e) const = 0;
  virtual void EntityTransformSet(SceneHandle scene, EntityHandle e,
                                  const Transform& t) = 0;
  virtual GameState GameState() const = 0;               // enum + chapter...
  virtual Timestamp GameClock() const = 0;
};
}  // namespace lod
```

**Value types at the boundary** (SDK header, frozen per version):

```cpp
struct Vec3 { float X, Y, Z; };
struct Quat { float X, Y, Z, W; };
struct Mat4 { float M[16]; };              // column-major
struct Color { uint8_t R, G, B, A; };
using Timestamp = int64_t;
struct EntityHandle { uint32_t ID; };      // scene-entity id; NOT an EnTT type
struct SceneHandle  { uint32_t ID; };      // scene context id
using RegistryId = std::string;            // "modid:entryid" (grammar per mod-loader.md §4)
```

These are project-owned SDK value types, so their fields take the data-member form
(camelCase would be reserved for a *scoped* variable or a private member, see
AGENTS.md naming section); single-letter members are capitalised accordingly. A
math type that turns out to be a GLM alias instead is third-party-named and keeps
GLM's spelling.

**Rules for boundary types (the C++ version of "no STL by accident"):**
- `std::string` / `std::vector` are **allowed and normal** across the
  boundary — safe under the toolchain-parity rule (§9.2), which the
  loader *enforces* (mismatched toolchain hash → load refused).
- `std::string_view` / `std::span` may be used **only as non-owning
  inputs, valid for the duration of the call** (documented per method).
- The EnTT registry is never exposed (entities are `EntityHandle`s;
  components are queried via typed methods on `GameplayAPI` /
  `CombatAPI`). `psx::` types are never exposed (engine services hand out
  decoded *results* as SDK value types).
- Every interface class and value struct is **layout-frozen** once shipped
  in a major version (§9.2).

**Registry access** (`RegistryAPI`, mirrors mod-loader.md semantics):

```cpp
class RegistryAPI {
public:
  static constexpr const char* ID = "lod.registry";
  static constexpr uint32_t Version = 1;
  virtual ~RegistryAPI() = default;
  // Lazy resolution: missing entry → Err(Status::ModNotLoaded) at USE
  // time (parity with Java RegistryDelegate behavior), attributed to the
  // owning mod id.
  virtual std::expected<EntryRef, Status> Entry(const RegistryId& id,
                                                EntryKind kind) const = 0;
  virtual std::vector<RegistryId> EntryList(EntryKind kind) const = 0;
  virtual std::expected<bool, Status>    ConfigBool(const RegistryId& id) const = 0;
  virtual std::expected<int32_t, Status> ConfigInt(const RegistryId& id) const = 0;
  virtual std::expected<float, Status>   ConfigFloat(const RegistryId& id) const = 0;
  virtual std::expected<std::string, Status> ConfigString(const RegistryId& id) const = 0;
};
```

## 4. Lifecycle

```
discover (mod dir scan: <mod>/native/*.so|dll|dylib + `LoDPluginCreateV*` presence)
  → validate (PluginInfo schema, id uniqueness, dependency closure,
              MinAPIVersion <= engine major, toolchain hash match §9.2)
  → load     (dlopen RTLD_LAZY / LoadLibrary / dlopen-macos; symbol lookup
              LoDPluginCreateV<max..1>; capture loader error)
  → init     (Info() + OnLoad(api): plugin fetches tables via api.Table<T>(),
              registers callbacks/entries; any Err → plugin marked failed,
              unloaded, reported in mod list — parity with Java failed-mod UX)
  → ready    (OnReady(); registered events fire; script functions available)
  → shutdown (OnShutdown(); engine verifies the plugin holds no engine
              resources — handle-leak check, logged; engine then unloads)
  → unload   (dlclose; Plugin* and all engine-side plugin state destroyed)
```

- **Hot reload: not supported in v1** (unchanged): unload with live
  resources (GPU, audio, registered callbacks) is unsafe; reload =
  orchestrated restart (Qt mod manager; mirrors Java's update-restart).
- **Crash containment:** a crashing plugin takes down the process —
  in-process dlopen limitation (unchanged). Mitigations: strict API
  contracts + ABI/fuzz/sanitizer tests; an out-of-process mode remains a
  documented future work item (the interface design — plain values +
  id/version discovery — keeps it possible without redesign).
- **Cross-plugin isolation:** plugins cannot reference each other's
  symbols (loader keeps per-plugin symbol visibility where the platform
  allows; otherwise documented). Coordination is via engine events/
  registries only.
- Failure at any stage → logged + surfaced in mod manager + in-game
  notification (parity: `GameOverlay.addNotification`).

## 5. Capability Areas (interface map)

| Interface (`ID`) | Capabilities |
|---|---|
| `lod.gameplay` (`GameplayAPI`) | entity queries/spawn/destroy, transforms, game state/clock, flags |
| `lod.events` (`EventsAPI`) | subscribe/unsubscribe (§6), post custom events (visible to Luau + native listeners) |
| `sc.input` (`InputAPI`) | read action state (pressed/held/repeat/axis), define new actions, bind queries |
| `sc.render` (`RenderAPI`) | custom draw callbacks on the render event (scoped `RenderScope`: pipelines created via engine builder, textures, vertex/index buffers), post-shader uniform hooks (e.g. `turn_order_bounds`-style) |
| `sc.audio` (`AudioAPI`) | register music (sequence file refs), play SFX from soundbanks, volume control, music event queries; `[audio-thread]`-marked callbacks documented |
| `sc.assets` (`AssetsAPI`) | register path-override roots (asset replacement), query/decode assets via engine decoders (TMD/TIM/ANM... → SDK value types), upload decoded data to GPU |
| `sc.save` (`SaveAPI`) | read/write the plugin's `modData` slot (scoped by plugin id; serialized as Tag entries — byte-compat with the Java `modData` slot layout for the same id), save-validation hooks |
| `sc.script` (`ScriptAPI`) | register story-script functions (op → callback with typed param in/out), query script flag state (parity with `ReadGlobalFlagsEvent` + `@ScriptDescription`) |
| `sc.combat` (`CombatAPI`) | battle lifecycle queries, **addition input evaluator registration** (§7), hit result overrides |
| `sc.log` (`LogAPI`) | leveled tagged logging (also available on `EngineAPI::Log`) |

## 6. Events (C++)

```cpp
// SDK: lod/events.hpp
namespace lod {
using EventID = std::string;   // "battle.turned", "render", ...

struct Event {
  EventID id;
  uint64_t seq;                       // monotonic
  // Typed payload access (per-event accessors documented 1:1 with the
  // Java event catalog — the catalog table ships in the API reference):
  // e.g. BattleTurnedEvent payload of event "battle.turned":
  //   auto turn = e.Payload<BattleTurnedEvent>();
  std::expected<void, Status> Cancel() const;   // CancelableEvent parity:
};                                        // stopPropagation/cancel semantics
using EventCallback = std::function<void(const Event&)>;

class EventsAPI {
public:
  static constexpr const char* ID = "lod.events";
  static constexpr uint32_t Version = 1;
  virtual ~EventsAPI() = default;
  // `handler` may be a lambda/std::function capturing plugin state; the
  // engine copies it (toolchain rule applies). Priority semantics mirror
  // mod-loader.md §3 (higher first; same priority = registration order).
  virtual std::expected<uint32_t, Status>
      Subscribe(const EventID& id, EventCallback handler,
                Priority priority = Priority::Normal) = 0;
  virtual void Unsubscribe(uint32_t subID) = 0;
  virtual std::expected<void, Status> Post(const EventID& id,
                                           const EventPayload& payload) = 0;
};
}  // namespace lod
```

- Synchronous dispatch on the calling thread (parity — mod-loader.md §3);
  handler exceptions are impossible (no exceptions); a handler signaling
  failure returns via the event's cancel path or `EngineAPI::Log`.
- Event ids mirror the Java catalog 1:1 (`render`, `engine.state.changed`,
  `battle.started`, `battle.ended`, `battle.entity.turn`, `monster.stats`,
  `enemy.rewards`, `input.pressed`, `input.released`, `game.loaded`,
  `game.saving`, `inventory.*`, `submap.*`, `worldmap.*`,
  `character.created`, `script.flags.read`, ...).

## 7. Combat Extensibility (the Playable-Lloyd seam)

Java hardcodes addition input to the attack/counter actions
(`AdditionOverlaysEffect44`). The native combat engine factors this into a
**C++ interface shared by Luau and native plugins** (one implementation,
luau-api.md §4):

```cpp
// SDK: lod/combat.hpp
namespace lod {
struct AdditionHitState {
  uint32_t frame;                    // sim frame within the hit window
  uint32_t lowerBound, upperBound;   // from AdditionHitProperties10 (× timing-window config)
  bool isCounter;
  // action query helpers (read the 5-battle-action state for `frame`):
  std::expected<bool, Status> ActionHeld(BattleAction action) const;
};
enum class AdditionHitResult { Pending, Success, FailWrong, FailEarly, FailLate };

class AdditionInputEvaluator {
public:
  virtual ~AdditionInputEvaluator() = default;
  virtual const std::string& ID() const = 0;
  // Called per sim frame during an addition hit window.
  virtual AdditionHitResult Evaluate(const AdditionHitState& state) = 0;
  // Optional: custom overlay draw (default = the retail timing square).
  virtual void DrawOverlay(const AdditionHitState& state, RenderScope& rs) {}
};

class CombatAPI {
public:
  static constexpr const char* ID = "lod.combat";
  static constexpr uint32_t Version = 1;
  virtual ~CombatAPI() = default;
  // Ownership: the engine takes the unique_ptr (transfer point, §1.5).
  virtual std::expected<void, Status>
      RegisterAdditionInputEvaluator(std::unique_ptr<AdditionInputEvaluator> ev) = 0;
  virtual std::expected<void, Status>
      SetAdditionEvaluator(const RegistryId& additionId,
                           const std::string& evaluatorId) = 0;
  // battle lifecycle queries...
};
}  // namespace lod
```

- Retail content registers the **timing-square evaluator** (attack/counter
  actions, window bounds, timing-window multiplier) — behavior identical to
  Java.
- A mod (Luau *or* native) registers an alternative (e.g. face-button
  sequences) and points specific additions at it. The combat state machine
  consumes `Evaluate()` results only — no core rewrite (requirement §11).
- Luau parity: `Combat.Addition:setInputEvaluator(...)` (luau-api.md) is an
  adapter over this same interface.

## 8. SDK Packaging

`sc-sdk` (build-system.md §2):
- `include/lod/*.hpp` — the frozen interface headers (this contract).
- `scengine` import library (VS `.lib` / ELF `.so` / Mach-O) — plugins link
  it; symbols resolve against the loaded engine at dlopen time.
- **Toolchain file** (`sc_toolchain.cmake`): sets C++23,
  `-fno-exceptions -fno-rtti` (MSVC `/EHs-c- /GR-`), warning flags,
  visibility flags, and records the toolchain hash string that
  `PluginInfo::toolchain` must carry (a CMake target property the SDK's
  manifest generator fills in).
- Sample plugin (`plugins/examples/`) + CMake boilerplate — the
  mod-author reference.
- The SDK **cannot** be built with exceptions/RTTI enabled — CI compile
  probe (the throw/rtti probes, build-system.md §2) runs on SDK headers
  with a minimal TU.

## 9. ABI Stability Policy

### 9.1 Versioning
- **API major** = the `Plugin` entry vtable + `EngineAPI` root layout.
  Symbol `LoDPluginCreateV<MAJOR>`; engine tries newest→oldest.
- **Interface version** (`T::Version`) = per-capability class layout.
  `QueryTable(id, version)` returns the highest version ≤ requested that
  the engine provides, or nullptr.
- Within a major: **frozen layout** — no added virtuals, data members, or
  changed enum values on shipped classes/structs; additive changes only
  (new methods appear only in a *new* interface class or a new version of
  that interface, `Version` bumped, old version retained).
- Breaking change = new major + old symbols/versions retained until the
  deprecation policy says otherwise (semver on the API itself).

### 9.2 Toolchain parity (the C++ compatibility anchor)
- A plugin **must** be built with the same compiler family and standard
  library as the engine: Windows = MSVC, Linux = the engine's GCC *or*
  Clang (one per engine build; recorded), macOS = AppleClang/libc++.
- The engine records a **toolchain hash** (compiler id+version+stdlib
  convention flags) at build time; `PluginInfo::toolchain` must match or
  the load is refused with a diagnostic naming both (this makes the
  `std::string`/`std::function`/vtable assumptions *checked*, not hoped).
- Why this is required: vtables are compiler-ABI-defined (Itanium/MSVC)
  and `std::` type layouts are stdlib-defined; no C++ compiler flags make
  a mixed-toolchain boundary safe. This is the documented cost of a C++
  (vs C) boundary, and it is what makes the interface usable as C++.

### 9.3 Ownership primitives
- Borrowed: `const T&` / `T&` / `Shared<T>` (engine-defined intrusive
  refcount — stable layout, no stdlib dependence).
- Transfer: `std::unique_ptr<T>` at explicitly documented points (e.g.
  `RegisterAdditionInputEvaluator`, resource creation APIs).
- Plugin-owned: everything else a plugin creates; released in
  `OnShutdown()`; verified by the handle-leak check.

### 9.4 ABI tests (testing.md §2.9, updated)
- **Oldest-plugin matrix**: a sample plugin compiled against SDK v1
  headers (same toolchain) loads and passes functional tests on the
  current engine (CI).
- **Frozen-layout probe**: the SDK headers carry `static_assert`s on
  sizes/offsets of every frozen type; CI compiles the probe against the
  SDK and against the *installed previous engine* headers — catches any
  accidental layout drift.
- **Negative tests**: wrong toolchain hash → refused load; missing entry
  symbol → clean failure report; unknown table id → nullptr + graceful
  degrade.
- **Compile probes** on SDK headers: `-fno-exceptions` (throw) and
  `-fno-rtti` (dynamic_cast) must fail to compile — the API surface
  enforces the project-wide policy by construction.
- Fuzz + ASAN/UBSAN/TSAN on the boundary (unchanged).

## 10. Limits & Documented Non-Goals

- No hot reload; no JIT; no plugin-provided GPU devices.
- No cross-plugin symbol references.
- Plugins cannot register *new registry kinds* in v1 (Luau can; v2
  consideration — Java allowed it; assess demand).
- Mixed-toolchain plugins are refused (documented above) — the mod author
  uses the shipped SDK toolchain file; this is a *documented build
  requirement*, mirroring how Java mods had to be compiled for the game's
  Java version.
- Audio callback timing: synth/SFX mixing stays on the audio thread;
  `[audio-thread]` callbacks carry the same no-heap-alloc expectation as
  Java's audio thread (documented).