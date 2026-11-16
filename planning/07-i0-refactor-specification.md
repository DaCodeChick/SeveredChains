# P0-07 — The I0 Source Audit and the Refactor Specification

Status: **Written 2026-11-15, as part of the architecture revision. This document changes no code.** It records what the I0 tree actually contains, where each piece belongs under the revised architecture (`planning/01`), and what has to move at which milestone. Every factual claim about the code was read out of the committed files at `93c6b1b`; where an earlier draft of the planning set described a different crate — a `compat_available()` predicate, a `HashMap` lookup, a 35-entry ledger, a `RngKind`/`GameRng` pair, a `bpp::t_page_word` function — **that code does not exist**, and the description has been corrected rather than the code.

## 1. How to read this

Two rules govern everything below.

**R1 — a relocation happens with its first consumer, never before.** Moving `timing.rs` to `clock` today changes no behaviour, exercises nothing, and spends the review attention that I3's actual work needs. A move is scheduled only where a consumer arrives in the same milestone.

**R2 — a relocation must be provably behaviour-preserving.** When a move happens: the fixture bytes stay identical, every test that covered the moved code moves with it, and the workspace test count stays at its baseline (72) until new work is added on top. A move that changes a test count is not a move.

Together these mean the answer to "when does the I0 code get reorganised?" is: **at I1b, I3, I5a and I5b, in pieces** — not in a separate cleanup milestone, and not before the code that needs the new shape exists.

## 2. The I0 inventory, as committed

Two crates, both real, no placeholder crates, no placeholders for future crates.

| | `psxrt` | `psxrt-compat` |
| --- | --- | --- |
| `src/` files | 9 (`lib.rs`, `blend.rs`, `bpp.rs`, `fixedpoint.rs`, `timing.rs`, `rng/{mod,random48,lcg16,lcg15}.rs`) | 9 (`lib.rs` + 8 policy modules) |
| `src/` lines | 1 165 | 938 |
| `tests/` lines | 2 443 (7 files) | 308 (1 file) |
| test functions | 65 | 7 |
| runtime dependencies | **none** (`[dependencies]` is empty) | `psxrt` (path) only |
| dev-dependencies | `proptest 1.12.0` | `proptest 1.12.0` |
| `testdata/` | 7 fixtures + `fixtures.manifest` + `fixtures.provenance` + 4 Java oracles | none |

Source facts verified by grep, not by memory:

- **No `unsafe` anywhere in the workspace**, and `unsafe_code = forbid` at the workspace level (planning/02 TD-03).
- **No host clock in either crate**: no `std::time`, `Instant`, `SystemTime`, `Thread::`, no sleep of any kind. `psxrt` has no time source; the frame cadence is data (`planning/01` §7.1). This is the property that lets the timing module be replayed at I3.
- **No file or directory access in `src/`.** `std::fs` and `env!("CARGO_MANIFEST_DIR")` appear in test code only, for fixture loading and citation resolution.
- **No reference-repository path is resolved at build or test time.** The two tests that touch the filesystem read this repository's own `testdata/` and `research/` trees. TD-25 holds as shipped, which is why it is recorded as a rule and not as a change.
- 72 test functions across 8 binaries (65 in `psxrt`, 7 in `psxrt-compat`). `cargo clippy --workspace --all-targets` is warning-free. `make check` exits 0.

The public surface, in full, because the refactor arguments are all about it:

