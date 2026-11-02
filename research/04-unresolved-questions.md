# Unresolved Questions and Research Risks

Register of open questions, known conflicts, and research risks. Maintained continuously; entries are closed by pointing to the evidence that resolved them (never deleted). Status values: `Open` / `Investigating` / `Closed (→ doc)`.

## Conflicts

Disagreements between repositories (or between a repository and external expectations) that must be investigated rather than silently resolved.

### C-01 — Fate opcode table differs between engine runtime and recompiler

- **Where:**
  - Severed-Chains `src/main/java/legend/game/scripting/OpType.java` (93 lines, no parameter metadata; `byOpcode` **throws** on unknown opcodes).
  - Script-Recompiler `src/main/java/org/legendofdragoon/scripting/OpType.java` (160 lines, parameter names/directions; `byOpcode` **returns null**; `byName` exists).
- **Discrepancies observed (both `Verified` from source):**
  - The runtime enum carries **no parameter metadata at all** (only `opcode` + `name`, single 2-arg constructor); the recompiler enum records a header param and typed op params per opcode (`OpParam.in/out/both`).
  - The runtime has **no** opcode 100; the recompiler has `NOOP(100, "noop")`.
  - The recompiler records param details for the debug ops that the runtime cannot express: `NOOP_96(96, "debug96", "?", in "?", in "?")`, `NOOP_98(98, "debug98", in "?")`, `DEPTH(99, "depth", out "dest")` vs runtime `NOOP_96(96, "debug96")`, `NOOP_98(98, "debug98")`, `DEPTH(99, "depth")`.
  - `byOpcode` **throws** a `RuntimeException` on unknown opcodes in the runtime, but **returns null** in the recompiler (which additionally provides `byName`).
  - Both agree on the specialization opcodes (`WAIT_CMP_0(4)`, `MOV_0(12)`, `JMP_CMP_0(66)`), and the recompiler documents the specialization rule (`modifyOp`: drop the leading operand when it resolves to 0) — consistent, but the runtime's `byOpcode` is the only runtime-facing decoder.
- **Interpretation at risk:** which table is authoritative for (a) what the runtime interpreter accepts, and (b) what the toolchain emits. Presumably (a) is the runtime table and (b) the recompiler table, and the differences are drift — but opcode 100 and the debug-op param counts need reconciliation against real script binaries.
- **Status:** `Open`. Owner: M3 (T-FATE-BIN). Needs: disassembler's opcode reading code + at least one real script binary (or the patches' decompiled texts) to confirm whether opcode 100 or the debug-op params ever appear in practice.

### C-02 — Minor: Mod-Loader version drift between consumers

- Severed-Chains pins `mod-loader:4.3.5` (matches the local Mod-Loader checkout, `Verified`); Dig-Dabas pins `mod-loader:4.2.5` (older). Not expected to be behaviorally significant, but the two consumers see different framework versions.
- **Status:** `Open` (low priority). Owner: M8. Needs: diff of Mod-Loader 4.2.5→4.3.5 (git history between the corresponding commits) to confirm no contract changes.

## Open Questions

Format: **Q-##** — question — status — owner (milestone) — notes/evidence needed.

### Platform and lifecycle (M1)

- **Q-01** — How exactly does `RENDERER.run()` drive the per-frame game loop? `RenderEngine.run()` shows `window.show()` → `PLATFORM.run()` with a `renderCallback` mechanism, and `gameLoop()` is the per-frame body, but the precise registration point (e.g., via `bindRendererEvents()` at `Scus94491BpeSegment.java` line 196, whose body was not fully examined in R0) is not yet confirmed. — `Open` — M1 — needs: full read of `RenderEngine.run()/renderFrame()`, `SdlPlatformManager.run()`, `bindRendererEvents()`.
- **Q-02** — Which thread(s) call `SPU.tick()` and with what alignment to the 60 tps constant? (`Spu.SOUND_TPS = 60`, `SAMPLES_PER_TICK = BASE_SAMPLE_RATE / 60`, `tick()` observed but call site not yet traced.) — `Open` — M1/M4 — needs: call-site trace.
- **Q-03** — What is `FUN_80020ed8` (called at the end of every frame; partially identified: resets `submapFullyLoaded_800bd7b4`, tracks `previousEngineState_800bdb88`, sets transition flags for BATTLE→SUBMAP and TITLE→SUBMAP)? — `Open` — M1 — needs: full read of the method (present in `EngineStates.java`).
- **Q-04** — Is the base 30 Hz rate (`vsyncMode_8007a3b8 = 2`, `hz = 60/frames * speed`) original game behavior or a reference choice? The README does not state it. `Inferred`: NTSC LoD runs ~30 fps; the reference preserves it. — `Open` — M1/M4 — needs: external corroboration (original hardware behavior) + check for frame-skip interactions.
- **Q-05** — What remote does the auto-updater contact, and what is the full update protocol (staged `updater.jar.new`, version comparison, asset preservation)? — `Open` — M1 — needs: read of `legend/core/Updater*.java`.
- **Q-06** — What is the full per-frame phase contract: which phases may post events, allocate objects, load files, or mutate GPU state? (R0 established the order, not the invariants.) — `Open` — M1 — needs: systematic call-site audit.

