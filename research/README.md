# Severed Chains — Research Index

Research foundation for the Rust reimplementation of the Severed Chains ecosystem for *The Legend of Dragoon* (PSX, SCUS94491).

**Phase status (2026-11-02):** R0 (reconnaissance), R1 (foundational architecture and execution model), the independent xhigh audit (`research/audits/r0-r1-xhigh-audit.md`), and **M2 (asset pipeline and binary formats)** are complete. M2 delivered ten documents under `research/assets/` (nine core + one supplementary music document). Detailed subsystem research continues with M3 (Fate scripting) only after approval. No disc images or extracted proprietary assets were available during M2; byte-level format claims rest on the reference decoders/encoders and are tagged `source-verified` / `format-corroborated`, with `asset-validated` reserved for real-data confirmation (none yet).

## Documents

| Document | Purpose | Status |
|---|---|---|
| [`01-repository-inventory.md`](01-repository-inventory.md) | Per-repository inventory: purpose, revision, build, dependencies, entry points, source layout, tests, CI | Complete (R0) |
| [`02-cross-repository-map.md`](02-cross-repository-map.md) | How the four repositories relate: dependencies, shared formats, integration points, high-level subsystem map | Complete (R0) |
| [`03-research-plan.md`](03-research-plan.md) | Milestone-based research plan (M1–M10) with scope, sources, questions, expected documents, verification criteria, dependencies; R1 phase alignment; M1/M2 completion | In progress (R0/R1/M2) |
| [`04-unresolved-questions.md`](04-unresolved-questions.md) | Register of unresolved questions, known conflicts, and research risks | Active register |
| [`platform/01-application-lifecycle.md`](platform/01-application-lifecycle.md) | Startup sequence, loading-screen gates, transition to game, ROM init, shutdown/cleanup, failure and recovery, thread inventory | Complete (R1) |
| [`platform/02-execution-model.md`](platform/02-execution-model.md) | Platform loop, frame-driver ordering (onDraw phases), gameLoop phase table, input processing order, timing model, audio update timing, render paths, essential-vs-reference ordering | Complete (R1) |
| [`platform/04-engine-states.md`](platform/04-engine-states.md) | Engine state types/instances, queued transitions, per-state hooks, per-state script-native tables, state machine diagram, save integration, render modes | Complete (R1) |
| [`platform/05-input-and-platform.md`](platform/05-input-and-platform.md) | Platform abstraction, window, input model (actions/activations/bindings), state semantics, deadzones, focus, rumble, dispatch order, reference-vs-original boundary | Complete (R1) |
| [`architecture/01-runtime-subsystems.md`](architecture/01-runtime-subsystems.md) | Subsystem inventory, communication mechanisms, service/init dependencies, resource lifetimes, error propagation, extension points, requirements-vs-incidental | Foundational pass (R1) |
| [`architecture/02-subsystem-interactions.md`](architecture/02-subsystem-interactions.md) | Per-frame interaction sequence, initialization interactions, data flows, event flows, cross-thread matrix, failure interactions | Foundational pass (R1) |
| [`architecture/03-state-and-ownership.md`](architecture/03-state-and-ownership.md) | Global game state (GameState52c + segment globals), service ownership by thread, frame-bound state, invariants to preserve, known unsafe corners | Foundational pass (R1) |
| [`psx/01-compatibility-boundaries.md`](psx/01-compatibility-boundaries.md) | Four-layer classification (PSX / psx-sdk candidate / LoD / reference) by domain; psx-sdk candidate pool; boundary cautions | Foundational pass (R1) |
| [`assets/01-iso-and-file-tree.md`](assets/01-iso-and-file-tree.md) | ISO/CD access (2352 raw sectors, PVD, directory records, Form-2), cross-disc tree merge, runtime virtual paths, mrg index files, DrgnFiles addressing, load lifecycle, failure contract | Complete (M2) |
| [`assets/02-mrg-archive-format.md`](assets/02-mrg-archive-format.md) | MRG container (magic 0x4d524720, 0x08 entry table, 0x800-aligned offsets), FileMap, virtual/duplicate/orphan entries, unmrg, runtime re-assembly, LoD structural conventions | Complete (M2) |
| [`assets/03-unpacker-pipeline.md`](assets/03-unpacker-pipeline.md) | Queue + branch/replacement pipeline, 29-leaf-transformer registry, BPE/DEFF formats, replacement/branch transformers, bugfix catalog, behavior changes, determinism | Complete (M2) |
| [`assets/04-original-executable-overlay.md`](assets/04-original-executable-overlay.md) | SCUS_944.91 executable, lod_engine slice (0xa00, 0x36228), embedded shadow/font assets, SMAP/S_BTLD overlay slices, consumers, exhaustiveness limits | Complete (M2) |
| [`assets/05-tmd-model-format.md`](assets/05-tmd-model-format.md) | TMD/TmdHeader/TmdObjTable1c layouts, TmdWithId, CContainer, DEFF parts, primitive-run semantics, updateTmdPacketIlen, CTMD bit-stream + converter, pointer rewriting | Complete (M2) |
| [`assets/06-tim-texture-format.md`](assets/06-tim-texture-format.md) | TIM layout (CLUT/no-CLUT, the no-CLUT source conflict), bpp formats, GPU upload, getTimSize, PXL sheets, MCQ strip format, retail TIM uses | Complete (M2) |
| [`assets/07-animation-format.md`](assets/07-animation-format.md) | TmdAnimationFile, Lmb/LmbType0 (Type1/2 unapplied), Cmb, Keyframe0c, LmbTransforms14, unpack-time animation patches, reference gaps | Complete (M2) |
| [`assets/08-text-and-i18n-formats.md`](assets/08-text-and-i18n-formats.md) | In-game text location (DRGN scripts), JSON/PNG reference fonts + embedded font.tim, save-card portrait/spirit extraction, DABAS.BIN fixed offsets, mod metadata | Complete (M2) |
| [`assets/09-fmv-mdec.md`](assets/09-fmv-mdec.md) | FMV .IKI sector streams, VHS sector structure, MDEC decode, XA ADPCM audio, 15 fps presentation, FFmpeg mod-video path, reusability notes | Complete (M2) |
| [`assets/10-music-formats.md`](assets/10-music-formats.md) | SSSQ event stream + 24-channel sequencer engine, SSHD sound container, XA music interleave (LODXA00-03), XA→Opus transcode, MIDI/soundbank dev tools | Complete (M2, supplementary) |
| [`audits/r0-r1-xhigh-audit.md`](audits/r0-r1-xhigh-audit.md) | Independent adversarial audit of R0/R1: findings register, coverage matrix, architectural risk assessment, M2 readiness (APPROVED, corrections applied) | Complete (2026-11-02) |