```
psxrt::            BlendMode, Rgb8, modulate · Bpp, Texel · PSX_DEG_PER_REV,
                   PSX_DEG_TO_RAD, TRIG_SCALE, psx_deg_to_rad, rsin, rcos ·
                   Lcg15, Lcg16, RandStream, Random48 ·
                   BASE_RATE_HZ, MIN_SPEED_MULTIPLIER, MAX_SPEED_MULTIPLIER,
                   FrameCadence, GameClock, ScriptTickGate
psxrt::blend       Rgb8::{new, from_bgr, to_bgr} ·
                   BlendMode::{ALL, ordinal, from_ordinal, channel, apply} · modulate
                   (modulate_channel is private; it is reached through modulate)
psxrt::bpp         Bpp::{ALL, code, from_code, width_divisor, width_mask, index_shift,
                   index_mask, is_paletted, index_in_word} · Texel (enum)
psxrt::rng         RandStream { STATE_BITS, restore_state, current_state } — a state
                   round-trip shape, not a draw shape: the draws are inherent methods on each
                   stream · each stream also carries inherent raw_state / restore_raw
                   (Lcg15/Lcg16/Random48) plus its own draws: Lcg16 has next_raw and
                   next_guarded(frame_count); Random48 has next_bits, next_u32,
                   next_u32_mixed, next_bounded_i32, next_f32{,_bounded},
                   next_f64{,_bounded}
psxrt::timing      FrameCadence::{new, with_vsync_count, vsync_count, rate_divisor,
                   frame_rate_hz, clock_increment_sixtieths} + 4 cadence constants ·
                   GameClock::{new, with_sixtieths, sixtieths, advance, advance_by} ·
                   ScriptTickGate::{always, every, ticks_now, advance, frames_per_tick}
psxrt-compat       Confidence, Layer, Availability, PolicyRecord, LEDGER (16 entries),
                   CompatPolicy (4 fields), reference_fidelity(),
                   SameZOrder (+ draw_position), GtePrecision (+ 2 scale constants),
                   PredictorReset, SweepEnvelope, VoiceCapacity (+ bound), Phase,
                   blend_select::{RECORD_EQUATIONS, RECORD_MASKING},
                   display_clamp::{CLAMPED_WIDTH, WIDE_WIDTH, BASE_WIDTH, BASE_HEIGHT,
                                    clamp_draw_width, 3 records},
                   rate_00::{NO_TRANSLUCENCY, translucency, page_translucency,
                             flag_translucency, PAGE_TRANSLUCENCY_SHIFT,
                             FLAG_TRANSLUCENCY_SHIFT, RECORD},
                   predictor::{7 constants, 4 records}
```

Two things this list shows, which the audit's findings depend on. `RandStream` exposes **state, not draws** — so a replay harness can snapshot and restore a stream without any generator knowing it is being recorded. And `psxrt` has no clock, no device, no transport, and no VRAM: everything I5a adds to it is new surface, not a change to this list.

## 3. What I0 already gets right

Recorded first, because three of these are the things the revision *keeps*.

1. **`BlendMode::HalfBackgroundPlusHalfForeground` is a description, not a transliteration** — the naming policy's requirement, met at the source. Same for `from_code` returning `Option` where "there is no fifth depth" is the real domain fact.
2. **The oracle independence discipline held.** `trig_strictmath_divergences.txt` records 61 float-ordering divergence cases *against* the implementation rather than from it; `blend_exhaustive.rs` pins FNV digests computed from Java expressions independently; `timing_matrix.rs` is driven by a transcribed oracle (`testdata/generators/GoldenTiming.java`), not by Rust output.
3. **`policy_ledger.rs` tests consequences, not wording.** It embeds two documents from outside the crate (`research/cross/02-compatibility-checklist.md` and `research/04-unresolved-questions.md`) with `include_str!`, and then checks three things as *data*: every `rows:` citation must still match a real checklist row (16 distinct rows are cited: G3, G4, G6, G10–G13, T1, T2, S2, S3, S7, X1, X4, X6, X7), every `register:` token must still name a real register item (12 distinct `Q-` items), and every `research/…` path named as evidence must still exist. It also fails if a record loses its divergence note or its closure condition, and it asserts that the knob surface is exactly the fields of `CompatPolicy`. **The revision retires the framework's shape and keeps every one of those checks**, redirected: a rewritten ledger test at I5a still resolves citations and still refuses a knob with nothing behind it.
4. **No placeholder crate and no placeholder module.** `psxrt-compat`'s 16 records were 83 lines of `lib.rs` plus real content per module; nothing existed to satisfy a count.
5. **The one framework property the revision depends on already holds:** `psxrt-compat` has no vtable in its public API and reaches no device — its eight modules are enums, constants and records. Its only project dependency is a path reference to `psxrt`, and that edge is used by **exactly one module** (`rate_00.rs`, for `psxrt::BlendMode`); the reference in `blend_select.rs` is a doc-link, not a code dependency.

