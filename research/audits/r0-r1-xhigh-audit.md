# R0/R1 Independent Research Audit (xhigh)

Status: Complete. Date: 2026-11-02. Audited revision: project `d4b5f66` (Phase R1).
Reference repositories verified at pinned revisions and unmodified: Severed-Chains @ `0c08c679b`, Script-Recompiler @ `3433e6f74`, Mod-Loader @ `c69213490`, Dig-Dabas @ `4c1fbfebc`.

Method: every significant claim in the R0/R1 documents was located, its citation inspected, the surrounding implementation and callers read, and the claim re-classified. Strong-word claims (always/never/atomic/race/thread-safe/original/equivalent) received call-site-level verification. This audit independently reconstructed the frame execution sequence, the full `vsyncMode` write-site census, the input dispatch pipeline, the audio thread loops, the state transition machinery, and the mod-framework semantics from source. Findings below are ordered by severity; all corrections listed in §7 were applied to the research documents in the same session and are marked `Corrected`.

## Executive Summary

**Overall assessment: R0/R1 is substantially reliable.** The core architecture is correct: the action-loop frame driver with input-before-render ordering, the complete `gameLoop` phase sequence, the queued frame-top state transitions, the per-state `tickMultiplier` cadence system, the audio thread topology (60 Hz SFX / 120 Hz OPEN_AL / 2 s device poll), the SPU edge-trigger semantics, the input one-frame-`pressed` model, the mod boot re-entrancy, the loading-screen gate, and the crash-recovery path all check out against source. The R0 inventory's concrete facts (1322 main source files, version pins, 21 registries, `lwjglx-debug` jar) verify exactly.

**However, one High-severity and three Medium-severity errors were found and corrected:**

1. **F-01 (High): the per-state frame-rate table is materially wrong.** Submap, world map, credits, and battle-transition contexts run at **60 Hz** (`vsyncMode = 1`) in the ROM code, not the "30 Hz common case" the R1 documents state. 30 Hz occurs only at title, game-over, world-map screens, and as the transient value forced at every state transition; battle runs at 20 Hz. The complete write-site census (19 non-debug sites, all decompiled ROM code) is in F-01. This error propagated to five documents (execution model §5/§9, psx/01 §2.5/§4.2, index summary, register Q-04).
2. **F-02 (Medium): the documented "SPU control-word data race" does not exist.** `Spu.keyOn`/`keyOff` and every other SPU control method are `synchronized(Spu.class)`, the same monitor `Spu.tick()` holds. The "known unsafe corner" was a false alarm built from a misread declaration.
3. **F-03 (Medium): scene teardown is same-frame, not "deferred by one frame".** Objects marked at a frame-top transition are physically deleted in the *same* onDraw's post-frame bookkeeping, after that frame's render pass — not in the next onDraw. The underlying invariant (no mid-frame frees; coalesced fixed-point deletion) is correct; only the quantification was wrong.
4. **F-04 (Medium): mod "active mod" attribution is a process-wide static, not thread-local.**

Twelve additional Low/Info findings (line-reference slips, a mislabeled global symbol, an omitted disc-wait behavior, operator/labeling nits) are listed in the register.

**Suitability for M2: suitable, with the corrections applied** (see M2 Readiness). No finding changes M2's scope (asset pipeline/formats); F-01's corrected rate table becomes the authoritative timing baseline for M4/M5/M9.

## Findings Register

Severity: Critical / High / Medium / Low / Info. Status: `Corrected` = demonstrable error fixed in the research documents this session; `Unresolved` = requires further investigation.

### F-01 — High — Per-state frame-rate table materially wrong