## Conventions

- **Confidence classification.** Every non-trivial finding is labeled `Verified`, `Inferred`, `Unresolved`, or `Proposed` (see `AGENTS.md` for definitions). Inferences are never presented as verified.
- **Evidence format.** Findings cite: repository, revision, file path, symbol, line range where practical, observed behavior.
- **Layer separation.** Findings are tagged where relevant as: *original game behavior*, *reference-implementation behavior* (fixes/enhancements/workarounds), or *proposed design*.
- **Language.** All documentation is in English and independent of the implementation language (no Rust API design in research documents).
- **No placeholder files.** A document exists only when its subject has actually been investigated.

## Research Topics and Status

Status values: `Not started` / `In progress` / `Documented` / `Needs re-verification`.

| ID | Topic | Status | Planned document(s) | Milestone | Repositories |
|---|---|---|---|---|---|
| T-PLAT | Application lifecycle, startup/shutdown ordering | **Documented** | `research/platform/01-application-lifecycle.md` | M1 | Severed-Chains |
| T-LOOP | Game loop, execution phases, frame timing | **Documented** | `research/platform/02-execution-model.md` (absorbed T-LOOP + T-TIME) | M1 | Severed-Chains |
| T-TIME | Timing model, threads, scheduling, vsync/frame-skip | **Documented** (frame-skip/turbo intent open: Q-36) | `research/platform/02-execution-model.md` §4-5 | M1 | Severed-Chains |
| T-STATE | Engine state machine (states, overlays, transitions) | **Documented** | `research/platform/04-engine-states.md` | M1 | Severed-Chains |
| T-INPUT | Input actions, bindings, controller support | **Documented** (retail action inventory deferred to M9) | `research/platform/05-input-and-platform.md` | M1 | Severed-Chains, Mod-Loader |
| T-ISO | PSX CD/ISO access and the DRGN file tree | **Documented (M2)** | `research/assets/01-iso-and-file-tree.md` | M2 | Severed-Chains |
| T-MRG | MRG archive format, virtual files, sector alignment | **Documented (M2)** | `research/assets/02-mrg-archive-format.md` | M2 | Severed-Chains |
| T-UNPACK | Unpacker pipeline, leaf transformations, bug-fix patches | **Documented (M2)** | `research/assets/03-unpacker-pipeline.md` | M2 | Severed-Chains |
| T-EXEC | Original executable overlay (SCUS_944.91) and embedded assets | **Documented (M2; exhaustiveness open: Q-11/Q-51)** | `research/assets/04-original-executable-overlay.md` | M2 | Severed-Chains |
| T-TMD | Model formats (.tmd/.ctmd), containers, Assimp-based conversion | **Documented (M2; GPRIM cross-check open: Q-53/Q-54)** | `research/assets/05-tmd-model-format.md` | M2 | Severed-Chains |
| T-TIM | Texture format (.tim), CLUTs, VRAM uploads | **Documented (M2; no-CLUT conflict open: Q-44/Q-58)** | `research/assets/06-tim-texture-format.md` | M2 | Severed-Chains |
| T-ANIM | Animation format (.anim), keyframes | **Documented (M2; LmbType1/2 open: Q-49)** | `research/assets/07-animation-format.md` | M2 | Severed-Chains |
| T-TEXT | Compressed text (.mcq), UI strings, i18n .lang files | **Documented (M2; retail string encoding is M3)** | `research/assets/08-text-and-i18n-formats.md`, MCQ in `assets/06` §4 | M2 | Severed-Chains, Mod-Loader |
| T-FMV | FMV/MDEC video format, playback, rumble data | **Documented (M2; audio-rate mismatch open: Q-48)** | `research/assets/09-fmv-mdec.md` | M2 | Severed-Chains |
| T-FATE-LANG | Fate text language (syntax, semantics, preprocessor) | Not started | `research/scripting/01-fate-language.md` | M3 | Script-Recompiler |
| T-FATE-BIN | Fate binary format (encoding, entries, strings, tables) | Not started | `research/scripting/02-fate-binary-format.md` | M3 | Script-Recompiler, Severed-Chains |
| T-FATE-TOOL | Decompiler, compiler, patcher behavior and round-trip rules | Not started | `research/scripting/03-toolchain.md` | M3 | Script-Recompiler |
| T-FATE-META | Meta system (named calls, parameters, enums; remote + local) | Not started | `research/scripting/04-meta-system.md` | M3 | Script-Recompiler, Severed-Chains |
| T-FATE-RUN | Runtime interpreter (ScriptState, ScriptManager, native calls) | Not started | `research/scripting/05-runtime-interpreter.md` | M3 | Severed-Chains |
| T-GPU | PSX GPU model: VRAM, draw area, ordering table, bpp modes | In progress (R1 boundary pass → `research/psx/01-compatibility-boundaries.md`; z-queue mechanism read, Q-21) | `research/psx/01-gpu-vram-ordering.md` | M4 | Severed-Chains |
| T-GTE | GTE register file and operations (fixed-point vs. float model) | In progress (R1 boundary pass; float model identified, Q-20) | `research/psx/02-gte.md` | M4 | Severed-Chains |
| T-SPU | SPU voices, ADSR, reverb, key on/off, tick rate | In progress (R1 boundary pass; model mechanics read in execution model §6, Q-22) | `research/psx/03-spu.md` | M4 | Severed-Chains, Dig-Dabas |
| T-XA | XA ADPCM format, coefficients, resampling, headers | In progress (R1 boundary pass + M2 decoder/transcoder pass in `assets/09` §5, `assets/10` §3-4; tables documented, Q-23/Q-47) | `research/psx/04-xa-adpcm.md` | M4 | Severed-Chains |
| T-COMPAT | Compatibility classification: hardware-accurate vs. behaviorally-equivalent vs. format-only | **Foundational pass done (R1)** → `research/psx/01-compatibility-boundaries.md`; per-constant classification in M4 | `research/psx/05-compatibility-classification.md` | M4 | All |
| T-RENDER | RenderEngine, queued models, batches, cameras, framebuffers | Not started | `research/rendering/01-render-engine.md` | M5 | Severed-Chains |
| T-TMDPIPE | TMD rendering pipeline (dedicated shaders, vertex processing) | Not started | `research/rendering/02-tmd-pipeline.md` | M5 | Severed-Chains |
| T-UI | UI toolkit (SItem), text rendering, fonts (retail TIM vs. modern) | Not started | `research/rendering/03-ui-and-text.md` | M5 | Severed-Chains |
| T-POST | Widescreen, post-processing shaders, resolution modes | Not started | `research/rendering/04-postprocessing.md` | M5 | Severed-Chains |
| T-AUD-ARCH | Audio architecture: AudioThread, sources, device handling | Not started | `research/audio/01-audio-architecture.md` | M6 | Severed-Chains |
| T-SSSQ | Sequencer/SSSQ: soundfonts, soundbanks, BGM sequences, MIDI conversion | **Format documented (M2)** → `research/assets/10-music-formats.md`; M6 architecture doc pending | `research/audio/02-sequencer-sssq.md` | M6 | Severed-Chains |
| T-OPUS | XA-to-Opus transcoding, Opus playback path | **Format documented (M2)** → `research/assets/10-music-formats.md` §3-4; M6 architecture doc pending | `research/audio/03-xa-opus-audio.md` | M6 | Severed-Chains |
| T-SAVE | Save data formats (RETAIL/V1–V10), memcards, campaigns, crash saves | Not started | `research/saves/01-save-formats.md` | M7 | Severed-Chains |
| T-CONF | Config system (entries, presets, .dcnf storage, global vs. per-save) | Not started | `research/saves/02-config.md` | M7 | Severed-Chains, Mod-Loader |
| T-MOD | Mod discovery/loading lifecycle (jar scanning, class loading, versioning) | In progress (R1: boot sequence + per-campaign re-entrancy documented in platform/01 §4; framework internals pending) | `research/modding/01-mod-loading.md` | M8 | Mod-Loader, Severed-Chains |
| T-REG | Registries, events, i18n/lang overrides | Not started | `research/modding/02-registries-and-events.md` | M8 | Mod-Loader, Severed-Chains |
| T-COREMOD | Core mods (lod_core, lod): content-as-mod, force-loaded mods | Not started | `research/modding/03-core-mods.md` | M8 | Severed-Chains |
| T-DABAS | Dabas minigame: "Mogul" machine model, de-emulation, host integration | Not started | `research/game/01-dabas-minigame.md` | M9 | Dig-Dabas, Severed-Chains |
| T-TITLE | Title state, FMV intro, update system | Not started | `research/game/02-title.md` | M9 | Severed-Chains |
| T-SMAP | Field exploration (submaps): environments, objects, collision, encounters | Not started | `research/game/03-submap.md` | M9 | Severed-Chains |
| T-WMAP | World map: continents, paths, locations, atmosphere | Not started | `research/game/04-worldmap.md` | M9 | Severed-Chains |
| T-BATTLE | Battle system: entities, turns, additions, formulas, effects, UI | Not started | `research/game/05-battle.md` | M9 | Severed-Chains |
| T-CHAR | Characters, stats, spells, level-up, unlocks, elements | Not started | `research/game/06-characters-and-progression.md` | M9 | Severed-Chains |
| T-INV | Inventory, items, equipment, goods, shops | Not started | `research/game/07-inventory-and-items.md` | M9 | Severed-Chains |
| T-XSYS | Cross-system: init ordering, data/event flow, shared state, ownership, limitations | **Foundational pass done (R1)** → `research/architecture/01..03`; full synthesis in M10 | `research/cross/01-architecture-review.md` | M10 | All |

