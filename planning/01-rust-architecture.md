# P0-05 — Rust Architecture (Revised)

Status: **Revised (2026-11-15), superseding the 2026-11-14 P0 draft.** The revision is driven by the dependency analysis in `planning/04-implementation-roadmap.md` and the I0 source audit in `planning/07-i0-refactor-specification.md`. Decisions below carry `ADR-00n` labels; each is stated in full at its point of use — there is no separate ADR file, and the label is a citation handle, not a pointer to another document. The 2026-11-14 draft's §1 (design axioms A1–A13, freedoms L1–L12, the nine invariants) is **unchanged and still binding**; it is not reproduced here. What changes is the crate topology, the module map, the dependency rules, the naming rules, and the execution model.

**Two corrections, and they are separable.** The 2026-11-14 draft grew separate crates for the GPU, the GTE and the SPU because the research corpus is organized by hardware unit; that inverted the dependency, making the research taxonomy the implementation taxonomy. This revision's first pass over-corrected and read as a workspace-wide three-crate limit. Neither is the rule. The project is not a PlayStation emulator and not a PSX emulator framework: it is a modern cross-platform runtime for one game and its ecosystem, with a compatibility-export layer on the side. Hardware-shaped code appears **only where observable behavior, bit-exact replay, or `psxrt-compat` actually requires it** — and *where* it appears is decided per responsibility, not by a crate budget.

---

## 1. Crate boundary rule (ADR-001)

**There is no workspace crate-count limit.** A new crate needs a concrete responsibility and a defensible dependency boundary; it does not need a slot. Nothing in this document caps the number of crates, and nothing in it retires an existing crate. What is fixed is narrower and more useful: the **responsibilities** of the three `psxrt`-family crates, and the placement of the PSX platform services *inside* one of them.

### 1.1 The three `psxrt`-family crates

| Crate | Responsibility | Direction |
| --- | --- | --- |
| `psxrt` | the modern runtime. Equivalent runtime services for graphics, geometry, audio, timing, random streams and math — modern first, hardware-exact only where a documented compatibility requirement forces it. Not a PlayStation emulator. | consumer of nothing below it |
| `psxrt-import` | original PlayStation asset → modern engine-ready representation: decoding, extraction, geometry/texture/audio/animation conversion, legacy numeric conversion, source-metadata preservation | original → modern |
| `psxrt-compat` | modern representation → original-compatible asset: encoding, quantization and hardware-limit validation, container reconstruction, replacement-asset generation, eventual disc reinjection and patching. Must not require a running engine. | modern → original |

The three exist because asset data flow has two directions and one substrate, and because a tool that turns a modern texture back into a TIM must link without a window, a device, or a game.

### 1.2 What is *not* a crate

GPU, GTE, SPU, RNG, timing and math are **internal modules and submodules of `psxrt`**, not `psxrt-gpu` / `psxrt-gte` / `psxrt-spu`. Three reasons, in order of strength:

- `gpu` and its blitter are one pipeline. The command vocabulary and the strategy that executes it must not be separated by a published-API boundary that has no reason to exist.
- Three GTE precisions (software float, integer, exact-emulation) and two audio/presentation backends are **configuration inside a module**, not crates.
- The reference keeps SPU and XA in one package, and `XaPlayer` has no SPU reference at all. That is evidence about *functional* pairing, not about Cargo crates — and the two-surface rule (§3) keeps the boundary honest without paying for one.

### 1.3 The rest of the workspace

The engine, game, SDK and executable crates from the 2026-11-14 draft remain in scope and are **not** absorbed. Concretely, and as written in `planning/04`: the wgpu and cpal backends live in the **engine** crate (`severedchains::render::wgpu_backend`, `severedchains::audio::cpal_backend`); `tag`, `save` and `config` live **inside the SDK**, with no separate save or config crate; game content, states and persistence live in the game crate. The only 2026-11-14 proposal retired here is *per-hardware-unit crate splitting* (`sc-gpu`/`sc-gte`/`sc-spu` and their names).

**Module growth path.** A module may be split out into a crate later on a demonstrated need: external reuse, a dependency-isolation requirement, a compile-time measurement, or a hard language-boundary problem. Not on architectural taste, and not to keep a count.

