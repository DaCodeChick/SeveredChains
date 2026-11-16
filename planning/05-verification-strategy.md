# P0-05 — Verification Strategy (Proposed)

Status: **Proposed (P0, 2026-11-14).** How the implementation (planning/04) is verified: the test layers, the golden-vector and property-test regimes, the headless harness, the determinism contract, the per-milestone verification criteria, and the evidence rules that keep the project honest about what is proven and what is not. This strategy inherits the research-phase epistemic discipline: **claims are classified (source-verified / format-corroborated / experimentally verified / asset-validated), and no implementation test can upgrade a claim beyond what its evidence allows.**

## 1. The verification principles

1. **Behavior over structure.** Tests assert documented *behavior* (the phase order, the draw order, the byte layout, the golden value), not the shape of the internal code. A refactor that preserves behavior must not break tests.
2. **The oracle is the documented contract, not the reference code.** The reference is the behavior source; the *oracle* for each test is the research document (finding ID, table, worked example). Every test names its oracle (the module doc-comment: `// oracle: M9-05b B12 — 10-step physical damage pipeline`). A test that cannot name its oracle is a bug in the test.
3. **Determinism is a contract, not a convenience.** Any behavior the documentation marks deterministic (the CL-GLP-17 families; the RNG streams; the save bytes; the patch CRC) is verified by golden artifacts that must be byte-identical across runs. Any behavior the documentation marks nondeterministic (the host-clock seeds; the MapTag iteration; the frame clock) is verified by *invariant* tests (the ranges, the ordering, the absence of corruption) — never by value equality.
4. **Source-derived fixtures; asset-validated only on real bytes.** Every fixture is derived from the documented layout (the manifest cites the layout doc). `testdata/` contains **no proprietary bytes** (no disc images, no extracted assets — the P-02 policy). The I10 asset pass is the only surface where real bytes appear, and it runs only if the user provides them.
5. **The negative is as testable as the positive.** Documented dead code, documented quirks, documented bugs (the no-abs accumulator; the sell-N-pays-once; the stage-6 absence; the AIOOBE table; the dead mix32) each have a test asserting they are *preserved* (the A13 obligation). A "fix" without a CD is a regression.
6. **No visual/sample parity claims.** The original PSX game's pixels and audio samples are not available as a baseline (the Blocked domains — cross/03 §9). Rendering and audio are verified against the *documented model* (the OT replay order, the SPU tick, the sample-accurate cadence), never against retail output. The I10 asset pass may add baseline material only if the user provides both the assets and a reference-capture procedure.
7. **Auditability.** The per-milestone gate record (the file list, the test counts, the row coverage, the claim classes, the open items) is written into the plan's log at each gate — the research-phase checkpoint discipline, continued.

## 2. The test layers