## Reference Repositories

| Repository | Revision (2026-11-02) | License | Role in research |
|---|---|---|---|
| `Severed-Chains` | `0c08c679b6b54e167b00851f1f1458d4dea3eade` (2026-11-02) | AGPL-3.0 | Primary engine + game (decompiled ROM with preserved addresses) |
| `Script-Recompiler` | `3433e6f74bb6d562aeff0490d11e8937da437511` (2026-06-21) | AGPL-3.0 | Fate toolchain (decompiler/compiler/patcher) + runtime token/opcode definitions |
| `Mod-Loader` | `c692134907a070a8b2c7db7b2b779010bc16deb9` (2026-10-11) | AGPL-3.0 | Mod framework: discovery, registries, events, i18n |
| `Dig-Dabas` | `4c1fbfebced123d43282b6e3b08286336b6b05f0` (2026-05-20) | AGPL-3.0 | Dabas minigame reference (separate "Mogul" machine model; not PSX hardware) |

See [`01-repository-inventory.md`](01-repository-inventory.md) for details.

## Known Conflicts

Tracked in [`04-unresolved-questions.md`](04-unresolved-questions.md) (section: Conflicts). As of R0, two conflicts are registered: **C-01** (significant) — the Fate opcode tables differ between the engine runtime (`legend.game.scripting.OpType`) and the recompiler (`org.legendofdragoon.scripting.OpType`), including the presence of opcode 100 in the recompiler only; **C-02** (minor) — the two consumers pin different Mod-Loader versions (4.3.5 vs 4.2.5).

