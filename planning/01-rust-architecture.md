# P0-05 — Rust Architecture (Revised)

Status: **Revised (2026-11-15), superseding the 2026-11-14 P0 draft.** The revision is driven by the dependency analysis in `planning/04-implementation-roadmap.md` and the I0 source audit in `planning/07-i0-refactor-specification.md`. Decisions below carry `ADR-00n` labels; each is stated in full at its point of use — there is no separate ADR file, and the label is a citation handle, not a pointer to another document. The 2026-11-14 draft's §1 (design axioms A1–A13, freedoms L1–L12, the nine invariants) is **unchanged and still binding**; it is not reproduced here. What changes is the crate topology, the module map, the dependency rules, the naming rules, and the execution model.

**Two corrections, and they are separable.** The 2026-11-14 draft grew separate crates for the GPU, the GTE and the SPU because the research corpus is organized by hardware unit; that inverted the dependency, making the research taxonomy the implementation taxonomy. This revision's first pass over-corrected and read as a workspace-wide three-crate limit. Neither is the rule. The project is not a PlayStation emulator and not a PSX emulator framework: it is a modern cross-platform runtime for one game and its ecosystem, with an original-format export toolchain on the side. Hardware-shaped code appears **only where observable behavior, bit-exact replay, or a reverse-conversion requirement actually forces it** — and *where* it appears is decided per responsibility, by what the code does at runtime, not by a crate budget and not by which research document recorded it.

**A second revision pass (2026-11-15) corrected the `psxrt-compat` role itself.** The first pass fixed the dependency *direction* but left the *contents* wrong: §2 and §3 still allowed compatibility policy to live in `psxrt-compat`, and `planning/04` I5a still listed eight policy selectors as "`psxrt-compat` deliverables". Those statements contradicted this document's own dependency argument, and they are what §§1–3 and §5 now say instead. The eight shipped modules are dispositioned item by item in `planning/03` §5 and `planning/07` §3.

---

## 1. Crate boundary rule (ADR-001)

**There is no workspace crate-count limit.** A new crate needs a concrete responsibility and a defensible dependency boundary; it does not need a slot. Nothing in this document caps the number of crates, and nothing in it retires an existing crate. What is fixed is narrower and more useful: the **responsibilities** of the three `psxrt`-family crates, and the placement of the PSX platform services *inside* one of them.

### 1.1 The three `psxrt`-family crates

| Crate | Responsibility | Direction |
| --- | --- | --- |
| `psxrt` | the modern runtime. Equivalent runtime services for graphics, geometry, audio, timing, random streams and math — modern first, hardware-exact only where a documented fidelity requirement forces a PSX-derived model. Not a PlayStation emulator. | consumer of nothing below it |
| `psxrt-import` | original PlayStation asset → modern engine-ready representation: decoding, extraction, geometry/texture/audio/animation conversion, legacy numeric conversion, source-metadata preservation. **Read-only with respect to the original formats: nothing in this crate writes one back.** | original → modern |
| `psxrt-compat` | modern representation → original-compatible asset: encoding, quantization and hardware-limit validation, container reconstruction, replacement-asset generation, eventual disc reinjection and patching. Must not require a running engine. | modern → original |

The three exist because asset data flow has two directions and one substrate, and because a tool that turns a modern texture back into a TIM must link without a window, a device, or a game.

**What `psxrt-compat` must *not* contain.** Not a compatibility-mode registry. Not a feature-flag bag. Not a policy-routing layer. Not a selector enum that a runtime subsystem queries to decide what to do. Not a place to stage types that a later `psxrt` module will implement. Every one of those is runtime policy wearing a toolchain label, and every one of them puts `psxrt-compat` on the runtime path — the exact inversion §2 argues against. The test for membership is one question: **does this code produce, validate, or round-trip an original-format artifact?** If it instead *chooses how the game behaves*, it belongs to the subsystem that runs it — `psxrt`, a game subsystem, engine configuration, a test, or documentation.

**Consequence for the I0 tree.** All eight modules in `crates/psxrt-compat/src/` fail that question as written: they are enum selectors, format-constant carriers, and traceability metadata. None of them encodes anything. They therefore do not become the `psxrt-compat` architecture; they are redistributed (planning/03 §5, planning/07 §3). What `psxrt-compat` is *for* — TIM emission, CTMD re-encoding, audio re-encoding, disc reinjection — is unbuilt, and that unbuilt half is its actual scope.

### 1.2 What is *not* a crate

GPU, GTE, SPU, RNG, timing and math are **internal modules and submodules of `psxrt`**, not `psxrt-gpu` / `psxrt-gte` / `psxrt-spu`. Three reasons, in order of strength:

- `gpu` and its blitter are one pipeline. The command vocabulary and the strategy that executes it must not be separated by a published-API boundary that has no reason to exist.
- Three GTE precisions (software float, integer, exact-emulation) and two audio/presentation backends are **a selection inside the subsystem that runs them**, not crates and not a separate policy crate. The mechanisms are `psxrt` code; the selector is engine configuration; the evidence is documentation.
- The reference keeps SPU and XA in one package, and `XaPlayer` has no SPU reference at all. That is evidence about *functional* pairing, not about Cargo crates — and the three-surface rule (§3) keeps the boundary honest without paying for one.

