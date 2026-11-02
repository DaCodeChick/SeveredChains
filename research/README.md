# Severed Chains — Research Index

Research foundation for the Rust reimplementation of the Severed Chains ecosystem for *The Legend of Dragoon* (PSX, SCUS94491).

**Phase R0 status (2026-11-02):** Repository reconnaissance complete. This index, the repository inventory, the cross-repository map, the milestone plan, and the unresolved-questions register are the initial deliverables. Detailed subsystem research begins with milestone M1 only after approval.

## Documents

| Document | Purpose | Status |
|---|---|---|
| [`01-repository-inventory.md`](01-repository-inventory.md) | Per-repository inventory: purpose, revision, build, dependencies, entry points, source layout, tests, CI | Complete (R0) |
| [`02-cross-repository-map.md`](02-cross-repository-map.md) | How the four repositories relate: dependencies, shared formats, integration points, high-level subsystem map | Complete (R0) |
| [`03-research-plan.md`](03-research-plan.md) | Milestone-based research plan (M1–M10) with scope, sources, questions, expected documents, verification criteria, dependencies | Proposed (R0) |
| [`04-unresolved-questions.md`](04-unresolved-questions.md) | Register of unresolved questions, known conflicts, and research risks | Active register |

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
| T-PLAT | Application lifecycle, startup/shutdown ordering | Not started | `research/platform/01-application-lifecycle.md` | M1 | Severed-Chains |
| T-LOOP | Game loop, execution phases, frame timing | Not started | `research/platform/02-game-loop-and-phases.md` | M1 | Severed-Chains |
| T-TIME | Timing model, threads, scheduling, vsync/frame-skip | Not started | `research/platform/03-timing-and-threads.md` | M1 | Severed-Chains |
| T-STATE | Engine state machine (states, overlays, transitions) | Not started | `research/platform/04-engine-states.md` | M1 | Severed-Chains |
| T-INPUT | Input actions, bindings, controller support | Not started | `research/platform/05-input-and-platform.md` | M1 | Severed-Chains, Mod-Loader |
| T-ISO | PSX CD/ISO access and the DRGN file tree | Not started | `research/assets/01-iso-and-file-tree.md` | M2 | Severed-Chains |
| T-MRG | MRG archive format, virtual files, sector alignment | Not started | `research/assets/02-mrg-archive-format.md` | M2 | Severed-Chains |
| T-UNPACK | Unpacker pipeline, leaf transformations, bug-fix patches | Not started | `research/assets/03-unpacker-pipeline.md` | M2 | Severed-Chains |
| T-EXEC | Original executable overlay (SCUS_944.91) and embedded assets | Not started | `research/assets/04-original-executable-overlay.md` | M2 | Severed-Chains |
| T-TMD | Model formats (.tmd/.ctmd), containers, Assimp-based conversion | Not started | `research/assets/05-tmd-model-format.md` | M2 | Severed-Chains |
| T-TIM | Texture format (.tim), CLUTs, VRAM uploads | Not started | `research/assets/06-tim-texture-format.md` | M2 | Severed-Chains |
| T-ANIM | Animation format (.anim), keyframes | Not started | `research/assets/07-animation-format.md` | M2 | Severed-Chains |
| T-TEXT | Compressed text (.mcq), UI strings, i18n .lang files | Not started | `research/assets/08-text-and-i18n-formats.md` | M2 | Severed-Chains, Mod-Loader |
| T-FMV | FMV/MDEC video format, playback, rumble data | Not started | `research/assets/09-fmv-mdec.md` | M2 | Severed-Chains |
| T-FATE-LANG | Fate text language (syntax, semantics, preprocessor) | Not started | `research/scripting/01-fate-language.md` | M3 | Script-Recompiler |
| T-FATE-BIN | Fate binary format (encoding, entries, strings, tables) | Not started | `research/scripting/02-fate-binary-format.md` | M3 | Script-Recompiler, Severed-Chains |
| T-FATE-TOOL | Decompiler, compiler, patcher behavior and round-trip rules | Not started | `research/scripting/03-toolchain.md` | M3 | Script-Recompiler |
| T-FATE-META | Meta system (named calls, parameters, enums; remote + local) | Not started | `research/scripting/04-meta-system.md` | M3 | Script-Recompiler, Severed-Chains |
| T-FATE-RUN | Runtime interpreter (ScriptState, ScriptManager, native calls) | Not started | `research/scripting/05-runtime-interpreter.md` | M3 | Severed-Chains |
| T-GPU | PSX GPU model: VRAM, draw area, ordering table, bpp modes | Not started | `research/psx/01-gpu-vram-ordering.md` | M4 | Severed-Chains |
| T-GTE | GTE register file and operations (fixed-point vs. float model) | Not started | `research/psx/02-gte.md` | M4 | Severed-Chains |
| T-SPU | SPU voices, ADSR, reverb, key on/off, tick rate | Not started | `research/psx/03-spu.md` | M4 | Severed-Chains, Dig-Dabas |
| T-XA | XA ADPCM format, coefficients, resampling, headers | Not started | `research/psx/04-xa-adpcm.md` | M4 | Severed-Chains |
| T-COMPAT | Compatibility classification: hardware-accurate vs. behaviorally-equivalent vs. format-only | Not started | `research/psx/05-compatibility-classification.md` | M4 | All |
| T-RENDER | RenderEngine, queued models, batches, cameras, framebuffers | Not started | `research/rendering/01-render-engine.md` | M5 | Severed-Chains |
| T-TMDPIPE | TMD rendering pipeline (dedicated shaders, vertex processing) | Not started | `research/rendering/02-tmd-pipeline.md` | M5 | Severed-Chains |
| T-UI | UI toolkit (SItem), text rendering, fonts (retail TIM vs. modern) | Not started | `research/rendering/03-ui-and-text.md` | M5 | Severed-Chains |
| T-POST | Widescreen, post-processing shaders, resolution modes | Not started | `research/rendering/04-postprocessing.md` | M5 | Severed-Chains |
| T-AUD-ARCH | Audio architecture: AudioThread, sources, device handling | Not started | `research/audio/01-audio-architecture.md` | M6 | Severed-Chains |
| T-SSSQ | Sequencer/SSSQ: soundfonts, soundbanks, BGM sequences, MIDI conversion | Not started | `research/audio/02-sequencer-sssq.md` | M6 | Severed-Chains |
| T-OPUS | XA-to-Opus transcoding, Opus playback path | Not started | `research/audio/03-xa-opus-audio.md` | M6 | Severed-Chains |
| T-SAVE | Save data formats (RETAIL/V1–V10), memcards, campaigns, crash saves | Not started | `research/saves/01-save-formats.md` | M7 | Severed-Chains |
| T-CONF | Config system (entries, presets, .dcnf storage, global vs. per-save) | Not started | `research/saves/02-config.md` | M7 | Severed-Chains, Mod-Loader |
| T-MOD | Mod discovery/loading lifecycle (jar scanning, class loading, versioning) | Not started | `research/modding/01-mod-loading.md` | M8 | Mod-Loader, Severed-Chains |
| T-REG | Registries, events, i18n/lang overrides | Not started | `research/modding/02-registries-and-events.md` | M8 | Mod-Loader, Severed-Chains |
| T-COREMOD | Core mods (lod_core, lod): content-as-mod, force-loaded mods | Not started | `research/modding/03-core-mods.md` | M8 | Severed-Chains |
| T-DABAS | Dabas minigame: "Mogul" machine model, de-emulation, host integration | Not started | `research/game/01-dabas-minigame.md` | M9 | Dig-Dabas, Severed-Chains |
| T-TITLE | Title state, FMV intro, update system | Not started | `research/game/02-title.md` | M9 | Severed-Chains |
| T-SMAP | Field exploration (submaps): environments, objects, collision, encounters | Not started | `research/game/03-submap.md` | M9 | Severed-Chains |
| T-WMAP | World map: continents, paths, locations, atmosphere | Not started | `research/game/04-worldmap.md` | M9 | Severed-Chains |
| T-BATTLE | Battle system: entities, turns, additions, formulas, effects, UI | Not started | `research/game/05-battle.md` | M9 | Severed-Chains |
| T-CHAR | Characters, stats, spells, level-up, unlocks, elements | Not started | `research/game/06-characters-and-progression.md` | M9 | Severed-Chains |
| T-INV | Inventory, items, equipment, goods, shops | Not started | `research/game/07-inventory-and-items.md` | M9 | Severed-Chains |
| T-XSYS | Cross-system: init ordering, data/event flow, shared state, ownership, limitations | Not started | `research/cross/01-architecture-review.md` | M10 | All |

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
M1 Platform & lifecycle
   |
   +--> M2 Asset pipeline & binary formats (needs loop/phasing context for load timing)
   |        |
   |        +--> M3 Fate scripting (toolchain + runtime; scripts drive M9 systems)
   |                 |
   +--> M4 PSX hardware layer (GPU/GTE/SPU/XA; needed to classify compatibility)
   |        |
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
   M10 Cross-system architecture review (synthesis; requires M1–M9)
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
  platform/                     (M1)
  assets/                       (M2)
  scripting/                    (M3)
  psx/                          (M4)
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
