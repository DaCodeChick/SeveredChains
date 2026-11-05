# M4-05 — Compatibility Classification

Status: Complete (M4, 2026-11-04). Reference: Severed-Chains @ `0c08c679b`, read-only. This is the M4 synthesis document: it classifies every behavior-significant finding of M4-01 (GPU/VRAM/ordering tables, `research/psx/01-gpu-vram-ordering.md`), M4-02 (GTE, `research/psx/02-gte.md`), M4-03 (SPU, `research/psx/03-spu.md`), M4-04 (XA ADPCM, `research/psx/04-xa-adpcm.md`) against the four-class framework, documents the behavior-significant fixed-point and RNG helpers in full (§2), and closes the M4 loop back to the R1 boundary pass (`research/psx/01-compatibility-boundaries.md`) and the plan's three-tier compatibility scheme. Evidence vocabulary and layer model as in the sibling documents; no public PSX documentation was consulted (network-restricted), so every layer-1 (PSX) attribution in the table is `Inferred`/`Unresolved` until the external cross-checks in Q-21..Q-23, Q-65..Q-67 land. No `asset-validated` claim is made anywhere.

## 1. Classification framework

Four proposed compatibility classes (the plan's three tiers plus an explicit unknown class):

- **Hardware-accurate** — the reference's behavior matches original PSX/LoD behavior with in-repo evidence that the match was *engineered for* (e.g. a deliberate integer-math patch made "to match retail"), or the behavior is a verbatim decompilation of the original's own code path. Highest bar: such rows carry source-verified in-repo justification.
- **Behaviorally-equivalent** — the observable result matches (or is within documented, bounded tolerance of) the original's, but the mechanism differs (re-timed clocks, float vs fixed-point arithmetic, software rasterizer, OpenAL delivery). Most M4 rows fall here: the reference was built to *look and sound like* the original, not to be it.
- **Format-only** — the concern is a file/format/structural property with **no observable runtime behavior** in the game (dead code, unimplemented parity code, internal host mechanics). Porting is a design choice, not a fidelity obligation.
- **Unresolved** — no evidence (in-repo or external) decides which behavior the original had; both candidate behaviors are documented, and the row names the evidence that would resolve it.

Classification is done **against the decompiled contract**: the reference is the ground truth for what the original did *as far as the decompilation shows*, and each row's confidence reflects that ground truth's strength (source-verified decompilation vs `Inferred` hardware attribution). "Divergence risk" = the risk that a faithful reimplementation choosing the *reference's* behavior would differ from the *original's* (not the risk of the reference diverging from itself).

## 2. Fixed-point and RNG helpers (behavior-significant, documented in full)

All in `legend/game/Scus94491BpeSegment.java` unless noted; all ROM-anchored (`@Method`), i.e. decompiled original code (layer 3).

### 2.1 PSX-degree convention (`MathHelper`, source-verified)

- `PSX_DEG_TO_RAD = 2π/4096`; `psxDegToRad(x) = x × 2π/4096` (int and float overloads, `MathHelper.java:190-196`). The game's angle unit is the **PSX degree: 4096 per full turn**; `radToPsxDeg` is the exact inverse for display/round-trip. Every rotation in the game (GTE matrix build, Fate trig ops, animation keyframes) flows through this convention.

### 2.2 `rsin` / `rcos` (0x80013598 / 0x800135b8, source-verified)

```java
public static short rsin(final int theta) {
  return (short)(MathHelper.sin(MathHelper.psxDegToRad(theta)) * 0x1000);
}
// rcos identical with cos
```

- **Mechanism:** float (IEEE single, `MathHelper.sin` returns `float`) sine/cosine scaled to a **0x1000 range** and cast to `short` (s13.12-shaped output). This is a *float computation shaped like the PSX fixed-point sine output* — not a hardware table lookup. The PSX GTE's ROTS family uses a fixed-point table (13.12, signed); within the output quantization (±1/0x1000 ≈ 0.024%) the float result is equivalent (`Inferred` layer-1; external cross-check Q-67).
- **Javadoc caveat (source-verified, call-site-dependent):** "ALWAYS returns a positive value even for negative angles" — the function is plainly *signed* for unrestricted input (negative theta → negative sin); the guarantee holds only at call sites that constrain `theta` to [0, 0x400] (e.g. `drawBoxagonEffect`'s `theta = angle·0x400/123`, `angle ≤ 123` — M4-01 §7.1). Documented as written; no call site outside that constraint was found.
- **Truncation:** the `(short)` cast truncates toward zero (e.g. `0.9999 × 0x1000 → 0xfff−1`), so rsin's max is 0xFFF, never 0x1000 — matches the PSX sine table's 0xFFF peak (`Inferred`).

### 2.3 `simpleRand` (0x800133ac, source-verified)

```java
int v1 = (simpleRandSeed_8004dd44 & 0xfff) == 0
       ? RENDERER.getVsyncCount() * 9   // "If seed is 0, seed with vblanks"
       : simpleRandSeed_8004dd44;
simpleRandSeed_8004dd44 = v1 * 9 + 0x3711;
return simpleRandSeed_8004dd44 / 2 & 0xffff;
```

- **Mechanism:** LCG (×9, +0x3711) on the ROM-recorded seed variable `simpleRandSeed_8004dd44` (initial value **3**, `Scus94491BpeSegment_8004.java:58`); output is the updated seed `>> 1 & 0xffff` — **16 bits**.
- **Re-seed guard:** the in-source comment says "if seed is 0", but the code checks **`(seed & 0xfff) == 0`** (low 12 bits zero) — a degeneration guard; whether the LCG state (multiplier 9) can reach that condition from the initial seed 3 is not established here (`Inferred`: practically unreachable in normal play). When it does fire, the reference seeds from **`RENDERER.getVsyncCount() × 9`** — the host renderer's vsync counter stands in for the PSX VBLANK counter (layer-4 substitution of a layer-1 signal; the value's distribution differs from the original's, but the path is practically unreachable).
- **Classification:** **Behaviorally-equivalent** — verbatim decompiled original algorithm; the only substitution (vsync source) sits behind a practically-unreachable guard. 20 game call sites (representative: `Battle.java` transition effects; `ScriptState.scriptRandom` — §2.5).

### 2.4 `rand` / `srand` (0x8002d260 / 0x8002d270, source-verified)

```java
private static int randSeed = 0x24040001;
public static int rand()  { randSeed = randSeed * 0x41c64e6d + 0x3039; return randSeed >>> 16 & 0x7fff; }
public static void srand(final int seed) { randSeed = seed; }
```

- **Mechanism:** 32-bit LCG (×0x41C64E6D, +0x3039); output = bits 30:16 (**15 bits**, 0–0x7FFF). Initial seed **0x24040001** (ROM-recorded).
- **Seeding — the layer-4 break:** the *only* `srand` call site in the game is at `SMap.java:4091` in a **LAB-labeled (decompiled) submap-load path** (`case INIT_0` of the loading-stage switch, `//LAB_800e5a34`): **`srand((int) System.nanoTime())`**. The `srand` call itself sits in decompiled original code (the original evidently seeded the RNG at submap load), but **the argument is a reference-authored host-clock placeholder** — the decompilation does not preserve what value the original passed (`Unresolved` — vblank, campaign data, or a fixed value). Consequences (source-verified mechanism, `Inferred` impact): (a) the reference's `rand()` stream **resets to a host-clock value on every submap entry** — gameplay rolls (battle accuracy/drops/loot that run after submap transitions) are **non-reproducible across runs** in the reference; (b) any determinism requirement for the reimplementation (e.g. reproducible battles for testing) must pick a seed source deliberately (Proposed, M9).
- **Classification:** algorithm **Behaviorally-equivalent** (decompiled original LCG); seed source **Unresolved** (Q-70). 50 `rand()` call sites, incl. `Battle.java:4331,8527,9124`, `BattleStateEf4.java:623-626`, `SBtld.java:118`, `SEffe.java:1966-2055`.

### 2.5 Scripting dependency map (M3 cross-reference)

The Fate VM exposes the helpers above as **built-in ops** (dispatched in `ScriptState`'s opcode switch, `ScriptState.java:760-763`), with source-verified bodies:

| Fate op (`OpType`) | Body (`@Method`) | Formula (source-verified) |
|---|---|---|
| `RAND` (49) | `scriptRandom` (0x80016c00, `ScriptState.java:1190`) | `params[1] = (params[0] × simpleRand()) >>> 16` → result in [0, X): **Fate `rand(X)` is powered by `simpleRand`** (the 16-bit LCG, §2.3) — *not* by `rand()` |
| `SIN_12` | `scriptSin` (`ScriptState.java:1196`) | `params[1] = rsin(params[0])` (PSX-degree in, 0x1000-scaled short out) |
| `COS_12` | `scriptCos` (0x80016c80, `ScriptState.java:1202`) | `params[1] = rcos(params[0])` |
| `ATAN2_12` | `scriptRatan2` (0x80016cb4, `ScriptState.java:1208`) | `params[2] = radToPsxDeg(atan2(params[0], params[1]))` (float atan2, PSX-degree int out) |

**Scope note (M3 interface):** these four op bodies are *not* part of the M3 native-function catalog (M3's 592 slots = the `@ScriptDescription`-registered callable functions; these are opcode-dispatched built-ins outside `descriptions.csv`). M3 documented the `RAND`/`SIN_12`/`COS_12`/`ATAN2_12` opcodes in the shared opcode tables; this document documents their bodies, completing the M3→M4 hand-off for scripting-dependent fixed-point behavior.

## 3. Classification table

Legend for "Confidence": H = source-verified decompilation with low ambiguity; M = source-verified mechanism with an `Inferred` layer-1 attribution; L = `Inferred`/`Unresolved` on a load-bearing point. "Risk" = risk that porting the *reference's* behavior diverges from the *original's*.

### 3.1 GPU / VRAM / ordering table (M4-01)

| # | Behavior | Reference implementation | Class | Rationale | Conf. | Risk | Follow-up |
|---|---|---|---|---|---|---|---|
| G1 | VRAM 1024×512; dual 15-bit/24-bit stores; 4/8-bit texels in low bits of 16-bit words | `Gpu` per-pixel Java arrays, both stores written atomically | Behaviorally-equivalent | Data semantics mirror the PSX VRAM organization; 1024×512 correspondence to physical VRAM is source-verified as the reference's model, hardware attribution external | M | Low | Q-65c |
| G2 | Bpp table (4/8/15/24, width divisors/masks) + TPage/CLUT field encoding; exact encode/decode pair | `Bpp` enum + `Graphics.GetTPage/GetClut` (0x8003b3f0/0x8003b430) | Behaviorally-equivalent | Decompiled libgs contract; ordinal = PSX TPage bpp field; full PSX register mirrored but only low bits used (1024×512) | H | Low | Q-65 |
| G3 | Center-origin draw offset; double buffer; PSDCNT frame counter invalidating the GsCOORDINATE2 matrix cache | `Gpu.drawingOffset`, `renderBuffers[2]` flip in `Gpu.tick`, `GsSwapDispBuff` 0x8003c350 | Behaviorally-equivalent | libgs/PSX double-buffer offset contract decompiled verbatim; cache-invalidation semantics exact | H | Low | — |
| G4 | 384 → 368 display/draw-width clamp | `Gpu.displayMode`/`drawingArea` rewrite width 384→368 | **Unresolved** | Reference-pinned correction of a PSX-era width quirk (layer-4); whether the original's effective NTSC width behaved the same is unknown; all "wide" contexts (title, menus, submap) render 368 | L | Medium | Q-65 |
| G5 | Ordering table: 14 bits, size 0x4000, zMax 0x3ffe (clamp) vs slot 0x3fff (clears), back-to-front traversal at frame end | `Gpu.zQueues`, `Gpu.tick` | Behaviorally-equivalent | Plan constants verified exactly (source-verified); back-to-front direction matches PSX OT scan (`Inferred` layer-1); re-derivation on reinit (`syncFrame_reinit`) exact | M | Low | Q-21, Q-65 |
| G6 | Same-z primitive order: **LIFO** in reference (reverse insertion) vs PSX FIFO (queue order) | `Gpu.tick` iterates each slot's list from the tail | **Unresolved** | The clearest GPU-model divergence; `Battle.applyTextureAnimation` (z=1, 3× VRAM copy) is load-bearing on the order (M4-01 §7.2); both orders are internally consistent with the decompiled code | L | **High** (if any same-z overlap is visible) | Q-66 |
| G7 | z insertion: `(avgZ/4 + zOffset) / 2^zShift` clamped to zMax; float division; `Math.round` at queue time | `tmd/Renderer.java:166-172` | Behaviorally-equivalent (against decompiled contract) | The /4 convention is the original libgs SDK contract (preserved javadoc); whether the OT-insert path scaled differently is Unresolved; float-vs-shift is value-identical at zShift=0 | M | Low-Medium | Q-67 |
| G8 | Rasterizer: clockwise re-orientation, zero-area drop, top-left edge rule, barycentric integer-division interpolation, bbox clip to scaledDrawingArea | `Gpu.rasterizeTriangle` | Behaviorally-equivalent | Structural faithfulness to the PSX pixel-selection rule is claimed and source-verified in mechanism; **pixel-exactness unverified** (no asset/visual baseline exists) | M | Medium | Q-65 (external), M5 |
| G9 | Texturing: 15-bit index from `vram15`, CLUT color from `vram24`; index-0 texel = transparent; `T×C >>> 7` (8.7) | `Gpu.getTexel`, `applyBlending` | Behaviorally-equivalent | Matches PSX texture-shade equation (`Inferred` layer-1); the 0-index transparency is a game-side CLUT convention | M | Low | Q-65 |
| G10 | Blend equations: (B+F)/2, B+F, B−F, B+F/4 (F/4 first, truncating); output alpha = front pixel's | `Gpu.handleTranslence` | Behaviorally-equivalent | Equations source-verified; PSX counterpart per in-source comments; operator order (F/4 before add) preserved exactly | H | Low | Q-65 |
| G11 | TPage translucency rate 00 → `B+F/2` | `Translucency` ordinal mapping, `tmd/Renderer` | **Unresolved** | The reference maps rate bits 00 to B+F/2; common PSX documentation suggests rate 00 may mean "no blending" for translucent GP0s — no external docs consulted; whether any LoD TMD uses rate-00 translucents is an asset-census item. **M5-02 advance:** the rate→mode table (00=(B+F)/2, 01=B+F, 10=B−F, 11=B+F/4) is now source-verified for *both* rendering paths — the modern loader's per-mode mesh split and the GPU path's TPage-rate command (rendering/02 §1.3/§4.5) — so the reference is internally consistent; only the layer-1 attribution remains open | L | Medium | Q-65 |
| G12 | Mask bit (attr 11) set/skip; `NOT_TO_MASKED_AREAS` (attr 12) draw gating; translucent-textured blending gated on front-pixel alpha | `Gpu.status`, `handleTranslence` condition | Behaviorally-equivalent | PSX draw-attribute semantics mirrored; the textured-alpha gate matches the documented PSX rule (`Inferred` layer-1) | M | Low | Q-65 |
| G13 | One-frame display latency: frame N composed at end of frame N, displayed N+1 (vblank model); mode-1 window blit | `Gpu.tick` buffer flip + `displayTexture` blit | Behaviorally-equivalent | Mirrors PSX vblank presentation (`Inferred` layer-1); frame-alignment matters for any visual comparison | M | Low | M5 |
| G14 | CPU→VRAM upload: both stores, 15-bit LE words, partial-rect truncation guard ("DEFF stuff"), assert-only bounds | `Gpu.uploadData15` | Behaviorally-equivalent | Game contract source-verified; truncation guard is layer-4 robustness the original (exact-rect writes) did not need; no production range checks | H | Low | — |
| G15 | VRAM→VRAM copy: per-pixel, mask-honoring, **no overlap protection** | `Gpu.copyVramToVram` | Behaviorally-equivalent | Game avoids in-place overlap via the (960,256) backing store (M4-01 §7.2); PSX BLT overlap semantics Unresolved (no external docs) | M | Low | Q-65 |

### 3.2 GTE (M4-02)

| # | Behavior | Reference implementation | Class | Rationale | Conf. | Risk | Follow-up |
|---|---|---|---|---|---|---|---|
| T1 | RTPS/RTPT/NCLIP/AVSZ3/AVSZ4/NCCS algorithms (register-model subset; SZ/SXY FIFOs, error/flag register, projection test, saturation, overflow flags) | `Gte.java` (float registers) | Behaviorally-equivalent | Every used op reproduced algorithmically; the unused tail (RTD/RTM/ROP/NRM/NCSR/DPA/MV10/MV20/RSQ) is absent by design — no game call sites | H | Medium (mechanism, per T2) | Q-67 |
| T2 | Float register model vs PSX fixed-point: per-op rounding — SXY 22.10 truncation (PSX) vs float+round (ref); NCCS 12.12 stage quantization vs float; MAC0 32-bit vs float→long | §3 of M4-02 (per-op table) | Behaviorally-equivalent (bounded divergence) | ±1 screen px on SXY; ±1 color step on NCCS; animation-matrix quantization (s16.12 in original) accumulates over long chains — the dominant GTE divergence | M | Medium-High | Q-20/Q-67 |
| T3 | Screen-edge handling: SXY saturation ±1024 sets error → **whole primitive discarded** (no clipping) in the legacy TMD path | `tmd/Renderer.java:146` + `Gte.hasError` | Behaviorally-equivalent | Matches the Inferred PSX GTE edge behavior; the float saturation boundary admits ±1 px more primitives than the hardware (a value like 1023.9 is drawn in the reference, discarded on PSX) | M | Medium | Q-67 |
| T4 | `GsSetSmapRefView2L` — **integer-precision** submap camera (int deltas, `(int)Math.sqrt`, ×0x1000/len normalization) | `Graphics.java:793-841` (reference-authored, non-`@Method`) | **Hardware-accurate** (engineered match) | The in-source comment states integer math was used "so that sobj and collision placements match retail, as higher precision math was causing noticeable shifts" — in-repo proof the original's submap camera math is integer-equivalent; the port must reproduce the integer variant | H (in-repo evidence) | None known (this is the reference's correction) | Q-67 (other camera contexts) |
| T5 | Depth-cue MAC0 term in RTPS (DQA/DQB constants, IR0, range flag) — implemented, **never read** by the game | `Gte.perspectiveTransform` | Format-only | Dead parity code; no observable behavior; porting is optional | H | None | — |
| T6 | Projection plane H: 1000 (`InitGeom` 0x8003e958) overridden to **640** by `gameInit`; `2z ≤ H` plane test sets error | `Graphics.setProjectionPlaneDistance` 0x8003f8f0 | Behaviorally-equivalent | Decompiled constants; the 640 default is LoD's (`Inferred` layer-3) | H | Low | — |
| T7 | Lighting: NCCS `(N·(−L))⁺ × Lc + B` per channel, up to 3 parallel lights, no specular/fog | `GsSetFlatLight` 0x8003c6f0 (negated direction convention), `Gte.normalColour` | Behaviorally-equivalent | Layer-3 semantics source-verified; PSX NCCS stage-quantization divergence per T2 | M | Low | Q-67 |

### 3.3 SPU (M4-03)

| # | Behavior | Reference implementation | Class | Rationale | Conf. | Risk | Follow-up |
|---|---|---|---|---|---|---|---|
| S1 | 60 tps SFX-thread tick; 735 stereo samples/tick at 44.1 kHz; SPU and sequencer share the tick | `Audio.soundLoop` (60 Hz), `Spu.tick` | Behaviorally-equivalent | The 60 tps is **LoD-verified** (the ROM's own `ticksPerSecond_42 = 60`, `initSpu` 0x8004b834); hardware sample-clock attribution external | M | Low | Q-22 |
| S2 | Key on/off: 64-bit edge words, accumulate/snapshot/zero, applied at the tick's first sample slot (60 Hz quantization ≤ 16.7 ms) | `Spu.keyOn/keyOff`, `Spu.tick` | Behaviorally-equivalent | Mechanism source-verified; absolute sub-tick timing differs from PSX register latency (relative ordering preserved) | H (ref) / M (hw) | Low-Medium | Q-22/Q-68 |
| S3 | 24 voices 1:1 bound to the 24 SSSQ note slots; game bound `playingSoundsUpperBound_03 = 20`; **no stealing** | `Audio.playingNotes_800c3a40`, `initSpu` 0x8004b834 | Behaviorally-equivalent (decompiled contract) | Allocation is game-side decompiled code; whether the original allocated/stole differently is Unresolved | M | Medium | Q-68 |
| S4 | ADSR: phase machine (Attack/Decay/Sustain/Release/Off), shift/step/exponential rules, 44.1 kHz cycle counting, `keyOn` reset (register 0x2000) | `Voice.tickAdsr`, `ADSR` | Behaviorally-equivalent | Rules source-verified from the PSX ADSR register layout in comments; hardware comparison external | M | Low | Q-22 |
| S5 | Voice ADPCM: 28 samples/8-byte address, 3-bit filter (0–4) with overflow clamp, POS/NEG tables, loop start/end/repeat flags | `Voice.decodeSamples/sampleVoice` | Behaviorally-equivalent | Same codec core as XA (M4-04); 28-sample/8-byte rate attribution external; loop-flag word usage source-verified | M | Low | Q-22/Q-23 |
| S6 | Pitch stepping (4096-step counter) + 512-position 4-tap cubic interpolation with cross-block lookbehind cache; FM via previous voice (capped 1.5×); single shared 16-bit noise generator | `Voice`, `Spu.noiseLevel` | Behaviorally-equivalent | Reference-specific interpolator; the PSX SPU's own interpolation/noise details are external (Q-22); FM/noise conventions `Inferred` layer-1 | M | Medium | Q-22 |
| S7 | Sweep-mode volume: **envelope unimplemented — full volume** (in-source TODO); the game packs sweep bits into note volumes | `Voice.processVolume`, `Audio.calculateNoteVolume` 0x8004ae94 | **Unresolved** (reference gap) | Reference behavior source-verified; the original's sweep behavior is hardware (external); whether retail data uses sweep bits is an asset-census item — sweep SFX are constant-loud in the reference | L | Medium | Q-68a |
| S8 | Reverb: float IIR network (wall reflections + 4 combs + 2 all-passes), work area 0x4000 = 65536 floats, config /32768 scaling, addresses ×8 | `Spu.reverb*` | Behaviorally-equivalent (reference network) | Implements the PSX reverb *register interface* with a reference-specific float network; hardware topology equivalence Unresolved; dual reverb (SPU float + OpenAL music) is a layer-4 hosting split | M | Medium | Q-22 |
| S9 | Synchronization: single `Spu.class` monitor over `tick` + all control methods; `setPlayerVolume` (host float) unsynchronized | `Spu.java` (R1 audit F-02) | Format-only | Host-internal concurrency with no observable behavioral counterpart in the original; the *observable* semantics is S2 | H | None | — |

### 3.4 XA ADPCM (M4-04)

| # | Behavior | Reference implementation | Class | Rationale | Conf. | Risk | Follow-up |
|---|---|---|---|---|---|---|---|
| X1 | 24-byte header (sync+header+subheader; coding info at offset 19: stereo/18900/8-bit flags); 18 × 128-byte portions; 2016 samples/sector/lane | `XaAdpcm`, `Fmv.java:420-431` | Behaviorally-equivalent | Geometry source-verified; format-corroborated by the 37.8 kHz rate × CD sector cadence; hardware sector sync bytes external | M | Low | Q-23 |
| X2 | ADPCM core: `s = (t << shift) + (old·f0 + older·f1 + 32)/64`, clamped to s16; POS/NEG tables | `XaAdpcm.decodeNibbles` | Behaviorally-equivalent | Core source-verified; the +32/64 rounding is asymmetric for negatives (Java truncation) — reference-specific rounding detail; hardware rounding external | M | Low | Q-23 |
| X3 | Filter field: **2 bits (0–3)** in XA decoders vs 3 bits (0–4) in the SPU voice decoder | `XaAdpcm` vs `Voice` | Behaviorally-equivalent (per-consumer) | Documented divergence, not reconciled; the decoders are separate consumers of one codec core | H | Low | Q-69c |
| X4 | FMV audio: native-rate decode (no resampling), `>>1` + float `FMV × MASTER` gain, 37.8 kHz OpenAL source, drop-on-full backpressure | `Fmv.java:342,421-431` | Behaviorally-equivalent | Rate source-corroborated (the source registration is the reference's playback contract); drop-on-full is layer-4 (the PSX would stall DMA); 8-bit/18900 flags parsed but unused | M | Low | Q-23 |
| X5 | 6→7 zigzag FIR (7×29 taps, 32-slot ring buffers, `sixStep = 6`, ×7/6 = 37800→44100) — **no caller** | `XaAdpcm.resampleTo44100Hz` (dead) | Format-only | Implemented-but-unwired; no observable behavior; porting is a design choice | H | None | Q-69b |
| X6 | Static never-reset predictor state (leaks across FMV files; ≤ 28 samples ≈ 0.74 ms per boundary) | `XaAdpcm` statics | **Unresolved** | Bug or intentional stream continuity — undecidable from the repo; impact bounded to the first block per FMV | L | Low | Q-69a |
| X7 | Mono files = two mono streams; lane-1 decoded with its own predictor; lane-0 duplicated to both output channels | `XaAdpcm.decode` | Behaviorally-equivalent | Matches M2's 16-track interleaving finding (M2-10 §3); the FMV track-index mapping is an open detail | M | Low | Q-69d |

### 3.5 Fixed-point / RNG (this document §2)

| # | Behavior | Reference implementation | Class | Rationale | Conf. | Risk | Follow-up |
|---|---|---|---|---|---|---|---|
| R1 | PSX degree: 4096 per full turn (`psxDegToRad`) | `MathHelper.java:187-197` | Behaviorally-equivalent | Layer-3 convention, decompiled verbatim; the whole angle pipeline flows through it | H | None | — |
| R2 | `rsin`/`rcos`: float sin/cos × 0x1000, `short` cast (peak 0xFFF, truncating) | `Scus94491BpeSegment.java:250-258` | Behaviorally-equivalent | Within 1/0x1000 of the PSX fixed-point sine output (`Inferred` layer-1); call-site angle constraints keep the signed/unsigned javadoc nuance invisible in practice | M | Low | Q-67 |
| R3 | `simpleRand`: LCG ×9 +0x3711, 16-bit out, low-12-bit vblank re-seed guard (initial seed 3) | `Scus94491BpeSegment.java:225-236` | Behaviorally-equivalent | Decompiled original algorithm; the re-seed source (renderer vsync) sits behind a guard whose reachability is not established (practically unreachable, `Inferred`) | M | Low | Q-70 |
| R4 | `rand`/`srand`: LCG ×0x41C64E6D +0x3039, 15-bit out (initial 0x24040001); **`srand(System.nanoTime())` at submap entry** (host-clock placeholder in a LAB-labeled decompiled path) | `Scus94491BpeSegment.java:585-597`; `SMap.java:4091` | Algorithm Behaviorally-equivalent; **seed source Unresolved** | The LCG is decompiled original code; the only seeding site's argument is a reference-authored host-clock placeholder — per-submap `rand()` streams are non-reproducible in the reference; the original's seed argument is not preserved by the decompilation | M / L | **High** (for reproducibility) | Q-70 |
| R5 | Fate `RAND`/`SIN_12`/`COS_12`/`ATAN2_12` op bodies (outside M3's 592-slot catalog) | `ScriptState.java:1190-1210` | Behaviorally-equivalent | Formulas source-verified (§2.5); Fate `rand(X)` = `X × simpleRand() >>> 16` — scripted randomness rides the R3 stream | H | Low | — |

## 4. Synthesis — compatibility posture

**Counts (43 rows):** Hardware-accurate **1** (T4 — the one row with in-repo evidence of an engineered hardware match); Behaviorally-equivalent **33** (the norm: the reference is a behavior-matching functional model with documented bounded divergences; R4 counts under BE for its decompiled algorithm, with its seed half flagged Unresolved); Format-only **3** (T5 the dead MAC0 parity term, S9 the host-internal sync, X5 the unwired FIR); Unresolved **6** (G4 the 384→368 clamp, G6 same-z LIFO/FIFO, G11 TPage rate-00 semantics, S7 sweep volumes, X6 FMV predictor-state continuity, R4's seed source).

**Highest-impact items for any reimplementation decision:**

1. **G6 (same-z order)** — the only GPU-model divergence with a known in-repo consumer (`Battle.applyTextureAnimation`); a "fix" to FIFO would break the reference's behavior; choosing either order without original-behavior evidence is a risk either way (Q-66).
2. **R4 (RNG seeding)** — the reference is non-reproducible across runs at submap boundaries by construction; a reimplementation wanting reproducible gameplay must choose a seed source deliberately (Q-70; M9).
3. **T2 (float GTE)** — bounded (±1 px / ±1 color) but *systematic*; T4 shows the game itself needs integer math in at least one camera context; M9's animation work should re-audit camera paths for similar integer requirements (Q-67).
4. **S7 (sweep volumes)** — a known reference gap with plausible gameplay exposure (any volume-swept SFX); asset census needed (Q-68a).
5. **G11 (TPage rate 00)** — if any retail TMD uses rate-00 translucents, the reference's B+F/2 mapping may be wrong for that content; asset census needed (Q-65).

**Systemic notes:** (a) every layer-1 attribution in this table is `Inferred`/`Unresolved` pending the external PSX documentation cross-checks (Q-21..Q-23, Q-65..Q-67) — the classifications would be re-confirmed or corrected in M10 when that evidence is admitted separately; (b) nothing in M4 is `asset-validated` — no retail bytes exist in this environment, and no claim is made as such; (c) the classification is against the **decompiled contract**: where the decompilation is a verbatim `@Method`-anchored copy (G2, G5, R3, R4-algorithm, T6), the row is as strong as the decompilation; where the reference is a layer-4 model (S1-S8, X1-X4, G1, G8-G15), the rows are as strong as the mechanism documentation plus the `Inferred` hardware attribution.

## 5. Consequences for later milestones

- **M5 (rendering):** must disposition the mode-0 renderer path (Q-37) *against* the G1-G15 rows — in particular the TPage/CLUT shader uniforms (G2/G9), the translucency pass quirks (R1 audit F-13/F-18, adjacent to G10/G11), frame alignment (G13), and the 368-wide content scaling (G4/G7 of M4-01 §8).
- **M6 (music/audio):** the SPU/XA rows (S1-X7) are the contract for the sequencer work; the dual-reverb split (S8) and the 37.8 kHz FMV rate (X4) are load-bearing hosting decisions; the Q-68/Q-69 gap lists travel with M6.
- **M9 (gameplay):** R3/R4 govern every accuracy/drop/loot/effect roll and scripted `rand` (R5); the reproducibility question (R4) should be answered as an explicit design decision with the seed source documented; the T4 integer-camera precedent suggests auditing other `GsSetRefView`-family contexts for integer-math requirements.
- **M10 (synthesis):** the external-documentation cross-checks (Q-21..Q-23, Q-65..Q-67, Q-68, Q-69) are the M10 evidence-admission worklist; this table's `Inferred` layer-1 rows are the re-verification checklist.

## 6. Register updates

- **Q-70** (new): **Original gameplay-RNG seed sources.** (a) `srand`'s only game call site is reference-authored `srand((int)System.nanoTime())` at submap entry (`SMap.java:4091`) — the original's seed source (vblank? campaign/battle data? fixed?) is unknown, and the reference's per-submap `rand()` stream is non-reproducible across runs as a result; (b) `simpleRand`'s re-seed guard (low-12-bits-zero, practically unreachable) sources from `RENDERER.getVsyncCount()` — a layer-4 stand-in for the PSX VBLANK counter; (c) consequence for the reimplementation: any determinism requirement (reproducible battles, regression testing) must choose and document a seed source (Proposed, M9). — `Open` — M9 + external/asset evidence — needs: original-behavior evidence for the submap seed (asset/external), and a documented design decision in M9.
- **Q-20/Q-21/Q-22/Q-23** — status unchanged by this document (advanced by M4-01..M4-04); this document consumes their findings as the classification basis and assigns them to M10 as the evidence-admission worklist (§5).
- **M3 interface note:** the Fate `RAND`/`SIN_12`/`COS_12`/`ATAN2_12` op bodies (this document §2.5) are documented here because they fall outside the M3 native-function catalog's scope (opcode-dispatched built-ins, not `descriptions.csv` slots) — the M3 documents' opcode tables remain correct as-is; no M3 correction is implied.
