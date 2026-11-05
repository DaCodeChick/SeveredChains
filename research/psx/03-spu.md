# M4-03 — Sound Processing Unit (SPU)

Status: Complete (M4, 2026-11-04). Reference: Severed-Chains @ `0c08c679b`, read-only. Refines the R1 boundary pass (`research/psx/01-compatibility-boundaries.md` §2.3) and R1 audit F-02 (SPU synchronization — *this document deliberately does not reintroduce the previously corrected data-race claim*; the verified synchronization model is §2.3). Cross-references: M2 `research/assets/10-music-formats.md` (SSSQ/SSHD/XA formats — not repeated here), M4-04 (the `XaAdpcm` music-stream decoder — a *separate* decoder from the SPU voice ADPCM in `Voice`, §3.1), R1 `research/platform/02-execution-model.md` §6 (audio threads).

Evidence vocabulary: **source-verified**, **format-corroborated**, **experimentally verified**, **asset-validated** (*none exists — no such claim is made*), **Inferred**, **Unresolved**. Layers: (1) PSX platform, (3) LoD-specific, (4) reference-specific. No public PSX SPU documentation was consulted (network-restricted); PSX SPU register/voice-layout attributions below rest on in-source register comments and the ROM state the game writes, marked accordingly.

## 1. Architecture: a re-timed model, not a DMA model (layer boundary)