## 4. Findings and dispositions

Eleven findings. Each names the evidence, the target, and the milestone. "Repath" means the code does not change; the path and the consumer do.

### F1 — `fixedpoint.rs` is not fixed-point arithmetic; it is the trigonometric path

121 lines: the π/180 factor (`PSX_DEG_TO_RAD`, computed as a pre-rounded product, not recomputed), `TRIG_SCALE = 4096.0`, `psx_deg_to_rad`, `rsin`, `rcos`, the crate-private `sine`/`cosine`, and `TrigonometryModel`. There is no fixed-point type, no Q-format helper, no scaling utility for anything else. The misnomer has already propagated one level up: `crates/psxrt/Cargo.toml` describes the crate as "fixed-point math, random streams, pixel formats, blend arithmetic, frame timing", which is the filename read as a responsibility.

**Target:** `psxrt::math::trig`, at **I5a** (when `gte` needs it). `sine`/`cosine` stay module-private; only `sine` is exercised directly by `tests/golden_trig.rs`, so the private status is real, not decorative.
**Do not** create `math/` before I5a (R1).

### F1a — and it carries one stale pointer

The module doc comment names `tests/golden_fixedpoint.rs` as the file that pins the angle factor. **That file does not exist**; the trig golden vectors are in `tests/golden_trig.rs`. It is one line, in a comment, and it is the only reference of its kind in either crate (`psxrt-compat/src/lib.rs` points at `tests/policy_ledger.rs`, which does exist) — found by grepping doc comments for test paths, which is not a check anyone was running.

It matters for one reason beyond tidiness: the sentence's whole purpose is "a change to how it is computed cannot slip through", and a pointer to a file that is not there is exactly the shape that stops being checked and keeps reading like a guard. **Fix:** correct the path when the module is repathed at I5a, or earlier if the owner wants a one-line change now. Do not fix it by deleting the sentence.

### F2 — `TrigonometryModel` is a one-armed enum with no caller

Grep-verified: `TrigonometryModel` appears in `fixedpoint.rs` and nowhere else — no production call site, no test reference. Its single arm `FloatExpression` is the reference's path (float `MathHelper.sin/cos` scaled by `0x1000`); the path it does *not* model is the original machine's, whose 456-entry ROM sine table (at `80015c80`) is not in the repository and whose only documented indexing rule is `theta & 0x3ff` after a +512 offset for the cosine. The module doc comment says all of this, and says it in order to refuse the seam rather than to fake one.

**Disposition:** this is the "placeholder enum" case `planning/01` §8 refuses. It is kept *only* because F1's repath carries it, and only if the I5a table work is real. **Decision needed from the owner (§7 Q1):** delete it now and let the table work introduce its own seam, or keep it as documentation of a seam nobody has walked. The argument for keeping it is that the doc comment is the only place the +512/index-`0x3ff` facts are recorded in code; the argument for deleting it is that an enum with one arm and no reader cannot be wrong and therefore cannot be right either.

### F3 — the angle-unit module claim in `planning/01` §4 overstated I0

I0 delivers the 4096-unit turn factor and the trig scale. It does not deliver the 65536-unit turn, the wrapping rules, or conversions between the two units. `planning/01` §4 has been corrected to say so; the `math::angle` row is now "I0 (the factor only) → I5a". No code change.

