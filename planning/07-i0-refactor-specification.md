# P0-07 — The I0 Source Audit and the Refactor Specification

Status: **Written 2026-11-15, as part of the architecture revision. This document changes no code.** It records what the I0 tree actually contains, where each piece belongs under the revised architecture (`planning/01`), and what has to move at which milestone. Every factual claim about the code was read out of the committed files at `93c6b1b`; where an earlier draft of the planning set described a different crate — a `compat_available()` predicate, a `HashMap` lookup, a 35-entry ledger, a `RngKind`/`GameRng` pair, a `bpp::t_page_word` function — **that code does not exist**, and the description has been corrected rather than the code.

## 1. How to read this

Two rules govern everything below.

**R1 — a relocation happens with its first consumer, never before.** Moving `timing.rs` to `clock` today changes no behaviour, exercises nothing, and spends the review attention that I3's actual work needs. A move is scheduled only where a consumer arrives in the same milestone.

**R2 — a relocation must be provably behaviour-preserving.** When a move happens: the fixture bytes stay identical, every test that covered the moved code moves with it, and the workspace test count stays at its baseline (72) until new work is added on top. A move that changes a test count is not a move.

**R3 — a relocation is decided by what the code *is*, not by which crate its name suggests.** Three of the eleven findings below end in "delete the file, re-express its content as a doc header plus a traceability row", and that is deliberate. A relocation preserves behaviour; a deletion of a declaration that has no consumer preserves nothing, because there was no behaviour to preserve. Where this document says *delete*, it also names where the information goes, and the information is the part that was worth having.

Together these mean the answer to "when does the I0 code get reorganised?" is: **at I1a, I1b, I3, I5a and I5b, in pieces** — not in a separate cleanup milestone, and not before the code that needs the new shape exists. The one exception runs the other way: the `psxrt-compat` policy scaffold is dismantled at **I1a**, together with the crate's first real module, because leaving a retired scaffold beside a new toolchain module makes the crate's purpose unreadable to whoever adds the third module.

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
3. **`policy_ledger.rs` tests consequences, not wording.** It embeds two documents from outside the crate (`research/cross/02-compatibility-checklist.md` and `research/04-unresolved-questions.md`) with `include_str!`, and then checks three things as *data*: every `rows:` citation must still match a real checklist row (16 distinct rows are cited: G3, G4, G6, G10–G13, T1, T2, S2, S3, S7, X1, X4, X6, X7), every `register:` token must still name a real register item (12 distinct `Q-` items), and every `research/…` path named as evidence must still exist. It also fails if a record loses its divergence note or its closure condition, and it asserts that the knob surface is exactly the fields of `CompatPolicy`. **The revision retires the framework's shape and keeps every one of those checks**, redirected into `scripts/citation_audit.py` (which resolves the same class of pointer, over the whole corpus, without a Rust harness) and into the per-subsystem matrix fixtures. What it does not keep is the idea that a compiled table is the right home for a citation.
4. **No placeholder crate and no placeholder module.** `psxrt-compat`'s 16 records were 83 lines of `lib.rs` plus real content per module; nothing existed to satisfy a count. The point cuts against the crate's purpose, not for it: the modules are real *files*, and a file can be honest documentation and still be the wrong shape for compiled code.
5. **The one framework property the revision depends on already holds, and it is what makes deletion cheap:** `psxrt-compat` has no vtable in its public API and reaches no device — its eight modules are enums, constants and records. Its only project dependency is a path reference to `psxrt`, and that edge is used by **exactly one module** (`rate_00.rs`, for `psxrt::BlendMode`); the reference in `blend_select.rs` is a doc-link, not a code dependency. Because nothing calls any of it, deleting it cannot break a caller — and because the checks in item 3 are being redirected rather than dropped, nothing learned is lost either.

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

`psxrt::blend` holds four equations plus `modulate`/`modulate_channel`, and `blend_exhaustive.rs` covers all 65 536 channel pairs per mode with an exhaustive check. That is a representation, and it is where it belongs.

`psxrt-compat::rate_00` holds `translucency(i32)`, `page_translucency(u32)`, `flag_translucency(u32)` — all returning `Option<BlendMode>` — plus `NO_TRANSLUCENCY` and the two shift constants. **These decode. They do not choose, and they do not encode.** A reader who wants to know "which blend applies to this draw" has to know that a `u32` flag word's bits 28-29 and a TPage word's bits 5-6 encode the same four modes, and that fact is currently filed under *policy*.