Comparison note (per the plan, "compare only"): the Dig-Dabas repository's `core/sound/Spu.java` (Dig-Dabas @ `4c1fbfeb`, read-only) is a **different device** — a single-channel **8-bit mono** output buffer with linear upsampling (44.1 kHz, `byte[]` data, no voices/ADSR/reverb/noise). It is the modeled sound peripheral of the in-game Dabas machine, **not** a PSX SPU model, and is out of scope for this document's contracts (Q-30 covers the Dabas machine's provenance).

The reference SPU (`legend/core/spu/Spu.java`) is a **60 Hz tick-driven software mixer** producing 16-bit stereo PCM into an OpenAL source:

- `SOUND_TPS = 60`, `SAMPLES_PER_TICK = BASE_SAMPLE_RATE / 60 = 44100/60 = 735` (`Spu.java:21-22`; `Constants.BASE_SAMPLE_RATE = 44_100`). One `tick()` produces 735 stereo samples (`spuOutput`, 2×735 shorts) and pushes them to the `GenericSource` (`AL_FORMAT_STEREO16`, 44.1 kHz) registered with the audio thread (`Spu.init`, `Spu.java:70-76`).
- **Thread topology (source-verified):** `Audio.soundLoop()` runs on a dedicated **"SFX" thread at 60 Hz** (`Audio.java:1539-1575`: `SOUND_TPS = 60`, `NANOS_PER_TICK = 1e9/60`, 3-tick behind-failsafe) and calls `SEQUENCER.tick(); SPU.tick();` each tick. The 120 Hz `AudioThread` (`AudioThread.java:80`: `nanosPerTick = 1e9/120`) is the OpenAL device/source manager — it does *not* drive the SPU. So the SPU's clock is the 60 Hz SFX thread, sample-accurate to the thread's scheduling (drift/failsafe as coded).
- The original PSX SPU is a DMA/word-clock-driven unit reading from sample memory; the reference's 60 Hz sample-slot loop is a **layer-4 re-timing of a layer-1 device** (R1 boundary §2.3/§4). **Layer-1 attribution of the 60 Hz cadence:** the ROM itself records it — `Audio.initSpu` (`@Method(0x8004b834)`) sets `soundEnv_800c6630.ticksPerSecond_42 = 60` — so 60 tps is **LoD-verified** (layer 3, source-verified); whether 60 tps *is* the PSX SPU's hardware cadence remains external (R1 audit F-16; Q-22).
- **Observable effects of the re-timing (Inferred, layer-4 consequence):** (a) key-on/off edges take effect once per tick, at the tick's first sample slot (§2.1) — up to 1/60 s (22.7 µs… precisely 735 samples ≈ 16.7 ms) later than the moment the game thread called `keyOn`; on PSX the SPU reacts at its own sample clock with register-write latency — the *relative* ordering of key edges vs sample position is preserved, the absolute sub-tick timing is not; (b) noise and reverb advance in lockstep with the 735-sample slots; (c) a dropped SFX thread tick (failsafe) loses 735·n samples of SFX *and* sequencer time together (they share the tick) — music and SFX stay mutually in sync but both jump.

## 2. Voice model (24 voices, edge-triggered keys)

### 2.1 Voices, key on/off, and the 64-bit edge words (source-verified)

- **24 voices** — `Spu.voices = new Voice[24]` (`Spu.java:28`); `Voice` carries the PSX voice register layout in in-source comments: volumeL @0, volumeR @2, **pitch @4** (default `0x1000`), data address @6, **ADSR @8/0xA**, adsr level @0xC, **repeat address @0xE** (`Voice.java:9-38`).
- **Key on/off are 64-bit edge bitmasks**, one bit per voice: `SPU.keyOn(mask)` / `keyOff(mask)` OR-accumulate the bits (`|=`) under the shared monitor; `tick()` snapshots both, **zeroes them once** (`Spu.java:94-98`), and applies them at the **first sample slot of the tick** (subsequent slots see zeroed words) (`Spu.tick`, `Spu.java:84-173`). Consequences: (a) multiple key-on calls for the same voice within one tick collapse to one edge; (b) a voice keyed on and off within one tick keys on then off in the same slot (on is processed before off per voice — source-verified order); (c) edges are 60 Hz-quantized (§1).
- `SPU.clearKeyOn()` zeroes pending on-edges (used when reverb is disabled, `Audio.sssqSetReverbType(0)`, §5).
- Per-voice tick pipeline (`Spu.tick` inner loop, source-verified):

```
edge keyOn?  -> v.keyOn()        (reset, phase = Attack)
edge keyOff? -> v.keyOff()       (phase = Release)
if phase == Off -> latest = 0; skip
sample = noiseMode[v] ? noiseLevel (shared generator) : sampleVoice(v)
sample *= v.adsrVolume >> 15        (15-bit envelope scale)
v.tickAdsr()
v.latest = sample                   (feeds FM of voice v+1)
sumL/R += sample * processVolume(v.volumeLeft/Right) >> 15   (if !muted)
if reverbMode[v] -> reverbSend += same weighted sample
processReverb(sendL, sendR); sumL/R += reverbL/R
clamp sum to s16; * mainVolumeL/R (15-bit) >> 15
out = sum * playerVolume (host config, layer 4)
```

- **Voice allocation is not done in the SPU model.** The 24 voices are statically 1:1 bound to the 24 sequencer note slots: `Audio.playingNotes_800c3a40[24]` (`Audio.java:128`); the game's SFX path (scripts → queue → SSSQ note → voice, §6) and the "free voice" search live in `Audio` (the `playingSoundsUpperBound_03 = 20` limit set by `initSpu`, `Audio.java:1049-1056`). **There is no stealing in the reference** — when all 24 are used, a new note fails to allocate (game-side `Inferred` from the search sites; the original PSX SPU had no stealing either — notes simply don't start — `Inferred` layer-1).

### 2.2 ADSR (source-verified, `Voice.tickAdsr`)