- **Affected documents/sections:** `platform/02-execution-model.md` §5, §9; `psx/01-compatibility-boundaries.md` §2.5, §4.2; `research/README.md` R1 summary items 3 and 9; `04-unresolved-questions.md` Q-04 closure.
- **Original claims:** "30 Hz is the common case (submap/title/world map)"; "20 Hz battle / 30 Hz default / 60 Hz transition"; "LoD's `vsyncMode = 2` halves it [60 Hz NTSC]" (psx/01 §4.2); "Frame rates are per-state and original: 30 Hz default, 20 Hz battle, 60 Hz battle transitions" (index).
- **Source evidence examined:** complete census of every write to `vsyncMode_8007a3b8` (grep over the whole tree), each in context:
  - Default: `Graphics.java:71` = 1.
  - `gameInit` (Scus94491BpeSegment.java:624) and `preload` (:640) → 2; `EngineStates.loadQueuedOverlay` (EngineStates.java:56) → 2 at **every** state transition.
  - **`SMap.tick`, `INIT_0` stage (SMap.java:4088, `@Method(0x800e5914L)`) → 1** — submap exploration runs at 60 Hz (also `SCRIPTS.setFramesPerTick(2)`, `resizeDisplay(384, 240)`).
  - **`WMap.initWmap` (WMap.java:707, `@Method(0x800ccbe0L)`) → 1**; **`WMap.restoreMapOnExitMenu_` (:681, `@Method(0x800cca74L)`) → 1** — world map runs at 60 Hz; `WMap.deallocate` (:1021) → 2, `transitionToEngineState` (:797) → 2, `transitionToTitle` (:830) → 2 — 30 Hz while wmap screens are open / on exit.
  - **`Credits.initCredits` (Credits.java:239, `@Method(0x800eaad4L)`) → 1** — credits at 60 Hz.
  - Battle: `Battle.java:2040` and `:2361` → 3 (20 Hz); `Battle.java:2578` → 2; battle transition `SBtld.java:359` → 1 (60 Hz).
  - Title: `Ttle.java:285, 609, 652` → 2; game over `GameOver.java:145` → 2.
  - Script-writable: game variable 25 (`GameVarParam.java:245`); debug tool `DebuggerController.java:293`.
  - Cross-check: `tickMultiplier()` — default 2 (`EngineState.java:89`, javadoc: "world map was 20FPS, we multiplied it by 3 to bring it to 60FPS"), WMap → 3 (WMap.java:465), Battle → 1 (Battle.java:704). The reference rate equals original rate × multiplier for every state: wmap 20×3=60, submap 30×2=60, battle 20×1=20, credits 30×2=60 — fully consistent with the 60 Hz exploration rates and with the 20 Hz battle staying at 1× (no speed-up).
- **Audit conclusion:** The R1 table was built from the transition/default write sites only and missed the per-state reassignments, inverting the actual structure: the game's *exploration* contexts run at 60 Hz; 30 Hz is the title/menu/transition rate. All sites are decompiled ROM code (`@Method`), so the rates are **Verified original behavior**, not a reference choice. The implied *original* rates (submap 30, wmap 20, battle 20, credits 30 fps) are `Inferred` from the `tickMultiplier` arithmetic.
- **Required correction:** full write-site table in execution model §5; §9 essential list corrected; psx/01 §2.5/§4.2 corrected; index summary corrected; Q-04 closure annotated. **Corrected.**
- **Confidence:** `Verified` for the reference/ROM rates; `Inferred` for the implied original rates.

### F-02 — Medium — "SPU control-word data race" is a false alarm

- **Affected:** `platform/02-execution-model.md` §6; `architecture/03-state-and-ownership.md` §2 (Spu row), §5 (corner 1); `04-unresolved-questions.md` Q-41.
- **Original claim:** "the main thread sets `keyOn`/`keyOff` long words **without a lock or volatile** — an observable data race that works in practice on 64-bit JVMs; `Verified` the declaration".
- **Source evidence:** `Spu.java` — `keyOff` (402-407) and `keyOn` (410-414) are `synchronized(Spu.class)`; `clearKeyOn`, `setMainVolume`, `getMainVolumeLeft/Right`, `setReverbVolume`, `setNoiseMode` (426), `setReverbMode` (434), `setReverb` (442), `mute`/`unmute`, `enableReverb`/`disableReverb`, `setNoiseFrequency` (480) are all `synchronized(Spu.class)`; `tick()` (85) holds the same monitor and reads/clears `keyOn`/`keyOff` under it. The only unsynchronized SPU field write reachable from the main thread is `setPlayerVolume` (line 77, a `float`).
- **Audit conclusion:** there is **no data race** on the control words; the monitor is shared with the model tick. The "unsafe corner" entry is void. The correctly-stated part (edge-trigger semantics: level words OR-accumulated, consumed and cleared once per tick) stands.
- **Required correction:** all three document locations rewritten; Q-41 closed with the correction. **Corrected.**
- **Confidence:** `Verified`.