**Target:** the three decoders + 3 constants → `psxrt::gpu::pixel` at **I1b** (they arrive with the colour paths they decode), or at **I3** if the colour paths slip; the "rate-00 is real translucency" divergence → `DIVERGENCE.md`. **Nothing about this stays in `psxrt-compat`.** A decoder is not the inverse of an encoder for placement purposes, and the actual inverse here — RGBA8 → the 2-bit index encodings, needed to *emit* a TIM — is `psxrt-compat::texture`, built alongside these and reading the same four modes from `gpu::pixel`. If the flag that chooses the decode model is ever needed it is a parameter of `gpu::pixel` naming a *decode model*, never a name containing "emulation" (CD-05).

### F6 — `predictor.rs` is a format header holding one parameter

Seven constants, six of which are XA layout facts: `SOURCE_RATE_HZ = 37_800`, `HEADER_BYTES = 24`, `CODING_INFO_OFFSET = 19`, `PORTIONS_PER_SECTOR = 18`, `PORTION_BYTES = 128`, `SAMPLES_PER_SECTOR_LANE = 2_016`; the seventh, `MAX_BOUNDARY_LEAK_SAMPLES = 28`, is a measured divergence bound, not a format fact.

**Target:** the six → `psxrt::spu::xa_adpcm` at **I1b** (with the codec), declared **once** and read by both directions — by `psxrt-import::audio::transcode` on the way in and by `psxrt-compat::audio` on the way out, which is the only arrangement that makes the round-trip gate meaningful. `MAX_BOUNDARY_LEAK_SAMPLES` → the divergence note beside the codec. `PredictorReset` becomes a **decoder parameter of `xa_adpcm`**, not a `psxrt-compat` module: its two values describe how a decoder carries state across a boundary, and a fixture can set one without a policy layer. The five-vs-six count in the first draft of `planning/03` §5 is corrected there.

### F7 — `display_clamp.rs` mixes a layout fact with a profile choice

`CLAMPED_WIDTH = 368`, `WIDE_WIDTH = 384`, `BASE_WIDTH = 320`, `BASE_HEIGHT = 240`, `clamp_draw_width`. The 384→368 clamp has one correct answer per mode; the *mode* is the choice.

**Target:** the constants + the function → `psxrt::gpu` at **I3**; the choice → a `PresentationProfile` discriminant at L2, owned by the engine crate's backend selection (**I5b**), consumed by `psxrt::gpu`. The two-distinct-368s note travels with the function, verbatim (CD-04).

### F8 — `voice_alloc.rs` holds a runtime data structure, not a policy

`VoiceCapacity` is an enum with a `bound(phase: Phase) -> u32` method over a `Phase` enum. It is state-bearing arithmetic over a hardware capacity table.

**Target:** `VoiceCapacity` + `Phase` → `psxrt::spu` at **I5a**, and so does the capacity question that was going to be left behind in `psxrt-compat` (which phase table applies, and when the 12/16 boundary moves). Splitting a capacity table from the structure that reads it produces a toolchain crate containing a `pub` type whose only purpose is to be instantiated by the runtime — the dependency pointing the wrong way in its smallest possible form.

### F9 — `same_z.rs` is the one module whose function really is its selector, and that is why neither half belongs in `psxrt-compat`

`SameZOrder::{Lifo, Fifo}` plus `draw_position(insertion_index, count) -> Option<usize>`. The function is the two orderings expressed as index arithmetic, so the two halves cannot be separated: a selector that has to be re-derived by each caller is not a selector, and an arithmetic helper stranded in a toolchain crate is a file whose reader has to guess which crate executes it.

**Target:** both halves to `psxrt::gpu`'s ordering-table replay at **I5a**, with `SameZOrder` as the parameter, and a modern-path counterpart in the same milestone or the knob is a knob and not a model (CD-01). Grep-verified as with all of these: `SameZOrder` currently has no caller outside its own crate, so this is a decision about where I5a will write the ordering, not a relocation of working code.

### F10 — the framework: what dies, and what is redirected

Dies, at **I1a**, as a block: `PolicyRecord` as a shipped type, the 16-entry `LEDGER` as a table, `CompatPolicy` as the wholesale four-field struct, and `Availability`'s two states as a way of talking about *alternatives* (they describe defaults). `Confidence` and `Layer` survive only as vocabulary — in `DIVERGENCE.md`, in module doc headers, in the `planning/06` columns — because "which layer is this, and how sure are we" is a question a reader answers, not a type a runtime consults.

The `psxrt → psxrt-compat` direction question dies with them, and so does its replacement candidate. A `psxrt::compat` module holding a `CompatFlags` bitset was proposed in the first pass of this revision and **withdrawn**: cross-subsystem flags, resolved at startup and addressed by name, are the retired `CompatPolicy` with the registry moved one directory up, and a flag nobody reads is a false claim about a capability. What replaces both is small: **each subsystem takes an explicit parameter, defaulting to the modern behaviour**, and a profile enum in `severedchains` chooses a set of them at boot.

