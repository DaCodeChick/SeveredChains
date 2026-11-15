# Implementation-Readiness Notes (M10)

Status: Complete (M10, 2026-11-14). This is an **evidence-backed readiness assessment, explicitly not a design proposal**: it names what the M1–M9 research has established, where the evidence is strong or thin, which architectural limitations of the reference would constrain *any* faithful reimplementation versus which are Java-specific and avoidable, and what remains open (and exactly what evidence would close it). No Rust crate, API, dependency, or architectural decisions are made; the future crate table in the project brief is context only. Cross-references: the architecture synthesis is `research/cross/01-architecture-review.md`; the consolidated constraint set is `research/cross/02-compatibility-checklist.md`; the full open-item re-triage is the "M10 Re-triage" section of `research/04-unresolved-questions.md`.

## 1. Purpose and method

The M10 research questions that drive this document (plan M10 RQ3–RQ5):

- **RQ3** — which reference limitations are *architectural* (would constrain any faithful reimplementation) vs *language-specific* (Java/GC/classloading) and thus avoidable? → §3–§4
- **RQ4** — what is the consolidated compatibility checklist? → delivered as `cross/02` (109 rows + 10 decision items)
- **RQ5** — what remains unresolved after M1–M9, and what are the concrete research tasks (and asset needs) to close them? → §2, §5, §6, the register re-triage

