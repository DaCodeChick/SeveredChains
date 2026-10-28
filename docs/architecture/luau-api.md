# Luau Scripting & Modding API — Design

Luau is the primary portable modding language (requirement §11). Design goals:
cover the Java mod capability surface (modding-analysis.md §3), add the seams
Java lacks (extensible combat input), keep the VM sandboxed and independent of
native plugins, and never let script errors become C++ exceptions.

## 1. VM & Lifecycle

- **One Luau VM per mod**, plus one shared `engine` VM for the built-in `lod`
  game module (if authored in Luau) — isolation: a crashing/looping mod
  can't corrupt another mod or the engine state.
- Lifecycle (mirrors Java mod boot, see modding-analysis.md):
  1. Package load (scripts extracted to a read-only view, see §5).
  2. `Engine.init` / `Engine.shutdown` global functions called in the mod's
     namespace (replacement for `@Mod` class load + static init).
  3. Registry population phase (mod registers content — mirrors Java's
     registry init order; engine enforces the same dependency order: config →
     campaignTypes → engineStateTypes → inputActions → characterTemplates →
     statTypes → statModTypes → goods → everything else).
  4. `loadingComplete` → mod `Engine.ready()`.
  5. Per-frame hooks run on the game thread inside the sim tick (exact Java
     call sites: `render` event, battle events, etc.).
  6. `Engine.shutdown` on game exit (or mod unload — restart-based, see
     native-api §4).
- **Sandboxing & resource limits** (Luau JIT off for safety in release;
  `Luau::setMemoryManager` with a per-VM memory cap, instruction-count cap
  per call via `Luau::setInstructionBudget` / step execution for
  long-running hooks, `noyield` where needed, read-only require graph):
  - No filesystem access (engine-controlled asset reads only, via `Assets`
    API with path policy).
  - No `os`/`io`/`debug` stdlib modules (Luau builds a minimal stdlib:
    `table`, `string`, `math`, `utf8`, `buffer` (if available in pinned
    version) — exclude `os`, `io`, `debug`).
  - Time budget: per-frame hook budget (e.g. 2ms wall, measured) — exceeded
    hooks get one warning then are disabled for that mod (reported in mod
    manager + crash-log style).
- **Error reporting without C++ exceptions** (requirement §3/§11): all
  engine→Luau calls go through `Luau::ProtectedFunction` /
  `Luau::protectedCall`-style trampoline; failures return engine `Result`
  errors with the Luau error string (`Luau::compile` diagnostics for load
  errors, runtime error string for calls). The mod is marked "errored" (its
  remaining hooks no-op, its content still valid) — mirroring Java's
  per-mod error isolation (`onModError`). Never `throw`/`try`/`catch` in C++
  around these calls; use the out-param + bool/status APIs the pinned Luau
  version provides.

## 2. Namespace Layout

```lua
Engine      -- lifecycle, events, registries, config, i18n, clock, scenes
Assets      -- asset queries/decodes/overrides
Input       -- actions, bindings, state
Render      -- render-event drawing (quads, models, text, UI boxes)
Combat      -- battle queries, additions, evaluators (§4)
Characters  -- templates, stats, additions queries
Items       -- items/goods/equipment/spells/shops
Saves       -- modData read/write, validation
Audio       -- music/sfx, volumes
Script      -- story-script functions & flag queries (parity with
            -- @ScriptDescription + ReadGlobalFlagsEvent)
Mod         -- this mod's metadata (id, version, config entries it registered)
```

## 3. Parity Surface (Java capability → Luau API)