- Phases: `Off → Attack → Decay → Sustain → Release → Off` (`Phase.next()`).
- Envelope value `adsrVolume` ∈ [0, 0x7fff], applied as `sample × adsrVolume >> 15`.
- Per-phase parameters (from the 16-bit `ADSR` register pair `lo/hi` — the PSX ADSR register layout, in-source comments `ADSR.java:14-56`):
  - **Attack:** target 0x7fff; shift = `lo>>10 & 0x1f`; step = `7 − (lo>>8 & 0x3)` (values +7..+4); exponential mode = `lo bit 15`.
  - **Decay:** target = `(sustainLevel+1) × 0x800` (`sustainLevel = lo & 0xf`); shift = `lo>>4 & 0xf`; step −8; **always exponential**.
  - **Sustain:** target 0 (runs until key-off); shift = `hi>>8 & 0x1f`; step `−8+(hi>>6&0x3)` (decrease) or `7−(hi>>6&0x3)` (increase); direction = `hi bit 14`; exponential = `hi bit 15`.
  - **Release:** target 0; shift = `hi & 0x1f` (releaseShift); step −8; exponential = `hi bit 5`.
- **Envelope timing (PSX formula, in-source comment + implementation, source-verified):** `cycles = 1 << max(0, shift − 11)`; `step = stepValue << max(0, 11 − shift)`; if exponential && increasing && level > 0x6000 → `cycles ×= 4`; if exponential && decreasing → `step = step × level >> 15`. The envelope advances at most once per `cycles` output samples (a `adsrCounter` delay). Cycles are counted **at the 44.1 kHz sample clock** (in-source comment) — i.e. per output sample slot, not per tick.
- `keyOn()` resets: old/older/currentAddress = startAddress, adsrCounter = 0, adsrVolume = 0, phase = Attack, **`counter.register = 0x2000`** (sample index 1, fraction 0 — sample 0 exists so the 4-tap interpolator has lookbehind on the very first block).

### 2.3 Synchronization model (R1 audit F-02 — preserved)

`Spu.tick()` holds `synchronized(Spu.class)`; **every** SPU control method (`keyOn`, `keyOff`, `clearKeyOn`, `setMainVolume`, `getMainVolumeLeft/Right`, `setReverbVolume`, `setNoiseMode`, `setReverbMode`, `setReverb`, `mute`/`unmute`, `enableReverb`/`disableReverb`, `setNoiseFrequency`) is `synchronized(Spu.class)` on the **same monitor** (R1 audit F-02, source re-verified against `Spu.java`). The only unsynchronized SPU state write reachable from the game thread is `setPlayerVolume` (a `float`, layer-4 config value) — a benign torn-float risk, not a control-word race. **The earlier R1 "data race" claim was a misread and must not be reintroduced.** Consequence for reimplementation: a single lock (or equivalent atomic handoff) around the edge words and control state is the *contract*; the edge-word accumulate/snapshot/zero pattern is the essential semantics (§2.1).

## 3. Sample generation per voice

### 3.1 XA-ADPCM decoding + interpolation (`Voice.sampleVoice` / `decodeSamples`, source-verified)

