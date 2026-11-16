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
| I5 (PSX models **+** wgpu/cpal backends) | **I5a, I5b** | hardware models are pure state machines; backends depend on the engine's frame contract. Built together, a renderer bug and a compatibility-policy bug are indistinguishable |
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
7. **No compatibility policy is implemented as a production-only mechanism.** Each keeps an in-memory, no-engine test backdoor (planning/03 §1.2).
8. **`crates/psxrt` never depends on `crates/psxrt-compat`.**
9. **Unresolved research items are not silent.** A module whose evidence is Blocked/Possible states that in its doc header and keeps its gate honest.

---

## 2. Milestones

### I0 — Workspace foundation + bit-exact primitives · **COMPLETE**

RNG, fixed-point, bpp addressing, blend equations, timing. 72 tests, 0 warnings. Delivered in commit `93c6b1b`.

Its crate layout and its `psxrt-compat/src/policy.rs` are superseded by this revision. The supersession is executed as part of I1a (planning/03 §4).

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
| `src/container/bpe.rs` | the BPE decoder **and its encoder** (round-trip testability, not a runtime need), plus the raw-LZSS variant; mode selection is data-driven (0 = raw, 1 = BPE, other → error) and is made **before** the payload is known to be a TMD/TIM/… | CL-AST-12 |
| `src/cache.rs` | the `files/` cache, `VERSION = 5`, the per-node incremental skip, the determinism/ordering constraints, transform failure → node skipped, not fatal, the whole-tree wipe below version 5 | CL-AST-03 |
| `src/pipeline.rs` | the orchestrator: the two halves (A cache rebuild, B patch application), the per-node transform registry, the unpack-thread entry, the init-gate signal, the rayon pool at depth 1 with the caller's workspace guard | CL-AST-03, CL-AST-12 |
| `src/patch.rs` | the patch **orchestration** only: the 353-entry `scripts.csv` walk, the CRC32 gate over the `#include` closure (not the single target file), the one-time backup, the restore-on-removal, behind an injected `ScriptPatchToolchain` trait. I1a ships the trait and a no-op recording stub; the real toolchain is I4 | CL-SCR-02, CL-SCR-04 (orchestration half) |

