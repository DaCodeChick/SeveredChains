# Cross-Repository Map

How the four reference repositories relate, the shared formats and conventions between them, and a high-level subsystem map. Based on the R0 reconnaissance (2026-11-02).

## 1. Dependency and Integration Graph

```
                        +-----------------------------------------------+
                        |                Severed-Chains                 |
                        |  (engine + game; 1322 main Java files)        |
                        |                                               |
   consumes as Maven    |  legend.core   (platform, PSX model, renderer,|
   artifacts (pinned):  |   audio, font, tags, memory)                 |
                        |  legend.game   (decompiled game + runtime     |
                        |   script interpreter + asset unpacker +       |
                        |   saves + modding glue + debugger)            |
                        |  legend.lodmod (retail content as a mod)      |
                        |  legend.turnorder (optional mod)              |
                        |  discord (rich presence)                      |
                        +---+----------------+----------------+---------+
                            |                |                |
              mod-loader    |                |                |  mogul-dabas
              4.3.5         |                |                |  0.0.1
                            v                v                v
        +--------------------------------+   +----------------------------------+   +--------------------------------+
        |          Mod-Loader            |   |        Script-Recompiler         |   |           Dig-Dabas          |
        |  (mod framework library)       |   |  (Fate tooling library + CLI)    |   |  (Dabas minigame library     |
        |                                |   |                                 |   |   + standalone app)          |
        |  - ModManager / @Mod / URLCL   |   |  - Disassembler / Assembler      |   |                            |
        |  - EventManager / events       |   |  - ANTLR Fate grammar            |   |  - "Mogul" machine model     |
        |  - Registries / RegistryId     |   |  - OpType (opcode table)         |   |    (ARMv4-style CPU, timers, |
        |  - LangManager (.lang)         |   |  - Patcher (text diffs)          |   |     SPU, interrupts)        |
        |                                |   |  - MetaManager (remote scmeta)   |   |  - Dabas game logic (6k lines)|
        +--------------------------------+   +----------------------------------+   +--------------------------------+

   Dig-Dabas also depends on mod-loader 4.2.5 (for @Mod-based organization of its own code).
   Script-Recompiler is standalone (no dependency on the other two).
```

`Verified` except where noted.

### Integration points, concretely

| Integration | How it works | Evidence |
|---|---|---|
| Main → Mod-Loader | `GameEngine` instantiates `ModManager` (force-loading `lod`, `lod_core`), `EventManager`, `Registries`, `LangManager`. Mods are JARs in `mods/` discovered via classpath scanning; all game content (items, spells, config, engine states, input actions…) is registered through `Registry`/`Registrar` objects; game systems post `Event`s that mods listen to. | `legend/core/GameEngine.java`, `legend/core/Registries.java` (21 registries), `legend/game/modding/**`, `ModManager.findMods` |
| Main → Script-Recompiler | Used at **two distinct times**: (1) at asset-unpack time, `ScriptPatcher` decompiles DRGN script binaries with the recompiler's `Disassembler`/`Translator`, applies unified diffs (and full text replacements) from `patches/`, recompiles with the `Assembler`, and writes patched binaries back into `files/`; (2) at runtime, the engine reuses the recompiler's token/opcode model (`org.legendofdragoon.scripting.tokens.Script`, `Op`, `OpType`, `Translator`) for script diagnostics/logging. | `legend/game/unpacker/scripts/ScriptPatcher.java`, `legend/game/scripting/ScriptState.java` (imports `org.legendofdragoon.scripting.*`) |
| Main → Dig-Dabas | The Dabas minigame runs as an embedded system: `legend/game/dabas/Dabas` implements `DabasInterface` (ticker, FPS, audio buffer, load/save, shutdown), renders the 32×32 framebuffer, maps engine input actions, and swaps the host renderer state while active. Save data (`Save60`) persists in the game config; rewards flow out via `DabasRewardsEvent`. Minigame data: `OHTA/MCX/DABAS.BIN` from the disc. | `legend/game/dabas/Dabas.java` (line 76), `legend/game/DabasManager.java`, `legend/game/inventory/screens/DabasScreen.java` |
| Dig-Dabas → Mod-Loader | Uses the `@Mod` annotation framework to organize its own code (older version pin 4.2.5 vs 4.3.5). | `Dig-Dabas/build.gradle` |
| Shared: Fate binary format | Single format, three consumers: the in-game runtime interpreter (Severed-Chains), the toolchain (Script-Recompiler), and the patch system (unified diffs over decompiled text). | `legend/game/scripting/OpType.java` vs `org/legendofdragoon/scripting/OpType.java` |
| Shared: meta (named calls) | `patches/meta/*.csv` in the main repo (params, descriptions, enums for script native calls) mirrors the remote metadata served from `legendofdragoon.org/scmeta/` that the recompiler's `MetaManager` consumes; CI builds it with the external `Severed-Chains-Metadata-Utility` scraper. | `Severed-Chains/patches/meta/`, `Shell.java` (MetaManager URI), `.github/workflows/publish.yml` |
| Shared: `.lang` i18n files | Mod-Loader's `LangManager` loads `.lang` files from mod jars and an overrides dir; the main repo ships `lod/lang/en.lang`, `lod_core/lang/en.lang`, `turn_order/lang/en.lang` as resources plus `lang/*.pl.lang` overrides at the repo root. | `Mod-Loader/.../LangManager.java`, `Severed-Chains/main/resources/**`, `Severed-Chains/lang/` |
| Shared: code duplication | `FileData` (byte-slice binary accessor) and the `@Method`/`Ref`/`IntRef` memory model exist in **both** `legend.core` (main) and `org.legendofdragoon.dabas.core` (Dig-Dabas) as separate classes — independent copies, not a shared library. | `Severed-Chains/src/main/java/legend/game/unpacker/FileData.java` vs `Dig-Dabas/.../core/FileData.java`; `legend/core/memory/*` vs `dabas/core/memory/*` |
| Shared: conventions | AGPL-3.0, Gradle + Maven publishing, log4j2, JOML, LWJGL, address-suffixed symbol naming in decompiled code (main and Dig-Dabas both use `name_0xaddress` globals and `@Method(0x...)` annotations). | All repos |