## Topic Dependencies

High-level dependency graph for the research plan (detailed per milestone in `03-research-plan.md`):

```
M1 Platform & lifecycle            ← DONE (R1, 2026-11-02)
   |
   +--> M2 Asset pipeline & binary formats  ← DONE (2026-11-02; research/assets/ 01-10)
   |        |
   |        +--> M3 Fate scripting (toolchain + runtime; scripts drive M9 systems)
   |                 |
   +--> M4 PSX hardware layer (GPU/GTE/SPU/XA; needed to classify compatibility)
   |        |          ^ R1 delivered the boundary-level classification pass (psx/01); M2 documented the XA decoder tables and TMD/MCQ GPU interactions
   +--> M5 Rendering & presentation (builds on M4 GPU model + M2 formats)
   |
   +--> M6 Audio & music (builds on M4 SPU/XA; sequencer is independent)
   |
   +--> M7 Saves & config (independent of render/audio; needed before M9 gameplay)
   |
   +--> M8 Modding & extensibility (framework-level; informs everything downstream)
   |
   v
   M9 Game systems (title, submap, worldmap, battle, characters, inventory, Dabas)
   |
   v
   M10 Cross-system architecture review (synthesis; requires M1–M9; R1 delivered the foundational pass)
```

M3 (scripting) and M4 (PSX hardware) can proceed in parallel once M1–M2 are done; M5–M8 are largely parallelizable. M9 and M10 are strictly last.

