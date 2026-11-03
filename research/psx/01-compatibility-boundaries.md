# PlayStation Compatibility Boundaries

Status: Foundational pass (R1). Deeper per-constant classification is M4; the consolidated compatibility checklist is M10. Repositories: Severed-Chains @ `0c08c679b` (primary), with the classification purpose tied to the project's `psx-sdk` goal (context only — no design decisions).

## 1. Method

Every behavior observed in the reference ecosystem is assigned to one of four layers:

1. **PSX platform behavior** — behavior of the PlayStation console/platform generally (hardware features, BIOS-level conventions, format standards).
2. **`psx-sdk` reuse candidate** — layer-1 behavior that the reference *actually implements* in a self-contained, LoD-independent way, and which the Rust project could plausibly reuse. (Candidate, not decision.)
3. **LoD-specific** — behavior specific to *The Legend of Dragoon* (its code, data, rates, formats, scripts, minigames).
4. **Reference-specific** — behavior specific to the Severed Chains reimplementation (host I/O, modern enhancements, workarounds, tooling).

The layer assignment is evidence-based: where the reference's code carries original ROM addresses (`@Method(0x…)`), it is decompiled LoD code (layer 3 unless it is clearly a re-hosted platform service); `legend.core` services without address annotations are reference-implementation models of layer-1 hardware (layer 2 candidates when LoD-independent); everything in `legend.core.platform`, modding, unpacking, and modern rendering is layer 4.

## 2. Classification by domain

### 2.1 Rendering

| Behavior | Layer | Evidence / notes |
|---|---|---|
| VRAM 1024×512, 15-bit and 24-bit pixel representations, CLUT pages | 1 / **2** | `Gpu` (Gpu.java:33-40): `STANDARD_VRAM_WIDTH/HEIGHT = 1024/512`, `vram15`/`vram24`, `vramTexture15` (R32 packed). LoD-independent model of PSX VRAM. |
| Double-buffered display with per-frame swap (draw into the non-displayed buffer) | 1 / **2** | `Gpu.renderBuffers[2]`, `drawBufferIndex` flip in `tick()`; `swapDisplayBuffer_8004dd40` called at frame start from ROM code — the *pattern* is PSX display convention, the *implementation* (software buffer + GL upload) is layer 4. |
| Ordering table (depth-sorted command queues): size 0x4000, zMax 0x3ffe, bits 14, zShift 0 | 1 / **2** (size/limits as *configured* are LoD choices) | PSX ordering-table hardware is general; the 0x4000/zMax values are set by LoD's `gameInit` (Scus94491BpeSegment.java:609-613). The queue implementation (`zQueues`, Gpu.tick) is layer 4 modeling the hardware. |
| GPU command set (primitives, BLTs, masks, draw area/offsets) | 1 / **2** | `GpuCommand*` family + `Gpu` queue/exec; the command vocabulary mirrors the PSX GPU. Per-command faithfulness is M4. |
| Bpp encodings (4/8/15/24-bit: width divisors, masks, shifts) | 1 / **2** | `legend.core.gpu.Bpp` — pure PSX pixel-format parameterization. |
| H-division pseudo-perspective (centered ortho + projection-plane division in shader) | 1 / 3 | The *technique* is the PSX's standard perspective emulation (layer 1); LoD's LEGACY render mode uses it (SMap/TITLE, engine-states doc §8). Implementation is layer 4. |
| Translucency formulas (B+F, B-F, B+F/2, B+F/4, first-pass opaque pixels) | 1 / **2** | PSX blending equations, implemented in the renderer's translucency passes (execution model §2 step 8) and the TMD shaders (`tmdTranslucency`). |
| 15→24-bit color expansion | 1 / **2** | `MathHelper.colour15To24`. |
| TMD model pipeline (model parts, MV transforms, CTMD animations, UV adjustments, shadows, light sources) | 3 | LoD's proprietary model format/technique (PSX-era, but not a console feature). M5. |
| 320×240 native resolution; widescreen modes; post-processing (CRT/scanlines/roll/pixelate/bloom) | 3 (native res) / 4 (widescreen, post) | `preload()` resizeDisplay(320,240) is ROM code; the rest is reference enhancement. |
| Render model pools, sorting (perspective/ortho depth + sequence), framebuffers, batch architecture, GL/GLES/Noop APIs | 4 | `legend.core.renderer` — modern re-hosting of the frame. |
| Three render presentation modes (OpenGL/legacy/VRAM) | 4 | `RenderEngine.legacyMode` (dev toggle; default 0 in releases). |

### 2.2 Geometry (GTE)

