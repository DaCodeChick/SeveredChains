# Implementation Roadmap

Refined from the provisional 12 milestones (task §20) using source research.
Each milestone: scope, prerequisites, deliverables, acceptance criteria,
verification steps. Native-API and Luau *architecture* is fixed from the
start (design docs delivered in Phase 0); their *implementation* lands at
M10 as proposed, but the registry/event/content-registration foundations they
build on exist from M5 (retail content registers through them from day one —
the LodMod pattern, engine-design.md §6).

**Dependency approval gates** (dependencies.md §2):
- G1: glslang + SPIRV-Cross + DXC (build-time shader tools) — **before M4**.
- G2: SDL3_image (PNG decode) — **before M2** (UI PNGs).
- G3: XA cache format decision (PCM/WAV vs libopus) — **before M6**.
- G4: libcurl (launcher auto-update) — **before M11** (launcher feature).
- G5: ffmpeg/libav (FMVs) — **before M11** (likely deferred to post-M12).
*(If any gate is denied, the affected scope is cut or substituted as
documented in dependencies.md; no milestone is blocked on a denied option
that has a documented fallback.)*

---

## Milestone 1 — Application Foundation

**Scope:** CMake+Ninja project skeleton (build-system.md); `sc_noexcept`
enforcement + `throw`-probe; SDL3 init (video+gamepad), window creation
(port of `SdlWindow` semantics: min size 320×240, resize, fullscreen <!-- naming-allow: Java class name cited verbatim -->
per-monitor incl. the Windows borderless +1px hack, monitor config),
input event plumbing (keyboard/mouse/gamepad → `InputKey/Action` model with
bindings — port `SdlPlatformManager`+`InputBindings`), `Action`-equivalent <!-- naming-allow: Java class name cited verbatim -->
scheduler (input 60Hz + render pacing), config load/save (Tag-based
`config.dcnf` reader — Tag serializer can be a minimal subset here, full in
M6), diagnostics (logging with markers, crash handler + crash log format,
version/commit/timestamp injection), clean shutdown order, `Version`
release-channel gating. No GPU yet (or a software-clear stub if needed).

**Prerequisites:** none (repo is empty).
**Deliverables:** buildable skeleton on Win/Linux/macOS; host executable that
opens a window, shows a debug overlay (FPS, backend n/a, input states),
handles resize/fullscreen, quits cleanly with correct log output.
**Acceptance:**
- Builds clean on the 3-platform matrix with the exception policy **observed** in
  **both** modes. What counts as observed is defined per toolchain, because the
  toolchains can prove different things (build-system.md §2, implemented in
  `cmake/ScToolchain.cmake`):
  - **clang / GCC** (macOS, Linux): `SC_ENABLE_EXCEPTIONS=OFF` must make the
    `throw` probe **fail to compile**. A probe that still compiles there means the
    flag was dropped, and that fails the configure step.
  - **MSVC** (Windows): `/EHs-c-` describes how to clean up *after* a throw, not
    whether one is allowed, so `cl.exe` accepts `throw` either way — demanding
    rejection would fail a correctly working compiler on every Windows leg.
    Acceptance is instead the **exception-model macro flip** (`_CPPUNWIND` /
    `__cpp_exceptions` undefined with the flag on, defined with it off) plus a clean
    MinGW / clang-cl cross-compile recorded as *compile-only* evidence. Where even
    the macro cannot decide, the configure log names the outcome `unverifiable`
    rather than passing it silently.
  - `psx` and the plugin SDK stay exception-free regardless (AGENTS.md §4).
- Window/input parity checklist vs Java (keyboard scancodes, gamepad
  buttons/axes incl. deadzone model, text input, focus behavior,
  background-event hint, controller DB loading).
- Config round-trip (global file create/load/save, presets skeleton).
- Crash path: injected failure → crash log with version/commit/timestamp +
  loaded-mods section (empty list) — byte-compatible layout with Java's
  crash log fields.
**Verification:** executed on macOS/arm64 — the priority-one platform and the only
machine available here — plus unit tests (scheduler timing, config, input mapping
tables). Windows and Linux are **compile-only** evidence (cross-compile) and CI lane
definitions until a second machine or a real CI run exists; no Windows/Linux clause
may be reported as verified from the macOS host, and Windows/MSVC plus D3D12
verification is deferred to after the macOS MVP.

