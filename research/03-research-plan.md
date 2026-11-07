# Research Plan

Milestone-based plan for the Severed Chains research effort. Foundational systems come before higher-level gameplay systems. Each milestone lists scope, primary reference sources, research questions, expected documentation, verification criteria, and dependencies.

**Ordering rationale (M1 → M10):**
- M1 (platform/lifecycle) defines the execution model everything else hangs off (threads, frame phases, timing).
- M2 (asset pipeline/formats) is a hard prerequisite for all game systems, because every game object is data-driven from the unpacked file tree.
- M3 (scripting) and M4 (PSX hardware models) are the two deep cross-cutting foundations; they can run in parallel after M1–M2.
- M5–M8 (rendering, audio, saves, modding) are the remaining horizontal subsystems and are largely parallelizable.
- M9 (game systems) is strictly last because it is the largest body and depends on everything else for meaningful interpretation.
- M10 (cross-system synthesis) closes the loop and feeds implementation planning.

Milestones are research activities, **not** implementation work. No Rust code, crates, or API design is produced at any milestone; outputs are evidence-grounded documents only.

---

## Phase Alignment (R1)

The user-defined phase **R1** ("Foundational Architecture and Execution Model") does not map 1:1 to a single plan milestone. Reconciliation, applied before starting R1:

