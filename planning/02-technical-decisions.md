# P0-02 — Technical Decision Records (Proposed)

Status: **Proposed (P0, 2026-11-14).** Each record: the problem, the research basis, the options considered, the trade-off, the **proposal**, the compatibility consequences, confidence, and the dependency/approval state. **None of these decisions is approved** — the user approves or amends the whole set (or per-item) after P0. Versions quoted for external crates are the crates.io index state verified at P0 preflight (2026-11-14); nothing is installed or pinned yet (the workspace does not exist). Confidence: **High** = research-anchored with a single defensible choice; **Medium** = multiple defensible choices, one recommended; **Low** = placeholder pending evidence.

Toolchain note (P0 preflight, verified): rustc/cargo 1.100.0 stable (aarch64-apple-darwin); edition 2024 is supported by this toolchain; crates.io reachable. Java reference confirmed at Java 25 (`sourceCompatibility = targetCompatibility = JavaVersion.VERSION_25` in `Severed-Chains/build.gradle`) — relevant to the `java.util.Random` algorithm identity (TD-13).

## TD-01 — Rust toolchain, edition, MSRV

- **Problem.** Which Rust toolchain generation the project is built on.
- **Research basis.** L1–L12 (language is free); the naming policy assumes modern idiom; the workspace does not exist yet.
- **Options.** (a) edition 2024 / current stable; (b) edition 2021 / older MSRV for longevity.
- **Trade-off.** (a) takes the language's current best (let-else, C-unions, if-let-chains, the prelude) — the codebase is greenfield, no legacy constraint; (b) buys compatibility with older toolchains the project has no reason to target.
- **Proposal.** Edition **2024**; MSRV = the verified-installed **1.100.0 stable** as of P0; MSRV re-verified at every dependency bump; the workspace pins `edition = "2024"` per crate; `rust-version` field set to the MSRV.
- **Compatibility consequences.** None (L-freedom).
- **Confidence / status.** High. Proposed. Depends on nothing.

## Revision, 2026-11-15

The architecture revision described in `planning/01` changes two of these records, confirms one, and leaves the rest standing. It does not reopen them on new evidence about the *technology*; it corrects where the technology is *placed*.

| Record | Status after the revision | What changed |
| --- | --- | --- |
| **TD-02** workspace shape | **revised** | "no 8th crate" was a crate budget, and a crate budget is not a dependency rule. Replaced below by a responsibility test. The engine / SDK / game / executable crates survive. |
| **TD-03** `psxrt` = the mechanical platform primitives | **confirmed unchanged** | TD-03 already proposed one crate holding `gpu`, `gte`, `spu`, `xa`, `bpp`, `blend`, `fixedpoint`, `rng`, `timing` as modules. The revision restores that position; the first draft's separate-crate reading was the departure from TD-03, not its continuation. Only a placement detail moves: `xa` folds under `spu`, `bpp`/`clut` under `gpu`, `fixedpoint` under `math`, and `unsafe_code = forbid` still holds workspace-wide. |
| **TD-22** GTE precision | **revised, unchanged in substance** | the three precisions are compile-time configuration inside `psxrt::gte`. The mandatory integer submap camera program stays, and it is built in `psxrt`, not only exposed as a knob in `psxrt-compat` (CD-03). |
| TD-01, TD-04 … TD-21, TD-23, TD-24 | **unchanged** | where a record names `sc-gpu`/`sc-gte`/`sc-spu` as crates, or a `psx_`-prefixed module path, read it against `planning/01` §4–§6. The decision itself is not withdrawn. |

Two rules the 2026-11-14 set did not have, added because the I0 code showed the need:

- **TD-25 — no dependency on reference source at build or test time.** The reference repositories are inputs to *understanding*, never to the build. Findings are transcribed into a `PSX-Behavior Map` header with file, revision, commit, function and line; nothing resolves a sibling checkout path, and no `make check` step shells out to a generator whose output is not committed. I0 shipped compliant, so this records and generalizes an existing property rather than changing one.
- **TD-26 — a compatibility policy module may not double as a data-format header.** I0's `psxrt-compat/src/predictor.rs` held five XA format constants beside a policy enum, and `rate_00.rs` held a flag-word decoder beside a policy note. Both are relocated per `planning/03` §5: a policy selects, a representation represents, and one file doing both cannot be tested for either.

## TD-02 — Workspace shape: enumerated crates, each justified by a responsibility

- **Problem.** The crate layout; whether test-harness/tooling code gets its own crates.
- **Research basis.** The six fixed names (user decision); A6 (the import pipeline), A7 (saves), A5 (modding) shape the splits; mission constraint: avoid artificial scaffolding milestones.
- **Options.** (a) exactly the six names fixed at P0 and nothing more, ever; (b) those six plus a binary crate, with the count frozen at seven; (c) a per-responsibility test — a crate exists when it has a stated responsibility and stated dependency edges, and the module-vs-crate question is answered per subsystem.
- **Trade-off.** (a) and (b) are the same error wearing different clothes: they let the *number* of crates answer a question only a dependency graph can answer. A frozen count silently forbids the crate a real problem later needs **and** silently permits the empty scaffolding crate it should forbid — a count is satisfied by either. (c) costs one sentence of justification per crate, which is the price of the check that actually catches a bad boundary. The (a)/(b) family also had a concrete failure mode: the binary-inside-a-library variant creates the runtime→game edge and a cycle.
- **Proposal.** **(c)**, applied as: the `psxrt` family is three crates (`psxrt`, `psxrt-import`, `psxrt-compat`); the engine, SDK, game and executable crates of the 2026-11-14 set remain in scope; in-crate `tests/` + `#[cfg(test)]` for test code; golden fixtures under per-crate `testdata/` (planning/05 §9); a dependency-lint test in the binary crate. Per-hardware-unit crate splitting (`sc-gpu`/`sc-gte`/`sc-spu`, and the `severedchains-sdk` + four-PSX-crates variant) is retired: GPU, GTE and SPU are internal modules of `psxrt`. **Adding a crate later requires naming its responsibility and its dependency edges; it does not require reopening this record.**
- **Why the count wording is withdrawn rather than kept.** "6 crates, no 8th" and the revision's first draft ("three crates only") were both in the tree at once, and they contradicted each other: `planning/04` I5b placed the wgpu and cpal backends in the engine crate while `planning/04` §1 rule 1 said only three crates exist. Recorded here so the contradiction is visible if the count phrasing reappears.
- **Confidence / status.** High. Revised 2026-11-15, pending user approval.

