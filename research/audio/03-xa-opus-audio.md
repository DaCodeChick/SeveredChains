# M6-03 — XA, Opus, and FMV Audio

Scope: the three audio paths that consume on-disc XA ADPCM — the LODXA music/cutscene track pipeline (unpack-time XA→Opus transcode + runtime Opus playback), the FMV audio path (runtime XA decode at native rate), and the layer-4 mod-video path — plus the `playXaAudio` API surface, the interleaving/SKIPS track map, and the full volume-routing consolidation. Companion to M6-01 (architecture) and M6-02 (SSSQ sequencer).

Reference: Severed-Chains @ `0c08c679b` — `legend/core/audio/xa/XaTranscoder.java`, `legend/core/audio/opus/{XaPlayer,OpusFile,OpusHead,OggPage,OpusTags}.java`, `legend/core/spu/XaAdpcm.java`, `legend/core/audio/{AudioSource,GenericSource,AudioThread}.java`, `legend/game/fmv/{Fmv,VideoPlayer}.java`, `legend/game/sound/Audio.java` (`playXaAudio` 0x8002c984), `legend/game/credits/Credits.java`, `legend/game/combat/SEffe.java`. Cross-refs: M2-09 §2/§5 (sector geometry, ADPCM core), M2-10 §3–4 (first-pass, **corrected in place by this document**), M4-04 (XA header, decoder census, dead FIR), M5-04 §4 (FMV presentation, 15 fps cadence), M6-01 (threads, routing domains, A1/A2/A11/A14). All claims **source-verified** unless marked; no retail bytes inspected — no `asset-validated` claims.

## 1. The three XA-consuming paths

| | LODXA music (BGM/cutscene) | FMV audio | Mod video (`VideoPlayer`) |
|---|---|---|---|
| On-disc format | XA sector streams, `XA/LODXA0n.XA` (interleaved multi-track) | XA audio sectors interleaved with MDEC video sectors (per-FMV file) | none (mod-supplied container) |
| Decode | **unpack-time**: XA→PCM→Opus (`XaTranscoder`), cached `XA/LODXA0n.XA/<track>.opus` | **runtime**: `XaAdpcm.decode` per audio sector, inside the 15 fps demux loop (main thread) | FFmpeg `IFrameGrabber` (layer 4) |
| Playback format | Ogg/Opus → 16-bit PCM, 48 kHz, 10 ms ticks (`XaPlayer`) | 16-bit PCM, **37800 Hz stereo** (`GenericSource`), pushed by the demux thread | 16-bit PCM, 48 kHz stereo (`GenericSource`) |
| Source host | single `XaPlayer` on the `AudioThread` | per-FMV `GenericSource` added to the `AudioThread` | per-video `GenericSource` on the `AudioThread` |
| Volume | SFX × MASTER (A11) | FMV × MASTER (M5-04 §4.2) | config volume (M5-04 §4) |
| Layer | format-only replacement of the original XA-hardware path (A2) | reference re-implementation of the original DMA path | pure layer 4 |

The original PSX played all three through SPU XA DMA hardware (layer 1); the reference replaces the music path with a pre-transcoded Opus pipeline and keeps a runtime software decoder for FMV audio. SFX soundbank samples (SPU voices) are the fourth, already-documented consumer (M4-03/M4-04, M6-02 §5.4).

## 2. XA on disc (source-verified; M4-04 cross-ref)

- Sector: 2352 bytes (0x930); 24-byte XA header (M4-04); data region = 18 portions × 0x80 bytes = 0x900 (the remaining 0x14 bytes of the 0x914 data region are zero-filled — in-source comment, both decoders).
- Coding info = **sector byte 19**: bit 0 = stereo, bit 2 = 18900 Hz, bit 4 = 8-bit-per-sample (parsed by `XaAdpcm.decode(xaadpcm, data[19])`; the transcoder hard-codes the rate instead).
- Submode byte 18: bit 7 = EOF (VHS flag — ends the transcode, M2-10 §4); type = DATA vs AUDIO (FMV demux, §5).
- Interleaving: mono LODXA files interleave **16** audio tracks (one 0x930 sector per track, round-robin); the stereo LODXA03 file interleaves **4** tracks (`XaTranscoder.transcode`: `interleaveMode = endsWith("3.XA") ? 4 : 16`; `channelSectorCount = sectorCount / interleaveMode`).
- Per block (0x20 bytes per channel-nibble plane): control bytes @4+blk×2 (shift nibble, 2-bit filter @0x30>>4 — M4-04), 28 samples at @16+blk+i×4 (4-bit, low nibble of each byte first).

