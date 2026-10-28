# Engine Design — Native C++23 Architecture

Status: proposal (Phase 0). Backed by the source research in `docs/research/`.
Goal: a clean, no-exceptions, C++23 engine that preserves Severed Chains'
behavior while dropping the Java/ROM-mirroring organization.

## 1. Module Layout (source tree)

```
CMakeLists.txt
psx/                         # INDEPENDENT reusable PS1 library — target `psx`,
                             #   namespace psx::, PREFIX-FREE (no Sc/SC/sc, no LoD/LOD/lod), zero
                             #   deps, compiles standalone;
                             #   self-contained subproject, may be extracted to its
                             #   own repo (build form: CMake BUILD_SHARED_LIBS). gte, gpu (+ software reference
                             #   rasterizer), spu, xa, sssq, cdrom, tmd, tim, anm,
                             #   archive (MRG/DEFF), bpe, tag, script (dis/asm).
                             #   See psx-library.md.
src/
  engine/                    # reusable, LoD-agnostic (the "engine")
    app/                     # App, MainLoop, Scheduler (Action equivalent), diagnostics
    platform/                # SDLPlatform, Window, Input (actions/bindings/devices),
                             #   Displays, Rumble, clipboard, URLs
    gpu/                     # SDLGpuDevice wrapper, frame graph (command buffers,
                             #   render passes, fences/cycling), pipeline cache,
                             #   resource lifetimes (RAII over SDL_GPU handles)
    render/                  # Renderer (frame structure), Batches, ModelPool,
                             #   QueuedModel equivalents, TextureManager,
                             #   Camera, post-processing, scissor stack;
                             #   legacy/VRAM modes drive psx::gpu's rasterizer
    audio/                   # AudioDevice (SDL3 audio), MusicSynth (120 Hz
                             #   sequencer scheduling — drives psx::spu voices),
                             #   Sfx (LoD SPU voice allocation), XaStream
                             #   (psx::xa decoded cache), resampling, volume model
    assets/                  # AssetManager (path chain: mod overrides → base),
                             #   async loaders, cache, error results
    ecs/                     # EnTT registry wrappers + component conventions
                             #   (actors, cameras, renderables, animations)
    script/                  # story-script VM (OpType port), script functions;
                             #   fate patch pipeline (uses psx::script tooling)
    save/                    # LoD save schema v10, migrations, mod data slots
                             #   (serialized with psx::tag)
    config/                  # ConfigCollection (typed entries, presets, global)
    events/                  # typed event bus (sync, cancelable, per-sender error
                             #   isolation)
    i18n/                    # lang keys + overrides (4-level specificity)
    mod/                     # mod package format, manifest, discovery, load order,
                             #   Luau VM host, native plugin host (C++23 API —
                             #   see native-api.md)
    util/                    # Result/Expected helpers, hashing, logging,
                             #   math (GLM wrappers), pools, non-throwing IO
  game/                      # LoD-specific ("game" — the native equivalent of LodMod
                             #   + the decompiled game code)
    content/                 # retail content registration: characters, stats,
                             #   additions, items, equipment, spells, shops,
                             #   encounters, campaigns, engine-state types,
                             #   elements, input actions, configs  (the "lod mod")
    decoders/                # LoD-only container decoders (CContainer, MCQ, CMB)
                             #   emitting psx:: IR types
    dabas/                   # host adapter for the Mogul Dabas minigame
                             #   (DabasScreen, rewards, save blob — dig-dabas.md)
    title/ submap/ wmap/ battle/ inventory/ text/ credits/ fmv/
    (each state: state object + tick/postScriptTick/overlayTick + save data)
  libs/
    dabas/                   # scdabas — Mogul Dabas handheld console + minigame
                             #   logic port (no SDL/Qt; host-interface driven)
  tools/
    qt/                      # Qt 6 desktop: launcher, settings, mod manager,
                             #   debugger, asset inspector (see qt-integration.md)
    fate/                    # fate CLI (disassemble/assemble/patch) — standalone,
                             #   links psx::script + the fate compiler
  plugins/
    examples/                # sample native plugins (C++23 SDK consumers)
tests/                       # unit + integration (see testing.md)
gfx/shaders/                 # GLSL sources (ported from reference, renamed/organized)
resources/                   # engine resources (fonts, UI art — non-copyrighted only)
```