### F-03 — Medium — Scene teardown is same-frame, not "deferred by one frame"

- **Affected:** `platform/04-engine-states.md` §2; `architecture/03-state-and-ownership.md` §3 (Obj/Texture row), §4 (invariant 4).
- **Original claim:** "actual deletion happens in the **next onDraw** … state teardown is **deferred by one frame**"; invariant 4: "scene teardown is deferred **exactly one frame**".
- **Source evidence:** the transition (marking via `Obj.clearObjList(false)`/`Texture.clearTextureList(false)`, EngineStates.java:77-78) runs inside `gameLoop` phase 4, which runs inside the current onDraw's `renderFrame()` (step 7). `Obj.clearObjList` (Obj.java:49-63) sets `deleted` immediately and logs a `Leaked: <name>` warning per non-persistent object; `Obj.deleteObjects()` (Obj.java:38-47) physically deletes marked objects, and it is called in the **same** onDraw's post-frame bookkeeping (RenderEngine onDraw step 9 — after the render pass). There is no delete pass at onDraw start; the only other delete sites are the pause-resume and frame-advance branches.
- **Audit conclusion:** marked scene objects survive to the end of the *current* frame's onDraw (they may even be rendered once more by that frame's render pass) and are then freed. The invariant's *property* — "never freed mid-frame; freeing is coalesced to a fixed point after the frame's render decisions" — is correct and is what a reimplementation must preserve; only the "one frame" quantification was wrong.
- **Secondary observation (new, `Verified`):** the bulk clear at every transition logs `Leaked: <obj>` for every non-persistent object that exists at transition time — a reference-implementation diagnostic indicating retail states generally leave their objects to the bulk clear rather than deleting them in `destroy()`.
- **Required correction:** three locations reworded to "deferred to the end-of-frame fixed point of the same onDraw (after the render pass)"; leak-diagnostic note added to engine-states §2. **Corrected.**
- **Confidence:** `Verified`.

### F-04 — Medium — "thread-local" active-mod attribution is a process-wide static

- **Affected:** `platform/02-execution-model.md` §8; `architecture/03-state-and-ownership.md` §2 (Mods/Events row).
- **Original claim:** "thread-local 'active mod' attribution during delivery" / "thread-local active mod (main-thread local in practice)".
- **Source evidence:** `ModContainer.java:24-25` — `private static ModManager activeManager; private static Class<?> activeModClass;`, set by `setActiveMod` (:98-101), called from `EventManager.postEvent` for each delivered listener and reset at post-end. Plain static fields — last writer wins, no thread confinement.
- **Audit conclusion:** behaviorally identical in the current design (all event delivery happens on the main thread), but the mechanism as documented is wrong; a reimplementation reading this would expect thread confinement that does not exist.
- **Required correction:** both locations reworded to "process-wide static (last-writer-wins); effectively main-thread-only because all delivery is on the main thread". **Corrected.**
- **Confidence:** `Verified`.

### F-05 — Low — Loop sleep granularity is milliseconds, not microseconds

- **Affected:** `platform/02-execution-model.md` §1.
- **Original claim:** "sleep(max(0, nextAction.nanosUntilNextRun())) // µs-granularity (DebugHelper.sleep)".
- **Source evidence:** `PlatformManager.run` (PlatformManager.java, run loop): `DebugHelper.sleep(Math.max(0, nextAction.nanosUntilNextRun() / 1_000_000))`; `DebugHelper.sleep` (DebugHelper.java:9-13) is `Thread.sleep(millis)`.
- **Audit conclusion:** nanoseconds are converted to **milliseconds** before sleeping. Functionally minor (the Action timer, not the sleep, is the pacing authority; Thread.sleep coarseness applies in any case), but the documented comment is inaccurate. **Corrected.** `Verified`.

### F-06 — Low — Crash-handler global mislabeled; dual-role global undocumented