| Behavior | Layer | Notes |
|---|---|---|
| GTE register file and per-operation semantics (MV transforms: rotations, scaling, transfers, normalizing, projections, OTZ) | 1 / **2** | `legend.core.gte.Gte` + `MV` — a float re-implementation of the PSX GTE. Float-vs-fixedpoint divergence is the central M4 question (register Q-20). |
| `rsin`/`rcos` (signed sine/cosine scaled by 0x1000, PSX degree input) | 1 / **2** | `Scus94491BpeSegment.rsin/rcos` (`@Method` — ROM code) with `MathHelper.psxDegToRad`; the 0x1000 scale and "always positive" quirk are PSX-fixed conventions LoD uses. |
| `simpleRand` (vsync-seeded LCG ×9 + 0x3711, `/ 2` & 0xffff) and `rand`/`srand` (×0x41c64e6d + 0x3039, >>>16 & 0x7fff) | 3 | ROM math helpers (`@Method`); behavior-significant for animation/determinism (M3/M9). Operator note: `simpleRand` uses integer division `/ 2` (truncation), not `>> 1` (audit F-10). |
| Projection plane distance (default 640) and per-state camera (`GsRVIEW2`) | 3 | LoD usage of the GTE projection model. |

### 2.3 Audio (SPU / XA)

| Behavior | Layer | Notes |
|---|---|---|
| SPU voice model: 24 voices, edge-triggered key on/off, per-voice volume (16-step scale), channel modes (noise/reverb per channel) | 1 / **2** | `Spu`/`Voice` (Spu.java:28, 84-175) — self-contained model; LoD-independent. |
| ADSR envelope (16 steps, phases, volumes) | 1 / **2** | `ADSR`/`Phase`/`Counter` in `legend.core.spu`. |
| Reverb (shaded-diffusion-style network, 0x4000 work area, per-channel input/output mix, mode selects) | 1 / **2** | `Spu.processReverb`, `Reverb`, `ReverbConfig`. PSX SPU reverb is a documented hardware block; the network constants need M4 cross-check (Q-22). |
| Noise generation (17-bit LFSR-style per noise-mode, per-tick) | 1 / **2** | `Spu.tickNoiseGenerator`. |
| XA ADPCM decoding (24-byte header, coefficient/step/zigzag tables, 4-bit samples) | 1 / **2** | `XaAdpcm` (tables visible in source; cross-check pending, Q-23). |
| 60 ticks/s SPU clock; `SAMPLES_PER_TICK = BASE_SAMPLE_RATE / 60` | 1? / **2** (model) | `Spu.SOUND_TPS = 60`; the 60 Hz cadence is a reference modeling choice matching PSX SPU update rates. Layer-1 attribution is **not established by repo evidence** and needs external PSX documentation (audit F-16; M4). |
| SSSQ soundfont format, soundbanks, instruments/layers/breathes, BGM command set | 3 | LoD's proprietary sequenced-music format (SSSQ); the *sequencer concept* is PSX-era software, not hardware. M6. |
| XA→Opus transcoding, Opus playback | 4 | Reference enhancement (PSX XA playback is expensive on modern hosts). |
| OpenAL device management, 120 Hz source loop, interpolation/pitch/effects quality options | 4 | Reference audio hosting. |
| Dual-stream architecture (SPU for SFX + sequencer for music) | 3 | How LoD uses the PSX's SPU (SFX) alongside a modern soundfont path (music) — the SFX-on-SPU split is original LoD behavior; the soundfont music path is a reference replacement for the original's hardware-sequenced music. |

### 2.4 Input

| Behavior | Layer | Notes |
|---|---|---|
| PSX pad hardware model (4-way D-pad, 4 buttons + 4 shoulder triggers, no axes) | 1 | Not directly modeled anywhere — the reference models *semantic* input (actions) instead. The original's pad bit layout survives in the retail action set (M9). |
| Action/activation/binding abstraction, deadzones, rebinds, gamepads, keyboard, rumble | 4 | `legend.core.platform.input` + `SdlPlatformManager`. |
| Rumble (vibration) | 3 (LoD feature — the game drives rumble via scripts) / 4 (implementation) | `legend.game.Rumble` (scripts) over platform rumble. |
| Sampling input between frames (SDL) vs in-frame pad polling (original) | 4 vs 3 | Execution model §9. |

### 2.5 Timing

| Behavior | Layer | Notes |
|---|---|---|
| 60 Hz game-time unit (`timestamp_a0`), per-context frame rates (60 Hz submap/wmap/credits/battle-transition, 20 Hz battle, 30 Hz title/game-over/wmap-screens — execution model §5 write-site census) | 3 | ROM code; LoD's timing structure (audit F-01 corrected the R1 "30 Hz default" reading). |
| Per-state `tickMultiplier` (original per-state frame-rate records) | 3 | Text cadence normalization. |
| Frame-locked (not fixed-timestep) simulation, software pacing (no vsync), tick skip on slowness | 4 | Reference host pacing. |
| 60 Hz SPU tick / 120 Hz audio loop (real-time, decoupled) | 4 (cadences) / 1 (the 60 Hz SPU convention) | See §2.3. |
| `simpleRand` vsync-seeding | 3 | ROM behavior (seeds from vblank count). |

### 2.6 Software components living in the game (not hardware)