| R1 deliverable | Plan milestone(s) | R1 document |
|---|---|---|
| Application lifecycle and initialization | M1 | `research/platform/01-application-lifecycle.md` |
| Execution model and update ordering (loop, phases, timing, threads, events, input order, audio timing) | M1 | `research/platform/02-execution-model.md` |
| (supporting) Engine state machine — part of R1 "state transitions" | M1 | `research/platform/04-engine-states.md` |
| (supporting) Input and platform model — part of R1 "input processing order" | M1 | `research/platform/05-input-and-platform.md` |
| Runtime subsystem responsibilities | M10 (foundational pass; M10 refines) | `research/architecture/01-runtime-subsystems.md` |
| Subsystem interactions and dependencies | M10 (foundational pass; M10 refines) | `research/architecture/02-subsystem-interactions.md` |
| State and resource ownership | M10 (foundational pass; M10 refines) | `research/architecture/03-state-and-ownership.md` |
| PlayStation compatibility boundaries | M4 (foundational pass; M4's per-subsystem deep dives refine) | `research/psx/01-compatibility-boundaries.md` |
| Architectural uncertainties and unresolved questions | register | `research/04-unresolved-questions.md` (updated) |

Consequences:
- R1 absorbs **M1 in full**; M1 is considered complete once the four `research/platform/` documents above exist and meet M1's verification criteria. **M1 is now complete** (R1, 2026-11-02); the consolidated document layout is recorded in the M1 section of this plan.
- R1 delivers **foundational passes** over two M10 topics (subsystem responsibility/dependency/ownership) and one M4 topic (compatibility boundaries). These R1 documents are boundary-level: they establish *which* behaviors belong to which layer and the ownership map. The M4 milestone (per-subsystem GTE/SPU/XA/GPU deep dives with byte/constant-level classification) and M10 (full synthesis, including what R1's passes do not cover) remain subsequent, separate work — R1 does **not** consume them.
- M2–M9 are unaffected and unchanged.
- The M1 exit gate (index update, verification criteria, stop-for-approval) applies to R1's platform documents; the architecture and psx foundational documents follow the same evidence discipline and are tracked in the index as partial completions of their topics.

---

## M1 — Platform, Lifecycle, and Execution Model

**Status: COMPLETE (delivered as Phase R1, 2026-11-02).** Delivered documents: `research/platform/01-application-lifecycle.md`, `research/platform/02-execution-model.md` (absorbed the plan's "game loop and phases" + "timing and threads" + "input and platform" topics), `research/platform/04-engine-states.md`, `research/platform/05-input-and-platform.md`. Register updates closed Q-01, Q-02, Q-04, Q-05, Q-39; Q-03/Q-06 remain open as noted. **Amended by the independent xhigh audit (2026-11-02, `research/audits/r0-r1-xhigh-audit.md`):** F-01 added the full per-context `vsyncMode` write-site census to platform/02 §5 (exploration runs 60 Hz, not 30 Hz); F-02 corrected Q-41's closure (no SPU data race exists); F-03/F-04 corrected teardown timing and active-mod attribution in the architecture documents.

**Scope.** Application lifecycle from process start to shutdown; boot ordering; the per-frame game loop and its phases; thread model and ownership; timing model (base rate, frame skip, game-speed multiplier, timestamp semantics); input architecture (actions, bindings, devices, activation modes); window/display management and resize behavior.

**Primary reference sources.**
- Severed-Chains: `legend/game/Main.java`, `legend/core/GameEngine.java`, `legend/core/renderer/RenderEngine.java` (`run()`, `renderFrame()`, `renderBatch()`), `legend/core/platform/*` (SdlPlatformManager, SdlWindow, SdlInput, input/*), `Scus94491BpeSegment.gameLoop()/gameInit()/preload()`, `legend/game/EngineStates.java`, `legend/core/audio/AudioThread.java` (thread interaction), `legend/core/Async.java`, `DebugHelper`, `Config`.
- Secondary: Mod-Loader (mod boot step), `gfx/shaders` + launch scripts (context only).

**Research questions.**
1. What is the exact startup sequence, including async steps (unpack thread, updater check, audio device init) and their synchronization points?
2. What drives the main loop (SDL event pump vs render callback vs dedicated tick), and what is the precise per-frame phase order and what each phase may observe/mutate?
3. How do the three threads (hardware/main, OpenAL audio, unpack + timer threads) coordinate? What state is shared, and by what locking?
4. What is the timing model: base 30 Hz (`vsyncMode=2`), frame skip, speed multiplier, `timestamp_a0` semantics, and how do subsystems (SPU 60 tps, sequencer, scripts `setFramesPerTick`) align to it?
5. What are the engine states (registry `engineStateTypes`), how are overlays queued/loaded, and what are the transition constraints (savable states, post-battle/post-credits states)?
6. What is the input abstraction (actions vs physical keys/buttons, axis activation modes, deadzones, per-device state) and what is PSX-legacy vs modern?
7. What happens on window close/resize/shutdown, and how does crash recovery interact with the lifecycle?

**Expected documentation.**
- `research/platform/01-application-lifecycle.md` (as planned)
- `research/platform/02-execution-model.md` (consolidated: the plan's separate "game loop and phases" and "timing and threads" documents were merged into one execution-model document during R1 — the loop, phases, threads, timing, input order, and audio timing are tightly interlocked and a single document avoids duplication)
- `research/platform/04-engine-states.md` (as planned)
- `research/platform/05-input-and-platform.md` (as planned)

**Verification criteria.**
- A single annotated lifecycle diagram (process start → title → gameplay → shutdown) where every arrow is backed by a cited call site.
- A phase-order table for the frame loop with per-phase invariants (what can read/write what), consistent with at least three independent call sites each.
- Thread/state-ownership table; every shared mutable service (GPU, SPU, RENDERER, SCRIPTS, CONFIG, SAVES) classified: single-thread owner, lock-protected, or thread-confined.
- Timing model stated with the concrete constants (30 Hz base, 60 tps SPU, `nanosPerTick` derivation) and their source lines.
- No phase order or threading claim without a `Verified` citation; ambiguities recorded as `Unresolved` with the conflicting evidence.

**Dependencies.** None (first milestone). Feeds: everything.

---

## M2 — Asset Pipeline and Binary Formats

**Status: COMPLETE (2026-11-02).** Delivered documents: `research/assets/01-iso-and-file-tree.md`, `02-mrg-archive-format.md`, `03-unpacker-pipeline.md`, `04-original-executable-overlay.md`, `05-tmd-model-format.md`, `06-tim-texture-format.md`, `07-animation-format.md`, `08-text-and-i18n-formats.md`, `09-fmv-mdec.md`, plus supplementary `10-music-formats.md` (SSSQ/SSHD/XA/music pipeline). Register updates: Q-08, Q-09, Q-10, Q-12, Q-14 closed; Q-07, Q-11, Q-13 partially closed; Q-23, Q-24, Q-43 closed or advanced; 15 new open items Q-44–Q-58 (all byte-level open items are asset-validated-only or M4 external-cross-check items — see register). All M2 research questions answered except those requiring real disc/asset bytes (explicitly deferred per the plan's constraint and P-02: no disc images were available during M2, so no claim is marked `asset-validated`).

**Scope.** PSX CD/ISO access; the DRGN/MRG file tree (all four discs, root on disc 4, `SECT/DRGN*.BIN` layout, virtual files, sector alignment); the unpacker pipeline (queue, virtual-thread execution, cache versioning); every leaf transformation (decompressor, MRG, DEFF, XA→Opus, MIDI→SSSQ, engine overlay, SMAP assets, LOD engine, and the hardcoded bug-fix patches); the original executable overlay `SCUS_944.91` and embedded assets (shadow, font); runtime file access (`Loader`, `FileData`); and the binary formats themselves: MRG, DEFF, decompression scheme, TIM, TMD/CTMD, ANIM, MCQ, SSSQ, FMV/MDEC, DABAS.BIN.

**Primary reference sources.**
- Severed-Chains: `legend/game/unpacker/*` (Unpacker 1531 lines, FileData 748, IsoReader, MrgArchive, Loader, CtmdTransformer, SubmapPxlTransformer, SaveCardTextureExtractor, Transformations, Finderator), `legend/game/unpacker/midi/*`, `legend/game/tmd/*`, `legend/game/tim/*`, `legend/game/fmv/*`, `legend/core/font/*`, `legend/game/sound/*` (SSSQ loaders), `Scus94491BpeSegment.loadSystemFont()/loadShadow()/initFmvs()`, `patches/` (replacements, libs, meta, lookup tables).
- Secondary: Dig-Dabas `DabasVars.loadRom` (DABAS.BIN layout); Script-Recompiler (script binaries are also unpacked files).

**Research questions.**
1. What is the exact MRG entry table layout (offsets, virtual entries, duplicate detection, sector alignment) and how does the root tree on disc 4 index discs 1–3?
2. What is the DRGN script-file addressing scheme (`DRGNxx/nnnn/mm`) and how do entrypoints map to files (e.g., `drgnBinIndex`, per-disc script indices)?
3. What are the decompressor and DEFF formats (byte-level), and which files use them?
4. What exactly is extracted from `SCUS_944.91` (offsets/sizes), and does the decompiled `Scus94491BpeSegment*` code correspond to that overlay?
5. What are the TIM/TMD/CTMD/ANIM/MCQ/SSSQ/FMV binary layouts (byte-level), including CLUT handling, container structures, and checksums where present?
6. What is the full list of hardcoded fix/patch transformations applied at unpack time (the "original bugs the reference fixes" catalog), with before/after semantics?
7. How does the cache versioning work (VERSION=5), and what invalidates it?
8. Which formats require bit-exact decoding for compatibility, and which are only consumed by the reference's own decoders (format-only compatibility)?

**Expected documentation.**
- `research/assets/01-iso-and-file-tree.md`
- `research/assets/02-mrg-archive-format.md`
- `research/assets/03-unpacker-pipeline.md` (including the fix-patch catalog)
- `research/assets/04-original-executable-overlay.md`
- `research/assets/05-tmd-model-format.md`
- `research/assets/06-tim-texture-format.md`
- `research/assets/07-animation-format.md`
- `research/assets/08-text-and-i18n-formats.md`
- `research/assets/09-fmv-mdec.md`
- `research/assets/10-music-formats.md` (supplementary: SSSQ/SSHD/XA music pipeline — added during M2 execution; the plan's nine core documents cover SSSQ within T-SSSQ's M6 scope, but the format documentation was completed here where its decoders live)

**Verification criteria.**
- Each documented format has a byte-level layout table (offset, size, type, endianness, meaning) with source citations for every field.
- The file-tree document includes a concrete example path from ISO sector to `files/` entry (e.g., `SECT/DRGN1.BIN/20` → `files/DRGN1/20`).
- The fix-patch catalog is exhaustive against the source's transformer list, each entry tagged original-bug-fix vs feature-enabler vs modding-enabler where determinable.
- **Constraint:** no game ISO/BIN assets are present in the checked-out repositories (`isos/` and `files/` are empty/gitignored), so byte-level claims rest on the reference decoders' source. Any claim that could only be confirmed against real ROM data is marked `Inferred` or `Unresolved` and listed in the register; obtaining sample assets (user-provided) is a research dependency, not an assumption.

**Dependencies.** M1 (load timing/phasing context). Feeds: M3 (script binaries live in the tree), M5–M9 (all data).

---

## M3 — Fate Scripting: Language, Format, Toolchain, Runtime

**Status: COMPLETE (2026-11-03; independent xhigh audit completed 2026-11-04 — all findings remediated in place, `research/audits/m3-xhigh-audit.md`, gate PASS).** Delivered documents: `research/scripting/01-fate-language.md`, `02-fate-binary-format.md`, `03-toolchain.md`, `04-meta-system.md`, `05-runtime-interpreter.md`, `06-native-script-functions.md`. All eight research questions answered; C-01 resolved with the discrepancies preserved (scripting/02 §6: toolchain-only `NOOP(100)`, runtime-unimplemented 5/6/7/11/13/14/15, div/mod-by-0 divergence, error-mode split; 67/68 entry counts and the op-89 gap audit-verified); five-plus byte-level encoding examples delivered (scripting/02 §9: one observed reference constant + seven derived; 9.2–9.5 and the corrected 9.6–9.8 byte layouts experimentally verified by the audit); round-trip conditions formalized (scripting/02 §8, 03 §10 — audit-extended: 0x24–0x27 and compare-header 8/9 one-way hazards, experimentally confirmed); the patch state machine documented (scripting/03 §8); meta authority resolved — local `patches/meta` is the in-game truth (Q-19); native catalog verified 592/592 against metadata (scripting/06; 578 unique implementations, 4 alias groups, 44 methods without params rows — audit-clarified; per-slot behavioral contracts deferred to M9/M4). Register updates: C-01, Q-15, Q-16, Q-18, Q-19, Q-42 closed; Q-17 advanced; new open items Q-59–Q-64 (all asset-validated-pending or external-PSX evidence; remote scmeta content uncheckable offline — Q-62; Q-18/Q-42/Q-60/Q-61 wording audit-corrected).

**Scope.** The Fate *language* (text syntax, preprocessor/includes, semantics), the *binary format* (encoding, entries, opcodes with parameter layouts, strings, entrypoints, tables), the *toolchain* (decompiler disambiguation and canonical output, compiler/codegen, round-trip stability, patcher diff semantics, meta system), and the *runtime interpreter* (ScriptState lifecycle, storage/registers/flags, call stack, fork/consume, native function calls, frame pacing, script object binding).

**Primary reference sources.**
- Script-Recompiler: `Disassembler` (846), `Tokenizer` (715), `Translator` (319), `Assembler` (73), `Patcher` (128), `OpType` (160), `tokens/*`, `compiler/*` + `FateLexer.g4`/`FateParser.g4`, `resolution/*`, `meta/*`, `Shell`.
- Severed-Chains: `legend/game/scripting/*` (ScriptState 1522, ScriptManager 470, OpType 93, Param hierarchy, RunningScript, FlowControl), `Scus94491BpeSegment` native `script*` functions (the native-call surface), `legend/game/unpacker/scripts/ScriptPatcher.java` + `patches/scripts.csv` + sample `.diff` files, `patches/meta/*.csv`, `patches/libs/*.fate|txt`.

**Research questions.**
1. What is the complete opcode set with exact binary encoding (header, param count/widths, immediate forms, string operands), reconciling the two `OpType` tables (see conflict C-01)?
2. What are the script binary's structural elements: entrypoints (count/location), string pool, pointer tables, table entries, and how are jump targets (relative/absolute) encoded?
3. What is the decompiler's canonical output (labeling, comment policy, op specialization such as `wait_cmp`→`wait_cmp0`, `mov`→`mov0`, `jmp_cmp`→`jmp_cmp0`), and what disambiguation heuristics does it use (branch vs table, while-recognition, register tracking)?
4. What is the round-trip guarantee: does compile(decompile(x)) == x hold, under what conditions, and what does the patcher depend on?
5. How does the meta system work end-to-end (remote scmeta versions, local CSV mirror, parameter types, enum lists, descriptions), and what is authoritative?
6. What is the runtime execution model: how are scripts spawned (fork/fork_reenter/consume), ticked (frames-per-tick, yield/wait/rewind semantics), how do native calls exchange parameters (the `Param` hierarchy incl. GameVar/Stor/RegistryId/Temp/Inline params), and how do scripts bind to game objects (`ScriptedObject`)?
7. What is the complete native function surface (all `script*` functions with parameters/return/FlowControl semantics), as enumerated from `patches/meta/params.csv` + source?
8. What state do scripts read/write directly in game memory (stor/gamevar/registry interplay), and what invariants (STORAGE_COUNT=63, REGISTRY_ID_COUNT=100, flag bits 16–27) exist?

**Expected documentation.**
- `research/scripting/01-fate-language.md`
- `research/scripting/02-fate-binary-format.md`
- `research/scripting/03-toolchain.md`
- `research/scripting/04-meta-system.md`
- `research/scripting/05-runtime-interpreter.md`
- `research/scripting/06-native-script-functions.md` (catalog)

**Verification criteria.**
- A single reconciled opcode table (opcode, name, header param, param list with in/out/both direction and types, binary width) with per-row citations from both repos; the C-01 conflict resolved or documented as a real discrepancy.
- Byte-level encoding examples: at least 5 real instructions (from `patches/` diffs or sample scripts) shown as raw bytes ↔ disassembly ↔ text, cross-checked against the disassembler's own logic.
- State-machine diagrams for: interpreter tick (yield/wait/rewind/consume), script lifecycle (spawn → run → fork/parent/child → deallocate), and patch application (CRC cache, backup, fail handling).
- Native function catalog: every `script*` function in `patches/meta/params.csv` cross-checked against its source implementation; any with missing/contradictory meta flagged.
- Round-trip behavior stated with the precise conditions under which the decompiler's output is stable; known non-determinisms listed.

**Dependencies.** M2 (script binaries are unpacked-tree artifacts). Feeds: M9 (gameplay is script-driven), M10.

---

## M4 — PSX Hardware Layer: GPU, GTE, SPU, XA, and Compatibility Classification

**Status: COMPLETE (2026-11-04; approved starting point `ce26ddb`).** All five deliverables delivered sequentially, each checkpoint-verified and committed: M4-01 `research/psx/01-gpu-vram-ordering.md` (plan constants verified exactly — VRAM 1024×512, ordering table 0x4000 / zMax 0x3ffe / 14 bits / zShift 0; same-z LIFO-vs-FIFO traversal divergence found — Q-66), M4-02 `research/psx/02-gte.md` (per-operation float-vs-fixed-point divergence table; the reference's deliberate integer-precision submap camera `GsSetSmapRefView2L` documented as in-repo evidence that float math diverges observably — Q-20 advanced, Q-67 opened), M4-03 `research/psx/03-spu.md` (60 tps LoD-verified via the ROM's own `ticksPerSecond_42`; 24-voice edge-key model, full ADSR, reverb work area = 0x4000 floats, single-monitor synchronization per R1 audit F-02 — Q-22 advanced, Q-68 opened), M4-04 `research/psx/04-xa-adpcm.md` (24-byte header verified; coding-info byte at offset 19; `sixStep = 6` 6→7 zigzag FIR documented as dead code; four-decoder census — Q-23 advanced, Q-69 opened), M4-05 `research/psx/05-compatibility-classification.md` (43-row master table — 1 hardware-accurate, 33 behaviorally-equivalent, 3 format-only, 6 unresolved; fixed-point helpers documented in full: `psxDegToRad` 4096°/turn, `rsin`/`rcos` float×0x1000, `simpleRand` LCG, `rand`/`srand` 15-bit LCG; the `srand(System.nanoTime())` submap seed placeholder inside a LAB-labeled decompiled path documented — Q-70 opened). Verification criteria met: every plan constant carries source citations (external cross-checks not possible offline — all layer-1 attributions marked `Inferred`/`Unresolved` and registered in Q-21..Q-23, Q-65..Q-70); the master table covers every documented GPU/GTE/SPU/XA behavior with class + rationale + confidence; one worked, divergence-annotated example per subsystem (M4-01 §7, M4-02 §4, M4-03 §4 and §6, M4-04 §5). No `asset-validated` claims (no disc images available). Execution discipline (checkpoint per deliverable, file-ending audit, register updates, committed before proceeding) held throughout.

**Scope.** The reference's PSX-facing models and what each preserves of the original hardware: GPU (VRAM dimensions, bpp modes and their width/index parameters, draw area and screen offsets, clear color, ordering table and Z-sort semantics, double-buffered draw targets, data upload paths), GTE (register file, per-operation semantics, float-vs-fixed differences, where rounding occurs), SPU (voice model, ADSR, reverb, key on/off, channel modes, 60 tps tick, volume semantics), XA ADPCM (header, coefficient/step/zigzag tables, resampling), and the cross-cutting **compatibility classification**: for each behavior — hardware-accurate, behaviorally-equivalent, or format-only is required for a faithful reimplementation.

**Primary reference sources.**
- Severed-Chains: `legend/core/gpu/*` (Gpu 709, GpuCommand*, VramTexture*, Bpp, Rect4i), `legend/core/gte/*` (Gte 399, MV, Transforms), `legend/core/spu/*` (Spu 491, Voice 218, XaAdpcm 185, ADSR, Reverb 73, Counter, Phase, Volume), `legend/game/Graphics.java` (952; render mode, vsync, ordering-table state), `Scus94491BpeSegment.gameInit()` (ordering table constants), usage sites throughout `legend/game` (how the game actually calls GPU/GTE/SPU).
- Secondary: Dig-Dabas `core/sound/Spu.java` (the 8-bit Dabas SPU — *not* PSX; compare only), public PSX hardware documentation (for cross-checking constants such as VRAM size, GTE register semantics, XA coefficient tables) — `Inferred`-cross-reference material, cited separately from repo evidence.

**Research questions.**
1. What is the exact GPU state the game relies on (draw area, offsets, z-max, ordering table size, bpp per use-site), and how does the reference implement the ordering table's sort and the 15/24-bpp pipeline?
2. Which GTE operations does the game use, and where does the float model diverge from fixed-point (rounding, saturation, truncation)? Do any game behaviors depend on specific fixed-point artifacts (e.g., specific rounding of projections/OTZ)?
3. Is the SPU model faithful in ways that matter audibly/behaviorally (timing of key on/off, reverb path, voice stealing, 60 tps vs original SPU DMA), and what are the observable constraints?
4. Are the XA ADPCM tables and header layout identical to documented PSX XA (cross-check), and how does resampling work (`sixStep`, ring buffer)?
5. **Classification:** for every documented behavior, assign a compatibility class (hardware-accurate / behaviorally-equivalent / format-only) with the evidence for the choice. This is analysis, not decision — the Rust project may later choose a different target, but the research must make the tradeoffs explicit.
6. What fixed-point math helpers (`rsin`/`rcos`, `simpleRand`, `rand`/`srand`) are behavior-significant (script determinism, animation), and what are their exact tables/seed semantics?

**Expected documentation.**
- `research/psx/01-gpu-vram-ordering.md`
- `research/psx/02-gte.md`
- `research/psx/03-spu.md`
- `research/psx/04-xa-adpcm.md`
- `research/psx/05-compatibility-classification.md` (master table)

**Verification criteria.**
- Constant tables (VRAM 1024×512, ordering table 0x4000/zMax 0x3ffe/bits 14, SPU 24 voices/60 tps/reverb area 0x4000, XA header 24 bytes, sixStep=6, bpp parameters) each with source citations and, where available, cross-checks against public PSX documentation (marked as external evidence).
- The compatibility-classification master table covers every GPU/GTE/SPU/XA behavior documented in M4, with class + rationale + confidence; disagreements or unknowns registered as `Unresolved`.
- At least one worked example per subsystem: a game call site → model behavior → original-hardware counterpart, annotated with where the two could diverge.

**Dependencies.** M1 (frame context). Can run parallel with M3. Feeds: M5, M6, T-COMPAT conclusions.

---

## M5 — Rendering and Presentation

**Status: COMPLETE (2026-11-05; approved starting point `244ac07`).** Execution discipline: the four deliverables are completed sequentially, each with a durable checkpoint (document written, principal claims verified, affected register items updated, file-ending audit run, committed) before the next begins; a bounded cross-document reconciliation pass follows the four checkpoints. Builds on M4 (`research/psx/01-gpu-vram-ordering.md` G1–G15 compatibility rows, `02-gte.md` Q-67, `04-xa-adpcm.md` FMV findings) and M2 (TMD/animation/TIM/FMV formats).

**Checkpoint log.**
- **M5-01 — `research/rendering/01-render-engine.md` — delivered and committed (2026-11-05).** Plan research question 1 answered (frame render structure: pools/batches/order/sort rules, cited render-order diagram §13; baseline resolution §3/§12); question 6 partially (G1–G15 carry-over table §10 — G6 same-z LIFO carried, Q-66 not re-adjudicated). Register: Q-36 advanced, Q-37 advanced (F-13 resolved, F-18 documented, scope narrowed to the per-object census), Q-71 opened (2D-pool LIFO vs 3D-pool FIFO equal-depth tie-break asymmetry).
- **M5-02 — `research/rendering/02-tmd-pipeline.md` — delivered and committed (2026-11-05).** Plan research question 2 answered (CPU GTE vs GPU shader transform split, §2/§4; traced load→pixel example §5); question 6 advanced (TMD-specific G-row carry-over §6). Register: Q-37 advanced (per-object mode-0 census delivered at mechanism level; ctmdFlags bit 0x2 latent), Q-67 advanced (TMD z formula + zMin, AVSZ→usePs1Depth, NCLIP winding, geometry-stage zfar/2 threshold added to the M10 list), G11 advanced (rate→mode table source-verified for both paths; layer-1 attribution still open). No new register items (latent double-blend, 1° init quirk, duplicate 0x800213c4 interpolation bodies, unimplemented TMD-id upper-bits feature documented in place).
- **M5-03 — `research/rendering/03-ui-and-text.md` — delivered and committed (2026-11-05).** Plan research question 3 answered (UI/text rendering architecture: the two 2D presentation surfaces and their mode visibility, the −512 UI VRAM region and retail UI asset pipeline (DrgnFile 6665 TIMs / 6666 UiFile glyph strips / font.tim), `Renderable58`/`Menus.renderUi` glyph-strip animation and geometry, the menu screen stack and ROM message-box state machine, the text system — JSON+PNG font stack (layer 4), `renderText` 4-pass shadow, textbox size contract (chars×9/2 × lines×6) and two-pass glyph drawing with per-character scissor, full-screen fade/lightning effects, the ×4 text-depth z-lattice); question 6 advanced (G4 384→368 menu clamp, Q-66 boxagon same-z datum). Register: Q-37 advanced (two-surface 2D model; font TIM retained but unsampled), Q-66 advanced (boxagon non-overlapping same-z tiles cannot discriminate LIFO/FIFO), Q-71 advanced (equal-depth 2D census delivered: 139 ortho call sites, z-lattice table, collision classes — border-vs-text, same-z renderables, screen stacking), Q-72 opened (original UI VRAM partition / +512 region). M2-08 §1 corrected in place (renderText uses the JSON font, not the font TIM).

- **M5-04 — `research/rendering/04-postprocessing.md` — delivered and committed (2026-11-05).** Plan research question 6 closed for the modern path (widescreen: three coordinate spaces — native model / render buffer (multiple-of-240 height) / window; per-mode ortho projections with `widescreenOrthoOffsetX` auto-vs-manual application; EXPANDED/FORCED_4_3/STRETCHED semantics; the 368/384 divergence — GPU model clamps 384→368, modern path does not: G4 advanced); question 7 answered (post-processing: the `screen.fsh` CRT shader family as a pure layer-4 final transform — full config→uniform map with defaults, the turn-order legibility cheat, pixelate-resolution modes, `time` from `vsyncCount`; FMV presentation: MDEC decode-to-quad at 15 fps / 2× CD cadence, 320-pinned letterbox at z = 100 via the no-offset `Matrix4f` overload, mode-state save/override/restore with deferred `stop()`, skip-to-confirm flow, rumble, the 18-FMV ROM table, the five call sites, and the layer-4 `VideoPlayer` FFmpeg boot intro at `GameEngine.java:467`). Register: Q-37 advanced (widescreen + FMV presentation documented; remaining scope = original-path attribution + visual cross-check incl. 384-vs-368 menu delta), Q-65 advanced (G4 modern-side disposition), Q-48 advanced (premise corrected — M4-04 showed the FMV path performs no resampling; the 37800 Hz registration matches; residual original-gain question subsumed by Q-73), Q-73 opened (original FMV presentation: VDP overlay vs letterbox, 1× vs 2× CD cadence, original gain staging, per-IKI frame sizes). M4-05 table: G4 advanced, G13 addressed, M5 disposition note marked complete.
- **M5 complete — bounded reconciliation + governance (2026-11-05).** Bounded cross-document reconciliation pass: M5-01 §10 G1–G15 dispositions cross-checked against M5-04 (G4/G13 closed for the modern path); M5-04 §3 post path verified consistent with M5-01 §2.2 phase 8 (same region, complementary anchors); M5-04 §4 reconciled with M4-04 §5 (FMV XA: native-rate decode, `>>=1`, `× volume`, 37800 Hz registration, drop-on-full) and M2-09 (18-FMV table, 15 fps); in-place corrections: M2-08 (M5-03), M2-09 `diskFmvs_80052d7c` anchor (:87-90 → :63-69). No `asset-validated` claims; no engine/Rust code; all four reference repositories verified unchanged at every checkpoint. **M5 gate: PASS.**

**Scope.** RenderEngine architecture (render APIs: OpenGL/OpenGL-ES/Noop; queued-model pools and sorting; batches; shader types: simple/copy/standard/TMD/battle-TMD/screen; cameras; framebuffers; scissor stack); the TMD model pipeline (dedicated vertex processing vs Assimp-converted meshes, model parts, animations, lighting); UI and text (SItem toolkit, message boxes, menu stack, retail TIM font vs modern TTF fonts, text geometry); widescreen/resolution modes and post-processing (CRT/scanlines/grille/pixelate/roll shaders); FMV presentation (MDEC playback + modern mp4 path, rumble).

**Primary reference sources.**
- Severed-Chains: `legend/core/renderer/*` (RenderEngine 1484, RenderApi, GlApi/GlesApi/NoopApi, Obj, QueuedModel*, Shader*, Texture*, Camera*, FrameBuffer*, ScissorStack, builders), `legend/game/Graphics.java`, `legend/game/SItem.java` (menu toolkit), `legend/game/Text.java`, `legend/game/textures/*` (TextureAtlas, TexturePacker, Image, RegisterAtlasTexturesEvent), `legend/core/font/*`, `legend/game/tmd/*` + `legend/core/gte/MV.java`, `legend/game/fmv/*`, `gfx/shaders/*` (the actual GLSL: standard/tmd/battle_tmd/copy/screen/post, `.vsh/.gsh/.fsh`), `legend/game/title/*`.

**Research questions.**
1. What is the frame's render structure: which pools/batches exist, in what order are they drawn, and what are the sorting rules (perspective vs ortho, opaque vs translucent, z-ordering interplay with the PSX ordering table)?
2. How are TMD models processed: which transforms happen on CPU (GTE) vs GPU (TMD shaders), and what do the model/animation containers feed?
3. What is the text system: glyph data (retail TIM page layout vs TTF), textbox geometry, text Z, color/shadow, and how i18n interacts?
4. What do the post-processing chain and widescreen modes change geometrically (projection offsets, ortho centering), and what is the baseline 320×240 behavior?
5. How is FMV presented (YCbCr conversion, sector decoding, timing with audio, rumble data), and where does the modern mp4 intro replace the original FMV?
6. Which rendering behaviors are PSX-compatibility-relevant (vs pure modern enhancement, e.g., post-fx)?

**Expected documentation.**
- `research/rendering/01-render-engine.md`
- `research/rendering/02-tmd-pipeline.md`
- `research/rendering/03-ui-and-text.md`
- `research/rendering/04-postprocessing.md`

**Verification criteria.**
- Frame render-order diagram with every batch/pool and its sort predicate, cited.
- TMD pipeline: a traced example (file load → container → model parts → animation → queued model → shader inputs) with citations.
- Baseline 320×240 projection/ortho parameters stated; widescreen/post-fx documented as reference-implementation enhancements (layer tag: reference behavior), not original behavior.
- Every shader family's purpose and inputs/outputs summarized (GLSL read, not copied wholesale).

**Dependencies.** M1, M4, M2 (formats). Feeds: M9, M10.

---

## M6 — Audio and Music

**Status: COMPLETE (2026-11-06; started 2026-11-06; approved starting point `a46c36a7f`).** All three deliverables delivered and committed (M6-01 `4d09d86`, M6-02 `fbb9fbb`, M6-03 `bbb6645`), followed by the bounded cross-system reconciliation (M2-10 §3/§4, M4-04 §1/§6, M4-05 X4/X7 + M6 carry-over note, M6-01 0x51 wording — all corrected in place). Execution discipline (as planned): each deliverable was completed sequentially with a durable checkpoint (document written, principal claims verified, affected register items updated, file-ending audit run, committed) before the next began. Builds on M1 (frame/thread timing), M2 (music formats, XA transcode), M4 (SPU/XA contracts, Q-68/Q-69), and M5 (FMV presentation).

**Checkpoint log.**
- **M6-01 — `research/audio/01-audio-architecture.md` — delivered and committed (2026-11-06).** Plan research questions 1 (threading model: what ticks what, at what rates, what synchronization exists — answered: the 120 Hz audio-thread min-pace with OpenAL-drain-gated source ticks, the 60 Hz SFX thread (game sequencer + SPU), the lock table, and the F-02 sync model confirmed complete — every SPU control method is `synchronized(Spu.class)`), 4 (tick-rate table with the sample-accurate music vs 60 Hz-quantized SFX split; the SSSQ tempo = BPM unit corroborated by the MIDI tooling's `60_000_000/tempo` conversion; the SFX conversion-vs-log discrepancy registered), 5 (volume/reverb semantics: dual-domain main volume, 7-bit sequence volume, per-voice chains, pan, dual reverb networks; music/SFX interaction: per-state `unloadSoundFile`/`sssqResetStuff`, menu backup/restore, no ducking/crossfade machinery beyond fades — documented), and 6 (original vs reference layering: A1–A14 table — OpenAL hosting, Opus replacement, the float music engine re-implementation, quality configs, hot-plug, the `No longer implemented`/`assert false` handler gaps, the preserved retail 1-tick fade-out bug, the dead `sssqTempoScale`, the pan-table anomaly, the XaPlayer SFX-knob routing) answered. Register: **Q-68(b) resolved for the reference** (dynamic voice allocation with bound-gated priority stealing and note-drop; M4-03 §1/§7 and M4-05 S3 corrected in place), **Q-74 opened** (SSSQ tempo unit / SFX tick-quantization mapping discrepancy — 8.0 ms vs 2.0833 ms per tick at the default; asset-validated-pending). No `asset-validated` claims.
- **M6-02 — `research/audio/02-sequencer-sssq.md` — delivered and committed (2026-11-06).** Plan research questions 2 (SSSQ binary structure — soundfont/soundbank/instrument/layer/breath layouts, BGM command encoding — answered: byte-level SSSQ/Sssqish/ChannelInfo layouts, the complete SFX event grammar with per-family byte tables and both repeat mechanisms, the BGM `SequenceBuilder` wire grammar and 13-command set, the shared 0x10 instrument/layer records with the cross-player flag consistency table, the 16-byte ADPCM block with per-consumer flag behavior, the ADSR lo/hi bit layout, the breath FNV-preset matching) and 3 (sequencer state machines — answered: the per-tick 24-channel SFX state machine incl. note-targeted CCs and the voice-binding lifecycle, the per-sample BGM engine incl. voice-pool allocation and NRPN loops, the settled SFX tick quantization — tempo pinned to 60/480 during playback, 0x51 functionally dead, 8.0 ms/tick definitive), plus the MIDI→SSSQ conversion (custom MIDI container, tempo = BPM corroboration chain, the dev bank-decode/SF2 workflow) and worked synthetic command-stream examples. Register: **Q-74 narrowed** (SFX leg settled — the debug-log formula is log-only with no runtime impact; BGM = BPM stands), **Q-56 advanced** (Sssqish subfile 4 = exclusive SFX sequence carrier), **Q-75–Q-78 opened** (dev-tool shift-nibble discrepancy; `MidiToSssq` trailing 0x00 byte; SFX NRPN 0x11/0x12/0x13 `assert false` stubs; 5-file music package bank-count file role). M6-01 corrected in place (0x51 dead in the SFX path). No `asset-validated` claims.
- **M6-03 — `research/audio/03-xa-opus-audio.md` — delivered and committed (2026-11-06).** The XA/Opus/FMV-audio leg of the M6 plan questions: the three XA-consuming paths (LODXA music — unpack-time XA→Opus transcode + runtime `XaPlayer` Opus playback; FMV audio — runtime `XaAdpcm` decode at native 37800 Hz inside the 15 fps main-thread demux loop; mod video — layer-4 FFmpeg path), the full transcode spec (18-portion sector layout, both-nibble-plane decode, 80-step Catmull-Rom 37800→48000 at +63/80 with the `>>1` halving, 128 kbps Opus / 960-sample frames, the Ogg container with pre-skip 312 and the informational 37800 original-rate header), the corrected SKIPS track map (mask = archive trailing digit: LODXA00/01 → tracks 1–15, LODXA02 → 1–7/9–11, LODXA03 → single stereo track — credits in-repo consumer `playXaAudio(3, 3, 1)`), the decompiled `playXaAudio` staged 0–4 API (only stages 2/3 preserved; file indices 1–31), the volume-routing master table, and the C1–C12 classification. Register: **Q-47 closed** (the transcoder-vs-decoder mono-nibble discrepancy did not exist — both decode both nibble planes; M2-10 §4 corrected in place), **Q-48 closed** (FMV source registered at 37800 Hz = native data rate; no resampling; no rate hazard; original mixer gain → Q-73), **Q-57 advanced** (map settled from source + consumer evidence; rationale asset-validated-pending; M2-10 §3 corrected in place), **Q-69 advanced** ((b) resolved — the 6→7 zigzag FIR is dead code; (d) resolved — FMV XA is a single stream per file; (a) static-predictor semantics and (c) retail filter-4 usage remain), **Q-79 opened** (retail FMV 18900 Hz coding would play at 2× speed — flag parsed, unhandled; asset-validated-pending). No `asset-validated` claims.

**Scope.** Audio architecture (AudioThread loop, sources and buffering, device handling/reinit, volume routing); the SSSQ sequencer (soundfonts, soundbanks, instruments/layers, breathes, channels; BGM sequence command set; loading from unpacked assets); the MIDI→SSSQ unpack conversion; XA music handling and the Opus transcode/playback path; SPU SFX playback (queue, key on/off, reverb config); FMV audio; and the timing alignment of all audio paths to the frame model (from M1/M4).

**Primary reference sources.**
- Severed-Chains: `legend/core/audio/*` (AudioThread 547, Constants, sequencer/* incl. assets: SoundFont, SoundBank, Instrument, InstrumentLayer, Breath, Channel, sequence/bgm/* commands; opus/*: OpusFile, OggPage, OpusHead/Tags, XaPlayer; xa/XaTranscoder), `legend/game/sound/*` (Sequencer 629, Audio, SSSQ loaders, ReverbConfig), `legend/core/spu/*` (from M4), `legend/game/unpacker/midi/*` (MidiToSssq, SoundbankDecoder, SoundfontWriter, MidiCommand), `legend/game/fmv/*` (FMV audio).

**Research questions.**
1. What is the audio threading model: what ticks what, at what rates (sequencer tick vs 60 tps SPU vs OpenAL buffer pacing), and what synchronization exists?
2. What is the SSSQ binary format (soundfont/soundbank/instrument/layer/breath layouts, BGM command encoding: KeyOn/KeyOff/ProgramChange/PanChange/PitchBendChange/BreathChange/ModulationChange/EndOfTrack/DataEntry(Lsb/Msb))?
3. How faithfully does MIDI→SSSQ convert the original music assets, and what is lost/gained?
4. What is the XA→Opus transcode (sample rates, channels, compression settings) and why (performance of PSX XA playback)?
5. How do music and SFX interact (priority, ducking, crossfades, the `sssqResetStuff` per-state reset), and what are the volume/reverb semantics?
6. Which parts are original game behavior (sequence data, voice model) vs reference implementation choices (OpenAL, Opus, modern interpolation options)?

**Expected documentation.**
- `research/audio/01-audio-architecture.md`
- `research/audio/02-sequencer-sssq.md`
- `research/audio/03-xa-opus-audio.md`

**Verification criteria.**
- Audio data-flow diagram (assets → decode → sources → OpenAL) with tick-rate table.
- SSSQ byte-level layout tables with citations; at least one traced BGM command stream example.
- Original-behavior vs reference-enhancement layering explicit for every documented behavior (e.g., Opus path = reference implementation; sequence data = original).

**Dependencies.** M1, M4 (SPU/XA), M2 (asset formats). Feeds: M9, M10.

---

## M7 — Persistence: Saves, Campaigns, and Configuration

**Status: IN PROGRESS (started 2026-11-07; approved starting point `5a37d97`).** Two sequential deliverables under `research/saves/` (01 save formats, 02 config system), each checkpoint-verified and committed, followed by a bounded cross-system reconciliation pass. Builds on M1 (engine states, `writeSaveData`/`readSaveData` flow, boot order) and the R1 state-and-ownership documentation; config-entry registry registration by mods is partially M8 territory (the mod side is documented there). No retail save files are available, so no `asset-validated` claims.

**Scope.** Save data formats (retail and V1–V10: magic codes, structure, per-version fields, migration chain), memcards and campaign organization (`.dsav` files, memcard splitting, campaign naming), save/load lifecycle (which engine states are savable, `writeSaveData`/`readSaveData` Tag flow, crash-recovery saves), the `Tag` data model, and the configuration system (config entries typed/registry-registered, global vs per-save storage, `.dcnf` format, presets, runtime events).

**Primary reference sources.**
- Severed-Chains: `legend/game/saves/*` (SaveManager, SaveVersion, SeveredSavedGame, SeveredSavedCharacter V1/V2, SavedGame/MemcardSavedGame/RetailSavedGame/InvalidSavedGame, SaveMatcher/Migrator/Serializer/Deserializer, serializers/*: Retail, Legacy, V8, V9, V10, ConfigCollection, ConfigEntry hierarchy, ConfigStorage( Location), ConfigPreset*), `legend/core/tags/*` (12 tag types), `legend/game/types/GameState52c`, `EngineState.writeSaveData/readSaveData`, `Main` crash-recovery path.

**Research questions.**
1. What is the byte-level layout of each save version (V1–V10 and RETAIL), and what exactly migrates between versions?
2. What is the campaign/memcard model on disk (naming, structure, multiple saves per memcard, uncategorized saves)?
3. Which engine states are savable, and what data travels (Tag structure per state) vs what is recomputed?
4. What is the `.dcnf` config format, and how are global vs per-save configs merged? What is the config entry lifecycle (registry registration → default → preset → user value → event)?
5. How do crash-recovery saves and the "last savable engine state" mechanism work?
6. What is the compatibility surface: can retail (original) saves be loaded without modification, and what do V2–V7 legacy saves represent (previous releases)?

**Expected documentation.**
- `research/saves/01-save-formats.md`
- `research/saves/02-config.md`

**Verification criteria.**
- Version table (magic, name, era, key differences) with the migration chain fully specified.
- Tag-model documentation (all 12 tag types and their serialization) with citations.
- Config lifecycle diagram (registration → defaults → storage → runtime access → events) with the `.dcnf` format specified.
- Retail-save compatibility documented as `Verified` from the RetailSerializer source.

**Dependencies.** M1 (state machine), M8 partially (config entries are registry-registered by mods). Feeds: M9.

---

## M8 — Modding and Extensibility

**Scope.** The modding contract at the *behavioral* level (not the Java class-loading mechanism): mod discovery and lifecycle (ids, semver, required mods, load order, failure/wrong-version reporting), the registry system (all 21 registries, entry lifecycle, locking, delegates, events), the event system (10 event families, priorities, cancelability, per-mod error isolation), i18n/lang overrides, the core mods (`lod_core`, `lod`) as reference content, the debugger as a modding-adjacent tool, and script-patch-based modding (replacement/diff patches, `patches/libs` Fate libraries, replacements of game behavior).

**Primary reference sources.**
- Mod-Loader: all 33 files (ModManager, Mod, ModContainer, ModState, Latch, events/*, registries/*, i18n/LangManager, IoHelper, LocaleSpecificity).
- Severed-Chains: `legend/game/modding/**` (CoreMod ~1000+ lines with all config entries and core registrations, events/* — battle/characters/config/engine/gamestate/input/inventory/scripting/submap/worldmap, coremod/* — CoreEngineStateTypes, CoreModConfig, elements, shops), `legend/lodmod/**` (LodMod, additions, battleactions, characters, config, equipment, items, spells), `legend/turnorder/**`, `legend/core/Registries.java`, `legend/core/GameEngine.bootMods()`, `legend/game/debugger/**`, `patches/scripts.csv` (replacement vs diff entries).

**Research questions.**
1. What is the complete mod lifecycle (discover → version-check → instantiate → register → event init → ready → error/missing handling), and which observable properties must a replacement preserve?
2. What is the registry contract (naming, ownership by mod, mutability windows, locking, delegation, event firing on add/remove), and what are the 21 registries' element types and purposes?
3. What is the event contract (families, payloads, priorities, cancelable vs not, error isolation and its user-visible consequences)?
4. How do core mods register retail content, and what does that imply about the boundary between "engine" and "content" for the Rust project (analysis only — no design decisions)?
5. What are the modding extension points for scripts (Fate libs, replacements, diffs, registry-read/write from scripts, script event hooks)?
6. What is the i18n override model (locale specificity, file formats, precedence)?
7. What are the Java-specific mechanisms (URLClassLoader, reflections, process-wide static active mod — audit F-04) and what is their *observable* contract vs implementation detail (to inform — not dictate — future design)?

**Expected documentation.**
- `research/modding/01-mod-loading.md`
- `research/modding/02-registries-and-events.md`
- `research/modding/03-core-mods.md`

**Verification criteria.**
- A language-neutral "mod contract" document: discovery inputs, lifecycle states, registration semantics, event semantics, failure modes — each with source citations, explicitly separating observable contract from Java implementation.
- Full registry inventory table (id, element type, registrar mod, lock behavior, key events).
- Event family table (event, payload, cancelable, priorities used, error behavior).
- Core-mod content inventory (what lod_core vs lod each register, with counts and examples).

**Dependencies.** M1 (boot order). Parallel with M3–M7. Feeds: M9, M10.

---

## M9 — Game Systems (Content and Rules)

**Scope.** The gameplay layer, system by system: title (menu, FMV intro, update check); submap/field (environment files, objects, collision primitives, scripted movement, encounters, save points, smoke/snow effects, cut transitions); world map (continents, locations, paths, destination markers, atmosphere/water/smoke effects, zoom overlays, Coolon Queen Fury overlay); battle (battle states, entity types BENT/DEFF, turn order, additions and their hit properties, formulas — damage/HP/MP/SP, effects, particles, encounters, post-battle actions, battle UI, environment); characters (stat types/mods, elements, level-up actions, spell unlocks, addition unlocks, dragoons/spirits, templates); inventory (goods/items/equipment, stack behavior, shops, the Dabas entry point and rewards); the Dabas minigame (Mogul machine model, game rules, save format, host integration); credits.

**Primary reference sources.**
- Severed-Chains: `legend/game/combat/**` (291 files: Battle, battlestate/*, bent/*, deff/*, formula/*, effects/*, encounters/*, environment/*, particles/*, postbattleactions/*, types/*, ui/*), `legend/game/submap/**` (41), `legend/game/wmap/**` (31), `legend/game/characters/**` (44), `legend/game/inventory/**` (104), `legend/game/additions/**` (9), `legend/lodmod/**` (retail values: items, spells, equipment, shops, characters, `patches/meta/legend.game.combat.bent.BattleEntityStat.csv` and sibling meta CSVs for stat/action/enum tables), `legend/turnorder/**`, `legend/game/title/**`, `legend/game/dabas/**` + Dig-Dabas (all of it), `legend/game/credits/**`.
- Script-Recompiler: Fate libraries in `patches/libs` (`char_utils.fate`, `queen_fury.fate`, etc.) and `patches/scripts/*.txt` (replacement scripts such as `player_combat_script.txt`, `throw_item.txt`, `widescreen.txt`) — these encode the modding-relevant gameplay surface.

**Research questions (per system).**
1. **Submap:** environment data model (textures, metrics, foreground/background), object types and their script bindings, collision (primitives, vertex info, primitive index), encounter data/rates, transitions (cuts, map transitions, world-map exits), save points, and what is data-driven vs code.
2. **World map:** continent/location/path data model, movement and camera model, atmosphere effects, markers, and the Coolon Queen Fury overlay (a known special case).
3. **Battle:** the battle state machine (states and transitions), entity model (BENT/DEFF stat layouts from the meta CSVs), turn order computation, addition system (types, hit properties, unlock criteria), damage/formula tables (exact formulas and constants), effects/particles, encounters, post-battle actions, and the battle UI.
4. **Characters/progression:** stat system (unary vs vitals, mod types, fractional stats), level-up actions, spell/addition unlock criteria (level, mastery, dragoon, spirit), elements, and the character template model.
5. **Inventory:** good/item/equipment distinctions, stack and inventory-entry model, shop model, equip/unequip rules, and the Dabas item/flow.
6. **Dabas:** complete machine model (CPU semantics used, timers, SPU, memory map), game rules (caves, chests, bosses, rewards, speech bubbles), save format (Save60 + tiles), and host integration details — including the open question of whether the "Mogul" machine maps to the real DABAS.BIN disassembly (Q-04).
7. **Cross-system:** which gameplay behaviors are script-driven (and thus in M3 scope) vs hardcoded, and where the two interleave (e.g., combat scripts + battle state machine).

**Expected documentation.** One document per system:
- `research/game/01-dabas-minigame.md`
- `research/game/02-title.md`
- `research/game/03-submap.md`
- `research/game/04-worldmap.md`
- `research/game/05-battle.md` (likely splits into 05a state machine / 05b entities & formulas / 05c additions & UI if size demands)
- `research/game/06-characters-and-progression.md`
- `research/game/07-inventory-and-items.md`

**Verification criteria.**
- Each system document contains: data model (structs with byte/field citations), state machines where applicable, behavior tables (e.g., damage formulas with every constant), the script/code boundary, and the retail-value source (lodmod registries / meta CSVs).
- Formulas and constants exact (bit layouts, scaling, rounding order) with citations; anything not derivable from source marked `Unresolved`.
- Every system explicitly tagged for original vs reference-implementation behavior (the reference fixes many original bugs; the catalog from M2's fix list informs this).

**Dependencies.** M1–M8 (all of them, in varying degrees). Feeds: M10.

---

## M10 — Cross-System Architecture Review

**Scope.** Synthesis: full initialization ordering (boot → title → each engine state), data flow and event flow across subsystems, shared-state and ownership inventory, runtime services (loader, audio, GPU, scripts, saves, config, i18n, input), the compatibility constraint set assembled from M2/M4/M5/M7, existing architectural limitations of the reference (threading constraints, global state, monolith of segment classes, Java-specific couplings), and an implementation-readiness assessment for the Rust project (informational — no crate/API decisions).

**Primary reference sources.** All of the above; M1–M9 documents as the primary input; cross-checks against `GameEngine`, `Scus94491BpeSegment`, `EngineStates`, `Registries`, and the unpacker.

**Research questions.**
1. Can a single, complete, cited dependency/initialization graph be produced (every service, its constructor-time deps, init-time deps, per-frame use, shutdown order)?
2. What are the shared-state hot spots (globals in segment classes, `GameState52c`, texture/obj pools, script storage) and their thread-safety story?
3. Which reference limitations are architectural (would constrain any faithful reimplementation) vs language-specific (Java/GC/classloading) and thus avoidable?
4. What is the consolidated compatibility checklist (from T-COMPAT + M2/M5/M7) that the Rust project must meet to be behavior-compatible?
5. What remains unresolved after M1–M9, and what are the concrete research tasks (and asset needs) to close them?

**Expected documentation.**
- `research/cross/01-architecture-review.md` (dependency/init graph, data/event flow, ownership, shared state)
- `research/cross/02-compatibility-checklist.md` (consolidated constraints)
- `research/cross/03-implementation-readiness-notes.md` (limitations + open items; explicitly *not* a design proposal)

**Verification criteria.**
- The architecture review cross-references at least one document per milestone M1–M9 and introduces no new unsourced claims.
- The compatibility checklist rows each trace back to a specific document + confidence class.
- All open items in `04-unresolved-questions.md` re-triaged: resolved (with pointer), or still open (with the exact evidence needed).

**Dependencies.** M1–M9 complete.

---

## General Working Method (all milestones)

1. **Incremental and evidence-first.** Investigate a subsystem, write its document, update the index, then proceed. No document without investigated content.
2. **Register hygiene.** New open questions/conflicts go into `04-unresolved-questions.md` immediately; resolved ones are closed with a pointer to the evidence, not deleted.
3. **Cross-referencing.** Documents cite sibling documents and the index; the index tracks every document's status.
4. **No implementation leakage.** Rust crates, APIs, or dependency choices are not designed in any document; the future crate table in the project brief is context only.
5. **Milestone exit review.** Before starting the next milestone: verify the exit criteria, list files created/modified, update the index, and **stop for approval** (per the Phase R0 directive; the same gate applies between later milestones unless the user waives it).

## Open Planning Questions (for user input, not blockers)

- **P-01:** Should M9 sub-milestones be approved one system at a time (recommended given size)?
- **P-02:** Is access to real LoD disc images (user-provided `isos/`) expected during research, which would allow byte-level verification of M2/M3 formats? If not, all such claims stay `Inferred`/`Unresolved` by source-only evidence.
- **P-03:** Should the debugger subsystem get its own topic/document (currently folded into M8 scope as modding-adjacent)?