| Java mod capability | Luau API |
|---|---|
| `@Mod(id, version)` + deps | `mod.toml` manifest + `Engine.init` |
| Registries (characters, stats, elements, additions, items, equipment, spells, shops, encounters, DEFFs, post-battle actions, battle actions, battle entity types/stats, element icons, campaign types, engine state types, input actions, configs, icons, atlas textures, level-up actions) | `Engine.Registry.register(kind, id, spec-table)` — spec tables per kind documented in a 1:1 mapping of the Java registry entry classes (e.g. `Characters.registerTemplate({...})`, `Additions.register({...hits = {...}, levels = {...}})`) |
| `@EventListener` methods | `Engine.Events.subscribe("battle.started", fn)` (event ids 1:1 with Java catalog) |
| Config entries | `Engine.Config.register(id, {type=, default=, category=, location=})` |
| Keybinds | `Input.registerAction(id, {defaults = {...}})` |
| i18n overrides | `lang/en.lang` (or `.toml`) files in the mod package — same key system as Java (`lod_core.ui...` keys) |
| Asset replacement | `Assets.override(path, file)` or package `assets/` tree (priority chain, see §5) |
| Save `modData` | `Saves.write(key, value-table)` / `Saves.read(key)` — serialized into the same Tag `ListTag` slot layout as Java (byte-compat for same mod id), `ReadSaveDataEvent`/`WriteSaveDataEvent` equivalents automatic |
| Story-script patching (fate) | fate files in package `patches/` — applied by the unpack/fate pipeline (tool parity, see asset-pipeline §6.4); plus `Script.registerFunction(op, fn)` for script-callable functions (Java `@ScriptDescription` parity) |
| `RenderEvent` drawing | `Render.onFrame(fn)` + `Render.drawQuad/drawText/drawModel/drawSprite/drawUIPart` (maps to the QueuedModel queue API) |
| DABA Save60 access | `Saves.originalCard` — read/write original-layout save slot fields (parity with `DabasManager`) |
| Turn-order-mod-style post-shader uniforms | `Render.setPostUniform(name, value)` (whitelisted uniform names, e.g. `turn_order_bounds`) |
| Battle: monster stats, rewards, spells, items, music | `Combat.*` query + event APIs (1:1 with battle event catalog) |
| Debugger visibility | `Debug.describe(...)` — registers inspectable state with the Qt debugger |

## 4. Extensible Combat (the required seam)

Retail combat behavior is registered by the `lod` module through these APIs,
so mods can replace pieces without core changes:

```lua
-- 1) Addition input evaluation (Playable Lloyd class)
local evaluator = Combat.Addition.newInputEvaluator({
    name = "face_sequence",
    evaluate = function(state)  -- state: {framesElapsed, window={lower,upper}, counter, ...}
        -- return "pending" | "success" | "failWrong" | "failLate" | "failEarly"
    end,
    drawOverlay = function(state, render) -- optional custom overlay
        -- default: the original timing square
    end,
})
Combat.Addition.useInputEvaluator("lod.dart.ultimate", evaluator)

-- 2) Hit/result resolution
Combat.Addition.defineResolver("my.addition", {
    onHit = function(ctx) -- ctx: {character, target, level, hitIndex, rolled}
        -- damage/SP/effect hooks; returns result table (retail resolver
        -- reproduces the original formula package results exactly)
    end,
})

-- 3) Custom battle actions / post-battle actions / DEFFs (registry parity)
Combat.registerAction({id="my.action", ...})
Combat.registerPostBattleAction({id="my.pba", fn=...})
Combat.registerDeff({id="my.deff", script=...})  -- CMB-format behavior script

-- 4) Animation sync
Combat.Animation.play(entity, "anm-ref", {speed=, loop=, onComplete=})
```

- The combat engine consumes these through the same evaluator interface the
  C++23 plugin API exposes (`lod::CombatAPI::AdditionInputEvaluator`,
  native-api.md §7) — **one implementation, two front ends** (Luau and native
  plugins interoperate: a native plugin can read which evaluator an addition
  uses; a Luau mod can react to combat events fired natively).
