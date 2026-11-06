# M2-09 — FMV / VHS (MDEC) Format and Playback

Phase M2 deliverable 9 of 9. Reference: Severed-Chains @ `0c08c679b` — `legend/game/fmv/*` (`Fmv.java` incl. the MDEC decoder + VLC tables, `SectorHeader`, `VideoSector`, `FrameHeader`, `RumbleData`, `PsxYCbCr_int`, `VideoPlayer`), `core/spu/XaAdpcm.java` (FMV audio decode). All claims **source-verified** unless noted. Layer notes: MDEC/VHS is **PSX hardware** (the MDEC unit decodes these streams in hardware on the console — format-corroborated: no$psx "PSXSPX MDEC", jPSXdec); the reference re-implements the decode in software. The exception classes in `legend/game/fmv` derive from **jPSXdec** (Michael Sabin, BSD-licensed third-party code vendored into the reference — source-verified by the file headers); the LoD-specific parts are the sector demux, playback loop, rumble, and audio routing.

## 1. Disc files (source-verified)

- 18 FMVs on disc under `STR/` as raw sector streams (`.IKI`): `diskFmvs_80052d7c` (ROM global, `Fmv.java:63-69` — anchor corrected in M5-04 reconciliation):
  ```
  DEMOH.IKI, DEMO2.IKI, OPENH.IKI, WAR1H.IKI, TVRH.IKI, GOAST.IKI, ROZEH.IKI,
  TREEH.IKI, WAR2H.IKI, BLACKH.IKI, DRAGON1.IKI, DENIN.IKI, DENIN2.IKI, DRAGON2.IKI, DEIASH.IKI,
  MOONH.IKI, ENDING1H.IKI, ENDING2H.IKI
  ```