**Excluded.** Every content decoder (I1b). Runtime asset *loading* (I3 — `asset::service` consumes this crate's output). The script toolchain itself (I4). Any `unsafe`.

**Module boundaries to fix before coding.** §A `Disk`, `Cache`, `Tree` all implement one `Space` trait but must stay separate types — an early design that made `Cache` a `BTreeMap`-backed `Space` and passed it to the pipeline made the pipeline untestable without real files. §B the streaming-decompression path (`Decompressor::new().read_to_end()`) and the two-buffer path (`decompress_slice`) are *different*: the streaming path reads 4 bytes beyond the end of the payload. That is legal inside an 0x800-byte-aligned buffer and illegal at an exact slice boundary; the reference had to special-case `disc:` reads. Whether the Rust reader replicates it or asserts it away is an I1a decision, recorded either way.

**Crates.** New `psxrt-import`, `edition = "2024"`, `unsafe_code = "forbid"` at crate level.

**Dependencies.**
- `thiserror` (workspace) — the domain error enum lives here, not in `psxrt` (TD-25).
- `rayon` (workspace, **`rayon` not `rust-gpu`** — the crate named in the earlier draft does not exist; TD-17).
- `log` (workspace) — the "skip and continue" path needs a recording sink for tests (TD-25).
- No `opus`, no `wgpu`, no `cpal`, no `image`. No `psxrt` dependency is required by any I1a module.

**Verification.**
- Per-module unit tests, each naming its contract: the ISO9660 conformance test on a hand-built 4-disc image; the sector/byte split enumerated over the trailing-digit mask; the cache version + ordering rules; the pipeline determinism test (same synthetic tree → byte-identical `files/` output, run twice and on two thread counts).
- Integration: a hand-built ISO9660 image with the 4-disc structure, run end-to-end → the expected `files/` tree.
- Fixture policy: synthetic, hand-built, layout-derived. No proprietary bytes (planning/05 §5).
- Property tests: `read_file` **never panics** for any 63-bit value (the 4 GiB guard and the `region_mask & mask == 0x03` predicate — 192 bits of input domain), and the `key` round-trip (encode → resolve → decode).

**Gate.** `cargo test --workspace` green; the pipeline output is byte-stable across runs and across thread counts; the patch orchestration is idempotent (re-run → no-op) and self-healing (tampered cache → re-apply); no content decoder exists yet; `cargo clippy --workspace --all-targets` zero warnings; the file-ending audit passes.

**Risks.** The in-place reader (no whole image materialized) is a genuinely new surface — the reference reads in place, so the synthetic-image test is the only oracle. The 63-bit key space cannot be enumerated exhaustively; the property test is a bounded sweep, and that limit is stated rather than presented as coverage.

---

### I1b — Content & Sequence Layer

**Objective.** Decode every original content format into engine-ready form, and build the XA→Opus transcode. The pipeline already exists and already decides whether to call a decoder — so from here on, adding a format means registering a transform, not extending a pipeline.

**Prerequisites.** I1a.

**Scope.**

| Module | Content | Checklist rows |
| --- | --- | --- |
| `psxrt-import/src/render/tmd` | TMD + CTMD pair: flag semantics (`0x1` fixp unloadable, `0x2` CTMD), the 0x1c-byte object table, the 8-byte vertex/normal stride (normals ×2¹²), primitive runs, `updateTmdPacketIlen` on-disk run-length rewriting (CTMD-authoritative rule), re-encode from DRGN1 data | CL-AST-04, CL-AST-05 |
| `psxrt/src/render/clut` | the pure CLUT codec: CLUT entries + TPage + X/Y + GXY, a 5-bit `Texel` codec, BGR555↔RGBA8 for 16-bit CLUTs. **No VRAM, no draw submit, no blit.** No `vram` module is created — its absence is documented, so nobody later mistakes it for an omission | CL-AST-06 |
| `psxrt-import/src/render/tim` | TIM parse + **encode**, the writer-pinned layout, PXL sheet emission, MCQ strips (two palette subtables, 256 B header, 0x4000-byte sheets, 0x2000-byte palette sheets), `decode_16_to_rgba` → RGBA8, `decode_4_to_16` / `decode_8_to_16` | CL-AST-06 |
| `psxrt-import/src/effect/anim` | the ANIM family: plain, LMB (the only truly compressed member), CMB (a plain container of three plain sub-animations). **No "CMB2". No DRGN compression.** | CL-AST-07 |
| `psxrt-import/src/fmv/mdec` | MDEC frame decode: the 10-sectors/frame demux, VLC/IDCT; **plus a DRGN1 data cross-check** — `patches/scripts/DRGN1/254.diff` proves the `disc:FILE:…`/`bin`/`bin\` shape, an 0x800-byte alignment rule, that some files are compressed and some are not, and that the mode flag is data-driven. The cross-check validates the *file-space and decompression* model, not the MDEC bitstream | CL-AST-09 |
| `psxrt-import/src/audio/{sssq,sshd}` | parse-only: 44-byte header, 240-byte instruments, per-channel event streams, SFX note streams, the 16-channel music vs 24-channel SFX split, the SSHD sound container | CL-AUD-10 |
| `psxrt-import/src/audio/transcode` | the transcode pipeline: decode XA at 37800 → 80/63 Catmull-Rom → 48000 → Opus at 128 kbps. **The four parser layers exist in the reference; the decoder and resampler do not** — they are reverse-engineered, and the implementation is gated on cross-checking against real SSSQ + SSHD once a disc image is available | CL-AST-10, X4, X7 |
| `psxrt/src/audio/xa_adpcm` | the pure codec: the 2-bit XA filter **field** (not table index), the repeat-count nibble, the 16-step magnitude nibble, the 28/24-byte block geometry, the 1/48 shift, the ±32768 clamp, the start/loop/early-eof filter flags (X3), and the 24-byte XA header/sector model (X1). It does **not** model the SPU's own 5-bit filter; the difference is documented, not reconciled | X1, X2, X3 |

**Excluded.** The SPU voice model (I5a), the runtime `Vram` + queue-then-submit `Gpu` + `GpuKind` (I5a), the FMV/player state and `VideoPlayer` (I3, then I6), the audio player runtime (I5b), runtime asset loading (I3).

**Crate dependency.** This is the first legitimate `psxrt-import → psxrt` edge: `render/tim` and `audio/transcode` consume the pure `psxrt::gpu::clut` and `psxrt::spu::xa_adpcm` codecs. It is not a layering violation, and it is recorded as the reason TD-19's direction rule is now "the default, not absolute".

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

**New in this revision.** (a) `tag`, `save`, `config` live **inside** `severedchains-sdk`; there is no `severedchains-te`, no separate save/config crate. (b) Module-name collision check before implementation: the planned `render`, `audio`, `fate` names must be re-checked against the planned `psxrt` module names and must pass a "which layer decides?" test — if both layers could change a behavior, it belongs to neither yet. (c) **`fate` belongs in `severedchains-sdk` only if the SDK stays engine-independent**; otherwise it moves to the engine crate. The decision point is I4, not here. (d) The four-level policy registry moves to `psxrt-compat` in I1a, so the SDK's config framework is a *host* for L1 entries, never a transport for L3 policy ids.

**Dependencies.** `serde`/`serde_json` (data-mod manifest only), `thiserror`, `log`.

**Gate.** Contract suites green; the framework vertical slice passes (a full `boot_mods` → `boot_registries` → config-load → i18n sequence with a synthetic test-only mod); zero dependencies beyond the TD-24 list.

---

### I3 — Engine core vertical slice (`severedchains`)

**Objective.** The frame contract, the state machine, the lifecycle, and the service hosting exist and are conformance-tested **headlessly** (Noop backends) — the largest architectural risk isolated before any content or presentation.

**Prerequisites.** I0, I1a, I1b, I2.

**Scope.** Unchanged from the previous revision: `builder`, `frame` (the 21-phase driver, the per-context rates, frame-locked pacing, one-frame input edges, the frame-start task queue), `state`, `input`, `render` (backend trait + Noop backend), `audio` (backend trait + Noop backend, the two-thread structure, the edge-word protocol host), `load`, `save`, `error`, and the `scus94491` binary.

**New in this revision.** Four modules that the previous revision implicitly deferred to I5 now land here, because they are state-owning and device-free: `gpu::vram` (owned linear scratch — the `0x80000`-word requirement and the `0x20000`-word allocation fact, the 5-bit/8-bit read paths, and the explicit `upload`/`download`/`copy_vram_to_vram` transfer surface that the reference has and the earlier draft of `planning/01` said it did not), `gpu::draw` (the command vocabulary — **queue-then-submit, not direct-mode**, with the `DrawCommand`/`DrawTopology`/`FilterMode` set that must stay distinct), `audio::device` (the transport's own device state: `open`/`active`/`buffered`/`started`/`paused`), `audio::transport` (the `AudioTransport` trait with **one device-owning implementation** and exactly one compile-time selectable backend per build — no runtime probe-then-replace).

`audio::device` must satisfy **five** constraints, each an I3 test: (a) seek, (b) frame-accurate clock alignment, (c) the ≤ 1/60 s key-on quantization, (d) the `VSync` domain boundary for FMV, (e) the four parser layers. The I3 Noop transport satisfies (a), (b), (c), (e); (d) is asserted only in the FMV state's own unit tests and that ceiling is recorded.

`files/` and `state_data/` stay **runtime storage**, not `psxrt-compat` policy modules (planning/03 §3.1). The two are already fixed.

**Dependencies.** `winit`/`gilrs`/`cpal`/`crossbeam`/`parking_lot` under `PSXRT_WITH_*` only; `rayon` for the load pool; `psxrt` (default feature); `psxrt-import` (default feature); `severedchains-sdk` (default feature).

**Gate.** The headless vertical passes end-to-end (boot → state A → queue → state B → teardown → shutdown) with the full 21-phase trace captured and matching platform/02 §3; the transport conformance suite passes across two backends; no `AudioPlayer` in `psxrt`; no `compat::` reference inside `psxrt`.

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

Plus **policy activation** in `psxrt-compat`: the G6 ordering policy in the OT traversal, the T2 precision policy + the T4 integer submap camera program, the G4 clamp, the G11 rate-00, the X6 predictor policy, the S7 sweep policy, the S3 allocation chain (the 20→8 bound, the matching retrigger → bound-gated steal → round-robin → finished/unused), the G10 blend selection (the exact equations in OT replay, the B+F/4 decomposition in the modern path).

**Test strategy, per the test-layer rule in `planning/05` §2.** A pure function (`xa_adpcm_block`, the blend arithmetic) is tested exhaustively. A state machine (the ADSR) is tested by a scripted sequence over a fixed clock. A transport is tested against **two** implementations (I3's `AudioTransport`). A policy is tested by a **matrix fixture** (planning/03 §5) — the matrix for `blend_select` enumerates the 256 (mode, blend) combinations and both sides of the `B + F/4`/32 boundary; the matrix for `voice_alloc` enumerates every allocation scenario.

**Gate.** Every CD-attached row (G6, G4, G11, S7, X6, T2, the Q-74 note) has its policy, default, knob, divergence note, and register pointer in place; the matrix fixtures are generated and checked; the 43 M4-05 rows are covered; **no wgpu or cpal code exists**; the frame-boundary integration test (planning/05 §4, the headless frame harness) exists and guards the model, not a renderer.

**Risks.** This is the milestone where a wrong GTE accumulator width hides: a model that renders plausibly for 200 frames and drifts by one pixel per frame is invisible to visual inspection. That is why I5a and I5b do not merge.

---

### I5b — Native presentation and audio backends

**Objective.** The wgpu and cpal backends exist, are feature-gated, and consume the hardware models rather than reimplementing them.

**Prerequisites.** I1b, I3, I4, I5a.

**Scope.** `severedchains::render::wgpu_backend` — the 3 presentation modes (mode 0 = the OT replay at `GPU.endFrame`; modes 1/2 = the modern 2D/3D pools with the LIFO/FIFO equal-depth tie-breaks), the G2 Bpp sampling, the G9 texturing, the G12 mask bit, the G3 draw offsets, the G15 VRAM→VRAM copy, the LEGACY H-division pseudo-perspective, the scissor stack, the display swap + the G13 latency, the post pass, the resize/reprojection. `severedchains::audio::cpal_backend` — the device hosting + the 2 s reinit poll, the music thread (the per-sample BGM renderer over the parsed SSSQ — the behavioral mirror, not claimed sample-identical), the sample-accurate BPM timing, the sfx thread (the 60 tps SPU tick, the SFX sequencer facade, the 8.0 ms SFX ticks), the FMV player runtime (the staged 0–4 API, the 10 ms ticks, the 8 buffers, the ≥ 40 ms guard, the pre-queue 4, the one 480-sample frame per tick, the `playerVolume` SFX×MASTER, the unload at EOF, the drop-on-full backpressure), the volume/routing chains.

**Modern-path semantics carried from the user's brief, recorded as the default path (MP-01…MP-06, model defaults — *not* CD rows; see `planning/03` §7).**** Opaque scene queue built during the frame, resolved at draw time by depth then order-of-submission with deterministic equal-depth tie-breaking; three logical layers (background, scene, overlay) with **the world-map fog being the one exception that changes a key**, not the general rule; a 32-bit linear float HDR target with per-layer exposure control, a fixed reference luminance and a configurable headroom multiplier — the HDR chain is a real linear float pipeline, not "B-gradients plus a flag"; explicit alpha, no automatic blending, an explicit draw-time blend description when a primitive needs one; a mask surface that does not imply a fixed "no-blend → full-viewport-passthrough → second pass" execution; a 32-bit linear-float audio bus with software voice count and 8/15-bit volume semantics preserved *in the conversion boundary*, default equal allocation, a reverb send on top.

**Gate.** The wgpu smoke renders one OT frame; the cpal Noop-device path runs the thread structure without a device; **no visual-parity claim is made** (Blocked domain — no baseline; documented in the gate record).

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

**Scope.** The cross-crate integration suite: the `psxrt` models ↔ `psxrt-import` decode ↔ `psxrt-compat` policy override triangle, exercised through the engine and game crates where those are the consumer. Full frame-accurate replay tests. Audio model conformance (the 1500–3000-file replay corpus, the ≤ 1/60 s quantization observable end-to-end). Compatibility-contract conformance: **each policy is tested against both sides of its own boundary** (the two equal-z backends, the two blend modes, the two projection modes). The replay tool and the trace-comparison tool.

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