## 2. High-Level Subsystem Map (Severed-Chains)

The main repo is layered. The arrows below are "depends on / calls" at a high level; per-subsystem details are deferred to milestones M1–M10.

```
+-------------------------------------------------------------------------------+
|  Application shell (Main, Updater, Version, Discord, crash recovery)          |
+----------------------------------+--------------------------------------------+
                                   |
        +--------------------------v---------------------------------------------+
        |  Engine orchestration (GameEngine): singleton services, boot order,    |
        |  mod boot, texture atlas, intro/loading UI, transition to game         |
        +------+----------------------+----------------------+-------------------+
               |                      |                      |
   +-----------v-----------+ +--------v----------+ +---------v-----------+
   | Platform (SDL)        | | PSX-style models  | | Rendering           |
   | window, input actions,| | GPU (VRAM, draw   | | RenderEngine + API  |
   | bindings, devices,    | |  area, ordering   | | (OpenGL / GLES /    |
   | rumble, timer rates   | |  table, bpp), GTE | | Noop), shaders,     |
   +-----------------------+ | (float register    | | queued models,      |
                             |  file), SPU (24    | | TMD pipeline,       |
                             |  voices, ADSR,     | | cameras, post-fx    |
                             |  reverb, XA ADPCM) | +---------------------+
                             +---------------------+
        +-------------------------------------------------------------------------------+
        |  Asset pipeline (Unpacker): ISO (ISO9660) -> file tree (MRG/DRGN) ->         |
        |  recursive transformations (decompress, unmrg, undeff, XA->Opus, MIDI->SSSQ, |
        |  engine-overlay extraction, embedded-asset extraction, bug-fix patches)      |
        |  -> files/ cache; Loader (sync/async file access); ScriptPatcher            |
        +------------------------------+------------------------------------------------+
                                         |
   +-------------------------------------v---------------------------------------------+
   |  Game core (decompiled ROM): engine state machine (EngineState/EngineStates,     |
   |  CoreEngineStateTypes + LodEngineStateTypes), per-frame phases (gameLoop),        |
   |  global game state (GameState52c), address-named segment classes                  |
   +------+------------------+------------------+------------------+------------------+
          |                  |                  |                  |
   +------v------+   +-------v--------+  +------v--------+  +------v--------+
   | Title /     |   | Submap (field) |  | World map     |  | Battle        |
   | FMV intro   |   | environments,  |  | continents,   |  | entities,     |
   |             |   | objects,       |  | paths,        |  | turns,        |
   |             |   | collision,     |  | atmosphere    |  | additions,    |
   |             |   | encounters     |  |               |  | formulas, UI  |
   +-------------+   +----------------+  +---------------+  +---------------+
          |                  |                  |                  |
   +------v---------------------------------------------------------v------+
   |  Script runtime (ScriptManager, ScriptState, RunningScript,           |
   |  OpType, native engine functions)  +  SSSQ sequencer + audio thread   |
   +-------------------------------------------------------------------------------+
          |
   +------v---------------------------------------------------------v------+
   |  Content & modding: lod_core + lod (force-loaded mods), 21 registries,|
   |  event bus (10 event families), config entries, saves (V1..V10)       |
   +-------------------------------------------------------------------------------+
```