- **Affected:** `platform/01-application-lifecycle.md` §7 (crash-recovery provenance).
- **Original claim:** "`submapScene_80052c38`/`submapCut_80052c30` are copied into the new `GameState52c` before the save".
- **Source evidence:** `Main.java` crash handler: `gameState_800babc8.submapScene_a4 = collidedPrimitiveIndex_80052c38; gameState_800babc8.submapCut_a8 = submapCut_80052c30;`. No symbol `submapScene_80052c38` exists. The same global `collidedPrimitiveIndex_80052c38` is what SMap's own save path copies (SMap.java:465), and it is initialized from `submapScene_80052c34` on submap entry (SMap.java:499, 509) — it doubles as "collided primitive index" (used for collision/transition lookup, SMap.java:1149, 3685) and as the live current-scene index.
- **Audit conclusion:** the documented *behavior* (scene/cut restored from live globals) is correct; the symbol name was wrong, and the decompiler's dual naming of 0x52c38 is worth recording. **Corrected.** `Verified`.

### F-07 — Low — Line reference for `Scus94491BpeSegment.main()` is off

- **Affected:** `platform/01-application-lifecycle.md` §5 step 2.
- **Evidence:** `main()` is at Scus94491BpeSegment.java:599-602 (`gameInit(); preload();` — behavior verified); 605-608 is inside `gameInit`. **Corrected.** `Verified`.

### F-08 — Low — Error-propagation example cites the wrong exception

- **Affected:** `architecture/01-runtime-subsystems.md` §5.
- **Original claim:** "`NotImplementedException` … native calls that retail never implemented (e.g., `FUN_8001c5fc`)".
- **Evidence:** `FUN_8001c5fc` (Scus94491BpeSegment.java:422) throws a plain `new RuntimeException("Not implemented")`. `NotImplementedException` (legend.game.scripting) does exist and is used elsewhere (e.g., `ScriptReadable` defaults, `scriptReadRegistryEntryVar` at Scus94491BpeSegment.java:507). **Corrected** (example replaced). `Verified`.

### F-09 — Low — Omitted loading-screen behavior: disc-wait loop

- **Affected:** `platform/01-application-lifecycle.md` §5.
- **Evidence:** `Unpacker.unpack` (Unpacker.java:148-190) loops until all **four** ISO readers open: `getIsoReaders` per attempt, per-disk status prompts (`unpacker.disk_help` + per-disk `unpacker.disk_status` shown on the loading screen), a `shouldStop` check (window close), and a 1 s poll. The lifecycle doc described the loading screen but omitted this prominent user-facing gate (the game waits for all four discs to be provided). **Corrected** (added to §5). `Verified`.

### F-10 — Low — `simpleRand` uses `/ 2`, not `>> 1`

- **Affected:** `psx/01-compatibility-boundaries.md` §2.2.
- **Evidence:** Scus94491BpeSegment.java:233 — `return simpleRandSeed_8004dd44 / 2 & 0xffff;`. For negative intermediate seeds `/ 2` (truncation) and `>> 1` (floor) differ by 1 before masking. **Corrected.** `Verified`.

### F-11 — Low — Shader family count

- **Affected:** `platform/01-application-lifecycle.md` §3.12.
- **Evidence:** RenderEngine registers six families: Simple, Copy, Standard, TMD, Battle TMD, Post (screen fragment shader) (RenderEngine.java:130-224). Doc said "five" while listing six names. **Corrected.** `Verified`.

### F-12 — Low — Phase-20 (DISCORD.tick) attribution nuance

- **Affected:** `platform/02-execution-model.md` §3, §9.
- **Evidence:** `DISCORD.tick()` is a statement *inside the decompiled `gameLoop` body* (Scus94491BpeSegment.java:194), so all 21 calls (phases 0-20) are decompiled source order. The *service* is reference architecture (the ROM had no rich presence), but the call site is part of the preserved frame body. The doc's "phase 20 … are reference architecture" is defensible but imprecise. **Corrected** (wording clarified). `Verified`.

### F-13 — Info — `B_PLUS_QUARTER_F` pass reuses the `B_PLUS_F` translucency state

- **Affected:** new note in `platform/02-execution-model.md` §2 step 8; register Q-37.
- **Evidence:** RenderEngine translucent pass (RenderEngine.java ~977): the fourth pass calls `this.api.translucency(Translucency.B_PLUS_F)` and then `entry.render(Translucency.B_PLUS_QUARTER_F, layer)` — i.e., the GL blend state set for the B+F/4 pass is the B+F state (either a reference bug or an intentional alias). Relevant to M4/M5 render-faithfulness work. **Noted.** `Verified` (mechanism); intent `Unresolved`.