### 1.3 The rest of the workspace

The engine, game, SDK and executable crates from the 2026-11-14 draft remain in scope and are **not** absorbed. Concretely, and as written in `planning/04`: the wgpu and cpal backends live in the **engine** crate (`severedchains::render::wgpu_backend`, `severedchains::audio::cpal_backend`); `tag`, `save` and `config` live **inside the SDK**, with no separate save or config crate; game content, states and persistence live in the game crate. The only 2026-11-14 proposal retired here is *per-hardware-unit crate splitting* (`sc-gpu`/`sc-gte`/`sc-spu` and their names).

**Module growth path.** A module may be split out into a crate later on a demonstrated need: external reuse, a dependency-isolation requirement, a compile-time measurement, or a hard language-boundary problem. Not on architectural taste, and not to keep a count.

## 2. Layer model: compatibility is a concern, not a layer (ADR-011)

Three vertical layers, and one horizontal axis that crosses all three.

| Layer | Crate | Asks | Provides |
| --- | --- | --- | --- |
| L1 modern runtime | `psxrt` | what does the modern runtime do? | runtime *mechanisms* — including PSX-derived ones where fidelity requires a mechanism |
| L2 original→modern | `psxrt-import` | how does original data become a modern representation? | decoders, demuxers, container readers, the unpack pipeline |
| L3 modern→original | `psxrt-compat` | how is a modern representation turned back into an original-compatible one? | encoders, quantizers, format validators, container writers, disc reinjection |

**Compatibility is orthogonal to the layered architecture — it is not a layer, and it is not a crate's contents either.** Three things get conflated under the word, and separating them is the whole point of this section:

| Kind | What it is | Where it lives | Test example |
| --- | --- | --- | --- |
| **Mechanism** | a way the runtime does something — an ordering strategy, a precision mode, a sample-rate table, an envelope evaluation, a memory clamp | `psxrt`, or the subsystem that executes it | "walk OT slot 31→0" is a mechanism |
| **Selection** | the choice of which mechanism runs, and when | the subsystem's own configuration, then the engine profile (planning/03 §2) | "battle use-battle-sort defaults to FIFO" is a selection |
| **Evidence** | what the original hardware did, and what `Unresolved` remains | documentation: the module's `PSX-Behavior Map` header, `DIVERGENCE.md`, the traceability matrix | "no retail byte has been read" is evidence |

What is **not** in any of the three, and what no crate may hold, is a *runtime-readable collection of compatibility choices* — a ledger, a flag bag, a `…Model` selector enum staged in a toolchain crate. That is the shape this revision retires.

**The dependency argument, unchanged and still the reason.** A `psxrt` module that reads a policy from a crate beneath it has inverted the dependency and created a cycle: the runtime would have to know about its own override surface, and the override surface would have to know about the runtime. The layer model therefore keeps a real job — it describes **asset data flow** (disc → decoded asset → frame). The subsystem taxonomy (display / effects / camera / math / world / audio) describes **state ownership**. Compatibility describes **which model a subsystem runs**. Three different axes; a crate boundary tracks the first one only.

Consequences, all three, not just the convenient one:
- `psxrt` ships a **modern-default** implementation of every subsystem, and it ships the PSX-derived models that fidelity requires. A game must be able to run with `psxrt-compat` entirely absent — not "would be nice", but the test: no runtime crate may list `psxrt-compat` as a dependency (§5 rule 1).
- A game may opt into a more compatible presentation or audio model. The opt-in lives in the engine (`severedchains::render::profile`, `severedchains::audio::backend`), and the *state* it selects is supplied by the game crate at boot. The opt-in is where compatibility *belongs*.
- **A compatibility escape hatch is a mechanism, so it is runtime code.** `CD-03`'s integer camera program and `CD-05`'s rate-00 mapping are compatibility behavior *that has no clean modern counterpart*; both are implemented in `psxrt` (`gte`, `gpu::pixel`) and neither has a `psxrt-compat` half. `cd04_display_clamp::apply` and `cd05_rate00::decode_tpage_mode0` already are runtime functions — the correction is to stop housing them in the export toolchain.

## 3. The three surfaces (ADR-011, the distinction most likely to be skipped)

Some subsystems have **three different surfaces**. Naming, module placement, and the crate boundary must reflect that they are different things:

| Subsystem | Surface 1 — representation (what the format/HW says) | Surface 2 — runtime execution (what runs per frame) | Surface 3 — reverse conversion (how it is written back out) |
| --- | --- | --- | --- |
| CLUT / pixel | `psxrt::gpu::clut`, `psxrt::gpu::pixel` — CLUT entries + TPage + X/Y + GXY, the 5-bit `Texel` codec, BGR555↔RGBA8 | `psxrt::gpu` (I5a) — VRAM, command queue, draw submit, blit | `psxrt-compat::texture` (I1b) — emit a valid TIM from a working sheet |
| ADPCM | `psxrt::spu::xa_adpcm` — the pure XA codec, **no SPU** | `psxrt::spu` (I5a) — the SPU voice model, its own 5-bit filter | `psxrt-compat::audio` (I1b) — re-encode a replacement track into a sector-accurate stream |
| audio out | `psxrt::audio` — sample format, channel count, clock rate, the `AudioTransport` contract | the device implementations (I5b), in the **engine** crate | — (no original audio container is written back) |
| models | `psxrt-import::render::tmd` — TMD/CTMD *decode* to the working model | `psxrt::gte` + `psxrt::gpu` — what executes | `psxrt-compat::model` (I1b) — CTMD/TMD *encode*, the round-trip gate |