### Assets and formats (M2)

- **Q-07** — What does "BPE" in `Scus94491BpeSegment*` denote (a segment/overlay label of the original executable)? And does the decompiled segment code correspond 1:1 with the `SCUS_944.91` overlay bytes extracted as `lod_engine` (slice 0xa00, size 0x36228)? — `Open` — M2/M10 — needs: cross-check of overlay size vs decompiled code extent; original CD file listing if assets become available.
- **Q-08** — What are the byte-level formats of the decompressor and DEFF (two unpacker transformations, `decompressDiscriminator/decompress`, `deffDiscriminator/undeff`), and which files use them? — `Open` — M2 — needs: full read of the Unpacker's decompress/undeff implementations (not examined in R0).
- **Q-09** — What is the MCQ format (`loadMcq(McqHeader, x, y)`, `McqBuilder` in the renderer) — presumed compressed UI text? — `Open` — M2 — needs: read of MCQ loader/builder.
- **Q-10** — What do `CtmdTransformer`, `SubmapPxlTransformer`, and `SaveCardTextureExtractor` each transform, and what are the target formats? — `Open` — M2 — needs: read of the three transformers.
- **Q-11** — Beyond shadow assets and `font.tim`, what else is embedded in the original executable overlay (`lod_engine`)? (R0 verified the four documented extractions; exhaustiveness unknown.) — `Open` — M2 — needs: scan of `lodEngineExtractor` callers and any other slice extractions.
- **Q-12** — What is the exact DRGN script-file addressing (`drgnBinIndex_800bc058`, the `DRGNxx/nnnn/mm` paths, per-disc script indices, `SECT/DRGN0.BIN` root indexing)? — `Open` — M2/M3 — needs: read of Unpacker's DRGN handling + Loader path usage + ScriptManager loading code.
- **Q-13** — What are `patches/replacements/chester.tim`, the `patches/libs/*.txt` lookup tables (`widescreen.full.txt`, `throw_item_effects.txt`, `shadow.txt`), and `patches/libs/lookup_tables.txt` used for, at which stage (unpack vs runtime vs tooling)? — `Open` — M2/M3 — needs: grep for consumers of each file.
- **Q-14** — How does the unpack cache versioning work in detail (current `VERSION = 5`; what bumps it, what is deleted/reused)? — `Open` — M2 — needs: read of `getUnpackVersion/deleteUnpack`.

### Scripting (M3)

- **Q-15** — Semantics of opcode 9 (`SWAP_BROKEN`) and of the `_0`-specialized ops (4, 12, 66): original VM quirks, or toolchain conveniences? — `Open` — M3 — needs: disassembler/assembler handling + any runtime code paths for these opcodes.
- **Q-16** — What does the recompiler `strip` command do (implementation not examined in R0)? — `Open` — M3 — needs: read of `Shell.strip` path.
- **Q-17** — Is the runtime interpreter (`ScriptState`) a faithful model of the original script VM, or a re-derivation? Where might it diverge (e.g., error handling, unknown-opcode behavior — the runtime **throws** per `OpType.byOpcode`)? — `Open` — M3/T-COMPAT — needs: systematic read of `ScriptState.tick` semantics + `NotImplementedException` usages.
- **Q-18** — What are the round-trip stability conditions for decompile→compile, and what invariants do the 300+ shipped script patches depend on? — `Open` — M3 — needs: Patcher/Translator/Assembler analysis + sampling of `patches/scripts/*.diff`.
- **Q-19** — Remote scmeta (`legendofdragoon.org/scmeta/`) vs local `patches/meta/*.csv`: which is authoritative, and are they currently in sync? (Remote not fetched during R0 — no network assumption made.) — `Open` — M3 — needs: fetch + diff if reachable; otherwise document local CSV as baseline and mark remote state `Unresolved`.