## Future Documentation Structure (Proposed)

Topic-specific documents will be created under `research/` in subdirectories by subject. This is a **proposal** for file organization, not a commitment to create all of them:

```
research/
  README.md                     (this index)
  01-repository-inventory.md
  02-cross-repository-map.md
  03-research-plan.md
  04-unresolved-questions.md
  platform/                     (M1 — complete, R1)
  architecture/                 (R1 foundational pass; refined in M10)
  assets/                       (M2 — complete, 10 documents)
  scripting/                    (M3)
  psx/                          (M4; foundational boundary doc from R1)
  rendering/                    (M5)
  audio/                        (M6)
  saves/                        (M7)
  modding/                      (M8)
  game/                         (M9)
  cross/                        (M10)
```

Subdirectories are created only when the first document for that subject is written.

## R0 Findings Summary

Key architectural discoveries from repository reconnaissance (details and evidence in the linked documents):

1. **Severed-Chains is a decompiled ROM, not an emulator.** Source symbols preserve original PSX memory addresses (`@Method(0x80011e1cL)` annotations, address-suffixed fields, `Scus94491BpeSegment*` classes). The code is a high-level rewrite of the original game binary, layered on modern rendering/audio/input.
2. **Game content is delivered as mods.** Force-loaded mods `lod_core` (CoreMod) and `lod` (LodMod) register all retail content (items, spells, characters, engine states, input actions, config entries) through the same registry/event framework used by third-party mods.
3. **The asset pipeline is an unpack-and-transform cache.** Four PSX CD images (ISO9660, 2352-byte raw sectors) are read, the DRGN/MRG file tree is extracted, and a recursive transformation pipeline decompresses, unpacks, transcodes (XA→Opus, MIDI→SSSQ), extracts embedded assets (including from the original executable overlay `SCUS_944.91`), and applies hardcoded bug fixes. Results are cached in `files/`.
4. **Fate scripting has three distinct parts** that must not be conflated: the *language/binary format* (DRGN script binaries), the *toolchain* (Script-Recompiler: decompiler, ANTLR-based compiler, text-diff patcher, remote meta), and the *runtime interpreter* (in-engine `ScriptState`/`ScriptManager` with native function calls). Patches are unified diffs applied to *decompiled text*, then recompiled.
5. **The PSX hardware layer is a functional model, not bit-exact emulation.** The GTE is implemented with floating-point JOML vectors; the GPU models VRAM (1024×512), an ordering table, and draw-area state, but renders through a modern OpenGL/OpenGL-ES renderer rather than executing PSX GPU command streams. Where bit-exactness actually matters is an open research question (T-COMPAT).
6. **Audio is dual-path.** SFX use a modeled 24-voice PSX SPU (ADSR, reverb, XA ADPCM, 60 ticks/s); music uses a modern soundfont-based sequencer (SSSQ) with XA audio transcoded to Opus.
7. **Dig-Dabas is not PSX hardware.** It reimplements the in-game "Dabas" minigame (32×32 1bpp, tile caves) on a modeled "Mogul" machine (ARMv4-style CPU, modeled peripherals at 0xA000000–0xD800010, RAM at 0x2000000+). Its git history documents a deliberate *de-emulation* (emulator → high-level code). The main repo embeds it as a library; its data comes from `OHTA/MCX/DABAS.BIN` on the disc. Whether the "Mogul" machine maps to the real `DABAS.BIN` disassembly is open (register Q-30).
8. **Saves are versioned binary blobs with magic codes** (retail `SC__` through `DSaa`/V10), with migration, memcard splitting, and crash-recovery saves.
9. **Reference-repository testing is minimal.** Only the main repo has tests (a JUnit boot harness, excluded from the build); the other three have no tests. Behavioral claims must rely on code inspection plus, where possible, reproducible behavior.