**Boundary rules:** `psx/` (target `psx`) is dependency-free (C++23 standard only)
and never includes engine, game, SDL, Qt, or Luau headers — it compiles
standalone and is prefix-free by design (independent project identity,
psx-library.md). `libs/dabas` depends on nothing but the C++ standard
(host-interface driven). `src/engine/` must not include `src/game/` headers;
both may include `psx/`. The engine links `psx` for the legacy/VRAM render
modes (psx::gpu), GTE camera math (psx::gte), SPU/XA/SSSq audio (M5), and
decoders (M6). `src/game/` registers all
content through the same mod API surface that third party mods use (see §6).
`psx::` types never cross the plugin boundary or the Luau API (native-api.md §1,
luau-api.md §7). Qt code (`src/tools/qt`) links engine but not game
where possible; the debugger needs game introspection hooks (see qt-integration).

## 2. Application Lifecycle & Main Loop

```
main()
 └─ App::run(args)
     ├─ Platform::init            # SDL_Init(VIDEO|GAMEPAD; AUDIO later)
     ├─ Diagnostics::init         # log init, crash handler, build info
     ├─ Config::load (global)
     ├─ Mods::discover + boot     # manifest validation, load order (deps),
     │                            #   registries, events, i18n, input bindings
     ├─ Gpu::init                 # SDL_CreateGPUDevice (see rendering doc)
     ├─ Audio::init (device + synth/SFX threads)
     ├─ Renderer::init (pipelines, targets, UI quads)
     ├─ Game::boot                # "lod" content mod registers everything;
     │                            #   lastSavableState tracking
     └─ MainLoop::run             # see below
```

**MainLoop** (single-threaded game, mirrors the verified Java structure but with
explicit sim/render separation):

```
while running:
    Platform::pollEvents()          # SDL_PollEvent → Window/Input state
    Scheduler::tickSim(now)         # fixed-rate sim ticks (30/60Hz per vsyncMode
                                    #   and speed multiplier; see §3)
        for each due sim tick:
            Game::tick(simFrame)    # = gameLoop order: endFrame → GPU.startFrame
                                    #   → timestamp → state.tick → events →
                                    #   menus → scripts.tick → postScriptTick →
                                    #   transitions → SFX start → textboxes →
                                    #   overlayTick → GPU.endFrame
    renderPending = needFrame()     # present when a sim tick produced output
    if renderPending:
        Renderer::renderFrame(...)  # game render callback (queue models) +
                                    #   batches + post → swapchain
        present
```

- The Java engine reprogrammed its 60Hz Action per frame; we keep the same
  *semantics* (sim rate = `60 / vsyncMode * speedMultiplier`) with a clean fixed
  step accumulator. Rendering runs at the display rate (vsync on); at 30Hz sim
  we present the last simulated frame on the odd display frames (matching the
  Java frame-skip presentation). Speed multipliers scale sim frequency exactly
  like the original.
- **Implemented contract (application-foundation milestone).** The loop above is
  live in `lod::App` with these concrete contracts:
  - *Events:* every subsystem can implement `PollFrame() -> FrameEvents`
    (`src/engine/include/lod/frame.h`); the loop folds the results into one
    frame summary with `MergeFrameEvents` — `quitRequested`/`resized`/
    `focusChanged` are sticky ORs, while size and focus take the newest **valid**
    contribution (an iconify's 0×0 is recorded as a resize but never overwrites
    the last usable size, which is what a swapchain would have to work with).
    Only initialized, not-yet-shut-down subsystems are polled. A quit request —
    OS event or `App::RequestQuit()` — ends the loop at the frame boundary
    *after* that frame's sim ticks, so input carried by the quitting event is
    not dropped.
  - *Timing:* fixed step with a clamped delta:
    `accumulator += min(now - previous, MaxFrameDeltaSeconds)`, then at most
    `MaxTicksPerFrame` ticks. The clamp is what stops a stall (modal dialog,
    debugger, window drag) from becoming a catch-up storm; the per-frame cap
    bounds one frame while the leftover accumulator is **retained**, so no sim
    time is silently lost. `RunOptions::MaxTicks` is an `optional`: a value =
    that exact number of ticks (a frame never runs past the budget), `nullopt` =
    run until quit. The step is carried in floating-point microseconds so a 60Hz
    step cannot drift, and the pacing sleep rounds *up* — a truncated
    sub-microsecond sleep would spin without the clock ever advancing.
  - *Measurement:* `LoopStats` reports frames, ticks, clamped/capped frame
    counts and the accepted (post-clamp) plus raw-max frame delta, logged at
    Info level on loop exit. These are the numbers the timing tests assert with
    injected clocks, so pacing is verified rather than assumed.