| Component | Layer | Notes |
|---|---|---|
| Fate script VM (interpreter, storage/params/flags, forks, native tables) | 3 | Software component of LoD (originally MIPS code); the *VM* is LoD-specific, not PSX platform behavior. M3. |
| Fate text/binary format + toolchain (Script-Recompiler) | 3 | LoD-specific format. M3. |
| Dabas minigame machine ("Mogul": ARMv4-style CPU, modeled peripherals, 32×32 1bpp tiles) | 3 | LoD-specific in-game software; the Dig-Dabas repo models a de-emulated version of the original minigame binary (`DABAS.BIN`). Not PSX hardware (Q-30: provenance of the machine model). |
| Save formats (retail + V1–V10), config `.dcnf`, tag model | 3 (retail) / 4 (V1–V10, `.dcnf`) | M7. |
| MRG/DEFF/TIM/TMD/ANIM/MCQ/SSSQ/FMV-MDEC formats | 3 | LoD asset formats (some generic PSX-era: MDEC is a Panasonic/PSX standard — layer 1 for the MDEC codec itself, LoD-specific for its container usage). M2/M5. |
| PSX CD/ISO structure (ISO9660, Form2, 2352-byte sectors) | 1 / **2** | The unpacker's ISO access is a self-contained, LoD-independent ISO9660+Form2 reader (M2). |

## 3. The `psx-sdk` candidate pool (foundational list)

Self-contained, LoD-independent, layer-1-modeled components in the reference that a `psx-sdk` could adopt or reimplement:

| Candidate | Reference location | Self-contained? | Open questions |
|---|---|---|---|
| VRAM model (1024×512, 15/24-bit, CLUTs, dirty tracking) | `Gpu` VRAM surface | Yes (display/presentation separable) | display-timing semantics (M4) |
| Ordering-table z-queue | `Gpu.zQueues` + command render | Yes | faithfulness vs hardware (M4) |
| GPU command set | `GpuCommand*` | Yes | per-command audit (M4) |
| Bpp pixel formats | `Bpp` | Yes | — |
| Blending (translucency) formulas | renderer passes + TMD shaders | Yes | first-pass opaque-pixel rule detail (M4/M5) |
| GTE model (float) | `Gte`/`MV` | Yes | fixed-point divergence (M4) |
| SPU model (voices, ADSR, noise, reverb, 60 Hz tick) | `Spu` + `legend.core.spu.*` | Yes | reverb network constants (M4) |
| XA ADPCM decoder + tables | `XaAdpcm` | Yes | table cross-check (M4) |
| 15→24 color, PSX degrees, rsin/rcos scale | `MathHelper`, segment class | Yes | — |
| ISO9660 + Form2 reader | `IsoReader` | Yes | sector/anchor edge cases (M2) |
| MDEC codec | `Fmv` (VLC/IDCT) | Mostly (codec is standard; container is LoD) | full codec audit (M5) |
| Double-buffered display model | `Gpu` display surface + ROM swap pattern | Yes | — |

This list is a *candidate inventory*, not a scope decision: the Rust project may choose to reuse, re-derive, or drop any of it. `Inferred` confidence for the "self-contained" assessments (verified that each component's inputs are data + the constants shown; full independence audit is M4/M10).

## 4. Boundaries to watch (frequently confused)

1. **"The GPU model" is not "the renderer".** `Gpu` (PSX command-stream model, VRAM, z-queues) coexists with `RenderEngine` (modern GL pipeline, pools, post). LoD objects are drawn by one or the other depending on `legacyMode` and per-object code paths. A compatibility analysis must not conflate them (Q-37).
2. **The exploration rates are LoD, not PSX.** The PSX runs at 60 Hz (NTSC); LoD's ROM code sets the per-context rates itself — 60 Hz for submap/world map/credits (vsyncMode 1), 30 Hz for title/game over/wmap screens (vsyncMode 2), 20 Hz for battle (vsyncMode 3). Any timing claim must cite the specific `vsyncMode` write site, never "PSX frame rate" or a blanket "30 Hz" (audit F-01).
3. **The SPU model is tick-driven (60 Hz), not DMA-driven.** The original SPU is DMA/burst-based; the reference models it as a 60 Hz sample-slot loop. Behaviorally close, architecturally different (M4).
4. **Music is not SPU audio.** The SPU plays SFX; music comes from the SSSQ sequencer (reference hosting) — the original's music path was hardware-sequenced XA/soundfont-style data. (M6.)
5. **Fate is not "PSX scripting".** It is LoD's in-game software VM; only its *natives* touch hardware-ish state. (M3.)
6. **Dabas is not a console feature.** It is an in-game minigame with its own modeled machine. (Q-30.)

## 5. Open items

- Q-20 (GTE divergence), Q-22 (SPU reverb/timing faithfulness), Q-23 (XA tables), Q-37 (render-path faithfulness), Q-30 (Dabas machine provenance), Q-04 (original per-state rates — now strongly supported: decompiled per-state `vsyncMode` assignments make the per-rate structure `Verified`; only external corroboration of NTSC 30 fps vs the ROM's own intent is outstanding — the ROM *is* the source of its own rates).
- Per-constant byte-level classification: M4.