## 2. Layer model: compatibility is orthogonal (ADR-011)

Three vertical layers, and one horizontal axis that crosses all three.

| Layer | Crate | Asks |
| --- | --- | --- |
| L1 modern runtime | `psxrt` | what does the modern runtime do? |
| L2 original→modern | `psxrt-import` | how does original data become a modern representation? |
| L3 modern→original | `psxrt-compat` | how is original behavior preserved or overridden? |

**Compatibility is orthogonal to the layered architecture — it is not a layer.** A compatibility policy is a decision *about* a subsystem, consumed by whoever runs it; it is not a stratum of data flow. This is what the earlier "three clean layers" phrasing blurred, and it is the reason `psxrt-compat` must not sit under `psxrt`: a `psxrt` module that reads a policy from a crate below it has inverted the dependency and created a cycle.

The layer model retains a real job: it describes **asset data flow** (disc → decoded asset → frame). The subsystem taxonomy (display / effects / camera / math / world / audio) describes **state ownership**. Compatibility describes **policy**. Three different axes; a crate boundary tracks the third one only.

Consequences, both of them, not just the convenient one:
- `psxrt` ships a **modern-default** implementation of every subsystem. A game must be able to run with `psxrt-compat` entirely absent.
- A game may opt into a more compatible presentation or audio model. The opt-in is where compatibility *belongs*.
- **A `psxrt-compat` compatibility-policy module may still be a compatibility escape hatch.** The `psxrt` module next to it is not automatically "modern and correct". The CD rows carry this: `CD-03`'s integer camera program and `CD-05`'s rate-00 mapping are compatibility behavior *that has no clean modern counterpart*, and they are implemented in `psxrt`, not only as a knob in `psxrt-compat`.

## 3. Two-surface rule (ADR-011, the consequence most likely to be skipped)

Some subsystems have **two different surfaces**, and naming, module placement, and the crate boundary must reflect that they are different things:

| Subsystem | Surface 1 — decode / representation | Surface 2 — runtime / execution |
| --- | --- | --- |
| CLUT | `psxrt::gpu::clut` — CLUT entries + TPage + X/Y + GXY, a 5-bit `Texel` codec, BGR555↔RGBA8 for 16-bit CLUTs | `psxrt::gpu` (I5a) — VRAM, command queue, draw submit, blit |
| ADPCM | `psxrt::spu::xa_adpcm` — the pure XA codec, **no SPU** | `psxrt::spu` (I5a) — the SPU voice model, its own 5-bit filter |
| audio out | `psxrt::audio` — sample format, channel count, clock rate, the `AudioTransport` contract | the device implementations (I5b), in the **engine** crate |

The decode/representation surface is **owned by `psxrt`** because it is a representation of data, not a policy. The runtime/execution surface is placed where its owner is. A policy module may *select between* the two, and may live in `psxrt-compat` — the `psxrt` representation module still has to exist.

**VRAM: what actually exists (corrected 2026-11-15, against the reference).** An earlier draft of this section claimed the reference had "no VRAM write path". That was wrong and is withdrawn. `Gpu` has an explicit VRAM surface — `uploadData15` (two overloads), `downloadData15` (two overloads), `copyVramToVram`, `clear` — over a linear `vram15` store, plus `updateVramTexture()` which uploads the whole 1024×512 store to the GPU texture under `vramLock` **when `vramDirty`**, and is called per-object (`BattleEntity27c:772`), not at frame start. Command submission is a separate surface: `queueCommand(z, command)` + `resetCommandBuffer()`, bracketed by `startFrame()` / `endFrame()` (`Gpu.java:116–146`, `:202–360`, `:346–357`).

Two consequences that *are* load-bearing: (a) the module is `psxrt::gpu`, and it owns both the VRAM store and the command queue — they are one pipeline, which is §1.2's reason for not splitting them; (b) `updateVramTexture` is a **transfer**, not a policy, so it belongs to the runtime module and its execution strategy, never to a `psxrt-compat` policy record.

## 4. Module map

