# P0-03 — Compatibility Decisions (Proposed Dispositions)

Status: **Proposed (P0, 2026-11-14).** This document dispositions the 10 decision items of `research/cross/02-compatibility-checklist.md` §10 — the rows a future implementation **cannot** close with in-repo evidence alone. For each: the observed reference behavior (source-verified), the known/missing original behavior, the options, the **proposed disposition** (a default + knob + divergence note, or an evidence requirement), the closure evidence (register pointer), what it blocks, and the owning crate.

**Status vocabulary.** Each CD has two statuses: a *research status* (the register's — `Open`/`Unresolved`/conflict — unchanged by P0; P0 is planning, not research closure) and a *design status* (**Proposed-pending-approval** — the disposition is proposed here and takes effect when the user approves P0; **Evidence-pending** — the default behavior is fixed (reference-mirror) but the item cannot be declared *closed* until its resolving evidence lands). No CD is silently closed. Per cross/03 §8: none blocks *starting* implementation; each blocks *declaring done* the subsystem it touches.

## CD-01 — G6: same-z primitive order (LIFO vs FIFO)

- **Observed reference behavior.** The GPU-model ordering table renders same-z primitives **LIFO** (insertion-reverse); the reference's modern 2D pool tie-breaks LIFO and its 3D pool FIFO at equal depth (layer 4 — Q-71). Load-bearing in the reference: `Battle.applyTextureAnimation`'s circular scroll depends on LIFO (cross/02 G6; rendering/01 §11).
- **Original behavior.** PSX FIFO (queue order) is the layer-1 attribution — `Inferred`, no external documentation consulted (Q-65/Q-66 worklist; cross/03 §6.2).
- **Options.** (a) LIFO default (reference-fidelity; the A13 default); (b) FIFO default (the plausible original); (c) a per-scene selection (no evidence supports scene-awareness — rejected).
- **Proposed disposition.** **(a) LIFO default**, with a **policy knob** in `psxrt-compat` (`same_z_order: Lifo | Fifo`, default `Lifo`), the divergence documented in the policy module (the row, the confidence `UR/L`, the load-bearing reference dependence, the closure evidence). The modern-pool equal-depth tie-breaks (2D LIFO / 3D FIFO) are preserved as layer-4 engine behavior (rendering/01) regardless of the knob — they are reference-observed, not PSX-modeled.
- **Blocks.** Declaring the GPU/presentation subsystem *done* (I5 gate includes the knob + divergence note; the LIFO behavior itself is testable now).
- **Owning crate.** `psxrt-compat` (policy); `severedchains` (the OT replay consumes it).
- **Research status.** Open (Q-66). **Design status.** Proposed-pending-approval.
- **Closure evidence.** Original-PSX observation or external PSX GPU documentation of same-slot ordering; or an asset + visual census of same-z overlaps in retail TMDs (cross/02 §10 item 1).

## CD-02 — R4/Q-70: the `rand()` seed source (and the corrected full RNG inventory)

- **Observed reference behavior (P0-verified, supersedes the M10 "three generators" table's naming).** Five RNG instances in four roles, three algorithms:

| Instance | Algorithm | Seed source (reference) | Consumers |
|---|---|---|---|
| `simpleRand` @ 8004dd44 - simpleRand seed | 16-bit LCG ×9 + 0x3711 (R3) | initial **3**; no reachable re-seed (the low-12-bit guard sources `RENDERER.getVsyncCount() × 9` — practically unreachable) | **all gameplay rolls** (turn-gauge init, `checkHit`, drops, encounter ID choice, escape/revive, the 20/40/40 confusion roll, Fate `RAND` op = `X × simpleRand() >>> 16` — R5) |
| `rand()` (the `randSeed` global) | 15-bit LCG ×0x41c64e6d + 0x3039, output bits 30:16 (R4) | initial **0x24040001**; the **only** `srand` site is the reference-authored `srand((int)System.nanoTime())` at submap entry (`SMap:4091`) — a placeholder for the original's argument (L11: not a contract) | **59 call sites, all presentation/cosmetic** (SEffe 31, WMap 22, SBtld 3, effect classes 2, RetailSubmap 1) |
| Battle RNG `seed_800fa754` | `java.util.Random`-family **48-bit LCG** (multiplier 0x5DEECE66D, addend 0xB; bounded `nextInt(bound)` = rejection sampler; `nextFloat(bound)` per Java 17+ semantics) — **P0 correction 2026-11-14: not a Mersenne Twister**; the label is the reference's own `@ScriptDescription` on `scriptSetMtSeed`; the class's mix32 override of no-arg `nextInt()` is **dead code** (every reference consumer — 60+ battle sites — uses bounded/float draws; zero no-arg `nextInt()` call sites) | process-initialized from the host clock at class load (once per process); the only script-visible re-seed is `scriptSetMtSeed` @ 0x800cff24 (fn 849): `setSeed(seed ^ 0x75b_d924)` | battle item randomizers (balls — `nextInt(pool.length)`), `scriptRand` @ 0x800cee50 (fn 738), battle effect particles/rain/stars (SEffe) |
| Turn-order jitter RNG (per battle) | same 48-bit LCG class | seeded from `timestamp_a0` (the 60 Hz game clock) at battle start (`BattleStateEf4.setTurnOrderSeed`, `BattleStateEf4:655`/`:692`) | the layer-4 turn-jitter multiplier ∈ [0.9, 1.1] (GH#2481 — a documented reference divergence from the retail turn system); the `turn_order` mod keeps a copy seeded identically |
| Snow RNG (per submap instance) | same 48-bit LCG class | host clock at instance construction | submap snow presentation |

- **Reproducibility analysis (the reason R4 is High-for-reproducibility, not for gameplay).** Given identical input sequences: the gameplay surface is **fully deterministic in the reference** (all gameplay rolls ride `simpleRand`, seed 3, no re-seed — CL-GLP-17); the `rand()` streams are **non-reproducible across runs by construction** (host-clock re-seed at every submap entry) — but all 59 of its consumers are cosmetic; the battle RNG is process-clock-seeded unless the (unknown) retail script calls `scriptSetMtSeed` (Q-97); the turn-order RNG is deterministic *given the frame count* (seed = game clock at battle start).
- **Options** (cross/03 §7, carried forward): **A** — mirror the reference (host-clock at submap entry); **B** — deterministic campaign-derived seed; **C** — fixed seed + user-configurable override; **D** — PSX-VBLANK-faithful stand-in (frame-counter seed; `Proposed` forever without evidence).
- **Proposed disposition.** **A (mirror) as the default** + a **dev-only seed override** (a `--seed <n>` CLI flag behind the dev feature, TD-21 — a C-lite for regression testing that does not enter the release config surface): the reference's non-reproducibility at the 59 cosmetic sites is preserved (the maximum-reference-fidelity choice; the L11 row says the host-clock argument is *not* a contract, so mirroring it is a documented choice, not an obligation — the divergence note records: "seed source = host clock at submap entry, mirroring the reference's placeholder; the original's argument is unknown (Q-70)"); the dev override enables the deterministic replay harness (planning/05 §4) without touching release behavior. The battle RNG keeps its process-clock initialization + the `scriptSetMtSeed` re-seed contract; the turn-order and snow instances keep their documented seeds. The gameplay stream is untouched (already deterministic).
- **Blocks.** Declaring the RNG subsystem *done* (I6 gate: the three instances wired + the seed-source divergence note + the dev override); the determinism-harness *claims* (I6+).
- **Owning crate.** `psxrt` (the generator primitives), `severedchains-game` (the instances + the submap-entry re-seed site), `scus94491` (the dev override flag).
- **Research status.** Open (Q-70; Q-97 for the retail caller). **Design status.** Proposed-pending-approval (the A+override hybrid is a new option combination — the user may prefer pure A, B, or D).
- **Closure evidence.** Original-behavior evidence (vblank? campaign data? fixed? — Q-70(a)) would supersede the mirror default; Q-97 (the retail DRGN0 script's seed expression) bounds the battle-RNG determinism chain.

## CD-03 — T2: float GTE vs fixed-point

- **Observed reference behavior.** The float register model (per-op rounding divergences: SXY 22.10 truncation ±1 screen px, NCCS 12.12 stage quantization ±1 color step; the animation-matrix s16.12 quantization accumulates over long chains — the dominant divergence). The **integer-precision submap camera** (`GsSetSmapRefView2L`: int deltas, `(int)Math.sqrt`, ×0x1000/len normalization) is the only Hardware-accurate row (T4) — an engineered match with an in-source comment.
- **Original behavior.** External PSX GTE documentation not consulted (Q-20/Q-67 worklist).
- **Options.** (a) float default (reference behavior; the documented bounded divergence); the integer variant mandatory for the submap camera (T4); (b) fixed-point default (the plausible original; no evidence); (c) per-context selection.
- **Proposed disposition.** **(a)**: `psxrt::gte` exposes both precisions (TD-22); `psxrt-compat`'s T2 policy defaults to float with the divergence table documented; the game's submap camera calls the integer variant (the T4 obligation — non-negotiable); a fixed-point *global* mode is a knob, default off, deferred until the Q-67 camera-path audit motivates it.
- **Blocks.** Declaring the GTE/rendering *done* (I5: the float model + the integer camera + the divergence note; the fixed-point mode is explicitly out of the I5 gate).
- **Owning crate.** `psxrt` (model), `psxrt-compat` (policy), `severedchains-game` (the submap camera).
- **Research status.** Open (Q-20, Q-67). **Design status.** Proposed-pending-approval.
- **Closure evidence.** External PSX GTE documentation + the per-camera-path integer-math audit (the T4 precedent — cross/03 §6.2).

## CD-04 — G4: the 384→368 display/draw-width clamp

- **Observed reference behavior.** The GPU model clamps wide contexts (384) to 368 for display/draw; there are **two distinct 368s** (the GPU-model clamp vs the renderer projection — mode 0 menus are 384-wide, mode 1 clamped 368 — rendering/03). The reference clamps.
- **Original behavior.** Whether the original's effective NTSC width behaved the same is `Unresolved` (Q-65/Q-37; external NTSC/PSX display documentation + the visual cross-check).
- **Options.** (a) implement the clamp as observed (reference behavior); (b) no clamp (384-wide) — no evidence.
- **Proposed disposition.** **(a)**: the clamp is implemented in `psxrt-compat`'s G4 policy (default on — the reference's behavior; A13), documented with the two-distinct-368s note and the wide-context table (320×240 baseline, wide 384 → 368, the RESOLUTION_CONFIG multiple-of-240 rule, the 320-pinned FOV aspect — CL-ENG-11).
- **Blocks.** Declaring the presentation geometry *done* (I5: the clamp + the CL-ENG-11 coupling points).
- **Owning crate.** `psxrt-compat` (policy), `severedchains` (render service).
- **Research status.** Open (Q-65, Q-37). **Design status.** Proposed-pending-approval.
- **Closure evidence.** External NTSC/PSX display documentation + the Q-37 visual cross-check (cross/02 §10 item 4).

## CD-05 — G11: TPage rate-00 translucency semantics

- **Observed reference behavior.** Rate 00 → `B+F/2` in all three source-verified consumers (the GPU TMD path, the modern mesh split, the UI glyphs); rate-00 translucents **do occur in shipped UI** (highlight backgrounds) — so the mapping is user-visible in the reference.
- **Original behavior.** The GP0 rate-bit encoding is `Unresolved` (external PSX GPU documentation + a retail TMD/UI asset census — Q-65).
- **Options.** (a) the reference mapping (00 → B+F/2); (b) an alternative 00 mapping — no evidence.
- **Proposed disposition.** **(a)**: `psxrt-compat`'s G11 policy defaults to the reference mapping, documented (the three consumers, the shipped-UI exposure, the closure evidence).
- **Blocks.** Declaring the TPage/blend surface *done* (I5).
- **Owning crate.** `psxrt-compat`.
- **Research status.** Open (Q-65). **Design status.** Proposed-pending-approval.
- **Closure evidence.** External PSX GPU documentation of the GP0 rate bits + the retail asset census (cross/02 §10 item 5).

## CD-06 — S7: SPU sweep-mode volumes (the reference gap)

- **Observed reference behavior.** The sweep envelope is **unimplemented — full volume** (an in-source TODO); the game packs sweep bits into note volumes (so the gap is exposed whenever retail data carries sweep bits — the retail census is asset-pending, Q-68a).
- **Original behavior.** Unknown (the PSX SPU sweep documentation is on the Q-68 worklist). cross/02 §11 classifies this as a *reference gap presented as an option, not an obligation*: "a reimplementation may implement it correctly; it is not obligated to reproduce the gap."
- **Options.** (a) mirror the gap (full volume; the reference's behavior); (b) implement the sweep envelope (the plausible layer-1 behavior; `Proposed`/`Inferred` until evidence); (c) a knob with (a) default.
- **Proposed disposition.** **(b) implement the sweep envelope**, with a knob defaulting to **on** (the correct envelope) — rationale: unlike A13's bug-preservation cases (documented *retail* bugs), S7 is a *missing implementation* in the reference, not a documented retail behavior; the retail behavior is plausibly a working sweep (the game packs the bits — why, if never read?); implementing it moves toward the original without claiming fidelity (the divergence note: "sweep envelope implemented per the documented register semantics; the reference's full-volume gap is not preserved; the envelope shape is `Proposed` pending Q-68/Q-68a"). If the retail census (Q-68a) shows zero sweep-bit usage, the envelope is unobservable and the default is immaterial — the knob then guards a no-op.
- **Blocks.** Declaring the SPU model *done* (I5: the voice model + the sweep policy + the divergence note).
- **Owning crate.** `psxrt` (the envelope mechanics), `psxrt-compat` (the policy/knob).
- **Research status.** Open (Q-68a). **Design status.** Proposed-pending-approval (the one CD where the default deliberately diverges from the reference — flagged for explicit user attention).
- **Closure evidence.** A retail SSSQ sweep-bit census + PSX SPU sweep documentation (cross/02 §10 item 6).

## CD-07 — X6: the never-reset XA predictor state

- **Observed reference behavior.** The static predictor state is never reset across FMV files (main-thread consumer; not a race); ≤ 28 samples (≈ 0.74 ms) of continuity leaks per FMV boundary (M4-05 X6; audio/03).
- **Original behavior.** Undecidable in-repo — bug or intentional continuity (Q-69a); an asset observation of the first block per FMV boundary would bound the impact.
- **Options.** (a) mirror (the static never-reset state — the reference's behavior); (b) reset per file (the cleaner behavior).
- **Proposed disposition.** **(a) mirror the reference** (static, never-reset), with a knob (`predictor_reset: PerFile | Never`, default `Never`) and the divergence note (the ≤ 28-sample boundary leak, the imperceptible transient, the closure evidence). Rationale: cross/03 §8 names X6 one of the two portability choices where "preserve the reference's behavior and document the divergence is a defensible default"; the impact is bounded and small; the knob costs nothing and the evidence, when it lands, can flip the default.
- **Blocks.** Declaring the XA/FMV-audio surface *done* (I5).
- **Owning crate.** `psxrt` (the state holder), `psxrt-compat` (the policy/knob).
- **Research status.** Open (Q-69a). **Design status.** Proposed-pending-approval.
- **Closure evidence.** Asset observation of the first block per FMV boundary (cross/02 §10 item 7).

## CD-08 — C-01: the runtime vs recompiler opcode-table discrepancy

- **Observed reference behavior.** Two coexisting opcode tables (the runtime VM's and the Script-Recompiler's) with a per-opcode discrepancy, **preserved, not silently resolved** (C-01; scripting/02 §6; the M3 resolution stands). The runtime table is the execution authority (the VM's); the recompiler table is the toolchain's.
- **Original behavior.** Per-opcode reconciliation against retail script bytes is asset-pending (the retail `STR/*.BIN` disassembly).
- **Options.** (a) VM = runtime table semantics; toolchain = recompiler table semantics (the reference's own split); (b) unify on one table — no evidence for which; (c) a per-opcode config — over-engineered.
- **Proposed disposition.** **(a)**: the SDK `fate::vm` executes the **runtime table** semantics (unknown opcode → the documented error path; the unimplemented rewind family → the documented stall/error); the SDK `fate_toolchain` decompiler uses the **recompiler table** semantics (the `byOpcode`-null → non-code discrimination); both tables are reproduced in full with the per-opcode discrepancy documented in the `fate::binary` module (the C-01 entry preserved as data — the AGENTS.md "discrepancies are data" rule). The per-opcode retail verification (which table the original MIPS VM used) stays open and asset-gated.
- **Blocks.** Declaring the Fate subsystem *done* (I4: the VM + toolchain conformance suites; the C-01 documentation).
- **Owning crate.** `severedchains-sdk` (`fate`).
- **Research status.** Conflict (C-01 — preserved). **Design status.** Proposed-pending-approval.
- **Closure evidence.** Per-opcode reconciliation against retail script bytes (cross/02 §10 item 8; the retail-script-disassembly asset class, cross/03 §5).

## CD-09 — Q-74: the SSSQ tempo unit / SFX tick-quantization mapping

- **Observed reference behavior (the three in-repo readings, all source-verified).** The SFX delta-conversion code, the in-source debug log, and the MIDI/music tooling **disagree** on the SSSQ tempo unit. What *is* settled: SFX ticks run at **8.0 ms/tick** during playback (tempo pinned 60/480; the `0x51` handler is functionally dead — audio/02 §3.8); BGM is **sample-accurate at the SSSQ BPM tempo** (the MIDI tooling's `60_000_000/tempo` conversion corroborates tempo = BPM — audio/01); `sssqTempoScale` is dead (settable, never read — CL-AUD-04).
- **Original behavior.** A retail SSSQ tempo value + an audible duration (or original-engine documentation) would resolve the unit question.
- **Options.** (a) implement the *settled* behavior (SFX 8.0 ms/tick fixed; BGM sample-accurate at BPM) and document the tempo-unit discrepancy as data; (b) pick one of the three tempo-unit readings — no evidence.
- **Proposed disposition.** **(a)**: the SFX sequencer ticks at the fixed 8.0 ms (60/480) — the confirmed playback behavior; the BGM renderer is sample-accurate at the SSSQ tempo-as-BPM; the three-reading discrepancy is documented in the SDK `fate`/game audio modules as preserved data (with the register pointer); the dead `sssqTempoScale` is **not** used as if live (CL-AUD-04) but the field is parsed and ignored (the observable contract).
- **Blocks.** Declaring the audio sequencers *done* (I5: the SFX tick + the BGM timing + the divergence note).
- **Owning crate.** `severedchains-game` (the sequencer content), `severedchains` (the audio hosting).
- **Research status.** Open (Q-74). **Design status.** Proposed-pending-approval (low risk — the settled behavior is what ships).
- **Closure evidence.** Retail SSSQ bytes (a tempo value + an audible duration) or original-engine documentation (cross/02 §10 item 9).

## CD-10 — Q-80: the 0x20000 memcard file shape

- **Observed reference behavior.** The *handling* is source-verified: the 0x20000-byte image, u16@0 = 0x434D ("MC"), the 16×0x80 directory (type 0x51 + 16-char product code), the 15×0x720 slot regions, the seven retail product codes, the timestamp-ordered re-serialization with mtime backdating (saves/01 §11.2; CL-SAV-07).
- **Original behavior.** Whether the 0x20000 file is a raw PSX MCr screen-subset dump or a tool repack is `Unresolved` — one real memcard dump decides it.
- **Options.** (a) implement the documented model exactly (the reference's handling); (b) a raw-MCr passthrough mode — no evidence of the raw shape's relationship to the 0x20000 file.
- **Proposed disposition.** **(a)**: the game's memcard model implements the documented 0x20000 layout as-is (it is the observed, user-visible contract — users exchange these files); the raw-MCr question is recorded as a *validation* gap (the model may turn out to be a lossless view of a raw MCr or a repack — either way the 0x20000 handling is what users see); no passthrough mode is designed (nothing to passthrough without evidence).
- **Blocks.** Declaring the persistence surface *done* (I6: the memcard model + the divergence note).
- **Owning crate.** `severedchains-game` (`persistence`).
- **Research status.** Open (Q-80). **Design status.** Proposed-pending-approval (low risk — the documented behavior is what ships).
- **Closure evidence.** One real PSX memcard dump (cross/02 §10 item 10; cross/03 §5).

## Cross-cutting notes

1. **Default posture.** Except CD-06 (flagged above), every default is the **reference's observed behavior** — the A13 bug-preservation principle applied to the UR rows: where the original is unknown, the reference is the only observed behavior, and the divergence is documented with its closure evidence. None of these defaults claims original fidelity; the layer-1 attributions stay `Inferred`/`Unresolved` in the register until their evidence lands (cross/03 §6.2 — the external-documentation worklist is a separate, user-approved evidence pass).
2. **Knobs.** Four policy knobs are proposed (G6 same-z order, T2 fixed-point mode, X6 predictor reset, S7 sweep enable). All are `psxrt-compat` policy entries, all default to the reference behavior (except S7, flagged), and none is exposed in the release config surface (they are build-time/diagnostic surfaces — the release config surface is the 88-entry census, CL-SAV-10). The dev `--seed` override (CD-02) is the only user-facing addition, behind the dev feature.
3. **What this does not do.** No register item is closed by P0 (P0 is planning; the register re-triage is M10's). The research statuses above are carried verbatim. The decisions take effect on user approval of P0 and are recorded in the register as *design decisions* (the register's vocabulary for this — see the Q-70 entry's "design decision (pre-implementation)" class, cross/03 §6 item 5).
4. **Blast radius per CD.** Each CD blocks only its subsystem's *done* declaration (cross/03 §8); the I0–I10 gates (planning/04) are structured so every CD's subsystem can be built against the default with the knob + divergence note, and flipped when evidence lands — no rework, a policy flip.