| Layer | Crate placement | What it verifies | Examples |
|---|---|---|---|
| Unit | co-located `#[cfg(test)]` | Single pure functions/structs against documented values | The Bpp decode of a 15-bit pattern; the LCG16's 10th output; the `checkHit` 101-window probability formula; the LEB127 round-trip |
| Golden vector | `tests/golden/` + `testdata/` (the manifest) | Bit-exact streams/bytes against the documented algorithm | 10,000 RNG outputs per parameterization; the `rsin` grid; the XA worked-path decode; the CTMD run-length rewrite; the 21-phase trace |
| Property | `proptest` | Invariant families over generated inputs | The Bpp round-trip; the MRG write→read identity; the decompile→assemble byte-identity; the save round-trip; the battle replay stability |
| Integration (headless) | `tests/integration/` (the binary's `tests/` for cross-crate) | The frame contract, the lifecycle, the state machines over scripted inputs | The phase-order suite; the transition + teardown; the boot chain; the full-game smoke |
| Model conformance | `tests/model/` (psxrt / psxrt-import) | The documented hardware/format worked examples | The psx/01 §7 OT trace; the psx/02 §4 GTE chains; the psx/03 tick traces; the SSSQ 16/24-channel split |
| Census | `tests/census/` | The documented counts (the data re-creation is faithful) | The 1238 registry entries (per-registry breakdown); the 70 input actions; the 88 config entries; the 353 patch entries; the 362 battle slots; the 54/148/37/41 catalogs; the Dabas 67-entry table; the wmap 256/249/133/66/794/104/49/9/6/400 geography |
| Divergence guard | `tests/divergence/` | The documented divergences and quirks are preserved (A13) | The L4 jitter RNG window; the P = chance/100 status roll; the no-abs accumulator; the sell-N-pays-once; the AIOOBE index; the stage-6 absence; the `status &= 0xc8` mask |
| Audit (repo-level) | `scripts/` (CI-invoked) | The project-wide obligations | The file-ending audit; the naming-policy scan (the placeholder-pattern detectors + the reverse-engineering map completeness); the Cargo.lock lock check; the dependency policy check (TD-24 allow-list) |

Layer discipline: unit tests never touch I/O; golden vectors are generated by a documented derivation (the generator is committed — the derivation is re-runnable) and committed as data; property tests carry their generator configs; integration tests are headless by default (the Noop backends) with an optional windowed smoke gated on a feature flag.

## 3. The golden-vector regime

1. **Manifest.** Every fixture under `testdata/` has a manifest entry: `{file, oracle (doc + finding), derivation (the generator or the hand-construction steps), seed, note}`. A fixture without a manifest is an audit violation.
2. **Generation is committed.** The golden generators (the RNG streams, the `rsin` grid, the synthetic ISO/MRG/CTMD/TIM/SSSQ/XA builders, the MDEC bitstream) are committed test utilities. Regeneration is a one-command operation; a regeneration that changes a committed golden without a doc change is a red CI (the drift alarm).
3. **The worked examples are first-class.** Where the research documents give an exact worked example (the psx/01 §7 OT trace, the psx/02 §4 GTE chains, the psx/03 §4/§6 SPU tick traces, the psx/04 §5 XA decode, the M3 round-trip hazard set, the M9 formula examples), the golden vector reproduces it exactly — the closest thing to an asset-validated oracle available without bytes.
4. **The determinism goldens.** The battle-replay traces (I8), the progression sequences (I9), and the patch-pipeline byte outputs (I10) are committed goldens. They encode the CL-GLP-17 contract: a re-run on the same seeds + inputs must be byte-identical. They are the regression net for *any* silent determinism break (a floating-point environment change, a hash-order change, a missing seed).
5. **The claim ceiling.** Golden vectors over source-derived fixtures prove `source-verified + experimentally verified` (the algorithm matches its documented form and its implementation matches the algorithm). They do **not** prove `asset-validated` (the documented form matches the retail bytes) — that remains the I10 asset pass (or, for formats where the research already has independent implementation/spec corroboration, the `format-corroborated` class stands on that corroboration, recorded in the register).

## 4. The headless frame harness (the central integration asset)

The harness (I3's deliverable, used by every later integration test) provides:

- **A scripted environment.** Input sequences (per-frame action masks, the analogue axes), asset callbacks (the fixture tree via the I1 pipeline), state scripts (the I4 VM), and observation hooks (per-phase callbacks + service-level probes: the SPU sink counter, the render-command log, the event bus tap, the RNG tap (draw count + values), the clock (the `timestamp_a0` + `vsyncMode` trace)).
- **A fixed clock.** The real-time pacing is replaced by a deterministic virtual clock (one harness "frame" = one driver iteration at the documented rate). The *real* pacing (the tick-skip, the speed multiplier) is verified separately in a dedicated suite (I3 (e)) against the documented cadence, using short wall-clock measurements with generous bounds — the pacing is a *liveness* property, not a determinism property.
- **Replay recording.** A harness run can be recorded (the input script + the seed manifest) and replayed; the recorded observation log is the golden. This is what makes the battle-replay and full-game-smoke goldens maintainable: the scenario is data, the expected output is data, and the test is "record → replay → compare."
- **The phase-trace oracle.** Every harness run emits the 21-phase trace; a standing assertion (every integration test) verifies the trace order matches platform/02 §3 (the Q-06 invariants, when the P2 census lands, are added as per-phase assertions from the census output — the harness consumes the census as a test data file).
- **The Noop backends are first-class citizens.** The Noop render/audio/input backends are not stubs: they are the *deterministic* oracles for the frame contract (the render-command log is the render oracle; the SPU sample sink is the audio oracle; the scripted input is the input oracle). The real backends (I5) are verified as *implementations of the same service surface* against the same harness (the command log of the wgpu backend must match the Noop log for the same state — the backend-parity test, gated on the backend's determinism where achievable, otherwise on the command-level invariants).

## 5. The determinism contract (CL-GLP-17, formalized)

The contract has three tiers, and each tier is tested differently:

1. **Tier 1 — Seeded streams (bit-exact, always).** The `simpleRand` (LCG16, seed 3), the `PresentationRng` (LCG15, the re-seed sites), the `BattleRng` + the `TurnOrderRng` + the snow instance (the 48-bit LCGs, the documented seeds), the spinner frame counter, the progression (no draws). *Test:* golden streams + the RNG tap in every integration test (a test that exercises a documented RNG draw site asserts the tap sequence matches the golden for the scenario's seed). The *count* of draws is also asserted (an extra draw anywhere is a regression — the tap count is part of the golden).
2. **Tier 2 — Host-seeded (invariant, never value-equal).** The host-clock seeds (the retail RNG's real seed; the turn-order jitter in the retail build; the snow in the retail submap). *Test:* the invariants (the LCG15 range ⊂ [0, 0x7fff]; the LCG16 the full 16-bit range; the bounded draws' ranges; the sequence length for the scenario). The dev `--seed` override (TD-21/CD-02) makes these streams *optionally* tier-1 in dev/test builds (the `--seed` path is tested as a dev-feature behavior: seed N → the tier-1 golden for N).
3. **Tier 3 — Inherently nondeterministic (structural invariants).** The MapTag wire order (the save is semantically stable, byte-stable only in the self-describing V10 where the field order is fixed by the writer), the frame clock, the audio device state. *Test:* semantic round-trips (save→load→save: the *model* identity; the V10 bytes are stable because the writer order is fixed — the stability is asserted, and its *source* (the fixed writer order) is documented in the test), the structural invariants (no torn state on a load failure; the event-order total order despite the parallel loads).

The contract's **meta-test:** the full determinism suite (all tier-1 goldens + the battle replays + the progression sequences) runs in the same environment twice (a `--rerun` harness mode) and diffs — this catches environment-dependent nondeterminism (hash-seed, thread scheduling that leaks into observable state) that single runs cannot.

## 6. Per-milestone verification criteria (the gate definitions)

Each milestone's gate (planning/04) is defined as: **(a)** the `cargo test` suite for the milestone's crates is green (unit + golden + property + integration), **(b)** the milestone-specific oracle suites pass (the named deliverables in planning/04), **(c)** the compatibility rows the milestone claims (planning/06) are marked `verified` with their test IDs, **(d)** the audit layer is clean (the file endings, the naming scan, the dependency allow-list), **(e)** the gate record is written (the checkpoint-log entry: the files, the counts, the rows, the claim classes, the open items). The specific load-bearing suites per milestone:

| Milestone | Load-bearing suites | The "proof of gate" artifact |
|---|---|---|
| I0 | The RNG golden (3 parameterizations × 10k); the `rsin`/`rcos` grid; the Bpp exhaustive pair; the 4 blend equations exhaustive; the timing unit table | The golden manifest v1 (the committed derivation) || I1 | The 12 format suites (incl. the ISO conformance over the synthetic 4-disc image); the pipeline determinism (the `files/` tree byte-stable); the patch-orchestration idempotence | The fixture manifest v1 (no proprietary bytes — asserted by a content scan) |
| I2 | The D-row contract suites (the 21-registry window; the 113-event-class dispatch matrix; the i18n 4-level matrix; the 12-step boot; the Tag 10-type round-trip; the config `.dcnf`/`.dpre` byte matrix) | The framework vertical slice (the synthetic first-party mod booted) |
| I3 | The 21-phase trace; the lifecycle suite (boot 14 steps + shutdown order); the teardown fixed point; the rate table; the error 4-class matrix; the edge-word protocol | The headless harness v1 (the reusable asset) |
| I4 | The VM conformance (allocation/pacing/storage/lifecycle/dispatch/ops/errors); the round-trip fidelity property (the hazard set + 100 random scripts); the meta alignment; the C-01 dual-table documentation | The round-trip gate report (the property pass + the hazard-set regression list) |
| I5 | The worked-example goldens (psx/01 §7, psx/02 §4, psx/03 §4/§6, psx/04 §5); the S3 allocation chain; the X6 continuity; the OT replay vs modern pool order traces; the G4/G13/G15 behaviors; the audio clock-domain cadence | The model-conformance report (43 rows + the CL-AUD-01..10 hosting) |
| I6 | The census (1238/70/88); the 11-magic save suite + V10 round-trip; the campaign flow; the title 11-case machine; the crash-recovery; the RNG wiring; the **end-to-end headless slice** (title→save→load) | The end-to-end slice record (the observation log golden) |
| I7 | The submap machine + media pipeline; the 144 natives (the checklist run); the encounter system (the no-abs + the weighted golden); the T4 camera bit-exactness; the wmap geography censuses + gating + navigation + encounters + the 4 special travels; the zero-script assertion | The exploration-session records (the submap + wmap integration goldens) |
| I8 | The **battle-replay goldens** (3+ scripted battles, byte-stable); the formula/pipeline goldens; the 362-slot census; the element/ailment/addition/dragoon/reward/exit suites | The battle determinism report (the replay diff = 0) |
| I9 | The catalog censuses (54/148/37/41/9/63/10×5/25+7); the no-stacking + gold-clamp + overflow suites; the item-effect family goldens; the shop transactions (incl. the sell-N quirk); the character/progression goldens (the no-RNG assertion); the Dabas 67-entry table + rules + save contract | The content-surface report + the Dabas machine record |
| I10 | The 353-entry patch pipeline (the CRC gate + idempotence + restore); the data-mod end-to-end (3-mod set over the full smoke); the i18n live re-translation; the debugger `*Source.DEBUGGER` tags; the **full-game smoke** (the composed integration); the determinism meta-test (the `--rerun` diff = 0); the asset pass (or the Blocked list); the audits | The final gate report (135/135 rows accounted; the CD statuses; the register updates; the claim-class ledger) |

The gate is **binary**: all (a)–(e) green, or the milestone is not complete. A red suite with an "acceptable" explanation is still red (the explanation goes to the register, the gate stays closed) — the research-phase discipline, continued.

### The I0 gate record (closed 2026-11-15)

**What ran.** `cargo test --workspace` — 72 tests over 8 test binaries (the two golden-vector
suites, the two exhaustive sweeps, the timing matrix, the property suite, the fixture suite, the
policy-ledger suite), no failures. `cargo clippy --workspace --all-targets` — no warnings.
`scripts/audit_file_endings.py` — no violations. `scripts/prepare_test_fixtures.sh --check` —
clean, including the target-directory copies.

**The oracle rule (§3, "the oracle is the documented contract, not the reference code").**
Held. Every expected value comes from outside the code under test: a committed generator under
`crates/psxrt/testdata/generators/`, a digest computed once from the reference's own expressions,
or an exhaustive sweep whose oracle is the reference's formula. Delivered volumes: 12,000 steps per
RNG stream (48,000 draws for the 48-bit stream), one 16,385-angle trig grid, 65,536 pairs × 4 blend
modes plus the modulation sweep, the full Bpp word/slot sweep, 352 cadence rows and 3 gate
divisors.

**Rows (gate item c).** 7 verified with their test IDs, 4 moved to I5; recorded in planning/06 §12.

**Audits (gate item d).** Naming scan: no placeholder-pattern identifier in the new code — the only
grep hit is the English word "undefined" inside a doc comment. Dependency allow-list: no runtime
dependency anywhere (`psxrt` none; `psxrt-compat` → `psxrt`; `proptest` is a dev-dependency in
both), so TD-04's std-only side holds. No proprietary bytes: the seven fixture files under
`crates/psxrt/testdata/` are machine-generated and manifest-registered, and the inventory test
fails on an unregistered file — that is §8.5 enforced at crate level, in both directions.

The §8.2 naming scan and the §8.3 map check were run by hand, because I0's scope carried the
file-ending audit only (planning/04 §I0) and no scanner was built. Naming: no placeholder-pattern
identifier in production or test code (the sole grep hit is the English word "undefined" in a doc
comment). Map: the crate's cited original identifiers are mapped in `crates/psxrt/src/REMAP.md`,
which also records the one cited original with **no** Rust item behind it — the two disagreeing
page-word encoders (`MathHelper.makeTpage` and `Graphics.GetTPage`), documented as a divergence in
`bpp.rs` and deferred to I5 with the ordering table they feed.

**Open after I0.** The R2 call-site guard (I5); the CL-ENG-02 per-context rate table (I3) and its
write-site census (I6–I9); CD-02's seed source (the I6 wiring, the I10 `A` evidence); and the §8.2
naming scanner, which stays manual until something enforces it. None of the four is asset-blocked.

**A fifth, added 2026-11-15: the citation rebuilder (no milestone owns it yet).** Roughly 1 350
citations in this corpus point at a *file and line* in one of the reference repositories, and
nothing in the workspace could tell whether one still resolves — `psxrt-compat`'s ledger test, as
shipped at I0, checked that cited `research/` paths exist and deliberately ignored
reference-repository paths, so that a reference checkout cannot become a build dependency (TD-25).
That is the right dependency rule and the wrong verification rule: a pointer that stops pointing is
worse than no pointer, because it still reads like evidence. The fix is a **citation rebuilder**
that resolves those citations against a reference checkout supplied through an environment variable,
and **skips itself when that variable is unset** — so the default `make check` stays
dependency-free and the check is opt-in for anyone who has the repos. See `planning/07` §8 for the
drift that prompted it and for what it must report.

**Update, same day: the rebuilder exists** as `scripts/citation_audit.py`, and the corpus has been
run through it against `d47ac8637`: **925 of 1 330 citations are byte-identical to the reference, 154
are open** (58 unresolvable file names, 22 changed blocks, 56 blank-line contexts, 18 range or
formatting items) and every one is itemised in `research/citation-exceptions.md` with what it needs.
Nothing was auto-rewritten: `--apply` was tried and reverted, because a second pass invents more
relocations than it resolves. See `planning/07` §8b for the measurement and the reasoning. The tool is
**opt-in by design** — `make citations` runs it, `make check` does not, and with no reference checkout
present it exits 0 having done nothing, so TD-25 holds either way. What remains open is not the tool
but the discipline: a milestone that quotes a reference line number runs the audit at the revision it
names, and `citation-exceptions.md` is where an unresolved pointer is declared rather than quietly
trusted.

**A sixth and seventh, added by the same revision — the two guards that replace the ledger's shape.**
Neither has a home yet and neither is asset-blocked. **(6) the dependency-direction guard:** a
workspace-level test that fails if any runtime crate lists `psxrt-compat` as a dependency, or if any
`psxrt` source file names `psxrt_compat` in a `use`, a `dep` edge or a doc comment. It belongs
wherever the runtime crates are enumerable — a workspace-level test target, not one crate's `tests/`
— and it is worth more than the ledger it replaces, because a ledger entry can be satisfied by a
type nobody calls while this one cannot be satisfied by anything but the real arrow. **(7) the
round-trip inventory:** for every decoder in `psxrt-import`, either a paired encoder exists and is
exercised by a `decode → encode → compare` test, or the module's doc header says why none is
planned (a format that cannot be reconstructed, or one the project has no reason to write). Without
it, "`psxrt-compat` is for exporters" quietly becomes "`psxrt-compat` is where unfinished exporters
and unowned notes go", which is the shape this revision just retired.

**Claim ceiling.** Nothing in I0 is `asset-validated` or `format-corroborated`. The seven fixtures
are `source-verified` derivations; the two identity checks that lean on a JDK (the 48-bit
generator's identity with `java.util.Random`, and `Math`/`StrictMath` agreement across the whole
trig grid) are `experimentally verified` on JDK 27, and the probe that established the first one is
committed so it can be re-established on another JDK.

## 7. The asset-validated pass (I10; the only claim-upgrade surface)

Procedure (runs only if the user provides the assets per P-02/P4; each step records its result in the register — the closure evidence for the 50 open items):

1. **Format validation (CL-AST-01..12).** For every format reader: read the real retail file, assert the documented layout (the header fields, the entry counts, the section offsets), and *decode* (the TMD parse, the TIM CLUT, the SSSQ channel split, the XA header). A layout mismatch is a research correction (the doc is updated with the retail byte evidence — the claim class of that row moves to `asset-validated`; the mismatch itself is recorded as a finding). A decode failure is a bug (or a doc gap → the register).
2. **The C-01 reconciliation (CD-08).** Disassemble the retail `STR/*.BIN` scripts; count the per-opcode occurrences against both tables (the runtime vs the recompiler); record the discrepancies (the ones the M3 xhigh audit predicted are confirmed; any new ones are register entries). This either closes CD-08 (the tables reconcile where expected) or refines it (the register).
3. **The M9 retail-side questions (Q-81–Q-98).** Against the disassembled scripts + the retail save structures (the memcard dump if provided — the Q-80 shape): each question is either answered (the doc updated, the register closed) or confirmed open (the evidence class recorded).
4. **The PSX asset censuses (Q-65 worklist; the G4/G6/G11/S7/X6/T2 evidence).** The retail TMD/UI same-z overlap census (the G6 default check); the rate-00 occurrence census (the G11); the wide-context observation (the G4); the SSSQ tempo unit (the Q-74/CD-09, with the audible duration if the user captures it); the FMV XA predictor continuity (the X6 — does retail exhibit the leak?); the `GsSetSmapRefView2L` integer camera (the Q-67 audit).
5. **The RNG seed-source evidence (Q-70/CD-02).** The retail behavior (the 59 cosmetic sites' reproducibility across sessions on real hardware is not observable from the reference; the disassembly confirms the *code path*; the decision stays as made — the asset pass records the code-path confirmation).
6. **No silent upgrades.** Every claim-class change is an explicit register update with the byte offset/evidence; the register is the ledger (the "before → after" is auditable).

If the assets are **not** provided: the pass is recorded as Blocked with the exact per-row missing-evidence list (the 50 open items, unchanged), and I10's gate (e) lists them as the open items — the implementation stands on its `source-verified`/`format-corroborated` classes, which is a complete and honest state (the cross/03 §5 position).

## 8. The audit layer (repo-level obligations, every milestone + CI)

1. **File endings** — `scripts/audit_file_endings.py` over the implementation tree (the research policy, applied: LF, one trailing newline, no CRLF, binaries untouched).
2. **Naming policy** — a scan for the placeholder-pattern classes (the AGENTS.md list: `fun_*`, `struct_1`-class, the `_80001234` address-derived, the `field_0x20` offset-derived, the `unk_*`/`unknown_*` without context) over production identifiers, with an allow-list for the documented exceptions (the reverse-engineering map modules, the serialized/registry/script-facing names, the hardware registers). A hit is a gate red (rename or add the map + the doc note).
3. **Reverse-engineering map completeness** — every original identifier that appears in a source-reference comment has a map entry (the original → the semantic name + the doc pointer); the map is per-module (`REMAP.md` or the module's doc block) and is itself audited (no dangling original identifiers).
4. **Dependency allow-list** — the `Cargo.lock` vs the TD-24 policy (the direct deps are the approved list; a new direct dep is a TD amendment, not a lock change).
5. **No proprietary bytes** — a content scan of `testdata/` (the hash allow-list of the synthetic fixtures; any file not in the manifest is a red — the P-02 policy enforced mechanically).
6. **Claim-class ledger** — the planning/06 matrix is the live document: every row's status (the doc claim class, the test ID, the milestone, the asset-pass state). The gate (c) diff-checks it (a row marked `verified` without a test ID is a red).

## 9. The adversarial-planning audit (P0's self-check, before the gate)

The P0 deliverable set is itself audited (the research-phase adversarial discipline, applied to the planning): a checklist sweep for (1) every CD has its owning crate + the blocking milestone; (2) every TD's "blocks" list is honored in the roadmap (no milestone assumes an undecided TD); (3) every planning/06 row maps to a real milestone and a real test layer (no orphan rows, no phantom tests); (4) the crate boundary rules (the SDK-no-crate-deps; the compat-depends-on-psxrt-only) hold in the proposed interface catalog; (5) no document states an `asset-validated` claim; (6) no production code is proposed in P0 (the planning-only boundary — the audit asserts the tree diff is `planning/` + the documented in-place corrections only); (7) the naming policy is respected in the planning's module sketches; (8) the file endings + the English-only rule over the new documents; (9) the P-02/R-01 boundary is not crossed anywhere (no disc images referenced as inputs to any I0–I9 test); (10) the 10 CDs' divergences-from-reference are explicitly flagged (CD-06 prominently; the rest in their cross-cutting notes); (11) the six fixed crate names appear nowhere else in altered form; (12) the "no commits by the agent" boundary is restated in the final report. Audit findings are remediated in place before the final report (the corrections are logged in planning/README.md).

## 10. What this strategy does not cover (explicitly)

- **Retail pixel/audio parity** — Blocked (no baseline; §1.6).
- **Performance guarantees** — the benchmarks (I10) establish baselines + no-regression; they do not set PSX-equivalent performance targets (the target is "flawless modern playback" — cross/03 §8: the quality target, not a speed target).
- **Security** — the trust model is single-user local (the mod code is user-installed Rust/registry content, not untrusted remote code); there is no sandbox requirement in the documented contract (the Mod-Loader's `ModContainer` isolation is layer 4 and not an obligation — recorded, not designed; a future CD if the user wants it).
- **Multi-platform** — the target is the documented one (macOS dev; the wgpu/cpal portability is a property, not a matrix). CI platform coverage is a build concern, out of scope.
- **Localization beyond the documented i18n** — the 4-level model + the working-dir override is the contract; new locales are data, not code.