- Threading: main thread = events + sim + render (like Java). Dedicated audio
  thread (synth @120Hz + SFX mix @60TPS, ring buffers into SDL3 audio stream).
  Async pool (std::jthread + task queue) for file loads/unpack, mirroring `Async`.
- Shutdown: deterministic teardown order (reverse of init); all subsystems
  implement `deinit() -> Result`; crash handler writes crash log + optional
  recovery save (verified behavior in `Main.java`).

## 3. Timing Contract (from verified Java behavior)

- `vsyncMode` ∈ {1, 2, ...} (frames per game tick; 2 in most field states, 1 in
  60Hz states) — part of engine state, affects `timestamp += vsyncMode` per tick.
- Sim tick rate = 60 / vsyncMode Hz × speedMultiplier (1..16).
- All combat timing (Addition windows, camera scripts, SEffe, effects) is in
  **sim frames** — preserved exactly. Rendering never advances game state.
- `timestamp_a0` / `tickCount` globals preserved as `GameState` fields (used by
  scripts/effects).
- Determinism: per-campaign RNG seed (port `simpleRandSeed` LCG + `legend.core
  .Random` algorithm); same seed + same inputs → same sequence. Save stores seed.

## 4. Core Subsystem Designs

### 4.1 Resources & ownership

> **Implemented (resource-management milestone):** the *CPU-side file* layer —
> root validation, discovery, path-confined loading, move-only owning blobs and
> psx-backed structural validation — is `lod::res`, documented in
> [resource-management.md](resource-management.md) with its ownership rules and
> the mechanisms deliberately left out. Everything below in this section is the
> **GPU-side** ownership model, which is still future work (M2/M4).

- RAII wrappers over `SDL_GPU*` handles (`GpuBuffer`, `GpuTexture`, `GpuSampler`,
  `GpuShader`, `GpuPipeline`, `CommandBuffer` scopes). No raw `SDL_Release*` in
  game code.
- **Upload ring**: one big `GpuTransferBuffer` ring (cycling) for per-frame data
  (transform blocks, primitive params, CLUT animations, per-draw uniforms) +
  an asset upload queue (async load → upload on next frame).
- **Deferred destruction**: resources marked `deleteNextFrame` after the frame
  that might still use them (mirrors `Obj.deleteObjects()`); backed by
  `SubmitGPUCommandBufferAndAcquireFence` + fence tracking for correctness.
- CPU-side pools: arena/stack allocators for per-frame model queues (no
  per-frame heap churn); object pools for game entities.

### 4.2 Events (engine bus)

- Synchronous, typed: `EventBus::post<T>(T&&)`; listeners registered at mod
  boot (registry-driven) or at runtime (Luau/native).
- `CancelableEvent` semantics (first `cancel()` wins).
- Per-listener error containment: a listener failure logs + isolates (no crash),
  mirroring `onModError`.
- Event list (ported from `legend.game.modding.events` — see modding-analysis):
  render, engine-state change, battle lifecycle (start/end/turn/rewards/...),
  input pressed/released, inventory, saves read/write, config, script flags,
  submap/worldmap, character, etc. Names/types 1:1 with Java so mods port.

### 4.3 Registries

- `Registry<T>` keyed by `RegistryId{modId, entryId}` (string-based, stable).
- Built-ins: character templates, stat types, stat mod types, elements, level-up
  actions, additions, items, goods, equipment types, spells, shops, encounters,
  DEFFs, post-battle actions, battle actions, battle entity types/stats,
  element icons, campaign types, engine state types, input actions, config
  entries, icons/atlas textures, script functions.
- Mods (Luau or C++) register entries at boot; `lod` game module registers all
  retail content. `RegistryDelegate`-style lazy registration preserved for
  configs.

### 4.4 ECS (EnTT) — where and how

EnTT is used for **scene/entity management**, not engine services:

- Registry per scene context (field scene, battle scene, world map, title):
  actors (player party members, monsters, NPCs), scene objects (sobjs),
  cameras, renderables (mesh + transform + material + queue pool slot),
  animation state (ANM/TMD anim cursors), effects (SEffe-driven), particles.
- Components (initial set): `Transform` (position/rotation/scale, float3),
  `Mesh` (mesh handle + tpage/clut/texture refs), `Lit` (light params),
  `Animated` (anim clip + frame), `Collider` (submap collision group),
  `BattleEntity` (stats, turn state, flags), `Camera` (mode + scripted path),
  `ScriptableObject` (script VM hook IDs), `RenderQueue` (pool + batch slot).
