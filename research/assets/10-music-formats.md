# M2-10 — Music Formats (SSSQ/SSHD/XA) [supplementary]

Supplementary M2 document (the M2 deliverable list covers TMD/TIM/ANIM/MCQ/SSSQ/FMV; SSSQ and the XA music pipeline get a dedicated home here rather than being scattered). Reference: Severed-Chains @ `0c08c679b` — `legend/game/sound/{Sssq,Sshd,Sequencer,VolumeRamp,PatchList}.java`, `core/audio/xa/XaTranscoder.java`, `core/spu/XaAdpcm.java`, `unpacker/midi/{MidiToSssq,SoundbankDecoder,SoundfontWriter}.java`. All claims **source-verified** unless noted.

## 1. SSSQ — LoD sequence format (source-verified)

`Sssq.java` (a `Sshd.Subfile`):

```
0x00  u8   volume
0x01  1B   (unparsed)
0x02  u16  ticksPerQuarterNote
0x04  u16  tempo
0x06  4B   (unparsed)
0x0C  u32  magic 0x7173_5353 ("SSsq")
0x10  16 × ChannelInfo (0x10 bytes each):
      0x00  2B   (unparsed)
      0x02  i8   instrumentIndex   (-1 = none)
      0x03  u8   volume
      0x04  u8   pan      (0x40 = middle; "0 left, 0x7f right" — source comment)
      0x05  4B   (unparsed)
      0x09  u8   modulation
      0x0a  u8   pitchBend
      0x0b  u8   (unparsed)
      0x0c  u8   breath
      0x0d  1B   (unparsed)
      0x0e  u8   volume (second)
      0x0f  1B   (unparsed)
0x110  event stream (MIDI-wire-like, below)
```

### 1.1 Event stream (source-verified, `Sequencer.sssqReadEvent` `@0x8004a848`-region + `sssqReadDeltaTime`)