- **Timing guarantee:** `evaluate` is called exactly once per sim frame during
  the window (same call site as Java's per-frame `isActionPressed` check), so
  window bounds remain frame-exact with the original.

## 5. Mod Package Format

```
my-mod/                     # or my-mod.zip (same layout)
  mod.toml                  # manifest:
                            #   id, version (semver), name, author,
                            #   license (SPDX), description,
                            #   depends = [ {id="lod", version="^3"} , ...],
                            #   api_version = "1.0",
                            #   entry = "init.luau"
  init.luau                 # entry script (Engine.init entry)
  src/...                   # additional .luau (require by relative path)
  assets/...                # asset overrides (path-mirrored, see below)
  lang/en.lang              # i18n overrides
  patches/                  # fate patches (applied at unpack, like Java patches/)
  native/                   # optional: my-mod.so|.dll|.dylib (+ manifest)
  config/                   # optional preset configs (ConfigPreset parity)
```

- **Asset override resolution** at load time: the asset manager checks
  `mod.assets` (enabled mods, reverse load order) → base `files/` tree.
  This replaces Java's unpack-time `patches/replacements` with a runtime
  chain (more flexible; same result for the default case).
- **Dependencies:** semver range matching on mod ids (parity with Java
  version ranges); cycle detection; missing → mod skipped with a clear
  report (parity: `getMissingMods`); wrong version → reported
  (`getWrongVersions` parity).
- **Existing Java/JAR mods are NOT binary compatible** (stated requirement) —
  migration path (documented for mod authors):
  1. Content mods (registries only): mechanical port — registry specs map 1:1
     (§3 table); provide a compatibility shim era of the API.
  2. Event-driven mods: `@EventListener` → `Engine.Events.subscribe`.
  3. Rendering mods (`RenderEvent` + render API): port to `Render.*` (API
     shapes match the Java render queue).
  4. fate/script mods: unchanged (fate format parity).
  5. Java-only mods (reflection/classloader tricks): reimplementation —
     track in a mod-porting backlog with the community.
- **Native + Luau independence** (requirement): a mod may ship only Luau,
  only native, or both. They share the same event/registry/config services;
  no mutual dependencies (native plugins must not require Luau — the C++
  plugin API has no Luau symbols; the engine mediates).

## 6. Luau Dependency

- **Luau** (MIT, lua-lang/Luau) — pinned version, built from source (the
  standard `prebuilt/VM` build, no-exceptions compile flags: Luau is
  exceptions-free C++ by design; verify the pinned version's flags in
  build-system.md).
- Luau provides the VM + compiler + stdlib + `luau-analyze` (static checks)
  + `luau-compile` (bytecode precompilation for faster mod load) +
  `luau-re` (for the debugger). All are the one approved dependency
  (requirement §2) — no further Lua-ecosystem deps.
- Mod scripts are **bytecode-precompiled** into the package cache
  (deterministic, versioned by Luau compiler version) — load = `loadbytecode`.
- **Luau version policy:** pin exact version; mods declare `api_version`
  (engine API), not Luau version (engine owns that). Language-level changes
  between Luau patches are handled by the engine team only.

## 7. Debugger Integration

- `Debug.describe` + Luau stack traces surface in the Qt 6 debugger
  (qt-integration.md): live script inspection (the Java `script_live_debugger`
  parity), step execution via instruction budgets, variable views (tables →
  tree view), per-mod VM state.

## 8. Risks

1. **Luau budgeting under no-exceptions:** all VM calls must be guarded
   (protected calls everywhere); the boundary layer is a hot path — measure
   per-frame hook overhead (target: <50µs for typical mod loads; the Java
   event dispatch had comparable cost).
2. **Bytecode compatibility across Luau updates:** precompiled cache must be
   keyed by compiler version (handled by cache key).
3. **Fate pipeline parity** is an external-tool dependency (see
   asset-pipeline §6.4) — a Luau content mod that ships fate patches depends
   on our fate tool being a faithful port; mitigate with format round-trip
   tests against Java output (disassemble→assemble identity on real scripts).