## Milestone 2 — SDL3 GPU Foundation

**Scope:** `SDL_CreateGPUDevice` (all three shader formats once G1 lands;
interim: SPIR-V only + Vulkan on Linux, Metal via MSL hand-compilation of
*one* test shader if G1 is still pending), swapchain, render pass, one
graphics pipeline, textured triangle + quad (rendering-sdl3-gpu.md §2),
upload path (transfer buffer ring), texture creation (R8G8B8A8 + R32UINT
support query), depth (D32/D24 query), backend selection logging, GPU debug
flag on dev builds, resource RAII wrappers + deferred-deletion scaffold,
fence-based correctness (submit + fence wait).

**Prerequisites:** M1; **G2** (PNG for the test quad); shader tools or a
single hand-compiled test shader per backend. **Also starts the `psx`
library** (psx-library.md): the independent, prefix-free PSX library
target (root subproject) lands now with its
first modules (`psx::bpe`, `psx::xa`, `psx::tag`, `psx::gte`) so the
golden suite (testing.md §2.11) is green from the library's first commit —
it is dependency-free and needs no GPU/SDL, so it proceeds in parallel
with the graphics work.
**Deliverables:** `scengine` GPU layer; test scene rendering on the
default backend; pipeline-creation smoke test infrastructure; `psx`
C17 target + initial modules + standalone psx suite (`sc_tests_psx`, pure-C consumer,
ABI symbol audit), with header-only C++23 wrappers over it (psx-c-api.md).
**Acceptance:**
- Textured quad + triangle render correctly on at least Vulkan (Linux),
  Metal (macOS), D3D12 (Windows) — with SPIR-V-only artifacts acceptable
  pre-G1 only for the Vulkan path; Metal/D3D12 need their artifacts (G1).
- Resize (render-target recreation) without crash/tear.
- 60fps steady with a simple scene (frame-time histogram logged).
- Resource-leak test: 10k create/destroy of textures/buffers, fence-verified.
**Verification:** headless-ish CI where possible + manual 3-OS check;
golden image of the test quad per backend.

## Milestone 3 — Cross-Platform Graphics

**Scope:** complete backend verification (rendering-sdl3-gpu.md §9.1):
device creation matrix (all available backends per OS), **full
pipeline-variant creation matrix** (every pipeline of rendering doc §3 on
every backend), format support matrix (R32UINT sampling, D24/D32),
swapchain depth APIs, per-backend quirks documented + handled (Metal
`mip_lod_bias` no-op, D3D12 RBT2, Vulkan swapchain), backend/device
selection plumbing into config (list devices, persist choice), failure
diagnostics (no backend → clear error, exit code 4).