### F-14 — Info — `RenderEngine.run()` closes the window on loop failure

- **Evidence:** RenderEngine.java run(): `PLATFORM.run()` is wrapped in try/catch; on `Throwable` it calls `window.close()` and rethrows (the exception propagates to `Main.main`'s crash handler). Not documented anywhere in R1. **Noted** in the exec-model §1. `Verified`.

### F-15 — Info — Phase-count labeling

- **Affected:** `research/README.md` ("20-phase gameLoop").
- **Evidence:** the exec-model table numbers phases 0-20 — 21 calls. **Corrected** (index now says 21). `Verified`.

### F-16 — Info — "60 Hz SPU cadence" layer-1 attribution needs external evidence

- **Affected:** `psx/01-compatibility-boundaries.md` §2.3.
- **Evidence:** the row labels 60 ticks/s as "1 (PSX SPU DMA rhythm convention) / 2 (model)". The repository evidence establishes only the *reference modeling choice* (the notes column already says so). Whether 60 Hz matches PSX SPU hardware convention requires external PSX documentation (M4, network-dependent). **Noted** (layering flagged as pending external corroboration). `Unresolved` for the layer-1 half.

### F-17 — Info — Input action registration is a field initializer

- **Affected:** `platform/02-execution-model.md` §1.
- **Evidence:** `PlatformManager` (PlatformManager.java): `private final Action input = this.addAction(new Action(this::tickInput, 60));` — a field initializer of the abstract base class, running at instance construction, *before* the SdlWindow render action is added. The functional claim (list order ⇒ input tick precedes render tick each iteration) is `Verified` and unchanged; the "PlatformManager's constructor" phrasing was made precise. **Corrected.** `Verified`.

### F-18 — Info — `RenderEngine` "battle colour" hack in the mode-0 pass

- **Evidence:** onDraw mode-0 pass (RenderEngine.java ~690): when the current state is Battle and `battle._800c6930 != null`, the `battle_tmd` shader is bound with a battle colour uniform before the main pass ("Gross hack bro" comment). Not documented in R1; recorded for M5. **Noted.** `Verified`.

## Coverage Matrix

Claims checked and citations verified per document. "Citations verified" = cited symbol/line-range located and content confirmed; remaining unverified areas are stated honestly.

| Document | Sections examined | Claims checked | Citations verified | Findings | Remaining unverified |
|---|---|---|---|---|---|
| `platform/01-application-lifecycle.md` | §1-9 (all) | ~55 | ~45 (GameEngine full file, Main full file, Scus94491BpeSegment main/gameInit/preload, Unpacker stop/disk-wait, Updater.check, AudioThread init/scheduler, SdlWindow ctor) | F-06, F-07, F-09, F-11 | Updater *download* staging internals (separate `UpdaterApplication` app — verified it exists and writes `updater.jar.new`; full protocol is M9); `loadBasicUiTexturesAndSomethingElse` MRG dir 6669 detail; font CLUT rect |
| `platform/02-execution-model.md` | §1-10 (all) | ~80 | ~55 (PlatformManager, Action, SdlWindow.tick/setFpsLimit, RenderEngine onDraw/renderFrame/renderBatch/renderPool/renderPoolTranslucent, Scus94491BpeSegment.gameLoop/bindRendererEvents/simpleRand/rsin, full vsyncMode census, tickMultiplier sites, AudioThread.run, Audio.soundLoop, Spu, Gpu.startFrame/tick) | F-01, F-02, F-04, F-05, F-12, F-13, F-14, F-17, F-18 | `pre()` viewport details; `Gpu.updateVramTexture` internals; per-object GPU-command vs model-pool routing (Q-37, M4/M5); turbo intent (Q-36) |
| `platform/04-engine-states.md` | §1-9 (all) | ~40 | ~35 (EngineStates full file, EngineState, CoreEngineStateTypes, LodEngineStateTypes, SMap/Ttle getRenderMode, all 7 transition call sites, native tables in Scus94491BpeSegment_8004) | F-03 | `lastSavableEngineState` update point (already flagged M7); per-state hook overrides per state (M9) |
| `platform/05-input-and-platform.md` | §1-7 (all) | ~45 | ~40 (SdlPlatformManager full tickInput + clearPressed + triggerBinding* + AxisInputState + isAction* queries, InputActionState full, InputBindings.initBindings/loadBindings, PlatformManager base) | minor: "repeats (focused window only)" should include the `RECEIVE_INPUT_ON_INACTIVE_WINDOW_CONFIG` override (qualifying note added); SdlWindow min-size line ref (141→137) | Retail action inventory (M9); original pad-bit layout (M3/M9) |
| `architecture/01-runtime-subsystems.md` | §1-8 (all) | ~35 | ~20 (Registries 21 entries, ModManager/EventManager cross-repo, Async pool, version pins, 1322 file count) | F-08 | Per-subsystem behavior (deferred milestones, as designed) |
| `architecture/02-subsystem-interactions.md` | §1-7 (all) | ~35 | ~25 (per-frame sequence re-derived from source; event table re-checked against postEvent sites; cross-thread matrix against call sites) | none (consistent with corrected F-02/F-04) | Script-native surface depth (M3); batch/pool internals (M5); sequencer protocol (M6) |
| `architecture/03-state-and-ownership.md` | §1-6 (all) | ~40 | ~25 (GameState52c field table spot-checks, Spu full sync surface, AudioThread locks, Obj/Texture deletion protocol, ModContainer statics) | F-02, F-03, F-04 | Full field-level global audit (M10, as designed); registry mutation windows (M8) |
| `psx/01-compatibility-boundaries.md` | §1-5 (all) | ~40 | ~20 (Gpu VRAM/ordering-table constants, Gte JOML register file, Spu/XaAdpcm self-containment, MathHelper/rsin, Bpp, gameInit ordering-table config) | F-01 (§2.5/§4.2), F-10, F-16 | Per-constant byte-level classification (M4); reverb network constants vs hardware (Q-22); XA tables vs PSX spec (Q-23) |
| `01-repository-inventory.md` (R0) | spot-check | ~15 | 13 (file counts, version pins, lwjglx-debug jar, dependency declarations) | none | Build/CI details (re-verified only as far as R1 depends on them) |
| `02-cross-repository-map.md` (R0) | spot-check | ~12 | 10 (mod framework integration, version pinning, Dig-Dabas embedding) | none | Dig-Dabas "Mogul" machine provenance (Q-30, M9) |
| `03-research-plan.md` | M1 completion status + phase alignment | ~8 | 8 | none | — |
| `04-unresolved-questions.md` | R1 closures (Q-01..Q-05, Q-36..Q-43) | ~15 | 12 | F-01 (Q-04 closure annotated), F-02 (Q-41 corrected) | the open questions themselves remain open by definition |

**Honesty note:** the claims checked are the significant technical claims — frame ordering, timing, synchronization, lifecycle gates, state machine, input semantics, and every strong-word statement. Exhaustive line-level verification of *every* sentence in the 13 documents was not performed; unverified residue is listed per row above and is largely work already assigned to M2-M9 by the plan.

## Architectural Risk Assessment

Mistakes in R0/R1 that could materially affect the Rust implementation, in impact order:

1. **Timing baseline (F-01, now corrected).** Had it stood, a Rust reimplementation targeted at "30 Hz default" would have run field exploration and the world map at half the reference's rate, doubling perceived animation/text cadence differences and breaking any `tickCount`-normalized logic against the reference. The corrected table (exploration 60 Hz, battle 20 Hz, title/menus 30 Hz, per-context 60 Hz transitions) is now the baseline. Residual risk: scripts can write `vsyncMode` (game var 25) at runtime; M9 must inventory any retail script that changes rates.
2. **Scene-teardown fixed point (F-03, now corrected).** The property "no mid-frame frees; one coalesced deletion point after the frame's render decisions" is the load-bearing invariant and was stated correctly in invariant 2; the wrong "one frame" quantification could have led to a design where transitioned objects survive an extra rendered frame (visible pop artifacts) or die too early (use-after-free in the render pass). The leak-diagnostic note (retail states rely on bulk clear, `destroy()` rarely cleans) is also an implementation-relevant behavior: a faithful port must keep the bulk-clear-at-transition, not per-state cleanup.
3. **SPU synchronization (F-02, now corrected).** The false "data race" could have justified (incorrectly) a lock-free handoff design; the verified design is a single monitor shared between control and tick, with per-tick edge-word consumption. The edge semantics (accumulate, consume once, clear) remain the essential contract.
4. **Compatibility boundary integrity (psx/01).** The four-layer method is sound and the candidate pool is correctly framed as candidates. Residual risks flagged: (a) layer-1 attributions that need external corroboration (60 Hz SPU cadence — F-16); (b) "self-contained" assessments are `Inferred` (full independence audit deferred to M4/M10); (c) the GPU-model-vs-renderer conflation warning (Q-37) is the single most likely source of future misjudgment — the mode-0 path interleaves both, and F-18 (battle colour hack) is another concrete instance of renderer-layer code keyed to game state.
5. **Determinism/serialization (not an error, a gap).** R1 documents the ordering and ownership invariants but does not address: RNG determinism across states (`srand` is seeded from `System.nanoTime()` in SMap INIT_0 — a *reference* choice replacing an original ROM seed source; `simpleRand` is vsync-seeded), and save-state serialization of frame-bound state (which globals are captured in `GameState52c` vs recomputed). These are M3/M7/M10 items, correctly assigned, but flagged here as the most likely future sources of subtle incompatibility.
6. **No critical (show-stopper) findings.** No R0/R1 claim was found that would invalidate the plan's structure, the milestone ordering, or the evidence discipline.

## M2 Readiness

**Recommendation: APPROVED.**

Concrete basis:

- The single High-severity error (F-01) and all three Medium-severity errors (F-02–F-04) are **corrected in the research documents in this session**, with the corrections listed above and the documents re-run through the file-ending audit. No demonstrable error remains open.
- No finding changes M2's scope. M2 (ISO/DRGN/MRG file tree, unpacker pipeline, original-executable overlay, byte-level format documentation) depends on the *asset pipeline and file-access* research, which this audit verified where it overlaps M1 (the `files/` two-stage model, async `Loader`/`DrgnFiles` handoffs, `Unpacker` startup behavior — including the disc-wait loop now documented, F-09). The corrected rate table (F-01) does not interact with M2 formats.
- Residual uncertainty is all *by-design deferral*: byte-level format claims await real ROM data (P-02 still open — disc images not yet provided), and per-constant PSX classification awaits M4 + external documentation. Neither blocks M2's start; both are tracked in the register.
- Conditions carried into M2 (not blockers): (1) the F-01 write-site table in `platform/02-execution-model.md` §5 is the authoritative timing baseline for all downstream milestones; (2) any M2 document claiming PSX-hardware (layer-1) behavior must keep the repository-evidence/external-evidence separation introduced in F-16.

## Corrections Applied (this session)

| File | Corrections |
|---|---|
| `research/platform/02-execution-model.md` | F-01 (§5 rate table rewritten with full write-site census; §9 essential list corrected), F-02 (§6 race claim removed), F-04 (§8 static attribution), F-05 (§1 ms granularity), F-12 (§3/§9 phase-20 wording), F-13 (§2 step 8 + §10 note), F-14 (§1 run() failure note), F-17 (§1 field-initializer precision) |
| `research/platform/01-application-lifecycle.md` | F-06 (§7 symbol + dual-role note), F-07 (§5 main() line ref), F-09 (§5 disc-wait loop), F-11 (§3.12 six shader families) |
| `research/platform/04-engine-states.md` | F-03 (§2 same-frame fixed point + leak diagnostic), input-doc qualifying note |
| `research/architecture/03-state-and-ownership.md` | F-02 (§2 Spu row, §5 corner removed), F-03 (§3 row, §4 invariant 4), F-04 (§2 Mods/Events row) |
| `research/architecture/01-runtime-subsystems.md` | F-08 (§5 example) |
| `research/psx/01-compatibility-boundaries.md` | F-01 (§2.5, §4.2), F-10 (§2.2 `/ 2`), F-16 (§2.3 layering flag) |
| `research/README.md` | F-01 (R1 summary items 3/9), F-15 (21 phases) |
| `research/04-unresolved-questions.md` | Q-04 closure annotated with corrected census; Q-41 corrected and re-closed; Q-37 extended with F-13 note; F-16 noted under Q-22 |

Audit trail: all corrections preserve the original claims' source references where the claim was right, and replace only the unsupported portion. No document was restructured.
