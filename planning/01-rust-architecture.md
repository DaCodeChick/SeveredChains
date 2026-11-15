# P0-01 — Rust Architecture (Proposed)

Status: **Proposed (P0, 2026-11-14; approved starting point `4a2acb8`).** This is the first implementation-phase document. It is a *design proposal grounded in the M1–M10 research corpus*, not an established fact: every research-grounded statement carries a `research/` citation; every forward-looking choice is marked **Proposed** and awaits explicit user approval. Decision items are split out to `planning/02-technical-decisions.md` (TD-*) and `planning/03-compatibility-decisions.md` (CD-*); the milestone sequence is `planning/04-implementation-roadmap.md`; the test strategy is `planning/05-verification-strategy.md`; row-level coverage of the 135-row compatibility checklist (the M10 "109" figure was a P0-corrected transcription error — cross/02 §12) is `planning/06-traceability-matrix.md`.

Evidence base: `research/cross/01-architecture-review.md` (init/shutdown, services, ownership, concurrency, flows), `research/cross/02-compatibility-checklist.md` (the consolidated constraint set — 135 rows + 10 decision items + the non-constraint list), `research/cross/03-implementation-readiness-notes.md` (A1–A13 architectural limitations, L1–L12 Java-specific limitations, decision points), `research/04-unresolved-questions.md` (M10 Re-triage), and the per-subsystem milestone documents they cite. Provenance: M1–M9-06 findings were verified against the primary reference at `0c08c679b`; M9-07 and M10 spot-checks against `71ce130364ed00431d420891370d5314a4e3d4c4` (cross/03 §2.2). P0 additionally verified the battle-RNG identity against `71ce13036` (`Battle.java:505`, `legend/core/Random.java`) — the "Mersenne Twister" label in M10 documents is the reference's own script-facing description; the class is a `java.util.Random` subclass (48-bit LCG). Corrected in place across the corpus with notes; see `planning/README.md` audit record.

## 1. Design axioms (what the research obliges, what it frees)

### 1.1 Obligations — the architectural limitations A1–A13 (cross/03 §3)

| # | Obligation (synthesis) | Design consequence in this document |
|---|---|---|
| A1 | Global mutable state is the shared surface (`GameState52c` @ 800babc8 - campaign game state + per-segment globals, cross/01 §4.2); ownership is conventional single-main-thread-writer | The game state is a plain owned struct with a single owning thread; the single-writer convention is *enforced structurally* (Rust ownership), not conventionally — the observable behavior is identical (cross/03 A1) |
| A2 | The segment-class monolith is the original's memory map; addresses/offsets are semantically load-bearing (scripts and saves address this state) | The state layout is a documented, asserted layout (offsets in layout docs + a compile-time size/offset assertion set); identifiers are semantic per the AGENTS.md naming policy; the reverse-engineering map (original identifier → Rust field) lives in one research-traceability module per state |
| A3 | Single-core frame-locked simulation, real-time audio; RNG streams are call-order-dependent | One owner thread for the frame loop; audio on independent real-time clocks communicating only via queues and control words (cross/01 §5); no parallelization of the simulation |
| A4 | The Fate VM is a core system (108 slots, 0–8 reserved, 1024-entry native table with per-state reinstall, 592-slot catalog, 63-slot dual int/float storage, fork/consume/deallocate) | The VM is a first-class SDK component (TD-07/TD-13); the VM contract (CL-SCR-05..09) is on the critical path |
| A5 | The modding surface is observable (saves store registry entry ids; 1238 retail content entries are mod-registered; the 113-event-class contract and one-shot Register windows are user-visible) | Registries/events are SDK framework with one-shot windows; all retail content is registered by first-party mods at boot, never hardcoded in engine code (CL-MOD-10) |
| A6 | The unpack-time transform pipeline defines the observable asset contract (the game reads the unpacked `files/` tree) | `psxrt-import` owns the pipeline and must produce equivalent unpacked bytes; patching machinery may differ, results must not (CL-AST-03, CL-MOD-08) |
| A7 | The save compatibility floor: all 11 magics loadable unmodified; V10 the only writer; the RETAIL 0x6E4 layout with the struct at file offset 0x1FC | Version readers are obligations in `severedchains-game` persistence; the Tag codec is SDK framework (CL-SAV-01/02) |
| A8 | RNG stream behavior is part of the observable contract (three generator families with distinct roles; the seed-source choice must be made and documented) | The three families are distinct owned generators (TD-13/CD-02); the seed-source decision is a compatibility decision (CD-02), not an afterthought |
| A9 | Frame presentation ordering is a visual-comparison contract (swap-before-onDraw + display buffer + FMV tick cadence) | The frame driver preserves the decompiled 21-phase order exactly (CL-ENG-01; platform/02 §3) — including the frame-start display swap |
| A10 | State-transition atomicity at the frame top (queued → applied next frame; old destroyed before new inits; native-table reinstall; end-of-frame teardown coalesced to the same onDraw) | The state machine is queued-transition with a frame-top apply point and a same-onDraw end-of-frame teardown fixed point (CL-ENG-04/05) |
| A11 | Dabas is a self-contained machine island (dememulation of retail `DABAS.BIN`, own timers/RTC/DAC; host contract = 11-stage screen machine + 5 input actions + mutable rewards event) | The Dabas machine is a self-contained subsystem of the game crate with a fixed host contract (CL-GLP-08) |
| A12 | The per-context timing structure (60 Hz submap/wmap/credits/battle-transition, 20 Hz battle, 30 Hz title/game-over/wmap-screens; 1/60 s game-time unit; frame-locked; speed 1..16) | The frame driver implements the per-context rate table with the 1/60 s unit and frame-locked pacing (CL-ENG-02/03) |
| A13 | Bug preservation is the default (every documented retail bug/quirk is a preserved-behavior constraint unless the per-system tables mark a reference correction) | No "cleaner" alternative is taken silently; each preserved quirk is traced in `planning/06` to its row and test |

### 1.2 Freedoms — the Java-specific limitations L1–L12 (cross/03 §4)

The port may replace the *mechanism* of each L-row, provided the named observable contract survives: explicit service construction instead of static initializers (L1); a non-classloader mod discovery with the same semantics (L2); a non-`CompletableFuture` async model with the null-on-failure contract (L3); a non-exception error model with the same error *classes* (L4); a non-`Properties` global store (L5); a non-static active-mod attribution (L6); a non-monitor lock surface of the same shape (L7); non-JavaFX tooling (L8); a non-GC reclamation with the same deletion *timing* (L9); non-weak-reference delegate caching (L10); a deliberate `rand()` seed source (L11 — not a contract at all, Q-70); no updater/Discord dependency (L12). The concrete replacements are TD items in `planning/02`.

### 1.3 The nine invariants (cross/01 §5.6)

1. Single owner thread for the frame loop and all frame-bound state. 2. Frame-bound deletion (mark at transition, delete at the same-onDraw end-of-frame fixed point). 3. One-frame input edges (the present frame sees input set by the tick before it; cleared after). 4. Atomic frame-top transitions with same-frame end-of-frame teardown. 5. Audio decoupling (independent real-time clocks; queues + control words only). 6. Isolated event delivery (per-handler; the critical set rethrown to the crash path). 7. Read-mostly config (explicit set + event; no per-frame writes). 8. The globals as the shared surface (the documented layout is the contract). 9. Locked async callbacks (any async completion touching a shared resource takes the narrow lock — VRAM, SPU, audio control, event listeners).

## 2. Workspace layout

**Proposed (TD-01, TD-02).** One Cargo workspace rooted at the repository root:

```
SeveredChains/
├── Cargo.toml                  # workspace: members, shared lint profile, Cargo.lock committed
├── AGENTS.md, research/, planning/, scripts/
└── crates/
    ├── psxrt/                  # reusable PSX platform primitives (TD-03)
    ├── psxrt-compat/           # PSX-compatibility policies + divergence documentation (TD-04)
    ├── psxrt-import/           # asset conversion/import: original formats → engine-ready (TD-05)
    ├── severedchains-sdk/      # modding/scripting/plugin framework: registries, events, Fate VM +
    │                           #   toolchain, i18n, mod loading, Tag, config framework (TD-07)
    ├── severedchains/          # core engine runtime: frame driver, state machine, services (TD-06)
    ├── severedchains-game/     # LoD implementation: states, retail content, persistence, 1st-party mods (TD-08)
    └── scus94491/              # binary crate (proposed name, TD-09): process entry, CLI, feature wiring
```