## 3. The SKIPS track map (corrected)

`SKIPS = {0xFFFE, 0xFFFE, 0xEFE, 0x2}` (`XaTranscoder`), selected by the archive's trailing digit (`Integer.parseInt(fullPath, 9, 10, 10)` on `XA/LODXA0n.XA`), applied as "track *t* is real audio iff bit *t* of the mask is set", with the loop `for track = 1..interleaveMode−1` — **track 0 is always discarded** (the loop never visits it):

| archive | mask | interleave | real tracks | output |
|---|---|---|---|---|
| LODXA00.XA | 0xFFFE | 16 | 1–15 (all 15) | `1.opus` … `15.opus` (mono) |
| LODXA01.XA | 0xFFFE | 16 | 1–15 (all 15) | `1.opus` … `15.opus` (mono) |
| LODXA02.XA | 0xEFE | 16 | 1–7, 9–11 (10 of 15; tracks 8 and 12–15 skipped) | `1..7.opus`, `9..11.opus` (mono) |
| LODXA03.XA | 0x2 | 4 | **1 only** (single stereo track) | `1.opus` (stereo) |

**In-repo consumer evidence:** `Credits.java:294` plays `playXaAudio(3, 3, 1)` — archive 3, file 1 — i.e. the credits music is the single stereo track of LODXA03, consistent with the 0x2 mask. Battle/stage XA music is script-driven via the `SEffe` op (`SEffe.java:4077`) with archive/file params supplied by game scripts (the in-range guard is file index 1–31, §6).

**Correction to M2-10 §3 (applied in place):** the earlier description assigned the masks as "LODXA00-02 → 0xFFFE (tracks 1–15), LODXA03 → 0xEFE (tracks 1–3, 3 stereo pairs)" — wrong on two counts: the mask follows the trailing digit (LODXA02 → **0xEFE**, LODXA03 → **0x2**), and LODXA03 yields **one** stereo track, not three. **Q-57 advanced** with the corrected map; the *rationale* for the masks (why these slots carry audio) and the reason track 0 is discarded remain asset-validated-pending (they encode the original XA index, which ships on the disc).

## 4. XA→Opus transcode (unpack-time, layer 4)

