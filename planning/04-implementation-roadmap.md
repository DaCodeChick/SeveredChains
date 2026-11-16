# P0-05 — Implementation Roadmap (Revised)

Status: **Revised (2026-11-15).** Milestone sequence and boundaries derived from the dependency analysis in `planning/01-rust-architecture.md` and the I0 source audit in `planning/07-i0-refactor-specification.md`. **No milestone begins until the user approves this revision and then explicitly authorizes the milestone.** Each milestone: objective, prerequisites, scope/exclusions, code modules with module ownership, dependencies, verification, completion gate, risks.

## 0. What changed and how to read the numbering

Three milestones were split because each originally bundled two concerns with different test surfaces and different failure modes; the split also makes dependency direction match dependency fact.

| Old | New | Why |
| --- | --- | --- |
| I1 (all formats + pipeline) | **I1a, I1b** | container/codec layer vs content/sequence layer; I1a is the dependency root of I1b, and the dependency direction is pipeline → decoder, never decoder → pipeline |
| I2 (SDK framework) | **I2** (unchanged, scope-checked for module-name collisions) | — |
| I3 (engine core) | **I3** (unchanged; gains `gpu::vram`, `gpu::draw`, `audio::device`, `audio::transport`) | the state-owning services that have no hardware dependency |
| I4 (Fate VM + toolchain) | **I4** (unchanged; prerequisites now include I3) | the frame contract must exist before the VM is ticked by it |
| I5 (PSX models **+** wgpu/cpal backends) | **I5a, I5b** | hardware models are pure state machines; backends depend on the engine's frame contract. Built together, a renderer bug and a compatibility-policy bug are indistinguishable. **Split further on 2026-11-15:** I5a builds the mechanics and their parameters inside `psxrt`; I5b declares the profiles that choose between them, in `severedchains`. Neither builds anything in `psxrt-compat` |
| I10 (conformance + validation) | **I10a, I10b** | the replay harness is the tool that performs asset validation, so it is built first |

Acceptance-harness placement (new, and the reason I5 splits): the title/save harness belongs to I6, the submap/world-map harness to I7, the battle harness to I8, the character/inventory/Dabas harness to I9. Each acceptance harness is built *in the same milestone as its subsystem*, so a subsystem is not declared complete on a harness written later against a changed contract.

Cross-reference note: earlier drafts of this roadmap cited three new planning files (`07`, `08`, `09`). Only **`planning/07-i0-refactor-specification.md`** is created by this revision; `08` and `09` were never written, and their citations have been repointed to the documents that actually hold the content (`planning/05` for test strategy and the frame harness, `research/assets/09` for the MDEC cross-check). Where an earlier draft said "I5" meaning the PSX-model work, read **I5a**; the backends are **I5b**.

**Sequence (14 milestones, I0 done):**

```
I0 ──→ I1a ──→ I1b ──→ I2 ──→ I3 ──┬──→ I4   (Fate)
                                    ├──→ I5a ──→ I5b
                                    └──→ I6 ──→ I7 ──→ I8 ──→ I9
                         I4 + I5b ────────────────────────────┘
                                   I10a ──→ I10b
```

## 1. Cross-cutting rules (all milestones)

1. **Module-first, not crate-first.** The PSX platform services (GPU, GTE, SPU, RNG, timing, math) are internal modules of `psxrt`, never `psxrt-gpu`/`psxrt-gte`/`psxrt-spu` (§1.2 of `planning/01`). Beyond that there is **no crate-count limit**: a milestone may add a crate when it can name the responsibility and the dependency edges, and the engine, SDK, game and executable crates of the 2026-11-14 planning set remain in scope. Workspace members are enumerated literally, never `crates/*` — that is a hygiene rule, not a cap. One `Cargo.lock`. `unsafe_code = forbid` workspace-wide; a single-file override must state why at the point of use.
2. **No repository-level Rust dependency on Java sources.** Reference source is read *during* development; its findings are transcribed into a `PSX-Behavior Map` section in the module doc header with file, revision, commit, function and line. No build script resolves a reference path, no test generator is run as part of `make check` unless the generator's output is committed and checked in, no test path-falls back to a hard-coded sibling directory.
3. **Oracle regeneration is a script with `--check` mode.** `make check` verifies committed oracles against the committed generator. No reference repository required.
4. **Per-module test strategy is declared in-module, in prose, before the tests.**
5. **Module boundaries precede naming.** Before implementing a module: name its input, its output, whether it owns hardware state, whether it performs I/O, whether a test can construct it without a running engine.
6. **A test is a conformance test only if it states the behavior it protects and the evidence it is derived from.** "Round-trip passed" is not evidence.
7. **No compatibility policy is implemented as a production-only mechanism.** Each keeps an in-memory, no-engine test backdoor (planning/03 §8). A policy is a parameter with a named consumer, or it is not built.
8. **No runtime crate depends on `crates/psxrt-compat`, and `psxrt-compat` is never on the boot path.** This is wider than "`psxrt` never depends on it": the engine, the SDK, the game crate and the executable are equally barred, because the failure mode being prevented is a game reading its own presentation policy out of an export toolchain. The only permitted edges are the two `dev-dependency` test hooks in `planning/01` §5, and they are recorded as exceptions where they are used.
9. **Placement is decided by the TD-27 test, not by the word "compatibility".** Before a module is placed: *does this code produce, validate, or round-trip an original-format artifact?* Yes → `psxrt-compat`. It selects or performs how the game behaves → the subsystem that runs it, as a parameter. It records what the original did → documentation. None of these → it is not built. The five categories and their destinations are in `planning/03` §3.
10. **Unresolved research items are not silent.** A module whose evidence is Blocked/Possible states that in its doc header and keeps its gate honest.