- The six library crate names are **fixed user decisions** (AGENTS.md, 2026-11-14). The binary crate name is **not** fixed; `scus94491` (the disc id — semantic per the naming policy) is proposed over `severedchains-app` (TD-09).
- Rust edition 2024, MSRV ≥ the verified-installed 1.100.0 stable at P0 preflight (TD-01). Dependencies are recorded in `planning/02` with their verified 2026-11-14 index versions; nothing is installed or pinned until implementation is approved (TD-01/TD-24).
- No crate beyond the seven is proposed; every candidate 8th crate (test harness, golden-vector generator, dev CLI) is placed *inside* an existing crate (in-crate `tests/`, `#[cfg(test)]` modules, the binary's dev features) to avoid scaffolding (mission constraint: avoid artificial scaffolding milestones).
- Feature flags (binary-level): `dev-tools` (debugger, frame-advance, dev seed override — off by default; the reference's debugger is tooling-adjacent with no engine-state or save dependency — L8/CL-MOD-11), `updater` (L12; off by default), `discord` (L12; optional, off by default). The SDK is an **internal development framework that runs in release builds** (user decision) — nothing in the SDK is `dev`-gated.

## 3. Crate chart

For each crate: responsibility, public boundary (module map, planning-level), dependencies, data ownership, threading assumptions, and the compatibility rows it is the *primary* owner of (full row-level coverage: `planning/06`).

### 3.1 `psxrt` — reusable PSX platform primitives (TD-03)

- **Responsibility.** The *mechanical* PSX platform models and pure primitives: GPU (VRAM dual 15/24-bit store, TPage/CLUT, the ordering-table *mechanism*, GPU command representation, display buffer model), GTE (the T1 register-model subset — SZ/SXY FIFOs, error/flag register, projection test, saturation — with **both** the float behavior (default) and the integer-precision variant (T4 obligation)), SPU (24-voice model: voice structs, the ADSR phase machine, the voice ADPCM codec core (28 samples/8-byte, 3-bit filter 0–4), pitch stepping + 512-position 4-tap interpolation, FM, the shared noise generator, the reverb *network*), XA ADPCM decoder (X2/X3: the 2-bit XA filter field, the never-reset static predictor *state holder* — reset policy is `psxrt-compat`), the Bpp codec (G2), the exact blend-equation primitives (G10: (B+F)/2, B+F, B−F, B+F/4 with F/4-first truncating semantics), the fixed-point helpers (R1 `psxDegToRad` 4096/turn; R2 `rsin`/`rcos` float×0x1000 short-cast), the timing primitives (the 1/60 s game-time unit, vsyncMode concepts, the virtual 60 Hz vsync counter), and the RNG *primitives* (generic LCG core; the `java.util.Random`-family 48-bit LCG with bounded `nextInt(bound)`/`nextFloat(bound)` rejection semantics — the P0-verified identity, not Mersenne Twister).
- **Boundary (modules).** `gpu`, `gte`, `spu`, `xa`, `bpp`, `blend`, `fixedpoint`, `rng`, `timing`. All pure or state-machine; no I/O, no threads, no allocation-driven globals.
- **Dependencies.** None of the project crates. External (provisional, TD-24): none strictly required (std-only is the proposal; `bytemuck`-style zero-cost casts may be std-inlined).
- **Ownership/threading.** All state is owned by its caller; the crate is re-entrant and thread-agnostic (the engine decides ownership — e.g., the SPU model is owned by the runtime audio service, ticked on the SFX thread).
- **Primary rows.** G1–G2, G5 (mechanism), G7 (mechanism), G9 (mechanism), G14–G15 (mechanism), T1, T3, T5, T6, T7, S4–S6, S8 (network), X2–X3, X5 (dead — not ported by default), R1–R2, R5 (the op *bodies* live in the SDK VM but the generator they consume is this crate's primitive).

### 3.2 `psxrt-compat` — PSX-compatibility policies (TD-04)

- **Responsibility.** The *policy* layer over the `psxrt` models: which documented behavior is selected, where the reference diverges from the original, and the documented knobs. The rule: **`psxrt` models what the PSX *is*; `psxrt-compat` decides which documented behavior we *run*, and records the divergence.** Contents:
  - Same-z ordering policy (G6/CD-01): LIFO (reference; load-bearing for `Battle.applyTextureAnimation`) default, FIFO (PSX) behind a knob; the modern 2D-pool LIFO / 3D-pool FIFO equal-depth tie-break is an engine (layer-4) matter, not here.
  - GTE precision policy (T2/CD-03): float default with the documented bounded divergence (±1 SXY px, ±1 NCCS color step, animation-matrix quantization accumulation); the integer-precision submap camera (T4) is **mandatory** (the only Hardware-accurate row) — implemented as the GTE integer variant used by the submap camera path.
  - Display-width clamp (G4/CD-04): the 384→368 GPU-model clamp, documented with the two-distinct-368s note.
  - TPage rate-00 mapping (G11/CD-05): rate 00 → B+F/2 (reference; three consumers source-verified; occurs in shipped UI highlight backgrounds).
  - XA predictor continuity (X6/CD-07): the static never-reset predictor (reference) default; reset-per-file behind a knob.
  - SPU sweep volume (S7/CD-06): the reference gap (full volume, in-source TODO) vs the plausible layer-1 envelope — policy + knob; default per CD-06.
  - SPU voice-allocation policy (S3): the dynamic allocation chain (matching retrigger → bound-gated priority steal (bound 20 from `initSpu`, 8 after `initSound`) → round-robin → finished/unused fallback).
  - Blend selection (G10): exact hardware equations in the OT-replay presentation; the B+F/4 decomposition (blend + 0.25 scale, the documented ~1/255 per-channel rounding delta) in the modern presentation.
  - Divergence documentation: each policy carries its evidence (row + confidence), the selected default, the rationale (reference-fidelity default per A13), and the closure evidence (register pointer).
- **Dependencies.** `psxrt`.
- **Primary rows.** G3, G4, G6, G8 (presentation policy), G10, G11, G12, G13 (presentation policy), T2, T4, S2, S3, S7, X1 (format policy), X4, X6, X7.

### 3.3 `psxrt-import` — asset conversion/import (TD-05)

- **Responsibility.** Original formats → engine-ready representations, at unpack time (A6): the ISO9660 in-place reader (CL-AST-01: 4 discs, 2352-byte sectors, Form2, disc-4 master directory merge), the transform registry (per-node, failure → node skipped not fatal), and the transforms: MRG container (CL-AST-02), BPE decompression, DEFF container, CTMD→TMD conversion (CL-AST-04/05: on-disk run-length semantics, the CTMD-authoritative rule), TIM (CL-AST-06, incl. PXL sheets and MCQ strips), the ANIM family (CL-AST-07), the submap PXL transform, MDEC frame decode (CL-AST-09: the VLC/IDCT; the 10-sectors/frame demux structure), SSSQ/SSHD parsing (CL-AUD-10: the 16-channel music vs 24-channel SFX split), XA extraction (X1), the XA→Opus transcode (CL-AST-10: 80/63 Catmull-Rom 37800→48000, 128 kbps, pre-skip 312, the original-rate-37800 header), and the `files/` cache framework (CL-AST-03: VERSION=5, per-node incremental skip, determinism/ordering constraints, the CRC-mirror patch cache). The **script-patch pipeline is an exception**: the Fate toolchain it uses belongs to `severedchains-sdk` (user-fixed role); the pipeline *orchestration* (the 353-entry `scripts.csv` walk, the CRC32 gate over the `#include` closure, one-time backup, restore-on-removal — CL-SCR-10/CL-MOD-08) is implemented here as a transform that calls the SDK toolchain through a small trait, keeping the dependency direction clean (TD-05/TD-16).
- **Boundary (modules).** `iso`, `mrg`, `bpe`, `deff`, `tmd`, `tim`, `anim`, `mdec`, `sssq`, `xa`, `cache`, `patch` (orchestration only), `pipeline`.
- **Dependencies.** `psxrt` (Bpp, the XA decoder for the transcode). No `severedchains-sdk` (the patch transform is injected).
- **Ownership/threading.** Unpack runs on the startup unpack thread; leaf transforms may run on a worker pool (rayon, TD-17) — all output is written to the cache tree by the pipeline owner; cache writes are ownership-ordered (one writer per node), mirroring the reference's per-node incremental model.
- **Primary rows.** CL-AST-01..12 (the formats + cache), X1 (extraction), X4/X7 (transcode-side), the script-patch *results* contract (CL-SCR-10/CL-MOD-08 — the toolchain semantics themselves are SDK, TD-13).

### 3.4 `severedchains-sdk` — engine-level modding/scripting/plugin framework (TD-07)

- **Responsibility.** The framework surface that mods and the game share — user-fixed role: **SDK, registries, events, Fate VM/toolchain, i18n, mod loading**, plus the cross-cutting framework primitives the research shows are framework-level (the Tag model + wire codec — used by saves, config, presets, `modData`; the config *entry* framework — typed/registry-registered entries, the `.dcnf` codec, the three locations, the `.dpre` preset format, `ConfigCollection` semantics). Contents:
  - **Registries** (CL-MOD-04): `RegistryId` (two-part grammar `[a-z][a-z0-9_-]*`, ≤255 chars, the mod component of the *entry* id is the only enforced one), `Registry<T>` (21 instances), the one-shot Register window (the only mutability event; no add/remove events), the per-boot entry re-creation semantics, delegates (pre-registration-valid, latch-cached) with the stale-entry hazard documented.
  - **Events** (CL-MOD-05/06): the exact-class dispatch bus (TypeId-keyed; no supertype fan-out), five priority levels with stable order, per-handler error isolation (handlers return `Result`; non-critical → log + 7 s overlay toast + remaining handlers run; the critical set — exactly the 21 Register events + `AddRegistryEvent` + `RegisterAtlasTexturesEvent` → rethrown to the crash path), cancellation (the engine enforces cancellation for exactly the 7 take/give/clone behaviors, and *honors* the documented-but-unenforced `Pre*`-level-up/`AdditionUnlockEvent` cancellations — CL-MOD-06's faithful choice), the 113-event-class surface re-created as the documented family set (10 families).
  - **Fate VM + toolchain** (CL-SCR-01..13; TD-13): the VM (108-slot fixed pool, 0–8 reserved, round-robin allocation with the per-frame upper-bound reset, the 63-slot dual int/float storage with mode bits and the documented slot conventions, the FlowControl model, fork/consume/deallocate with the destructor gate, `framesPerTick` pacing, the 1024-entry native table global-first + per-state override reinstall at transition, the 592-slot catalog registration, the 4 built-in op bodies — `RAND` draws the *injected* gameplay stream (R5: `X × simpleRand() >>> 16`), the error paths (unknown opcode → error, null function → stall = PAUSE_AND_REWIND, the paramCount > 10 guard, the OOB catch-and-warn), the 9-point `ScriptLifecycleEvent` surface, script-object binding); the toolchain (decompiler, translator, assembler, patcher, meta — round-trip fidelity as the foundational gate, the documented hazard set, strip semantics, call-index substitution, table-length pinning); the C-01 per-opcode choice is CD-08 (VM = runtime table semantics; toolchain = recompiler table semantics; the discrepancy preserved, not silently resolved).
  - **i18n** (CL-MOD-07): the four-level per-key precedence (OVERRIDE > LANGUAGE_COUNTRY > LANGUAGE > FALLBACK) with key-itself fallback, the three sources (per-mod in-package, the working-dir `lang/` override tree with the ignored filename prefix, programmatic maps), `String.formatted`-equivalent templates with malformed-template key fallback, live re-translation on `lod_core:language` change without a reboot.
  - **Mod loading** (CL-MOD-01/02/03): discovery semantics (the `./mods/` scan, the semver gate over the game version, required mods `lod` + `lod_core` with the hard-failure surface, failure recording), the re-entrant 12-step `bootMods` rebuild (framework reset → load → listener discovery → `AddRegistryEvent` → the 8 registry windows → input bindings → i18n → failure toasts), `bootRegistries` (the 13 remaining windows), the campaign-scoped reboots (the 13 documented call sites are *game* call sites — CL-MOD-02), the per-campaign `enabled_mods`/`known_mods` binding codec (u16 count + NUL-terminated ASCII, the `lod-core`→`lod_core` read alias).
  - **Tag + config framework** (CL-SAV-10..15): the 10-type Tag model with the wire encodings (LEB127 varint, varint+UTF-8 string, u24+ASCII RegistryId, the MapTag nondeterministic order), the `.dcnf` codec (defaults written, skip-unknown-id, wrong-location discard, clear-then-load, the missing-file silent clear, the corrupt-file swallowed warning, the 100 KiB write buffer), the built-in byte codecs (bool/int/float/string/enum/colour/campaign-name), the `.dpre` preset format (FORMAT_VERSION 1 with legacy acceptance, schema-validated load with per-entry skips + notification, atomic temp+move writes), the `ConfigCollection` semantics (explicit values + default fallback, revision counter, `setConfig` = put + onChange + event-after-hook, the advisory mod locks, the preset-identity `refreshPreset`).
- **Boundary (modules).** `registry`, `event`, `i18n`, `mod` (container/manager/package), `fate` (`vm`, `toolchain`, `binary`, `meta`), `tag`, `config`.
- **Dependencies.** None of the PSX crates; no `severedchains`. External (provisional, TD-24): `serde`/`serde_json` for *dev-time and mod-package manifest* serialization only — never for the save/config wire formats (those are hand-rolled per the documented byte layouts — TD-15); `thiserror` for the error types.
- **Ownership/threading.** Framework data (registries, event listeners, i18n tables, the VM) is main-thread-owned (the delivery model is main-thread — cross/01 §5.4); the mod manager holds the package tree (disk) + the in-memory registry content.
- **Primary rows.** CL-SCR-01..13, CL-MOD-01..07 (the framework halves), CL-SAV-10..15 (framework; content entries are game), the R5 op-body semantics (the generator injection is game-owned, TD-13).

### 3.5 `severedchains` — core engine runtime (TD-06)

- **Responsibility.** The frame loop, the state machine, and the engine services — user-fixed role. Contents:
  - **Service construction** (CL-ENG-08): an explicit `ServiceBuilder` that constructs the fixed service set in the documented order (MODS → EVENTS → REGISTRIES → SCRIPTS → SEQUENCER → CONFIG → SAVES → PLATFORM → RENDERER → FONTS, then the PSX models and the audio threads — cross/01 §2.1 step 2), replacing the static-initializer idiom (L1) while preserving the *order* (the contract). Everything the first rendered frame might touch is initialized before the render loop starts (the reference's verified pattern, cross/01 §2.1).
  - **Frame driver** (CL-ENG-01..03; §6.2 below): the decompiled 21-phase `gameLoop` order (platform/02 §3), the per-context rate structure (CL-ENG-02), the frame-locked pacing with tick-skip and speed 1..16 (CL-ENG-03), the one-frame input edges (CL-ENG-01), the frame-start task queue (cross/01 §5.3 item 6).
  - **Engine state machine** (CL-ENG-04/05/06): single instance, queued transitions applied at frame top (menu-stack reset, `previousEngineState`, the forced `vsyncMode = 2`, destroy-before-init, the per-state native-table reinstall, `EngineStateChangeEvent`), the end-of-frame teardown fixed point (same onDraw, after the render pass; marked objects may render once more), the per-state render mode (only SUBMAP and TITLE use the LEGACY H-division pseudo-perspective).
  - **Input** (CL-ENG-07): the two-tier model (semantic actions over physical activations), per-state action sets, deadzones, rebinds, the rumble lerp, the `scriptInputActionPressed/Held` bridge surface.
  - **Rendering service**: the presentation-mode machinery (the three reference modes: 0 = GPU-model/OT-replay PSX-compatible, 1/2 = modern pools), the OT replay pass (the engine walks the ordering table z-descending and issues draws in command order — the engine-logic realization of the G5/G6 contract), the modern 2D/3D pools with the LIFO/FIFO equal-depth tie-breaks (layer 4, Q-71), the post pass (layer-4 CRT family — feature, not fidelity), the scissor stack, the display-swap + one-frame latency (G13), the LEGACY H-split projection, the async→VRAM hand under the VRAM lock (the canonical cross-thread write, cross/01 §2.2).
  - **Audio service** (CL-AUD-01/10; §6.6): the device hosting (cpal, TD-11), the two real-time threads (music ≥ 120 Hz min-pace; SFX 60 Hz with the 3× catch-up skip), the volume/routing domains (the dual 8/15-bit main volume, the 7-bit sequence volume, per-voice chains, the pan tables, the dual reverb networks — S8), the SPU edge-word protocol host (S2: accumulate/snapshot/zero at the tick's first sample slot), the `XaPlayer` contract (CL-AUD-08: the staged 0–4 API, the 10 ms ticks, 8 buffers, ≥ 40 ms guard, drop-on-full backpressure, the playerVolume SFX×MASTER scaling), the device-watch reinit (2 s poll).
  - **Resource loading** (CL-AST-11): the async pool (rayon, TD-17), the **null-on-failure** contract (absent asset → completed-with-null, never a torn state), the `scriptWaitForFilesToLoad` wait primitive (the VM returns PAUSE_AND_REWIND while loads are pending — the primitive is engine; the op dispatch is SDK), the VRAM upload on dirty at frame start.
  - **Error model + shutdown** (CL-ENG-09/12; §6.7): the layered error classes (gameplay crash → recovery save; recoverable infrastructure; the critical-event set), the documented shutdown order, the crash-recovery save path (the observable surface: a save exists at the last savable state).
  - **Save/config hosting** (CL-SAV-05/06/07): the save manager (the 11-magic dispatch is *game* — the readers live in `severedchains-game`; the runtime hosts the load-path *ordering* — config → registry boot → `createGameState` → state data → `modData` event → campaign hook → `GameLoadedEvent` → menu unload), `lastSavableEngineState` (set only by the WMap/SMap states), the campaign directory model.
- **Boundary (modules).** `builder`, `frame` (driver, pacing, input edges), `state` (machine, transitions, teardown), `input`, `render` (modes, OT replay, pools, post, VRAM service), `audio` (threads, SPU host, XaPlayer, routing), `load` (pool, wait primitive), `save` (ordering, campaign model), `error`.
- **Dependencies.** `psxrt`, `psxrt-compat`, `psxrt-import`, `severedchains-sdk`. External (provisional): `wgpu`/`winit`/`naga` (TD-10), `cpal` (TD-11), `gilrs` (TD-12), `rayon` (TD-17), `thiserror`/`anyhow` (TD-18), `tracing` (TD-19).
- **Ownership/threading.** The frame-loop thread (the main thread; "Hardware" in the reference) owns all frame-bound state. The audio threads own their sample pipelines and communicate via SPSC queues + the control words (the narrow locked surface of cross/01 §5.4, realized as std `Mutex`/atomics over exactly those surfaces — L7). The unpack thread and the async pool are startup/I/O only.
- **Primary rows.** CL-ENG-01..13, CL-AUD-01, CL-AUD-07 (routing), CL-AUD-08 (hosting), CL-AUD-09, CL-AST-11 (hosting), the G-row *presentation* obligations (G3, G8, G13 via the render service), the S2 host protocol, the CL-MOD-12 J-table contracts it hosts.

### 3.6 `severedchains-game` — LoD implementation (TD-08)

- **Responsibility.** The LoD-specific game — user-fixed role: retail content, engine states, battle, persistence, first-party mods. Contents:
  - **The `GameModule`** (the static first-party integration; TD-08): implements the SDK's module trait — registers the three first-party mods (`lod_core` 127 entries, `lod` 1110, `turn_order` 1 — the 1238-entry census, CL-MOD-10), the 88 config entries (80/7/1 split), the 70 input actions (41/29), the engine states (title, credits, submap, world_map, battle, final_fmv, game_over — registry-registered per CL-MOD-10, including title/credits as *mod* entries), the 63 spells / 9 elements / 17 stat types / 9 character templates / 512 encounters / 148 equipment / 54 items / 37 goods / 41 shops / 35 additions / 10 battle actions / 44 DEFFs / 2 bent types / 4 level-up actions / 1 campaign type.
  - **The states** (I6–I9 of the roadmap): the `GameState52c` (the documented layout, offsets 00–4e3, cross/01 §4.1 — a compile-time layout assertion set) + the segment-class globals (cross/01 §4.2 inventory), title (the 11-case loading machine, the 5-option menu, the FMV-0 takeover as a render-callback takeover that queues TITLE — CL-ENG-13), submap (the 18-value machine, the 8-stage media pipeline, the collision/transition geometry, movement + the 144 native script functions, the encounter system with the float accumulator @ 800c6ae8, `mapTransition` dispatch, the integer-precision camera (T4), the savepoint MRG directory, the legacy 0x3256 migrator), world map (the path-graph navigation over the 256-location geography, the `wmapFlags` gating, the int accumulator, special travel — Coolon/Wingly/Queen Fury/Barrier Stations as pseudo-cut-999 transitions), battle (the 28-stage load machine, the turn system, the formula surface, the additions, the status ailments, dragoons, the post-battle report — the "engine is the law, the script is the courtroom" boundary, CL-GLP-01), credits/game_over (the 640×240 MCQ game-over screen, the submap reset to cut 675 on defeat), Dabas (the dememulated machine island, CL-GLP-08).
  - **Persistence** (CL-SAV-01..09): the 11-magic readers (RETAIL + V1–V10, each version understanding itself; no V_n→V_{n+1} converter), the RETAIL 0x6E4 layout (the struct at file offset 0x1FC; the 0x19C/0x1E8/0x2E9 inventory fields), the V10 writer (self-describing MapTag, the 512×512 PNG atlas, `engineStateData`, `modData`, the config tail), the migrators (WMAP 0x3156 / SMAP 0x3256, the V8 `SaveMigrator`), the memcard model (0x20000, 16×0x80 directory, 15×0x720 slots, 7 product codes, the timestamp-ordered re-serialization — Q-80 open), the crash-recovery save (CL-ENG-12), the savable-state model (WMap/SMap only; the 5-field WMap path Tag; the SMap 4-field + `primaryPartyBackup` + 60.0f unstuck budget).
  - **Gameplay rule sets** (M9): the M9 retail-vs-reference tables are the row-level authority (CL-GLP-01..17); every documented retail bug/quirk is preserved (A13/CL-GLP-16); the RQ7 spectrum is the placement contract (title/wmap: zero scripts; Dabas: the machine island; submap: script as policy layer; battle: engine law + script courtroom; inventory/characters: native + data-driven, script-thin edge).
  - **Audio content** (CL-AUD-02..06, CL-AUD-10): the BGM sequencer (the per-sample renderer over the parsed SSSQ — the behavioral mirror, CL-AUD-07; BGM sample-accurate at BPM), the SFX sequencer (the decompiled facade over the SPU model; the SFX event grammar; the 8.0 ms/tick at the 60/480 default — Q-74 documented; the 0xa0 note-on family; the documented gaps), the per-cut music tables (submap 8×8, battle 32-entry ROM tables), the sound content (the SSHD banks).
  - **RNG wiring** (CL-GLP-02; CD-02): the three generator families as owned instances — `GameplayRng` (LCG16 ×9+0x3711, seed 3 @ 8004dd44, the low-12-bit vsync guard), `PresentationRng` (LCG15 ×0x41c64e6d+0x3039, initial 0x24040001, re-seeded at submap entry per the CD-02 decision), `BattleRng` (the `java.util.Random`-family 48-bit LCG; process-initialized; `scriptSetMtSeed` = `setSeed(seed ^ 0x75bd924)`, fn 849) — plus the separate per-battle turn-order jitter RNG (seeded from `timestamp_a0`, layer 4 GH#2481) and the per-instance submap snow RNG (P0 verification: five instances total, not three).
- **Boundary (modules).** `module` (GameModule + the three first-party mod definitions), `state` (`title`, `submap`, `wmap`, `battle`, `credits`, `game_over`, `dabas`), `state_data` (the `GameState52c` layout + globals + the reverse-engineering map), `persistence` (the 11 readers, the V10 writer, the migrators, the memcard model), `content` (the retail catalogs), `characters`, `inventory`, `shops`, `progression`, `audio`, `rng`.
- **Dependencies.** `severedchains`, `severedchains-sdk` (and transitively `psxrt` where state code names PSX model types — e.g., the GTE camera program).
- **Ownership/threading.** Main-thread-only (all game state; the async load *callbacks* that write VRAM go through the runtime's locked hand — cross/01 §5.4 hazard 1).
- **Primary rows.** CL-GLP-01..17, CL-SAV-01..09 (content + readers/writer), CL-AUD-02..06/10, the CL-ENG-02 write-site census (the per-context rates live here as state behavior), the R1–R5/R3/R4 *consumer* sides, the CL-MOD-10 content entries, the CL-AST-12 runtime-consumer side.

### 3.7 `scus94491` — binary (TD-09, proposed)

Process entry: argument parsing (clap, TD-21: `--assets`, `--mods`, `--config`, `--saves`, dev flags), the boot-chain orchestration (cross/01 §2.1: updater skip (L12) → service construction → the unpack thread ∥ main init handoff (`INIT_LOCK` analog) → platform + `bootMods` → global config → audio init → renderer init → the render loop → the transition gate → ROM init (`gameInit` + `preload` as *game-provided* boot steps) → SFX thread start → the render-callback swap → FMV 0 → TITLE), the shutdown order (CL-ENG-09), the top-level catch (crash-recovery path, CL-ENG-12). The dev features (debugger, frame-advance, dev seed override, the updater) are feature-gated here. **Proposed name**; alternatives in TD-09.

## 4. Dependency graph (investigated, not assumed)

The user's starting hypothesis was *game → sdk + runtime; runtime → sdk + psxrt + psxrt-compat + psxrt-import*. Investigation (against the M8-01 dual SC/SCDK deployment, the M8-02 registry/event ownership, the M3 VM placement, and the A5/A6/A7 obligations) confirms the hypothesis **with two refinements**:

```
scus94491 (bin) ─┬─────────────▶ severedchains-game
                 └─────────────▶ severedchains ─┬─▶ severedchains-sdk
                                                ├─▶ psxrt-import ─▶ psxrt
                                                ├─▶ psxrt-compat ─▶ psxrt
                                                └─▶ (external: wgpu, cpal, gilrs, rayon, ...)
severedchains-game ─▶ severedchains ─▶ severedchains-sdk (standalone; no PSX deps)
```

Refinement 1 — **`psxrt-import` also depends on `psxrt`** (the transcode and the Bpp upload path need the platform codecs); the hypothesis did not name this edge. Refinement 2 — **`severedchains-sdk` depends on no other project crate** (the user hypothesis was silent on sdk's edges). This is what avoids the one real cycle risk: the SDK defines the shared contracts (`EngineState`, `GameModule`, the registry/event/i18n/mod/Tag/config frameworks, the Fate surface) that the runtime *implements and hosts*; if the runtime's public API were the source of those contract types, the SDK would have to depend on the runtime while the runtime hosts the SDK's services — a cycle. Placing the contracts in the SDK (the user-fixed "SDK" role) makes every edge one-directional:

| Edge | Justification (research-grounded) |
|---|---|
| bin → game, bin → runtime | The process hosts both; the game provides the `GameModule` (static first-party integration — TD-08) |
| game → runtime | States call runtime services (frame, input, render, audio, load, save ordering); the runtime cannot know game states (they are registry content — A5) |
| game → sdk | Content registration (1238 entries), event handlers, native script functions, i18n, first-party mod definitions, the Tag/config *content* |
| runtime → sdk | The runtime *hosts* the registry/event/i18n/mod-manager/VM services and implements the SDK contract traits (the construction order MODS → EVENTS → REGISTRIES — cross/01 §2.1 step 2) |
| runtime → psxrt(-import/-compat) | The render/audio/load services consume the PSX models, the compatibility policies, and the format codecs (runtime asset loading of TMD/TIM/ANIM/SSSQ — cross/01 §3 services table) |
| import → psxrt | The XA→Opus transcode decodes via the XA codec; the Bpp conversions use the codec |
| compat → psxrt | Policies select/parameterize the models |

No cycle exists. The SDK's independence is the load-bearing property (it must be able to compile/patch scripts and host registries without the engine — the M8-03 debugger's live-stepping coupling to the recompiler `Translator` is the precedent for the toolchain standing alone).

## 5. Runtime execution architecture

### 5.1 Process lifecycle (CL-ENG-08; cross/01 §2.1)

**Proposed** mapping of the 14-step boot chain to Rust ownership (the *order* is the contract — L1):

1. *Updater swap* — feature-gated (L12); no-op by default. 2. *Service construction* — the `ServiceBuilder` (ordered, explicit fields; no statics). 3. *Update check* — feature-gated; async; never blocks (the `UPDATER_LOCK` analog is a `std::sync` handshake). 4. *Unpack thread* — sets the loading-screen render callback **before** the renderer init (the reference's verified cross-thread registration, safe because read only at onDraw time); runs the `psxrt-import` pipeline (ISO → transforms → `files/` VERSION=5 → the script-patch walk); signals unpack-complete under the init gate. 5. *Platform + mods* — platform init (window/gamepads), `findMods`, `bootMods(all)` (the 12-step re-entrant rebuild — SDK). 6. *Global config* — **after** `bootMods` (entries must be registered first — CL-SAV-12). 7. *Audio* — the audio service init (device + context; the BGM/XaPlayer sources registered; the device-watch scheduler) → SPU init (the SPU's source registered). 8. *Renderer + GPU* — window; shader families; the GPU model init (VRAM textures, display mesh, 320×240). 9. *Render loop starts* — the music thread starts (120 Hz); the SFX thread does **not** exist yet (intro silence is by design — cross/01 §2.1 step 9). 10. *Transition gate* — `cinematicFinished && !unpacking && updateCheckDone → transitionToGame()` under the init gate. 11. *ROM entry* — `gameInit()` + `preload()` as game-provided boot steps (the ordering-table configuration (bits 14, zShift 0, size 0x4000, zMax 0x3ffe), draw offsets, `InitGeom` + projection 640, `initSound`, `vsyncMode = 2`, `loadSystemFont` (font.tim → VRAM (832, 424) + 32-entry CLUT), `SCRIPTS.clear()`, `loadShadow`, `initFmvs`; `preload`: the menu sounds, `resizeDisplay(320, 240)`, the async DRGN0/6669 MRG load with the vramLock VRAM upload, textZ = 13). 12. *SFX thread start*. 13. *Frame-callback swap* — `gameLoop` becomes the per-frame callback; window close → the shutdown chain. 14. *FMV 0 → TITLE* — the opening MDEC (`STR/DEMOH.IKI`) as a render-callback takeover that queues TITLE (not an engine state — CL-ENG-13).

Shutdown (CL-ENG-09): close callbacks in registration order (game handler: stop sound → SFX thread stops; audio service stop → device destroyed; debugger exit — no-op unless started) → async pool shutdown → platform stop → loop exit → config save → service destruction chain (shaders/objs/textures → audio → renderer → platform). **Proposed**: the final step is an explicit clean process exit (the reference's "JVM exits" is `Inferred` — cross/01 §2.6; the Rust equivalent is deterministic).

### 5.2 Frame driver (CL-ENG-01; platform/02 §3)

**Proposed**: the frame driver executes the decompiled 21-phase order **exactly** (A9 — the visual-comparison contract), each phase bound to its service call:

| # | Phase (decompiled) | Rust binding |
|---|---|---|
| 0 | `endFrame()` — display swap at frame start (the frame renders into the *other* buffer; `syncFrame`/`swapDisplayBuffer` lambdas) | `gpu.end_frame()` (swap + the mode-change reinit frame) |
| 1 | `GPU.startFrame()` — dirty VRAM → texture; ordering-table resize | `gpu.start_frame()` |
| 2 | timestamp — `timestamp_a0 += vsyncMode` if `advancesTime()` | `game_state.advance_time(vsync_mode)` |
| 3 | pacing — `hz = 60 / max(1, vsyncMode) × speedMultiplier`; `setFpsLimit` + `setInputTickRate` | `pacer.set_rate(hz)` (frame-locked; tick-skip on slowness; speed 1..16; CL-ENG-02/03) |
| 4 | `loadQueuedOverlay()` — the queued transition (frame-top; CL-ENG-04) | `state_machine.apply_queued()` (menu-stack reset, `previous`, forced `vsyncMode = 2`, destroy/init/readSave, native-table reinstall, `EngineStateChangeEvent`) |
| 5 | `renderUi()` | `game.render_ui()` |
| 6 | `currentEngineState.tick()` — gameplay **before scripts** | `state.tick(ctx)` (base: the four movement-axis actions → analogue magnitude/angle) |
| 7 | `EVENTS.postEvent(RENDER_EVENT)` — the one shared static instance (handlers side-effect-only — CL-MOD-06) | `events.post(&mut render_event)` |
| 8 | `loadAndRenderMenus()` | `game.load_and_render_menus()` |
| 9 | `uploadRenderables()` | `game.upload_renderables()` |
| 10 | `SCRIPTS.tick()` — the VM frame (every `framesPerTick`: 1 normal, 2 submap exploration) | `scripts.tick()` |
| 11 | `postScriptTick(ticked)` | `state.post_script_tick(ticked)` |
| 12 | `tickAndRenderTransitionIntoBattle()` — SBtld timing (`vsyncMode = 1` during the transition) | `game.tick_battle_transition()` |
| 13 | `handleFullScreenEffects()` — fades | `game.tick_fullscreen_effects()` |
| 14 | `startQueuedSounds()` — SPU key-on queued voices (the edge-word consumer point) | `audio.start_queued_sounds()` (SPU tick sees the accumulated words — S2) |
| 15 | `handleTextboxAndText()` + `renderTextboxes()` (scroll speeds ÷ `tickMultiplier`) | `game.tick_textboxes()` |
| 16 | `overlayTick()` — after everything else is rendered | `state.overlay_tick()` |
| 17 | `FUN_80020ed8()` — submap transition tracking (`_800bd7b0` = 2/9/−4/3; game var 67; `_800bd7ac`) | `game.track_submap_transition()` |
| 18 | `tickCount_800bb0fc++` | `frame_counter += 1` |
| 19 | `GPU.endFrame()` — z-queue render (highest z down) + display present + flip | `gpu.end_frame_render()` |
| 20 | `DISCORD.tick()` — layer-4 (feature-gated, L12) | feature-gated; no-op by default |

The ordering guarantees (cross/01 §5.3) are enforced structurally: input before present, present before tick, tick before clear (the one-frame input edges — cleared after the frame); transition before any other phase (phase 4); `state.tick` → `SCRIPTS.tick` → `postScriptTick`; SPU queued sounds and textboxes after scripts; overlay tick last; `GPU.endFrame` after overlay tick; `tickCount` increment at 18. The per-frame *wrapping* (task flush, resize, pre, stale-ref purge, the render pipeline, bookkeeping — the reference's `RenderEngine` structure, rendering/01) is the driver's outer structure (L — mechanism free; the *contents* of the pipeline are the render service's job, §3.5).

### 5.3 Threads and clock domains (cross/01 §5.1–5.2)

**Proposed** Rust thread model (the reference's 8 threads; the mechanism is free — L3 — the *contract* is the thread inventory's requirement classes):

| Thread (Rust) | Reference analog | Contract |
|---|---|---|
| main ("hardware") | main/"Hardware" | **Single owner** of the frame loop and all frame-bound state (A3); input tick → render tick (swap + onDraw → the 21 phases) |
| music | OPEN_AL | Real-time BGM synthesis (per-sample sequencer over SSSQ; sample-accurate at BPM — CL-AUD-02); the XaPlayer ticks (10 ms, 8 buffers, ≥ 40 ms guard); SPU sample consumption; device watch (2 s). Communicates with main **only** via queues and control words (A5 in cross/01 §5.2) |
| sfx | SFX | 60 tps SPU tick (735 stereo samples/tick at 44.1 kHz — S1); the game sequencer facade (8.0 ms SFX ticks — CL-AUD-02); the edge-word protocol (S2). Per-tick error isolation (the reference's per-tick `Throwable` catch — the Rust analog: the tick returns a `Result`, logged, thread never dies) |
| unpack | unpack thread | Startup only; the `psxrt-import` pipeline; the init-gate handoff |
| pool (rayon) | Async pool | On-demand I/O/transforms; **async completions touching VRAM take the VRAM lock** (the canonical hand — cross/01 §2.2); null-on-failure (CL-AST-11) |
| (none) | JavaFX | Debugger only (feature-gated; L8); the pause/frame-advance flags become a dev-mode channel |
| (none) | Discord | Feature-gated (L12) |

The 10 clock domains (cross/01 §5.2) are preserved: the 60 Hz game-time unit; frame pacing (software, no vsync, tick-skip — CL-ENG-03); the SPU 60 tps; the music ≥ 120 Hz min-pace; the SFX 8.0 ms ticks; BGM sample-accurate at BPM; Opus 10 ms ticks; FMV 15 fps / 2× CD sector cadence driven by render ticks; Dabas machine clocking (T0 = frame tick); speed multiplier (1..16 scaling both real rate and the virtual counter). Consequence preserved: under sustained slowness the game runs slower than nominal while audio keeps real time.

**Lock inventory (the narrow locked surfaces, cross/01 §5.4 — L7):** (1) the VRAM store (all writes, incl. async, under it; the main-thread upload at frame start); (2) the SPU model (tick + all control methods — the reference's `synchronized(Spu.class)`; the `setPlayerVolume` float stays unlocked — audit F-02); (3) the audio-thread control (the reference's `synchronized(this)` + per-source monitors → a single audio-control mutex + per-source guards); (4) the event-listener list (the reference's listeners lock — list mutation during dispatch). **Eliminated by ownership** (hazards that vanish in Rust, with the contract intact): the active-mod statics (per-dispatch-context attribution — L6), the `Properties` config store (owned by the config framework — L5), the `statusText` single-writer (owned by the unpack pipeline, published by value), the debug flags (owned by the dev feature), the `loadingMonsterModels` counter (a `std::sync::atomic::AtomicUsize` — a cross-thread counter, not a lock, as in the reference), the `Config` store (read-mostly, main-owned). The XA static predictor (X6/CD-07) is **kept as static-style process state by design** (the policy decision records the divergence).

### 5.4 State ownership and the shared surface (cross/01 §4)

**Proposed** ownership table (the A1/A2 obligations realized structurally):

| State | Owner | Lifetime | Notes |
|---|---|---|---|
| `GameState52c` (800babc8) + campaign | main; owned by the save manager | campaign | Created at new game; reloaded at save load; the serializers walk it (CL-SAV-02); the layout is the documented 00–4e3 field map with compile-time assertions (A2) |
| Segment-class globals (cross/01 §4.2 inventory) | main | process/campaign | Each in its owning service/state (timing globals → frame driver; submap/battle/wmap globals → their states; the encounter accumulator 800c6ae8 → a typed split (float in submap, int in wmap — the same address, different types, documented) |
| Script states (108 slots) + the native table (8004e29c) | main; owned by the SDK VM | process | The 1024-entry table is reinstalled per-state at transition (CL-ENG-04); `doNothingScript` @ 8004f650 is the null default |
| Battle state (8006e398) + entities | main; owned by the battle state | battle | The 256-slot asset pool + combat vars 1–20; `CombatantStruct[10]`; the write-back contract (CL-GLP-10) |
| Dabas machine | main; owned by the Dabas island | session | The mirrored memory map (virtual = 0x2000000 + ROM offset); the 0x260-byte save tail (CL-GLP-08) |
| VRAM (1024×512) | render service; **locked** (the one true shared resource) | process | All writes under the lock (async included); main-thread upload at frame start (G14) |
| `files/` cache | import pipeline | machine | VERSION=5; per-node incremental (CL-AST-03) |
| Resource objects (meshes, textures) | render service pools | scene/persistent | The frame-bound deletion protocol (CL-ENG-05): mark at transition, delete at the same-onDraw end-of-frame fixed point; `persistent` survives (shadow parts, UI) |
| Save data / campaigns / memcards | save manager (game) | disk | The load-path ordering (CL-SAV-06) |
| Config (GLOBAL/CAMPAIGN/SAVE) | config framework (SDK) + content (game) | disk | Read-mostly; explicit set + event (invariant 7) |
| RNG instances (5 — CL-GLP-02 + P0 census) | main; owned by the game (injected into the VM for `RAND`) | process/battle | Stream order = call order (A3); the seed-source decision (CD-02) |
| Registry content / event listeners / i18n tables | SDK framework; main-thread | process/boot | One-shot windows (CL-MOD-04); per-boot entry re-creation |
| Audio sample buffers / device | audio service | process | Real-time threads; the narrow locked control surface (§5.3) |

The nine invariants (§1.3) map 1:1 onto this table (invariant 8: "the globals as the shared surface" = this table is the contract; the reverse-engineering map per state keeps the original identifiers — the AGENTS.md source-traceability rule).

### 5.5 Asset loading (CL-AST-11; cross/01 §3)

**Proposed**: the load contract is **null-on-failure** (absent asset → a completed load with `None`; never a torn state) — the reference's Q-43-closed contract. The pool is rayon (TD-17); loads are per-node against the `files/` tree; the runtime load API returns a handle resolved at completion; the script-side wait (`scriptWaitForFilesToLoad` @ 0x80017564) is an engine primitive returning the PAUSE_AND_REWIND flow control while pending (the VM consumes it — the op is SDK, the mechanism is runtime). Async completions that write VRAM take the VRAM lock (the canonical hand — `preload`'s DRGN0/6669 upload is the documented instance).

### 5.6 Audio execution (CL-AUD-01/08; cross/01 §3)

**Proposed**: three pipelines on two threads (the hosting is L — free; the contracts are fixed): **music** (the per-sample BGM renderer over the parsed SSSQ on the music thread — the behavioral mirror, CL-AUD-07: timing and output are **not** claimed sample-identical to retail; BGM sample-accurate at the SSSQ BPM tempo — CL-AUD-02), **SFX** (the decompiled facade + the SPU model on the sfx thread; the edge-word protocol S2 — accumulate/snapshot/zero at the tick's first sample slot, ≤ 1/60 s quantization; the dynamic voice allocation S3), **XA/Opus** (the `XaPlayer` on the music thread — the staged 0–4 API, 10 ms ticks, 8 buffers, ≥ 40 ms guard, pre-queue 4, one 480-sample frame per tick, the SFX×MASTER scaling, drop-on-full backpressure — CL-AUD-08; plus the main-thread FMV path: the 15 fps demux loop with native-rate XA decode — X4). Volume routing (CL-AUD-07): LODXA under the SFX knob, FMV under the FMV knob, BGM under the MUSIC knob; the dual-domain 8/15-bit main volume; the 7-bit sequence volume; the pan tables; the dual reverb networks (S8). The SFX sequencer *content* (the grammar, the banks) is game; the SPU model is `psxrt`; the edge-word protocol is `psxrt-compat` policy + runtime hosting.

### 5.7 Error model and shutdown (CL-ENG-12; CL-MOD-05; L4)

**Proposed** (TD-18): the reference's exception-based flow (L4) becomes a `Result`-based model with the same **error classes** as contract: (a) *gameplay crash* — any unhandled error in gameplay (the Rust panic, caught at the frame boundary via `catch_unwind`) → if a campaign is active and `create_crash_save` (default true): load all saves, unique "Crash Recovery N" name, submap position restored from the live collision globals (mirroring SMap's own save path), `lastSavableEngineState` reconstructed, save written → crash log + mod census + `CRASHLOG-<ts>.log` copy → process exit(1); (b) *recoverable infrastructure* — unpack failure = stuck loading screen (non-fatal); audio device loss = park + retry loop; (c) *mod failure* — recorded + title toast (no pre-UI hard failure except missing required mods — CL-MOD-01); (d) *critical event handler error* — the critical set (21 Register + `AddRegistryEvent` + `RegisterAtlasTexturesEvent`) rethrown to the crash path (CL-MOD-05); non-critical → log + 7 s overlay toast + remaining handlers run. The observable recovery surface is the contract; the mechanism (panic vs Throwable) is free (L4).

## 6. SDK / runtime / game boundaries (the ten boundary questions)

1. **How do game systems access engine services?** Through an explicit service context passed to state init/tick — the runtime's `EngineServices` struct (frame pacing, input, render, audio, load, save ordering, RNG injection, the native-table handle). No statics (L1); no global service access from game code. The context is constructed per-frame (cheap: a fat pointer) and handed to `state.tick(&ctx)`; async callbacks carry the narrow locked handles they need (VRAM, SPU).
2. **How does the runtime invoke game behavior?** Through the SDK contract traits the game implements: `EngineState` (`init`, `tick`, `post_script_tick`, `overlay_tick`, `render`, `destroy`, `advances_time`, `write_save_data`/`read_save_data`, the render mode) and `GameModule` (the first-party mod/content registration). The runtime never names game types; game states are *registry content* (the `engineStateTypes` registry — CL-MOD-10: even title/credits are mod-registered).
3. **Which SDK contracts exist?** `Registry<T>` + `RegistryId` + the window/delegate model; the event bus (exact-class, priority, isolation, cancellation, the critical set); `i18n` (the 4-level precedence, live re-translation); the mod manager (discovery, the semver gate, `bootMods`/`bootRegistries`, the campaign binding); the Fate surface (`ScriptHost` — the VM's native-dispatch + var/flag/storage access + lifecycle events; the toolchain library); `Tag` (the 10-type model + codec); the config framework (entries, `.dcnf`/`.dpre`, `ConfigCollection`); `EngineState` + `GameModule` (the two integration contracts).
4. **How do scripts interact with engine/game state?** The VM (SDK) addresses: `var[]` = `scriptData_08` (the documented indices — 25 `vsyncMode`, 40/41 Dabas hand-off (retail), 42 forced turn, 43 battle entry, 45[242/243] retail tutorial/counter, 51 ally count, 63 party-flag backup, 67 submap transition mode, 125 shop id), `stor[63]` dual int/float, the flag spaces (bits 16–27 script range; the cancelable `ScriptFlags1/2ChangedEvent`/`ReadGlobalFlagsEvent` interception), `scriptReadRegistryEntryVar` (registry entries implementing the script-readable surface), and the 1024-entry native table (533 distinct functions; global-first + per-state override, reinstalled at every transition — CL-SCR-07). The game provides the `ScriptGameAccess` implementation over its state; the runtime provides the frame-tick scheduling and the `scriptWaitForFilesToLoad` mechanism; the 4 built-in op bodies (R5) consume the injected gameplay RNG.
5. **Which registries/events are shared across the three layers?** All 21 registries and the 113-event-class surface are SDK framework; *content* is game (1238 entries) with engine state types contributed by first-party mods (CL-MOD-10). The event bus is shared: the engine posts the per-frame events (`RenderEvent` phase 7, `InputPressed/ReleasedEvent`, `EngineStateChangeEvent`), the game posts the gameplay events (the M8-02 family table: `EnemyRewardsEvent`, `MonsterStatsEvent`, `ShopSellPriceEvent`/`ShopContentsEvent`, `DabasRewardsEvent`, `RepeatItemReturnEvent`, `PartyFlagsChangeEvent`, `GiveGoodsEvent`, `LevelUp*` Pre/Post, `AdditionUnlockEvent`, the 9-point `ScriptLifecycleEvent`), and mods observe/mutate within the isolation + cancellation contract.
6. **How do first-party content, states, and mods integrate?** The `GameModule` (statically linked, TD-08) instantiates the three first-party mod definitions (`lod_core`, `lod`, `turn_order` — SDK `ModDef` implementations) and their 1238 registry entries in the documented boot windows (the 8 `bootMods` + 13 `bootRegistries`); the per-campaign enabled-mod sets, the required-only memcard boot, and the new-game/title reboots are the 13 documented call sites (CL-MOD-02) — in the game states, calling the SDK mod manager. Saves store registry entry ids (A5) — the ids are the stable external representation (the AGENTS.md compatibility-exception list: serialized keys, registry identifiers, script-facing names are never renamed).
7. **How are external mods and plugins handled?** **Proposed (TD-16)**: two tiers. (a) *Data mods* — a drop-in package under `./mods/<mod-id>/` (a manifest with id/version/required/enabled-by-default, `lang/` i18n overrides, asset files applied through the import pipeline's replacement mechanism — the reference's one Chester-texture precedent, the `replacements/` model, modding/03 §3; script patches through the pipeline). Loaded by the mod manager at `bootMods` with the documented discovery/semver/failure semantics. (b) *Code mods* — first-party only at initial release: statically linked as `ModDef` implementations. A dynamic code-mod ABI (a dlopen-style boundary) is a **deferred decision** — recorded here, not designed (it would need the compatibility-exception list and an ABI stability policy; the reference has no such boundary — mods are same-process classes — so there is no reference contract to preserve, only an ecosystem expectation).
8. **How are dependency cycles prevented?** The one-directional graph of §4 (contracts in the SDK; the SDK depends on nothing project-side; the runtime implements the contracts; the game depends on both; the bin wires them). Enforced at the workspace level (a dependency-lint test in the bin crate's `tests/` asserting the edge set — **Proposed**).
9. **How is dev tooling excluded from runtime-only builds?** Feature flags (TD-02/TD-09): `dev-tools`/`updater`/`discord` off by default; the SDK is never dev-gated (it runs in release builds — user decision); the debugger is a dev feature of the bin (the reference's debugger is JavaFX-on-a-thread with no engine-state/save dependency — L8/CL-MOD-11 — so its Rust equivalent can be a separate dev subsystem communicating via the pause/frame-advance channel).
10. **How are compatibility-critical operations testable without a GPU/audio device?** The render and audio services sit behind traits with **Noop implementations** (the reference has a `NoopApi` analog — rendering/01) — the headless test harness (I3, planning/05) drives the full frame contract with a null presentation; the PSX models are pure (unit-testable bit-exactly); the VM is pure given an RNG injection; the save/config codecs are pure byte I/O.

## 7. Cross-crate interface catalog (planning-level signatures; **Proposed**)

Signatures are sketches for review — the exact shapes are design detail, not contract (the contracts are the CL rows). Naming follows the AGENTS.md semantic-naming policy (no placeholder identifiers; the reverse-engineering map keeps the original identifiers in docs/comments).

```rust
// severedchains-sdk
pub struct RegistryId(/* two-part: mod + entry; [a-z][a-z0-9_-]*, ≤255 */);
pub trait Registry<T: RegistryEntry> { /* register (window-only), get, get_delegate, list */ }
pub trait RegistryEntry: 'static + Send { fn id(&self) -> RegistryId; }
pub struct EventManager; // exact-class dispatch, 5 priorities, isolation, the critical set
pub trait Event: 'static + Send {} // TypeId-keyed; no supertype fan-out
pub enum HandlerError { NonCritical, /* critical set rethrown */ }
pub struct I18n; // 4-level precedence, key-itself fallback, live re-translation
pub struct ModManager; // discovery, semver gate, boot_mods(12-step), boot_registries(13 windows)
pub trait ModDef { fn id(&self) -> &str; fn version(&self) -> Version; fn required(&self) -> bool;
                   fn register(&self, ctx: &mut ModBootContext); } // the 12-step rebuild calls this
pub trait EngineState { fn init(&mut self, ctx: &mut StateContext); fn tick(&mut self, ctx: &mut TickContext);
                        fn post_script_tick(&mut self, ctx: &mut TickContext, ticked: bool);
                        fn overlay_tick(&mut self, ctx: &mut TickContext);
                        fn render(&mut self, ctx: &mut RenderContext);
                        fn destroy(&mut self, ctx: &mut StateContext);
                        fn advances_time(&self) -> bool;
                        fn write_save_data(&self, ctx: &SaveContext) -> Option<Tag>;
                        fn read_save_data(&mut self, tag: &Tag, ctx: &SaveContext);
                        fn render_mode(&self) -> RenderMode; } // SUBMAP/TITLE → legacy (CL-ENG-06)
pub trait GameModule { fn register_first_party_mods(&self, ctx: &mut ModBootContext);
                       fn rom_init(&self, ctx: &mut RomInitContext); } // gameInit + preload (cross/01 §2.2)
pub enum Tag { Bool, Int, Float, Str, RegId, Map, List, /* the 10 documented types */ }
pub struct ConfigCollection; /* entries, .dcnf/.dpre, the 3 locations, the event-after-hook */
pub struct FateVm; /* 108 slots, the 63-slot dual storage, pacing, the 1024-entry table, the 9 lifecycle points */
pub trait ScriptHost { fn native(&self, index: u16, args: &[ScriptValue]) -> FlowControl;
                       fn var(&self, i: usize) -> i32; fn set_var(&mut self, i: usize, v: i32);
                       /* the flag surface with the 2 cancelable interception events */
                       fn rng_draw(&mut self) -> u16; } // the RAND op body (R5) consumes this
pub mod fate_toolchain { pub fn decompile(...); pub fn assemble(...); pub fn apply_patch(...); /* round-trip gate */ }

// severedchains
pub struct Engine { /* the builder output: frame driver, state machine, services */ }
pub struct ServiceBuilder; /* the ordered construction (cross/01 §2.1 step 2) */
pub struct FrameDriver; /* the 21-phase order; the per-context rates; the frame-locked pacing */
pub struct EngineServices; /* the per-frame context (§6.1) */
pub trait RenderBackend { /* the 3 presentation modes; OT replay; the modern pools; the Noop impl for tests */ }
pub trait AudioBackend { /* the device hosting; the 2 threads; the Noop impl for tests */ }
pub trait InputBackend { /* the 2-tier model; winit+gilrs impl */ }
pub struct LoadPool; /* rayon; null-on-failure; the wait primitive */

// severedchains-game
pub struct Game; /* the GameModule impl; the 3 first-party ModDefs; the states; persistence; content */
pub struct GameState52c { /* the documented 00–4e3 layout; compile-time offset assertions (A2) */ }
```

## 8. Extensibility model (synthesis)

- **Content**: registry-driven (21 registries; one-shot windows; delegate resolution; the 1238-entry census as the first-party baseline — adding/removing content is a mod operation, never an engine change; saves store ids — A5).
- **Behavior**: event-driven (113-event-class surface; exact-class; the isolation + cancellation contract; the critical set) + the RQ7 spectrum (each gameplay behavior placed with evidence — CL-GLP-01).
- **Scripts**: the unpack-time patch pipeline (353 entries; the CRC gate; the assembly patch language) + the 9-point lifecycle events + the flag/var surface + the native table (mods register natives through the catalog slots — the 592-slot census).
- **Persistence**: versioned (11 magics loadable; V10 writer; the migrators; the mod forward-compatibility policy — survives additions, degrades on removed content, fails on removed characters/stat types — CL-SAV-04) + config presets (`.dpre`; the vanilla = 12 retail overrides; the speedrunner = 5).
- **External**: data-mod packages (TD-16); the deferred code-mod ABI (recorded, not designed); the i18n override tree.
- **Compatibility exceptions** (AGENTS.md naming policy): the exported registry ids, serialized keys/field names, the script-facing names, the binary format signatures (the 11 magics, the MRG magic, the Tag wire format), and the PSX register names (TPage/PSDCNT/…) are never renamed; internal Rust names differ freely through explicit mappings (the reverse-engineering map per state keeps the original identifier ↔ Rust field correspondence).

## 9. Naming and documentation conventions in code (AGENTS.md)

The semantic-naming policy binds all implementation work: every identifier is named for its demonstrable responsibility; no decompiler artifacts (`FUN_*`, `Struct1`, `_80001234`, `field_10`, `unk_*`) in production code; uncertain symbols get a narrowly descriptive name + documented uncertainty + a register entry where material; the original identifiers survive in (a) the per-state reverse-engineering map module, (b) source-reference comments, (c) the compatibility-exception list. The PSX address/offset convention binds all human-readable annotations (eight-digit lowercase addresses — `800c6ae8 - encounter accumulator`; zero-padded lowercase offsets — `0a - Field description`; the exceptions clause for typed bit masks and cited byte layouts). The layout assertions (A2) document the offsets as data (a build-time test), never as field names.

## 10. What this document deliberately does not decide

- The TD items (toolchain, dependencies, graphics/audio/input backends, the VM representation, the error model, the mod package format, the binary name) — `planning/02`, each **Proposed** pending approval.
- The CD items (the 10 compatibility decision dispositions — G6, R4/Q-70, T2, G4, G11, S7, X6, C-01, Q-74, Q-80) — `planning/03`, each with its evidence requirements and register pointer.
- The milestone sequence, gates, and row coverage — `planning/04`/`planning/06`.
- Any production code, crate creation, or dependency installation — requires explicit approval after P0.