`Verified` at the level of package structure and the call sites examined during R0; the exact per-frame call graph and data flow are M1/M10 topics.

## 3. Where "Reusable PlayStation Functionality" Lives

Per the project's `psx-sdk` objective (future-intent only; no crate design here), R0 found the following candidate pools, each needing M4–M6 classification (hardware-accurate vs behaviorally-equivalent vs format-only compatibility):

| Candidate area | Location in references | Notes from R0 |
|---|---|---|
| GPU: VRAM (1024×512), 4/8/15/24-bpp modes, draw area + offsets, ordering table (0x4000 entries, zMax 0x3ffe), double-buffered draw targets | `legend/core/gpu/*` (13 files) | Functional model; rendering delegated to modern GL APIs. `Bpp` encodes PSX width-mask/index parameters. Whether command-stream fidelity matters is unresolved (T-GPU, T-COMPAT). |
| GTE: full R0–R63 register file (V, RGBC, OTZ, SXY, SZ, MAC, RT, light dir/colour, translation, background colour, screen offset, H, FLAG), operations (NCLIP, AVSZ3/4, rotations, projections…) | `legend/core/gte/Gte.java` (399 lines) | **Floating-point** (JOML) implementation of originally fixed-point hardware. Bit-exactness requirements unknown (T-GTE, T-COMPAT). |
| SPU: 24 voices, ADSR, reverb (0x4000 work area), per-channel FM/noise/reverb modes, key on/off, 60 ticks/s | `legend/core/spu/*` (9 files) | Functional model; outputs to the modern audio thread. Original SPU timing constraints (T-SPU) unverified. |
| XA ADPCM: 24-byte header, coefficient/step tables, zigzag tables, 6-step resampling | `legend/core/spu/XaAdpcm.java` | Close to documented PSX XA behavior (tables visible in source); still needs format verification against real XA streams (T-XA). |
| Fixed-point trig (`rsin`/`rcos` 256-entry style tables, `simpleRand` LCG, `rand`/`srand`) | `Scus94491BpeSegment` (lines 225–262) | Original game math helpers; likely behavior-significant for script determinism. |
| PSX CD access: ISO9660 on 2352-byte raw sectors, PVD at sector 16, Form2 microsector handling | `legend/game/unpacker/IsoReader.java` | Used only at unpack time, not at runtime (assets are cached to disk). |
| DRGN/MRG file-tree formats, decompression, DEFF, engine-overlay extraction | `legend/game/unpacker/*` | LoD-specific container layer over the PSX CD; the decompressor and DEFF need format-level documentation (T-UNPACK, T-MRG). |
| TIM texture format (image + CLUT data, upload rects) | `legend/game/tim/*`, `loadSystemFont` | LoD-specific wrapper over PSX VRAM texture data (T-TIM). |
| TMD/CTMD model format, ANIM animation, MCQ compressed text, SSSQ sound data, FMV/MDEC bitstream | `legend/game/tmd`, `tim`, `fmv`, `unpacker/midi`, `sound` | LoD-specific formats, some with PSX-flavored elements (MDEC is genuine PSX video codec) (T-TMD, T-ANIM, T-TEXT, T-SSSQ, T-FMV). |
| Controller input model (gamecontrollerdb, input actions/axes/buttons, deadzones, rumble) | `legend/core/platform/*` | Platform-generic; PSX-relevant only insofar as button mapping semantics (T-INPUT). |
| 30 Hz base frame timing, timestamp semantics | `GameEngine`/`gameLoop` | Original game timing model (T-TIME). |

**Not PSX-related** (despite living in the ecosystem): Dig-Dabas' "Mogul" machine (ARMv4-style CPU, 32×32 1bpp display, 8-bit SPU) is a modeled machine for the Dabas minigame; nothing in R0 evidence ties it to actual PSX hardware (T-DABAS, see unresolved questions Q-04).

## 4. Architectural Observations (R0)

Each item is `Verified` from source inspection unless tagged otherwise.