- EnTT views process sim ticks in fixed order (transform → animation →
  collision → camera → queue submission). EnTT's deferred destroy matches our
  deferred destruction.
- **EnTT stays out of the public plugin API** (requirement): plugins address
  entities via `lod::EntityHandle`s + typed capability methods through the
  C++23 interface (see native-api.md).
- Not forced into ECS: engine services (renderer, audio, assets, saves), the
  battle state machine (explicit state objects with EnTT as their entity store),
  UI screens (screen objects, not entities).

### 4.5 Scripting (story VM)

- Port of the original script VM: `OpType` set (YIELD, REWIND, WAIT, WAIT_CMP,
  ...), `ScriptState` (1,522 lines — the core), param types (game vars, inline,
  flags, registry IDs, storage, temp), tick-driven execution
  (`SCRIPTS.tick()` returns whether anything ran).
- Script *functions* (the `@ScriptDescription` mechanism) become registered
  callable ops: `ScriptFunctionRegistry` — the game registers the same set as
  Java; mods (Luau/native) can add functions (parity goal).
- The VM is data-driven over `SECT/DRGN0.BIN/xxxx/1xxx` scripts (patched at
  unpack time via the fate pipeline).

### 4.6 Save system

- `Tag` binary format ported byte-exact (V10); migration chain V8→V10.
- Save file = campaign + version + config collection + engine state (type +
  Tag payload via `EngineState::writeSaveData`) + DABA `Save60` (original save
  card slot) + mod `ListTag` data (Read/WriteSaveData events).
- Memory-card import (original `.mcr`/DABA) preserved.
- Crash-recovery save preserved (config-gated).

### 4.7 Input (implemented: platform foundation)

Two layers, deliberately separated so the state machine is testable without SDL
and without a window:

```
SDL event queue ──► Platform::PollFrame()          src/engine/src/platform.cpp
                      │  (the only place SDL input types appear)
                      │  • filter: axis/button events for pads we never opened
                      │  • translate raw axis (int16) → deadzone-shaped value
                      ▼
                  InputManager                    src/engine/src/input.cpp
                      • bindings, action phases, repeat cadence
                      • per-source axis state, Steam Input guard, focus gate
                      • frame snapshot queries (ActionHeld / ActionPressed / …)
```

- **One frame, one snapshot.** `PollFrame()` wraps the event drain in
  `InputManager::BeginFrame` / `EndFrame`, so a frame's input is a single
  coherent snapshot instead of 20 individually-timestamped events. The clock is
  the frame clock (`BeginFrame(nowNs)`), which makes repeat timing testable.
- **Controllers are opened for every `GAMEPAD_ADDED` device.** SDL only emits
  that event for devices it maps as a gamepad, so the native layer does no vendor
  matching of its own; a failed open is logged rather than swallowed.
- **Phantom input is filtered at the boundary.** Both layers ignore axis/button
  events that name a pad which was never opened (the test injects one naming a
  pad id that does not exist, which `SDL_PushEvent` makes easy to produce).
  Because the Steam Input guard keys off "gamepad input held", an input event
  from a device that does not exist would otherwise mute the keyboard.
- **Controller coverage is virtual-pad coverage.** The tests drive SDL's virtual
  joystick, which exercises the event pipeline but proves nothing about real
  hardware mappings — devices SDL does not map as a gamepad (Steam Deck
  built-in controls, various Switch Online adapters) are unverified here, and
  no retail controller has been tested. Tracked as an open question in
  `docs/architecture/roadmap.md` (Cross-Milestone Notes).
- **Reference-parity behaviour kept deliberately**, each pinned by a test rather
  than silently "fixed": unfocused frames *process* events but wipe all held
  state at `EndFrame` (no input is observable while unfocused); a degenerate
  deadzone region divides by zero and reads as silence or full deflection; the
  repeat cadence is 100 ms, spent as two 50 ms phase transitions. The reference
  `isActionRepeat` is the union of press-state and fired-repeat — the native
  layer exposes the two queries separately (`ActionPressed`, `ActionRepeat`) and
  a consumer that wants the union ORs them.
- **Deliberate deviations** (device-fault paths, not authored gameplay, each
  pinned by a test): held buttons are tracked as a *set of (pad, button) pairs*
  rather than an integer that can drift negative; unplugging a pad releases the
  actions only that pad was driving (the reference clears axis state only, which
  leaves a button held — and the guard armed — forever after a mid-press yank);
  a keyboard press has no trackable device source, so unplugging a pad cannot
  cancel it.
