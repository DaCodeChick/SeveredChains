# M4-04 — XA ADPCM

Status: Complete (M4, 2026-11-04). Reference: Severed-Chains @ `0c08c679b`, read-only. This document covers the **XA ADPCM codec as implemented in the reference** (format geometry, predictor, rounding, resampling, state management) and cross-references M2 for the asset-pipeline side (SSSQ/SSHD/LODXA containers: `research/assets/10-music-formats.md` §3-4; the Opus transcode path: `research/assets/09-fmv-mdec.md` §5; SPU voice ADPCM: `research/psx/03-spu.md` §3.1). Evidence vocabulary: **source-verified**, **format-corroborated**, **experimentally verified**, **asset-validated** (*none exists — no such claim is made*), **Inferred**, **Unresolved**. Layers: (1) PSX, (3) LoD, (4) reference. No retail XA bytes exist in this environment — the worked example uses synthetic nibbles chosen for traceability, not retail data (per the task).

## 1. Sector geometry (source-verified, `XaAdpcm.java`)

- **Header:** `BYTES_PER_HEADER = 24` is skipped before any block data — "skip sync, header and subheader" (in-source comment). The **plan's 24-byte header is verified** as the constant actually used. The **coding-info byte is at offset 19** (last byte of the subheader) — source-verified at the call site: `Fmv.java:421` passes `data[19]`; flags parsed from it: bit 0 = stereo, bit 1 = 18900 Hz, bit 4 = 8-bit-per-sample (`XaAdpcm.decode`, source-verified).
- **Sector:** 0x12 (18) × 128-byte (0x80) portions = **2304 (0x900) data bytes**; the in-source comment states the remaining 0x14 bytes of the 914h sector data region are 00h-filled (source-verified comment; the comment itself is the reference's record of the hardware sector size — hardware attribution external, Q-22/Q-23).
- **Per 128-byte portion:**
  - bytes 0–3: not read (per-portion sync in the PSX XA layout — `Inferred` layer-1 from the geometry).
  - bytes `4 + blk·2 + lane`, blk = 0..3, lane = 0/1: **8 control bytes** — one per (block, channel-lane); byte layout: **shift = 12 − (byte & 0x0F)**; **filter = (byte & 0x30) >> 4 — a 2-bit field (0–3)**.
  - bytes `16 + blk + i·4`, blk = 0..3, i = 0..27: **112 sample bytes** — sample *i* of block *blk* sits at 4-byte stride; **low nibble = lane 0 (L), high nibble = lane 1 (R)**; 28 samples/block → 112 samples per portion per lane → **2016 samples per sector per lane** (= 0x7E0).
- **Rate corroboration:** 2016 samples/sector is exactly the PSX XA sector audio count (a CD sector ≈ 53.3 ms of 37.8 kHz audio). At the reference's FMV source rate of **37800 Hz** (below), 2016/37800 = 53.33 ms — the geometry is **format-corroborated** (source geometry × in-repo rate × the well-known sector cadence), with the hardware attribution external (Q-23).

## 2. ADPCM core (source-verified, `XaAdpcm.decodeNibbles`)

- **Coefficients:** `positiveXaAdpcmTable = {0, 60, 115, 98, 122}`, `negativeXaAdpcmTable = {0, 0, −52, −55, −60}` — **identical to the SPU voice decoder's tables** (`research/psx/03-spu.md` §3.1) and to `XaTranscoder`'s (M2-09). One codec core, three consumer decoders.
- **Per sample:** `t = signExtend4(dataByte >> lane·4)`; `s = (t << shift) + (old·f0 + older·f1 + 32) / 64`; `sample = clamp(s, −0x8000, 0x7FFF)`.
  - **Rounding/saturation (source-verified, reference behavior):** the `+32` before `/64` is a round-toward-+∞ bias; Java integer division truncates toward zero, so negative numerators round half toward zero — the reference's rounding is **not symmetric** for negative values. Saturation is `Math.clamp` to the s16 range (both ends). Whether the PSX SPU's hardware rounding/saturation matches this exact scheme is **Unresolved** (Q-23 external).
  - **Filter field width differs by decoder:** XA music/FMV = **2 bits (0–3)** here; the SPU voice decoder (`Voice`) uses **3 bits (0–4)** with an in-source overflow-clamp note (Crash Bandicoot's filter-7, M4-03 §3.1). Documented, not reconciled — the decoders are separate consumers (C-01-style drift, layer 4).
- **Predictor state:** `old/older` per lane, updated in order `older = old; old = sample` per sample (source-verified).
- **Signed 4-bit:** `value << 28 >> 28` (arithmetic shift sign-extension, source-verified; identical in `XaTranscoder`).

## 3. Channel handling and output layout (source-verified, `XaAdpcm.decode`)

- Stereo: lane 0 → L list, lane 1 → R list, then **de-interleaved** to L,R,L,R… output.
- **Mono:** the same file is decoded as **two separate mono streams** (lane 0 and lane 1 each with their own `old/older` — the in-source note in `decodeNibbles`'s callers: `l.addAll(decodeNibbles(..., 1, oldLRef...))` when not stereo) and the **L stream is duplicated** for both output channels ("duplicating because out output expects 44100 Stereo" — in-source comment; note the typo "out" is in the source). This matches M2's finding that LODXA files interleave **16 mono tracks** (15 used, track 0 discarded) — the codec is mono-stream-native; "stereo" is two adjacent mono streams (M2-10 §3; layer 3/4).
- **No resampling in `decode()`**: output is at the file's native rate; the `is18900hz`/`is8BitPerSample` flags are parsed but **unused** (only referenced in a commented-out `Console.WriteLine` — a C#-port trace, source-verified).

## 4. The 6→7 zigzag FIR resampler — implemented but **dead code** (source-verified)

- `resampleTo44100Hz` (XaAdpcm.java): each input sample enters a **per-channel 32-slot ring buffer** (`resampleRingBuffer[2][32]`, pointer masked `& 0x1F`); the static **`sixStep = 6`** (the plan's constant, verified as written) counts down per input sample; at zero it is **reset to 6** and **7 output samples** are produced — one per table of the **7-phase × 29-tap** `zigZagTable` (short coefficients, each tap weighted `× coeff / 0x8000`, sum **clamped to [−0x8000, 0x7FFF]**; taps read `ring[pointer − i]`, i = 0..28, i.e. including the just-written sample — the 32-slot depth covers the 29-tap lookbehind).
- **Ratio:** 6 in → 7 out = **×7/6**. With the FMV source rate of 37800 Hz (§5): 37800 × 7/6 = **44100 — the method name is arithmetically consistent**: this resampler upsamples 37.8 kHz XA to 44.1 kHz (`Inferred` rate attribution: source-verified constants, arithmetic corroboration; the in-source `//todo handle 18900hz` marks the half-rate case as unhandled).
- **Dead code (source-verified by full-repo grep):** **no caller** of `resampleTo44100Hz` (nor `zigZagInterpolate`) exists anywhere in the reference — the only `XaAdpcm` consumer is `Fmv.java:421` via `decode()`, which returns **native-rate** samples. The ring buffers, `sixStep`, and `resamplePointer` are static state that is never exercised in the running game. Consequences: (a) the 44.1 kHz output of the XA music/FMV path in the reference does not use this FIR (FMV plays at 37.8 kHz, §5); (b) the statics (like the predictor state, §5.2) would be shared/corrupted if the path were ever wired up; (c) for the reimplementation, the FIR is documented as an **available-but-unwired reference asset** — porting it is a design choice, not a fidelity obligation (Proposed, layer 4).

## 5. Worked example — FMV audio sector, full path (source-verified)

1. `Fmv` demux: each 2352-byte (0x940) sector read is classified by its `SectorHeader.submode`; `TYPE.AUDIO` sectors go to `XaAdpcm.decode(data, data[19])` (`Fmv.java:420-431`, cross-ref M2-09 §5 for the sector header layout).
2. `decode`: skip 24 header bytes; for 18 portions × 4 blocks: read the (block, lane) control byte → shift/filter; decode 28 samples/lane with the §2 recurrence; interleave L,R (stereo) or duplicate L (mono dual-stream). Output: **4032 shorts** per sector (2016 × 2 channels).
3. **Gain staging (source-verified):** each sample `>>= 1` ("Halve the volume" — in-source comment) then `×= volume`, where `volume` is a **float** set to `FMV_VOLUME × MASTER_VOLUME` host-config values (`Fmv.java:229,343,423-426`; layer-4 gain, Java `short *= float` truncation semantics apply).
4. Delivery: `source.bufferOutput(decodedXaAdpcm)` under `synchronized(source)` with a `canBuffer()` guard — drops the whole sector's audio if the OpenAL source cannot buffer (backpressure = **drop**, source-verified; divergence annotation: the PSX would stall the video/audio DMA; the reference prioritizes video).
5. The OpenAL source was registered as `new GenericSource(AL_FORMAT_STEREO16, 37800)` (`Fmv.java:342`) — **37.8 kHz native playback** (reference-specific hosting choice, layer 4; the hardware XA rate set is an external item, Q-23).

**Synthetic decode trace (source-derived values, not retail data):** one portion, block 0, lane 0, control byte `0x0C` → shift = 12−12 = 0, filter = 0 (f0 = f1 = 0); first sample byte at `position+16` = `0x05` (low nibble 5) with `old = older = 0` → `s = (5 << 0) + (0 + 0 + 32)/64 = 5 + 0 = 5` → sample 5 (32/64 truncates to 0). Next byte `0x07`, `old = 5`, filter 0 → `s = 7 + (5·0 + 0 + 32)/64 = 7`. (Filter 0 = raw 4-bit-extended samples — a convenient trace case; with filter 1, f0 = 60: `s = (t << shift) + (5·60 + 32)/64` etc.)

## 6. Decoder census: four XA-ADPCM decoders, one math core (source-verified)

| Decoder | Consumer | State model | Filter field | Resampling | Rounding |
|---|---|---|---|---|---|
| `XaAdpcm.decode` (`core/spu/XaAdpcm.java`) | **FMV audio** (`Fmv.java:421`) — the only in-game caller | **static** `old/older` ×2 lanes, **never reset** (persists across FMV files — first 28-sample block of a new FMV starts from the previous FMV's final predictor state; audible impact Inferred: ≤ 28 samples ≈ 0.74 ms at 37.8 kHz per file boundary); static ring/`sixStep` dead (§4) | 2 bits (0–3) | none in `decode()`; 6→7 FIR present but uncalled | `(t<<s) + (old·f0 + older·f1 + 32)/64`, clamp s16 |
| `XaTranscoder.decodeNibbles` (`core/audio/xa/XaTranscoder.java`) | **LODXA → Opus transcode** (offline tooling; M2-09 §5, M2-10 §3) | **per-file instance** state; `reset()` zeroes `old/older[2]` per file, primes 3-sample `EMPTY` lookbehind windows, `interpolationCounter = 160`, `PRE_SKIP = 312` Opus priming (M2-09) | 2 bits (0–3) | own linear-interpolation pipeline (`resampleSourceBuffers`, 80-weight table — M2-09) | same recurrence, same clamp (identical core) |
| `Voice.decodeSamples` (`core/spu/Voice.java`) | **SPU music-stream/voice samples** (M4-03 §3.1) | **per-voice instance** state; `keyOn` resets (`counter.register = 0x2000`) | **3 bits (0–4)** + overflow clamp to 4 | none (fixed 44.1 kHz, 512-step interpolation) | `(t<<s) + (old·f0 + older·f1 + 32)/64`, clamp s16; **nibble layout differs** (sequential-by-nibble per 8-byte address, not the XA portion geometry) |

Synthesis: **one ADPCM math core (POS/NEG tables, +32/64 rounding, s16 clamp), three in-game/offline-tooling consumers** (plus a fourth dev-side decoder, `legend/game/unpacker/midi/SoundbankDecoder`, documented by M2-09 with the identical core — not part of the running game). The reimplementation must choose per-consumer state semantics (the static-never-reset FMV state is a reference quirk, not an obvious design — Q-69); the math core can be shared (Proposed, layer 4).

## 7. Error handling and malformed input (source-verified)

- No bounds checks against the actual `xaadpcm` length — the loop assumes ≥ 24 + 18×128 bytes; a short sector would throw `ArrayIndexOutOfBoundsException` (unhandled crash, source-verified).
- No validation of shift/filter values beyond the table indices (filter > 3 impossible via the 2-bit extraction; shift 0..12 bounded by construction).
- Mono files whose second lane is unused/zero: the lane-1 mono stream decodes as (near) silence from zero nibbles (the predictor of a zero-nibble stream with filter 0 stays 0 — traceable, source-verified).
- The `canBuffer()` drop (§5.4) is the only backpressure behavior; no logging of dropped sectors (Inferred — no log statement at that site).

## 8. Register updates

- **Q-23** (XA resampling): *advanced* — the 24-byte header, 2-bit filter, 2016-samples/sector geometry, +32/64 rounding, and the 6→7 (37800→44100) zigzag FIR are all documented from source with their dead-code status; the FIR's rate attribution is arithmetically corroborated in-repo (FMV source = 37800). Remaining: external PSX XA documentation (hardware rounding, rate set, sector sync bytes) — stays open, M4/M10 + external.
- **Q-69** (new): XA decoder duplication and state-semantics questions: (a) `XaAdpcm`'s **static never-reset predictor state** (cross-FMV-file leak, §6) — reference bug or intentional continuity? asset/behavioral evidence needed; (b) the **uncalled 6→7 resampler** — was the 44.1 kHz FMV path ever intended (the `//todo handle 18900hz` and C#-port traces suggest incompleteness); (c) the **2-bit vs 3-bit filter field** divergence between the XA decoders and the SPU voice decoder; (d) mono "dual-stream" files vs M2's "16 interleaved mono tracks" — the exact track-index mapping for FMV (vs music) XA files. — `Open` — M4/M6 + external/asset evidence.
