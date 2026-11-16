# P0-06 — Research-to-Implementation Traceability Matrix

Status: **Proposed (P0, 2026-11-14).** Row-level coverage of all **135 compatibility rows** (the 43 carried M4-05 rows of cross/02 §2 + the 92 new `CL-*` rows of cross/02 §3–§9) and the 10 decision items (cross/02 §10), mapped to owning crate, implementation milestone (planning/04), and verification method (planning/05 layers). This is the *plan* of coverage; at each milestone gate the row's **Status** is updated from `planned` to `verified` (with the test ID) or `deferred` (with the evidence class and register pointer). A row is complete only when every sub-part that has an implementation obligation is `verified`; sub-parts that are *evidence-only* (asset/external-documentation pending) close at I10's asset pass or stay open in the register — their obligation is the *documentation*, which is verified at the owning milestone.

Legend — **V** columns: `U` unit, `G` golden vector, `P` property, `I` integration (headless), `M` model conformance, `C` census, `D` divergence guard, `A` asset pass (I10). **O** = owning crate: `rt` psxrt, `cp` psxrt-compat, `im` psxrt-import, `en` severedchains, `sdk` severedchains-sdk, `gm` severedchains-game, `bin` scus94491.

**Owning crate means the crate that must make the behavior correct, and after 2026-11-15 that is never a policy crate.** `psxrt-compat` is a reverse-conversion toolchain — it emits original-format bytes and holds no runtime decision point (planning/01 §2, planning/03 §5–§6). A row may *cite* `psxrt-compat` as the place its evidence came from, as several rows did, without `psxrt-compat` owning the row. Six rows were re-owned on 2026-11-15 for exactly that reason; each note below says where the mechanism lives and, where one is still wanted, which subsystem owns the parameter that selects between mechanisms.

**Milestone letters.** This matrix was written against the pre-split milestone set and keeps its letters, so a bare `I1`/`I5`/`I10` here means the pair in `planning/04`: **I1 = I1a containers+codecs + I1b content+sequences**, **I5 = I5a PSX models in `psxrt` + I5b native backends and profiles in `severedchains`**, **I10 = I10a toolchain + I10b end-to-end**. Where a row's note now names a specific module, that name is the precise statement and the letter is the approximate one. Per-row owning crate beats both.

## 1. CL-PSX — the 43 carried M4-05 rows (cross/02 §2; source psx/05 §3)