## TD-03 — `psxrt`: the mechanical PSX platform primitives

- **Problem.** What belongs in the reusable PSX platform crate (fixed name).
- **Research basis.** M4 (psx/01–04): the GPU/VRAM/ordering models, the GTE register-model subset (T1), the SPU voice model (S4–S6, S8), the XA codec (X2/X3), the Bpp codec (G2), the blend equations (G10), the fixed-point helpers (R1/R2), the 1/60 s game-time unit (CL-ENG-02), the RNG primitives (R3/R4/CL-GLP-02 incl. the P0-verified `java.util.Random`-family 48-bit LCG identity).
- **Options.** (a) Models + primitives only (no policy); (b) models + embedded policy defaults; (c) a single PSX crate including compatibility policy.
- **Trade-off.** (b)/(c) mix "what the PSX is" with "which documented behavior we run" — the M4-05 four-class framework (HA/BE/FO/UR) shows these are distinct concerns: the *mechanisms* (G5/G7/S5/X2) are reusable; the *selections* (G6 LIFO/FIFO, T2 float/int, X6 reset/no-reset) are policy with evidence and knobs. (a) keeps `psxrt` platform-general and `psxrt-compat` the evidence-bearing policy layer.
- **Proposal.** **(a)** per planning/01 §3.1: `gpu`, `gte` (float + integer variants), `spu`, `xa`, `bpp`, `blend`, `fixedpoint`, `rng` (LCG core, LCG16/LCG15 parameterized, the `java.util.Random`-family LCG with bounded-draw rejection), `timing`. Pure, thread-agnostic, no I/O.
- **Compatibility consequences.** Primary owner of G1–G2, G5 (mechanism), G7, G9, G14–G15, T1, T3, T5, T6, T7, S4–S6, S8, X2–X3, R1–R2 (planning/06).
- **Confidence / status.** High. Proposed.

## TD-04 — `psxrt-compat`: the policy layer and divergence documentation

- **Problem.** Where the documented PSX-behavior selections and divergences live (fixed name).
- **Research basis.** The M4-05 classification (43 rows; 6 UR: G4/G6/G11/S7/X6/R4-seed); cross/03 §8 (3 design decisions, 2 external-doc, 5 asset/observation); the A13 bug-preservation default.
- **Options.** (a) Policy as config scattered per-consumer; (b) a dedicated policy crate with one module per policy family, each carrying evidence + default + knob + divergence note; (c) fold policy into the game crate.
- **Trade-off.** (a) scatters the divergence documentation (the mission requires it visible); (c) loses the "reusable PSX compatibility" role the name implies and mixes LoD content with PSX policy; (b) centralizes the evidence.
- **Proposal.** **(b)** per planning/01 §3.2: one module per policy (same-z, GTE precision, display clamp, rate-00, predictor continuity, sweep, voice allocation, blend selection), each with: the row + confidence, the selected **default = the reference's behavior** (A13: preserve and document; divergences only where CD-* says so), the knob (for G6/T2/X6/S7 per CD-01/03/06/07), and the closure evidence (register pointer). The T4 integer-precision submap camera is **mandatory** (the only Hardware-accurate row) — it is a model variant in `psxrt`, invoked by the game's submap camera.
- **Compatibility consequences.** Primary owner of G3, G4, G6, G8, G10, G11, G12, G13 (presentation policy), T2, T4, S2, S3, S7, X1, X4, X6, X7 (planning/06).
- **Confidence / status.** High. Proposed. Depends on TD-03.

## TD-05 — `psxrt-import`: the unpack pipeline and format codecs

- **Problem.** The asset-conversion crate's scope (fixed name); where the script-patch pipeline sits.
- **Research basis.** M2 (assets/01–10: CL-AST-01..12), the transform registry + `files/` cache (A6), the script-patch semantics (CL-SCR-10/CL-MOD-08), the XA→Opus transcode (CL-AST-10), MDEC (CL-AST-09).
- **Options.** (a) Import crate owns everything unpack-time including the Fate patch walk; (b) import owns the pipeline + formats; the Fate toolchain (SDK, user-fixed) is injected through a transform trait; (c) the patch pipeline lives in the game.
- **Trade-off.** (a) inverts the user's SDK role assignment (Fate toolchain = SDK) and forces an import→sdk edge; (c) puts the observable asset-contract machinery (A6) in content; (b) keeps A6 where it belongs with a clean edge set.
- **Proposal.** **(b)** per planning/01 §3.3: `iso`, `mrg`, `bpe`, `deff`, `tmd`, `tim`, `anim`, `mdec`, `sssq`, `xa`, `cache`, `pipeline`, and a `patch` module that owns the *orchestration* (the 353-entry walk, CRC32 gate, backup/restore, idempotency) while calling an injected `ScriptPatchToolchain` trait implemented by `severedchains-sdk`. Dependencies: `psxrt` only.
- **Compatibility consequences.** Primary owner of CL-AST-01..12, X1 (extraction), X4/X7 (transcode side), the CL-SCR-10/CL-MOD-08 *results* contract (planning/06).
- **Confidence / status.** High. Proposed. Depends on TD-03.