**Prerequisites:** M2; G1 (all three artifact types).
**Deliverables:** backend verification test suite (CI-gated on Linux+macOS;
D3D12 on the Windows runner), device-selection config, docs page
(`docs/guides/backends.md` for users: what works where).
**Acceptance:**
- Pipeline-creation matrix passes on all 3 backends.
- User can force a backend/device; wrong device → clean error.
- No backend → documented diagnostic (per-platform text), no crash.
**Verification:** CI matrix + manual multi-GPU laptop test (integrated +
discrete switching, like Java's GPU_PREFERENCE scenario).

## Milestone 4 — Shader Pipeline

**Scope:** shader build integration (ScShader.cmake): GLSL sources (ported
from `gfx/shaders`, reorganized per shader-migration.md §5 binding
conventions) → glslang → SPIR-V → (SPIRV-Cross → MSL) → (SPIRV-Cross → HLSL
→ DXC → DXIL); content-hash caching + install layout; runtime artifact
loader (per-backend selection, hash verification, `--shaders-dir` override,
hot-reload in dev mode); all 6 shader families implemented (simple/copy/
post/standard/tmd/battle_tmd) with the **geometry-shader replacement**
(primitiveParams path, shader-migration.md §3 option B, incl. the CPU
per-primitive cull/avg-depth pass); diagnostics (compile errors surfaced
with file:line; runtime logs artifact hash+backend).

**Prerequisites:** M2; G1.
**Deliverables:** `gfx/shaders/` native sources (GLSL 450, explicit
layout), build pipeline, loader, tmd pipeline without GS (functionally
complete vs the Java GS behavior).
**Acceptance:**
- All shaders compile zero-error on all 3 targets; build is cache-stable
  (no-op rebuild on unchanged sources).
- `standard`/`tmd` render a TMD fixture mesh with correct: PS1 lighting
  (NCCS values golden-tested), CLUT decode (4/8/15bpp golden pixels),
  tpage/clut remap, CLUT animation remap, PS1 projection (H division) vs
  modern projection, screen-offset ordering.
- GS replacement: near-plane cull test (a fixture primitive half-behind
  H/2 disappears, parity with the Java GS output), avg-depth test
  (usePs1Depth fixture renders with per-primitive constant depth — compare
  against Java capture).
- Scissor-in-shader path active (fixture: scissored quad, no leak).
**Verification:** golden pixel comparisons (render a TMD fixture scene with
all blend modes × all projection modes vs Java reference captures);
unit tests for the CPU primitive pass (cull decisions, avg values).

## Milestone 5 — Engine Services

**Scope:** resource management (asset-free: mesh/texture/pipeline caches,
arena allocators, deferred deletion hardened), **timing system** (fixed
sim step 30/60Hz from vsyncMode + speed multiplier — engine-design §3;
presentation double-frame for 30Hz), full input (port of the action model
incl. repeat/hold/axis state, keybinding config entries, rumble + lerp),
**audio foundations** (SDL3 audio device + device enumeration/hot-swap;
audio thread; SFX engine driving the **`psx::spu` voice processor** — 24
voices, edge key control, FM/noise/reverb, cubic resample, 60TPS; volume
model master×(music|sfx); XA/PCM streaming player per G3 decision decoding
via **`psx::xa`**; **music synth port** (`AudioThread`+`sequencer`
— 120Hz scheduling over `psx::spu` voices, **`psx::sssq`** SSSQ/SSHD
parsing, interpolation/pitch/granularity
config)), **EnTT integration** (registry wrappers, component conventions,
scene contexts, view processing order — engine-design §4.4),
**registry + event bus** (all core registries from the Java catalog —
semantics verified in mod-loader.md; synchronous bus with cancel +
per-listener isolation; config entries registry-driven; i18n key/override
model, 4-level specificity), **story-script VM port**
(`OpType`/`ScriptState`/params — execution; function registry
`ScriptFunctionRegistry` with the retail function set; fate patching is
M6 — it runs at unpack time in the reference, verified).

**Prerequisites:** M4 (VM + content need rendering to be observable; audio
is independent), G3.
**Deliverables:** timing-verified sim loop (no rendering content yet — a
tick counter + audio test signals), SFX/music playing real SShd/SSSQ/
sequence assets (loaded via a minimal synchronous loader ahead of the M6
asset manager), EnTT scene test, registry/event integration test, script
VM executing a retail script (log-driven, no visual yet).
**Acceptance:**
- Timing: sim tick rate measurement at vsyncMode 1/2 × speed 1/2/4/16 within
  ±2% (testing.md §2.15); timestamp accumulation parity.
- Audio: SFX key-on latency <2 audio ticks; music output PCM golden diff vs
  Java reference (fixed seed/sequence) within documented tolerance;
  device hot-swap works; no audio-thread stalls (TSAN clean).
- EnTT: 10k entity scene tick within budget (measured); deferred destroy
  correctness.
- Registry/event: full core registry catalog boots; a test "mod" (in-proc)
  registers entries + subscribes; event ordering deterministic.
- Script VM: N retail scripts execute with identical op-trace vs Java
  (trace-diff golden test on a fixed set of scripts, opt-in data).
**Verification:** golden audio/VM tests; TSAN; measurement reports committed
(measure, don't assume — task §1).

## Milestone 6 — Asset Pipeline

> **Landed early** (the user's "Milestone 3" brief, resource management and PSX
> data integration): resource **discovery, ownership and validation** over an
> extracted tree (`lod::res`, architecture/resource-management.md) and the
> `psx::bpe`, `psx::archive` (MRG/DEFF), `psx::tim` (parse/validate only)
> decoders, plus a CLI inspect/validate mode. Not landed from this milestone:
> ISO reading, unpacking/extraction, SMAP/OVL splitting, per-file patchers, mod
> overrides, async loading, TMD/CTMD/DABA decoders.
> Evidence: reports/milestone-3-resource-management.md. Those decoders were then
> migrated to a **C17 core + header-only C++23 wrappers** (`psx::psx` / `psx::cpp`,
> psx-c-api.md); evidence in reports/psx-c17-migration.md.

**Scope:** full unpacker port (asset-pipeline.md §4): ISO9660 reader
(4-disk merge, region IDs) via **`psx::cdrom`**, BPE decompressor
(vectorized) via **`psx::bpe`**, MRG/DEFF archives via **`psx::archive`**,
SMAP overlay extraction (fixed offsets), engine overlay split,
the per-file patcher set (each = a documented data fix; ported 1:1),
replacement chain (mod overrides → base — runtime, replacing
unpack-time replacements), `files/`-equivalent cache (versioned by format
version + disc hashes; rebuildable; low-memory/file-backed mode parity),
async loader (task handles, error results), **decoders**: TMD/CTMD→mesh IR
(**`psx::tmd`**), TIM/DABA→tex15/tex24 upload layout (absolute VRAM coords,
stitching; **`psx::tim`**), ANM (**`psx::anm`**), CContainer (+CLUT
animations) + MCQ (game/ — LoD-only containers, emit psx IR), SShd/SSSq
(**`psx::sssq`**, for M5 audio), XA→cache format (per G3, `psx::xa`), Tag
serializer (**`psx::tag`**; saves use it in M8), memcard (DABA Save60)
reader, save-card texture extraction, `DrgnFiles` index caching. **Fate
script-patching pipeline at unpack time** (verified reference behavior —
script-recompiler.md §5): `psx::script` disassembler/assembler + canonical
text codec, in-repo unified-diff component, the native **fate compiler**
(hand-rolled per the Q-SCR-1 recommendation — approval pending), bundled
meta CSVs, `scripts.csv` patch lists, backup/cache/CRC-of-includes
invalidation + restore-on-deletion (parity with `ScriptPatcher`). The
`fate` *CLI tool* is M12; the pipeline itself is M6.

**Prerequisites:** M5 (loader feeds sim), G3.
**Deliverables:** psx decoder suite complete (psx-library.md §2) +
`assets/` manager; unpack CLI path (first-run unpacker with status
 callback — port of the `Unpacker` UX: wait-for-discs, per-file transform
progress, version file); **fate patch pipeline** (applied to the shipped
patch corpus → patched tree byte-identical to the Java unpacker's output);
asset error taxonomy live (specific Status per malformed class).
**Acceptance:**
- Unpack a real 4-disc set (opt-in data) → `files/` tree **byte-identical**
  to the Java unpacker output (tree diff; version file matches) — **incl.
  the fate-patched script tree** (all `scripts.csv` entries applied).
- Decoder golden tests (`psx` standalone suite — synthetic + real fixtures,
  testing.md §2.11) all pass; every malformed-class → specific Status.
- Load-time: all title-screen assets load (fonts, UI png, title TMDs,
  shadow model, save card textures) with async completion tracking.
- Unpacker performance: ≤ Java unpack time on the same machine (measured;
  the vectorized BPE should beat the byte-at-a-time Java).
- OOM/low-memory mode: file-backed unpack completes on a constrained
  allocation ceiling (simulated).
**Verification:** tree-diff harness; golden decoder suite; measurement
report; manual first-run UX pass on 3 OSes.

## Milestone 7 — Scene Rendering

**Scope:** render the original scenes end-to-end: render target ping-pong +
batches + 4-pass ordering + sorting (port `RenderEngine` frame flow
exactly), model queue system (QueuedModel equivalents: standard/tmd/
battle_tmd, per-model transforms2/lighting 128-blocks upload, CLUT
animation UBO, per-draw uniforms), cameras (camera2d/3d, **`psx::gte`**
derived view matrices, `worldToScreenMatrix`), projection (PS1 H-division +
modern +
ortho, screen offset order, widescreen ortho offset, 4:3 letterbox,
resolution-snap-to-240), text rendering (fonts, textboxes, textZ), UI
quads (MCQ menus, line/quad/poly builders), TMD/CTMD model rendering
(incl. battle uniform lighting + ctmdFlags), translucent two-pass quirk,
in-shader scissor, post/`screen` shader (full CRT chain + turn_order_bounds
hook — the mod uniform is exposed now so M10+ mods can use it), **software
GS fidelity mode** (the `Gpu`+`Graphics` software rasterizer runs from
**`psx::gpu`** — the port lands in M7 as part of the `psx` library; legacy path +
`worldToScreenMatrix` usage — the parity
reference oracle), free-cam + debug keys (pause/frame-advance/wireframe/
shader-reload; render-mode TAB cycling: OpenGL/legacy/VRAM), Discord
presence (optional, G-independent — via a small HTTP-free stub or
deferred; decision: **defer Discord to M11**, it's cosmetic).

**Prerequisites:** M6 (real assets), M4 (pipelines), M5 (sim tick drives
the queue).
**Deliverables:** the title screen renders (menu navigation works with
keyboard/gamepad but does *nothing* yet — states land in M8); field scenes
load and render with navigation-free camera; a battle arena renders with
models + addition overlay geometry (no combat logic yet).
**Acceptance:**
- Golden screenshot suite (testing.md §2.15): title, field (HQ + legacy +
  VRAM modes), battle arena, translucent-heavy scene, CRT on/off, 4:3 and
  widescreen — native-vs-Java diff within tolerance, visual review of the
  rest.
- Render-mode parity: legacy/VRAM (software GS) output is byte-identical
  to Java's (same assets/seed) — this is the strongest fidelity proof and
  the oracle for the modern path.
- Frame pacing: 30Hz scenes present at display rate with the double-frame
  behavior (capture a 2-second screen recording, count unique frames).
- Performance report: frame time vs Java on the same machine per scene
  class (title/field/battle) — measure, no assumptions.
**Verification:** golden diff harness; manual playtest of the rendered
scenes (no gameplay yet).

## Milestone 8 — Gameplay Reconstruction

**Scope:** engine state machine (Title/Submap/Battle/WMap/Menus — state
objects with tick/postScriptTick/overlayTick/advancesTime/writeSaveData,
state-type registry, deferred transition `loadQueuedOverlay` parity,
`lastSavableState` tracking, Discord-stub), **Title state** (save cards,
options menu — full registry-driven options parity incl. keybinding
editor, new game flow, update button stub until M11), **Submap state**
(`SMap` port: navigation/collision via `CollisionGeometry`, sobjs +
`SubmapObject210` + attached effects, chapter title cards, camera, smoke/
snow/dust effects, cut flags, battle-transition trigger
(`tickAndRenderTransitionIntoBattle`)), **World map state** (`WMap` port:
NEWROOT.RDT data, prompts, chapter menus), **Text/dialogue** (textbox
state machine, `Text` port, I18n live), **Menus/inventory** (MCQ menu
system, `Menus` port: inventory/equipment/items screens, shops,
save/load screens), **Saves** (Tag V10 full, migrators, per-save configs,
memcard import, crash recovery save, `SavedGame`/`Campaign` model,
Write/ReadSaveData events), story scripts live (VM drives events: flags,
cutscene triggers, object on/off, engine-state transitions), **Mogul
Dabas minigame** (scdabas port: handheld console — timers/RTC/interrupts/
power/DAC/32×32 1bpp framebuffer, host-driven fixed-step ticks per
dig-dabas.md Q-DAB-2; game-logic port with ROM data tables; the host
adapter (render hijack, 5-button input mapping, mono8 audio source,
`dabas_save_data` config blob — byte-exact 0x260 layout), `DabasScreen`
in the in-game menu, `DabasRewardsEvent` rewards flow; crash→error-result
isolation parity), encounters
triggered into the battle *stub* (battle logic = M9).

**Prerequisites:** M7 (everything renders), M5 (VM, timing, registries).
**Deliverables:** playable field game: title → new game → submap
navigation through story → dialogue → inventory/shops → **Dabas minigame
(usable + savable)** → save/load (V10
byte-compat) → world map → encounter triggers battle (stubbed battle).
**Acceptance:**
- Play the opening (title → first submap → first dialogue → save → reload →
  world map) end-to-end with no crash, save byte-compatible with a Java
  save of the same point (diff).
- Dabas: a scripted dig session (fixed input tape) reproduces the Java
  session's rewards (testing.md §2.13); save blob byte-identical to a
  Java-produced blob; a VM fault reports the error and the game continues.
- Script-gated content works (a scripted object/cutscene sequence plays;
  flag reads via the script VM verified).
- State transitions: all EngineStateChangeEvents fire with correct
  previous/current (event trace golden test).
- Input parity: menu navigation with gamepad + keyboard + mouse (menu
  mouse parity with Java).
- Crash recovery: kill -9 mid-submap (after save-point state) → recovery
  save exists and loads.
**Verification:** scripted playthrough (input recording replay) + manual
playtest; save golden diff; event-trace goldens.

## Milestone 9 — Combat

**Scope:** the battle state (`Battle` 9.4k lines + `SEffe` 4.6k +
`BattleCamera` 2.9k + `BattleHud` 2.2k + `Monsters` + `SBtld` +
effects/particles/formula/encounters/environment/postbattleactions/bent/
deff/ui sub-packages — 40k LOC, the dominant cost). Decomposition:
- **9a — Battle core:** battle load (`SBtld`: preload, transitions
  `tickAndRenderTransitionIntoBattle`), entities (`BattleEntity27c` +
  player/monster), turn order, battle camera scripts, menu system
  (`BattleHud` + `BattleMenuStruct58`: attack/guard/items/spells/options,
  target arrows), base hit resolution, `formula` package port (golden),
  monster stats/`Monsters` table, DEFF/CMB behavior scripts
  (`deff`), post-battle (rewards, `postbattleactions`, battle report
  overlays), battle music/stage, `BattleUiParts` (preloaded UI TMDs).
- **9b — Additions & effects:** the addition pipeline
  (`AdditionOverlaysEffect44` timing-square port with the **input
  evaluator seam** — engine-design §6.3 / luau-api §4: retail evaluator =
  original behavior; alternative evaluators registerable), addition
  registry + per-level multipliers + hit tables (retail additions for all
  9 characters incl. Dragooons), ANM-driven addition animations (async
  load parity), auto modes (tutorial/auto-addition config),
  **effects/particles** (SEffe engine, LmbAnimationEffect,
  ElectricityEffect, Starburst, ScreenCapture/ScreenDistortion/LensFlare/
  GradientRays/SpriteWithTrail/WeaponTrail, particle managers — the
  screen-capture effect uses the M7 ping-pong `getLastFrame` parity).
- **9c — Dragon transformations & specials:** Dragoon addition flow
  (transformation animations, `INPUT_ACTION_BTTL_TRANSFORM`), spell
  casting (spell registry, element sets), counter/guard/intercept logic,
  battle camera shakes/zooms, hit-stop frames.

**Prerequisites:** M8 (transitions, saves, menus), M7 (effects rendering,
screen capture), M5 (formulas/timing goldens).
**Deliverables:** fully playable battles: turn order, menus, attacks,
items/spells, guard/counter, all retail additions (timing square),
transformations, monster AI (DEFF), effects/particles, post-battle,
win/lose flows, battle saves.
**Acceptance:**
- Battle state snapshots: recorded input script through N real battles →
  per-sim-frame snapshot (positions, HP, turn order, effect timeline,
  camera) equals Java reference snapshots (opt-in data lane; start with
  3 battles: a basic fight, a fight using an addition, a Dragon fight).
- Addition timing: property suite (testing.md §2.6) + golden overlays
  (screenshot the timing square at fixed frames vs Java).
- Formula goldens: damage/SP/crit/element results for a matrix of
  character/level/target combos (generated from Java, committed).
- Effect timeline: SEffe-driven sequence frame counts match Java (trace).
- No frame-rate dependence: battles at 144Hz display == 60Hz display
  (sim is display-independent; measured).
**Verification:** snapshot-diff harness; manual battle playtest (all 9
characters × a representative battle each); performance report (battle
frame time vs Java).

## Milestone 10 — Native & Luau Extensions

**Scope:** **Mod package format + loader** (mod.toml manifest, deps/semver,
load order, asset override chain live, lang overrides, enabled-set),
**Luau host** (per-mod VMs, sandbox/limits per luau-api §1, protected-call
boundary, bytecode precompile cache, all namespaces implemented:
Engine/Assets/Input/Render/Combat/Characters/Items/Saves/Audio/Script/Mod —
full parity surface luau-api §3), **native plugin host** (the **C++23
plugin API** live per native-api.md: `sc-sdk` (frozen interface headers +
toolchain file), entry-symbol discovery (`LoDPluginCreateV<N>`), toolchain-
parity check, capability interfaces `lod::GameplayAPI/EventsAPI/InputAPI/
RenderAPI/AudioAPI/AssetsAPI/SaveAPI/ScriptAPI/CombatAPI/LogAPI`,
dlopen/LoadLibrary, version negotiation, lifecycle state machine, failure
UX parity, handle-leak
checks, sample plugin built with the SDK toolchain file), **the "lod"
content module** is now fully
reachable by mods (content registered only through the APIs — audit that
no retail path bypasses the registries), **script functions** registered
by mods (Luau `Script.registerFunction` + native `lod::ScriptAPI`), combat
evaluator seam exercised (a Luau button-sequence addition works in a test
battle — the Playable-Lloyd scenario), mod error isolation (compile/runtime
errors → mod disabled, game continues, reported), save `modData` slots
(Luau + native, byte-compat with Java layout for same ids).

**Prerequisites:** M9 (combat seam), M6 (asset overrides), M5 (registries/
events/VM foundations).
**Deliverables:** mod system end-to-end; sample Luau mod (a new item + a
config entry + a render overlay + an event subscription); sample native
plugin (spawns an entity on an event + registers a script function + reads
saves); docs (`docs/modding/`: authoring guide, API reference generated
from the tables, migration guide Java→native for mod authors).
**Acceptance:**
- A third mod (independent from the samples) builds and works against the
  API without engine changes (the "weird mod" test: e.g. the turn-order
  mod port — `legend.turnorder` as a Luau mod — must work, incl. the
  `turn_order_bounds` post uniform).
- ABI: plugin compiled against API v1 works on engine v1.x (CI).
- Isolation: a mod with a runtime error / a crashing (segfault) native
  plugin — the former isolates cleanly; the latter dies the process with a
  diagnostic (documented, tested as far as possible).
- Dependency: missing/wrong-version/cycle cases all report correctly
  (golden tests).
- Playable Lloyd scenario: button-sequence addition registered by a Luau
  mod, played in a battle, correct input evaluation + overlay + result.
**Verification:** the mod test suite (unit + integration + ABI + fuzz per
testing.md §2.8–2.10); manual mod authoring pass by a second engineer
(docs usability test).

## Milestone 11 — Feature Parity (Severed Chains enhancements)

**Scope:** audit every Java enhancement against the native build and close
gaps (the reference "Current Progress" list: bug fixes, features, options):
widescreen modes (submap widescreen + legacy forced-4:3), resolution
scaling, all config entries (full `CoreMod`/`LodConfig`/retail config
catalog — the ~60+ entries: addition overlay modes/sizes/RGB, timing
window, CRT full parameter set, combat stage, turbo, auto-addition,
languages, fonts (default/smooth), music interpolation/pitch/granularity,
deadzones, disable-mouse, receive-input-inactive, low-memory unpacker,
GPU preference, monitor select, controller config, presets), save cards
(full `savepoint`/save-card flow + save-card texture flow), **memcard
import UX** (original memory-card saves playable), crash recovery polish,
auto-updater (G4: staged binary update with the `updater.jar.new`-parity
semantics — saves/mods/ISOs/config preserved), update-available button on
title, Discord rich presence (chapter/character states), credits screen
(end credits, `CreditsScreen` port), FMVs — **only if G5 approved**
(otherwise documented gap: cutscenes without video clips), title-screen
icons / about screen (credits parity incl. `credits.txt` content),
`gamecontrollerdb.txt` updates flow.

**Prerequisites:** M10 (mod/config system for the option entries), G4.
**Deliverables:** feature-parity checklist (public doc `docs/parity.md`:
every Java feature → native status: done/partial/gap + evidence), the
missing features implemented, launcher (Qt) v1 (G4 features: disk
detection, GPU device select, audio device, mods list, launch, update).
**Acceptance:** parity checklist ≥95% "done" with the remainder explicitly
accepted gaps (user-acknowledged); every config entry works (test per
entry: set → persist → effect visible/measured); update flow tested
(staged update preserves saves/mods/isos/config); credits + about render.
**Verification:** the parity checklist is the acceptance artifact; manual
options-menu full pass; update-flow script test.

## Milestone 12 — Advanced Modding

**Scope:** **fate tooling** (the `fate` CLI — the `Shell` equivalent — on
top of the M6 pipeline: `psx::script` disassembler/assembler + the native
fate compiler + the unified-diff component; commands
versions/decompile/compile/genpatch/applypatch/undopatch/strip; format
parity with Java's fate output: round-trip identity tests on real
scripts; meta CSV-driven patches; a Luau mod
shipping fate patches works), **custom characters** end-to-end (a full
new character: template + stats + additions + spells + element + portraits
+ models + animations + battle behavior, all via mod APIs — the flagship
"custom character" requirement), **alternate combat mechanics** showcase
(button-sequence additions, custom resolvers, custom battle actions,
custom DEFFs — documented patterns + one shipped example mod), **asset
replacement** polish (runtime override chain documented + tooling in the
inspector), **expanded tooling**: Qt debugger full (all 6 JavaFX
debugger panels ported: script live debugger, gamestate viewer/editor,
combat debugger, smap model debugger, registry selector + in-engine
DebugServer), Qt asset inspector (decode/preview/export), Qt save
inspector, Qt mod manager (dependency graph, validation reports, install
from zip, per-mod license view), Qt settings (all config + keybinds +
presets + devices), **total-overhaul path** proven (a mod that disables
`lod` and registers an alternate campaign/engine-state set boots and
plays — the `LodMod`-disabling requirement from its own Javadoc),
out-of-process plugin isolation (evaluated — ship if the test harness
proves it viable, else documented future work).

**Prerequisites:** M11.
**Deliverables:** the modding ecosystem complete: `docs/modding/` full
(authoring, API reference, fate guide, Java-mod migration guide with the
backlog), the example mod suite (custom character, face-button additions,
turn-order port, asset-swap sample, fate-patch sample), the Qt tool suite
(launcher/settings/modmanager/debugger/inspector/save-inspector), the
`fate` CLI.
**Acceptance:**
- A brand-new character (community-style, all content from a mod package)
  is playable: recruitment, level-up, additions, transformation, battle
  AI interplay — with zero engine code changes.
- fate round-trip: disassemble→assemble on 100 retail scripts == identity
  (byte-exact or documented-canonicalized); a fate patch changes a story
  beat (golden test).
- Qt debugger: live-inspect a running game's battle state + step a story
  script (parity with the JavaFX debuggers' features).
- Total-overhaul mod boots and is playable (the "disable lod" proof).
- The example mod suite passes the M10 mod test suite.
**Verification:** the example-mod CI (build + functional tests); manual
mod-authoring marathon (a third party builds a custom character from the
docs alone — the real acceptance test of the docs + APIs).

---

## Cross-Milestone Notes

1. **Native-API & Luau from the start (task §20):** the *designs* are
   finalized (Phase 0 docs); the *registry/event/entry data model* they
   expose is built M5; the **C++23 plugin API** (native-api.md) is stable
   from M10 but its data types (value structs, `RegistryId` strings, event
   ids) are used internally
   from M5 — no rework expected, and any change before M10 is free.
2. **Golden-value generation** (from the Java reference) is a recurring
   Phase 1 task: run the Java reference (user's discs, headless where
   possible) with fixed seeds to generate: gameplay snapshots, audio PCM,
   script traces, screenshot baselines. This is the verification backbone
   and needs the user's cooperation once (data + machine access).
3. **Discord** deferred to M11 (cosmetic; JNA-based in Java).
4. **FMVs** gated on G5 (likely a post-M12 gap unless approved).
5. Milestones 1–4 are pure engine; 5–9 are the game; 10–12 are the
   modding/ecosystem layer. A "vertical slice" (title + one submap + one
   basic battle, unpolished) is achievable at the end of M9a — an optional
   internal checkpoint for early feedback.
6. **Open questions (as of the input work).** (a) *Controller coverage is
   virtual-pad coverage*: the input tests drive SDL's virtual joystick, and no
   retail controller has been tested. Whether SDL maps the Steam Deck's built-in
   controls and the Switch Online adapters correctly on each platform is
   unverified, and it cannot be verified from a single macOS host — it needs
   hardware or a contributor with it. (b) *M2/M3 acceptance names three GPU
   backends on three OSes*; only the Apple-Silicon/Metal lane can be executed
   locally, so the Vulkan/Linux and D3D12/Windows lanes exist as CI definitions
   and cross-compile checks, not as verified rendering. Neither of these is
   evidence of completion; both are stated here so "green on macOS" is never
   read as "green everywhere".