### F4 — `bpp.rs` models the depth *field*; the page-word encoding is not modelled, and that is correct

`Bpp` carries the depth code (0/1/2/3, where codes 2 and 3 both read 16-bit words and code 3 is the no-palette mode) and the addressing parameters that follow from it — `width_divisor` is 4/2/1/1. The TPage word that *packs* depth with CLUT base, page x/y, and the translucency bits is not assembled or decoded anywhere in the tree, because the two reference encoders disagree about the packing. `crates/psxrt/src/REMAP.md` documents that disagreement instead of inventing a resolution, and the earlier planning claim that a `bpp::t_page_word` function exists has been deleted.

**Target:** `psxrt::gpu::pixel` at **I1b** (the colour paths arrive with TIM/CTMD). The page-word decode lands in `psxrt::gpu` at **I3**, and it must arrive as **two** functions if the two-encoder disagreement is still unresolved then — one per encoder, with the call sites deciding. Collapsing them into one "correct" decoder is the failure mode this note exists to prevent.

### F5 — `blend.rs` and `rate_00.rs`: the equations are in the right crate, the decoder is in the wrong one

`psxrt::blend` holds four equations plus `modulate`/`modulate_channel`, and `blend_exhaustive.rs` covers all 65 536 channel pairs per mode with an exhaustive check. That is representation (Kind B), and it is where it belongs.

`psxrt-compat::rate_00` holds `translucency(i32)`, `page_translucency(u32)`, `flag_translucency(u32)` — all returning `Option<BlendMode>` — plus `NO_TRANSLUCENCY` and the two shift constants. **These decode. They do not choose.** A reader who wants to know "which blend applies to this draw" has to know that a `u32` flag word's bits 28-29 and a TPage word's bits 5-6 encode the same four modes, and that fact is currently filed under *policy*.