## TD-06 — `severedchains`: the core engine runtime

- **Problem.** The engine crate's scope (fixed name).
- **Research basis.** M1 (platform/01–05), M5 (rendering/01–04), M6 (audio/01–03), cross/01 (the 14-step boot chain, the 21-phase frame contract, the 8-thread/10-domain model, the 10 hazards, the 9 invariants), CL-ENG-01..13.
- **Options.** (a) Engine = frame + states + all services (including content); (b) engine = frame + services + state *machine*, content in the game crate; (c) engine = frame only.
- **Trade-off.** (a) violates A5 (content must be registry content, not engine code) and mixes layers; (c) leaves the state machine (the load-bearing CL-ENG-04/05 contract) without a home; (b) matches the research split exactly (mechanism in the engine; configured behavior in content — the M8-03 engine/content boundary).
- **Proposal.** **(b)** per planning/01 §3.5: `builder`, `frame`, `state` (machine only), `input`, `render`, `audio`, `load`, `save` (ordering + campaign model), `error`. Backends behind traits with Noop implementations (planning/01 §6.10).
- **Compatibility consequences.** Primary owner of CL-ENG-01..13, CL-AUD-01/07/08/09/10 (hosting), CL-AST-11 (hosting), the G-row presentation obligations (planning/06).
- **Confidence / status.** High. Proposed. Depends on TD-03/04/05/07.

## TD-07 — `severedchains-sdk`: registries, events, Fate, i18n, mod loading, Tag, config framework