### PSX hardware and compatibility (M4)

- **Q-20** — Where does the float-based GTE model diverge observably from fixed-point hardware (rounding/saturation per operation), and does any game behavior depend on specific fixed-point artifacts? — `Open` — M4 — needs: per-operation audit of `Gte.java` against documented PSX GTE behavior (external evidence, cited separately).
- **Q-21** — What are the observable requirements of the ordering table (0x4000 entries, zMax 0x3ffe, bits 14) — sort stability, collision policy, per-frame lifecycle — and does the reference implement PSX-like depth-sort behavior or an approximation? — `Open` — M4/M5 — needs: read of `Gpu` z-queue implementation + usage patterns.
- **Q-22** — SPU fidelity: does the reference's 60 tps voice tick reproduce PSX SPU timing constraints (key on/off alignment, reverb path), or is it a re-timed model? — `Open` — M4 — needs: read of `Spu.tick`/`Voice` + external PSX SPU documentation cross-check.
- **Q-23** — Are the XA ADPCM tables (positive/negative coefficient, step, zigzag) identical to documented PSX XA? (Tables are visible in `XaAdpcm.java`; cross-check pending.) — `Open` — M4 — needs: external cross-reference + real XA sample if available.

### Rendering, audio, saves, modding (M5–M8)

- **Q-24** — Which audio path plays which content: SSSQ sequencer (music), SPU (SFX), Opus-based `XaPlayer` (which XA streams — FMV audio? music fallback?)? — `Open` — M6 — needs: trace of `loadXa`, `loadBackgroundMusic`, FMV playback audio routing.
- **Q-25** — What is the `.dcnf` config file format, and how do global vs per-save configs merge? — `Open` — M7 — needs: read of `ConfigStorage`.
- **Q-26** — What do save versions V1 (`DSav`), V2–V7 (`DSa2`–`DSa7`, "LegacySerializer"), V8, V9 represent historically (previous releases of the project?), and what exactly migrates? — `Open` — M7 — needs: read of the four serializer classes + project history.
- **Q-27** — What is the observable mod contract vs the Java class-loading implementation (URLClassLoader, reflections scanning, thread-local active mod)? (R0 established the mechanism; the contract abstraction is the M8 deliverable.) — `Open` — M8 — needs: systematic read of Mod-Loader + all Mod-Loader call sites in the main repo.
- **Q-28** — What does the debugger subsystem (JavaFX) expose as a development API, and what development workflow does it imply for the decompiled codebase? — `Open` — M8/M10 — needs: read of `legend/game/debugger/*` (22 files).
- **Q-29** — Are there engine-state or save behaviors that depend on JavaFX or other UI-framework components (i.e., hard platform coupling), or is JavaFX cleanly confined to the updater/debugger? — `Open` — M8/M10 — needs: import-graph check of JavaFX usage.

### Game systems (M9)

- **Q-30** — The Dabas "Mogul" machine: is it a disassembly of `DABAS.BIN` under a synthetic memory map (hence the ARMv4-style CPU and 0xA000000+ peripherals), or a fully re-modeled machine? What were the minigame's real execution constraints? — `Open` — M9 — needs: Dig-Dabas git history (dememulation commits), `DabasVars`, and `DABAS.BIN` if obtainable.
- **Q-31** — How do minigame rewards map to host goods (`DabasRewardsEvent`, `DabasScreen` reward handling)? — `Open` — M9 — needs: read of both classes.
- **Q-32** — Where does the host obtain the Dabas `int[][]` tiles passed to `loadGame` (defaults vs saved vs config)? — `Open` — M9 — needs: read of `DabasScreen`/`Dabas.loadGame` path.
- **Q-33** — Which battle/combat behaviors are script-driven vs state-machine-driven, and where do they interleave (the `legend/game/combat` tree is 291 files; only package-level structure was inspected in R0)? — `Open` — M9 — needs: systematic M9 read.