- **Fixed-width events** with running status: `[command-or-status][param0][param1][param2]` (param2 skipped only for the end-of-track meta `0xFF 0x2F 0x00`); a byte with bit 7 clear repeats the previous command with new params.
- **Variable-length delta time** follows each event: up to 4 bytes, 7 bits per byte, high bit = continuation (standard MIDI varint), in **ticks** (scaled by `ticksPerQuarterNote`/tempo).
- Commands (source-verified dispatch, `Sequencer.tick` `@Method(0x80045cb8L)`):
  | op | meaning |
  |---|---|
  | `0x80` | key off |
  | `0x90` | key on |
  | `0xA0` | polyphonic key pressure |
  | `0xB0` | control change: `0x1` modulation wheel, `0x2` breath, `0x6` data entry, `0x7` volume, `0xA` pan, `0x40` sustain (damper), `0x41` portamento, `0x60` **repeat** (SSSQ extension: "jumps to a different part of the sequence"; sets repeat flag/destination — not MIDI's "data increment"), `0x62/0x63` NRPN LSB/MSB |
  | `0xC0` | program change |
  | `0xE0` | pitch bend |
  | `0xF0` | meta: `0x2F` end of track, `0x51` tempo |
- **Tick engine** (`sssqReadDeltaTime`): ticks → sequencer ticks at `soundEnv.ticksPerSecond_42` (60) using 1-decimal fixed-point with a `longTick` rounding flag (round-half-up to the nearest tick, remainder carried) — original behavior (decompiled).
- 24 sequence channels (`sequenceData_800c4ac8[24]`) — matches the 24-voice SPU model (R1 audio doc); key-on/off are OR-accumulated per channel into `soundEnv.keyOn_3a/keyOff_3e` once per processed frame (cross-ref R1 SPU cadence).

## 2. SSHD — sound container (source-verified)

`Sshd.java` — the per-sound file combining a soundbank, sequence, and envelopes:

```
0x00  u32  sshdSize
0x04  u32  soundBankSize
0x08  4B   (unparsed)
0x0C  u32  magic 0x6468_5353 ("SShd")
0x10  28 × u32 subfileOffsets   (-1 = absent)
```

- Subfile types (source comments + classes): index 1 = `VolumeRamp`, index 3 = `PatchList` ("only used if ptr 4 is also set?"), index 4 = "embedded SSSQ file? Has 24 entries instead of 16. Most header information is 0" — i.e. an SSSQ variant with 24 channel info records (the base SSSQ has 16).
- Subfile size = up to the next subfile offset (computed, not stored).
- Layer: **LoD-specific** (the PSX hardware knows nothing about SSHD; the original runtime parses it in software — `Inferred` from the decompilation's ownership).

## 3. XA music on disc (source-verified)

- `XA/LODXA00.XA` … `LODXA03.XA`: raw XA sector streams (M2-09 §2 sector geometry; ADPCM core per M2-09 §5).
- Interleaving (source-verified, `XaTranscoder.transcode`):
  - **LODXA00/01/02 (mono files)**: **16** interleaved audio tracks per sector set (each 2352-byte sector belongs to one track, round-robin); the skip mask `SKIPS = {0xFFFE, 0xFFFE, 0xEFE, 0x2}` selects which tracks are real audio: for LODXA00-02 (mask `0xFFFE`) tracks 1..15 are used (**track 0 is always discarded** — the transcode loop starts at track 1); each used track → `<path>/<track>.opus`.
  - **LODXA03 (stereo file)**: **4** interleaved tracks, mask `0xEFE` → tracks 1..3 used (3 stereo pairs).
- Runtime: `Audio.playXaAudio`-style loads (`Audio.java:908`): `AUDIO_THREAD.loadXa(Loader.loadFileSync("XA/LODXA0%d.XA/%d.opus".formatted(xaArchiveIndex, xaFileIndex)))` — i.e. **the reference's in-game XA music playback consumes the unpack-time Opus transcodes**, not raw XA (source-verified). The original PSX played XA via SPU DMA hardware at 37800/18900 Hz (format-corroborated; the original's track selection is data-driven by the game — `Inferred`).

## 4. XA → Opus transcode (unpack-time) (source-verified)

`core/audio/xa/XaTranscoder.java` (M2-03 #29, `Unpacker.xaTransformer`):

- Decode: XA ADPCM (same core as M2-09 §5 — `shift = 12 - (b & 0xf)`, 4 filter tables, 28 samples per block call; mono uses the **low nibble only** here, unlike `XaAdpcm`'s SPU path which decodes both halves into one channel — a deliberate difference between the music transcode and the SFX/SPU decode, `Inferred` intent: music files are single-channel-per-nibble, SFX soundbank "mono" is dual-nibble; registered as an open item for asset validation).
- Resample 37800 → 48000: cubic interpolation, 80-step weight table, counter step 63 (output rate = input × 80/63 × … = 48000 exactly), output **halved in amplitude** (`>> 1`).
- Encode: Opus, **48 kHz**, 960-sample frames, 128 kbps, `PRE_SKIP = 312`; the written Opus **container header records 37800** (the source rate) with comments `tracknumber=`, `totaltracks=`, `album=<original path>`.
- Sector end: stream stops when sector byte 18 (submode) has bit `0x80` set (`readUByte(sectorOffset + 18) >>> 7 & 1` — the VHS EOF flag, M2-09 §2).
- Output files: `XA/LODXA0%d.XA/<track>.opus` in the `files/` tree (re-queued nodes; the `.opus` files are what the runtime loads).

## 5. MIDI tooling (unpacker/midi, dev tools) (source-verified)

- `MidiToSssq` (dev tool, `main`): converts a MIDI file into an SSSQ by **copying the first 0x110 bytes of an original SSSQ as the header template**, overwriting tempo at `0x2` (u16), rewriting tempo meta events to `60_000_000 / mpqTempo` (microseconds per quarter), and emitting the MIDI track events as SSSQ commands starting at `0x110`. Confirms: SSSQ event stream is a subset/superset-compatible mapping of MIDI (running status + varint deltas + the enumerated opcodes), and the 0x110 header size is fixed.
- `SoundbankDecoder` (dev tool, `main`): decodes the LoD-soundbank XA samples in `files/SECT/DRGN0.BIN/5820/3` to 18900 Hz mono PCM (console playback) — per-sample blocks carry `shift/filter/loopEnd/loopRepeat/loopStart` flags (XA instrument samples with loop control — the LoD SPU instrument format, `Inferred` scope: soundbank = XA ADPCM samples + loop/shift metadata).
- `SoundfontWriter` (dev tool): writes a SoundFont from the decoded samples (modding workflow: original instrument → modern soundfont).
- Layer: **reference tooling**, not original game data behavior — but the formats they encode/decode (SSSQ event grammar, soundbank XA samples) are original.

## 6. Register — open items created/updated by M2

Continuing the `04-unresolved-questions.md` numbering (Q-44+):

| ID | Question | Status/owner |
|---|---|---|
| Q-44 | No-CLUT TIM layout: `Tim.java` (flat, data @0x14) vs `Unpacker.getTimSize` (image segment @0x14) disagree. Do retail no-CLUT TIMs exist? Which layout is correct? | M2 finding; asset-validated only |
| Q-45 | ISO Form-2 detection: reference tests bit `0x20` of header byte 6 (sector byte 18); Yellow Book control-byte Form-2 bit is `0x04`. PSX-toolchain convention? | M2 finding; external PSX docs |
| Q-46 | DEFF part u32@0x10 ("nextOffset"): exact semantic (end-of-region vs next-part; file-relative vs DEFF-relative) and why u16@0x14 is skipped for type 3 pointer adjustment | M2 finding; asset-validated only |
| Q-47 | XA mono decode discrepancy: `XaTranscoder` (low nibble only) vs `XaAdpcm` (both halves → one channel). Which matches retail XA music vs SFX? | M2 finding; asset-validated only |
| Q-48 | FMV audio rate: resampled 44100 Hz data buffered into a 37800 Hz OpenAL source — latent bug or masked by AudioThread? | M2 finding; runtime test |
| Q-49 | LmbType1/2 animations: parsed but not applied in the reference ("Not implemented"). Do retail models use them? What is original behavior? | M2 finding; M9 + assets |
| Q-50 | `encounters` (S_BTLD, 0x7000 B) and `SUBMAP/savepoint` (0x904 B): no decompiled consumers found — original runtime purpose? | M2 finding; M3/M9 |
| Q-51 | Which disc physically contains `SCUS_944.91`, and the per-disc directory trees (LBA/length, name-collision set under the disc-4 merge) | M2 finding; asset-validated only |
| Q-52 | BPE (LoD variant: size@0x0, marker@0x4, per-block self-describing dictionary) vs documented SCE `BPE_` variants (no$psx) — byte-level identity? | M2 finding; external PSX docs + assets |
| Q-53 | PSX GPRIM primitive-id (mode byte) cross-check: reference bit semantics (lit = bit0 clear, textured = 0b100, quad = 0b1000, gouraud = 0b1_0000, supported = bits5-6 == 01) vs the no$psx GPRIM table; `updateTmdPacketIlen` handles 0x30-0x3d which the renderer rejects | M2 finding; M4 |
| Q-54 | CTMD id low byte 0x41 and `TmdWithId` id upper-16 translucency — full id field semantics across asset types | M2 finding; M4 + assets |
| Q-55 | MCQ vramWidth/Height constraints (the 256-height expectation) and MAGIC_1 vs MAGIC_2 behavioral difference (screen offsets) | M2 finding; M4 + assets |
| Q-56 | SSSQ "24-entry" subfile-4 variant vs base 16-entry: which retail sounds use it? | M2 finding; M9 + assets |
| Q-57 | XA `SKIPS` mask rationale (which interleaved slots carry real audio per LODXA0n) — derived from the original XA index or empirical? | M2 finding; assets |
| Q-58 | `getTimSize` low-word assumption (`F < 0x10000`, segSize < 0x10000): do any retail TIMs exceed it? | M2 finding; assets |

*(Existing Q-36/Q-37/Q-40/Q-42/Q-43 from R1 remain; Q-37 gains the TMD-renderer vs PSX-GPU cross-refs from M2-05 §6 and M2-10 §6.)*