- **Problem.** The SDK crate's scope (fixed name + user-fixed role).
- **Research basis.** M8 (modding/01–03: the 21 registries, the 113-event-class/10-family contract, the 1238-entry census, the i18n 4-level model, the mod lifecycle), M3 (scripting/01–06: the VM contract + toolchain + C-01), M7 (saves/02: the config framework; saves/01: the Tag model), planning/01 §4 (the cycle-prevention argument — the SDK must be dependency-free within the workspace).
- **Options.** (a) The user-fixed role exactly; (b) that role + the Tag/config framework (moved from the game) + the VM (moved from a game-repo location, per the reference's `legend.game.scripting`); (c) the VM in the runtime instead.
- **Trade-off.** (c) would force the runtime to expose VM internals to game native functions and blur the modding boundary (the debugger's live-stepping precedent shows the toolchain standing alone); the Tag model and config framework are *framework* (mods register config entries; presets are mod-defined; `modData` is a Tag) — the M7/M8 documents treat them as shared infrastructure. (b) makes the SDK the single source of the cross-layer contracts (planning/01 §4's load-bearing property).
- **Proposal.** **(b)** per planning/01 §3.4: `registry`, `event`, `i18n`, `mod`, `fate` (`vm`, `toolchain`, `binary`, `meta`), `tag`, `config`. No project-crate dependencies.
- **Compatibility consequences.** Primary owner of CL-SCR-01..13, CL-MOD-01..07 (framework), CL-SAV-10..15 (framework) (planning/06).
- **Confidence / status.** High. Proposed.

## TD-08 — `severedchains-game`: content, states, persistence, first-party mods

- **Problem.** The game crate's scope (fixed name + user-fixed role).
- **Research basis.** M9 (game/01–07: the per-system contracts + retail-vs-reference tables), M7 (the save readers/writer), cross/01 §4 (the state inventory), A5 (content is registry content), A11 (the Dabas island).
- **Options.** (a) One game crate; (b) split into `severedchains-content` (data) + `severedchains-game` (logic).
- **Trade-off.** (b) would split the 1238-entry census across crates and the reverse-engineering map from its state code — the M9 documents treat content and rules as one surface (the "data is the table" boundary); the census coherence is a verification asset (planning/05 §5).
- **Proposal.** **(a)** per planning/01 §3.6: `module`, `state/*`, `state_data` (the `GameState52c` layout + globals + the reverse-engineering map), `persistence`, `content`, `characters`, `inventory`, `shops`, `progression`, `audio`, `rng`.
- **Compatibility consequences.** Primary owner of CL-GLP-01..17, CL-SAV-01..09, CL-AUD-02..06/10 (content), the CL-ENG-02 write sites, the R3/R4 consumer sides, the CL-MOD-10 content (planning/06).
- **Confidence / status.** High. Proposed. Depends on TD-06/07.

## TD-09 — The binary crate: name, features, CLI

- **Problem.** The process entry's identity and what is feature-gated (not a fixed name).
- **Research basis.** CL-ENG-08/09 (the boot chain and shutdown order live here), L8/L12 (debugger/updater/Discord are tooling, no game-behavior dependency), cross/01 §9 check 3 (the 25-file JavaFX boundary).
- **Options.** Name: (a) `scus94491` (the disc id — semantic: the executable *is* the SCUS94491 experience); (b) `severedchains-app`; (c) `severedchains` shadowing the lib name. Features: `dev-tools` (debugger, frame-advance, the dev seed override — the CD-02 hook), `updater`, `discord` (all off by default).
- **Trade-off.** (c) is confusing (lib/bin name clash); (b) is generic; (a) is semantic and collision-free. The dev features must be off by default: the release build is the game, and the reference's tooling is provably non-load-bearing (L8/L12).
- **Proposal.** Name **(a) `scus94491`**; features per above; the CLI (TD-21) parses asset/mod/config/save locations + dev flags.
- **Compatibility consequences.** None (process shape is L-free; the *order* of boot steps is the CL-ENG-08 contract).
- **Confidence / status.** Medium (name is a matter of taste). Proposed.

## TD-10 — Graphics: wgpu + winit, OT-replay presentation, Noop backend

- **Problem.** The rendering backend; how the PSX-compatible presentation is realized on modern GPUs.
- **Research basis.** rendering/01 (the three presentation modes; the OT queue; the pools and the LIFO/FIFO equal-depth tie-breaks; the FBO/presentation targets; the Noop API), M4-01 (the ordering table; the Bpp sampling in the vertex stage; the blend decomposition), G13 (the one-frame latency), CL-ENG-06 (LEGACY H-split for SUBMAP/TITLE only).
- **Options.** (a) wgpu (pure Rust, cross-platform, WGSL via naga) + winit; (b) miniquad; (c) a bespoke GL loader; (d) software renderer first.
- **Trade-off.** (d) abandons the fidelity ambition and the reference's GPU-model replay; (c) reintroduces the C-ABI fragility the rewrite is escaping; (b) is thin but limits compute/multi-target needs; (a) is the standard modern pure-Rust stack, supports the multi-target FBO structure (rendering/01), and its `naga` path compiles WGSL for all backends.
- **Proposal.** **(a) wgpu 30.0.2 + winit 0.30.13 + naga 30.0.2** (verified 2026-11-14; `winit` 0.31.0-beta.4 and `smallvec` 2.0 exist but are pre-release — not tracked). Presentation: mode 0 = **OT replay** (the engine walks the ordering table z-descending at `GPU.endFrame` and issues draws in command order — engine logic over wgpu command buffers, not a GPU feature); modes 1/2 = the modern 2D/3D pools with the documented tie-breaks (layer 4); the LEGACY projection is vertex-stage math; the display swap + one-frame latency (G13) is the double-buffer present; the **Noop backend** implements the same trait for headless tests (planning/05 §4). Shaders in WGSL (the 6 reference shader families re-derived, not copied).
- **Compatibility consequences.** G1–G15 presentation obligations, G13, CL-ENG-06/11/13 (planning/06). Visual parity vs the original remains **Blocked** (cross/03 §9 — no baseline); the OT-replay path is the reference-fidelity path by construction.
- **Confidence / status.** Medium (backend choice is conventional but the OT-replay realization is a genuine design surface). Proposed. Depends on TD-02/06.

## TD-11 — Audio: cpal hosting, two real-time threads, Opus via libopus

- **Problem.** The audio device hosting; the Opus decode/encode path (the reference's Opus path is layer 4 — CL-AST-10/CL-AUD-08).
- **Research basis.** M6 (audio/01–03: the three pipelines, the clock domains, the `XaPlayer` contract, the transcode spec), psx/03 (the SPU model), CL-AUD-01..10.
- **Options.** (a) cpal (the standard pure-Rust audio device API) + `opus` (libopus bindings) for decode **and** the import-time encode; (b) rodio (a higher-level mixer) as the host; (c) a bespoke ALSA/CoreAudio backend.
- **Trade-off.** (b) hides the sample-accurate per-sample scheduling the BGM mirror needs (CL-AUD-02) and the dual-thread structure (CL-AUD-01); (c) duplicates a solved problem; (a) gives exact sample-buffer control on both threads with the documented contract (120 Hz min-pace music, 60 tps SFX). The `opus` crate (libopus-sys) is the same codec family the reference consumes (`.opus` files) — the transcode spec (80/63 Catmull-Rom 37800→48000, 128 kbps, pre-skip 312, the original-rate-37800 header) is reproduced in the import pipeline; the Ogg demux (960-sample frames, pre-skip) is a small custom parser per the documented container (audio/03).
- **Proposal.** **(a) cpal 0.18.2 + opus (version verified at implementation start — the P0 index snapshot did not pin it; provisional)**. The music thread runs the per-sample BGM renderer (the behavioral mirror — **not** claimed sample-identical to retail, CL-AUD-07); the SFX thread runs the 60 tps SPU tick + the 8.0 ms SFX sequencer; the `XaPlayer` contract is the runtime audio service; the device-watch reinit (2 s) is preserved.
- **Compatibility consequences.** CL-AUD-01/07/08 (hosting), S1–S8 (the model is psxrt; the protocol is psxrt-compat/runtime) (planning/06).
- **Confidence / status.** Medium (cpal is conventional; the per-sample mirror is a large pure-Rust surface — the biggest audio risk). Proposed. Depends on TD-06.

## TD-12 — Input: winit + gilrs, the two-tier model

- **Problem.** The input backend; the action/activation model.
- **Research basis.** CL-ENG-07 (the two-tier model, per-state action sets, deadzones, rebinds, the rumble lerp, the script bridge), platform/05 (the SDL3 hosting — L-free mechanism), the 70-action census (41 mod-registered — CL-ENG-07).
- **Options.** (a) winit (keyboard/mouse) + gilrs (gamepad, libretro-gamepad API); (b) SDL2/3 bindings (the reference's mechanism).
- **Trade-off.** (b) preserves the mechanism unnecessarily (L-free) and pulls the C-ABI; (a) is pure Rust and covers keyboard/mouse/gamepad with per-device activation tracking. The *observable* contract (the action model, the one-frame edges, the per-state sets) is language-free.
- **Proposal.** **(a) winit 0.30.13 (shared with TD-10) + gilrs 0.11.2**. The two-tier model is a runtime service: physical activations → semantic actions (registry content, 70 actions) with deadzone/rebind persistence; the frame driver consumes the action edges (the one-frame edge contract — CL-ENG-01).
- **Compatibility consequences.** CL-ENG-07 (planning/06).
- **Confidence / status.** High. Proposed.

## TD-13 — Fate VM + toolchain: a direct port of the documented contract

- **Problem.** The VM representation; the toolchain scope; the C-01 choice; the RNG injection.
- **Research basis.** M3 (scripting/01–06; the m3-xhigh audit — the 2 High findings remediated; the round-trip hazard set), CL-SCR-01..13, R5 (the op bodies), the P0-verified battle-RNG identity (the `java.util.Random`-family 48-bit LCG — not Mersenne Twister; the mix32 override dead), Java 25 (the reference's `java.util.Random` = the standard 48-bit LCG, multiplier 0x5DEECE66D, addend 0xB; `setSeed` masks to 48 bits; bounded `nextInt(bound)` = the rejection sampler; `nextFloat(bound)` per Java 17+ semantics).
- **Options.** (a) Port the VM 1:1 against the documented contract (108 fixed slots as a `[[u64; 63]]`-style storage, the dual int/f32 representation per slot + mode bit, `FlowControl` enum, the per-frame allocation upper-bound reset, the destructor gate, `framesPerTick` pacing, the 1024-entry table as `[Option<NativeFn>; 1024]` with per-state override arrays); (b) a "cleaner" VM (dynamic slots, typed storage) that approximates the contract.
- **Trade-off.** (b) violates A4 (the VM contract is on the critical path) and the CL-SCR-05/06/08 rows *are* the observed behavior (slot reservation, the dual storage, the pacing reset) — "cleaner" would be a compatibility break; (a) is mechanical and fully testable. The toolchain (decompiler/translator/assembler/patcher/meta) is reimplemented from the documented behavior (M3-03 is the spec); the round-trip fidelity gate (CL-SCR-04) with the documented hazard set (0x24–0x27, the CMP-header 8/9, the one-way types) is the acceptance test, not a nice-to-have.
- **Proposal.** **(a)**: the VM per the contract; the toolchain per M3-03; the **C-01 choice = CD-08** (VM executes the runtime table semantics; the decompiler uses the recompiler table semantics; the discrepancy preserved per-opcode, documented in the SDK `fate::binary` module); the `RAND` op body consumes the **injected** gameplay stream (`host.rng_draw()` → `X × draw() >>> 16` — R5); the battle RNG (48-bit LCG with bounded draws) and the turn-order/snow instances are `psxrt::rng` types owned by the game (CD-02). The VM is single-threaded (main thread) — the stream-order invariant (A3).
- **Compatibility consequences.** CL-SCR-01..13, R5 (planning/06). Q-17 (per-op parity with the original MIPS VM) stays open — the layer-4 additions are not presented as original (CL-SCR-13).
- **Confidence / status.** High. Proposed. Depends on TD-07.

## TD-14 — Registry + event design: TypeId exact-class dispatch, Result isolation

- **Problem.** The dispatch mechanism; the error isolation; the cancellation semantics.
- **Research basis.** CL-MOD-04/05/06 (exact-class dispatch — no supertype fan-out; 5 priority levels, stable order; per-handler isolation with the 7 s toast; the critical set = exactly 21 Register + `AddRegistryEvent` + `RegisterAtlasTexturesEvent`; cancellation enforced for exactly 7 take/give/clone behaviors + the unenforced Pre*/AdditionUnlock documented; `stopPropagation` unused; `RenderEvent` a reused static instance — handlers side-effect-only).
- **Options.** (a) TypeId-keyed exact-class tables (handlers registered per concrete event type); (b) a trait-object vtable dispatch with `dyn Event`; (c) a macro-generated hierarchical dispatch.
- **Trade-off.** (c) changes observable behavior (fan-out — the D10 contract); (b) is equivalent to (a) but adds a layer without benefit; (a) is the direct realization of the contract and the fastest path. Isolation: Rust handlers return `Result<(), HandlerError>` — no `catch_unwind` needed for *handler* errors (the L4 mapping of the per-handler `Throwable` catch); the critical set propagates to the crash path (planning/01 §5.7). Cancellation: the 7 enforced behaviors are enforced; the documented-unenforced cancellations are **honored** (CL-MOD-06: "honoring all of them is the faithful choice").
- **Proposal.** **(a)** + Result isolation + full cancellation honoring, per planning/01 §3.4.
- **Compatibility consequences.** CL-MOD-04/05/06 (planning/06).
- **Confidence / status.** High. Proposed.

## TD-15 — Serialization: hand-rolled Tag/save/config codecs; serde for manifests only

- **Problem.** Where serde fits.
- **Research basis.** CL-SAV-01..15 (the Tag wire encodings — LEB127, varint+UTF-8, u24+ASCII RegistryId, the MapTag nondeterministic order; the `.dcnf`/`.dpre` byte layouts; the 11 magics; the PNG atlas), the mod package format (new — TD-16).
- **Options.** (a) serde everywhere (derive the save formats); (b) hand-rolled codecs for the documented byte formats; serde only for new dev-time/mod-package JSON surfaces.
- **Trade-off.** (a) is impossible for the compatibility floor: the Tag/`.dcnf`/save wire formats are *documented byte layouts* (A7) — a serde derive would not produce them (LEB127, u24+ASCII, the defaults-written `.dcnf`, the 100 KiB buffer) and would couple the format to the type tree; (b) keeps the byte formats as explicit, testable codecs (planning/05 §5 round-trip suites).
- **Proposal.** **(b)**: the SDK `tag` module (the 10-type codec), the SDK `config` module (`.dcnf`/`.dpre`), the game `persistence` module (the 11 readers + V10 writer + migrators); the `png` crate (or `image` with default-features off) for the V10 512×512 atlas (version verified at implementation start — provisional); serde/serde_json only for the mod package manifest (TD-16) and dev tooling.
- **Compatibility consequences.** CL-SAV-01..15 (planning/06). Byte-stability is **not** a goal (CL-SAV-09).
- **Confidence / status.** High. Proposed.

## TD-16 — Modding: the data-mod package format; code mods deferred

- **Problem.** The external mod surface in Rust (the reference's mods are same-process jars — L2; the *semantics* are the contract).
- **Research basis.** CL-MOD-01/02/08 (discovery/semver/required/failure; the 12-step boot; the unpack-time patch surface), modding/03 (the `./patches/` layout, the `replacements/` model, the i18n override tree, the 1238-entry census), the A5 obligation (saves store registry ids).
- **Options.** (a) Data mods = drop-in packages (`./mods/<id>/`: manifest + `lang/` + assets + script patches), applied through the existing pipelines; code mods = statically linked first-party only; the dynamic ABI deferred (recorded); (b) attempt a dynamic code-mod ABI (dlopen/OSL) now; (c) no external mods at initial release.
- **Trade-off.** (b) has no reference contract to preserve (the reference has no ABI — mods are same-process classes) and would burn a release on ABI stability with zero observable benefit; (c) abandons A5 for external content (the ecosystem expects registry-extensible content — the M8 documents); (a) delivers the observable contract (the *semantics* of discovery/version-gate/failure/persistence) with the data surface that the pipelines already realize.
- **Proposal.** **(a)** per planning/01 §6.7: the package manifest (id, version — NPM-semver gate over the game version, required flag, the `lod`/`lod_core` required pair) via TD-15's JSON; data mods contribute registry content (entries resolved at `bootMods`), i18n (the override tree), assets (the `replacements/` pipeline), and script patches (the CRC-gated pipeline); the per-campaign `enabled_mods`/`known_mods` codec is preserved (u16 count + NUL-terminated ASCII, the `lod-core`→`lod_core` read alias); failure = recorded + toast; missing required = hard failure (crash log + exit, no UI — CL-MOD-01). The code-mod ABI is a **deferred decision** (planning/03 has no CD for it — it is out of the compatibility scope; recorded in planning/01 §6.7 and the final report).
- **Compatibility consequences.** CL-MOD-01/02/08 (semantics) (planning/06).
- **Confidence / status.** Medium (the package layout is new — Proposed, not reference-derived). Proposed.

## TD-17 — Concurrency: std threads + rayon; no async runtime

- **Problem.** The threading model.
- **Research basis.** A3 (single frame owner), cross/01 §5 (the 8-thread inventory, the requirement classes), L3 (the async mechanism is free; the null-on-failure contract is the contract), CL-AST-11.
- **Options.** (a) std::thread for the named threads (music, sfx, unpack) + a rayon pool for I/O/transforms; (b) an async runtime (tokio) for everything; (c) green threads.
- **Trade-off.** (b) adds a scheduler to the real-time audio path (latency nondeterminism — the opposite of the documented model) and makes the frame loop a task (the single-owner invariant becomes a convention); (a) maps 1:1 onto the reference's requirement classes with the narrow locked surfaces of planning/01 §5.3; (c) is unnecessary.
- **Proposal.** **(a) std threads + rayon 1.12.0** (the pool is fixed-size, I/O-bound). SPSC queues for the audio hand (game → threads: queued sounds, control words; threads → game: none — the reference's model is one-way from the game's perspective). No `async`/`tokio` anywhere.
- **Compatibility consequences.** The thread inventory's requirement classes (cross/01 §5.1) — not directly row-mapped (L-free), but the audio-decoupling contract (CL-ENG-10) depends on it.
- **Confidence / status.** High. Proposed.

## TD-18 — Error handling: thiserror + anyhow + the frame-boundary catch

- **Problem.** The error model mapping (L4).
- **Research basis.** CL-ENG-12 (the crash-recovery path), CL-MOD-05 (the critical set), the J-tables (the error *classes* are contract; the throw mechanics are implementation), the L4 row.
- **Options.** (a) `thiserror` 2.x domain errors + `anyhow` 1.x at the binary boundary + `catch_unwind` at the frame boundary for the crash-recovery path; (b) no catch — panics abort; (c) a custom unwind-free error protocol throughout.
- **Trade-off.** (b) loses the observable recovery surface (a "Crash Recovery N" save at the last savable state — CL-ENG-12); (c) is a large surface for little gain over (a); (a) maps the four documented error classes (planning/01 §5.7) directly and keeps debug builds honest (`RUST_BACKTRACE` + the catch still runs in debug — **Proposed**: the catch is active in all builds; debug builds additionally print the backtrace).
- **Proposal.** **(a)** with the four-class model of planning/01 §5.7; the crash log + mod census + `CRASHLOG-<ts>.log` copy are preserved (the observable surface).
- **Compatibility consequences.** CL-ENG-12, CL-MOD-05 (planning/06).
- **Confidence / status.** High. Proposed.

## TD-19 — Logging and diagnostics: tracing + the CRASHLOG analog

- **Problem.** The diagnostics surface.
- **Research basis.** The reference's crash-log + mod census (CL-ENG-12), the 7 s toasts (CL-MOD-05), the in-source debug logs (the SFX debug-log formula — log-only, CL-AUD-04's neighbor), the mod-failure boot toasts.
- **Options.** (a) `tracing` 0.1.44 with a file layer (rolling, the `CRASHLOG-<ts>.log` copy on crash) + the overlay-toast surface for user-visible mod errors; (b) println!; (c) a bespoke logger.
- **Trade-off.** (b)/(c) are not worth the surface; (a) is the standard, structured, and cheap.
- **Proposal.** **(a)**: `tracing` throughout; the crash path writes the structured crash record (backtrace, mod census — the active mod set + versions, the campaign id); user-visible errors go to the overlay toast (7 s, the mod named per CL-MOD-05).
- **Compatibility consequences.** Part of CL-ENG-12/CL-MOD-05 observable surfaces (planning/06).
- **Confidence / status.** High. Proposed.

## TD-20 — Test infrastructure: headless harness, golden fixtures, property tests

- **Problem.** The first-milestone test infrastructure (planning/05 §9 names it; this TD fixes the *where*).
- **Research basis.** cross/03 §10 (the pre-implementation verification steps), planning/05 (the strategy), the Noop backends (planning/01 §6.10), the determinism requirements (A3, CL-GLP-17).
- **Options.** (a) In-crate tests + per-crate `testdata/` golden directories + the binary crate's `tests/` for cross-crate headless integration; (b) a separate test-kit crate.
- **Trade-off.** (b) is scaffolding (TD-02); (a) keeps every test next to the code it tests, with the cross-crate harness in the only crate that sees all crates (the bin).
- **Proposal.** **(a)**: the Noop render/audio backends live in `severedchains` (`#[cfg(any(test, feature = "headless"))]`); the headless frame-driver harness (scripted input, N frames, state-transition assertions) is a `severedchains` test module reused by the bin's integration tests; golden vectors (RNG streams, GTE worked examples, SPU tick traces, the battle replay) under per-crate `testdata/` with a manifest recording the derivation (source-derived, never asset-validated — planning/05 §1); proptest 1.12.0 for the property suites (Bpp round-trip, OT invariants, the Fate round-trip, the Tag codec); criterion 0.8.2 for the audio/VM hot paths (optional, off by default).
- **Compatibility consequences.** Enables the planning/06 verification-method column; no direct row.
- **Confidence / status.** High. Proposed.

## TD-21 — CLI: clap

- **Problem.** The command surface.
- **Research basis.** The reference's launch model (args: none meaningful — the jar manifest + the working directory: `./mods`, `./patches`, `files/`, `saves/`, `config.dcnf`, `gfx/` — all working-directory-relative; the `config.conf`/`.dcnf` stores), L-free mechanism.
- **Options.** (a) clap 4.6.7 with working-directory defaults + explicit overrides (`--assets`, `--mods`, `--config`, `--saves`, `--data`); (b) env-only; (c) no CLI.
- **Trade-off.** (c) breaks the test harness (the headless harness needs to point at fixture trees); (b) is invisible; (a) preserves the reference's working-directory model as the default (the observable layout — A6/CL-SAV-07) while giving the harness and users control.
- **Proposal.** **(a)** with defaults: assets = `--assets` (the unpacked tree or the disc location), `./mods`, `./patches`, `files/` cache, `saves/`, `config.dcnf`, the `gfx/`-equivalent resource root — all overridable; the dev flags (`--dev-tools`, `--seed <n>` (the CD-02 dev hook), `--headless`) behind the dev feature.
- **Compatibility consequences.** The on-disk layout contract (A6/CL-SAV-07) as defaults (planning/06).
- **Confidence / status.** High. Proposed.

## TD-22 — GTE precision: float default + mandatory integer submap camera

- **Problem.** The T2/T4 disposition at the architecture level (the compatibility decision is CD-03; this TD fixes the *implementation shape*).
- **Research basis.** T1 (the register-model subset), T2 (the per-op rounding; the bounded divergence; the animation-matrix quantization accumulation), T4 (the integer-precision submap camera — the only HA row; the in-source comment: integer math "so that sobj and collision placements match retail"), Q-67 (the per-camera-path integer-math audit is the precedent to follow).
- **Options.** (a) `psxrt::gte` exposes the register model with a precision parameter (float / integer); the submap camera calls the integer variant; the audit of other camera paths (Q-67) is a pre-I5 task; (b) two parallel GTE implementations.
- **Trade-off.** (b) duplicates the register model; (a) keeps one model with the precision as a policy (psxrt-compat T2) + the mandatory T4 invocation.
- **Proposal.** **(a)**. The NCCS lighting (T7), the projection plane (T6: 1000 → 640 by `gameInit`), the screen-edge discard (T3) all run in both precisions.
- **Compatibility consequences.** T1–T7 (planning/06).
- **Confidence / status.** High (the shape) / the precision *default* is CD-03. Proposed.

## TD-23 — On-disk layout: mirror the reference's working-directory model

- **Problem.** The persistence/asset on-disk contract.
- **Research basis.** A6 (the unpacked tree is the observable asset contract), CL-SAV-07 (the campaign directory model: `saves/<dir>/`, `*.dsav`, `campaign_config.dcnf`; the memcard model), CL-AST-03 (the `files/` cache VERSION=5), modding/03 (the `./patches/` layout, the `lang/` override tree), saves/02 (the three config locations: GLOBAL `config.dcnf`, CAMPAIGN, SAVE tail).
- **Options.** (a) Mirror the layout exactly (default working-directory model, TD-21); (b) a new layout with a migration.
- **Trade-off.** (b) breaks the observable ecosystem layout (saves/patches/lang are user-visible and mod-visible — A5) with no benefit; (a) is the contract.
- **Proposal.** **(a)**: the exact reference layout as the default; every path overridable (TD-21); the `files/` cache VERSION=5 kept (a cache, but its version semantics are documented — CL-AST-03).
- **Compatibility consequences.** CL-SAV-07, CL-AST-03, the modding layout rows (planning/06).
- **Confidence / status.** High. Proposed.

## TD-24 — Dependencies: version policy, pinning, provenance

- **Problem.** The dependency surface and its governance.
- **Research basis.** None (L-free); the P0 preflight index snapshot (2026-11-14): wgpu 30.0.2 / naga 30.0.2 / wgpu-types 30.0.2, winit 0.30.13 (0.31.0-beta.4 pre-release — not tracked), cpal 0.18.2, rodio 0.23.0 (not used — TD-11), symphonia 0.6.1 (not used — TD-11), image 0.25.10, bytemuck 1.25.2, bitflags 2.13.2, thiserror 2.0.21 (1.0.69 available for v1), anyhow 1.0.104, serde 1.0.229, tracing 0.1.44, smallvec 1.13.2 (2.0.0-beta.5 pre-release — not tracked), crossbeam 0.8.5, rayon 1.12.0, proptest 1.12.0, criterion 0.8.2, clap 4.6.7, flate2 1.1.11, gilrs 0.11.2, miniz_oxide 0.9.1; `opus`/`png` versions **not pinned at P0** (verified at implementation start — provisional).
- **Options.** (a) Minimal set (only what the TDs name): wgpu, winit, naga, cpal, opus, gilrs, rayon, thiserror, anyhow, serde, serde_json, tracing, proptest, criterion, clap, png (or image), bitflags, bytemuck, smallvec, crossbeam; (b) a broad set for convenience.
- **Trade-off.** (b) inflates the supply chain for a project whose compatibility surface is byte formats (each crate's bytes are irrelevant; each crate's *vulnerability* is not); (a) keeps every dependency accountable to a TD.
- **Proposal.** **(a)** — the minimal set; `Cargo.lock` committed from the first build; every version recorded in this file with its verification date; MSRV re-verified at every bump; a dependency-lint test (TD-02) asserts no crate imports an undeclared external dependency. Provisional items: `opus` (decode + encode), `png` (atlas), and the `thiserror` major (v2 proposed) — verified at implementation start.
- **Compatibility consequences.** None (L-free); the supply-chain risk is the reason for (a).
- **Confidence / status.** High. Proposed.

## Decision summary table

| ID | Decision (one line) | Confidence | Blocks |
|---|---|---|---|
| TD-01 | Edition 2024; MSRV 1.100.0 | High | I0 |
| TD-02 | enumerated crates, each justified by a responsibility; no per-hardware-unit splitting | High | I0 → revised 2026-11-15 |
| TD-03 | `psxrt` = mechanical PSX primitives | High | I0 |
| TD-04 | `psxrt-compat` = policy + divergence docs | High | I0 |
| TD-05 | `psxrt-import` = pipeline + formats; toolchain injected | High | I1 |
| TD-06 | `severedchains` = frame + services + state machine | High | I3 |
| TD-07 | `severedchains-sdk` = registries/events/Fate/i18n/mods/Tag/config | High | I2 |
| TD-08 | `severedchains-game` = content + states + persistence + 1st-party mods | High | I6 |
| TD-09 | Binary `scus94491`; dev features off by default | Medium | I3 |
| TD-10 | wgpu/winit/naga; OT-replay + modern pools; Noop backend | Medium | I5 |
| TD-11 | cpal + opus; 2 real-time threads; per-sample BGM mirror | Medium | I5 |
| TD-12 | winit + gilrs; two-tier input | High | I3 |
| TD-13 | 1:1 Fate VM port; toolchain per M3-03; C-01 = CD-08; RNG injected | High | I4 |
| TD-14 | TypeId exact-class events; Result isolation; full cancellation honoring | High | I2 |
| TD-15 | Hand-rolled Tag/save/config codecs; serde for manifests only | High | I2/I3 |
| TD-16 | Data-mod packages; code-mod ABI deferred | Medium | I10 |
| TD-17 | std threads + rayon; no async runtime | High | I1/I3 |
| TD-18 | thiserror + anyhow + frame-boundary catch (4-class model) | High | I3 |
| TD-19 | tracing + CRASHLOG analog + overlay toasts | High | I3 |
| TD-20 | In-crate tests + golden fixtures + headless harness in the bin | High | I0 |
| TD-21 | clap; reference layout as defaults | High | I3 |
| TD-22 | One GTE model, precision parameter; integer submap camera mandatory | High | I5 |
| TD-23 | Mirror the reference on-disk layout | High | I1/I3 |
| TD-24 | Minimal dependency set; Cargo.lock committed; provenance recorded | High | I0 |