---

## 2. Milestones

### I0 — Workspace foundation + bit-exact primitives · **COMPLETE**

RNG, fixed-point, bpp addressing, blend equations, timing. 72 tests, 0 warnings. Delivered in commit `93c6b1b`.

Its crate layout is superseded by this revision, and so is every line of `crates/psxrt-compat/src/`. The audit and the per-file disposition are `planning/07`; the execution is the first half of I1a. Nothing from I0's `psxrt-compat` is carried forward — not the eight policy modules, not `Confidence`/`Layer`/`Availability`/`PolicyRecord`/`CompatPolicy`, not `LEDGER`, and not the crate's dependency on `psxrt`. What carries forward is the *finding*: that a divergence statement is worth keeping and a divergence statement compiled into a runtime crate is not.

---

### I1a — Container & Codec Layer · **NEXT MILESTONE**

**Objective.** Given a disc image or an unpacked tree, resolve, read and decompress any game file; rebuild the `files/` cache. This is the dependency root of I1b — BPE decompression and the cache are a prerequisite for every content decoder, because the pipeline, not the decoder, decides whether to call one.

**Prerequisites.** I0.

**Scope — `psxrt-import` crate creation + these modules:**

| Module | Content | Checklist rows |
| --- | --- | --- |
| `src/space.rs` | the canonical file-space abstraction: the 63-bit address, the four kinds (ORIGINAL / DERIVED / GENERATED / DEPENDENCY), the four namespaces (FILE / DISC / REGION / OBJECT) and the requirement that a path string never encode a kind in its spelling, the 4 GiB high-bit wall and its two independent guards, DRGN-name resolution with the region-4 `DISC:\` branch, the 16 MiB full-disc fallback, the three error codes (`MissingDependency`, `MissingSource`, `CrcMismatch`) | CL-AST-01, CL-AST-03 (the naming half) |
| `src/container/iso.rs` | the `files/` cache reader — region-major 63-bit keys and the namespace-isolation hazard, the 4-byte size header, four depth levels, path-length-bound keys (16 MiB when > 127 chars) | CL-AST-01 |
| `src/container/mrg.rs` | the MRG container: the sector-aligned vs byte-offset split (the trailing-digit mask), the virtual-entry parent offsets, the duplicate detection, the per-file *no*-compression rule (no archive-wide container exists in the reference) | CL-AST-02, CL-AST-12 |
| `src/container/bpe.rs` | the BPE **decoder**, plus the raw-LZSS variant; mode selection is data-driven (0 = raw, 1 = BPE, other → error) and is made **before** the payload is known to be a TMD/TIM/…. The encoder half is `psxrt-compat::container`, reached from here as a `dev-dependency` for the round-trip test — it is not a second half of this module | CL-AST-12 |
| `src/cache.rs` | the `files/` cache, `VERSION = 5`, the per-node incremental skip, the determinism/ordering constraints, transform failure → node skipped, not fatal, the whole-tree wipe below version 5 | CL-AST-03 |
| `src/pipeline.rs` | the orchestrator: the two halves (A cache rebuild, B patch application), the per-node transform registry, the unpack-thread entry, the init-gate signal, the rayon pool at depth 1 with the caller's workspace guard | CL-AST-03, CL-AST-12 |
| `src/patch.rs` | the patch **orchestration** only: the 353-entry `scripts.csv` walk, the CRC32 gate over the `#include` closure (not the single target file), the one-time backup, the restore-on-removal, behind an injected `ScriptPatchToolchain` trait. I1a ships the trait and a no-op recording stub; the real toolchain is I4 | CL-SCR-02, CL-SCR-04 (orchestration half) |