| Row | Obligation (short) | O | Milestone | V | Notes |
|---|---|---|---|---|---|
| G1 | VRAM 1024×512, dual 15/24-bit stores, 4/8-bit texels | rt | I5 (backend) | M G | **I0 correction (2026-11-15): the dual-store model was not built in I0.** What I0 delivered is the texel-width half — the 4/8/15/24 line packing and the index-within-word rule — verified exhaustively in `psxrt/tests/bpp_exhaustive.rs`. The 1024×512 geometry, the two stores and the mode-0 `vramTexture15` sampling land together in I5, where there is a surface to sample into |
| G2 | Bpp 4/8/15/24 encode/decode pair + TPage/CLUT fields | rt | I0 | G P | **Verified at I0.** Exhaustive decode over all 65,536 words × slots at each paletted depth (`psxrt/tests/bpp_exhaustive.rs`, 9 tests) + the packed-line round-trip property over in-domain indices and the out-of-domain companion (`psxrt/tests/properties.rs`). No fixture: the sweep is its own oracle, digests taken from the reference's expressions |
| G3 | Center-origin offset; double buffer; PSDCNT frame-counter cache invalidation | rt | I0 / I5 | M G | The offset model in I0; the matrix-cache invalidation in the I5 wgpu backend |
| G4 | 384 → 368 display/draw-width clamp | rt | I5 | M D | **CD-04** owns the clamp behavior; UR row — the retail-width evidence is the I10 asset/external pass (`A`); the clamp is a divergence-note row. **Re-owned from `cp` (2026-11-15):** the clamp is `psxrt::gpu` arithmetic; the on/off is a `PresentationProfile` discriminant owned by `severedchains` at L2 |
| G5 | Ordering table (14 bits, 0x4000, zMax clamp, slot-0x3fff clear, back-to-front) | rt | I0 / I5 | M G | The mechanism in I0; the psx/01 §7 worked trace as golden in I5 |
| G6 | Same-z order: LIFO (reference) vs FIFO (PSX) | rt | I5 | M D | **CD-01** (default LIFO, parameter to FIFO on `psxrt::gpu`'s ordering replay); the `Battle.applyTextureAnimation` dependency = the I8 divergence guard; the retail-overlap census = I10 `A`. **Re-owned from `cp` (2026-11-15):** ordering is a runtime mechanism; `SameZOrder` is its parameter, not a `psxrt-compat` module |
| G7 | z-insertion `(avgZ/4 + zOffset)/2^zShift` clamped, float division | rt | I5 | M G | **I0 correction: not built in I0** — the formula is meaningless without the ordering table it writes into, and I0 built no table. I5 with the trace |
| G8 | Rasterizer edge rules (clockwise, zero-area drop, top-left, integer barycentric) | rt | I5 | M | Mode-0 replay reproduces the rules; pixel-exactness stays Blocked (no baseline) — model-conformance only |
| G9 | Texturing: 15-bit index, CLUT from vram24, index-0 transparent, `T×C>>>7` | rt | I0 (arithmetic) / I5 (lookup) | M G | **Partly verified at I0:** the `T×C>>>7` modulation and the 15-bit index extraction exist and are swept exhaustively (`psxrt::blend::modulate` + `psxrt/tests/blend_exhaustive.rs`, `Bpp::Bits15` decode). The CLUT-from-vram24 read and the index-0 transparency need a VRAM model and move to I5, together with the `vertColour × texColour` float path |
| G10 | Blend equations (B+F)/2, B+F, B−F, B+F/4 (F/4 first, truncating) | rt | I0 | G | **Verified at I0.** `psxrt/tests/blend_exhaustive.rs` sweeps all 65,536 (B,F) pairs per mode and pins the F/4-first truncating order; the saturation/truncation census per mode is asserted, so the sweep cannot quietly become a no-op. The B+F/4 decomposition rounding-delta stays a documented divergence, re-checked at I5 against the shader path |
| G11 | TPage rate-00 → B+F/2 | rt | I5 | M D | **CD-05** (default: keep the reference mapping); the retail rate-00 census = I10 `A`. **Re-owned from `cp` (2026-11-15):** the three decoders went to `psxrt::gpu::pixel` at I1b/I3; a decoder is not a policy and not an encoder — the encoding half is `psxrt-compat::texture`, which is a different module and does not own this row |
| G12 | Mask bit set/skip; NOT_TO_MASKED_AREAS; textured-alpha gate | rt | I5 | M G | The two-pass rule over a scripted attribute stream |
| G13 | One-frame display latency; swap-before-onDraw +1; FMV render-tick cadence | rt | I5 | M G | The buffer-flip model golden; the FMV cadence cross-checked in I6 (CL-ENG-13) |
| G14 | CPU→VRAM upload (both stores, 15-bit LE words, partial-rect guard) | rt | I5 | M G | **I0 correction: not built in I0** — no upload path without a VRAM to upload into. I5; I5 host (the async load path — CL-AST-11) |
| G15 | VRAM→VRAM copy (per-pixel, mask-honoring, no overlap protection) | rt | I5 | M D | The (960,256) backing-store behavior; the PSX BLT overlap semantics stay Unresolved (register) — the *no-protection* behavior is the D-guard |
| T1 | RTPS/RTPT/NCLIP/AVSZ/NCCS register-model subset | rt | I5 | M G | The psx/02 §4 worked-example chains; the unused-tail absence asserted (not ported) |
| T2 | Float vs fixed-point per-op rounding (SXY 22.10, NCCS 12.12, MAC0 32) | rt | I5 | M D | **CD-03** (default: float model + documented bounded divergence; the integer variant exists per T4); the external worklist = I10 `A`. **Re-owned from `rt/cp` (2026-11-15):** `GtePrecision` becomes `TransformPrecision`, a parameter of `psxrt::gte`. A transform precision is a way the runtime computes, not a statement about a disc format |
| T3 | SXY saturation → whole-primitive discard (no clipping) | rt | I5 | M D | The discard-over-saturation golden; the float ±1-px admission documented |
| T4 | `GsSetSmapRefView2L` integer-precision submap camera | rt | I7 | M G | **The only HA row** — bit-exact integer-program golden (int deltas, `(int)Math.sqrt`, ×0x1000/len); the Q-67 camera-path audit = I10 `A` |
| T5 | MAC0 depth-cue term (dead) | rt | — | D | **Not ported** (explicitly not a constraint, cross/02 §11) — the non-port is documented in the psxrt GTE module |
| T6 | Projection plane 1000→640 (`gameInit`); `2z ≤ H` test | rt | I5 / I6 | M G | The constant model in I5; the `gameInit` override in the I6 boot |
| T7 | NCCS lighting (up to 3 lights, no specular/fog) | rt | I5 | M G | The lighting golden; the stage-quantization divergence per T2 |
| S1 | 60 tps SFX tick; 735 stereo samples/tick @ 44.1 kHz | en | I5 | I M | The tick-cadence model golden; the sfx-thread integration in I5 |
| S2 | 64-bit edge words, accumulate/snapshot/zero, first-sample-slot apply | en | I3 (protocol host) / I5 (SPU sink) | I G | The edge-word protocol suite (I3 (i)); the ≤ 16.7 ms quantization documented |
| S3 | Dynamic note→voice allocation (retrigger → bound-gated steal 20/8 → round-robin → finished/unused) | rt | I5 | M D | The allocation-chain scenario goldens (the M6-01 correction is the oracle) |
| S4 | ADSR phase machine + 44.1 kHz cycle counting + keyOn reset | rt | I5 | M G | The tick-trace goldens over scripted key sequences |
| S5 | Voice ADPCM (28 samples/8 bytes, 3-bit filter, POS/NEG, loop flags) | rt | I5 | M G | The block-decode goldens (shared codec core with X2/X3) |
| S6 | Pitch stepping + 512-position 4-tap interpolation + FM + shared noise | rt | I5 | M G | The interpolation golden; the reference-specific interpolator documented (layer 4) |
| S7 | Sweep-mode volume unimplemented (full volume) | rt | I5 | D | **CD-06** — the only CD whose default *diverges* from the reference (the sweep envelope is implemented per the documented SPU behavior; the divergence note is prominent); the retail sweep-bit census = I10 `A`. **Re-owned from `cp` (2026-11-15):** the envelope evaluator is `psxrt::spu` arithmetic over `Clock` phases; `spu::SweepMode` is its parameter and `AudioProfile::psx_reference()` is where a set of them is chosen |
| S8 | Reverb float IIR network (work area 0x4000, /32768) | rt | I5 | M G | The network-output golden; the hardware-topology equivalence stays Unresolved (register) |
| S9 | Single Spu monitor (host-internal) | en | I5 | I | Format-only (FO) — the Rust equivalent is the narrow locked surface (cross/01 §5.4); the observable semantics is S2 |
| X1 | XA 24-byte header; 18×128-byte portions; 2016 samples/sector/lane | im | I1 | M G | The synthetic-sector golden; the format corroboration (37.8 kHz × CD sector) noted |
| X2 | ADPCM core `s = (t<<shift) + (old·f0+older·f1+32)/64`, s16 clamp | im | I1 | M G | The psx/04 §5 worked path (the exact sample values where documented) + the s16 clamp invariants |
| X3 | 2-bit XA filter vs 3-bit SPU filter (documented divergence) | im/rt | I1 / I5 | D | The per-consumer divergence asserted (the same core, two field widths) |
| X4 | FMV audio: native-rate decode, `>>1` + FMV×MASTER gain, drop-on-full | im/en | I1 (decode) / I6 (FMV hosting) | M I | The gain path golden; the FMV stage wiring in I6 (CL-ENG-13) |
| X5 | 6→7 zigzag FIR (dead) | — | — | D | **Not ported** (cross/02 §11) — documented in the im::xa module |
| X6 | Static never-reset XA predictor (leaks across FMV files) | im | I1 | D | **CD-07** (default: preserve the leak as the documented behavior; the knob for per-file reset is dev-only); the retail-boundary observation = I10 `A` |
| X7 | Mono = two mono streams; FMV XA single stream per file | im | I1 | M G | The lane-geometry golden over a synthetic mono sector |
| R1 | PSX degree 4096/turn | rt | I0 | G | **Verified at I0.** `psxrt/tests/golden_trig.rs` pins the constant's bit pattern (`0x3ac90fdb`) and the 16,385-angle grid it produces with |
| R2 | `rsin`/`rcos` float×0x1000 short-cast; positive-only-at-constrained-sites | rt | I0 (golden) / I5 (sites) | G D | **Golden half verified at I0:** 32,770 assertions over θ = −8192…8192 from an independent Java oracle (`trig_grid.txt`), including the double narrowing. The D-guard half needs call sites — a state passing an out-of-range theta is a divergence test that can only exist once there are states, so it lands at I5 with the call-site census |
| R3 | `simpleRand` LCG16 (×9+0x3711, seed 3; /2 & 0xffff; the unreachable guard) | rt | I0 (algo) / I6 (wiring) | G I | **Algorithm verified at I0:** 12,000 steps from seed 3, replayed in file order, from an independent Java oracle (`rng_lcg16.txt`), plus the reseed branch exercised deliberately (`golden_rng.rs`, `properties.rs`). Two findings the fixture recorded: the `/2` is a truncating signed divide, not a shift, and the guard multiplies the frame count by 9 *before* the ordinary update, so a guarded step advances by 81. The guard's vsync-sourcing is documented; the consumer census still lands at I7/I8 |
| R4 | `rand` LCG15 (×0x41c64e6d+0x3039; bits 30:16; initial 0x24040001) + **seed source** | rt/en | I0 (algo) / I6 (wiring) | G D | **Algorithm verified at I0:** 12,000 steps from 0x24040001 (`rng_lcg15.txt`) and the output-range invariant swept as a property. **CD-02** still owns the seed source (default: mirror the reference's host-clock `srand` at submap entry + the dev `--seed` override); the 59 call sites' non-reproducibility = the tier-2 invariants (planning/05 §5); the original-behavior evidence = I10 `A` (Q-70). **Re-owned from `rt/cp` (2026-11-15):** the stream is `psxrt::rng`; the seed source is the game crate's boot decision, taken explicitly (`WiringProfile`), and is not released as a runtime-readable flag |
| R5 | Fate op bodies: `RAND = X × simpleRand() >>> 16`; SIN_12/COS_12/ATAN2_12 | sdk | I4 | G | The op-body goldens against the injected LCG16 stream (the R3 integration point) |

The M4-05 posture check: the 4 HA/BE/FO/UR classes carry into the status column semantics — the FO rows (T5, S9, X5) are *non-port or host-internal* (D or I), the UR rows (G4, G6, G11, S7, X6, R4-seed) are CD-owned (planning/03) with their evidence at I10, the HA row (T4) is the bit-exact I7 golden.

## 2. CL-ENG — engine, lifecycle, frame contract (13 rows; M1/M5)

| Row | Obligation (short) | O | Milestone | V | Notes |
|---|---|---|---|---|---|
| CL-ENG-01 | Frame loop shape + one-frame input edges | en | I3 | I D | The 21-phase trace (I3 (a)); the edge suite (I3 (b)) |
| CL-ENG-02 | Per-context rates (60/30/20 Hz; the 19-site census; the 1/60 s unit) | en | I3 (rates) / I6–I9 (write-site census) | I D | The rate-table suite (I3 (e)); the per-context write sites land with each state (the census is the D-guard: a state that writes an undocumented rate is a regression). **The unit half is verified at I0** — `psxrt::timing` models the cadence → rate → clock-increment chain and the tick gate over a 352-row oracle matrix (`psxrt/tests/timing_matrix.rs`); what remains is where the rates get *written*, which needs states |
| CL-ENG-03 | Frame-locked pacing; tick-skip; speed 1..16 | en/bin | I3 | I | The pacing suite (liveness bounds, planning/05 §4); the speed multiplier over both real rate + virtual counter |
| CL-ENG-04 | Queued transition (frame-top apply; destroy-before-init; forced vsyncMode 2; native-table reinstall at apply) | en | I3 | I D | The transition suite (I3 (c)); the reinstall call recorded (the I4 hook exercised at I4's integration) |
| CL-ENG-05 | Teardown at the same onDraw's end-of-frame fixed point | en | I3 | I D | The teardown suite (I3 (d)); the "renders once more" observation |
| CL-ENG-06 | LEGACY render mode = SUBMAP + TITLE only | en | I3 (plumbing) / I5 (projection) / I6–I7 (content) | I D | The mode census as a standing assertion (a D-guard over the state set); the H-division projection in I5 |
| CL-ENG-07 | Two-tier input; per-state action sets (41 of 70 mod-registered); deadzones; rebinds; rumble lerp | en | I3 (framework) / I6 (content) | I C | The framework suite in I3; the 70-action census at I6 (with CL-MOD-10); the rebind persistence via the config framework (I2) |
| CL-ENG-08 | The 14-step boot chain (unpack ∥ main; the INIT_LOCK handoff; SFX thread last) | bin/en | I3 | I | The lifecycle suite (I3 (f)); the handoff gate |
| CL-ENG-09 | Shutdown order (the documented chain) | bin/en | I3 | I D | The shutdown-order suite (the chain as a D-guard) |
| CL-ENG-10 | Audio decoupling (queues + control calls only; the 60 Hz SPU convention) | en | I3 (structure) / I5 (threads) | I | The thread-structure suite; the queue-only communication asserted by the observation hooks (no shared clock/sample state — a D-guard) |
| CL-ENG-11 | Presentation geometry (320×240 baseline; wide 384/clamped 368; multiple-of-240; the 320-pinned FOV) | en | I3 (plumbing) / I5 (backends) | M D | The geometry invariants; the G4 clamp integration (CD-04) |
| CL-ENG-12 | Crash-recovery (the observable surface: a save at the last savable state; the crash log; exit 1) | en/gm | I3 (scaffold) / I6 (save-writing) | I D | The error-model suite (I3 (h)); the "Crash Recovery N" + submap-position-restore suite at I6 |
| CL-ENG-13 | FMV presentation (15 fps / 10 sectors/frame; render-tick driven; FMV 0 = callback takeover, not a state) | en/gm | I3 (cadence model) / I6 (the FMV state + FMV 0) | I G | The cadence golden; the takeover contract (a D-guard: FMV 0 is not an engine state — asserted by the state machine) |

## 3. CL-AST — asset pipeline (12 rows; M2)

| Row | Obligation (short) | O | Milestone | V | Notes |
|---|---|---|---|---|---|
| CL-AST-01 | 4 discs, 2352-byte sectors, ISO9660+Form2, disc-4 master merge, in-place reading | im | I1 | M G | The synthetic 4-disc ISO conformance (the I1 headline) |
| CL-AST-02 | MRG container (magic, entry table, virtual entries, duplicates, the trailing-digit split) | im | I1 | M P | The round-trip property + the split fixture suite |
| CL-AST-03 | Unpack pipeline (transform registry; VERSION=5 cache; determinism/ordering; failure → skip) | im | I1 | I P | The pipeline determinism (the byte-stable `files/` tree); the failure-skip suite |
| CL-AST-04 | TMD (flag semantics; 0x1c object table; 8-byte stride; normals ×2^12; `updateTmdPacketIlen` rewrite) | im | I1 | M G | The CTMD/TMD twin fixture; the run-length rewrite golden (the on-disk low-16 authoritative only for CTMD) |
| CL-AST-05 | CTMD (compressed TMD; the bit-stream run counts authoritative on disk) | im | I1 | M G | The CTMD decode golden |
| CL-AST-06 | TIM (writer-pinned layout; GPU upload; PXL sheets; MCQ strips) | im | I1 | M G | The CLUT-case golden + the PXL/MCQ fixtures |
| CL-AST-07 | Animation family (plain ANIM, LMB, CMB; unpack-time patches) | im | I1 | M G | The per-family fixtures |
| CL-AST-08 | Text locations + the reference's JSON+PNG font stack (layer 4) | im/en | I1 (locations) / I5 (the font stack) | M I | The in-game-text locations as data; the font stack is the documented layer-4 behavior (not a fidelity obligation) |
| CL-AST-09 | MDEC (sector structure; demux; VLC/IDCT decode; in-band XA) | im | I1 | M G | The synthetic-bitstream decode invariants (no encoder exists — the risk noted in I1); the Q-73 per-IKI header details = I10 `A` |
| CL-AST-10 | Music formats (SSSQ 16/24 split; SSHD; XA→Opus transcode 80/63, 128 kbps, pre-skip 312) | im | I1 | M P | The SSSQ/SSHD parse suite; the transcode consistency test (planning/04 I1 — decoder-level, not a fidelity claim); the `opus` version is TD-24-provisional |
| CL-AST-11 | Runtime load contract (async I/O; failure → completed-with-null; `scriptWaitForFilesToLoad` PAUSE_AND_REWIND; vramLock) | en | I3 | I D | The load-contract suite (I3 (g)); the null-on-failure as a D-guard (never a torn state) |
| CL-AST-12 | Original executable overlay (SCUS_944.91 + OVL; runtime consumers; embedded-asset inventory) | im | I1 | M C | The overlay parse; the embedded-asset inventory as a census (exhaustiveness stays `Inferred`/`Unresolved` — the register) |

## 4. CL-SCR — Fate scripting (13 rows; M3)

| Row | Obligation (short) | O | Milestone | V | Notes |
|---|---|---|---|---|---|
| CL-SCR-01 | Binary container (LE 32-bit words; entrypoint table; instruction layout; parameter words) | sdk | I4 | M P | The round-trip property base; the container golden |
| CL-SCR-02 | C-01 master opcode table (discrepancy preserved) | sdk | I4 | C D | The dual-table documentation module (per-opcode); **CD-08** (VM = runtime table, toolchain = recompiler table); the per-opcode retail reconciliation = I10 `A` |
| CL-SCR-03 | Header-validity predicate (code vs data) | sdk | I4 | M G | The predicate golden (positive + negative cases) |
| CL-SCR-04 | Round-trip fidelity (coordinate-stable; the patch-on-canonical-only flow) | sdk | I4 | P D | **The round-trip gate** (planning/04 I4 — the property + the hazard-set fixtures 0x24–0x27, CMP-header 8/9, the one-way types); the non-canonical path documented as unsupported |
| CL-SCR-05 | VM allocation (108 slots; 0–8 reserved; round-robin; per-frame reset; exhaustion; the init) | sdk | I4 | I D | The allocation-order suite (I4 conformance); the exhaustion message D-guard |
| CL-SCR-06 | 63-slot dual int/float storage + mode bits + slot conventions | sdk | I4 | I G | The storage golden (the `Math.round` mirror); the convention slots as data |
| CL-SCR-07 | 1024-entry native table (global-first + per-state override; the 592-slot catalog) | sdk | I4 (framework) / I6–I9 (content) | I C | The dispatch suite in I4; the catalog census (86/362/144, 578 unique, 4 alias groups) at the milestones that install each slice (I4: 86 base; I7: +144 submap; I8: +362 battle) |
| CL-SCR-08 | Pacing (framesPerTick 1/2; the per-frame bound reset; the fork/consume/deallocate lifecycle) | sdk | I4 | I D | The pacing suite (the 1/2 cadence; the transition-forced 1 via the CL-ENG-04 hook); the destructor gate |
| CL-SCR-09 | The 9-point lifecycle event surface + the flag-observation events | sdk | I4 | I D | The lifecycle-point suite (the events fire at the documented points); the events post on the I2 bus |
| CL-SCR-10 | Runtime patch pipeline (353 entries; CRC32 gate; one-time backup; restore-on-removal; idempotent/self-healing; the 24 configs) | im | I1 (orchestration, no-op toolchain) / I10 (completion with the real toolchain) | I P | The orchestration idempotence in I1 (the synthetic patch set); the full 353-entry walk + the CRC identity + the restore at I10 |
| CL-SCR-11 | Meta system (CSV row = call index = table slot; enum tables; the two metadata worlds) | sdk | I4 | M G | The alignment golden (a synthetic meta CSV over a synthetic script) |
| CL-SCR-12 | Toolchain text tiers (Fate ↔ assembly; the translator; the CLI) | sdk | I4 | P I | The round-trip property covers the tier mapping; the CLI is a thin optional surface (TD-13) |
| CL-SCR-13 | Reference-specific vs original VM (the layer-4 additions enumerated; not presented as original) | sdk | I4 | D | The enumeration module (each layer-4 addition tagged); the Q-17 per-op parity stays open (the register — asset/external evidence) |

## 5. CL-AUD — audio and music (10 rows; M6)

| Row | Obligation (short) | O | Milestone | V | Notes |
|---|---|---|---|---|---|
| CL-AUD-01 | Three pipelines, two SSSQ players, four threads (the 120 Hz music / 60 Hz sfx / 10 ms XaPlayer) | en | I5 | I M | The thread-structure + clock-domain cadence suite (the I5 audio suite) |
| CL-AUD-02 | BGM sample-accurate at SSSQ BPM; the 8.0 ms SFX ticks; the tempo-unit disagreement | en | I5 | M G | The sample-accurate BPM timing golden (the tick cadence); **CD-09** owns the Q-74 disposition (default: the reference's conversion code); the retail evidence = I10 `A` |
| CL-AUD-03 | SFX event grammar (the 0xa0 note-on family; the 0x80/0x90 gap documented) | en | I5 | D | The grammar suite (the 0xa0 family driven; the gap handlers preserve the documented non-implementation — a D-guard, not an obligation to throw) |
| CL-AUD-04 | Documented gaps (the CC/NRPN asserts; the dead `sssqTempoScale`) | en | I5 | D | The dead field is *not used* (a D-guard: a read of the dead field is a regression); the asserts are optional (cross/02 §11) |
| CL-AUD-05 | Preserved bugs/fixes (the GH#1623 1-tick fade-out preserved; the GH#1719 re-timing; `wasteSomeCycles` removed) | en | I5 | D | Each row as a divergence guard (the preserved bug's test; the fix's test; the removal's non-presence) |
| CL-AUD-06 | `panMergeTable[0x67] = 0x5B` anomaly | en | I5 | D | The data quirk preserved (the table golden includes the 0x67 entry) |
| CL-AUD-07 | Volume routing (LODXA→SFX, FMV→FMV, BGM→MUSIC); the behavioral-mirror disclaimer | en | I5 (routing) / I7+ (content) | I D | The routing suite (the knob → domain mapping); the "not sample-identical" note as the standing disclaimer (the I5 gate record) |
| CL-AUD-08 | XaPlayer contract (the stage 0–4 API; the guards; the 10 ms ticks; the 8 buffers; the ≥ 40 ms guard; pre-queue 4; one 480-sample frame/tick; the volume; drop-on-full) | en | I5 | M G I | The XaPlayer suite (every guard as a case; the backpressure D-guard) |
| CL-AUD-09 | Concurrency (music-load callbacks write game state from the I/O thread) | en | I5 | I | The single-consumer game-thread reads asserted (the observed hazard with its assessment — no race in observed use is the invariant) |
| CL-AUD-10 | Sound-font model (`Sssq` 16 / `Sssqish` 24 — the split is a format fact) | im | I1 | M G | The format-split parse suite (I1 — the format fact lives in the importer; the hosting is I5's) |

## 6. CL-SAV — saves and configuration (15 rows; M7)

| Row | Obligation (short) | O | Milestone | V | Notes |
|---|---|---|---|---|---|
| CL-SAV-01 | All 11 magics loadable (each version understands itself; no converter; V10 the only writer) | gm | I6 | M G | The 11-magic suite (hand-built fixtures per the documented layouts); the no-converter invariant (a D-guard) |
| CL-SAV-02 | RETAIL 0x6E4 layout (the 0x1FC struct; the 0x19C/0x1E8/0x2E9 fields; positional characters; the u8 id tables; the transform bits) | gm | I6 | M G | The byte-layout golden against the documented offsets; the retail-byte validation = I10 `A` (the compatibility floor — saves/01 D1) |
| CL-SAV-03 | No lossless round-trip (old → V10; the inexpressible fields dropped by design) | gm | I6 | I D | The per-version drop matrix (a D-guard over the documented per-version list) |
| CL-SAV-04 | Mod forward compatibility (the registry-id survival; the warn+skip vs fail split; the `lod-core` alias) | gm | I6 | I D | The policy-split suite (a removed item → warn+skip; a removed character/stat type → fail); the alias round-trip |
| CL-SAV-05 | Savable states = WMAP+SMAP only; the Tag shapes; the `lastSavableEngineState` set sites | gm | I6 (model) / I7 (the state Tags) | I G | The Tag round-trip; the set-site census (a D-guard: only `WMap.init`/`SMap.init` set it) |
| CL-SAV-06 | Save/load lifecycle ordering (the documented event order; the `GameLoadedEvent` replacement) | gm | I6 | I D | The ordering suite (the scripted save load posts the events in order); the replacement contract |
| CL-SAV-07 | Campaigns on disk (the directory model; the naming; the timestamp ordering; the memcard import shape) | gm | I6 | I M | The campaign-flow suite; the memcard 0x20000 model per **CD-10** (the documented handling; the retail-shape question Q-80 = I10 `A`) |
| CL-SAV-08 | Character load semantics (V1 replay vs V2+ copy) | gm | I6 | I G | The per-version load-suite cases (the V1 replay golden; the V2+ copy) |
| CL-SAV-09 | Validity/stability (no checksums; the MapTag nondeterminism; the lossy V10 PNG; no byte-stability expected) | gm | I6 | D P | The invariants (the parse-success validity; the semantic round-trip; the V10 byte-stability asserted *only* for the fixed-writer-order part, with the lossy-atlas note — a documented exception) |
| CL-SAV-10 | Config stores (the two coexisting stores; the single bridge; the entry model; the 88 entries) | sdk | I2 (framework) / I6 (content) | I C | The framework matrix in I2; the 88-entry census at I6 (80/7/1) |
| CL-SAV-11 | `.dcnf` layout + codecs + read semantics (clear-then-load; skip-unknown; wrong-location discard; the re-apply; the missing/corrupt behavior) | sdk | I2 | M G | The byte-layout suite over hand-built files (the full semantic matrix) |
| CL-SAV-12 | Three storage locations (GLOBAL post-bootMods; CAMPAIGN; the per-save tail) | sdk | I2 (framework) / I6 (the load ordering) | I D | The location-matrix suite (the GLOBAL-after-bootMods ordering as a D-guard) |
| CL-SAV-13 | Presets (the `.dpre` Tag format; FORMAT_VERSION 1 + legacy; the per-entry skips; the built-ins code-defined; the vanilla = 12 overrides; atomic writes) | sdk | I2 (framework) / I6 (content) | M G | The framework suite in I2; the built-in census at I6 (the vanilla = exactly the 12 M10-verified overrides) |
| CL-SAV-14 | `ConfigCollection` semantics (default fallback; the revision; the setConfig order; the advisory locks; the preset identity) | sdk | I2 | I D | The semantics suite (the event-after-hook order as a D-guard) |
| CL-SAV-15 | Crash-safety split (`.dpre` atomic vs `.dcnf` truncating; the corrupt `config.dcnf` → empty) | sdk | I2 | I D | The write-path matrix (the atomic vs truncating as a D-guard) |

## 7. CL-MOD — modding (12 rows; M8)

| Row | Obligation (short) | O | Milestone | V | Notes |
|---|---|---|---|---|---|
| CL-MOD-01 | Discovery/identity (the `./mods` scan; the semver gate; the required `lod`+`lod_core`; the hard failure; the no-ctor/no-init/no-unload) | sdk | I2 (framework) / I10 (the data-mod packages) | I D | The discovery suite in I2 (the synthetic `ModDef`s); the data-mod package format per TD-16 at I10 |
| CL-MOD-02 | `bootMods` 12-step rebuild + the 13 call sites (the campaign-scoped reboots; the required-only memcard boot) | sdk | I2 (framework) / I6 (the 13 sites) | I C | The 12-step suite (the idempotence) in I2; the call-site census at I6 (the game's call graph — the M10-corrected 13) |
| CL-MOD-03 | `bootRegistries` (the 13 remaining windows; the 3 call sites) | sdk | I2 (framework) / I6 (the 3 sites) | I C | The window suite in I2; the site census at I6 |
| CL-MOD-04 | Registry contract (no add/remove events; the one-shot window; the per-boot re-creation; the delegates; 21 registries) | sdk | I2 | I C | The window-semantics suite (the I2 headline); the 21-registry census |
| CL-MOD-05 | Event contract (exact-class dispatch; the 5 priorities; the per-handler isolation; the critical set) | sdk | I2 | I D | The dispatch matrix (the exact-class discrimination as a D-guard — a supertype fan-out is a regression); the critical-set suite |
| CL-MOD-06 | Cancellation (the exactly-7 enforced behaviors; the honored `Pre*`/`AdditionUnlock`; the `stopPropagation` API-only) | sdk | I2 | I D | The cancellation suite (the 7 behaviors observe it; the `Pre*` honored — the faithful choice per the row); the `RenderEvent` side-effect-only note |
| CL-MOD-07 | i18n (the 4-level precedence + key fallback; the malformed-template fallback; the live re-translation; the 2 extension points; the filename-prefix-ignored rule) | sdk | I2 (framework) / I10 (the working-dir `lang/` tree + the live end-to-end) | I G | The precedence matrix in I2; the live re-translation integration at I10 (the language change → no-reboot re-resolution over real content) |
| CL-MOD-08 | Script-patch modding (unpack-time only; the observable unit = the unpacked tree; the idempotent re-unpack; the restore; the assembly patch language; the config-in-CRC) | im | I1 (orchestration) / I10 (completion) | I P | The orchestration suite in I1 (the synthetic set); the full 353-entry + CRC identity + restore at I10 (shared with CL-SCR-10) |
| CL-MOD-09 | Engine↔shipped-script interlock (`char_utils`: the SMap:433 FIFO; the CharacterData2c:32 removal flag) | gm | I7 (the submap state) / I10 (the shipped-script verification) | I D | The interlock suite (the synthetic submap script driving the FIFO + the flag); the shipped-script verification at I10 (the real `char_utils` patches applied over the fixture tree) |
| CL-MOD-10 | The engine/content boundary (the 1238 retail entries mod-registered + delegate-resolved; the saves store ids) | gm | I6 | C I | **The census suite** (the 1238 per-registry breakdown — 127/1110/1; the 70 input actions; the 88 config — at I6); the delegate-resolution integration (the states resolve content through the registries, not hardcoded — the I6–I9 standing D-guard) |
| CL-MOD-11 | Debugger `*Source.DEBUGGER` tags (the tooling-adjacent boundary) | bin | I10 | I D | The tag suite (the debugger mutations carry the distinct tags through the normal events); the no-engine-dependence boundary (a D-guard over the debugger module) |
| CL-MOD-12 | Java mechanisms are implementation, not contract (the J-tables) | sdk | I2 (the contract layer) / all (the standing rule) | D | The J-table separations are the oracle for the I2 contract suites; the language-neutral contract (modding/01 §8) is the standing D-guard (no test may assert a Java mechanism) |

## 8. CL-GLP — game systems (17 rows; M9)

| Row | Obligation (short) | O | Milestone | V | Notes |
|---|---|---|---|---|---|
| CL-GLP-01 | The RQ7 spectrum (per-system script-vs-native boundary with evidence) | gm | I6 (title/wmap boundary) / I7 (submap policy layer) / I8 (battle engine-law) / I9 (inventory/characters) | I D | Per-system boundary assertions (the title's zero-script census at I6; the wmap's zero-script at I7; the submap's 144-native census at I7; the battle's engine-law surface at I8; the script-thin edge at I9) — each a D-guard over the documented boundary |
| CL-GLP-02 | The RNG census (the `simpleRand`/`rand`/battle-RNG identities + roles) — **the P0-corrected 5-instance surface** | gm | I6 (wiring) / I7–I9 (consumers) | C D | The census suite (the 5 instances: the LCG16, the LCG15, the battle 48-bit LCG, the turn-order 48-bit LCG, the snow 48-bit LCG — the identities per the P0 correction, not Mersenne); the per-consumer roll-sites traced at the owning milestones (a D-guard: a gameplay roll on the wrong stream is a regression) |
| CL-GLP-03 | Turn gauge (the [50,216]/[0,216] init; the 32-round sim; the 0xd9; the increment; the L4 jitter documented) | gm | I8 | M G D | The gauge goldens (the fixed `simpleRand` stream); the jitter window [0.9, 1.1] over the fixed `TurnOrderRng` (the GH#2481 divergence as a D-guard) |
| CL-GLP-04 | Physical damage pipelines (the quadratic asymmetry; the 10-step chain) | gm | I8 | M G D | The pipeline goldens (the 10 steps; the player-vs-monster asymmetry golden — the D-guard against "cleaner" math) |
| CL-GLP-05 | `checkHit` (the 101-window, P = (d+1)/101 retail; the GH#2440 P = chance/100 status-roll divergence) | gm | I8 | M G D | The 101-window golden; the divergence D-guard (the status-infliction path uses chance/100 — the documented reference behavior) |
| CL-GLP-06 | Ailment lifecycle (the 3-turn tick words; the un-stun-on-damage; the poison MAX_HP/10; the `status &= 0xc8`; the two init paths) | gm | I8 | M G D | The lifecycle goldens (the scripted turn sequences); the persistence-mask D-guard |
| CL-GLP-07 | Element system (the attacker-side ×150/×50; the mutual oppositions; the thunder no-strong; the divine self-only; the packed flags 0x0–0x80) | gm | I8 | M G | The 9-element matrix golden; the packed-flag decode golden |
| CL-GLP-08 | Dabas (the dememulation; the 67-entry table; the 15-state battle; the 0x260 save contract; the formulas; the 192-boundary rewards; zero script involvement) | gm | I9 | M C I G | The machine-model suites (the 67-entry census against the documented table — the I9 headline; the formulas; the save tail round-trip; the `DabasRewardsEvent` encode/decode; the zero-script D-guard) |
| CL-GLP-09 | Encounters (the submap float accumulator + the no-abs bug; the wmap int accumulator + the 5120 threshold + the AIOOBE invariant; the deterministic trigger) | gm | I7 | M G D | The accumulator goldens (the no-abs D-guard: negative movement does not decrease); the AIOOBE D-guard (the 100-row table indexed at 100 = the documented behavior, no panic); the charm-potion reset cross-system at I9 |
| CL-GLP-10 | The reward pipeline (the ROM rewards table; the `EnemyRewardsEvent`; the 10/10 XP+DXP drains; the raw-division vs safeDiv split; the write-back) | gm | I8 (battle side) / I9 (the report + the split) | M G D | The drain goldens (the CONFIRM skip); the split golden (the vanilla 0.5); the write-back field matrix (the HP ≥ 1; the castable MP; the mask; the refund gate) |
| CL-GLP-11 | Containers/limits (the `items_2e9` cap 32 + no stacking; the `equipment_1e8` cap 255; the goods unique set + the bitset; the gold clamp; the overflow buffers; the 148-entry catalog; the 4 special classes; the 7 dragoon armors; the 7 element stones) | gm | I9 | C I G | The catalog censuses (54/148/37 — with CL-MOD-10's content); the no-stacking round-trip (the I6 persistence integration); the gold-clamp golden; the overflow → battle-scoped buffer suite |
| CL-GLP-12 | Shops (the 41-shop census; the price×2 debit; the sell-N-pays-once L4 quirk; the MERCHANT action → submap shop; the SMap:4050/5272 entry points) | gm | I9 | C I D | The shop census; the transaction goldens (the buy debit; the sell-N quirk as a D-guard); the MERCHANT path integration (the I8 exit → the submap shop) |
| CL-GLP-13 | Battle item pipeline (the DEFF-by-item-ID + the `throw_item` frame; the entrypoints 32–44; the `stor[28]/[30]/[8]`; the recovery % model; the purifiers; the buffs; the balls) | gm | I8 (the battle-side pipeline) / I9 (the item-effect families) | M G I D | The 3-stage pipeline suite (the synthetic bent: the DEFF load + the frame push + the `stor` words); the family goldens (the 0/0x8/0x10/0x40 multipliers; the % model; the 3/4-turn rule; the ball pools over the fixed `BattleRng`); the Q-89/Q-90 retail side = I10 `A` |
| CL-GLP-14 | Characters/progression (the dlevel-1 creation; the MP+20/SP+100; the eligibility = spirit-in-goods; the XP curves; the 9 elements; the additions; the 63 spells; **no RNG in any progression path**) | gm | I9 | M G D C | The progression goldens (the per-template curves; the dragoon level-up; the spell gates); the **no-RNG assertion** (the draw count = 0 — the I9 D-guard); the Q-82–Q-88 retail side = I10 `A` |
| CL-GLP-15 | Dragoon battle economy (the SP/100 drain; the pooling; the first-vs-second DEFF; the non-interlocking budget words; the d-attack + the 4-segment HUD) | gm | I8 | M G D | The economy goldens (the drain; the pooling; the DEFF selection); the budget-word semantics as a D-guard (never initialized from SP in the L4 — the F-dragooninit documented; the retail side Q-81 = I10 `A`) |
| CL-GLP-16 | Bug preservation is the default (every documented retail bug/quirk is a preserved-behavior constraint unless the table marks a reference correction) | gm | all (I6–I9) | D | **The standing D-guard program**: each per-system table row (the F-bug-1/2, the no-abs, the AIOOBE, the sell-N, the GH#1623 fade, the `setCounterOverlayRgb` wrong-key, the window-default mismatch, …) has its divergence-guard test at the owning milestone; the GH#1623/#1719/#2440/#2481 corrections are the *exceptions* with their own guards |
| CL-GLP-17 | Determinism (the gameplay surface deterministic given the seeds; the `rand()` non-reproducibility by construction; the seed-source choice documented) | gm | I8 (the replay goldens) / I9 (the progression) / I10 (the meta-test) | G D P | The tier-1/2/3 contract (planning/05 §5); the battle-replay byte-stability; the `--seed` dev path; **CD-02** is the documented choice (the divergence note) |

## 9. The 10 decision items (cross/02 §10) → CD dispositions → closure paths

Owning crate here is **where the mechanism lives** — the crate that has to compute something when the alternative is selected. The place a *choice* is declared is named in the Status column, and after 2026-11-15 it is never `psxrt-compat`.

| # | Item | CD (planning/03) | Owning crate | Decision milestone | Closure path (evidence) | Status at P0 |
|---|---|---|---|---|---|---|
| 1 | G6 same-z order (LIFO ref vs FIFO PSX) | CD-01 | rt | I5 (the ordering replay + `SameZOrder`) | Retail TMD/UI same-z overlap census + external PSX GPU documentation | **Open** (default: LIFO, parameter to FIFO on `psxrt::gpu`; the I10 `A` may close it) |
| 2 | R4/`rand()` seed source (Q-70) | CD-02 | gm | I6 (the wiring) | Original-behavior evidence (asset/external) — or the decision stands as documented | **Decided by proposal** (Option A: mirror the host-clock seed + the dev `--seed`; the user approval is the P0 gate). The seed source is a `WiringProfile` value passed into the game states, not a flag read out of a crate |
| 3 | T2 float GTE (Q-20/Q-67) | CD-03 | rt | I5 (the transform model + `TransformPrecision`) | External PSX GTE documentation + the per-camera-path integer audit (the T4 precedent) | **Open** (default: float model + bounded-divergence note; the integer variant exists for T4) |
| 4 | G4 384→368 clamp (Q-65/Q-37) | CD-04 | rt | I5 (the clamp + `PresentationProfile`) | External NTSC/PSX display documentation + the visual cross-check | **Open** (default: keep the reference clamp) |
| 5 | G11 rate-00 (Q-65) | CD-05 | rt | I1b/I3 (the decode) + I5 (the drawing path) | External PSX GPU GP0 documentation + the retail TMD/UI rate-00 census | **Open** (default: keep the reference B+F/2 mapping, which is the only mapping until a second decoder exists) |
| 6 | S7 sweep volumes (Q-68a) | CD-06 | rt | I5 (the envelope + `SweepMode`) | Retail SSSQ sweep-bit census + PSX SPU sweep documentation | **Decided by proposal — the deliberate divergence** (default: implement the sweep envelope per the documented SPU behavior, diverging from the reference's gap; flagged prominently for user attention) |
| 7 | X6 predictor continuity (Q-69a) | CD-07 | rt/im | I1 (the codec, then the transcode that calls it) | Asset observation of the first block per FMV boundary | **Open** (default: preserve the documented leak; the reset point is a parameter of `psxrt::spu::xa_adpcm`, supplied by `psxrt-import::audio::transcode`, not a policy module) |
| 8 | C-01 opcode tables | CD-08 | sdk | I4 (the dual-table module) | Per-opcode reconciliation against the retail script bytes (the I10 disassembly pass) | **Open** (default: VM = runtime table, toolchain = recompiler table, the discrepancy preserved as data) |
| 9 | Q-74 SSSQ tempo unit | CD-09 | en | I5 (the tempo code) | Retail SSSQ bytes (a tempo value + an audible duration) or original-engine documentation | **Open** (default: the reference's conversion code as-is) |
| 10 | Q-80 memcard shape | CD-10 | gm | I6 (the model) | One real PSX memcard dump | **Open** (default: the documented 0x20000 handling) |

Cross-cutting: CD-02 and CD-06 are the two rows where the *proposal itself is a decision* (the others are "keep the documented behavior + parameter + evidence path"). Every CD carries its owning crate, its blocking milestone, its parameter, and its register pointer (planning/03) — this table is the single view of their closure state at P0. **The owner column was revised on 2026-11-15**, from a wording that treated a decision as something a crate contains: a decision is recorded here and *realised* by a parameter with a named consumer, or by a writer that produces an original-format artifact. Six of the ten rows changed owner (CD-01, CD-02, CD-04, CD-05, CD-06, CD-07 — the five that named `psxrt-compat`, plus CD-02 which dropped it); four kept theirs (CD-03, CD-08, CD-09, CD-10). No default changed and no decision was dropped.

## 10. Coverage accounting

| Section | Rows | Mapped | Unmapped |
|---|---|---|---|
| §2 CL-PSX (43) | 43 | 43 | 0 (T5/X5 mapped as *non-port* — D; S9 mapped as host-internal — I) |
| §3 CL-ENG | 13 | 13 | 0 |
| §4 CL-AST | 12 | 12 | 0 |
| §5 CL-SCR | 13 | 13 | 0 |
| §6 CL-AUD | 10 | 10 | 0 |
| §7 CL-SAV | 15 | 15 | 0 |
| §8 CL-MOD | 12 | 12 | 0 |
| §9 CL-GLP | 17 | 17 | 0 |
| **Total** | **135** | **135** | **0** |

Plus the 10 decision items (§9 above) and the cross/02 §11 explicit non-constraint list (which this matrix does *not* map — by definition they carry no implementation obligation; they are the negative space the D-guards protect).

Milestone roll-up (rows whose *implementation* obligation lands at the milestone; evidence-only sub-parts accrue to I10):

| Milestone | Rows landed | Headline obligations |
|---|---|---|
| I0 | 7 verified (R1, R2-golden half, R3-algorithm, R4-algorithm, G2, G10-equations, the CL-ENG-02 timing unit) + 4 moved out (G1-dual-store, G7-arithmetic, G9-lookup, G14-upload → I5, with the VRAM and ordering model they need) | The bit-exact primitives |
| I1 | 15 (CL-AST-01..12, X1–X3, X5-nonport, X7, CL-AUD-10) | The format + codec core |
| I2 | 9 (CL-MOD-01..07-framework, CL-SAV-10..15-framework) | The SDK contract layer |
| I3 | 13 (CL-ENG-01..13 framework halves, S2, CL-AST-11, CL-ENG-02 rates) | The frame contract, headless |
| I4 | 8 (CL-SCR-01..09, 11, 12, 13; R5) | The Fate VM + toolchain |
| I5 | 27 (the G-row presentation halves, the T-rows, the S-rows, X4-decode/X6-reset, CL-AUD-01..09, CL-ENG-06/11 projection, CL-ENG-10 threads) | The technical-risk cluster, as `psxrt` modules with parameters |
| I6 | 20 (CL-SAV-01..09, CL-MOD-02/03 sites + 10 census, CL-ENG-07 content + 12 save half, CL-ENG-13 content, CL-GLP-01 title half, CL-GLP-02 wiring, R3/R4 consumers) | The first real slice |
| I7 | 7 (CL-GLP-01 submap/wmap halves, CL-GLP-09, CL-ENG-02 write sites, T4, R3/R4 legs, CL-MOD-09) | The exploration states |
| I8 | 10 (CL-GLP-01 battle half, CL-GLP-03..07, 10 battle side, 13 battle side, 15, CL-GLP-17 replay, CL-SCR-07 battle slice) | The core loop |
| I9 | 7 (CL-GLP-08, 10 report side, 11, 12, 13 item families, 14, CL-GLP-01 inventory/characters half) | The content surfaces |
| I10 | 8 (CL-SCR-10/CL-MOD-08 completion, CL-MOD-07 live, CL-MOD-11, CL-MOD-09 shipped-script, CL-AST-12 census, CL-GLP-17 meta-test, + the evidence-only sub-parts of the CD rows) | The closure |

(The roll-up counts *landing*, not first mention — a row's framework half may land at I2 while its content half lands at I6; the per-row table above is authoritative.)

## 11. The standing rules (how this matrix is maintained)

1. **A row may not be marked `verified` without a test ID** (the planning/05 §8(c) gate check — mechanical).
2. **A row may not be marked `deferred` without an evidence class + register pointer** (the deferral is an explicit register entry, not silence).
3. **The CD rows (G4, G6, G11, S7, X6, R4-seed, T2, C-01, Q-74, Q-80) carry dual status**: the *implementation* status (verified at the owning milestone — the mechanism is live, its parameter is declared at the call site that reads it, and the divergence note exists) and the *evidence* status (open until the I10 pass or external documentation closes it — the register). The implementation status never blocks on the evidence status.
4. **The P0 corrections propagate here as notes, not silent edits**: the battle-RNG identity (the 48-bit LCG, not a Mersenne Twister) and the 135-row count are the two P0 in-place corrections to the research corpus; this matrix's CL-GLP-02 note is the implementation-side statement.
5. **New rows are added only by a research correction** (a finding that changes a constraint) — the plan does not invent obligations; the register is the queue.

## 12. The I0 record (closed 2026-11-15)

The first application of rule 1, and the reason this section exists: from here on, a row's status
is carried with its evidence, not with anyone's memory of what the milestone was supposed to do.
Two crates exist — `psxrt` (fixed-point and trig, three RNG streams, texture-depth addressing,
blend arithmetic, frame cadence) and `psxrt-compat`, which at I0 was a policy framework carrying
the CD defaults as data. 72 tests run. The test-data contract, the fixture inventory and the two
fixture-reading paths are in `README.md`; enforcement is `crates/psxrt/tests/fixtures.rs` plus
`scripts/prepare_test_fixtures.sh`, which together check the inventory, the bytes, the provenance
and the target-directory copies.

**Correction, same date, and it reaches the paragraph above.** `psxrt-compat`'s I0 shape was
retired by the planning revision on 2026-11-15 — see `planning/03` §5–§6 and `planning/07` §8b.
The description "compatibility layer", the cross-subsystem `CompatPolicy` struct, and the
string-keyed ledger that wrapped it were all wrong, and the crate's actual job — emitting original
format bytes — had no implementation yet. Six §1 rows were re-owned as a result (G4, G6, G11, S7,
T2, R4). Nothing recorded in this section is withdrawn by that: 72 tests still pass, the fixture
census is unchanged, and the six re-owned rows describe mechanisms that were never built in either
crate — which is why the owner column was the thing that had to change, rather than a status.

### What is verified, and against what

| Row | Status | Check | Expected values from |
|---|---|---|---|
| R1 | **verified** | `psxrt/tests/golden_trig.rs` (constant + grid) | `testdata/generators/GoldenTrig.java` |
| R2 (golden half) | **verified** | `psxrt/tests/golden_trig.rs`, 32,770 comparisons over 16,385 angles | `trig_grid.txt`, same oracle |
| R2 (site half) | open → I5 | needs call sites to guard | — |
| R3 (algorithm) | **verified** | `psxrt/tests/golden_rng.rs` (12,000-step replay, guard branch), `properties.rs` | `rng_lcg16.txt` from `GoldenRng.java` |
| R4 (algorithm) | **verified** | `psxrt/tests/golden_rng.rs` + the range property in `properties.rs` | `rng_lcg15.txt` |
| G2 | **verified** | `psxrt/tests/bpp_exhaustive.rs` (9 tests, every word × slot per paletted depth) + round-trip and out-of-domain properties | the reference's packing expressions, digested independently |
| G10 | **verified** | `psxrt/tests/blend_exhaustive.rs` (all 65,536 pairs × 4 modes, plus the modulation sweep and its saturation census) | the reference's blend expressions, FNV digests taken from Java |
| CL-ENG-02 (unit half) | **verified** | `psxrt/tests/timing_matrix.rs` (353-row cadence matrix + the tick-gate bitmap), the composition test, `properties.rs` gate equivalence | `timing_matrix.txt`, `script_gate.txt` from `GoldenTiming.java` |

### What was moved, and why it is not a deferral

Four halves that P0 assigned to I0 were not built: the dual 15/24-bit store model (G1), the
z-insertion arithmetic (G7), the CLUT lookup and index-0 transparency (G9), and the CPU→VRAM
upload (G14). They now sit at I5, which is where the ordering table, the VRAM surface and the
shader path land together — I5's own scope has always named them.

This is a scope correction, not an evidence deferral, so it carries no register entry and no
evidence class: nothing about the original hardware is in question here — the mechanism was simply
never written. How it was recorded is worth keeping, because it is how a milestone passes its own
check on paper. The I0 scope line in `planning/04` lists five `psxrt` modules, none of them a
memory model, while the row list beside it claimed "G1 (mechanism)"; and the milestone roll-up at
§10 counted eleven rows, adding G7's arithmetic, G9's decode, G14's upload and R5 — R5 being the
Fate op bodies, whose own row has always said `sdk`/I4. Both lists now say what was built and
what was not, and nothing in the I0 suite claims a VRAM, an ordering table or an upload path.

### The claim ceiling on all of the above

Every golden above is source-derived: it proves the Rust matches the documented/reference formula
as transcribed by an independent program (`source-verified` + `experimentally verified`, per
planning/05 §3.5). None of it is `asset-validated`; no retail bytes were used, and none of these
rows needed any. Three things the fixtures settled rather than assumed, each recorded in
`testdata/fixtures.provenance` with the derivation:

- the 48-bit generator's identity with `java.util.Random` (48,000 comparisons, zero mismatches,
  re-runnable via `Random48Probe.java`) — the P0 battle-RNG correction, now executable;
- `mix32` widens a **sign-extended** int, so half of all draws are negative before the mix — the
  first implementation zero-extended and matched nothing;
- `Math` and `StrictMath` agree across the whole trig grid, which is why the open jOML question
  stays classified as immaterial instead of being guessed at.

### The rule for the next fixture-adding milestone

No fixture is shared between crates today. The first documented case where two crates will want
data out of the same source is the ADPCM core: the `psx/04` §5 worked example feeds the codec in
`psxrt` (rows X2/X3, and the S5 block goldens, which this matrix says share that codec core) and
`psxrt-import`'s XA extraction/transcode fixtures at I1. That is a shared **oracle**, not a shared
**file** — each crate gets its own fixture from its own generator, and if two byte streams ever do
come out identical, the sharing has to be declared in both manifests and then checked against one
fingerprint. `psxrt/tests/fixtures.rs` pins the current census (`psxrt: 7`, and no `testdata/`
directory in `psxrt-compat`), so an accidental cross-crate name collision fails the suite rather
than passing on a coincidence. See `README.md` §Fixtures shared between crates.