- **Block structure:** one voice "address" = **8 bytes**; a decode reads **16 bytes** at `currentAddress × 8` (`spuAdpcm[16]`): 2 control bytes + 14 sample bytes. Byte 0: **shift** (low nibble) + **filter** (bits 5–7); byte 1: **flags** — bit 0 loop-end, bit 1 loop-repeat, bit 2 loop-start (consumed by `sampleVoice`). The 14 data bytes hold **28 4-bit samples, low nibble first per byte** (the in-source note: "the ADPCM decoding is the same as on XA but the layout here is sequential by nibble where on XA [it] is grouped by nibble line" — the music-stream `XaAdpcm` decoder, M4-04, is the sibling with the other nibble order).
- **ADPCM:** `s = (nibble × 2^shift) + (old × f0 + older × f1 + 32) / 64`, clamped to s16, with the shared tables **POS {0, 60, 115, 98, 122} / NEG {0, 0, −52, −55, −60}**, `filter = (byte0 & 0x70) >> 4` (0–4; values > 4 clamped to 4 — the in-source note cites *Crash Bandicoot* setting 7 at the end of its first level, overflow-safe here). This is the same coefficient/shift scheme documented in M2 for the XA music path (`research/assets/10-music-formats.md` §3-4; Q-23's external cross-check covers both decoders).
- **Pitch/stepping:** a 12-bit fractional counter (`Counter.register`): bits 16:12 = current sample index (0–27), bits 11:0 = fraction (4096 steps/sample); the interpolation position is the fraction quantized to **512 steps** (`interpolationIndex = register >> 3 & 0x1ff`). Per output sample: `register += pitch` (the @4 register; default `0x1000` = 1.0 sample/sample = 44.1 kHz output from 44.1 kHz "input sample rate"; the pitch step unit is 1/4096 sample). **Rate attribution caveat:** with 28 samples per 8-byte address and 1.0 pitch, a voice consumes 12,600 bytes/s; the documented PSX XA byte-rate attribution is an external item (Q-22/Q-23) — the *reference* numbers are source-verified, the *hardware* equivalence is not claimed.
- **4-tap cubic interpolation** at the 512-position: weights `0.45·(−t³+2t²−t, 3t³−5t²+2, −3t³+4t²+t, t³−t²)` (Catmull-Rom shape, precomputed `interpolationWeights[512]`, `Spu` constructor). The last 3 samples of the previous block are cached (`lastBlockSample26/27/28`) so the lookbehind works across block boundaries (`Voice.getSample`, source-verified).
- **Looping:** on a 28-sample block boundary (`currentSampleIndex ≥ 28`): index −= 28, `currentAddress += 2`; then the *current* block's flags: loop-end → loop-repeat ? `currentAddress = adpcmRepeatAddress` : `phase = Off, adsrVolume = 0`. Loop-start (bit 2, read at decode time) sets `adpcmRepeatAddress = currentAddress`. (Source-verified; note the flag-word used for loop-end/repeat is the one *just finished*, per the in-source comment.)
- **FM (pitch modulation):** if `channelFmMode` bit v is set and v > 0: `step = (step × (voices[v−1].latest + 0x8000)) >> 15 & 0xffff`, capped at `0x4000` (1.5×). The modulation source is the **previous voice's latest output sample** (centered at 0x8000) — the classic PSX FM-voice-pair behavior, where an odd voice's sample wave scales the even voice's pitch (layer-1 `Inferred` from the in-source "From previous voice" comment + PSX FM convention).
- **Noise mode:** if `channelNoiseMode` bit v is set, the sample is `noiseLevel` from the **single shared 16-bit noise generator** (PSX SPU has one noise generator for all channels — `Inferred` layer-1; the reference has exactly one `noiseLevel`/`noiseTimer`, source-verified). Generator per output sample (in-source PSX pseudocode + implementation, source-verified): `timer −= (noiseFrequencyStep + 4)`; `parity = bit15 ⊕ bit12 ⊕ bit11 ⊘ bit10 ⊕ 1`; if `timer < 0`: `level = level×2 + parity`, reload `timer += 0x20000 >> noiseFrequencyShift` (up to two reloads per sample to cover large steps). `setNoiseFrequency(packed)`: shift = `packed >> 2 & 0xf`, step = `packed & 0x3` (`Spu.java:480-492`).
- **Silence:** a voice with no data decodes `SILENT = {0x77…}` (16 × 0x7 = zero-nibble ADPCM → decodes to a constant) — an unbound voice is quiet, not zero-noise (source-verified).

### 3.2 Volumes and mixing (source-verified)

- **Per-voice volume:** 16-bit register; bit 15 = **sweep mode**, low 15 bits = fixed volume. `processVolume`: **sweep mode returns 0x7fff (full volume) — the sweep envelope is NOT implemented** (`Voice.java:110-115`, in-source "todo handle sweep mode volume envelope"). The game *does* use sweep bits: `Audio.calculateNoteVolume` (0x8004ae94) packs `playingNote._4a << 8 | vol >> 7` into the register when `_4a != 0` — so **sweep-mode notes play at constant full volume in the reference** (behavior gap, source-verified; divergence risk: any SFX using volume sweeps sounds louder/steadier than the original).
- **Main volume:** `setMainVolume(l, r)` — 15-bit values; the game's `setMainVolume` (0x8004c894) converts 8-bit values with a **bit-7 sign convention**: negative = `(v<<7) + 0x7fff` (i.e. a 7-bit signed value scaled to 15-bit with offset).
- **Mix order (per sample slot, §2.1):** sum of 24 voices (16.15 per voice) → clamp s16 → × main volume >> 15 → × host `playerVolume` (CoreMod SFX × master config, layer 4) → 16-bit stereo. No per-voice panning beyond L/R volume; no hardware-style "reverb send is post-volume" distinction — the reverb send uses the **same** per-voice weighted sample as the dry path (source-verified).
- **Reverb in/out:** per-channel `channelReverbMode` mask; `sssqSetReverbVolume(l, r)` (0x8004c558, 0–127 → `<< 8` = 16-bit 7-bit-scaled output gain).

### 3.3 Reverb (source-verified; hardware cross-check = Q-22)

- **Work area:** `reverbWorkArea = new float[0x4_0000]` — **0x4000 = 65,536 FLOATS** (256 KB), not bytes. The plan's "reverb area 0x4000" constant is verified *as a float-array length*; the PSX hardware reverb buffer size is an external item (Q-22). Addresses wrap modulo 2^16 (`& (length−1)`); `reverbCurrentAddress` advances once per **output sample** (44.1 kHz), so the maximum implemented delay is 65,536 samples ≈ 1.487 s.
- **Network topology (in-source comments):** input mix (`vLIN`/`vRIN`) → **same-side** (L→L, R→R) and **cross-side** (L→R, R→L) IIR wall reflections (`vWALL`, `vIIR`) → **4 comb filters** (`vCOMB1..4`) → **2 all-pass filters** (`vAPF1/2`, delays `dAPF1/2`) → output × reverb-output-volume. All state is float; coefficients are loaded from the game's `ReverbConfig` table (`reverbConfigs_80059f7c[type−1].config_02`) via `sssqSetReverbType` (§5), scaled by `/32768` (15-bit config values) with buffer addresses ×8 (`<< 3`, i.e. config addresses in 8-sample units).
- `disableReverb()`/`enableReverb()` gate only the **writes** (`writeReverb` early-returns) — while disabled, reads still sample stale state (source-verified; the game's disable path also zeroes the reverb output volume, §5).
- **Layer attribution:** this is a *reference-specific float IIR network* (layer 4) implementing a *PSX reverb register interface* (the `ReverbConfig` field set — dAPF/mL*/vIIR/etc. — mirrors the documented PSX reverb register family; the mapping is `Inferred` layer-1). **Whether this network sounds like / is topologically the PSX hardware reverb is Unresolved (Q-22)** — needs the external PSX SPU reverb documentation. Note the music path has a **separate** reverb: `AUDIO_THREAD.setReverb(type)` runs an OpenAL reverb for the sequencer (M6) — the dual-reverb split is a reference hosting decision (layer 4).

## 4. Worked example — a script SFX, full path (source-verified)

`scriptPlaySound(0, 5, 0, 0)` from a battle script (`Audio.scriptPlaySound`, 0x8001ab34, `Audio.java:333-340`):

1. **Game thread (frame phase "SPU stuff", M4-01 §4.4 step 11 / R1 execution model §6):** `getSoundFileFromIndex(0)` → `menuSoundFile`; delays scaled by the state's `tickMultiplier` (battle = 1, submap = 2 — M3 cadence); `playSound` creates a `QueuedSound28` in `queuedSounds_800bd110` (initial delay 0).
2. **Same frame, `startQueuedSounds()` (0x8001a4e8):** the queue entry's `initialDelay_24` (0) passes; the internal `playSound(type=3, …)` (Audio.java:295) binds the note to the SSSQ note machinery → a free `playingNotes_800c3a40[v]` slot (the search honors `playingSoundsUpperBound_03 = 20`); the note's sample data (from the SSHD soundbank, M2-10) is pointed into `SPU.voices[v]` (data/startAddress/pitch/ADSR/volumes), and `SPU.keyOn(1L << v)` queues the edge.
3. **SFX thread, next 60 Hz tick:** `SPU.tick()` snapshots the edge at sample slot 0 of 735; voice v: `keyOn()` reset (register 0x2000, Attack), then 735 × [ADPCM block decode as needed (28 samples/8 bytes, 512-step cubic interpolation at pitch 0x1000), ADSR ×, volume ×, noise? no, reverb send? per `channelReverbMode`]; the 735 stereo samples are buffered into the OpenAL source; the 120 Hz AudioThread flushes them to the device.
4. **End of sample:** the block's loop-end flag (no repeat) → `phase = Off` → the voice mutes (`latest = 0`) and the note slot is freed by the game-side bookkeeping (the `keyOff`/stop paths, `Audio.java:1448-1509`).

**Divergence annotations:** step 2→3 edge latency is 60 Hz-quantized (vs PSX sample-clock register latency); step 3 interpolation is float 4-tap (the PSX SPU has its own sample interpolation — the PSX's exact interpolator is an external item, Q-22); sweep volumes (if the note had them) would be full-volume here (§3.2); the reverb network is the reference's float IIR (§3.3).

## 5. Reverb / master / noise game-side contracts (source-verified)

- `sssqSetReverbType(type)` (0x8004c494): type ≠ 0 → `setReverbMode(0)` (no channels routed to reverb by default), `enableReverb`, load `reverbConfigs_80059f7c[type−1]`, `AUDIO_THREAD.setReverb(type)` (music reverb); type = 0 → **`clearKeyOn()`** (drops pending SFX!), reverb volume 0, `disableReverb`.
- `sssqSetReverbVolume(l, r)` (0x8004c558): 0–127 per channel → `<< 8`.
- `setMainVolume(l, r)` (0x8004c894): 8-bit with bit-7 sign → 15-bit offset-scaled.
- `initSpu` (0x8004b834): upper bound 20, `ticksPerSecond_42 = 60` (ROM-recorded cadence, §1), unmute, note-slot clear, `portamentoNote_4e = 120` per voice.
- Fade-out: `soundEnv_800c6630.fadeOutVolL/R` captures `getMainVolume() >>> 7` (Audio.java:1306-1307) — the SFX-fade path scales main volume (M6 detail).

## 6. Register updates

- **Q-22** (SPU fidelity): *advanced* — voice model, ADSR, pitch/stepping, noise, FM, reverb network, threading, and the synchronization model documented with sources; the sweep-volume gap and the 28-samples/8-byte rate attribution are now concrete sub-items; remaining: external PSX SPU documentation cross-check (reverb topology/register mapping, SPU sample clock, interpolator) — stays open, M4/M10 + external.
- **Q-68** (new): SPU reference-specific gaps vs original behavior: (a) **sweep-mode volume envelopes unimplemented** (`Voice.processVolume` returns full volume; the game packs sweep bits via `calculateNoteVolume._4a`) — SFX with volume sweeps are constant-loud in the reference; (b) **no voice stealing** (24 notes 1:1, bound 20 in submap contexts per `initSpu`) — whether the original's allocation differed is Unresolved; (c) reverb float network vs PSX reverb hardware (Q-22 hardware half); (d) 60 Hz edge quantization vs PSX sample-clock key latency. — `Open` — M4/M6 + external/asset evidence — needs: external PSX SPU docs; for (a), a census of sweep-bit usage in retail SSHD data (asset-validated-pending).