- Read raw (`IsoReader.readSectors(raw=true)` because the name ends in `.IKI` — M2-01 §1), so each file is a contiguous run of 2352-byte sectors exactly as on the disc (VHS streams span consecutive disc sectors; the ISO9660 entry's LBA/length are the only addressing — `Inferred` standard VHS layout, format-corroborated).
- Rumble data per FMV: `RumbleData.load(Loader.loadFileSync("SECT/DRGN0.BIN/5721/" + fmvIndex))` (`playCurrentFmv`) — a 0xc-record stream `[i32 frame][u16 initialIntensity][u16 endingIntensity][i32 duration]` terminated by `frame == -1` (`RumbleData.java`).

## 2. Sector structure (source-verified)

Each 2352-byte sector (`SectorHeader.HEADER_SIZE = 24`, then 32 bytes of MDEC subheader, then 2016 bytes of payload):

```
offset  size  field
0x000   12    sync pattern
0x00C   12    CD subheader:
  0x00C+3 = 15   u8   mode
  0x00C+5 = 17   u8   channel
  0x00C+6 = 18   u8   submode:
                     0x80 EOF (final sector of stream)
                     0x40 realtime
                     bits 4-5 (>>>5 & 1) form (1 = FORM2)
                     0x10 trigger
                     bits 1-3 (& 0xe): 2 = VIDEO, 4 = AUDIO, 8 = DATA
                     0x01 end-audio
  0x00C+7 = 19   u8   coding:  0x10 = 8-bit/samples (else 4-bit)
                     0x04 = 18900 Hz (else 37800 Hz)
                     0x01 = stereo
0x018   32    MDEC video subheader (video sectors):
  +4 (28)  u16  chunk number
  +6 (30)  u16  chunk count
  +8 (32)  u32  frame number
  +12 (36) u32  demuxed size (total demuxed frame bytes, from first video sector)
  +16 (40) u16  frame width
  +18 (42) u16  frame height
  +20 (44) u16  uncompressed block count ("number of 32-byte blocks to hold uncompressed MDEC codes")
0x038   2016  payload: video → demuxed frame data; audio → XA ADPCM sector
```

## 3. Demux and playback (source-verified)

`Fmv.play(file, doubleSpeed)`:

- **5 sectors per frame** (`sectorCount = doubleSpeed ? 10 : 5`; comment: "(150|75) sectors per second / 15 frames per second") — the VHS standard 150 sectors/s at 15 fps; `doubleSpeed` (used by the demo loop, `Inferred` from call sites) doubles both.
- **15 fps presentation**: the render callback sets `RENDERER.window().setFpsLimit(15 * gameSpeedMultiplier)` and `PLATFORM.setInputTickRate(15 * …)` per frame.
- Per frame: read `sectorCount` raw sectors; for each: if submode EOF → stop; if `TYPE.DATA` → `VideoSector.readSector` copies the 2016-byte payload into `demuxedRaw[2016 × videoSectorIndex]` (the first DATA sector supplies `demuxedSize`); if `TYPE.AUDIO` → decode XA (below) and buffer into the audio source (synchronized, `canBuffer()`-gated).
- Presentation: MDEC-decode the frame (§4) → RGBA texture (resize if dimensions changed) → rendered as a textured quad in `PERSPECTIVE` mode with projection forced to 320×240 and widescreen allowed (`ALLOW_WIDESCREEN` forced on, `Inferred`: FMVs render letterboxed/scaled); the game's usual render state (render mode, projection size, clear colour, fps limit) is saved/restored around playback.
- Skip: input-bound "FMV skip" action (keyboard/gamepad/mouse, with a 60-frame "press again to confirm" text prompt); `engineStateOnceLoaded_8004dd24 = afterFmvState` set at start (the state transition after the FMV — cross-ref R1 engine-states doc).
- `sssqResetStuff()` + `submapId_800bd808 = -1` at play start (stops SFX sequences, clears submap id).
- Audio source: `AUDIO_THREAD.addSource(new GenericSource(AL_FORMAT_STEREO16, 37800))`; volume = `FMV_VOLUME × MASTER_VOLUME` config.
  - **Observation (source-verified, intent Unresolved)**: `XaAdpcm.decode` *resamples* its output 37800 → 44100 Hz (§5), but the OpenAL source is registered at **37800** Hz — the resampled data would play at 5/6 rate (a whole tone low) if taken at face value. Whether this is a latent reference bug or is masked elsewhere (e.g. `AudioThread` resampling) is not established by the files read; registered (M2-10).
- **FFmpeg path** (`VideoPlayer`): a separate player for **mod-provided videos** (not retail VHS): bytedeco/javacv `FFmpegFrameGrabber`, 48 kHz stereo audio, 60 fps cap, ends when buffered audio loops past the video (lastPosition tracking). Retail FMVs always use the software MDEC path.

## 4. MDEC frame decode (source-verified)

Demuxed frame layout (after the 10-byte `FrameHeader`):

```
0x00  u16  runLengthCodesCount
0x04  u16  width
0x06  u16  height
0x08  u16  compressedCodesSize
0x0A  compressed initial block codes (RLE):
       flag byte, MSB-first bits:
         bit 0 → literal byte follows
         bit 1 → copy: size = (byte & 0xff) + 3 (3..255),
                  offset = (byte & 0x7f) [+ next byte if bit 0x80 set] + 1  (1-based back-reference)
       decompressed size = (width/16 rounded up) × (height/16 rounded up) × 6 × 2
0x0A + compressedCodesSize  AC bit-stream (MPEG1-style VLC, MDEC variant)
```

Per macroblock (16×16), 6 blocks in order **Cr, Cb, Y1, Y2, Y3, Y4**:
- **Initial code** (2 bytes, big-endian 16-bit from the RLE-decompressed table): top 10 bits = signed DC coefficient (`<< 22 >> 22`), bottom 6 bits = block quantizer.
- DC: `coefficients[0] = dc * quantizationMatrix[0]`.
- **AC**: variable-length codes from the embedded MPEG1 run/level table (vendored from jPSXdec) with:
  - **escape code** (13-bit prefix `000001` + 6-bit run + 10-bit level),
  - **MDEC end-of-block** code (10-bit `10` + `MDEC_END_OF_DATA_TOP6/BOTTOM10` — MDEC-specific EOB, unlike MPEG's);
  - dequantize: `coeff * quantizationMatrix[zigzag[vectorPos]] * blockQuant + 4 >> 3` (round-half rounding of /8, matching the PSX MDEC's fixed-point);
  - `vectorPos > 63` → `RuntimeException("Too many AC coefficients codes")`.
- **IDCT**: 2×8 fixed-point PSX cosine IDCT using `PSX_DEFAULT_COSINE_MATRIX` (`Fmv.java:212`) — the PSX MDEC's actual cosine table (format-corroborated as the console's MDEC math).
- **Color conversion**: `PsxYCbCr_int` — the PSX MDEC's YCbCr→RGB integer conversion (including the PSX-specific clamping/shifts).
- Frame padding: odd widths add 2 pixels, odd heights 1 line (source comments at `Fmv.java:784,801,821` — matches MDEC's 2-pixel chroma rounding behavior).
- Chroma: 4:2:0 — `chromaW/H = blockW/H × 8`, luma `×16` per macroblock.

## 5. FMV audio: XA ADPCM (source-verified)

`core/spu/XaAdpcm.java` (shared with the SPU voice path, M2-10 §2):

- Input: one 2352-byte sector (raw); `decode(byte[] sector, byte codingInfo)`:
  - coding bits: `0x1` stereo, `0x4` (bit 2) 18900 Hz, `0x10` (bit 4) 8-bit/sample (FMVs are 4-bit stereo 37800 — `Inferred` from retail VHS, asset-validated only);
  - skips the 24-byte header; 18 × 128-byte portions × 4 blocks × 28 samples (per nibble);
  - ADPCM: 4-bit coefficients, `shift = 12 - (coeffByte & 0xf)`, filter index `(coeffByte & 0x30) >> 4` with tables `POS {0,60,115,98,122}` / `NEG {0,0,-52,-55,-60}`; sample = `clamp((t << shift) + (old*f0 + older*f1 + 32)/64)`;
  - mono files decode **both** nibble halves into the same channel (shared predictor state) — PSX XA "mono" is 2×14 samples/block per channel from one byte stream (source-verified; format-corroborated: PSX XA mono uses both nibbles);
  - output: resampled **37800 → 44100** via a 6:7 zigzag polyphase filter (7 × 29-tap tables, 32-sample ring, "todo handle 18900hz" — **known reference gap**: 18900 Hz XA is not resampled correctly), mono duplicated to stereo ("output expects 44100 Stereo" — source comment);
  - FMV path halves the samples (`>>= 1`) before volume scaling (M2-09 §3).
- The original console decoded XA in **SPU hardware** at 37800 Hz (format-corroborated: PSX SPU XA playback); the reference decodes in software on the audio thread — a behaviorally-equivalent substitution (M4 compatibility-classification input).

## 6. psx-sdk / reusability notes (proposed-layer, no design decisions)

- Reusable PSX side: VHS sector format (MDEC subheader fields), MDEC bitstream (MPEG1-VLC + MDEC escape/EOB), PSX cosine IDCT, PSX YCbCr conversion, XA ADPCM decode + 6:7 resample. A future `psx-sdk` MDEC/XA module is the natural home; the jPSXdec-derived tables should carry their license/attribution.
- LoD-specific side: the 5-sectors-per-frame demux policy, rumble table (DRGN0.BIN/5721/…), 15 fps presentation, the FFmpeg mod-video path, audio routing/volume behavior.
- Layer-1 faithfulness question for M4: the software MDEC decode is *intended* to be bit-exact with the hardware MDEC (same tables); validation requires real FMV sectors (asset-validated only) — cross-ref M4 GPU/MDEC faithfulness questions.