## R1 Findings Summary

Foundational architecture and execution model (details and evidence in the linked documents):

1. **The frame driver is a software-timer action loop, not vsync.** `PlatformManager.run()` ticks registered fixed-period `Action`s (input first, then each window's render action) and sleeps to the next due time. `SDL_GL_SetSwapInterval(0)` — no vsync; the window's render action tick is `swap → onDraw → clearPressed`. Slow machines drop ticks (catch-up skip), so the simulation is **frame-locked, variable-time** (max one logic tick per presented frame), while audio threads run real-time and decoupled. (`Verified`; platform/02 §1, §5.)
2. **The ROM's `gameLoop()` is preserved as the per-frame body** and runs as the render callback inside onDraw, in its original phase order (21 numbered calls, phases 0-20, including the PSX display-buffer swap at frame start and timestamp advance by `vsyncMode`). Input dispatch to the game state happens *between* frames (SDL input tick precedes the render tick each loop iteration) — a reference architectural choice replacing the original in-frame pad polling. (platform/02 §3, §4, §9.)
3. **Frame rates are per-context and original** (audit F-01 corrected the R1 draft): **60 Hz** for submap/world-map/credits exploration and battle transitions, **20 Hz** for battle (1× the original rate), **30 Hz** for title, game over, and world-map menu screens; every state transition forces 30 Hz first, and scripts can write the rate (game var 25). Full write-site census in platform/02 §5. The 60 Hz timestamp unit advances by `vsyncMode` per frame; `tickMultiplier()` records original per-context rates (submap 30, wmap 20, battle 20, credits 30 fps) for text cadence. (Q-04 closed, annotated by the audit.)
4. **Four steady-state threads + pools**: main ("Hardware") owns all frame-loop services; "OPEN_AL" (120 Hz) runs the music synthesis + Opus + SPU-source consumption; "SFX" (60 Hz) runs the game music state machine + the 24-voice SPU model; Async pool (fixed) does file I/O; plus a 2 s audio-device poll and on-demand JavaFX. Ownership and locks are mapped in architecture/03. (lifecycle §8, platform/02 §6, architecture/03.)
5. **Engine states: 7 retail states, queued frame-top transitions**, with one-frame-deferred scene teardown, per-state script-native override tables, and `LEGACY` (PSX H-division pseudo-perspective) render mode used only by SUBMAP and TITLE. (platform/04.)
6. **The PSX GPU model and the modern renderer coexist.** `Gpu` models VRAM (1024×512, 15/24-bit), double-buffered display, and ordering-table z-queues of GPU commands; `RenderEngine` is a modern GL pipeline with model pools, sorting, PSX translucency passes, and a post chain. Three presentation modes (OpenGL/legacy/VRAM, dev-toggleable) select how the GPU model's output is displayed; which objects use which path is an M4 question (Q-37). (platform/02 §7.)
7. **Startup is a two-layer bootstrap with explicit cross-thread gates** (`INIT_LOCK` unpack↔transition, `UPDATER_LOCK` update check), a loading-screen gate (cinematic + unpack + update check), and a crash-recovery save path. Unpack failure leaves the user on the loading screen (no in-app recovery). (platform/01.)
8. **The global ROM address space is the shared-state surface**: `GameState52c` (0x52c-byte campaign state) plus address-suffixed segment globals, all main-thread-owned; scripts, mods, and game code address the same objects by reference. Nine behavioral invariants to preserve are listed in architecture/03 §4.
9. **Compatibility boundary (first pass)**: self-contained, LoD-independent layer-1 models in the reference — VRAM/ordering-table/GPU-command model, GTE model, SPU model (voices/ADSR/noise/reverb, 60 Hz tick — layer-1 attribution of the cadence pending external PSX evidence, audit F-16), XA ADPCM decoder, Bpp formats, blending formulas, ISO9660+Form2 reader, MDEC codec, double-buffered display — form the `psx-sdk` candidate pool (psx/01 §3). Frequently-confused boundaries (GPU model vs renderer; the per-context LoD rates are not "PSX frame rate"; music is not SPU audio; Fate/Dabas are game software, not platform features) are flagged in psx/01 §4.
10. **Mod boot is re-entrant and per-campaign.** `bootMods` fully rebuilds the mod/event/registry/binding/language state and is called at startup, new game, title, save management, and — notably — per campaign from the campaign-selection screen (campaigns carry their own enabled-mod set via `ENABLED_MODS_CONFIG`, with fallback to all mods). "Which mods are loaded" is therefore a per-campaign runtime property, not a process-wide constant. (platform/01 §4.)

## M2 Findings Summary

Asset pipeline and binary formats (details and evidence in `research/assets/` 01-10). Confidence follows the M2 categories: `source-verified` (stated directly by reference code), `format-corroborated` (source + an independent corroborator, e.g. two decoders or a shipped asset), `asset-validated` (requires real disc/asset bytes — **none available yet**, so no M2 claim is at this level).

1. **The asset layer is a four-disc ISO9660 merge with per-entry disc provenance.** Four CD images (raw 2352-byte sectors; 2048-byte ISO blocks) are parsed from sector 16 (PVD); the disc-4 tree is the master and lower discs are grafted with `putIfAbsent` (disc 4 wins name collisions). Each `DirectoryEntry` retains its source-disc `IsoReader`, so data is read from the correct physical disc after the merge. `PVD` validation checks id 0x01, "CD001", version 0x01, "PLAYSTATION". (assets/01 §2-3; disc IDs SCUS94491/94584/94585/94586; 28 known other-region IDs → 7 region names.)
2. **Runtime asset access is a virtual path space, not an archive.** `Loader` resolves virtual paths under `Unpacker.ROOT` (`./files`); there is no runtime archive layer — only directories plus `mrg` index files, from which `loadDirectorySync` re-assembles virtual `FileData` views (`virtualSize`, `realFileIndex`). `.IKI`/`.XA` are forced to whole-sector raw reads. (assets/01 §4-5, assets/02 §5-6.)
3. **MRG is a trivial container.** Magic `0x4d524720` ("MRG "), u16 count @0x06, 0x08-byte entry table (name + u32 0x800-aligned sector offset), data region after; `unmrg`/`FileMap` round-trip it. Virtual/duplicate/orphan entry semantics and LoD structural conventions are catalogued. (assets/02.)
4. **The unpacker is a concurrent leaf-queue + sequential branch/replacement pipeline with a versioned cache.** `VERSION = 5`; mismatch deletes `files/` (keeps `.gitignore`). 29 leaf transformers in exact registration order (BPE decompress, DEFF undeff, TMD/CTMD, TIM/PXL, ANIM/LMB/CMB, XA→Opus, MIDI→SSSQ, save-card extraction, …); branch transformers (Submap PXL, Claire model fixer) and replacements (Chester texture) run after all leaf work. Failures complete async futures with `null` (`onFileLoadingException`, Loader.java:154). OOM → low-memory spill mode + restart. (assets/03.)
5. **BPE (LoD variant) and the DEFF container are byte-pinned.** BPE: decompressed size @0x0, marker `0x1a455042` @0x4, per-block self-describing byte-pair dictionary, ≤0x800-byte blocks, word-aligned; coverage content-based with a path exclusion band. DEFF: magic `0x46464544`, count @0x06, 8-byte slots, file n = [offset_n, offset_n+1). The BPE variant's identity with documented SCE BPE is open (Q-52). (assets/03 §5-6.)
6. **Embedded assets live in the original executable and two overlays.** `lod_engine = SCUS_944.91[0xa00 .. 0xa00+0x36228)`; from it: `shadow.ctmd` @0x3d0/0x14c, `shadow.anim` @0x51c/0x28, `shadow.tim` @0x544, `font.tim` @0xb6744 (uploaded as the system font at 64×56 @ (832,424)). From `SMAP.OV_`: `SUBMAP/savepoint` @0x10694/0x904 + 13 TIMs. From `S_BTLD.OV_`: `player_combat_script = slice(0x4, 0x68d4)` (consumed by Battle.java:9301) and `encounters = slice(0x68d8, 0x7000)` (no consumer found — Q-50). Exhaustiveness is explicitly unestablished (Q-11). (assets/04.)
7. **TMD is a LoD model container with a ROM-side primitive fixup.** `TmdHeader` + `TmdObjTable1c`; `Models.updateTmdPacketIlen` (`@Method(0x8003e638L)`, `updateTmdPacketIlen`) rewrites primitive-run lengths (size table 0x30-0x3d) at load. `CContainer` wraps TMD/CLUT/anim with a `TmdWithId` id field. CTMD is a compressed TMD (12-bit vertices, 10-bit normals, ROM bit-stream primitives at 0x800de840) with a converter that rewrites DEFF/CContainer pointers to emit a plain TMD. (assets/05; GPRIM cross-check Q-53.)
8. **TIM is the texture unit.** With CLUT: CLUT first (16bpp CLUT = u16 pairs, 4bpp CLUT = 256×u8), then a 0x14-byte image header, then 4/8/16-bit image data; `getTimSize` pins the on-disk sizes (and its u16 assumptions — Q-58). **No-CLUT TIMs: `Tim.java` and `Unpacker.getTimSize` disagree on the layout (Q-44); all reference-generated TIMs have CLUT.** PXL sheets (per-sobj 4bpp CLUT textures + 16 CLUT rows) convert to TIMs; the Lenus-2 CLUT fix is a documented bugfix. MCQ (0x2c header, magics 0x151_434d/0x251_434d) is a 4bpp background strip, not compressed text. (assets/06; Q-55.)
9. **Animation is a small family of LoD containers.** `TmdAnimationFile` (0xc part count, 0xe frames, keyframes @0x10), LMB (magic `0x42_4d4c`, `LmbType0` applied; **Type1/2 parsed but `apply` throws "Not implemented" — Q-49**), CMB (magic `0x2042_4d43`, per-frame delta transforms). DEFF part types dispatch: 0=LMB, 1/2=animated TMD, 3=single TMD, 4=sprite, 5=CMB. (assets/07, assets/05 §4.)
10. **Text and i18n are mostly reference-side or script-side.** Retail in-game strings live in DRGN scripts (M3); the reference UI uses JSON + PNG-atlas fonts plus the embedded `font.tim`; save-card portraits/spirits are extracted from three concatenated TIMs in `SECT/DRGN0.BIN/6665`; `DABAS.BIN` is a fixed-offset mod data blob read from the disc image (Dig-Dabas), not a self-describing archive. (assets/08.)
11. **FMVs are raw VHS sector streams decoded in software.** `STR/*.IKI` (18 retail files) → 5 sectors/frame (10 double-speed), 15 fps presentation, MDEC (software decode of the PSX MDEC codec) for video, XA ADPCM for audio. The reference resamples XA audio 37800→44100 Hz but registers the OpenAL source at 37800 Hz (Q-48 — latent one-tone-low playback). Mod videos can replace FMVs via FFmpeg. (assets/09.)
12. **Music: SSSQ/SSHD/XA are byte-pinned; XA music is transcoded to Opus at unpack time.** SSSQ: 0x110 header (tempo/ticks @0x2/0x4, magic 0x7173_5353, 16 channel records @0x10) + MIDI-wire event stream (varint deltas, running status, opcodes incl. the SSSQ `0x60` repeat extension), consumed by a 24-channel sequencer ticking at 60 Hz with 1-decimal fixed-point rounding. SSHD: 0x10 header, 28 u32 subfile offsets (incl. VolumeRamp, PatchList, and a 24-entry SSSQ variant — Q-56). XA music: LODXA00-02 = 16-interleaved mono tracks (15 used), LODXA03 = 4-interleaved stereo (3 used), skip masks 0xFFFE/0xEFE/0x2; transcoded 37800 Hz XA → 48 kHz Opus (960-sample frames, 128 kbps, preskip 312) — the in-game XA playback consumes the Opus files. XA ADPCM core (filter/coefficient/step tables, 28 samples/block-call, 18×128 portions/sector) is identical across `XaAdpcm`, `XaTranscoder`, and the dev `SoundbankDecoder` (mono-nibble discrepancy noted — Q-47). The SSSQ event grammar is confirmed by the `MidiToSssq` dev tool. (assets/10.)
13. **The unpack pipeline also carries the game's hardcoded bugfixes and behavior changes** (player/enemy/item damage caps removed, UI slash patch, skip party permutations, Claire model UVs, Chester texture, Lenus CLUT, specific DRGN script fixes) — a catalogued reference layer that a reimplementation must classify (original bug vs. reference fix) before replicating. (assets/03 §7-8.)