**24 modules listed — 11 in `psxrt`, 13 in `psxrt-import`.** The count is total, not per crate, and it is a count of *rows in this section*, not a cap: §1.3's rule is that a module may later become a crate on demonstrated need, and a module may be split (as `audio` is below) without the map's arithmetic mattering. Each entry: module → what it owns → layer → the milestone where it is built.

### `psxrt`

| Module | Owns | Layer | Milestone |
| --- | --- | --- | --- |
| `math::trig` | `rsin`/`rcos` + the π/180 factor. Public **inside the workspace** (the game layer's collision and effect math calls it), kept off the general-purpose utility surface by module path — `psxrt::math::trig::sine`, not a crate-root re-export. See §6.5 for the naming caveat. | L1 | I0 (done) → repath I5a |
| `math::angle` | the 4096/turn (PSX degree) unit — and that is all I0 delivers: `PSX_DEG_PER_REV`, `PSX_DEG_TO_RAD`, `TRIG_SCALE`, all three inside `fixedpoint.rs`. The 65536/turn unit, the wrapping rules, and the conversions between the two units are **I5a work**, not delivered by I0 | L1 | I0 (the factor only) → I5a |
| `rng` | the three generator families as deterministic streams (ADR-006) | L1 | I0 (done) |
| `gpu::clut` | see §3, surface 1 | L1 | **I1b** |
| `gpu::pixel` | the BGR555 family, `Texel`, RGBA8, the 4-bit/8-bit index read paths, the mask/alpha semantics | L1 | I0 (4-bit addressing done) → I1b (the colour paths) |
| `gpu` | the facade (**queue-then-submit**), `GpuCommand`, `CmdBuf`, `DrawCommand`, `DrawTopology`, `FilterMode`, the `Blitter` trait + the Native impl, the VRAM store + the transfer surface of §3, the CLUT/TPage decode | L1, with L3 policy | **I3**, completed I5a |
| `gte` | the `Rotation2d`/`Transform3d`/`TransformMode` vocabulary, the three precisions. **No `Mat3`/`Mat4`/`Vector3` aliases, no `math::` GTE surface** | L1, with L3 policy | I5a |
| `spu` | the voice model (ADSR, voice ADPCM, pitch, the reverb network) and, at I1b, the pure XA codec (`spu::xa_adpcm`) before the model exists around it | L1, with L3 policy | I1b (the codec) → I5a (the model) |
| `audio` | the sample contract: `SampleFormat`/`SampleRate`/`ChannelLayout`, the `AudioTransport` trait. Split at I3 into `audio::transport` (the trait, with **one** device-owning implementation and one compile-time backend per build) and `audio::device` (the device state: `open` / `active` / `buffered` / `started` / `paused`) | L1 | I3 |
| `clock` | the game clock. **Keeps only the `tick()` and `sync()` cadence entry points — not the 21 phase hooks, not `Sleep`-style host sleep** | L1 | I0 (done) → reworked I3 |
| `compat` (in-crate) | the `Feature` vocabulary, the `GameConfig` shape, the `CompatFlags` bitset | cross-cutting | I5a |

### `psxrt-import`

| Module | Owns | Layer | Milestone |
| --- | --- | --- | --- |
| `space` | the canonical file-space abstraction: the 63-bit address, the four kinds, the four namespaces, DRGN-name resolution, the `disc:`/`region:`/`object:` access path, the three error codes (planning/07 §3.1) | L2 | **I1a** |
| `container::iso` | the `files/` cache reader — region-major 63-bit keys, the 4-byte size header, four depth levels, path-length-bound keys | L2 | I1a |
| `container::mrg` | the MRG container; per-file *no*-compression rule | L2 | I1a |
| `container::bpe` | the BPE codec (both directions), the raw-LZSS variant, the three-mode flag | L2 | I1a |
| `cache` | the `files/` cache write path, `VERSION = 5` | L2 | I1a |
| `pipeline` | the orchestrator, the rayon pool, the 63-bit key builder, the streaming decoder | L2 | I1a |
| `render::tmd` | TMD/CTMD decode **and encode**, the `TmdFormat` vocabulary, `updateTmdPacketIlen` | L2 | I1b |
| `render::tim` | TIM parse + encode, the PXL sheet, the MCQ strip, `decode_16_to_rgba`, `decode_4_to_16`/`decode_8_to_16` | L2 | I1b |
| `effect::anim` | the ANIM family — plain, LMB (the only truly compressed member), CMB (a plain container of three plain sub-animations) | L2 | I1b |
| `fmv::mdec` | MDEC decode + the 10-sectors/frame demux, with a DRGN1 data cross-check | L2 | I1b |
| `audio::sssq` | SSSQ parse-only | L2 | I1b |
| `audio::sshd` | SSHD parse-only | L2 | I1b |
| `audio::transcode` | decode → resample → encode; `AudioFileInfo` | L2 | I1b |

### `psxrt-compat`

No module is carried forward unchanged. The eight policy modules and the collecting framework in `lib.rs` are dispositioned one by one in `planning/03` §5; the shape is decided by the three-level registry there, not by symmetry with `psxrt`'s module list.

## 5. Dependency rules (ADR-001, ADR-008)

**Allowed, permanently:**
```
psxrt-import ──► psxrt                      (I1b onward: TIM + transcode consume clut + xa_adpcm)
psxrt-compat ─► psxrt                      (a policy parameterizes a runtime model)
severedchains ────────────► psxrt, psxrt-import, psxrt-compat
severedchains-game ───────► severedchains + the three above
severedchains-sdk ────────► whatever it needs to host scripts, registries, i18n and config
scus94491 (bin) ──────────► everything
```

Any further crate added later joins this graph by *stating its edges*, not by inheriting a slot. A crate that cannot name who consumes it and why the boundary is a crate rather than a module is the case ADR-001 rejects.

**Rules.**

1. **`psxrt` never depends on `psxrt-compat`.** A `psxrt` subsystem ships a modern default. Compatibility behavior enters `psxrt` as *internal* behavior (the CD-03 camera program, the CD-05 rate-00 mapping), or is selected by a caller above.
2. **The default direction is `psxrt-import → psxrt`, and it is a default, not an absolute.** The earlier rule — import must never depend on psxrt — is retired. Both surfaces of CLUT and ADPCM live in `psxrt` for representation reasons; `psxrt-import` consumes them. That is the first legitimate use of the edge, and the reason the rule is now stated this way.
3. **Test-only exceptions are named in the manifest.** The planned one: `psxrt-compat`'s test build may read `psxrt-import`'s test utilities. Test-only, not a permanent crate dependency, and it must say so at the point of use.
4. **Module visibility does not substitute for dependency direction.** A `pub(crate)` type still crosses a boundary if a trait signature exposes it.
5. **A new public trait may not smuggle a hardware identity through its name or its API surface** (ADR-008, carried forward).
6. **Workspace members are enumerated, not globbed.** `members = [...]` is spelled out; `crates/*` is not used. This is a *hygiene* rule — an unexpected crate should not appear silently — and it is **not** a cap on the member count. What is prohibited is an empty placeholder directory and a module "to be created later"; a crate with a real responsibility and a stated dependency direction is added by the milestone that needs it. `unsafe_code = forbid` and edition 2024 apply workspace-wide, whatever the member count.
7. **No repository-level Rust dependency on Java sources** (ADR-009). Findings are transcribed into `PSX-Behavior Map` headers.
8. **`unsafe_code = forbid`** at the workspace level, applied to all three crates, with a per-file override only where a documented need exists. The reference's `SeekableByteChannel` + `map16` read pattern is replaced by `read_at` into aligned scratch buffers — **plain safe Rust, no `mmap`, no `unsafe`** (ADR-012).
9. **Edition 2024.** A single `Cargo.lock` at the workspace root.
10. **Default members** are the three libraries. `cargo test` without a `--workspace` flag must not build backends that are not being worked on, and CI must not become slower than the work that was actually done.

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
| `role::capability` | the `psxrt-compat` policy modules | where the module is a policy, not a mechanism |

A name must say **what it does**, not **which emulator component it imitates**. `Blitter` (what it does) over a hardware-unit imitation; `Native` (what this build runs) over `Hardware` (what this build pretends to be).

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
| hardware model | `psxrt` | the SPU model (the ADSR state machine, the voice ADPCM, pitch, the reverb network), the pure XA codec |
| runtime / service | `psxrt` | the transport contract and the player semantics |
| policy | `psxrt-compat` | predictor reset, envelope-vs-gap sweep, voice allocation — **plus the two SFX-side model layers and their replay fixtures** |
| device backend | `severedchains` (I5b) | `cpal`, the device-hosting implementation |

Three facts that constrain the design. (1) and (2) were re-checked against the reference on 2026-11-15; (3) is carried from `research/audio/01` + `research/psx/03` and is not re-verified here.

1. **The reference has no multi-backend audio API.** It has *four stacks*: a JavaFX-side player (`Audio` → `XaPlayer`/`GenericSource`), a PC-native player (`Sequencer` — the BGM), a PC-SFX player (`SoundSource28`), and the SPU side (`Spu`, `XaAdpcm`, `Voice`, `VoiceCounter`, `Sssq`). Hosted by one `AudioThread`. One device-owning implementation, one compile-time selectable backend per build. (`NativePlayer` appears in the M4 inventory as present-but-unused; treat that as research-carried, not re-verified.)
2. **`Spu` and `XaPlayer` share almost nothing** except that both are audio: there is no `AudioPlayer` type in the reference, and no shared player abstraction between the two. So the split into *device*, *transport*, and *voice model* is not a tidy three-way: `XaPlayer` reaches from device hosting into transport, and `VoiceCounter` reads a raw sample byte directly out of `Voice`, bypassing SPU register semantics.
3. **The two-clock rule.** Sequencer tick rates (8 ms, 16 ms, the SFX default) are a *reconstruction* — reference-specific, layer-4. The clock the game scripts were written against is the **SPU 60 tps clock**, and **SFX scheduling keys to it.** The reconstruction is therefore not optional; it must match the clock the scripts expect. **Sample-accurate scheduling is a `psxrt` runtime capability.** `psxrt-compat` decides *which clock a given reference layer uses*.

`XaPlayer` is **not** reproduced as a `psxrt` public type. Its semantics are preserved; the player state is owned where the state machine lives. **A player type in `psxrt` is explicitly out of scope** — the reference has no such type either (§7.3 fact 2), so this costs nothing in fidelity. Transport state lives in `audio::device` (the five states: `open` / `active` / `buffered` / `started` / `paused`) with the clock-domain, buffer, and volume semantics that state must carry.

### 7.4 FMV stack

| Layer | Crate | Content |
| --- | --- | --- |
| format decoder | `psxrt-import` (I1b) | MDEC frame decode, the 10-sectors/frame demux, `Ssd` |
| player state machine | `severedchains` + `severedchains-game` (I3, then I6) | `VideoPlayer`, `Fmv`, the `FmvRuntime`, the video/audio thread skeleton, the state machine, the player semantics |
| audio policy | `psxrt-compat` | the predictor, the clock domains |
| device backend | `severedchains` (I5b) | `cpal` |

The 15 fps / sector-quad frame clock lives with the player, not in `clock`. FMV audio is **a third clock domain**, not a mode of the BGM clock. The reference comment "No attempt is made to stay in sync with the video stream" is a fact to write down. **The FMV plumbing stays in the first game vertical slice (I6)** — that is why I5 precedes I6.

The FMV demux loop does four things, and the split is not one-to-one: it reads **30 bytes** of header per frame (not 24 — four separate audio portions plus four channel values), it decodes the audio (including the silent-A/D special case), it copies raw MDEC bitstream into the command buffer, and it issues the MDEC decode. The two audio halves and the two video halves use **one shared 1760-byte buffer**, the only two `CmdBuf` users, which is legal only because playback is strictly sequential (FMV, then battle, never overlapping). **`CmdBuf` is therefore owned linear scratch, not a reusable pool, and the 0x80000-word requirement is a documented allocation fact, not an arbitrary choice.**

## 8. What this document deliberately does not decide

The TD items (`planning/02`, TD-25 … TD-32 are the new/changed ones), the CD dispositions (`planning/03`), the milestone gates (`planning/04`), and the test strategy (`planning/05`).