**Excluded.** Every content decoder (I1b). Runtime asset *loading* (I3 — `asset::service` consumes this crate's output). The script toolchain itself (I4). Any `unsafe`.

**Scope — `psxrt-compat` is redirected in the same milestone.** Two things happen in I1a and they are one change, not two: the crate's eight policy modules and its `lib.rs` framework are deleted, and its first class-D module is added.

| Module | Content | Why it is a `psxrt-compat` module |
| --- | --- | --- |
| `psxrt-compat/src/container.rs` | the writers that make a round trip possible: an MRG writer (sector-aligned vs byte-offset layout, the trailing-digit mask, the virtual-entry parent offsets), a SEADS/BPE encoder including the raw-LZSS variant, and a `files/` cache writer. Their purpose in I1a is to be **test oracles** for the decoders above them; whether any of them is also a public export API is decided at the end of I1b, not assumed here | they emit original-format or reference-derived bytes. The BPE encoder exists so the BPE decoder can be tested against its own inverse — a round-trip gate is a test (TD-28), and the thing it drives is an encoder, which is a decision |
| `psxrt-compat/src/disc.rs` | **not built in I1a.** Named here only so it is not later mistaken for an omission: disc reinjection (writing a changed file back into an ISO9660 layout and regenerating the affected cache entries, `VERSION = 5` on the write side) is the one module that cannot be designed without real disc bytes, and it is blocked on P-02/R-01 |

**Two things this explicitly does not do.** It does not create a runtime policy layer, in `psxrt-compat` or anywhere else: `planning/01` §8's open question ("does I0's `TrigonometryModel` stay?") is still open, and a `psxrt::compat` module holding a cross-subsystem flag bitset was considered and **rejected** (`planning/03` §6) — a bitset of unrelated flags is the same design error as the retired `CompatPolicy`, moved one directory. And it does not move the `files/` *reader*: the reader stays in `psxrt-import::cache`, which reads a derived format; what `psxrt-compat::container` adds is the writer half, so that a decoder can be tested against a synthesised input.

**Crate.** `psxrt-compat` continues to exist. Its `Cargo.toml` description changes — "Compatibility policy layer … as typed defaults with knobs" describes a crate that is being deleted, and a description is a promise to the next reader. Its `psxrt` dependency is dropped, and re-added per module only if a writer turns out to need a runtime model.

**Module boundaries to fix before coding.** §A `Disk`, `Cache`, `Tree` all implement one `Space` trait but must stay separate types — an early design that made `Cache` a `BTreeMap`-backed `Space` and passed it to the pipeline made the pipeline untestable without real files. §B the streaming-decompression path (`Decompressor::new().read_to_end()`) and the two-buffer path (`decompress_slice`) are *different*: the streaming path reads 4 bytes beyond the end of the payload. That is legal inside an 0x800-byte-aligned buffer and illegal at an exact slice boundary; the reference had to special-case `disc:` reads. Whether the Rust reader replicates it or asserts it away is an I1a decision, recorded either way.

**Crates.** New `psxrt-import`, `edition = "2024"`, `unsafe_code = "forbid"` at crate level. `psxrt-compat` is redirected in the same milestone — see its scope block below.

**Dependencies.**
- `thiserror` (workspace) — the domain error enum lives here, not in `psxrt` (TD-25).
- `rayon` (workspace, **`rayon` not `rust-gpu`** — the crate named in the earlier draft does not exist; TD-17).
- `log` (workspace) — the "skip and continue" path needs a recording sink for tests (TD-25).
- No `opus`, no `wgpu`, no `cpal`, no `image`. No `psxrt` dependency is required by any I1a module.

**Verification.**
- Per-module unit tests, each naming its contract: the ISO9660 conformance test on a hand-built 4-disc image; the sector/byte split enumerated over the trailing-digit mask; the cache version + ordering rules; the pipeline determinism test (same synthetic tree → byte-identical `files/` output, run twice and on two thread counts).
- Integration: a hand-built ISO9660 image with the 4-disc structure, run end-to-end → the expected `files/` tree.
- **The BPE round trip** (encode → decode → compare, over hand-built payloads and over random ones) lives in `psxrt-import`'s test tree, driving the `psxrt-compat` encoder as a `dev-dependency`. It is not evidence that `psxrt-import` should contain an encoder.
- Fixture policy: synthetic, hand-built, layout-derived. No proprietary bytes (planning/05 §5).
- Property tests: `read_file` **never panics** for any 63-bit value (the 4 GiB guard and the `region_mask & mask == 0x03` predicate — 192 bits of input domain), and the `key` round-trip (encode → resolve → decode).

**Gate.** `cargo test --workspace` green; the pipeline output is byte-stable across runs and across thread counts; the patch orchestration is idempotent (re-run → no-op) and self-healing (tampered cache → re-apply); no content decoder exists yet; `cargo clippy --workspace --all-targets` zero warnings; the file-ending audit passes. **Plus the two architecture gates:** no `crates/psxrt-compat/src/*.rs` file that existed at I0 survives into the I1a tree, and no crate whose purpose is runtime execution lists `psxrt-compat` as a dependency. The second is checked by a test, not by review (`planning/03` §6).

**Risks.** The in-place reader (no whole image materialized) is a genuinely new surface — the reference reads in place, so the synthetic-image test is the only oracle. The 63-bit key space cannot be enumerated exhaustively; the property test is a bounded sweep, and that limit is stated rather than presented as coverage.

---

### I1b — Content & Sequence Layer

**Objective.** Decode every original content format into engine-ready form, and build the XA→Opus transcode. The pipeline already exists and already decides whether to call a decoder — so from here on, adding a format means registering a transform, not extending a pipeline.

**Prerequisites.** I1a.

**Scope.**

| Module | Content | Checklist rows |
| --- | --- | --- |
| `psxrt-import/src/render/tmd` | TMD + CTMD pair, **decode** to the working model: flag semantics (`0x1` fixp unloadable, `0x2` CTMD), the 0x1c-byte object table, the 8-byte vertex/normal stride (normals ×2¹²), primitive runs, `updateTmdPacketIlen` on-disk run-length rewriting (CTMD-authoritative rule). **The re-encode half is `psxrt-compat::model`**, whose input is the model this module produces — so the round-trip gate needs both crates, as a `dev-dependency` | CL-AST-04, CL-AST-05 |
| `psxrt/src/gpu/clut` | the pure CLUT codec: CLUT entries + TPage + X/Y + GXY, a 5-bit `Texel` codec, BGR555↔RGBA8 for 16-bit CLUTs. **No VRAM, no draw submit, no blit.** No `vram` module is created here — that arrives in I3 as `gpu::vram`, and its absence in I1b is documented, so nobody later mistakes it for an omission. `psxrt-compat::texture` reads this module to emit a CLUT block | CL-AST-06 |
| `psxrt-import/src/render/tim` | TIM parse, the writer-pinned layout, PXL sheet emission, MCQ strips (two palette subtables, 256 B header, 0x4000-byte sheets, 0x2000-byte palette sheets), `decode_16_to_rgba` → RGBA8, `decode_4_to_16` / `decode_8_to_16`. **Read-only** despite "sheet emission": emission here means assembling an in-memory working sheet, and a sheet handed back out as a `.TIM` file goes through `psxrt-compat::texture` | CL-AST-06 |
| `psxrt-import/src/effect/anim` | the ANIM family: plain, LMB (the only truly compressed member), CMB (a plain container of three plain sub-animations). **No "CMB2". No DRGN compression.** | CL-AST-07 |
| `psxrt-import/src/fmv/mdec` | MDEC frame decode: the 10-sectors/frame demux, VLC/IDCT; **plus a DRGN1 data cross-check** — `patches/scripts/DRGN1/254.diff` proves the `disc:FILE:…`/`bin`/`bin\` shape, an 0x800-byte alignment rule, that some files are compressed and some are not, and that the mode flag is data-driven. The cross-check validates the *file-space and decompression* model, not the MDEC bitstream | CL-AST-09 |
| `psxrt-import/src/audio/{sssq,sshd}` | parse-only: 44-byte header, 240-byte instruments, per-channel event streams, SFX note streams, the 16-channel music vs 24-channel SFX split, the SSHD sound container | CL-AUD-10 |
| `psxrt-import/src/audio/transcode` | the transcode pipeline: decode XA at 37800 → 80/63 Catmull-Rom → 48000 → Opus at 128 kbps. **The four parser layers exist in the reference; the decoder and resampler do not** — they are reverse-engineered, and the implementation is gated on cross-checking against real SSSQ + SSHD once a disc image is available | CL-AST-10, X4, X7 |
| `psxrt/src/spu/xa_adpcm` | the pure codec: the 2-bit XA filter **field** (not table index), the repeat-count nibble, the 16-step magnitude nibble, the 28/24-byte block geometry, the 1/48 shift, the ±32768 clamp, the start/loop/early-eof filter flags (X3), and the 24-byte XA header/sector model (X1). It does **not** model the SPU's own 5-bit filter; the difference is documented, not reconciled. **Decode-side geometry only** — the XA encoder, the sector/interleave writer and the SSHD/SSSQ writers are `psxrt-compat::audio`, built in the same milestone and reading these same constants. The six constants that I0 held in `psxrt-compat::predictor` land here; this is their one home | X1, X2, X3 |

**Excluded.** The SPU voice model (I5a), the runtime `Vram` + queue-then-submit `Gpu` + `GpuKind` (I5a), the FMV/player state and `VideoPlayer` (I3, then I6), the audio player runtime (I5b), runtime asset loading (I3).

**Crate dependency.** This is the first legitimate `psxrt-import → psxrt` edge: `render/tim` and `audio/transcode` consume the pure `psxrt::gpu::clut` and `psxrt::spu::xa_adpcm` codecs. It is not a layering violation, and it is recorded as the reason TD-19's direction rule is now "the default, not absolute".

**Scope — `psxrt-compat` gains its remaining I1b modules.** Three, all class D (`planning/03` §3), none on the runtime path:

| Module | Content |
| --- | --- |
| `psxrt-compat/src/texture.rs` | TIM emission from a working sheet (PXL or MCQ): header + CLUT/image blocks, 4/8/15/24-bit emission, quantization of over-wide sources, the `B0` (no transparency) and `B1` (24-bit) flags. Includes the RGBA8 → 2-bit index map, which is the exact inverse of `psxrt::gpu::pixel`'s read path — that pair is the reason a decoder and an encoder must not share a file (TD-26) |
| `psxrt-compat/src/model.rs` | TMD and CTMD **encode**: reverse the I1b decomposition, re-emit the 24-byte header, the 32-byte GPU orders and the per-model geometry, and the CTMD block packing. Its input is the model `psxrt-import::render::tmd` produces |
| `psxrt-compat/src/audio.rs` | re-encode a replacement track into a sector-accurate stream: the XA encoder, the sector/interleave writer, the SSHD and SSSQ writers. **The reference has no SPU or XA encoder** — this is designed from the documented sector format, so it is a greenfield reverse-conversion module, not a port, and its verification is a round-trip against the I1b decoder, never a fidelity claim |

**Test hooks wired in this milestone** (planning/01 §5 rule 3, both `dev-dependencies`, neither a runtime edge): `psxrt-compat`'s test build reads `psxrt-import`'s decoders so each encoder is proved against its own inverse; `psxrt-import`'s test build calls a `psxrt-compat` writer to synthesise an input fixture. Six of the eight I0 `psxrt-compat` files are gone by the end of this milestone and the last two (`lib.rs`'s framework, and `predictor.rs`'s constants, which have moved) go with them.

**Dependencies.** `byteorder` (endian-agnostic primitives — the reference uses explicit little-endian accessors, so this is a correctness dependency, not convenience); `opus` **optional**, `PSXRT_WITH_OPUS`, and a compile-time constant `OPAQUE` fallback plus a `OPAQUE`-mode oracle generator. The 80/63 resampler and the Opus header are pure Rust, so the non-Opus mode tests them without a C library (the I0 `Random48Probe.java` pattern); `#[cfg(test)]` is explicitly not a substitute because a test-only gate is invisible in release builds and silently mislabels cache entries.

**Verification.**
- TMD: primitive-run rewrite idempotence; the on-disk low-16 authoritative **only for CTMD** — non-CTMD TMDs read `len` from `ileng`, while the runtime path always re-derives from `cmd_len`; both paths are tested and the disagreement is a fixture, not a bug to smooth over.
- TIM: encode→decode round-trip against the 20 documented facts, plus the reference's own three-layer check (encoder → decoder → re-encoder).
- ANIM: the documented 1000-file corpus test is blocked on P-02; until then the structural invariants (no per-file `0xFE` compression marker).
- SSSQ: the 16/24-channel split, instrument/layer/ADSR records, the 68-instrument 0x100-byte-block census.
- Transcode parity: decode synthetic XA at 37800 → resample → Opus-encode → Opus-decode → compare against pre-Opus PCM within a stated loss budget. **A decoder-level consistency test, not a fidelity claim** (planning/05 §7).
- Cross-subsystem chain: SSSQ → instrument → key-on → envelope → pitch → ADPCM block → samples, at the model level.

**Gate.** All decoders compile with no engine and no GPU/audio device; every test above exists and passes; every parser is cross-checked against at least one reference implementation (MDEC against Dig-Dabas, TIM/XA/resampler against the reference `XaTranscoder`, TMD against both `CtmdTransformer` and `TmdObjLoader`, the SFX grammar against both SFX sequencers); no decoder is declared *validated* (source-derived fixtures only).

**Risks.** MDEC has no reference encoder — the synthetic bitstream is the only oracle, so a decode bug surfaces only at I10b. Mitigated by the VLC code-table conformance test, not eliminated. `Audio.findTranscodedName()` resolves `SSHD→OPUS` while `TranscodeChecker` records `.xa` — the two must agree or the cache key silently changes; that is a test, and it is I1b's.

---

### I2 — SDK framework core (`severedchains-sdk`)

**Objective.** The cross-layer contract layer exists and its framework semantics (registries, events, i18n, mod management, the Tag codec, the config framework) are conformance-tested against the documented contracts — with no engine and no game yet.

**Prerequisites.** I0 (workspace). Does not require I1.

**Scope.** Unchanged from the previous revision: the `registry`, `event`, `i18n`, `mod`, `tag`, `config` modules with their documented semantics (the RegistryId grammar, the one-shot Register window, the TypeId exact-class bus, the 5 priority levels, the critical-event set, cancellation, the 4-level i18n precedence, mod discovery + the 12-step `boot_mods`, the campaign binding codec, the 10-type Tag model + wire codec, the `.dcnf`/`.dpre` layouts).

**New in this revision.** (a) `tag`, `save`, `config` live **inside** `severedchains-sdk`; there is no `severedchains-te`, no separate save/config crate. (b) Module-name collision check before implementation: the planned `render`, `audio`, `fate` names must be re-checked against the planned `psxrt` module names and must pass a "which layer decides?" test — if both layers could change a behavior, it belongs to neither yet. (c) **`fate` belongs in `severedchains-sdk` only if the SDK stays engine-independent**; otherwise it moves to the engine crate. The decision point is I4, not here. (d) **Corrected on 2026-11-15; the first draft of this revision said the opposite.** An earlier draft of this revision read "the four-level policy registry moves to `psxrt-compat` in I1a", so that the SDK's config framework would be a host for L1 entries only. The conclusion holds and the destination does not: there is no policy registry to move, and a compatibility selector reaching an SDK config transport would still be a cross-subsystem flag bag wearing a mod-loader's clothes. The SDK's config framework hosts L1 entries and nothing else; a compatibility parameter is constructed by the game crate and passed to the subsystem that reads it, and `severedchains-sdk` never imports a compatibility selector (rule 8).

**Dependencies.** `serde`/`serde_json` (data-mod manifest only), `thiserror`, `log`.

**Gate.** Contract suites green; the framework vertical slice passes (a full `boot_mods` → `boot_registries` → config-load → i18n sequence with a synthetic test-only mod); zero dependencies beyond the TD-24 list.

---

### I3 — Engine core vertical slice (`severedchains`)

**Objective.** The frame contract, the state machine, the lifecycle, and the service hosting exist and are conformance-tested **headlessly** (Noop backends) — the largest architectural risk isolated before any content or presentation.

**Prerequisites.** I0, I1a, I1b, I2.

**Scope.** Unchanged from the previous revision: `builder`, `frame` (the 21-phase driver, the per-context rates, frame-locked pacing, one-frame input edges, the frame-start task queue), `state`, `input`, `render` (backend trait + Noop backend), `audio` (backend trait + Noop backend, the two-thread structure, the edge-word protocol host), `load`, `save`, `error`, and the `scus94491` binary.

**New in this revision.** Four modules that the previous revision implicitly deferred to I5 now land here, because they are state-owning and device-free: `gpu::vram` (owned linear scratch — the `0x80000`-word requirement and the `0x20000`-word allocation fact, the 5-bit/8-bit read paths, and the explicit `upload`/`download`/`copy_vram_to_vram` transfer surface that the reference has and the earlier draft of `planning/01` said it did not), `gpu::draw` (the command vocabulary — **queue-then-submit, not direct-mode**, with the `DrawCommand`/`DrawTopology`/`FilterMode` set that must stay distinct), `audio::device` (the transport's own device state: `open`/`active`/`buffered`/`started`/`paused`), `audio::transport` (the `AudioTransport` trait with **one device-owning implementation** and exactly one compile-time selectable backend per build — no runtime probe-then-replace).

`audio::device` must satisfy **five** constraints, each an I3 test: (a) seek, (b) frame-accurate clock alignment, (c) the ≤ 1/60 s key-on quantization, (d) the `VSync` domain boundary for FMV, (e) the four parser layers. The I3 Noop transport satisfies (a), (b), (c), (e); (d) is asserted only in the FMV state's own unit tests and that ceiling is recorded.

`files/` and `state_data/` stay **runtime storage**, owned by `psxrt-import::cache` (read) and `psxrt-compat::container` (write). They are not a compatibility policy and they are not a `psxrt-compat` *policy* module (planning/03 §3, class A). The two are already fixed.

**Dependencies.** `winit`/`gilrs`/`cpal`/`crossbeam`/`parking_lot` under `PSXRT_WITH_*` only; `rayon` for the load pool; `psxrt` (default feature); `psxrt-import` (default feature); `severedchains-sdk` (default feature).

**Gate.** The headless vertical passes end-to-end (boot → state A → queue → state B → teardown → shutdown) with the full 21-phase trace captured and matching platform/02 §3; the transport conformance suite passes across two backends; no `AudioPlayer` in `psxrt`; **`psxrt` does not name `psxrt_compat` anywhere** — not in a `use`, not in a `dep` edge, not in a doc comment — and no engine module takes a compatibility selector as an unnamed flag.

---

### I4 — Fate VM + toolchain (`severedchains-sdk`)

**Objective.** The script VM and the Fate toolchain exist and are conformance-tested against the M3 contract.

**Prerequisites.** I2 (the framework), **I3 (the headless harness — the VM is ticked by the frame contract, so the integration test needs it)**.

**Scope.** Unchanged: the `vm` (the 108-slot pool, the round-robin allocator, the 63-slot dual int/float storage, `FlowControl`, the fork/consume/deallocate lifecycle, `framesPerTick`, the 1024-entry native table, the 592-slot catalog, the 4 built-in op bodies, the error paths), the `binary` (the container, the header-validity predicate, the C-01 documentation module), the `toolchain` (decompiler, translator, assembler, patcher, meta system).

**New in this revision.** The I1a `ScriptPatchToolchain` stub is replaced by the real toolchain and the `--check` form (the toolchain's own CRC gate, distinct from the orchestrator's) is specified in `planning/05` §5.8 so the two gates are not conflated. Crate placement is decided here per I2's rule (d).

**Gate.** The round-trip property passes over the hazard-set fixtures + 100 random synthetic scripts; the VM integrates into the I3 harness; the C-01 module documents both tables per-opcode; the 9 lifecycle events fire at the documented points.

---

### I5a — PSX compatibility models (hardware, not backends)

**Objective.** The three hardware compatibility models and their compatibility policies exist, are unit-testable on their own, and change no presentation behavior.

**Prerequisites.** I0, I3.

**Scope.** `psxrt::gpu` (the dual 15/24-bit VRAM store, the ordering-table mechanism, the command executor over `psxrt::gpu::draw`'s vocabulary); `psxrt::gte` (the T1 register subset, the SZ/SXY FIFOs, the flag register, the projection test, the saturation, the overflow flags, both precisions, the T3 screen-edge discard, the T6 projection plane, the T7 NCCS lighting); `psxrt::spu` (the S4 ADSR phase machine, the S5 voice ADPCM over the I1b pure codec — the SPU's 5-bit filter stays here, S6 pitch stepping + interpolation + FM + shared noise, S8 the reverb network).

**Module naming.** These are modules of `psxrt`, named for the subsystem because the subsystem *is* the unit of behavior here (`gpu`, `gte`, `spu`). Types inside them follow `planning/01` §6 — no `Psx` prefixes, and no `GpuKind`-style enum whose discriminants advertise a hardware identity.

**The compatibility models, and where their selectors live.** The seven items below are the class-A mechanics this milestone builds, each with the parameter that selects between its alternatives. **None of them is in `psxrt-compat`, and none of them is activated from there** — there is no policy module to activate, and `psxrt-compat` is not on the boot path (rule 8). Each parameter is supplied by the engine crate's profile at construction (`severedchains::render::profile::PresentationProfile`, `severedchains::audio::backend::AudioProfile`), which is why I5a can be verified with no renderer and no audio device: the parameter is a function argument, and a fixture can pass one directly.

- the G6 same-z tie order, in the OT traversal — `gpu::SameZOrder` (LIFO / FIFO)
- the T2 precision, and the **T4 integer submap camera program**, which is built here rather than deferred, because it has no clean modern counterpart to default to — `gte::TransformPrecision` (float / integer / exact)
- the G4 display clamp and the display extents — a `DisplayExtent` value and a clamping on/off in the render profile, not a `psxrt-compat` module: the clamp has one correct answer, so the *fact* is a layout constant and only the *profile* is a choice
- the G11 rate-00 translucency mapping, over the flag-word decoder that lands in `gpu::pixel` — the selector, when it is needed, names a decode model, not "emulation"
- the X6 predictor reset (never / per-file) — an `xa_adpcm` decoder parameter
- the S7 sweep envelope (gap / envelope) — `spu::SweepMode`, default **envelope**; the reference's gap is not preserved
- the S3 allocation chain: the 20→8 bound, the matching retrigger → bound-gated steal → round-robin → finished/unused — `spu::VoiceCapacity`, supplied by the audio profile; the capacity itself is a runtime structure owned by `spu`, not a policy declaration
- the G10 blend selection: the exact equations in OT replay, the `B+F/4` decomposition in the modern path — `gpu::BlendModel`, over the arithmetic I0 already ships in `psxrt::blend`

The two facts that make this list testable rather than aspirational: the *mechanics* above do not exist yet (I5a builds them), and the *selectors* that currently stand in for them have no caller — `SameZOrder`, `GtePrecision`, `SweepEnvelope`, `VoiceCapacity`, `PredictorReset` and `clamp_draw_width` are read nowhere in `crates/` outside their own crate. I5a writes the mechanics fresh, from `research/cross/02` and the per-system research documents, and the selector is written as that mechanic's parameter in the same step.

**Test strategy, per the test-layer rule in `planning/05` §2.** A pure function (`xa_adpcm_block`, the blend arithmetic) is tested exhaustively. A state machine (the ADSR) is tested by a scripted sequence over a fixed clock. A transport is tested against **two** implementations (I3's `AudioTransport`). A parameter with more than one alternative is tested by a **matrix fixture** (planning/03 §5) — the matrix for the blend model enumerates the 256 (mode, blend) combinations and both sides of the `B + F/4`/32 boundary; the matrix for voice capacity enumerates every allocation scenario. A matrix fixture is keyed by **case, not by policy** (open question Q2, planning/01 §8): one file per matrix, its rows naming the parameter value each row pins, so a later change of mind about a default does not require a new fixture.

**Gate.** Every CD-attached row (G6, G4, G11, S7, X6, T2, the Q-74 note) has its mechanism, its default, its selector with a named consumer, its divergence note, and its register pointer in place; the matrix fixtures are generated and checked; the 43 M4-05 rows are covered; **no wgpu or cpal code exists**; **no `psxrt-compat` module is on the boot path**, which is checkable in the manifest rather than by inspection; the frame-boundary integration test (planning/05 §4, the headless frame harness) exists and guards the model, not a renderer.

**Risks.** This is the milestone where a wrong GTE accumulator width hides: a model that renders plausibly for 200 frames and drifts by one pixel per frame is invisible to visual inspection. That is why I5a and I5b do not merge.

---

### I5b — Native presentation and audio backends

**Objective.** The wgpu and cpal backends exist, are feature-gated, and consume the hardware models rather than reimplementing them.

**Prerequisites.** I1b, I3, I4, I5a.

**Scope.** `severedchains::render::wgpu_backend` — the 3 presentation modes (mode 0 = the OT replay at `GPU.endFrame`; modes 1/2 = the modern 2D/3D pools with the LIFO/FIFO equal-depth tie-breaks), the G2 Bpp sampling, the G9 texturing, the G12 mask bit, the G3 draw offsets, the G15 VRAM→VRAM copy, the LEGACY H-division pseudo-perspective, the scissor stack, the display swap + the G13 latency, the post pass, the resize/reprojection. `severedchains::audio::cpal_backend` — the device hosting + the 2 s reinit poll, the music thread (the per-sample BGM renderer over the parsed SSSQ — the behavioral mirror, not claimed sample-identical), the sample-accurate BPM timing, the sfx thread (the 60 tps SPU tick, the SFX sequencer facade, the 8.0 ms SFX ticks), the FMV player runtime (the staged 0–4 API, the 10 ms ticks, the 8 buffers, the ≥ 40 ms guard, the pre-queue 4, the one 480-sample frame per tick, the `playerVolume` SFX×MASTER, the unload at EOF, the drop-on-full backpressure), the volume/routing chains.

**Where the audio clock choice is made, and where its evidence lives.** Which reference layer uses the 8.0 ms tick and which uses sample-accurate playback (CD-09, Q-74) is an `audio::backend::AudioProfile` decision, not a runtime lookup. The evidence for it is recorded per layer in `research/audio/*` and pinned by the model-conformance fixtures; **it is not read out of `psxrt-compat` at run time**, and no `psxrt` or `severedchains` module imports a clock selector from there. The profile variants follow `planning/01` §6.4: they are named for what the model does (`PsxBgmStream`, `PsxSequencer`, `PcmSwDrums`), never after a host API — a variant that names an ALSA/Windows abstraction is a port label, not a model.

**Modern-path semantics carried from the user's brief, recorded as the default path (MP-01…MP-06, model defaults — *not* CD rows; see `planning/03` §7).**** Opaque scene queue built during the frame, resolved at draw time by depth then order-of-submission with deterministic equal-depth tie-breaking; three logical layers (background, scene, overlay) with **the world-map fog being the one exception that changes a key**, not the general rule; a 32-bit linear float HDR target with per-layer exposure control, a fixed reference luminance and a configurable headroom multiplier — the HDR chain is a real linear float pipeline, not "B-gradients plus a flag"; explicit alpha, no automatic blending, an explicit draw-time blend description when a primitive needs one; a mask surface that does not imply a fixed "no-blend → full-viewport-passthrough → second pass" execution; a 32-bit linear-float audio bus with software voice count and 8/15-bit volume semantics preserved *in the conversion boundary*, default equal allocation, a reverb send on top.

**Gate.** The wgpu smoke renders one OT frame; the cpal Noop-device path runs the thread structure without a device; **the two profile enums exist and are the only place a set of compatibility selectors is chosen together** — there is no flag bag, no string-keyed registry, and no `psxrt::compat` module to hold one (planning/03 §6); **no visual-parity claim is made** (Blocked domain — no baseline; documented in the gate record).

---

### I6 — Game core vertical slice + Title & Save acceptance harness

**Prerequisites.** I0–I5b. **Scope unchanged** from the previous revision (the `module` census, `state_data`, `state/title`, the submap *entry* path, `persistence`, the config content, the RNG wiring, `state/credits` + `state/game_over`).

**New.** The **Title and Save acceptance harness** is built here, not deferred: the game boot, the configuration system, the persistence system, the save-format conversion chain, the FMV plumbing and the title sequence, driven headlessly. The FMV plumbing stays in this slice — that is why I5a/I5b precede I6.

**Gate.** As before (census test asserts 1238/70/88; the save round-trip over all 11 magics; the RNG wiring; the retail-byte validation gap recorded) **plus** the harness runs and the title/save trace is checked against the original, not only against the reimplementation.

---

### I7 — Submap + world map + Submap acceptance harness

**Prerequisites.** I0–I5b, I6. **Scope unchanged** (the submap scene model and layer split, the 4-layer script bridge, the submap collision, the camera programs, world-map navigation, fog, battle initialization).

**New.** The **Submap acceptance harness** — a walk-and-interact scenario over the submap. The 4-layer script bridge (§B's remaining gap) is delivered here and its verification is the bridge trace plus the harness scenario, **not** "four layers exist". World-map fog keeps its exception status verbatim: **one** exception that changes a key (the fog), **one** exception that affects runtime draw state only (the wmap water gradient). `submap_fog_mode` moves from *implementation surface missing* to *specified* here; the §A open question ("does any 24-bit sprite layer write the mask at all?") blocks the mask half, not everything else.

---

### I8 — Battle + Battle acceptance harness

**Prerequisites.** I0–I5b, I6, I7. **Scope unchanged** (battle entities, damage formulae, additions, turn order, DEFF, battle UI, battle camera/lighting, per-battle camera profiles, battle-specific GTE precision).

**New.** The **Battle acceptance harness** — the 11-step turn contract with scripted enemies. The **battle replay** is built here against the scripted enemy AI, and this is the milestone that must prove a replay needs no hidden state: the hidden-state list is SPU register values, active voice state, FMV buffer contents, FMV play position, world-map fog, submap collision. Anything else discovered is recorded, not silently absorbed.

---

### I9 — Remaining subsystems + Character & Inventory acceptance harness

**Prerequisites.** I0–I5b, I6–I8. **Scope unchanged** (characters, inventory, the six inventory screens, Dabas, FMV presentation, the script toolchain completion).

**New.** The **Character, Inventory, Dabas acceptance harness**. Completion here means: implemented, unit-tested, acceptance-tested, integrated, and traceable to the corresponding checklist and register items.

---

### I10a — Engine-level conformance and cross-crate integration

**Prerequisites.** I0–I9.

**Scope.** The cross-crate integration suite: the `psxrt` model ↔ `psxrt-import` decode ↔ `psxrt-compat` encode round trips, and the same triangle exercised through the engine and game crates where those are the consumer. Full frame-accurate replay tests. Audio model conformance (the 1500–3000-file replay corpus, the ≤ 1/60 s quantization observable end-to-end). Compatibility-contract conformance: **each parameter is tested against both sides of its own boundary** (the two equal-z backends, the two blend modes, the two projection modes). The replay tool and the trace-comparison tool.

**Gate.** All I2–I9 suites still green with the integration layer on top; every policy passes both sides of its boundary.

---

### I10b — Asset validation and full-game conformance (asset-gated)

**Prerequisites.** I10a **and the P-02 asset decision.**

**Scope.** Real-disc replay, rendering parity, audio parity, full-game conformance, the MDEC decode cross-check against real FMV bitstreams (the cross-check recorded in `research/assets/09-fmv-mdec.md`, run through the asset pass of `planning/05` §7), performance and memory profiling, release packaging.

**Two kinds of gate, stated so neither can quietly absorb the other.** *Development milestone* — the code is built and unit-tested without assets. *Validation milestone* — the asset-dependent check runs against a real disc image.

**Gate.** No `Unresolved`/`Blocked` item may be closed at I10b without the asset decision. Nothing here is claimed until the evidence exists.

---

## 3. Dependency and parallelism

```
I0 → I1a → I1b ─┬─→ I2 ──→ I3 ──┬─→ I4 ─┐
                └─────────────────┴─→ I5a ┴─→ I5b → I6 → I7 → I8 → I9 → I10a → I10b
```

Three facts about this graph that a naive reading hides:

- **I2 does not depend on I1.** The SDK framework is data-structure and contract work; it can proceed in parallel with I1b.
- **I5a depends on I3, not on I5b.** The models are testable without any backend, which is the point of the split.
- **I1b's audio work and I1b's render work are independent.** They are one milestone for sizing reasons, not because either blocks the other.

## 4. Risk-ordered view

| Rank | Risk | Where it bites | Mitigation in this plan |
| --- | --- | --- | --- |
| 1 | `files/` naming and identity (the 63-bit key, the 4 GiB wall, the path-as-identity cache key) | wrong identity model → silent wrong data, and the bug is invisible at test time | §A §3.1; I1a `space.rs` with a never-panic property test; planning/05 §5.6 |
| 2 | GTE accumulator width and rounding | renders plausibly, drifts; only a golden trace catches it | I5a before I5b; the headless frame harness (planning/05 §4); the T4 integer camera program |
| 3 | multi-backend audio semantics (no multi-backend API today) | the reference has four audio stacks, not one transport | I3 defines + tests against two implementations; I5b implements |
| 4 | MDEC decode with no reference encoder | single oracle | decode invariants; DRGN1 cross-check; I10b cross-check |
| 5 | the FMV audio pipeline (six requirements with no single reference abstraction) | failure only becomes visible mid-FMV | the five-constraint list in I3 |
| 6 | the submap script bridge | a layer exists, the interaction contract is wrong | I7's bridge trace + harness scenario |
| 7 | 4 GiB and 16 MiB fallback correctness | only fires on specific inputs | the I1a property test, bounded, with its limit stated |
| 8 | performance of the non-OpenGL presentation path | late | deferred to I10b, recorded as deferred |