### Tooling and provenance (cross-cutting)

- **Q-34** — What is `lwjglx-debug-1.0.0.jar` (present in both Severed-Chains and Dig-Dabas, ~696 KB)? — `Open` (low priority) — M1/M10 — needs: jar manifest/class listing (non-destructive).
- **Q-35** — Published-artifact provenance: the main repo pins `mod-loader:4.3.5`, `script-recompiler:0.7.11`, `mogul-dabas:0.0.1`; all three match the local checkouts' declared versions (`Verified`). Do the published artifacts' contents equal the local HEADs (i.e., was each version cut from the checked-out revision)? — `Open` (low priority) — M8 — needs: compare against published sources jars or git tags (network-dependent).

## Research Risks

| ID | Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| R-01 | **No ROM/asset access.** `isos/` and `files/` are empty/gitignored in all checkouts; no game assets ship in the repositories. Byte-level format claims (M2, M3, M6) rest on source inspection of the reference decoders only. | Medium — format documents may carry systematic errors invisible to source-only evidence | Certain (current state) | Every byte-level claim cites the decoding source; claims needing real-data confirmation are marked `Inferred`/`Unresolved` and queued here; ask the user (plan question P-02) whether real disc images can be provided for verification |
| R-02 | **Near-zero test coverage in references.** Only the main repo has a boot harness, and it is excluded from builds; nothing validates formats or script round-trips. | Medium — no executable oracle for behavioral claims | High | Multi-call-site corroboration rule in milestone verification criteria; reproducible experiments only where the reference can be run (Java available) without modifying the repos |
| R-03 | **Original-vs-modified boundary is fuzzy.** The decompiled code contains many fixes/enhancements (300+ script patches, unpacker fix transformers, "reference implementation-specific" features) that are textually indistinguishable from original logic. | High — risk of documenting reference behavior as original behavior | High | Layer-tagging rule (original / reference / proposed) enforced in all documents; M2 fix-patch catalog; `patches/scripts.csv` description column used as fix inventory; git history consulted per subsystem |
| R-04 | **External dependencies of the reference ecosystem.** Remote scmeta host, the external meta-scraper repo (`Severed-Chains-Metadata-Utility`), GitHub Packages hosting — any may be unavailable or changed. | Low–Medium — meta documentation (M3) could be based on a stale mirror | Medium | Local `patches/meta/*.csv` is the baseline; remote state recorded only if actually fetched during research, otherwise `Unresolved` |
| R-05 | **Scale of the game layer.** `legend.game` ≈ 1000 files (combat alone 291). M9 coverage gaps are likely. | Medium — incomplete gameplay documentation | Medium | Per-system verification criteria with struct/state/formula inventories; per-milestone exit reviews; sub-milestone approval (plan question P-01) |
| R-06 | **Language-feature conflation.** Java 25 features (virtual threads in the unpacker, switch expressions, records-style patterns) could be mistaken for design requirements. | Low — analysis drift | Medium | Language-agnostic analysis rule in `AGENTS.md`; concurrency is documented as *behavior* (which work happens when, under what guarantees), not as Java idioms |
| R-07 | **Licensing.** All four reference repositories are AGPL-3.0; the new workspace's `LICENSE` is likewise AGPL-3.0 (34,523 bytes, matching). Copyleft obligations for code/assets/derivatives are a project-level decision. | Outside technical research scope; flagged for the user's awareness | n/a | Noted here only; no technical research action |
| R-08 | **Single-maintainer style.** The repositories show a consistent, author-driven style (renames, large refactors, dememulation) — meaning structure may be in flux at any revision. | Low — R0 findings may partially age | Low | Revision pinning in the index; re-verify at milestone entry if a repo's HEAD has moved materially |

## Constraints Assumed for R0 (and carried into the plan)

1. Reference repositories are read-only (per project directive).
2. No network fetches were performed during R0 (remote scmeta, published artifacts, and external PSX documentation are **not** yet part of the evidence base; they will be cited separately from repo evidence when used).
3. The four local checkouts are treated as the authoritative upstream of the pinned Maven artifacts (see Q-35 for the provenance caveat).
4. The proposed six-crate Rust structure is **context only**; no research document is organized by it, and none of the R0 findings are framed as crate assignments.