- **Headless**: `HasKeyboardFocus()` returns true for the dummy video driver.
  An unfocused-reporting host would disable all input in CI and locally on a
  machine with no window manager, and it would do so silently.

Config surface (see §5): `menu_deadzone_*`, `movement_deadzone_*`,
`steam_input_guard`, `receive_input_on_inactive_window`. A deadzone region
outside 0..1 or with `outer <= inner` is replaced by the default pair — repair,
not clamping into something the player did not ask for.

## 5. Configuration

- Typed config entries (bool/int/float/enum/string/color/campaign-name) in a
  registry; categories; storage location (global / campaign / per-save);
  presets; keybinding entries (per-input-action, device, activation, mods,
  deadzones).
- Global `config.dcnf` (Tag-serialized — keep the name for user continuity).
- Low-level `App` prefs (window size, fullscreen, monitor) in the same file.
- **Interim (application-foundation milestone):** only the live systems are
  configurable, so `lod::Config` reads/writes a `key value` text file
  (`configDir/config.txt`: `tick_hz`, `headless`, `window_width`,
  `window_height`, `window_title`, `resource_dir`) with per-key fallback to defaults and a
  Warning, plus range clamping. It is deliberately *not* named `config.dcnf`:
  when the Tag reader lands it must never be handed a text file under a Tag
  extension, and the registry of typed entries replaces this file rather than
  silently inheriting it.

## 6. Game ↔ Mod Parity Strategy

1. The native **`game/` module registers retail content exclusively through the
   mod API** (registries + events + i18n + configs + script functions), exactly
   as `LodMod` does in Java. This guarantees: (a) "total overhaul" mods can
   replace `lod`; (b) every retail feature is reachable by mods by construction.
2. Every in-game behavior with a mod hook in Java gets the same hook in native.
3. Where Java lacks a hook (addition input evaluators — see modding-analysis
   §2.4), native adds a *designed* seam (documented in luau-api.md) without
   changing retail behavior.
4. Content effects (item/effect lambdas in `LodItems`, battle actions) become
   either data + registered C++ effect functions (retail) or Luau-provided
   functions (mods) through one `EffectResolver` interface.

## 7. Logging & Diagnostics

- Structured logger (level, markers/tags like `INPUT`, `AUDIO_THREAD`, `SPU`,
  `SCRIPT`, `ACTIONS` — port the marker scheme), console + rotating file
  (`debug.log`), crash log with version/commit/timestamp + loaded mods (as in
  Java's crash path).
- Free-cam (port of RenderEngine free-cam) + debug keys (pause/frame-advance/
  wireframe/reload-shaders) preserved in-engine; deep debuggers move to Qt 6
  (qt-integration.md).
- GPU debug: SDL3 GPU debug flag on dev builds; backend validation
  (Vulkan layers, D3D12 debug layer, Xcode Metal capture) documented in
  testing.md.

## 8. Key Decisions & Tradeoffs

| Decision | Rationale |
|---|---|
| Keep single-threaded sim+render (like Java) | The original game is frame-ordered; multi-threaded sim would change timing semantics for no gameplay benefit. Render can use multi-threaded command encoding later if profiling shows it matters. |
| Port the software GS as an optional fidelity mode | Genuinely reusable PS1 tech; provides the most-faithful reference for parity testing of the modern path; small (≈1.6k LOC) and self-contained. |
| EnTT only for scenes/entities | Requirement: don't force everything into ECS; keeps battle state machine explicit and debuggable. |
| Content-as-mod for retail | Proven pattern (LodMod); makes overhaul mods and the "disable lod" workflow work; keeps engine clean. |
| Byte-exact Tag saves | Users keep saves across the port; V10 is the current reference format. |
| Sim/render decoupled but rate-locked to original vsyncMode | Preserves 30/60Hz timing + turbo while allowing high-refresh displays. |

## 9. Risks

1. Battle state machine port correctness (largest code, frame-exact timing) —
   mitigated by milestone decomposition + golden tests (testing.md).
2. Music synth fidelity — mitigated by A/B audio measurement against Java
   output.
3. SDL3 GPU API immaturity (3.2.0+ is young) — pin SDL version; fallback plan
   per feature (e.g. if any-write depth proves problematic on a backend,
   emulate linear depth without gl_FragDepth for that backend, documented).
4. External tooling deps for shaders (glslang/SPIRV-Cross/DXC) — see
   dependencies.md (approval needed).