`XaTranscoder.transform(node, transformations)` — invoked by the unpack pipeline (M2-03 #29):

1. **Setup:** `channels = 2` for `…3.XA` else `1`; `interleaveMode`/`channelSectorCount` as above; per track (mask-gated): `reset()` (encoder `OPUS_RESET_STATE`, zeroed per-channel predictors, primed interpolation counter = 160, lookbehind windows), open an `OpusFile(channels, PRE_SKIP = 312, originalSampleRate = 37_800)` with comments `tracknumber=<track+1>`, `totaltracks=<interleaveMode>`, `album=<path>` — note the **tracknumber comment is off by +1** relative to the `<track>.opus` file name (in-source quirk; comments are ignored by the player).
2. **Sector loop** (`sector = 0..channelSectorCount`): take sector `(sector×interleaveMode + track)`; `processSector`; stop when that sector's submode byte 18 has bit 7 (EOF).
3. **`processSector`:** skip 24-byte header; 18 portions; per portion: roll each channel's 3-sample lookbehind window, then 4 blocks × (`decodeNibbles(nibble 0 → channel 0)` + `decodeNibbles(nibble 1 → channel channels−1)`), then `resampleSourceBuffers()`.
   - **`decodeNibbles`** (per channel): `shift = 12 − (ctrl & 0x0F)`, `filter = (ctrl & 0x30) >> 4` (2-bit, 0–3), the standard recurrence `s = (t<<shift) + (old×POS[f] + older×NEG[f] + 32)/64` clamped to s16, low nibble first. **Both nibble planes are decoded in both modes** — mono: sequentially into the *same* channel (nibble 0's 28 samples, then nibble 1's 28, per block, sharing that channel's predictor state); stereo: nibble 0 = left, nibble 1 = right. **Q-47 corrected:** M2-10's "XaTranscoder decodes the low nibble only" was a misread — `XaAdpcm` and `XaTranscoder` agree on both-nibbles semantics (M2-10 §4 corrected in place).
4. **`resampleSourceBuffers`** (37800 → 48000): 80-step Catmull-Rom (4-point, precomputed weight table), counter increment **+63** per output sample — ratio 80/63 = 48000/37800 exactly; loop bound `112 × 80 × (3 − channels)` samples (mono: 224 input → ~284 output per portion; stereo: 112 → ~142), counter remainder carried across portions; each output sample is **halved** (`>> 1`) before the s16 clamp.
5. **Encode:** Opus, 48 kHz, 128 kbps, 960-sample frames (`OPUS_APPLICATION_AUDIO`); frames appended as Ogg segments.
6. **Container (`OpusFile`):** in-memory Ogg muxer — page 0 = `OpusHead` (channel count, pre-skip 312, **original sample rate 37800**), page 1 = `OpusTags` (comments, filled at `toBytes()`), page 2+ = audio; random serial number; EOS flag on the final page. The recorded 37800 is **informational** — the player decodes Opus sample-rate-independently and plays the buffers at the OpenAL source rate (48000, §5).
7. **Output:** `node.fullPath + '/' + track + ".opus"` queued into the unpack cache (re-queued nodes, M2-03).

## 5. `XaPlayer` — runtime Opus playback (layer 4)

`legend/core/audio/opus/XaPlayer.java` (extends `AudioSource` with an **8-buffer** ring):

- `samplesPerTick = 48000 / 100 = 480` — **one tick per 10 ms** of 48 kHz audio (M6-01 §4 row).
- `loadXa(FileData)` (under `playbackLock`): in-memory `op_open_memory`; channel count re-detected from the Opus header (mono default, **stereo supported** — the credits track is stereo); `sampleCount = op_pcm_total × channels`; **rejects files shorter than 4 buffers (40 ms)** with `RuntimeException("XA file is less than 4 buffers in length (40ms)")`; pre-fills 4 buffers; starts playback.
- `tick()` (audio thread, M6-01 tick-suppression applies): `op_read` 480 samples (per channel), apply `playerVolume` **post-decode in 16-bit fixed point** (`pcm[i] *= playerVolume` — float scale truncated), advance `samplesRead += pcm.length` (per-channel sample count), and **self-terminate** when `samplesRead > sampleCount` (EOF → `op_free`, inactive).
- `playerVolume = SFX_VOLUME × MASTER_VOLUME` at construction (A11 — LODXA music under the SFX knob); re-routable via `AudioThread.setXaVolume` (config).
- **Hosting (M6-01 A1/A14 cross-ref):** a single `XaPlayer` is added to the `AudioThread` (`AudioThread.java:252`). `AudioThread.loadXa` (synchronized on the AudioThread): if the player is initialized → `xaPlayer.loadXa` **directly on the calling (game) thread** (the `playbackLock` serializes it against the audio-thread tick; the 4-buffer pre-fill also runs on the calling thread); otherwise the data is held as `pendingXa` and applied once the device is (re)initialized (`AudioThread.java:141-142`). Unload on stop/reinit/destroy (`:195-200, 361`). The player never ticks when inactive; a new `loadXa` re-arms it (restart-from-top semantics).

## 6. `playXaAudio` / `stopXaAudio` (decompiled — original API surface)

`Audio.playXaAudio(xaLoadingStage, xaArchiveIndex, xaFileIndex)` (`@Method(0x8002c984)`, `Audio.java:898-918`):

- Guard: `xaFileIndex == 0 || xaFileIndex >= 32 || xaLoadingStage >= 5` → no-op (file indices 1–31, stages 0–4).
- **Stage 3** → log "Playing XA archive %d file %d", synchronous `Loader.loadFileSync("XA/LODXA0%d.XA/%d.opus")` + `AUDIO_THREAD.loadXa(...)`, set state flag `_800bf0cf = 4`.
- **Stage 2** → set `_800bf0cf = 4` only (arm without load — the original's async load evidently completed in an earlier stage; stages 0/1/4 are no-ops in the decompilation).
- Callers: the `SEffe` script op (battle effects, `SEffe.java:4077` — archive/file from script params) and the credits' hard-coded `playXaAudio(3, 3, 1)` (`Credits.java:294`). `stopXaAudio()` → `AUDIO_THREAD.stopXa()` (unload).

The staged 0–4 protocol is the decompiled **original game's** XA-request surface (layer 3 API shape); only the terminal stages are preserved (the earlier stages' async load machinery is reference-replaced by the synchronous Opus load — B/C classification below).

## 7. FMV audio (runtime XA decode)

Presentation cadence per M5-04 (15 fps, sector demux inside the render callback on the **main thread**). Audio leg (source-verified, `Fmv.java:342, 420-433`):

1. At FMV start: `source = AUDIO_THREAD.addSource(new GenericSource(AL_FORMAT_STEREO16, 37_800))` — a **37800 Hz stereo** OpenAL source with a 16-buffer ring; at stop the source is released (`:690`).
2. Per demuxed sector with submode `TYPE.AUDIO`: `XaAdpcm.decode(data, data[19])` → gain: `sample >>= 1` then `× volume` (`volume = FMV_VOLUME × MASTER_VOLUME`, M5-04 §4.2) → `synchronized(source) { if (source.canBuffer()) source.bufferOutput(decoded); }`.
   - **No backpressure:** when the ring is full the sector's audio is **dropped** (the video keeps demuxing); drops are possible only under sustained main-thread underrun (the 16-buffer ring at 37800 Hz holds ≫ one 15 fps frame's 224×18×2 samples).
3. **`XaAdpcm`** (`legend/core/spu/XaAdpcm.java`): 18 portions × 4 blocks; nibble 0 → left, nibble 1 → right if stereo else **also left** (dual-nibble mono, shared left predictor — identical semantics to the transcoder); stereo output interleaved L,R; mono **duplicated to pseudo-stereo** (in-source comment: "duplicating because out output expects 44100 Stereo"). Predictor state (`oldL/oldR/olderL/olderR`, `sixStep`, ring buffers) is **static and never reset** — it persists across sectors *and* across FMVs (a new FMV's first blocks decode from the previous FMV's final predictor values; the error decays over the low-order filter's convergence). No per-stream codec-state header is read (the original XA DMA state is not emulated).
4. **No resampling occurs** (M4-04/M5-04 finding, now with the full source picture): the 6→7 zigzag FIR (`resampleTo44100Hz`, 7 tables × 29 taps, ×7/6 = 37800→44100, `//todo handle 18900hz`) has **no caller** — dead code. The `is18900hz` flag (coding-info bit 2) is parsed but unhandled: **a retail FMV coded at 18900 Hz would play at 2× speed in the reference** (Q-79, new).
5. **Q-48 closed (source-verified):** the FMV source is registered at 37800 Hz and the data is 37800 Hz native — the "44100 data into a 37800 source, whole tone low" hazard named in Q-48 does not arise (that hazard belonged to the *uncalled* 44.1 resampler's intended path). The original PSX mixer's gain shaping remains Q-73 (M5-04).

## 8. `VideoPlayer` (mod video, pure layer 4)

`legend/game/fmv/VideoPlayer.java` (boot-intro/mod convenience, M5-04 §4): FFmpeg `IFrameGrabber` decodes a mod-supplied container; audio is fully **pre-buffered** (`duration × sampleRate × channels × 2` + 4 KB, volume-scaled) into a `GenericSource(STEREO16, 48_000)` — if the container's audio is not 48 kHz the source rate does not match the decoded rate (an in-source latent mismatch, layer 4, mod path only); video ends when the buffered audio loops (in-source comment: "Used to end video when buffered audio loops"). Not a game-code path; documented for completeness.

## 9. Volume-routing master table (consolidated)

| stream | knob chain | source anchor | layer |
|---|---|---|---|
| SFX (SPU voices) | SFX × MASTER (15-bit SPU domain) | M6-01 §5 | 4 (domain split is decompiled) |
| BGM (music engine) | MUSIC × MASTER (8-bit music domain) | M6-01 §5 | 4 (domain split is decompiled) |
| SSSQ sequence volume | per-song 7-bit (BGM) / per-channel 0..128 (SFX) | M6-01 §5, M6-02 §3/§4 | 3 (format) |
| LODXA music (XaPlayer) | **SFX × MASTER** | `XaPlayer` ctor, M6-01 A11 | 4 (routing choice) |
| FMV audio | **FMV × MASTER** | `Fmv` volume, M5-04 §4.2 | 4 (routing choice) |
| Reverb (music) | NRPN 0x10 (v/128 L=R) | M6-02 §4.5 | 3/4 |
| Reverb (SPU) | NRPN 0x0F/0x10 per M6-01 §5.4 | M6-01 | 3 (decompiled) |
| Mod video | config volume | M5-04 §4 | 4 |

## 10. Compatibility classification (C1–C12)

| ID | behavior | layer | classification |
|---|---|---|---|
| C1 | LODXA music: unpack-time XA→Opus transcode + runtime Opus decode replaces the original SPU XA DMA playback of the same tracks | 4 | Format-only replacement (M6-01 A2, now with full transcode spec); audibly-equivalent pending asset check (Q-57 residual) |
| C2 | Opus header records original rate 37800 + pre-skip 312; playback at 48000 (source-buffer rate) | 4 | Container metadata is informational; no rate conflict |
| C3 | SKIPS mask per archive (0xFFFE/0xFFFE/0xEFE/0x2) and 16-vs-4 interleave | 3 (format fact) | Source-verified in the transcoder; rationale asset-validated-pending (Q-57) |
| C4 | Track 0 always discarded (loop starts at 1); file names 1-based; `tracknumber` comment off by +1 | 3 / 4 (comment) | Source-verified; comment is a dev-tool quirk |
| C5 | `XaAdpcm` static never-reset predictor state (persists across FMVs) | 4 (reference quirk) | Source-verified; original's per-stream state handling Unresolved (Q-69a) |
| C6 | 6→7 zigzag FIR (37800→44100) uncalled — dead code | 4 | Resolved (Q-69b): the 44.1 FMV path was never wired in the reference |
| C7 | 18900 Hz coding flag parsed but unhandled (`//todo`) — 2× speed if present in retail FMVs | 4 (gap) | Q-79 (new), asset-validated-pending |
| C8 | FMV audio produced on the main thread inside the 15 fps demux loop (original: SPU DMA hardware, independent of frame rate) | 4 | Architectural substitution; timing coupled to the frame loop in the reference |
| C9 | FMV audio drop-on-full ring (no backpressure) vs the reference's tick-suppressed sources elsewhere | 4 | Source-verified behavior; original: DMA flow control |
| C10 | `playXaAudio` staged 0–4 protocol, only stages 2/3 preserved (synchronous Opus load replaces the original async load) | 3 (API shape) / 4 (implementation) | Decompiled contract; earlier stages' behavior Unresolved |
| C11 | XA-volume routing (LODXA under SFX, FMV under FMV knob) | 4 | Routing choice (M6-01 A11) |
| C12 | `VideoPlayer` FFmpeg path incl. the 48000-source/native-rate latent mismatch | 4 | Mod path only |

## 11. Register — items closed/advanced/created by M6-03

- **Q-47 (closed — source-verified):** the named discrepancy did not exist. `XaTranscoder` decodes **both** nibble planes in both modes (mono: sequentially into one channel with shared predictor state; stereo: L/R) — identical semantics to `XaAdpcm`. M2-10 §4 ("low nibble only") corrected in place. Residual: whether retail mono XA is genuinely dual-nibble content is subsumed by R-01 (no independent question).
- **Q-48 (closed — source-verified):** the FMV source is registered at 37800 Hz (`Fmv.java:342`) and `XaAdpcm.decode` emits 37800 Hz native samples with **no resampling** — the "whole tone low" hazard does not arise. Original mixer gain shaping: Q-73.
- **Q-57 (advanced):** per-archive mask/interleave/track map corrected from the transcoder source (§3) with in-repo consumer evidence (credits → archive 3, file 1). M2-10 §3 corrected in place. Remaining: the masks' rationale and the track-0 discard (they encode the original XA index) — asset-validated-pending.
- **Q-69 (advanced):** (b) **resolved** — the 6→7 zigzag FIR has no caller (dead code; `//todo handle 18900hz`); (a) precisely characterized — static never-reset predictors, cross-FMV persistence, bounded by filter convergence (original's per-stream state Unresolved); (c) confirmed as a source fact — XA decoders use the 2-bit filter (0–3) while the SPU voice decoder clamps a 3-bit field to 0–4 (whether retail XA ever uses filter 4: asset-validated-pending); (d) FMV XA is a **single stream per FMV file** (not multi-track interleaved) — the track-index mapping applies only to the LODXA archives. (a)/(c) remain open.
- **Q-79 (new):** retail FMV XA 18900 Hz coding. `XaAdpcm` parses the 18900 flag (coding-info bit 2) but never handles it (`//todo`); a 18900 Hz retail FMV stream would play at 2× speed in the reference. Needs: retail FMV XA header inspection (asset-validated-pending; M10 worklist).
- **M2-10 in-place corrections (applied with this document):** §3 SKIPS/interleave/track map (§3 above); §4 mono-nibble claim (§4 above). M2-10 §6's Q-47/Q-57 rows superseded by the register entries above.

Carried to the M6 cross-system reconciliation (after M6-03): the M1 thread/frame-timing intersection (FMV audio on the main 15 fps loop vs the 120 Hz audio thread), the M2 music-format closure notes (M2-10 corrections done; M2-03 transcode queueing cross-ref), the M4 SPU/XA contract lines (X4/X7 decoder-census rows gain `XaPlayer`/`OpusFile` as the Opus leg), and the M5 FMV-timing hand-off (Q-48 closed, Q-73/Q-79 residuals).