Surface 1 is **platform description** and lives in `psxrt` (or, for a container, in `psxrt-import`). Surface 2 is **execution** and lives with the crate that runs it. Surface 3 is **reverse conversion** and is the only thing that makes `psxrt-compat` a crate. Nothing in surface 3 is read at run time; nothing in surfaces 1 and 2 writes an original-format file.

**VRAM: what actually exists (corrected 2026-11-15, against the reference).** An earlier draft of this section claimed the reference had "no VRAM write path". That was wrong and is withdrawn. `Gpu` has an explicit VRAM surface — `uploadData15` (two overloads), `downloadData15` (two overloads), `copyVramToVram`, `clear` — over a linear `vram15` store, plus `updateVramTexture()` which uploads the whole 1024×512 store to the GPU texture under `vramLock` **when `vramDirty`**, and is called per-object (`BattleEntity27c:772`), not at frame start. Command submission is a separate surface: `queueCommand(z, command)` + `resetCommandBuffer()`, bracketed by `startFrame()` / `endFrame()` (`Gpu.java:116–146`, `:202–360`, `:346–357`).

Two consequences that *are* load-bearing: (a) the module is `psxrt::gpu`, and it owns both the VRAM store and the command queue — they are one pipeline, which is §1.2's reason for not splitting them; (b) `updateVramTexture` is a **transfer**, not a policy, so it belongs to the runtime module and its execution strategy, never to a `psxrt-compat` policy record.

## 4. Module map

**28 modules listed — 10 in `psxrt`, 13 in `psxrt-import`, 5 in `psxrt-compat`.** The count is total, not per crate, and it is a count of *rows in this section*, not a cap: §1.3's rule is that a module may later become a crate on demonstrated need, and a module may be split (as `audio` is below) without the map's arithmetic mattering. Each entry: module → what it owns → layer → the milestone where it is built. **The `L1` / `L2` / `L3` column is now the layer of the *data flow*, per §2 — it no longer means "this row is a compatibility policy".** Where a module both models a PSX mechanism and offers more than one way to run it, the row says so and names who supplies the choice.

### `psxrt`