**Target:** the three decoders + 3 constants → `psxrt::gpu` at **I3** (with F4's page-word work); the "rate-00 is real translucency" divergence → `DIVERGENCE.md`. The flag that *chooses* the decode model stays in `psxrt-compat`, and its name must say "decode model", not "emulation" (CD-05).

### F6 — `predictor.rs` is a format header holding one policy

Seven constants, six of which are XA layout facts: `SOURCE_RATE_HZ = 37_800`, `HEADER_BYTES = 24`, `CODING_INFO_OFFSET = 19`, `PORTIONS_PER_SECTOR = 18`, `PORTION_BYTES = 128`, `SAMPLES_PER_SECTOR_LANE = 2_016`; the seventh, `MAX_BOUNDARY_LEAK_SAMPLES = 28`, is a measured divergence bound, not a format fact.

**Target:** the six → `psxrt::spu::xa_adpcm` at **I1b** (with the codec). `MAX_BOUNDARY_LEAK_SAMPLES` → the divergence note beside the codec. `PredictorReset` stays as a policy and takes `psxrt::spu` as its consumer at I5a. The five-vs-six count in the first draft of `planning/03` §5 is corrected there.

### F7 — `display_clamp.rs` mixes a layout fact with a profile choice

`CLAMPED_WIDTH = 368`, `WIDE_WIDTH = 384`, `BASE_WIDTH = 320`, `BASE_HEIGHT = 240`, `clamp_draw_width`. The 384→368 clamp has one correct answer per mode; the *mode* is the choice.

**Target:** the constants + the function → `psxrt::gpu` at **I3**; the choice → a `PresentationProfile` discriminant at L2, owned by the engine crate's backend selection (**I5b**), consumed by `psxrt::gpu`. The two-distinct-368s note travels with the function, verbatim (CD-04).

### F8 — `voice_alloc.rs` holds a runtime data structure, not a policy

`VoiceCapacity` is an enum with a `bound(phase: Phase) -> u32` method over a `Phase` enum. It is state-bearing arithmetic over a hardware capacity table (Kind C).

**Target:** `VoiceCapacity` + `Phase` → `psxrt::spu` at **I5a**; the allocation *policy* (which phase table applies, and when the 12/16 boundary moves) stays in `psxrt-compat` with `psxrt::spu` as consumer.

### F9 — `same_z.rs` is the one module whose function really is its policy

`SameZOrder::{Lifo, Fifo}` plus `draw_position(insertion_index, count) -> Option<usize>`. The function is the two orderings expressed as index arithmetic, so it moves *with* the selector rather than being split from it: a policy selector that has to be re-derived by each caller is not a selector.

**Target:** stays in `psxrt-compat`, consumed by `psxrt::gpu`'s ordering-table replay at **I5a**, and it needs a modern-path counterpart in the same milestone or the flag is a flag and not a model (CD-01).

### F10 — the framework: what dies, what is redirected

Dies: `PolicyRecord` as a shipped type, the 16-entry `LEDGER` as a table, `CompatPolicy` as the wholesale four-field struct, `Availability`'s two states as a way of talking about *alternatives* (they describe defaults). The `psxrt → psxrt-compat` direction question dies with them, because no `psxrt` module will ever read a policy: each takes an explicit parameter, defaulting to the modern behaviour.

Redirected, keeping the check: the citation-resolution test (it must still resolve every `planning/` and `research/` citation after the moves), the "no record claims retail validation" test, the ledger-census test (reworded: no flag appears without an implementation *and* a consumer), and the reference-fidelity comparison (the shipped-default set and the reference-faithful set differ in exactly one field — the sweep envelope — and that statement keeps a test).

## 5. The move table

| Now | Target | Crate | Milestone | Kind of change |
| --- | --- | --- | --- | --- |
| `psxrt/src/fixedpoint.rs` | `psxrt/src/math/trig.rs` | `psxrt` | I5a | repath; `TrigonometryModel` disposition per Q1 |
| (the 4096-factor pieces stay with trig) | `psxrt/src/math/angle.rs` | `psxrt` | I5a | **new** work: the 65536 unit, wrapping, conversions |
| `psxrt/src/bpp.rs` | `psxrt/src/gpu/pixel.rs` | `psxrt` | I1b | repath + colour paths added |
| `psxrt/src/blend.rs` | `psxrt/src/gpu/blend.rs` | `psxrt` | I3 | repath, equations unchanged; exhaustive test moves with it |
| `psxrt/src/timing.rs` | `psxrt/src/clock.rs` | `psxrt` | I3 | repath; no phase hooks, no host sleep |
| `psxrt-compat/src/rate_00.rs` (decoders) | `psxrt/src/gpu/translucency.rs` | `psxrt` | I3 | relocation of Kind-B code out of the policy crate |
| `psxrt-compat/src/display_clamp.rs` (constants + clamp) | `psxrt/src/gpu/display.rs` | `psxrt` | I3 | relocation; the profile choice goes to the engine crate at I5b |
| `psxrt-compat/src/predictor.rs` (6 constants) | `psxrt/src/spu/xa_adpcm.rs` | `psxrt` | I1b | relocation |
| `psxrt-compat/src/voice_alloc.rs` (`VoiceCapacity`, `Phase`) | `psxrt/src/spu/voice.rs` | `psxrt` | I5a | relocation |
| `psxrt-compat/src/{same_z,gte_precision,sweep}.rs` | stay | `psxrt-compat` | I5a | gain a named consumer; nothing moves |
| `psxrt-compat/src/lib.rs` | decomposed | — | I1a (`compat` vocabulary), I5a (ledger) | retirement, per `planning/03` §6 |
| `psxrt/src/REMAP.md` | stays; `psxrt-compat` gains none | both | — | it needs one only when a module cites an original code identifier; today it cites checklist rows (`G12`, `S7`, `X6`), not identifiers |

Test files move with their subject, unchanged: `golden_trig.rs` with F1, `bpp_exhaustive.rs` with F4, `blend_exhaustive.rs` with F5, `timing_matrix.rs` with the clock repath, `properties.rs` splits along the same seams, `fixtures.rs` stays where the manifest is. `policy_ledger.rs` is rewritten at I5a, not deleted (§4/F10).

**The dependency consequence.** `psxrt-compat` declares `psxrt` as a dependency, and exactly one file uses it: `rate_00.rs`, which imports `psxrt::BlendMode` in order to *decode a flag word into* that type. Once F5 moves the three decoders to `psxrt::gpu`, nothing in `psxrt-compat` references `psxrt` at all — so the `Cargo.toml` edge is deleted at **I3**, and `psxrt-compat` becomes a crate with zero project dependencies. That is the architecture requirement "`psxrt-compat` must not require a running engine" met literally rather than by argument: a policy crate that depends on nothing cannot be dragged down to a device.

**The rule that follows, for any later policy module:** do not let a policy crate depend on a runtime crate in order to reuse a type. Where both sides genuinely need one enum, that enum is a representation type and lives in `psxrt`, and the policy module names a *selection between implementations* instead of sharing the vocabulary. `rate_00` is the case that proves the rule: the shared type existed only because a decoder had been parked in the wrong crate.

## 6. Verification after each move

The baseline is **72 tests / 8 binaries / 0 clippy warnings / 7 fixtures byte-identical**. A milestone that performs a move re-runs `make check` and additionally confirms:

1. fixture bytes and FNV digests unchanged (so an oracle was not quietly re-derived);
2. every relocated test still asserts the same predicates — an exhaustive test stays exhaustive; a matrix fixture stays a matrix fixture;
3. `REMAP.md` gained a line for every new identifier comment, and lost its line for anything removed;
4. the property tests still fail if reverted: the recorded shrink seeds in `crates/psxrt/tests/properties.proptest-regressions` are replayed first, so a regression in the LCG16 guard or the Bpp packing surfaces without new work;
5. the naming scan (`planning/05` §8.2) is still run by hand, since no scanner exists.

**No I0 test takes retail bytes as input, and none may start to.** All 7 fixtures are synthetic and oracle-generated. That boundary is P-02's, not this revision's.

## 7. Open questions for the owner

Three here, plus a fourth in §8. None is answerable from the repository alone.

**Q1 — `TrigonometryModel`: keep or delete?** (§4/F2). Recommendation: **delete at the I5a repath**, keep its doc comment as a header note on `math::trig`, and let the table work introduce its own seam when there are two arms to choose between. The alternative is defensible only if the owner wants the +512 / `0x3ff` facts to stay in code rather than in `research/psx/02-gte.md`.

**Q2 — do the per-subsystem matrix fixtures hold one fixture per checklist *row* or one per *policy*?** `research/cross/02` holds 135 rows (43 carried from M4-05 + 92 `CL-*`), and 16 of them are cited by the I0 ledger records (disposed in §4 and `planning/03` §5). The revision classifies the rows; it does not delete any, and `planning/06` still maps all 135. The open question is only the fixture shape at I5a/I5b. Recommendation: one per policy, because three of the ten CDs share a mechanism; the row→fixture mapping stays in `planning/06`.

**Q3 — script the naming/map scanner now, or keep it manual?** It is the only gate item in `planning/05` §8 that has no implementation. Recommendation: keep it manual through I3 and revisit at I5a, when there are enough modules for a scanner to pay for itself. Until then, the REMAP rule is enforced by review, and this document is where that is recorded.

**Not a question, a correction to note:** the earlier draft of this audit described a `HashMap`-based `compat_available()` and a `CompatPolicy` keyed by `PolicyId`. Neither exists. If a future milestone is justified by "fixing" them, that justification is void; the real findings are F1–F10 (ten labels, eleven entries: F1a sits under F1).

## 8. Reference-repository drift — found during the final check, 2026-11-15

**The fact.** `Severed-Chains` is checked out at `d47ac8637`. The revision recorded for it by `research/01-repository-inventory.md` is `0c08c679b` (2026-11-02), and `research/audits/r0-r1-xhigh-audit.md` records all four repositories as "verified at pinned revisions". The other three are still exactly at their recorded pins (`3433e6f74`, `c69213490`, `4c1fbfebc`). All four are **unmodified** — zero dirty files, so nothing in this project has written to a reference repository. The recorded pin is an ancestor of the current checkout, which sits **four commits and 838 inserted lines** above it (`Addition Practice` #2907, `Quantity Discard/Buy/Sell` #2915, `Equipment Filtering` #2908, the infinite-symbol item-list fix #2917). The `git reflog` of that repository shows the checkout moved on **2026-11-12** (to `71ce13036`) and again on **2026-11-15** (to `d47ac8637`) — both by the user pulling upstream, not by anything in this workspace.

**What that costs.** *(Prototype-era counts, superseded by §8b — kept because they are what triggered the investigation; the authoritative census is `research/citation-exceptions.md` §2.)* 26 Java files changed in those four commits, several of them load-bearing for the corpus: `SItem.java` (+36), `Battle.java`, `SMap.java`, `WMap.java`, `LodMod.java`, `EquipmentScreen.java`, `ShopScreen.java`, `ItemListScreen.java`, `CycleMessageBoxScreen.java` (new, +348), `QuantityMessageBoxScreen.java` (new), `CoreMod.java`, `ItemShopExtension.java`. Automated over the corpus: **1 346 citations** point at a file-and-line inside a drifted file; **936 of them resolve identically at both revisions** (the changed lines are elsewhere in the file); about **261 point at different text now than they did at the recorded pin**. All of those counts come from a scripted line-content comparison, and the token heuristic behind them is noisy — it flags prose words like "parallel" and "called" — so the numbers bound the problem, they do not adjudicate it.

**What it does not cost.** A second check, deliberately blunt: for every citation whose quoted identifier could be tested, does that identifier still exist anywhere in the target file? **Nothing has disappeared.** The drift moves *pointers*, not *facts* — the upstream commits add features (quantity buy/sell, equipment filtering, an addition-practice handler) rather than rewriting the behaviour the research documents describe. Two specific checks were then done by hand, because both feed a planning claim: the config-entry census still reads **88** (`CONFIG_REGISTRAR.register` appears 80 times in `CoreMod.java` at *both* revisions; `LodConfig` and `TurnOrderConfigs` are unchanged in the relevant respects), so the correction recorded in `planning/README.md` §2 does not depend on which revision is checked out. One content change did surface, and §8b item 4 corrects where it lives: the listener census in `research/modding/02-registries-and-events.md` is 7 classes at the recorded pin and **8** at `d47ac8637`. This paragraph originally attributed the addition to the `debugger/` package — wrong; the four debugger controllers were already listeners at the pin. The new one is `lodmod/LodTrainingHandler.java`. That document now carries a "re-count before implementation" note at that table (§4.3), which is the mechanism working.

**The gap this exposed.** No mechanism in the workspace could see this class of staleness, and one was *designed not to*: `psxrt-compat`'s `document_paths()` helper takes only `research/…` paths out of a citation string, explicitly so that a named reference file cannot become a build or test dependency. That reasoning is right (TD-25) and it left ~1 350 pointers with no checker.

## 8b. What was executed on 2026-11-15 — the checker, and why the pointers were left alone

The owner chose **re-pin forward to `d47ac8637`** rather than reverting the working copy. The counts
in §8 came from the first prototype and are superseded by §8c; nothing touches `crates/`.

**1. The checker exists: `scripts/citation_audit.py`.** It resolves a citation by locating the cited
file inside the repository named by the surrounding prose (a path containing `/` must match exactly; a
bare file name that matches more than one path is reported `AMBIGUOUS-REPO` rather than guessed), then
compares the **exact text** at the cited lines between two revisions: identical and unique →
`RESOLVED`; byte-identical at exactly one other place → `MOVED` (a pointer problem); otherwise
`CHANGED` (a content problem, never auto-fixed). Classification is by block identity, not by scanning
for quoted tokens — the §8 prototype used an identifier heuristic and produced 324 mostly-spurious
reports before it was replaced. Reference repositories are reached only through `--repos-root` /
`SEVERED_CHAINS_REPOS`; with neither set the script reports and exits 0, so the default gate stays
dependency-free (TD-25). Exposed as `make citations`, deliberately outside `make check`.

**2. Automatic re-pointing was tried and reverted.** 206 pointers were rewritten in three experimental
passes — 178 by block identity, 21 by an identifier-anchor pass, 7 by hand — and the result was
**rolled back file by file**, for two reasons. The passes did not converge: after 131 rewrites the
`MOVED` set refilled with 101 new entries and 29 citations moved from *matching* to *not matching*, so
the rewrite was manufacturing work, not removing it. Worse, the first sweep corrupted three 40-character
commit hashes into 46-character strings that name no commit — the exact class of damage a "helpful"
auto-fix produces. The corpus therefore stands with its citations **as committed**. Where a pointer was
known to be wrong it was fixed by hand from printed source, not by a scanner.

**3. The residue is a register, not a silence.** `research/citation-exceptions.md` records 925
verified citations and 154 open ones with their class, concentration and what to re-read. It also
withdraws two entries an earlier draft of it asserted — a `loadEnemyEvent` → `loadAndRenderSubmapModels`
chain attributed to `game/05a`, and four `tmd/Renderer.java` citations attributed to `rendering/02`.
**Neither identifier exists in the reference repository at either revision, and neither document contains
those citations.** They had been transcribed from the noisy prototype's output without opening the
documents. The real finding in that area is a ~5-line offset on `Battle.java:9326`, whose method
(`loadEnemyDropsAndScript`, `@ 0x80109808`) is intact and still does what `game/05b` §3.3 says —
including posting `LoadEnemyEvent` with its replaceable `scriptFuture`.

**4. One content change did survive verification, and it is in the event layer.** The listener census
in `research/modding/02-registries-and-events.md` moves **7 classes → 8** at `d47ac8637`: the new one is
`legend/lodmod/LodTrainingHandler.java` (Addition Practice, #2907), instance-registered at
`LodMod.java:312`, whose `handleLoadEnemy` replaces `LoadEnemyEvent.scriptFuture`. `LodMod` also
**lost its class-level `@EventListener`** (2 such sites at the baseline → 1 now; the survivor is
`CoreMod.java:95`, which the document cites correctly). Explicit `priority = …` is still unused, so the
document's `NORMAL`-only finding holds. That document is the evidence behind CL-ENG-02, which
`policy_ledger.rs` pins by embedding — the mechanism working as designed.

**Honest limit.** "925 resolve" means the quoted text is still there at `d47ac8637`, not that every
claim made about it still holds. 154 citations are open, 73 cannot be checked at all (no baseline for
`Mod-Loader` / `Dig-Dabas`), and the 5-line offset above was found only because five `CHANGED` entries
were opened by hand. A citation into one of the four drifted areas (`ShopScreen`, `LodMod`, `Battle`,
`SMap`) is unverified until re-read at whatever revision is checked out then.

**5. What was *not* rewritten: the `at <sha>` markers.** Each still names the revision its document was
**written** against, including the two audit transcripts, which are evidence records of a pass run at
`0c08c679b`. Rewriting those to HEAD would have made the corpus *look* verified while destroying the
information about what was checked when. The revision citations are *verified against* is stated once
(`research/01` §7, `AGENTS.md`); provenance stays local and immutable.