Method: no new subsystem research was undertaken for this document (the mission's bounded-verification exception applied to the four register-adjudication checks in cross/01 §9). Every statement here is either (a) a direct synthesis of a cited M1–M9 finding, or (b) an explicit assessment (`Inferred`) labeled as such.

## 2. Evidence baseline: what M1–M9 has established

### 2.1 Corpus and verification track record

| Milestone | Deliverables | Verification performed | Residual posture |
|---|---|---|---|
| R0/R1 | 3 repo docs + 8 platform/architecture/psx docs | **Independent xhigh audit** (`audits/r0-r1-xhigh-audit.md`): 18 findings, all remediated (incl. the F-01 frame-rate misread, F-02 SPU-lock misread, F-03 teardown timing) | Strong for platform mechanics; the "30 Hz common case" correction is canonical |
| M2 | 10 asset docs | Per-checkpoint source verification; the MRG nibble-plane and SKIPS-map claims corrected by M6-03 (Q-47 closed) | Format layouts source-verified / format-corroborated; **no byte validated against retail** (R-01) |
| M3 | 6 scripting docs | **Independent xhigh audit** (`audits/m3-xhigh-audit.md`): 2 High findings **confirmed experimentally** and remediated (round-trip edge cases) | The binary format and VM contract are the best-evidenced subsystem; C-01 opcode discrepancy preserved |
| M4 | 5 PSX docs | Per-constant classification (the 43-row table); all layer-1 attributions explicitly `Inferred` (no external PSX docs consulted — network-restricted) | Behavioral models strong; hardware attributions pending the Q-20..Q-23/Q-65..Q-67 worklist |
| M5 | 4 rendering docs | G1–G15 carry-over dispositions (rendering/01 §10); the same-z worked trace (rendering/01 §11) | The mode-0 modern pipeline fully traced; pixel-exactness vs PSX unverified (no baseline) |
| M6 | 3 audio docs | The S3 voice-allocation correction (M4-03 was wrong: allocation is dynamic); Q-68(b) resolved in the reference; Q-48 closed; Q-74 opened | The clock-domain model is settled; the SSSQ tempo-unit conflict is documented, not resolved |
| M7 | 2 save/config docs | The 11-magic loadable-without-modification table; the 88-entry config census (superseding the plan's 90) | The compatibility floor (RETAIL layout) is source-verified from the parser; retail bytes asset-pending |
| M8 | 3 modding docs | The 21-registry/113-event-class/1238-entry censuses (1238 = M10 census correction); the J-tables separating contract from Java mechanism | The modding contract is fully enumerated; no mod jar or campaign file was available (all claims from the pinned reference source) |
| M9 | 7 game docs | **Adversarial verification per checkpoint**: M9-05c (all corrections committed), M9-06 (C12/C23/C32/C33/C15 + others), M9-07 (~25 specific claims checked, 9 factual errors caught and corrected), the bounded reconciliation (six interface pairs, 13 in-place corrections, 59-site `rand()` census, 148-entry catalog, pin advanced to `71ce130`) | The RQ7 spectrum is consolidated; the per-system retail-vs-reference tables are the row-level authority |
| M10 | 3 cross docs + register re-triage | Bounded verifications (cross/01 §9); the 10-point adversarial cross-document test (plan log, 2026-11-14) | This document |

**Confidence distribution (assessment, `Inferred`):** the corpus is dominated by `source-verified` (in-repo, against pinned revisions) and `format-corroborated` findings; a substantial minority of layer-1 (PSX hardware) attributions are `Inferred`; the `Unresolved` set is small, named, and evidence-specified (10 decision items in cross/02 §10 plus the register cohorts). **Zero `asset-validated` claims** — the single most important structural fact about the evidence base.

### 2.2 Provenance note (preserved per the reconciliation)

M1–M9-06 findings were verified against the primary reference at `0c08c679b`; M9-07 and the M10 spot-checks were verified against `71ce130364ed00431d420891370d5314a4e3d4c4`. The Phase-A delta census (`game/07` §12.1: 18 files, +614/−32, entirely L4 UI — the quantity boxes, sort/filter/unequip, ∞ glyph, `INVENTORY_SIZE_CONFIG` wiring; no `@Method` address or retail-data changes) confirms no prior finding is invalidated. Both pins must be carried into any implementation work's evidence records.

## 3. Architectural limitations (constrain any faithful reimplementation)

Plan RQ3, first half. These are properties of the *original's* architecture or of the reference's *observable contract* that survive any language choice:

| # | Limitation | Evidence | Consequence for any faithful port |
|---|---|---|---|
| A1 | **Global mutable state is the shared surface.** Campaign state lives in the `GameState52c` struct (800babc8 - campaign game state) plus per-segment-class globals (the cross/01 §4.2 inventory); ownership is *conventional* (single main-thread writer), not enforced by any mechanism in the decompiled contract | architecture/03; cross/01 §4 | The port must reproduce the same shared state surface (same fields, same semantics); the single-writer convention is a *design freedom* the port may enforce structurally (the reference cannot) — the observable behavior is identical either way |
| A2 | **The segment-class monolith is the original's memory map.** `Scus94491BpeSegment` (+ `_8004`/`_800b` split in the reference) holds the ROM-anchored globals: game state, the script native table (8004e29c - native script function table), the RNG seeds (8004dd44 - `simpleRand` seed), the state queue (8004dd04/8004dd24/8004dd28), the encounter accumulator (800c6ae8 - encounter accumulator) | cross/01 §4.2; the @Method/@Global anchors throughout M1/M4/M9 | The addresses/offsets are *semantically load-bearing* (scripts and saves address this state; the semantic-naming policy keeps them in layout docs, not in identifier names). A port must treat them as the structure's documented layout |
| A3 | **Single-core frame-locked simulation, real-time audio.** The entire game tick (input → state → scripts → GPU model) runs on one thread at the per-context rate; audio runs on independent real-time clocks communicating only through queues and control words | platform/02 §3/§6; cross/01 §5 | A faithful port must keep one owner for the frame loop and the audio decoupling (CL-ENG-01/10); parallelizing the simulation is not a faithful option (it would change `simpleRand` stream ordering — the RNG streams are call-order-dependent) |
| A4 | **The Fate VM is a core system, not an optional layer.** 108 script slots (0–8 reserved), the 1024-entry native table with per-state reinstall at transitions, the 592-slot catalog, the 63-slot dual int/float storage, the fork/consume/deallocate lifecycle | scripting/05; CL-SCR-05..09 | Submap, battle, and (through the thin edge) every other system *are* their scripts; the VM contract is on the critical path of gameplay fidelity |
| A5 | **The modding surface is observable, not cosmetic.** Saves store registry entry ids (M7); 1238 retail content entries are mod-registered (M8 D20); the 113-event-class contract and the one-shot Register windows are user-visible behavior (crash vs toast, per CL-MOD-05) | modding/01–03; cross/01 §6.7 | A port that hardcodes retail content into the engine *shrinks the observable mod surface* — it is a compatibility break for the ecosystem, not just a design choice |
| A6 | **The unpack-time transform pipeline defines the observable asset contract.** The game reads the *unpacked* `files/` tree; transforms (BPE, DEFF, CTMD, PXL, MIDI→SSSQ, script patches) run before boot; the script-patch observable unit is the unpacked script tree | assets/03; modding/03 D21 | A port must produce equivalent unpacked bytes (formats CL-AST-02..10) — it need not replicate the patching machinery, but it must reproduce the *results* |
| A7 | **The save compatibility floor is the RETAIL layout + the 11-magic load table.** All 11 magics must load without modification; V10 is the writer; the retail 0x6E4 layout with the struct at 0x1FC is the floor (CL-SAV-01/02) | saves/01 §3/§4/§13 | Version readers are a compatibility obligation; the Tag/JSON conveniences are not |
| A8 | **RNG stream behavior is part of the observable contract.** Three distinct generators with distinct roles (CL-GLP-02); the gameplay stream (`simpleRand`, initial seed 3) is deterministic across runs in the reference; the `rand()` streams reset to a host-clock value at every submap entry (non-reproducible *by construction*); the battle MT is script-seeded (`scriptSetMtSeed` — the retail caller unknown, Q-97) | game/07 I38; cross/01 §4.7 | The port must reproduce the *algorithms and roles* exactly; the seed-source decision (§7) is the one point where the port must consciously choose — and document — a behavior |
| A9 | **Frame presentation ordering is a visual-comparison contract.** Swap-before-onDraw + the GPU-model display buffer + the FMV tick cadence compose a specific frame-alignment (G13/CL-ENG-13) | rendering/01 §2.4; rendering/04 §4.5 | Any visual regression test against the original must model this ordering, or it will be off by a frame |
| A10 | **State-transition atomicity at the frame top** (queued → applied next frame; old destroyed before new inits; native-table reinstall; end-of-frame teardown coalesced to the same onDraw) | platform/04 §2–§3; audit F-03; CL-ENG-04/05 | Mid-frame transitions are not faithful; the queue-and-apply pattern is a behavioral requirement |
| A11 | **Dabas is a self-contained machine island.** The dememulated `DABAS.BIN` runs under the binary's own memory map (virtual = 0x2000000 + ROM offset) with its own timers/RTC/DAC; the host integration is 11-stage screen machine + 5 input actions + the mutable rewards event | game/01 | A port must either run the dememulation or byte-match its behavior; the host contract (the 0x260-byte save tail, the 192-boundary encoding) is fixed |
| A12 | **The per-context timing structure** (60 Hz submap/wmap/credits/battle-transition, 20 Hz battle, 30 Hz title/game-over/wmap-screens; 1/60 s game-time unit; frame-locked not fixed-timestep; speed 1..16) | platform/02 §5; CL-ENG-02/03 | The write-site census is the contract; a port may change the pacing *mechanism* (host loop) but not the rates, the unit, or the frame-locking |
| A13 | **Bug preservation is the default** (CL-GLP-16): every documented retail bug/quirk is a preserved-behavior constraint unless the per-system tables mark a reference correction | the F/T/B/U/C/I tables; cross/02 §9 | The "mathematically cleaner" alternative is a compatibility break by the project's own convention (discrepancies are data) |

## 4. Language-specific limitations (Java-specific; avoidable)

Plan RQ3, second half. Each row: the Java mechanism, the observable contract it carries (which is preserved), and why the mechanism is incidental.

| # | Java mechanism (reference) | Observable contract it carries | Why incidental |
|---|---|---|---|
| L1 | Static-field singletons with class-load construction order (MODS → EVENTS → REGISTRIES → … → the static block) | A fixed service set with defined construction/init order (CL-ENG-08) | The *order* is the contract; static-initializer ordering is a Java idiom (the reference's own J-tables classify it as implementation) |
| L2 | URLClassLoader + Reflections `@Mod` scan for mod discovery | Discovery semantics: jar scan, `@Mod{id,version}` semver gate, required mods, failure recording (CL-MOD-01) | modding/01 J1–J4; a different discovery mechanism with the same semantics is observationally identical |
| L3 | `CompletableFuture` / virtual threads for async loads | The null-on-failure load contract + the `scriptWaitForFilesToLoad` wait primitive (CL-AST-11) | Q-43 (closed): the contract is the *completion semantics*, not the future |
| L4 | Exception-based control flow + the crash-everything gameplay model | The layered error model: crash-on-gameplay-Throwable (with the recovery save), recoverable infrastructure failures, the critical-event set (CL-ENG-12, CL-MOD-05) | The J-tables document the error *classes* as contract, the `throw` mechanics as implementation |
| L5 | `Properties` (config.conf) for the global store | The flat global store is itself layer-4 reference (saves/02 D1); the typed entry model + 3 locations are the contract | The file format of `config.conf` is a convenience, not an interface |
| L6 | Process-wide active-mod statics (thread-affinity *assumed*, not enforced — audit F-04) | Per-handler mod attribution + the 7-s toast naming the mod (CL-MOD-05) | modding/02 D13: a per-dispatch-context attribution is observationally identical in the reference's single-threaded delivery |
| L7 | `synchronized` monitors / Object monitors (VRAM `vramLock`, `Spu.class`, audio control, event listeners) | The narrow locked surfaces + the single-writer invariants (cross/01 §5.4) | Any locking with the same *surface* preserves the contract; the monitor is the idiom |
| L8 | JavaFX (debugger + updater UI) | Tooling only — the 25-file import boundary (21 debugger + 2 updater + 2 integration points: the `RenderEngine` pause/frame-advance flags and the close-handler `Platform.exit`); no engine-state or save behavior depends on it | cross/01 §9 check 3 (M10); CL-MOD-11 |
| L9 | JVM GC + object identity (script objects, renderables, pools) | The frame-bound lifetime *protocol* (mark at transition, delete at the end-of-frame fixed point — CL-ENG-05) | The reclamation mechanism is invisible; the timing of the *deletion* is the contract |
| L10 | Weak-reference delegate maps | No observable effect while delegates are statically held (the in-repo pattern) | modding/02 D12: relevant only to transient delegates (none in-repo) |
| L11 | `System.nanoTime()` as the `rand()` seed argument | **Not a contract** — the decompilation did not preserve the original's argument (Q-70); the host-clock choice is the reference's placeholder | psx/05 R4; the seed source is an open decision (§7), not a Java artifact to preserve |
| L12 | The `updater.jar` self-update + Discord rich presence | No game-behavior dependency (the updater runs pre-boot; rich presence is a main-thread housekeeping tick) | platform/01 §3.1/§7 |

**The RQ3 answer (synthesis):** the reference's *behavioral* architecture (A1–A13) is what a faithful reimplementation inherits; its *Java* architecture (L1–L12) is a hosting choice the port is free to make differently, provided the named observable contracts survive. The two lists together are the boundary the future design work must draw against — no design is made here.

## 5. What blocks implementation: the asset-dependent cohort

The single largest readiness gap is structural, not research quality: **no retail disc images, extracted proprietary assets, retail script disassembly, or real save/memcard files were available at any point in M1–M10** (register R-01). Every byte-level format claim therefore rests on the reference decoders' source (source-verified / format-corroborated) and cannot be upgraded to asset-validated without assets. The affected register items, by asset type (full detail in the re-triage):

| Asset type | Register items (representative) | What it would close |
|---|---|---|
| Retail disc images (`isos/`, user-provided per plan P-02) | Q-44–Q-58 (M2 byte-level: MRG/DEFF/TMD/TIM/ANIM/MCQ sector details), Q-59–Q-64 (M5 visual baseline: same-z census, render-path faithfulness, UI VRAM partition), Q-73 (FMV frame headers), Q-79 (retail 18900 Hz FMV coding), the R-01 standing risk | Upgrades source-verified format claims to asset-validated; enables the Q-65/G4/G11/G6 asset censuses |
| Retail script disassembly (the shipped `STR/*.TXT`/`.BIN` disassembled) | Q-81 (retail d-attack DEFF internals), Q-89–Q-98 (M9-07 retail-side: item-use scripts, the 0x800 marker bit, instakill flag consumer, shop prices, multi-item sell, unused ID slots, `equipableFlags_03`, `GoodsSource`, the MT seed's retail caller), Q-97, Q-82–Q-88 (retail-side character questions), Q-74 (a retail SSSQ tempo value) | Closes the "retail side" of every M9 retail-vs-reference table row; settles the F-dragooninit question |
| Real PSX memcard dumps | Q-80 (the 0x20000 file shape: raw MCr or tool repack) | Upgrades the memcard-import handling to asset-validated |
| Retail SSSQ/SSHD bytes + audible duration | Q-74 (the tempo-unit conflict: three in-repo readings disagree) | Resolves the SFX tick-quantization mapping |
| Retail save files (beyond the reference's own parsers) | The D1 row's retail-byte half; the save round-trip edge cases | Confirms the retail layout against original bytes |

**Readiness consequence:** the Rust project can *begin* implementation of the strong-evidence core (frame loop, state machine, script VM, save readers, the M9 rule sets) without assets, but (a) no format reader may be declared *validated* until byte-validated, (b) the 10 decision items in cross/02 §10 that need asset/external evidence cannot be closed by implementation work, and (c) plan P-02 (whether `isos/` access is expected) is the gating user decision.

## 6. What blocks implementation: open research items (asset-independent)

The named, evidence-specified gaps that do **not** need retail assets:

| # | Item | Exact evidence needed | Class |
|---|---|---|---|
| 1 | **Q-06 — per-phase frame invariants** (what each `gameLoop` phase may mutate): the *ordering* is fully verified (cross/01 §5.3); the systematic call-site invariant audit (which phase writes which global, and whether any write escapes its expected phase) is the largest remaining M10-scale research gap | A call-site census of the 21 phases against the cross/01 §4.2 global inventory | research task (in-repo) |
| 2 | **The external PSX documentation worklist** (Q-20/Q-21/Q-22/Q-23, Q-65/Q-66/Q-67, Q-68, Q-69): every M4 layer-1 attribution is `Inferred` because no public PSX documentation was consulted (network-restricted environment) | Admission of external PSX GPU/GTE/SPU/XA documentation under the project's evidence rules (a separate, user-approved evidence-admission pass) | external evidence (no assets) |
| 3 | **Q-17 — runtime-VM fidelity vs the original MIPS VM**: the reference-specific layer is enumerated (scripting/05 §12); per-op parity is unverifiable without the original binary | Original-binary disassembly (asset-type, but a *different* asset than disc images — the decomposed `SCUS_944.91`) | asset-adjacent |
| 4 | **Q-36/Q-37, Q-71, Q-72, Q-73 — the visual-evidence cohort** (turbo residual frames, render-path faithfulness, same-z order observation, equal-depth 2D order, UI VRAM partition, original FMV presentation): these need *observed original behavior* (playthrough capture or hardware), not just source | Original-behavior observation + the G6/G11/G4 censuses | external observation |
| 5 | **Q-70 — the seed-source decision**: not research but a *design decision* with research inputs (§7); it must be made and documented *before* the RNG surfaces are implemented | A documented choice with the reproducibility consequences stated | design decision (pre-implementation) |

## 7. The RNG seed-source question (Q-70): options, not a decision

The reference's three seed surfaces today (verified facts, CL-GLP-02):

1. **`simpleRand`** — seed 3 at process start; no reachable re-seed (the low-12-bit guard sources a layer-4 vsync stand-in but is practically unreachable). The gameplay stream is therefore **deterministic across runs in the reference** (given identical input sequences).
2. **`rand()`** — re-seeded to `System.nanoTime()` at every submap entry (`SMap:4091`, reference-authored placeholder). 59 presentation/cosmetic call sites; **non-reproducible across runs by construction**.
3. **The battle MT** — seeded only by `scriptSetMtSeed` (a registered native; the retail script caller is unknown — Q-97; the L4 replacement scripts' behavior is what the reference exhibits).

Options a reimplementation could take (assessment only — **no decision is made in M10**):

- **Option A — mirror the reference exactly** (host-clock seed at submap entry). Maximizes reference fidelity; preserves the reference's non-reproducibility; no documentation burden beyond "same as reference."
- **Option B — deterministic campaign-derived seed** (e.g., derived from the save/campaign identity at submap entry). Reproducible per campaign; diverges from the reference's observable behavior at the 59 presentation sites (imperceptible in practice — those sites are cosmetic); requires the derivation rule to be documented as a deliberate divergence.
- **Option C — fixed seed + user-configurable override.** Maximally reproducible (regression testing); diverges from both the reference and (presumably) the original; the configuration surface is a new one (no retail counterpart).
- **Option D — PSX-VBLANK-faithful stand-in** (a frame-counter seed at submap entry). The most *layer-1-plausible* reconstruction (the original evidently seeded at submap load from something the decompilation did not preserve); unverifiable against the original without Q-70(a) evidence; must be labeled `Proposed` forever unless the evidence arrives.

The decision's blast radius is small and known (presentation rolls only — gameplay determinism rides `simpleRand`, which is already deterministic); that is precisely why it can be deferred to the design phase *with* the requirement that it be documented there. The M4-05 R4 row carries the High risk flag *for reproducibility*, not for gameplay.

## 8. The compatibility decision points (from cross/02 §10)

The 10 rows that must be *decided* (with evidence) before or during implementation — summarized from cross/02 §10 (the row-level authority): G6 same-z order (Q-66), the R4 seed source (Q-70, §7 above), T2 GTE fixed-point (Q-20/Q-67), G4 the 384→368 width (Q-65/Q-37), G11 TPage rate-00 (Q-65), S7 sweep volumes (Q-68a), X6 the predictor-state continuity (Q-69a), the C-01 opcode discrepancy (per-opcode), Q-74 the SSSQ tempo unit, Q-80 the memcard file shape. Of these: **3 are design decisions** (R4/Q-70, and the two portability choices G6/X6 where "preserve the reference's behavior and document the divergence" is a defensible default), **2 need external documentation** (T2, G4/G11 partially), **5 need asset/observation evidence** (the rest). None of them blocks *starting* implementation; each blocks *declaring done* the subsystem it touches.

## 9. Per-domain readiness assessment (informational)

Assessment scale: **Ready** (strong evidence, no unresolved core question) / **Ready with open questions** (core behavior settled; named items must be decided before the domain is declared complete) / **Blocked** (cannot be declared validated without external evidence/assets).

| Domain | Assessment | Rationale (cross-refs) |
|---|---|---|
| Frame loop & lifecycle | **Ready** | The 14-step boot chain, the 21-phase frame contract, the shutdown order, and the two cross-thread handoffs are fully cited (cross/01 §2/§5; CL-ENG-01..13); audit-remediated |
| Engine state machine | **Ready** | The queued-transition contract, the teardown fixed point, the render-mode table, the 7-state machine + transition graph (cross/01 §2.3–§2.4) |
| Scripting (VM + toolchain + catalog) | **Ready** (C-01 per-opcode choices excepted) | The best-evidenced subsystem (M3 xhigh audit, 2 High experimentally confirmed and remediated; CL-SCR-01..13); the round-trip guarantees are the toolchain's foundation |
| PSX platform models (GPU/GTE/SPU/XA) | **Ready with open questions** | The 43-row classification is complete; the G6/T2/G4/G11/S7/X6 rows are the named decision items (§8); every layer-1 attribution awaits the external worklist (§6.2) |
| Rendering (modern pipeline + TMD path) | **Ready with open questions** | G1–G15 dispositions delivered (rendering/01 §10); pixel-exactness vs PSX is unverified by construction (no baseline) — Q-65/Q-37 |
| Assets (formats + unpack) | **Ready to implement; blocked to validate** | Layouts source-verified (CL-AST-01..12); byte-validation needs disc images (§5) |
| Audio | **Ready with open questions** | Clock domains settled; the SSSQ tempo unit (Q-74) and the sweep gap (S7) are the named items; the music engine's non-sample-identity is explicitly documented (CL-AUD-07) |
| Saves & config | **Ready** | The 11-magic table, the retail floor, the config model (CL-SAV-01..15); retail-byte confirmation pending (non-blocking) |
| Modding | **Ready** | The full contract enumerated (CL-MOD-01..12); the J-tables name what is incidental |
| Game systems (M9) | **Ready with open questions** | The rule sets are the strongest part of the corpus (adversarially verified, reconciled); the retail-side questions (Q-81..Q-98) and the F-dragooninit/U35 items await retail script disassembly; the RQ7 spectrum is the placement contract for any ported behavior |
| Dabas | **Ready** | The dememulation + the machine interface + the rules + the save contract are fully documented (game/01); Q-30 closed |
| Visual parity vs the original | **Blocked** | The entire cohort (Q-36/Q-37/Q-65/Q-66/Q-71/Q-72/Q-73) needs original-behavior observation; this is the one domain where M1–M9 evidence *cannot* close the loop without new evidence types |

**Overall (assessment, `Inferred`):** the research foundation is **implementation-ready for the strong-evidence core** — the frame/lifecycle/state-machine/scripting/save/modding/game-rule subsystems have cited, adversarially verified, cross-consistent contracts. The four conditions that attach to that readiness: (1) the asset-dependent items stay *unvalidated* until assets exist (no claim upgrade without evidence); (2) the 10 decision items are decided (with their evidence or documented divergence) before the touching subsystem is declared complete; (3) the Q-70 seed-source decision is documented in the design phase; (4) the external-documentation worklist is admitted as a separate evidence pass, not silently folded in.

## 10. Pre-implementation verification steps (not design)

Concrete, in-order, evidence-producing steps for the implementation phase to take before or alongside the first code (none of these is a design decision):

1. **Admit the external PSX documentation worklist** (Q-20..Q-23, Q-65..Q-69) as a separate, user-approved evidence pass; re-classify the M4 layer-1 rows against it (the M4-05 table is the re-verification checklist — psx/05 §5).
2. **Decide the 10 compatibility decision items** (cross/02 §10) with the evidence or a documented divergence note per item; record each decision in the register with its class change.
3. **Obtain the asset types in §5** (disc images per plan P-02; retail script disassembly; a memcard dump; retail SSSQ + audible duration) and run the byte-validation pass over CL-AST-01..12 and the M9 retail-side items.
4. **Complete the Q-06 per-phase invariant census** (in-repo; the global inventory in cross/01 §4.2 is the checklist).
5. **Document the Q-70 seed-source choice** (§7) with the reproducibility consequences stated per option.
6. **Run a cross-document re-verification** of this document and cross/01/02 against the pinned references (the M10 adversarial test in the plan log is the template: 10 specific claims, each re-checked against source at `71ce130`).
7. **Re-confirm the reference pin** before any implementation work begins (the Phase-A precedent: a delta census before the pin advances — `game/07` §12.1).

## 11. What M10 deliberately did not do

- No Rust architecture, crate, API, or dependency decisions (the mission's explicit boundary; plan working-method 4).
- No new subsystem research beyond the four bounded register-adjudication checks (cross/01 §9: the lwjglx jar manifest, the Mod-Loader 4.2.5→4.3.5 git diff, the JavaFX import graph, the frame-skip/turbo intent — each performed solely to adjudicate a register item with direct evidence).
- No re-adjudication of any `Unresolved` row against original behavior (none of the required evidence is in-repo).
- No bulk formatting rewrite of M1–M9 documents (the new PSX address/offset convention applies to newly written material — cross/01 §8; the register re-triage edited in place only where substantively touched).
- No commit, no milestone beyond M10, no start of any implementation phase.

## 12. Pointer summary

| Question | Answer lives in |
|---|---|
| The full initialization/dependency graph, data/event flows, ownership, concurrency | cross/01 (the cited 14-step boot chain; the 21-phase frame contract; the §4 state inventory; the §5 concurrency model; the §6 flows) |
| The consolidated constraint set (109 rows + 10 decision items + the non-constraint list) | cross/02 |
| The limitation split (architectural A1–A13 vs Java-specific L1–L12) | this document §3–§4 |
| The asset dependency classes | this document §5 |
| The asset-independent open items | this document §6 |
| The seed-source options (no decision) | this document §7 |
| Every open item's re-triaged status (resolved-with-pointer or still-open-with-evidence) | `research/04-unresolved-questions.md`, "M10 Re-triage" |
| The audit history (the evidence for the verification track record) | `audits/r0-r1-xhigh-audit.md`; `audits/m3-xhigh-audit.md`; the M9-05c/06/07 adversarial passes in `research/03-research-plan.md` |