| Module | Owns | Layer | Milestone |
| --- | --- | --- | --- |
| `math::trig` | `rsin`/`rcos` + the π/180 factor. Public **inside the workspace** (the game layer's collision and effect math calls it), kept off the general-purpose utility surface by module path — `psxrt::math::trig::sine`, not a crate-root re-export. See §6.5 for the naming caveat. | L1 | I0 (done) → repath I5a |
| `math::angle` | the 4096/turn (PSX degree) unit — and that is all I0 delivers: `PSX_DEG_PER_REV`, `PSX_DEG_TO_RAD`, `TRIG_SCALE`, all three inside `fixedpoint.rs`. The 65536/turn unit, the wrapping rules, and the conversions between the two units are **I5a work**, not delivered by I0 | L1 | I0 (the factor only) → I5a |
| `rng` | the three generator families as deterministic streams (ADR-006) | L1 | I0 (done) |
| `gpu::clut` | see §3, surface 1 | L1 | **I1b** |
| `gpu::pixel` | the BGR555 family, `Texel`, RGBA8, the 4-bit/8-bit index read paths, the mask/alpha semantics | L1 | I0 (4-bit addressing done) → I1b (the colour paths) |
| `gpu` | the facade (**queue-then-submit**), `GpuCommand`, `CmdBuf`, `DrawCommand`, `DrawTopology`, `FilterMode`, the `Blitter` trait + the Native impl, the VRAM store + the transfer surface of §3, the CLUT/TPage decode. The PSX-derived **ordering model** (the 32-slot reverse-order OT, the same-z tie order, the VRAM half-width) also lives here, as strategies a caller selects — not as a `psxrt-compat` import | L1, + the L3 models it can run | **I3**, completed I5a |
| `gte` | the `Rotation2d`/`Transform3d`/`TransformMode` vocabulary, the three precisions, the 16-bit input truncation, the overflow sentinel, the lighting-mode routing. `TransformPrecision` is declared here because the transforms are implemented here — that makes it a **runtime strategy parameter**, not a compatibility policy, and it is supplied by the render profile that constructs the transform | L1, + the L3 models it can run | I5a |
| `spu` | the voice model (ADSR, voice ADPCM, pitch, the reverb network), the master-atomic voice sweep, and — at I1b only — the XA sector geometry (`spu::xa_adpcm`, the pure codec: the 28-byte frame layout, the 8-shift/14-shift split, the two filter definitions, the 512-byte sector geometry). **The encoding half of that codec is `psxrt-compat::audio` (§3 surface 3); its decoding half is `psxrt-import::audio::transcode`.** | L1, + the L3 models it can run | I1b (the geometry) → I5a (the model) |
| `audio` | the sample contract: `SampleFormat`/`SampleRate`/`ChannelLayout`, the `AudioTransport` trait. Split at I3 into `audio::transport` (the trait, with **one** device-owning implementation and one compile-time backend per build) and `audio::device` (the device state: `open` / `active` / `buffered` / `started` / `paused`) | L1 | I3 |
| `clock` | the game clock. **Keeps only the `tick()` and `sync()` cadence entry points — not the 21 phase hooks, not `Sleep`-style host sleep** | L1 | I0 (done) → reworked I3 |

There is **no `psxrt::compat` module.** The first pass of this revision listed one (`Feature` vocabulary + `GameConfig` shape + a `CompatFlags` bitset) and it is withdrawn: a bitset of cross-subsystem flags is the same design error as the retired `CompatPolicy`, moved one directory. What remains in `psxrt` is *per-subsystem* strategy types with named consumers (`gpu::OrderingStrategy`, `gpu::SameZOrder`, `gte::TransformPrecision`, `spu::VoiceCapacity`) — mechanisms and their parameters, each owned by the module that executes it, each supplied explicitly by a caller. There is no workspace-wide flag collection at any layer.

### `psxrt-import`

| Module | Owns | Layer | Milestone |
| --- | --- | --- | --- |
| `space` | the canonical file-space abstraction: the 63-bit address, the four kinds, the four namespaces, DRGN-name resolution, the `disc:`/`region:`/`object:` access path, the three error codes (planning/07 §3.1) | L2 | **I1a** |
| `container::iso` | the `files/` cache reader — region-major 63-bit keys, the 4-byte size header, four depth levels, path-length-bound keys | L2 | I1a |
| `container::mrg` | the MRG container; per-file *no*-compression rule | L2 | I1a |
| `container::bpe` | the BPE **decoder**, the raw-LZSS variant, the three-mode flag. The encoder half is `psxrt-compat::container` and reaches these tests as a `dev-dependency` (§5 rule 3) — it is not a second half of this module | L2 | I1a |
| `cache` | the `files/` cache **reader** (the derived-format side of §3 — a cache is not an original format, but it is still read, not written, here). The writer that regenerates cache entries is `psxrt-compat::disc`, which also owns `VERSION = 5` on the write side | L2 | I1a |
| `pipeline` | the orchestrator, the rayon pool, the 63-bit key builder, the streaming decoder | L2 | I1a |
| `render::tmd` | TMD/CTMD **decode** to the working model, the `TmdFormat` vocabulary, `updateTmdPacketIlen`. The encoding half is `psxrt-compat::model`; this module supplies the model it encodes from | L2 | I1b |
| `render::tim` | TIM parse, the PXL sheet, the MCQ strip, `decode_16_to_rgba`, `decode_4_to_16`/`decode_8_to_16`. **Read-only:** a working sheet, once edited, is handed to `psxrt-compat::texture` to be emitted as a TIM | L2 | I1b |
| `effect::anim` | the ANIM family — plain, LMB (the only truly compressed member), CMB (a plain container of three plain sub-animations) | L2 | I1b |
| `fmv::mdec` | MDEC decode + the 10-sectors/frame demux, with a DRGN1 data cross-check | L2 | I1b |
| `audio::sssq` | SSSQ parse-only | L2 | I1b |
| `audio::sshd` | SSHD parse-only | L2 | I1b |
| `audio::transcode` | decode → resample → **encode to Opus**, and `AudioFileInfo`. **This is the one place a writer sits in `psxrt-import`, and it is not a reverse-conversion module:** Opus is a modern replacement container, not an original PlayStation format, and the write exists so a converted asset can be cached and re-read by the decoder above it. An original-format audio writer (XA, SSHD, SSSQ) is `psxrt-compat::audio` | L2 | I1b |

### `psxrt-compat`

Five modules, all of them encoders, writers, or validators. **None is a policy module, and none is on the runtime path.** The eight policy modules that I0 shipped here, and the collecting framework in `lib.rs`, are dispositioned one by one in `planning/03` §5 and `planning/07` §3 — none of them is carried forward.

| Module | Owns | Layer | Milestone |
| --- | --- | --- | --- |
| `container` | the writers that make a round-trip possible: an MRG writer, a SEADS/BPE encoder, a `files/` cache writer, for use as **test oracles** for `psxrt-import`'s decoders. `pub(crate)` or test-only unless a tool actually calls them — promoting one to a public export API is a decision, not a default | L3 | I1a |
| `texture` | TIM emission from a working sheet (PXL or MCQ): header + CLUT/image blocks, 4/8/15/24-bit emission, quantization of over-wide sources, the `B0` (no transparency) and `B1` (24-bit) flags | L3 | I1b |
| `model` | TMD and CTMD **encode**: reverse the I1b decomposition, re-emit the 24-byte header, the 32-byte GPU orders and the per-model geometry, and the CTMD block packing. Its input is the working model that `psxrt-import::render::tmd` produces, so the round-trip gate needs both crates — as a **test** dependency, named in the manifest (§5 rule 3) | L3 | I1b |
| `audio` | re-encode a replacement track into a sector-accurate stream: XA encoder, the sector/interleave layout writer, the SSHD/SSSQ writers. The reference has **no** SPU or XA encoder — this is designed from the documented sector format, so it is a greenfield reverse-conversion module, not a port | L3 | I1b |
| `disc` | disc **reinjection**: write a changed file back into a read-only-referenced ISO9660 layout and regenerate the affected `files/` cache entries. Blocked on P-02/R-01 — it cannot be built or verified without real disc bytes | L3 | I10b, conditional |

**What a round-trip gate is, and where its code lives.** `decode → encode → compare` is a **test**, and it lives in the `tests/` tree of the crate that owns the round trip (planning/05 §2). The encoder it drives lives in `psxrt-compat` or is test-local. A test that compares a decode against a reference oracle is *not* a reverse-conversion product surface and must not be cited as the reason `psxrt-compat` holds a policy selector.

## 5. Dependency rules (ADR-001, ADR-008)

**Allowed, permanently:**
```
psxrt-import ──► psxrt                      (I1b onward: TIM + transcode consume clut + xa_adpcm)
psxrt-compat ─► psxrt                       (an encoder reuses a runtime format model)
psxrt-compat ─► psxrt-import                (only where a round-trip tool needs the decoder)
severedchains ────────────► psxrt, psxrt-import          (NOT psxrt-compat — see rule 1)
severedchains-game ───────► severedchains + the three above, minus psxrt-compat
severedchains-sdk ────────► whatever it needs to host scripts, registries, i18n and config
scus94491 (bin) ──────────► everything, tool subcommands only, never on the boot path
```

Any further crate added later joins this graph by *stating its edges*, not by inheriting a slot. A crate that cannot name who consumes it and why the boundary is a crate rather than a module is the case ADR-001 rejects.

**Rules.**

1. **No runtime crate depends on `psxrt-compat`, and `psxrt-compat` is never on the boot path.** This is stronger than the rule this section previously stated ("`psxrt` never depends on `psxrt-compat`"), and the widening is the correction: the narrow version still let the *engine* read a policy out of the export toolchain, which re-creates the inversion one level up. The engine selects a model by supplying a parameter to a subsystem it already depends on (`psxrt`, `severedchains::render::profile`), not by importing the crate that writes TIMs. Where a shipped binary needs a tool subcommand that touches `psxrt-compat`, it is a separate command, off the boot path, and it says so.
2. **The default direction is `psxrt-import → psxrt`, and it is a default, not an absolute.** The earlier rule — import must never depend on psxrt — is retired. Both surfaces of CLUT and ADPCM live in `psxrt` for representation reasons; `psxrt-import` consumes them. That is the first legitimate use of the edge, and the reason the rule is now stated this way.
3. **Test-only exceptions are named in the manifest.** Two are planned: `psxrt-compat`'s test build may read `psxrt-import`'s test utilities and its decoders (the round-trip gate), and `psxrt-import`'s test build may call a `psxrt-compat` writer to synthesize a fixture. Both are `dev-dependencies`, both are named at the point of use, and neither is a permanent crate dependency.
4. **Module visibility does not substitute for dependency direction.** A `pub(crate)` type still crosses a boundary if a trait signature exposes it.
5. **A new public trait may not smuggle a hardware identity through its name or its API surface** (ADR-008, carried forward).
6. **Workspace members are enumerated, not globbed.** `members = [...]` is spelled out; `crates/*` is not used. This is a *hygiene* rule — an unexpected crate should not appear silently — and it is **not** a cap on the member count. What is prohibited is an empty placeholder directory and a module "to be created later"; a crate with a real responsibility and a stated dependency direction is added by the milestone that needs it. `unsafe_code = forbid` and edition 2024 apply workspace-wide, whatever the member count.
7. **No repository-level Rust dependency on Java sources** (ADR-009). Findings are transcribed into `PSX-Behavior Map` headers.
8. **`unsafe_code = forbid`** at the workspace level, applied to all three crates, with a per-file override only where a documented need exists. The reference's `SeekableByteChannel` + `map16` read pattern is replaced by `read_at` into aligned scratch buffers — **plain safe Rust, no `mmap`, no `unsafe`** (ADR-012).
9. **Edition 2024.** A single `Cargo.lock` at the workspace root.
10. **Default members** are the three libraries. `cargo test` without a `--workspace` flag must not build backends that are not being worked on, and CI must not become slower than the work that was actually done.
11. **`psxrt-compat` must link and test without a window, a device, an engine, or retail bytes.** That is the architectural requirement, and it is what distinguishes the corrected role from the retired one: a policy crate satisfies it accidentally (it happens to reference nothing), an export toolchain satisfies it by construction (its inputs are data, its outputs are data).

## 6. Naming and module-boundary rules (ADR-002, ADR-003 — revises TD-23)

### 6.1 What may be named after the original

| Allowed | Example | Why |
| --- | --- | --- |
| register semantics | `TPage`, `Texel`, `CmdBuf` | these are the actual PSX/GPU concepts; the name carries information a coined name would lose |
| hardware-derived data layouts | `Bgr555`, `XaAdpcm`, `Ssd` | the name names the format, not the emulator |
| deterministic replay primitives | `Lcg15`, `Lcg16` | the algorithm **is** the identifier; `Lcg` is explicitly allowed |
| module-qualified subsystem names | `gpu::DrawCommand`, `spu::VoiceModel`, `audio::XaTransport` | the module already provides the namespace |

### 6.2 What must not

- A `psx_` prefix where the module namespace already says it. `gpu::DrawCommand`, **not** `psx::gpu::PsxDrawCommand`. A module named for a hardware unit is permitted (§1.2 fixes `gpu`/`gte`/`spu` as internal module names); a *type* named for one is not, unless the name is register vocabulary (`TPage`, `Texel`, `CmdBuf`).
- A public `GpuKind::PsxSoftware` discriminant. A project-specific enum value is the second half of the name. **`Native`, not `Hardware`.** `Hardware` implies the crate models hardware; it does not, and it must not want to.
- An emulator-framework surface. **No `Platform`, no `Backend` trait, no `Clock`, no `Memory`, no `Components`, no `GamePak`** (the Dig-Dabas names — Dig-Dabas is a reference for *understanding* the original, not the Rust API target).
- An internal helper promoted to public because it was convenient to test.
- A Java identifier transliterated into Rust. Transliteration does not satisfy the naming policy; the policy requires a name grounded in observed responsibility.
- A placeholder name (`Todo`, `Unused`, `Dummy`, `Stub`, `Placeholder`, `Unknown`) for a module that does not exist yet.

### 6.3 Three word orders, chosen deliberately and by layer

| Pattern | Example | Use |
| --- | --- | --- |
| `domain::capability` | `gpu`, `gpu::clut`, `gte`, `spu`, `audio` | the default |
| `format::kind` | `container::bpe`, `fmv::mdec` | an import-layer decoder, where the format identity is the information |
| `role::capability` | `container::bpe` paired with a `psxrt-compat::container` writer | where a format has two directions and the pairing is the information |

A name must say **what it does**, not **which emulator component it imitates**, and it must not name a *policy* where it contains a mechanism. `Blitter` (what it does) over a hardware-unit imitation; `Native` (what this build runs) over `Hardware` (what this build pretends to be). A module whose name is a noun phrase about compatibility (`same_z`, `blend_select`, `display_clamp`) rather than about a transformation is a signal to re-read §2.

### 6.4 Module-name conflict rule (four steps, mandatory before implementing a module)

1. **Name the input.** What data enters, in what representation?
2. **Name the output.** What leaves, and who consumes it?
3. **Does it own hardware state?** If yes, it is a runtime module, not a codec.
4. **Can a test construct it without a running engine?** If no, the boundary is in the wrong place.

Then: check the name against the *other* crate's planned module list. A collision means the boundary is wrong — **not** that a prefix is needed. Adding `import_` to fix `import::tim` against `psxrt`'s colour-decode surface is the wrong fix; it hides a layer-ownership error behind a naming convention.

**Worked example.** The CLUT codec exists at two layers. Resolution: `psxrt::gpu::clut` (the representation and codec, I1b) with `psxrt::gpu` (the runtime, I5a). Not two `tim` modules, and not `tim::clut` plus `gpu::clut`. `Tim` is a file format; its header carries a CLUT; a CLUT is not a file format.

### 6.5 Identifier provenance

Two distinct kinds of comment, and they must not be merged:

| Section | Contains | Where |
| --- | --- | --- |
| `PSX-Behavior Map` | original behavior and reference-implementation facts: file, revision, commit, function, line | the module doc header |
| `REMAP.md` | one line per **original identifier** that survives into a comment | three places, deliberately |

The three places, because a single table cannot answer three different questions:

| Location | Answers |
| --- | --- |
| `crates/psxrt/src/REMAP.md` | where a subsystem's original names live in the runtime |
| `crates/psxrt-import/src/REMAP.md` | where a format's original names live in the import layer |
| `research/game/*/REMAP.md` | where a state's original names live in the game module |

Scope note, so this is not mistaken for a runtime-wide map: the existing `crates/psxrt/src/REMAP.md` covers ten entries across `fixedpoint.rs`, `bpp.rs`, `blend.rs`, `timing.rs`, and the rng modules. That is a map of the code that exists. `rsin`/`rcos` move to `math::trig`; the map entry moves with them and the old location keeps a pointer.

Three rules hold wherever the map lives:
1. A comment citing an original identifier without a corresponding map entry is a gate failure.
2. A map entry citing a `research/` document that does not exist is a gate failure.
3. A reference-only name whose provenance is unknown must say so. **`rsin`/`rcos` are marked as reference-implementation names, with `Unknown` status for original-MIPS provenance** — the repository does not establish whether they are the game's own labels or decompiler labels. `Rsin`/`Rcos` as Rust fn names would be transliteration and are not permitted.

`scripts/audit_file_endings.py` is extended to scan comments for placeholder and transliterated-identifier patterns (planning/05 §8).

## 7. Execution model

### 7.1 Three clocks, one of which is not a clock

The I0 record distinguished host wall time from a virtual game clock and deliberately left the host clock out of `psxrt`. That conclusion holds, with the reasoning corrected:

- **The reference's game clock is empty.** `GameClock.tick()` and `sync()` are empty methods (verified at I0 preflight). So the honest description is *"a cadence contract with one non-observed write to `currentTime`"* — and **four** distinct things that must stay separate:
  1. **VSync / display cadence** — a display concept, no host timer implied;
  2. **sim tick** — the frame-loop iteration count;
  3. **scheduling** — countdown counters (`SoundSource28.tick()`, `XaPlayer.tick()`, `VoiceCounter`, the SFX-sequencer wait counters), which are tick-gated, not time-gated;
  4. **the host sleep** — a pacing mechanism living in the driver or the platform layer, **not** inside `clock`.
- **`clock` keeps `tick()` and `sync()` and nothing else.** A phase-0 `endFrame()` *is* the display-swap step (platform/02 §3), so the 21 phase hooks do not belong in `psxrt`; they are the game's frame driver. The `Clock.sleep(Long.MAX_VALUE)` idiom is a host-clock sleep and does not move either.
- **`frame_count` semantics are preserved** — 60 Hz, halved at 640-wide, and the 4× write in the FMV demux loop (the `for (; b != 4; frame_count.inc())` shape).
- **The GTE does not consume `frame_count` or `tickCount` directly.** No reference module exists for a Rust `gte` module to mirror, so this is a boundary fact, not a policy to choose. What *is* preserved is the **cache-invalidation trigger `PSDCNT & 3 == 3`, derived from the frame counter**, and it is named for its function: the "matrix-cache invalidation" section.

### 7.2 Frame pipeline (the layer split)

Re-derived 2026-11-15 from `Scus94491BpeSegment.gameLoop()` (`:138–175`) rather than carried from the earlier draft, which had two errors in this table. Verified order:

| # | Call | Owner | Notes |
| --- | --- | --- | --- |
| 1 | `endFrame()` | engine | runs **first**, not last. In-source reason: "the frame isn't actually ending when this callback completes… we can just do that stuff before we actually start rendering anything" (resize jank). This is the frame-boundary teardown point |
| 2 | `GPU.startFrame()` | `psxrt::gpu` | the frame marker; the VRAM store becomes readable by draw commands |
| 3 | `timestamp_a0 += vsyncMode` | `clock` | gated on `advancesTime()`. `vsyncMode` is 1 or 2 — the 60 Hz / halved cadence, and the *only* place the halving is applied |
| 4 | `hz = 60 / frames * speedMultiplier`; `window().setFpsLimit(hz)`; `PLATFORM.setInputTickRate(hz)` | engine / platform | **host-clock pacing lives here**, in the window and platform layer. This is the evidence for keeping the pacer out of `psxrt` |
| 5 | `loadQueuedOverlay()` | engine | the queued overlay/state change |
| 6 | `renderUi()` | engine | |
| 7 | `state.tick()` | game | the gameplay tick |
| 8 | `EVENTS.postEvent(RENDER_EVENT)` | sdk | the render hook |
| 9 | `loadAndRenderMenus()`; `uploadRenderables()` | game | the `uploadRenderables` call is marked in-source as temporary, pending the controls move |
| 10 | `SCRIPTS.tick()` → `state.postScriptTick(...)` | sdk + game | |
| 11 | `tickAndRenderTransitionIntoBattle()`; `handleFullScreenEffects()` | game | |
| — | the audio and FMV ticks | `audio` | **not in `gameLoop`.** SFX/XA ticks are driven from `AudioThread`, a separate thread; see §7.3 |

Two corrections against the earlier draft of this table, both recorded because both were believed for a milestone:

- **"frame-start task flush" does not exist.** There is no task-queue drain in `gameLoop`. The nearest thing is `updateVramTexture()`, and it is called per object from `BattleEntity27c:772` — a texture refresh, not a frame-start step. A design built on a frame-start flush would have been built on nothing.
- **`uploadDirtyVramTextures` is not an identifier in the reference.** The real one is `Gpu.updateVramTexture()` (`Gpu.java:116`), and the lock is a Java `synchronized(vramLock)` monitor, not an acquired lock. Naming a Rust hook after a mistranscribed identifier guarantees the two trees drift.

Threading contract: game logic is single-threaded; the frame contract never requires a `Gpu` reference to cross a thread. **Async asset loading must not write raw VRAM bytes directly — this is a requirement.** It may produce decoded assets, and it may upload through the explicit transfer surface (`gpu::upload_to_rect` / `gpu::copy_vram_to_vram`, the Rust counterparts of §3's `uploadData15` / `copyVramToVram`) under the VRAM lock, at a point the engine names.

### 7.3 Audio stack (four layers, one device owner)

| Layer | Crate | Content |
| --- | --- | --- |
| hardware model | `psxrt` | the SPU model (the ADSR state machine, the voice ADPCM, pitch, the reverb network), the pure XA codec. The **envelope-vs-gap choice** (CD-06) and the **voice-capacity choice** (CD-07) are parameters of this model, supplied by whoever builds it — a `SweepMode` parameter and a `VoiceCapacity` parameter, not a lookup into another crate |
| runtime / service | `psxrt` | the transport contract and the player semantics |
| selection | `severedchains::audio` (I5b) | the audio profile: which predictor model, which sweep mode, which clock domain each layer runs, and how many concurrent streams. The state itself is supplied by the game crate at boot (planning/03 §2, level L2) |
| device backend | `severedchains` (I5b) | `cpal`, the device-hosting implementation |

Three facts that constrain the design. (1) and (2) were re-checked against the reference on 2026-11-15; (3) is carried from `research/audio/01` + `research/psx/03` and is not re-verified here.

1. **The reference has no multi-backend audio API.** It has *four stacks*: a JavaFX-side player (`Audio` → `XaPlayer`/`GenericSource`), a PC-native player (`Sequencer` — the BGM), a PC-SFX player (`SoundSource28`), and the SPU side (`Spu`, `XaAdpcm`, `Voice`, `VoiceCounter`, `Sssq`). Hosted by one `AudioThread`. One device-owning implementation, one compile-time selectable backend per build. (`NativePlayer` appears in the M4 inventory as present-but-unused; treat that as research-carried, not re-verified.)
2. **`Spu` and `XaPlayer` share almost nothing** except that both are audio: there is no `AudioPlayer` type in the reference, and no shared player abstraction between the two. So the split into *device*, *transport*, and *voice model* is not a tidy three-way: `XaPlayer` reaches from device hosting into transport, and `VoiceCounter` reads a raw sample byte directly out of `Voice`, bypassing SPU register semantics.
3. **The two-clock rule.** Sequencer tick rates (8 ms, 16 ms, the SFX default) are a *reconstruction* — reference-specific, layer-4. The clock the game scripts were written against is the **SPU 60 tps clock**, and **SFX scheduling keys to it.** The reconstruction is therefore not optional; it must match the clock the scripts expect. **Sample-accurate scheduling is a `psxrt` runtime capability.** Which clock a given reference layer uses is a *selection*, recorded per layer in `research/audio/*` and pinned by the model-conformance fixtures of planning/05 §2 — not a value read out of `psxrt-compat` at run time.

`XaPlayer` is **not** reproduced as a `psxrt` public type. Its semantics are preserved; the player state is owned where the state machine lives. **A player type in `psxrt` is explicitly out of scope** — the reference has no such type either (§7.3 fact 2), so this costs nothing in fidelity. Transport state lives in `audio::device` (the five states: `open` / `active` / `buffered` / `started` / `paused`) with the clock-domain, buffer, and volume semantics that state must carry.

### 7.4 FMV stack

| Layer | Crate | Content |
| --- | --- | --- |
| format decoder | `psxrt-import` (I1b) | MDEC frame decode, the 10-sectors/frame demux, `Ssd` |
| player state machine | `severedchains` + `severedchains-game` (I3, then I6) | `VideoPlayer`, `Fmv`, the `FmvRuntime`, the video/audio thread skeleton, the state machine, the player semantics |
| audio selection | `severedchains::audio` (I5b) | which predictor model and which clock domain a layer runs — supplied to the `psxrt` model as a parameter, recorded in the audio-profile fixtures |
| device backend | `severedchains` (I5b) | `cpal` |

The 15 fps / sector-quad frame clock lives with the player, not in `clock`. FMV audio is **a third clock domain**, not a mode of the BGM clock. The reference comment "No attempt is made to stay in sync with the video stream" is a fact to write down. **The FMV plumbing stays in the first game vertical slice (I6)** — that is why I5 precedes I6.

The FMV demux loop does four things, and the split is not one-to-one: it reads **30 bytes** of header per frame (not 24 — four separate audio portions plus four channel values), it decodes the audio (including the silent-A/D special case), it copies raw MDEC bitstream into the command buffer, and it issues the MDEC decode. The two audio halves and the two video halves use **one shared 1760-byte buffer**, the only two `CmdBuf` users, which is legal only because playback is strictly sequential (FMV, then battle, never overlapping). **`CmdBuf` is therefore owned linear scratch, not a reusable pool, and the 0x80000-word requirement is a documented allocation fact, not an arbitrary choice.**

## 8. What this document deliberately does not decide

The TD items (`planning/02`, TD-25 … TD-28 are the new/changed ones), the CD dispositions (`planning/03`), the milestone gates (`planning/04`), and the test strategy (`planning/05`).

Three things the `psxrt-compat` correction leaves genuinely open, so that "not a policy crate" is not read as "fully designed". These are placement questions, separate from the three decision items carried in `planning/07` §7 (`TrigonometryModel`'s disposition, fixture granularity, the naming/map scanner); both sets are open, neither blocks I1a.

1. **Are the `container` writers a product surface or test-only?** An MRG/BPE writer built only to prove a decoder is test scaffolding and stays `pub(crate)`; one a modder calls to author a replacement archive is a product surface. The answer changes the crate's public API but not its dependency edges, and each case is decided at the milestone that builds it (I1a for containers, I1b for texture/model/audio).
2. **Does `psxrt-compat` depend on `psxrt`, on `psxrt-import`, on both, or on neither?** The graph allows all three shapes. The decision is per-module and is forced by where the shared format model is declared — `psxrt::gpu::pixel` for pixel formats, `psxrt-import::render::*` for container parsers. Decided at I1b.
3. **Is disc reinjection a milestone of its own?** `disc` is listed at I10b *conditional on P-02/R-01*: it cannot be built or verified without real disc bytes, and a reinjection writer with no disc to test against is exactly the placeholder §5 rule 6 prohibits. If the disc question resolves before I10b, it becomes its own milestone; if not, it stays unbuilt rather than stubbed.