1. **Decompiled ROM with preserved addresses.** The game code is organized around the original binary: `@Method(0x...)` annotations, address-suffixed globals, segment classes named after the original executable (`Scus94491BpeSegment*`). Unnamed `FUN_` methods remain. Consequence for research: "original behavior" is *usually* directly recoverable from this code, but (a) the code may contain reference-implementation fixes indistinguishable from original logic without ROM cross-checks, and (b) renamed/merged logic loses some fidelity.
2. **Content-as-mod.** The retail game's content (all items, spells, characters, engine states, input actions, config options, campaign types) is registered by two force-loaded mods (`lod_core`, `lod`) through the same registry/event framework that third-party mods use. The "engine" is therefore closer to a mod platform with a mandatory base content set.
3. **Two-stage asset model.** Assets never come from the CD at runtime: an unpacker transforms the whole disc tree into a `files/` cache (versioned, deleted when the cache version bumps), and the game loads plain files. All format decoding and most bug fixes happen at unpack time.
4. **Script patching via text round-trip.** Script fixes ship as unified diffs (or full replacements) against *decompiled* Fate text, cached by CRC32, with backups. This makes the decompiler's canonical output format a de-facto stable ABI for patches.
5. **Functional hardware models, modern I/O.** GPU/GTE/SPU are re-implemented models (float GTE, GL-backed GPU, OpenAL-backed SPU), not emulations. The PSX-specific parts worth preserving are: VRAM dimensions and bpp modes, ordering-table sort semantics, SPU voice/ADSR/reverb model, XA ADPCM tables, 30 Hz timing, and the binary asset formats.
6. **Dual audio stacks.** PSX-style SPU for SFX (XA ADPCM, 24 voices, 60 tps) coexists with a modern soundfont sequencer (SSSQ) and an Opus-transcoded XA path for music; MIDI→SSSQ conversion happens at unpack time.
7. **Versioned persistence.** Saves (magic `SC__`→`DSaa`), config (`.dcnf`), and the unpack cache are all versioned with explicit migration paths; crash-recovery saves are a first-class feature.
8. **Mod framework is classloading-based in Java.** Discovery uses `URLClassLoader` + reflection scanning of `@Mod` classes. The *observable* modding contract (mod id, semver, required mods, registrations, event listeners, lang overrides, failure reporting) is language-neutral and is what a Rust reimplementation would need to preserve — not the class-loading mechanism itself (`Inferred` framing; the contract details are M8 scope).
9. **Dig-Dabas demonstrates the team's de-emulation method.** Its history (emulator → dememulated high-level code) is a documented precedent for converting machine-level behavior to functional models — relevant to how the Rust project should treat its own "hardware" layers (`Proposed` relevance, `Verified` history).
10. **Metadata is external.** Friendly names for script native calls, opcodes' parameter docs, and enum lists are served from a remote host (`legendofdragoon.org/scmeta/`) and mirrored locally as CSV; CI rebuilds them from a separate scraper repo. The Rust tooling must either re-derive or consume this metadata (T-FATE-META).
11. **Test coverage is near zero.** Only the main repo has a boot harness, excluded from builds; nothing in the ecosystem validates binary formats or script round-trips automatically. The Rust project's research documents should therefore carry heavier verification expectations (see plan, M2/M3).
12. **Debug tooling is substantial.** A JavaFX debugger (script debugger, live script debugger, combat/smap/character/gamestate editors, registry selector) suggests the decompiled codebase relies on in-engine inspection for development; a Rust reimplementation will need an equivalent story (M10 concern).

## 5. Areas Requiring Deeper Investigation (from R0 vantage)

- The exact per-frame call graph and thread ownership (which thread calls `SPU.tick`, how `RENDERER.run()` drives `gameLoop`, SDL event pump vs render callback) — M1.
- The DRGN/MRG file tree: how `SECT/DRGN0.BIN` (root, disc 4) indexes the other discs, script file addressing (`DRGNxx/nnnn/mm`), and the decompressor/DEFF formats — M2.
- What `SCUS_944.91` (the original executable overlay) contains beyond the extracted assets, and whether the decompiled `Scus94491BpeSegment*` code corresponds to it — M2/M10.
- Fate binary encoding details (param widths, string pools, table resolution) and the decompiler's disambiguation rules (branch vs table, while-loop recognition) — M3.
- Which GTE/GPU behaviors the game actually relies on in a bit-sensitive way (ordering-table collisions, dithering, blending, OTZ rounding) — M4/M5.
- Original-vs-modified behavior boundaries: catalog of the unpacker's hardcoded fix patches and the script patch set (300+ diffs) — M2/M3.
- SSSQ format and the MIDI→SSSQ conversion fidelity — M6.
- The Dabas "Mogul" machine: what the modeled CPU/timers/SPU correspond to in `DABAS.BIN` (is it a real disassembled binary with a synthetic memory map, or a fully re-modeled system?) — M9.