Redirected, keeping the check: the citation-resolution test (it must still resolve every `planning/` and `research/` citation after the moves — now via `scripts/citation_audit.py`, which covers the whole corpus rather than one crate's records), the "no record claims retail validation" test, and the reference-fidelity comparison (the shipped-default set and the reference-faithful set differ in exactly one field — the sweep envelope — and that statement keeps a test, expressed against `AudioProfile::default()` versus `AudioProfile::psx_reference()`). The ledger-census test is replaced by something with real teeth: **a workspace test that fails if any runtime crate lists `psxrt-compat` as a dependency, or if any `psxrt` source file names `psxrt_compat`.**

## 5. The move table

Class column per `planning/03` §3: **A** runtime mechanism, **B** selector/parameter, **C** research evidence, **D** original-format encoding, **E** obsolete. A slash means the file contains more than one class and the classes go to different places. **No row is class D** — that absence is the finding, and the right-hand column of `planning/03` §5 shows what would have been.

| Now | Target | Crate | Milestone | Class | Kind of change |
| --- | --- | --- | --- | --- | --- |
| `psxrt/src/fixedpoint.rs` | `psxrt/src/math/trig.rs` | `psxrt` | I5a | A | repath; `TrigonometryModel` disposition per Q1 |
| (the 4096-factor pieces stay with trig) | `psxrt/src/math/angle.rs` | `psxrt` | I5a | A | **new** work: the 65536 unit, wrapping, conversions |
| `psxrt/src/bpp.rs` | `psxrt/src/gpu/pixel.rs` | `psxrt` | I1b | A | repath + colour paths added |
| `psxrt/src/blend.rs` | `psxrt/src/gpu/blend.rs` | `psxrt` | I3 | A | repath, equations unchanged; exhaustive test moves with it |
| `psxrt/src/timing.rs` | `psxrt/src/clock.rs` | `psxrt` | I3 | A | repath; no phase hooks, no host sleep |
| `psxrt-compat/src/rate_00.rs` | decoders + 3 constants → `psxrt/src/gpu/pixel.rs`; the divergence note → `crates/psxrt/src/gpu/DIVERGENCE.md`; **no selector is created** | `psxrt` | I1b or I3 | A / C | **delete the file.** Nothing in it is class D: it decodes, and the inverse (RGBA8 → 2-bit) is a separate class-D module, `psxrt-compat::texture` |
| `psxrt-compat/src/display_clamp.rs` | constants + clamp → `psxrt/src/gpu/`; the on/off → `severedchains::render::profile::PresentationProfile`; the 3 records → `DIVERGENCE.md` | `psxrt`, then `severedchains` | I3 (facts), I5b (profile) | A / B / C | **delete the file.** The profile lives in the engine crate because the profile is where a *set* of choices is made |
| `psxrt-compat/src/predictor.rs` | 6 constants → `psxrt/src/spu/xa_adpcm.rs` (declared once, read by the I1b decoder **and** the I1b encoder); `PredictorReset` → a parameter of that module; the leak bound → `DIVERGENCE.md` | `psxrt` | I1b | A / B / C | **delete the file** |
| `psxrt-compat/src/voice_alloc.rs` | `VoiceCapacity`, `Phase`, the allocation chain → `psxrt/src/spu/`; the capacity limit is a parameter the audio profile supplies | `psxrt` | I5a | A / B | **delete the file** |
| `psxrt-compat/src/same_z.rs` | both halves → `psxrt/src/gpu/` (the OT replay), `SameZOrder` as its parameter | `psxrt` | I5a | A / B / C | **delete the file.** It does not "stay with a named consumer" — the consumer is written in `psxrt`, in the same step, and a parameter declared next to the code that matches on it is the shape that survives review |
| `psxrt-compat/src/gte_precision.rs` | the 2 scale constants → `psxrt/src/gte.rs` with the transforms; `GtePrecision` → `TransformPrecision`, that module's parameter | `psxrt` | I5a | A / B / C | **delete the file** |
| `psxrt-compat/src/sweep.rs` | the envelope evaluator → `psxrt/src/spu/`; `SweepEnvelope` → `spu::SweepMode`; S7 → `DIVERGENCE.md` | `psxrt` | I5a | A / B / C | **delete the file** |
| `psxrt-compat/src/blend_select.rs` | the 2 records → the `psxrt/src/gpu/blend.rs` doc header + the `planning/06` rows | documentation | I1a | C / E | **delete the file.** Two records and no code: this file was never a module, it was a comment in a language that requires it to compile |
| `psxrt-compat/src/lib.rs` | retired wholesale at **I1a**, with the crate's `Cargo.toml` description and its `psxrt` dependency | — | I1a | E | retirement, per `planning/03` §6 |
| `psxrt/src/REMAP.md` | stays; **`psxrt-compat` gains its own only when a writer cites an original identifier** | both | — | C | today `REMAP.md` cites checklist rows (`G12`, `S7`, `X6`), not identifiers, so there is nothing to carry across |

**Read the milestone column as the ordering constraint it is.** Eight of these fourteen rows happen at **I1a or I1b**, and five of them are pure deletions. That is not a refactor schedule; it is a correction to a scaffold written before the question "what is this crate for?" was settled. Rows landing at I3/I5a are the ones where a *mechanism* has to be written, and they are written from `research/cross/02`, not copied from `psxrt-compat`.

Test files move with their subject, unchanged: `golden_trig.rs` with F1, `bpp_exhaustive.rs` with F4, `blend_exhaustive.rs` with F5, `timing_matrix.rs` with the clock repath, `properties.rs` splits along the same seams, `fixtures.rs` stays where the manifest is. `policy_ledger.rs` is **replaced** at I1b, not rewritten: by the matrix fixtures, and by the dependency-direction guard described in F10 and `planning/03` §6.

**The dependency consequence.** `psxrt-compat` declares `psxrt` as a dependency, and exactly one file uses it: `rate_00.rs`, which imports `psxrt::BlendMode` in order to *decode a flag word into* that type. Once F5 moves the three decoders, nothing in `psxrt-compat` references `psxrt` at all — so the `Cargo.toml` edge is deleted at **I1a**, and the crate starts its toolchain life with zero project dependencies. If `psxrt-compat::texture` or `::audio` later needs `psxrt::gpu::pixel` or `psxrt::spu::xa_adpcm` in order to emit bytes correctly, the edge comes back, and it comes back as `psxrt-compat → psxrt`, which is the direction the architecture wants (planning/01 §8 question 2). Either way the crate never sits *underneath* a runtime crate — and that is the property the deleted scaffold was quietly moving it toward.

**The rule that follows, for any later module in this crate:** do not let a toolchain crate depend on a runtime crate in order to reuse a type, and do not let a runtime crate depend on a toolchain crate in order to read a choice. Where both sides genuinely need one enum, that enum is a representation type and lives in `psxrt`, and the toolchain side names an *encoding of it*. `rate_00` is the case that proves the rule: the shared type existed only because a decoder had been parked in the wrong crate, and once the decoder moved, the dependency had nothing to justify it.

## 6. Verification after each move

The baseline is **72 tests / 8 binaries / 0 clippy warnings / 7 fixtures byte-identical**. A milestone that performs a move re-runs `make check` and additionally confirms:

1. fixture bytes and FNV digests unchanged (so an oracle was not quietly re-derived);
2. every relocated test still asserts the same predicates — an exhaustive test stays exhaustive; a matrix fixture stays a matrix fixture;
3. `REMAP.md` gained a line for every new identifier comment, and lost its line for anything removed;
4. the property tests still fail if reverted: the recorded shrink seeds in `crates/psxrt/tests/properties.proptest-regressions` are replayed first, so a regression in the LCG16 guard or the Bpp packing surfaces without new work;
5. the naming scan (`planning/05` §8.2) is still run by hand, since no scanner exists.

**No I0 test takes retail bytes as input, and none may start to.** All 7 fixtures are synthetic and oracle-generated. That boundary is P-02's, not this revision's.

**One addition, for the I1a and I1b gates.** Two checks that do not exist today and are cheap to build once the shape is fixed: (a) the dependency-direction guard — no runtime crate lists `psxrt-compat`, no `psxrt` source file names `psxrt_compat` (F10); (b) a round-trip inventory — for every decoder in `psxrt-import`, either a paired encoder exists somewhere and is exercised, or the module's doc header says why none is planned. (b) is what stops "the compat crate is for exporters" from degrading into "the compat crate is where unfinished exporters go".

## 7. Open questions for the owner

Three here, plus a fourth in §8. None is answerable from the repository alone.

**Q1 — `TrigonometryModel`: keep or delete?** (§4/F2). Recommendation: **delete at the I5a repath**, keep its doc comment as a header note on `math::trig`, and let the table work introduce its own seam when there are two arms to choose between. The alternative is defensible only if the owner wants the +512 / `0x3ff` facts to stay in code rather than in `research/psx/02-gte.md`.

**Q2 — do the per-subsystem matrix fixtures hold one fixture per checklist *row* or one per *parameter*?** `research/cross/02` holds 135 rows (43 carried from M4-05 + 92 `CL-*`), and 16 of them are cited by the I0 ledger records (disposed in §4 and `planning/03` §5). The revision classifies the rows; it does not delete any, and `planning/06` still maps all 135. The open question is only the fixture shape at I5a/I5b. Recommendation: **one fixture per matrix, which is usually per parameter but not always** — the blend matrix and the voice-allocation matrix are the two that are genuinely one-per-mechanism, and three of the ten CDs share a mechanism. The row→fixture mapping stays in `planning/06`.

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
