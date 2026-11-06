# M6-02 — SSSQ Sequencer, Soundbanks, and MIDI Conversion

Scope: the byte-level SSSQ/SSHD sequence formats, the event grammars and sequencer state machines of **both** SSSQ players (the decompiled game SFX sequencer and the reference-authored music engine), the shared soundfont/soundbank/instrument/layer/breath/ADSR data formats, the dev MIDI↔SSSQ conversion tooling, and worked command-stream examples. Companion to M6-01 (architecture, threads, clock domains, volume/routing) — this document re-uses its citations without re-deriving them.

Reference: Severed-Chains @ `0c08c679b` — `legend/game/sound/{Sequencer,Audio,Sssq,Sssqish,Sshd,Sequence,SssqReader,SequenceData124,SoundEnv44,PlayingNote66,Instrument,InstrumentLayer10,PatchList,VolumeRamp}.java`, `legend/core/audio/sequencer/**`, `legend/core/audio/{Constants,EffectsOverTimeGranularity,PitchResolution,InterpolationPrecision}.java`, `legend/game/unpacker/midi/{MidiToSssq,MidiWriter,SoundbankDecoder,SoundfontWriter}.java`. Cross-refs: M2-10 (format first-pass), M4-03 §3 (SPU ADPCM), M4-04 (XA/ADPCM consumers), M6-01 (architecture, Q-74). All claims **source-verified** unless marked otherwise; no retail bytes were inspected — no `asset-validated` claims.

## 1. Two players, one container

The SSSQ sequence format is consumed by two independent players in the reference:

| | SFX player (game) | Music player (core) |
|---|---|---|
| Class | `legend/game/sound/Sequencer` (decompiled, original) | `legend/core/audio/sequencer/Sequencer` (reference-authored) |
| Thread | SFX thread, 60 Hz (`Audio.soundLoop`) | Audio thread, 120 Hz min-pace (M6-01 §4) |
| Note events | **0xA0 polyphonic key pressure** (on: velocity ≠ 0; off: velocity 0) | **0x80 key off / 0x90 key on** (velocity 0 on 0x90 also = key off) |
| 0x80/0x90 | **`throw new RuntimeException("No longer implemented")`** (`Sequencer.java:347-353, 772-774`) | Fully implemented |
| 0xA0 | Fully implemented (the only note path) | No handler (parser rejects it) |
| Sequences | Up to 24 concurrent, per-channel, one SSHD subfile-4 ("Sssqish") at a time | One song at a time, 16 channels |
| Voices | 24 `SPU` voices via `findMatchingOrFreeNote` (M6-01 §3/§9; Q-68(b)) | `voiceCount` float voices (config), free → low-priority → oldest → **throw** |
| Output | 16-bit 44.1 kHz mono → `Spu` → OpenAL | 32-bit float stereo 48 kHz → OpenAL |
| Event delta unit | SSSQ ticks → 60 Hz game ticks (8.0 ms/tick at the pinned 60/480 tempo, §3.8) | SSSQ ticks → **samples** (sample-accurate, §4.5) |

**Key architectural fact (Inferred):** retail SFX sequences express both note-on and note-off through 0xA0 (velocity 0 = key off, `sssqHandlePolyphonicKeyPressure` → `keyOffMatchingNotes`). If a retail SFX stream contained 0x80/0x90 events, the SFX thread would throw on the first one (caught by the `soundLoop` catch-all, M6-01 §2.3 — sequencer state left mid-tick). The presence of a complete 0xA0 on/off path with per-instrument layer selection and note-targeting therefore indicates the retail SFX grammar is 0xA0-centric; the 0x80/0x90 stubs are decompilation gaps of code that retail SFX never reaches. The BGM grammar is the MIDI-derived one (0x80/0x90), which retail BGM uses. No retail byte stream was inspected, so this split is `Inferred` from the two parsers' complementary coverage: each player fully implements exactly the note family the other does not, and each *rejects* (throws) the other's family.

Shared between the players: the 0x110-byte SSSQ header container (§2), the 16-byte `ChannelInfo` record (§2.3), the SSHD container (M2-10 §2), the instrument/layer records (§5.2–5.3), the SPU-ADPCM soundbank (§5.4), the velocity ramp and breath tables (§5.7), and the 60/480 default (tpqn/tempo) pair.

## 2. SSSQ container and headers

### 2.1 16-channel BGM SSSQ (standalone file)

`legend/game/sound/Sssq.java` (also parsed by the music engine's `BackgroundMusic`):

```
0x00  u8   volume_00          (base sequence volume, 0..128 scale; music: /128.0)
0x01  1B   (unparsed)         (music: `Channel` index byte, see §2.3)
0x02  u16  ticksPerQuarterNote
0x04  u16  tempo              (BGM: BPM — §4.5; SFX: see §3.8)
0x06  4B   (unparsed)
0x0C  u32  magic 0x7173_5353 ("SSsq")
0x10  16 × ChannelInfo (0x10 bytes each)
0x110  event stream (per-player grammar, §3/§4)
```

The music engine reads the same file directly (`BackgroundMusic.java:66-110`): `volume = u8(0)/128`, `tpqn = u16(2)`, `tempo = u16(4)`, 16 × `Channel` from `sssq.slice(0x10 + c*0x10, 16)`, event stream from `sssq.slice(0x110)`, songId = `u16(0)` of the *package header file* (not the SSSQ).

### 2.2 24-channel SFX "Sssqish" (SSHD subfile 4)

`legend/game/sound/Sssqish.java` — no magic, no tpqn/tempo fields (M2-10: "most header information is 0"):

```
0x00  u8   volume_00
0x01  0xF  (unparsed)
0x10  24 × Sssq.ChannelInfo (0x10 bytes each)
0x190  Entry2List (instrument table):
       0x00  u16  count_00
       0x02  (count+1) × i16  entryOffsets (relative to 0x190; -1 = absent)
       then instrument records (size = next sorted offset − this offset;
             elementCount = (recordSize − 0x8) / 0x10)
```

The sequence data is **not** at a fixed offset: `Audio.getSequence` (`Audio.java:936-968`) walks `SSHD` subfile 3 (`PatchList`): `patches_02[patchIndex].sequences_02[sequenceIndex]` → `Sequence(FileData, offset)`, a reader over the SSHD at an arbitrary offset. A single SFX sound (one `PlayableSound0c`/SSHD) may therefore carry many sequences grouped in patches, and the queued-sound API selects `(patchIndex, sequenceIndex)`. If subfile 4 or the patch/sequence is missing, `getSequence` logs "Failed to find sequence" (source comment: "Known to happen in Archangel sword attack") and the sound does not start. **Q-56 advanced:** the 24-entry variant is the *only* SFX sequence carrier (every SFX start goes through `getSequence` → requires subfile 4); the 16-entry standalone SSSQ is the BGM carrier.

### 2.3 ChannelInfo — dual interpretation of the same 16 bytes

| offset | SFX (game, decompiled) | Music (reference) |
|---|---|---|
| 0x00 | (unused) | (unused) |
| 0x01 | (unused) | `index` (u8 — the channel's own index; used for logging and voice-channel matching) |
| 0x02 | `instrumentIndex` (i8, −1 = none; written by 0xC0 program change, §3.3) | `instrumentIndex` (i8, −1 = none) |
| 0x03 | `volume_03` (u8; per-voice `volume_34` at note-on) | `volume` (u8/128) |
| 0x04 | `pan_04` (u8; 0x40 middle per source comment) | `pan` (u8) |
| 0x05–0x08 | (unparsed) | (unparsed) |
| 0x09 | `modulation_09` (u8) | `modulation` (u8) |
| 0x0A | `pitchBend_0a` (u8; 0x40 center; reset by 0xC0/0xA0) | `pitchBend` (u8 → (v−0x40)×2 fine-pitch units) |
| 0x0B | `_0b` (written to 0x40 by 0xC0; no other consumer found) | `priority` (u8; 0x7f = high-priority tier, §4.4) |
| 0x0C | `breath_0c` (u8) | `breath` (u8 → fixed-point period, §5.7) |
| 0x0D | (unparsed) | (unparsed) |
| 0x0E | `volume_0e` (u8; per-voice `channelVolume_28` at note-on — the *second* volume) | (commented out: `adjustedVolume`) |
| 0x0F | (unparsed) | (unparsed) |

The two interpretations agree on the shared fields; the music player's use of 0x01/0x0B is a reference-author extension (no decompiled consumer exists for 0x01, and 0x0B's only SFX write is the 0xC0 reset). `Inferred` that retail ChannelInfo 0x01 equals the array position (the music engine's channel matching would otherwise be meaningless).

## 3. SFX event grammar (decompiled — original behavior)

### 3.1 Event framing

`Sequencer.tick` (`@Method(0x80045cb8)`), `sssqReadEvent` (`@0x8004a848`, `Sequencer.java:1406-1426`):

1. Read byte 0. If bit 7 set → new status byte: `command = previousCommand = byte0`. If bit 7 clear → **running status**: `command = previousCommand`, the reader rewinds, and byte 0 becomes `param0`.
2. `param0 = byte1` (or byte 0 under running status), `param1 = byte2`, `param2 = byte3`.
3. **Exception:** for the exact end-of-track event `FF 2F 00`, `param2` is *not* read (the event is 3 bytes, not 4).
4. After every dispatched event, `sssqReadDeltaTime` (`@0x8004a5e0`) reads a **trailing** delta varint: up to 4 bytes, 7 bits each, LSB-first, high bit = continuation (same varint encoding as the BGM stream and standard MIDI).

The delta therefore follows the payload — the reverse of MIDI's leading-delta convention. Per-family byte counts before the varint (from the handlers' `advance(N)` calls):

| family | bytes (after byte 0) | fields |
|---|---|---|
| 0x80/0x90 (unimplemented) | 3 + varint | note, velocity (payload shape only — handler throws before consuming) |
| 0xA0 note on/off | 4 + varint | note, velocity, **instrument** (param2) |
| 0xB0 CC 0x01 modulation | 5 + varint | cc=1, value, nextCommand(instrument), note |
| 0xB0 CC 0x02 breath | 5 + varint | cc=2, value, nextCommand, note |
| 0xB0 CC 0x06 data entry | 3 + varint | cc=6, value |
| 0xB0 CC 0x07 volume ramp | 6 + varint | cc=7, time, newVolume, nextCommand, note |
| 0xB0 CC 0x0A pan ramp | 6 + varint | cc=0xA, time, newPan, nextCommand, note |
| 0xB0 CC 0x40 sustain | (assert false — §8 B2) | — |
| 0xB0 CC 0x41 portamento | 6 + varint | cc=0x41, time, portamentoFlag, nextCommand, note |
| 0xB0 CC 0x60 repeat | 5 + varint | cc=0x60, offsetLo, offsetHi, count |
| 0xB0 CC 0x62/0x63 NRPN | 3 + varint | cc, value |
| 0xC0 program change | 2 + varint | program |
| 0xE0 pitch bend | 2 + varint | value (0x40 center) |
| 0xF0 meta 0x2F EOT | 2 | (no param2, no extra payload; delta still read) |
| 0xF0 meta 0x51 tempo | 4 + varint | meta=0x51, tempoLo, tempoHi (u16 LE), — |

"nextCommand" in the note-targeted CCs is the *instrument index* byte (param2 of the targeting note event, stored as `PlayingNote66.nextCommand_3e`); the match key for CC targeting is (`playableSound`, nextCommand/instrument, `noteNumber`, and for modulation also `sequenceData`) — §3.6.

### 3.2 Per-tick state machine

```
tick():  synchronized(Sequencer.lock)                     [M6-01 §3]
  sssqFreeFinishedNotes()                                  // §3.6
  for ch in 0..23:                                         // per-channel, independent
    if !sequenceData.soundPlaying: continue
    setActiveSequence(sequenceData):                       // §3.3 0xC0/0xA0 instrument selection
      // reads the CURRENT event's param2 (byte 3) as the instrument index
      // into the Sssqish 0x190 table; selects ChannelInfo[ch]
    while sequenceData.deltaTime == 0:                     // drain events until a non-zero delta
      sssqReadEvent; dispatch (§3.3 table); sssqReadDeltaTime
      (0x60 reads its delta inside the switch and breaks out)
    if soundPlaying: soundEnv.keyOn |= keyOn bits; keyOff |= keyOff bits   // OR-accumulated
    handleVolumeChanging(sequenceData)
    if sequenceData.repeat: reader.jump(repeatOffset); command = repeatDestCommand  // §3.4
  SPU.setNoiseMode(soundEnv.noiseMode) ; SPU.setReverbMode(soundEnv.reverbMode)
  if keyOff bits: SPU.keyOff(bits); if keyOn bits: SPU.keyOn(bits); clear both
  sssqTickEffectsOverTime(); handleFadeInOut()
```

Each of the 24 channels owns a `SequenceData124` with its own reader position, delta state, repeat state, and (effectively pinned) tempo — a sound is a *set of concurrently running channels* over one SSHD. SPU key-on/off are batched once per tick from the OR-accumulated 24-bit bitmasks (M6-01 §4: SPU voices get the note numbers; here the bit index is the voice index).

### 3.3 Event handlers (complete table)

| op | handler | behavior (source-verified) |
|---|---|---|
| 0x80 key off | `sssqHandleKeyOff` (`Sequencer.java:772-774`) | **`throw RuntimeException("No longer implemented")`** — decompilation gap (M6-01 A6) |
| 0x90 key on | `sssqHandleKeyOn` (`Sequencer.java:347-353`) | **`throw RuntimeException("No longer implemented")`** — decompilation gap (M6-01 A6) |
| 0xA0 note | `sssqHandlePolyphonicKeyPressure` (`@0x80046224`) | velocity 0 → `keyOffMatchingNotes` (§3.6). Otherwise: `instrumentIndex = note − instrument.startingKeyPosition_06` (negative → ignored); `instrumentLayerIndex += instrumentIndex` into the current instrument's layer array (cumulative, reset by `setActiveSequence`); `ChannelInfo.pitchBend = 64`; `voiceIndex = findMatchingOrFreeNote(layer.minKey, layer.maxKey, playableSound)` (−1 → reader `advance(4)`, channel marked not-loaded, event dropped); fill `PlayingNote66` (§3.6 field table); flags: 0x1 → `finished=false` (sustained/retriggerable) else `finished=true` (one-shot); 0x20 → modulation on (breath source: 0x40 → instrument's `_05` else layer's `_0e`), initial `modulation=127` |
| 0xB0/0x01 | `sssqHandleModulationWheel` (`@0x8004906c`) | note-targeted: matched voices get `modulationEnabled=true`, `modulation = value` |
| 0xB0/0x02 | `sssqHandleBreathControl` (`@0x80049250`) | note-targeted (channel + nextCommand + note + sound + sequenceData); `breath = 240 / (60 − value×58/127)` (integer; 4..120 ticks) |
| 0xB0/0x06 | `sssqHandleDataEntry` (`@0x80049f14`) | NRPN data entry — switch on `nrpn_11f` (§3.5) |
| 0xB0/0x07 | `sssqHandleVolume` (`@0x80049638`) | note-targeted volume ramp: `newVolume = param2`, `totalTime = time×4×tps/60` ticks, `previousVolume = velocityVolume` (ramp starts from the *velocity* volume, not the current chain) |
| 0xB0/0x0A | `sssqHandlePan` (`@0x80049980`) | note-targeted pan ramp (same shape) |
| 0xB0/0x40 | `sssqHandleSustain` (`@0x80049cbc`) | **`assert false`** — no-op in production (asserts disabled), event consumed (M6-01 A7) |
| 0xB0/0x41 | `ssqHandlePortamento` (`@0x80049480`) | note-targeted: `portamentoChanging=true`, `newPortamento = param2`, `time = time×4×tps/60` ticks; applies `portamentoNote` only while a ramp is already in progress |
| 0xB0/0x60 | `sssqHandleRepeat` (`@0x80049e2c`) | SSSQ-extension repeat — §3.4 |
| 0xB0/0x62 | `sssqDataEntryLsb` (`@0x8004a2c0`) | NRPN LSB — §3.5 |
| 0xB0/0x63 | `sssqDataEntryMsb` (`@0x8004a34c`) | NRPN MSB — §3.5 |
| 0xC0 | `sssqHandleProgramChange` (`@0x80048fec`) | only when `!soundPlaying`: `ChannelInfo.instrumentIndex = value`, `pitchBend = 0x40`, `_0b = 0x40`. **No SFX consumer of `ChannelInfo.instrumentIndex` exists outside the unimplemented 0x80/0x90** — `Inferred`: vestigial selection for the lost key-on/off path |
| 0xE0 | `sssqHandlePitchBend` (`@0x8004a46c`) | `ChannelInfo.pitchBend = value`; immediately recomputes `SPU.voices[i].pitch = calculateSampleRate(rootKey, note, cents×4, pitchBend, multiplier)` for every used voice on this channel/sound/sequence (M6-01 §4 pitch table) |
| 0xF0/0x2F | `sssqHandleEndOfTrack` (`@0x80048f3c`) | `soundEnded = true`, flags cleared (delta=0, pitch-shift/reverb per-sequence state), `soundPlaying = false`; the voice-release pass runs in `sssqFreeFinishedNotes` once no voice references the sequence (§3.6) |
| 0xF0/0x51 | `sssqHandleTempo` (`@0x80048f98`) | `tempo_108 = u16LE(readShort(2))` — **functionally dead during playback**, §3.8 |

### 3.4 Repeat mechanisms (two parallel systems)

**A. CC 0x60 (self-contained loop).** Payload `[offsetLo][offsetHi][count]` (u16 LE reader offset, absolute within the sequence stream). Semantics (`sssqHandleRepeat`):

- `count == 0` → set `repeatOffset`, `repeatDestCommand = byte at offset`, `repeat = _0e6 = true`, **no counter** — infinite loop.
- `count != repeatCounter` → same, plus `repeatCounter++`.
- `count == repeatCounter` → loop finished: counter reset, `repeat = false`.

Applied after the inner event-drain loop: `reader.jump(repeatOffset)`, `command = previousCommand = repeatDestCommand`, and (first application) `soundPlaying = true` (via `_0e6`) or `deltaTime = 0` — i.e. the loop restarts immediately at the destination status.

**B. NRPN 0x14/0x1E markers (the BGM-style loop).** `0x63` MSB `0x14` marks the loop start (`repeatOffset = current reader offset`, `repeatDestCommand = current command`); the count is set by `0x62` (lsbType 0: `param1` = count) or CC 0x06 with NRPN 0; `0x63` MSB `0x1E` decides: `count == 0x7F` → infinite; `repeatCounter < count` → `repeatCounter++`, loop; else finished. The music engine implements *only* this mechanism (as `BackgroundMusic.repeatPosition/repeatCount/repeat`, §4.4) and its parser **throws** on 0x60 — `Inferred` corroboration that retail BGM uses NRPN loops and retail SFX may use either.

### 3.5 NRPN / data-entry state machine

State: `lsbType_11e`, `nrpn_11f`, `repeatCount_11d`, `instrumentIndex_120`, `sustainDirection_122` (per `SequenceData124`).

`0x63` (MSB):

| value | effect |
|---|---|
| 0x00–0x0F | `lsbType = 0x10`, `instrumentIndex_120 = value` (NRPN-selected instrument; 0x62 LSB not consumed for this form) |
| 0x10 | `lsbType = 1` (next 0x62 = NRPN selector LSB) |
| 0x14 | `lsbType = 0`, `nrpn = 0`, mark loop start (§3.4B) |
| 0x1E | `lsbType = 0`; loop-end decision (§3.4B) |
| 0x7F | `lsbType = 2`, `instrumentIndex_120 = 0xFF` (clear NRPN instrument) |

`0x62` (LSB): lsbType 0 → `nrpn = 0`, `repeatCount = value` (loop count); lsbType 1/2 → `nrpn = value`.

CC 0x06 (data entry, `value` = param1), switch on `nrpn`:

| NRPN | effect (source-verified) |
|---|---|
| 0x00 | `repeatCount = value` |
| 0x04 | **write** layer ADSR: `adsrLo = (adsrLo & 0xFF) \| ((0x7F − value) << 8)` — attack (linear) shift |
| 0x05 | same `\| 0x8000` — attack (exponential) |
| 0x06 | `adsrLo = (adsrLo & 0xFF0F) \| (((0x7F − value) / 8) << 4)` — decay shift |
| 0x07 | `adsrLo = (adsrLo & 0xFFF0) \| (value / 8)` — sustain level |
| 0x08 | `adsrHi = (adsrHi & 0x3F) \| ((0x7F − value) << 6) \| (0x4000 − sustainDirection)` — sustain (linear) |
| 0x09 | same `\| 0x8000` — sustain (exponential) |
| 0x0A | `adsrHi = (adsrHi & 0xFFC0) \| ((0x7F − value) / 4)` — release (linear) |
| 0x0B | same `\| 0x20` — release (exponential) |
| 0x0C | `sustainDirection = value > 0x40 ? 0x4000 (increase) : 0 (decrease)` |
| 0x0F | `sssqSetReverbType(value)` (M6-01 §5.4) |
| 0x10 | `sssqSetReverbVolume(value, value)` (M6-01 §5.4) |
| 0x11 | `FUN_8004c690(value)` — **`assert false`** stub (Q-77) |
| 0x12/0x13 | `FUN_8004c5e8(value)` — **`assert false`** stub (Q-77) |
| default | (no action) |

The 0x04–0x0B entries **mutate the in-memory layer's `adsrLo/adsrHi` words** — a live ADSR-override channel. But no SFX note path ever *reads* those words (the decompiled SFX note/voice code has no ADSR consumption; the SPU voice itself has no ADSR stage, M4-03): in the SFX player these writes are observable state changes with no audible consumer — `Inferred`: vestigial from the shared music-format encoding (the music player *does* decode ADSR from the same words, §5.6, though it too leaves the NRPN override unimplemented).

### 3.6 Voice binding and lifecycle

`PlayingNote66` fields set at 0xA0 (offsets per class): `used`, `noteNumber`(0x02), `sequenceChannel`(0x04), `sequenceData` ptr(0x06), `_0a`(priority-order, from `soundEnv._00`), `instrumentIndex`(0x0E), `maxKeyRange`(0x1E)/`minKeyRange`(0x20), `playableSound`(0x22), `patchIndex`(0x24), `sequenceIndex`(0x26), `channelVolume`(0x28 = ChannelInfo `volume_0e`), `instrumentVolume`(0x2A = instrument header byte 1), `velocityVolume`(0x2C = `VolumeRamp.ramp[velocity]`), `instrumentLayerVolume`(0x2E = layer `volume_0b`), initial L/R pan volumes (from `calculatePan(4, 0)` — M6-01 §5.3), `volume`(0x34 = ChannelInfo `volume_03`), `cents`(0x36 = layer `cents_03`), `pitchBend`(0x38 = 64), `pitchBendMultiplier`(0x3A), `breath`(0x3C = ChannelInfo `breath_0c`), `nextCommand`(0x3E = **instrument index**), portamento fields (0x44/0x4E/0x62), `rootKey`(0x40), layer `lockedVolume` (0x4A), `pan`(0x4C = layer `pan_0c`). The volume chain at render time (M6-01 §5.2) multiplies these per-voice values.

- **One-shot vs sustained:** layer flag 0x1 → `finished = false`, `_0c = 1` (retriggerable/looping note, stays bound until key-off); no flag → `finished = true` (released when its SPU voice's `adsrVolume` decays below 16, §below).
- **`keyOffMatchingNotes`** (0xA0 velocity 0): matches used voices by (`playableSound`, `nextCommand`, `noteNumber`, `_0c == 1`); voices of the *same* `sequenceData` are preferred (`matchingNotesThisSequence` fallback `matchingNotesOtherSequence`); matched voices get `setKeyOff` (bit → `SPU.keyOff`) and `finished = true`.
- **`sssqFreeFinishedNotes`** (tick start, `synchronized(Spu.class)` — M6-01 §3): (a) a `soundEnded` sequence whose voices are all gone is torn down (slots cleared, `playableSound` released, env pitch-shift flags cleared); (b) per voice, when `(adsrVolume & 0x7FFF) < 16` and the note `finished`: decrement the env's active counter `_0d`, re-index every higher `_0a` priority order (−1), and clear the note (GH#1719 lockstep zeroing, M6-01 A8).
- **`findMatchingOrFreeNote`** (0x80048000): matching-note retrigger → bound-gated priority steal (bound 20 from `initSpu`, 8 after `initSound`'s `setMaxSounds(8)`; silent note-drop at exhaustion) → round-robin free scan → finished/unused fallback (M6-01 §9; Q-68(b) resolved; M4-03/M4-05 corrected in place).

### 3.7 Ramp timing

Volume/pan/portamento ramp durations are `timeByte × 4 × ticksPerSecond / 60` **game ticks** (at 60 tps: 4 ticks per time unit; `soundEnv.ticksPerSecond_42 = 60`, M6-01 §4). The per-tick decrement and interpolation run in `sssqTickEffectsOverTime` (M6-01 §4.3).

### 3.8 Tempo and tick quantization (settled form of Q-74's SFX leg)

`sssqReadDeltaTime` (`@0x8004a5e0`, `Sequencer.java:1333-1426`) — after reading the varint:

```java
if (sequenceData.soundPlaying_02a) {
    sequenceData.tempo_108 = 60;
    sequenceData.ticksPerQuarterNote_10a = 480;
}
```

`soundPlaying` is set at sequence load (`Audio.loadSoundIntoSequencer`, `Audio.java:982`) and cleared only at EOT/stop — so **every delta read during playback overwrites the stored (tempo, tpqn) pair with (60, 480)**. Consequences, all source-verified:

1. The 0xF0/0x51 handler (`sssqHandleTempo`) stores its u16 into `tempo_108` and is **immediately clobbered** by the delta read of the same event: in-sequence tempo changes are functionally dead in the SFX path.
2. The SFX tick rate is therefore **definitively** `msPerTick = tempo×tpqn×10 / (tps×60)` = 28800×10/3600 = 80 → **8.0 ms per SSSQ tick** (= 1/8 game tick at 60 Hz), regardless of any header/meta tempo content.
3. The conversion carries 0.1 ms remainder with round-half-up (`longTick_10c`): `remainder×2 > msPerTick` → round up and flag a "long tick" (next conversion subtracts one `msPerTick` first) — M6-01 §4.3.
4. Q-74's SFX leg is **narrowed**: the only remaining discrepancy is the in-source *debug log* formula (`delta ms = deltaTime / (tempo×tpqn/60_000)` → 2.0833 ms at 60/480), which is a log-only expression with no runtime consumer; the runtime conversion (8.0 ms) is settled. The BGM leg (tempo = BPM, §4.5) is independently corroborated by the MIDI tooling. Register updated accordingly.

## 4. BGM grammar and sequencer (music player)

### 4.1 `SequenceBuilder` — the reference parser (layer 4, format facts source-verified)

`legend/core/audio/sequencer/assets/sequence/bgm/SequenceBuilder.java` — builds the command list **at load time** (any unsupported byte throws `RuntimeException` and the music fails to load — a build-time, not run-time, failure):

- Running status: bit 7 clear → repeat `previousEvent`; else new status (low nibble = MIDI channel).
- Trailing delta varint (same 7-bit/0x80 continuation encoding as the SFX path).
- Per-event layout (all offsets from the event's status byte):

| status | payload bytes | command |
|---|---|---|
| 0x8n key off | note, **1 skipped byte** (velocity) | `KeyOff` |
| 0x9n key on | note, velocity | `KeyOn` — **velocity 0 → `KeyOff`** |
| 0xBn CC 0x01 | value | `ModulationChange` |
| 0xBn CC 0x02 | value | `BreathChange` |
| 0xBn CC 0x06 | value | `DataEntry` (NRPN data, §4.4) |
| 0xBn CC 0x07 | value | `VolumeChange` (v/128) |
| 0xBn CC 0x0A | value | `PanChange` |
| 0xBn CC 0x62 | value | `DataEntryLsb` |
| 0xBn CC 0x63 | value | `DataEntryMsb` |
| 0xCn program | program | `ProgramChange` |
| 0xEn pitch bend | value | `PitchBendChange` ((v−0x40)×2) |
| 0xF0 meta 0x2F | (none) | `EndOfTrack` |
| 0xF0 meta 0x51 | u16 | `TempoChange` |

Anything else (other CCs incl. 0x60/0x40/0x41, other metas, other statuses) → `RuntimeException("Unexpected …")`. This is the mirror image of the SFX dispatch: the BGM parser accepts exactly the families the music engine implements and rejects the SFX-only ones (0xA0, 0x60, 0x41, 0x40).

### 4.2 `BackgroundMusic` — package assembly

Package = a `SECT/DRGN0.BIN/<id>` directory (M6-01 §6; M2-10). File layout (index roles source-verified from the constructor's arithmetic; `Inferred` naming):

| layout | [0] | [1] | [2] | [3] | [4] |
|---|---|---|---|---|---|
| 4-file | songId (u16 @0) | SSSQ | SSHD | soundbank | — |
| 5-file | songId | **bank-count file** (u16 @0 = number of banks) | SSSQ | SSHD | primary soundbank |

5-file banks: `soundBankData[0..sshd.soundBankSize_04)` ← file [4] (read from its tail); then for `i in 1..bankCount−1`: append `DRGN0.BIN/<fileId + i>` wholesale (`BackgroundMusic.java:38-58`) — a **multi-bank concatenation** into one `SoundBank` (Q-78: the role of file [1] in the 5-file layout rests on the index arithmetic, asset-validated-pending).

Assembly (source-verified): songId ← [0].u16(0); SSSQ header (§2.1); `volume = u8(0)/128`; `tpqn = u16(2)`; `tempo = u16(4)` → `setTempo`; 16 × `Channel` (§2.3); command list ← `SequenceBuilder(sssq.slice(0x110))`; SSHD subfiles: **0** = `SoundFont` (§5.1), **1** = velocity ramp (u16 count @subfile, then 128-byte table at +2 → `velocityRamp[0..0x80)`), **2** = breath tables (u16 count @subfile, (count+1) i16 relative offsets, each table = 0x40 bytes → `Breath.get`, §5.7). `createVictoryMusic(files)` (M6-01 §7) re-uses this song's breath controls, velocity ramp, sound font, and current tempo with a *different* SSSQ (victory variant).

### 4.3 Command set (13 classes)

| command | fields | effect at tick (source-verified) |
|---|---|---|
| `KeyOff` | channel, note | key-off all voices of (channel, note) |
| `KeyOn` | channel, note, velocity | skip if channel volume 0 or instrument null; **one voice per matching layer** (multilayer → several simultaneous voices); `voice.keyOn(channel, instrument, layer, note, velocityRamp[velocity], breathControls, playingVoices)` |
| `ModulationChange` | channel, value | channel + all its voices get `modulation` (per-sample `handleModulation`) |
| `BreathChange` | channel, value | `channel.breath = Breath.convert(value)` (fixed-point period, §5.7) |
| `VolumeChange` | channel, value | `channel.changeVolume(value/128, sssqVolume)` + `updateVolume()` on its voices |
| `PanChange` | channel, value | stereo → `setPan`; mono → forced 0x40; voices `updateVolume()` |
| `ProgramChange` | channel, program | `setInstrument`, `pitchBend = 0`, `priority = 0x40` |
| `PitchBendChange` | channel, value | `setPitchBend((v−0x40)×2)` + `updateSampleRate()` on its voices |
| `TempoChange` | value (BPM) | `backgroundMusic.setTempo` → recomputes `samplesPerTick` (in-stream tempo **works** here, unlike SFX) |
| `DataEntry` | value | NRPN data — §4.4 |
| `DataEntryLsb` / `DataEntryMsb` | value | NRPN pair — §4.4 |
| `EndOfTrack` | — | `backgroundMusic.rewind()`: `sequencePosition = 0`; if `repeat` set → continue, else `backgroundMusic = null` (song ended) |

### 4.4 NRPN / loop mechanics (music side)

State: `nrpn`, `lsbType`, `dataInstrumentIndex`, `repeatCount`, `repeatCounter`, `repeatPosition`, `repeat` (per `BackgroundMusic`).

`DataEntryMsb` (0x63): 0x00–0x0F → `lsbType = 0x10`, `dataInstrumentIndex = value`; 0x10 → `lsbType = 1`; **0x14** → mark loop start (`repeatPosition = sequencePosition`); **0x1E** → loop-end decision (`repeatCount == 0x7F` → infinite; `repeatCounter < repeatCount` → `repeatCounter++`, `repeat = true`; else finished, `repeatPosition = 0`); 0x7F → `dataInstrumentIndex = 0xFF`.
`DataEntryLsb` (0x62): lsbType 0 → `nrpn = 0`, `repeatCount = value`; lsbType 1/2 → `nrpn = value`.
`DataEntry` (CC 0x06), switch on `nrpn`: 0x00 → `setRepeatCount(value)`; **0x04–0x0C → `LOGGER.warn("Unimplemented Data Entry — Attack/Decay/Sustain/Release …")`** (the SFX side *implements* these as ADSR writes, §3.5 — the gap is asymmetric, B7); 0x0F → `setReverbConfig(value)`; 0x10 → `setReverbVolume(value, value)`; default → error log "Unknown Data entry NRPN".

Loop application: after each command, if `samplesToProcess != 0` and `backgroundMusic.handleRepeat()` → `sequencePosition = repeatPosition`, `samplesToProcess = 0` (immediate re-drain). End-of-track + `repeat` → seamless restart from 0.

### 4.5 Music state machine (per-sample, source-verified)

Per AudioThread tick (800 samples, M6-01 §4): for each granularity step (`EffectsOverTimeGranularity`: Retail = 1×800, Double = 2×400, …, `scale = 1<<shift`, `samples = 48000/(60×scale)`): per sample — `clearFinishedVoices()` (frees finished voices, re-indexes `priorityOrder`), `tickSequence()` (`samplesToProcess` counts down **once per 48 kHz sample**; at 0 it executes the next command and sets `samplesToProcess = (int)(delta × samplesPerTick)`), each voice `tick(output, reverb)`, `reverb.processReverb`, mix: `(dry + reverb) × engineVolume(L/R) × playerVolume` → 32-bit float stereo buffer. Per granularity step — `handleVolumeChanging` (linear old→new over `time×scale`), `handleFadeInOut` (fade counter over `time×scale`; `setMainVolume`/`setSequenceVolume` cancel in-flight fades — "can happen when running at high speed"), `voice.handleModulation()` (breath-driven pitch modulation, §5.7).

`samplesPerTick = 48000 × 60 / (tempo × tpqn)` (`calculateSamplesPerTick`) — with `tempo` in **BPM**: at 120 BPM / tpqn 480 → 5 samples/tick (sample-accurate event timing, M6-01 §4.2; MIDI-tool corroborated, §6.2).

Voice pool (`voiceCount` from the music-quality config, M6-01 A4): `selectVoice()` = first free → lowest-`priorityOrder` **low-priority** voice → oldest (lowest `priorityOrder`) → **`throw RuntimeException("Voice pool overflow")`**. Voice priority tier (`VoicePriority.getPriority`): layer flag 0x1 (highPriority) absent → LOW; present → MEDIUM unless channel priority (ChannelInfo 0x0B) is 0x7F → HIGH; key-off decreases HIGH→MEDIUM else →LOW.

Reverb: `reverb.setConfig(config)` (NRPN 0x0F) — the music `Reverberizer`'s 10 `Config` entries (0 = all-zero off; **1–9 are index-for-index identical to the game's ROM `reverbConfigs_80059f7c[1..9]`** — verified value-by-value against `Audio.java:62-76`) and `setVolume(v/128, v/128)` (NRPN 0x10); default config 3, default volume 0x3000/32768 (M6-01 §5.4's 0x30 reverb default — consistent). Work area `float[0x8000]` vs the SPU's `float[0x4000]` (M4-03 §5) — in-source comment: configs 5/7/8/9 need the larger area "but they shouldn't be used".

## 5. Shared soundfont / soundbank / instrument / layer / breath / ADSR formats

### 5.1 Instrument tables

- **Music `SoundFont`** (SSHD subfile 0): u16 `count` @0, (count+1) i16 offsets (relative to subfile start, −1 = absent), records walked backwards (each record = [offset, nextOffset)). `getInstrument(index)` bounds-checked, null otherwise.
- **SFX `Sssqish.Entry2List`** (0x190, §2.2): u16 `count`, (count+1) i16 offsets; sizes recovered by sorting offsets (last = subfile end − 0x190); `elementCount = (recordSize − 8) / 0x10`.

### 5.2 Instrument — 8-byte header (shared record)

| offset | field | notes |
|---|---|---|
| 0x00 | count / **type** | Music decode: `0xFF` = **SFX type**; bit 0x80 = **MULTILAYER**; else **STANDARD**. Layer count: SFX → signed byte @0x07; else `count & 0x7F`. SFX comment: instruments in Sssqish subfiles are "always 0xff" (SFX type) |
| 0x01 | volume | /128 (music); SFX `patchVolume_01` (per-voice `instrumentVolume`) |
| 0x02 | pan | music |
| 0x03 | (unused) | |
| 0x04 | pitchBendMultiplier | music (overridden per layer by flag 0x10) |
| 0x05 | breathControlIndex | music (overridden per layer by flag 0x40); SFX: breath table selector under modulation flag 0x40 |
| 0x06 | startingNote | **SFX type only**: `layerIndex = note − startingNote` (single layer per note — the 0xA0 path); also SFX 0xA0's `instrumentIndex = note − startingKeyPosition` |
| 0x07 | layer count (SFX type, signed byte) | |
| 0x08… | layers (0x10 each) | §5.3 |

Layer selection (music): SFX type → exactly `layers[note − startingNote]`; STANDARD → **first** layer whose key range contains the note; MULTILAYER → **all** matching layers (polyphonic layer stacking).

### 5.3 InstrumentLayer — 0x10 record (byte-identical in both players)

| offset | field | notes |
|---|---|---|
| 0x00 | keyRangeMin | note match: `min ≤ note ≤ max` |
| 0x01 | keyRangeMax | |
| 0x02 | keyRoot | pitch: offset from root |
| 0x03 | finePitch (i8) | music: ×8 ("originally sixteenths. Increased for more accuracy"); SFX: `cents_03` (×4 at sample-rate calc) |
| 0x04 | soundBankOffset (u16) | **×8** → byte offset into the soundbank (§5.4) |
| 0x06 | adsrLo (u16) | §5.6 |
| 0x08 | adsrHi (u16) | §5.6 |
| 0x0A | lockedVolume | music `AdsrPhase`-adjacent (read, stored) |
| 0x0B | volume | /128 (music); per-voice `instrumentLayerVolume` (SFX) |
| 0x0C | pan | music; per-voice `pan_4c` (SFX) |
| 0x0D | pitchBendMultiplier | |
| 0x0E | breathControlIndex | |
| 0x0F | flags | 0x01 highPriority/retrigger (SFX: sustained-note flag), 0x02 noise, 0x10 pitchBendMultiplier-from-instrument, 0x20 modulation, 0x40 breathControlIndex-from-instrument, 0x80 reverb — **consistent across both players** |

### 5.4 Soundbank — SPU ADPCM samples

Samples are the PSX SPU's 4-bit ADPCM blocks, 16 bytes each:

| byte | field |
|---|---|
| 0x00 | **shift** (nibble): runtime decoders compute `shift = 12 − (b & 0x0F)`; filter `(b & 0x70) >> 4` clamped ≤ 4; **dev-tool `SoundbankDecoder` uses the raw nibble as shift — discrepancy, Q-75** |
| 0x01 | flags: 0x01 end/loop-end marker, 0x02 pass (only in combination), 0x04 loop-start marker |
| 0x02–0x0F | 14 bytes = 28 × 4-bit samples, low nibble first (M4-03 §3.1) |

Per-consumer block-flag behavior (source-verified):

| consumer | 0x00/0x02 | 0x01 | 0x03 | 0x04/0x06 | 0x07 | other |
|---|---|---|---|---|---|---|
| SPU voice (44.1 kHz, `Spu`, M4-03) | advance | sample ends | loop back to repeat point | set repeat point, advance | (unused) | — |
| music `SoundBankEntry` (48 kHz float) | advance | `index=0`, **end** | `index = repeatIndex` | set `repeatIndex`, advance | no-op | **throw** "Unknown Sound Bank Flag" |
| `XaTranscoder` (XA music, M2-10 §4) | advance | end | loop | set repeat | — | — |
| `SoundbankDecoder` (dev) | advance | end (zero-word) | loop | set repeat | — | — |

Decoded recurrence (all four): `s = (t << shift) + (old×POS[f] + older×NEG[f] + 32) / 64`, POS = {0,60,115,98,122}, NEG = {0,0,−52,−55,−60} (M4-03 §3/M4-04). Clamping: SPU/XaTranscoder clamp to s16; the music float path does not (values stay in float, M4-04 census — `SoundBankEntry` is the fifth consumer, added by M6-02).

Bank **layout**: the music player parses banks by **scanning 16-byte boundaries for a fully-zero 16-byte word** (`SoundBank.findSounds` — each zero word starts a sample; sample bytes run to the next zero word) — a headerless, delimiter-based layout. The SFX player never parses the bank: layers reference samples by explicit `soundBankOffset × 8` into the raw file. Bank source: SFX = the SSHD's soundbank subfile/`PlayableSound.data`; BGM = package file [3] or [4]+extras (§4.2).

### 5.5 ADSR — the lo/hi u16 encoding and the two consumers

The bit layout (consistently implied by the music `AdsrPhase.getPhases` decode *and* the SFX data-entry writes, §3.5 — `Inferred` as a format fact, source-verified in both decoders):

| word | bits | field |
|---|---|---|
| lo | 0–3 | sustain level ((v+1)×0x800 cap 0x7FFF; SFX write: `value/8`) |
| lo | 4–7 | decay shift (SFX write: `(0x7F−v)/8`) |
| lo | 8–9 | attack step (7 − (v&3); SFX write: `(0x7F−v)`) |
| lo | 10–14 | attack shift |
| lo | 15 | attack exponential |
| hi | 0–4 | release shift (SFX write: `(0x7F−v)/4`) |
| hi | 5 | release exponential |
| hi | 6–7 | sustain step |
| hi | 8–12 | sustain shift |
| hi | 14 | sustain direction (0 = decrease, 1 = increase) |
| hi | 15 | sustain exponential |

Music consumer (`AdsrEnvelope`, per-sample float stage): 4 phases ATTACK→DECAY→SUSTAIN→RELEASE→OFF with a `1<<30` sample counter and `cycleAmount` (attack shift → `2^min(30, 41−shift) × 44100/48000` samples); exponential rising halves `cycleAmount` above level 0x6000, exponential falling scales the step by `level/32768`; rising sustain holds (phase stays SUSTAIN until key-off); `currentLevelF = level/32768`. Targets: attack 0x7FFF, decay `(lo&0x0F+1)×0x800`, sustain 15 or 0x7FFF (direction), release 15. SFX consumer: **none** — the words are parsed and NRPN-mutable but never applied (the SPU voice has no ADSR stage, M4-03; §3.5 note).

### 5.6 Breath (modulation LFO)

Original: 60-value breath tables (SSHD music subfile 2 / SFX waveform lists). Music player: each retail table (0x40 bytes) is **FNV-1a hashed** (base 2166136261, mul 16777619) and matched against **8 hard-coded presets** (sine / triangle / half / double-frequency / abs variants; `Breath.java` static init); an unknown hash **throws** "Unknown breath table file" — a reference-author assumption that all retail tables equal one of the 8 (layer-4, `Inferred`, asset-validated-pending; B8). Playback waveforms are 240-point (4×-upsampled, layer-4 accuracy boost) with Catmull-Rom or linear interpolation per preset.

Rate: `Breath.convert(v) = BREATH_MAX_VALUE / (60 − 58×v/127)` with `BREATH_MAX_VALUE = 60 << 47` (fixed-point samples per period). SFX equivalent (0x02 breath CC): `240 / (60 − v×58/127)` **game ticks** per period (integer division: 4 ticks at v=0 → 66 ms, 120 ticks at v=127 → 2 s). Same functional form, different domain (sample-accurate float vs 60 Hz ticks) — a consequence of the two clock domains (M6-01 §4).

## 6. MIDI ↔ SSSQ conversion (dev tooling, layer 4)

### 6.1 Custom MIDI container

`out-new-all-type0.mid` is a **non-standard MIDI file**: `MThd` chunk with `u16 numberOfTracks @0x0A` and `u16 ticksPerQuarterNote @0x0C` (the standard format/division fields are repurposed), followed by standard `MTrk` chunks (standard leading-delta varints, running status).

### 6.2 `MidiToSssq` (MIDI → SSSQ)

1. Load a **retail SSSQ as template** (`SECT/DRGN0.BIN/5820/1-orig`); copy its first 0x110 bytes (header + 16 ChannelInfos).
2. Write the custom header's tpqn into SSSQ `u16 @0x2` (the SSSQ **tempo** field @0x4 keeps the retail template value — the header-level SSSQ tempo is *not* the MIDI tempo).
3. Parse every `MTrk`: 0x80/0x90 (note+velocity); 0xB0 **only** CC {0x01, 0x02, 0x06, 0x07, 0x0A, 0x63} (no 0x62!); 0xC0; 0xF0 meta {0x03 name (dropped), 0x2F, 0x51}. Unsupported bytes throw.
4. **Tempo**: `60_000_000 / µs-per-quarter (3 bytes)` = **BPM**, emitted as 2 bytes (lo, hi) — little-endian u16, matching the readers' `readShort`.
5. Sort all commands by absolute time, relativize to deltas, and emit from 0x110: status byte only when changed (running status), payload, **trailing** varint — exactly the BGM wire format of §4.1.
6. Quirk: the output is padded with **one trailing 0x00 byte** (`new byte[offset+1]`) — a bare running-status byte that the music parser would read as a repeat of the last event; how/whether the repack step trims it is untraced (Q-76, dev-tool only).

Corroboration chain for the **tempo = BPM** unit (BGM leg of Q-74): MIDI µs/q → BPM here, BPM ↔ µs/q in `MidiWriter` (`60_000_000/BPM`, M6-01 §4.2), and the music engine's `samplesPerTick = 48000×60/(tempo×tpqn)` which is dimensionally correct only for BPM. Three independent in-repo consumers agree.

### 6.3 `MidiWriter` (SSSQ → MIDI)

The inverse for the BGM grammar (M6-01 §4.2): re-emits MIDI with standard **leading** deltas and µs/q = 60_000_000/BPM tempo metas — the delta-position and tempo-unit conversions confirm both the trailing-varint SSSQ layout and the BPM unit.

### 6.4 `SoundbankDecoder` / `SoundfontWriter` (bank → WAV/SF2)

`SoundbankDecoder` (standalone `main`): decodes `DRGN0.BIN/5820/3` (the BGM soundbank of package 5820) block-by-block (16-byte stride; a fully-zero 16-byte word ends the scan) and plays the PCM at **18900 Hz** (`44100 × 3/7`; the reason for the rate choice is `Inferred` — a console-preview convenience). It prints per-block shift/filter/end/repeat/start and uses the **raw shift nibble** (Q-75). `SoundfontWriter` builds a SoundFont 2 (`RIFF sfbk` with `sdta`/`smpl`) from 22 decoded sample WAVs (`3_%05d.wav`) + the raw bank bytes — the modding workflow: unpack → decode bank → edit music as MIDI against the SF2 in a DAW → `MidiToSssq` → repack.

## 7. Worked command-stream examples (synthetic — no retail bytes)

**7.1 SFX: infinite CC-0x60 loop, one 0xA0 note.** Instrument table: 1 instrument (SFX type, startingNote 60), one layer (keys 60–72, bank offset 0, flag 0x01 sustained). Stream (channel 3, reader offset 0):

```
byte  value  event
00    A3     status 0xA0 ch3
01    3C     note 60
02    40     velocity 64
03    00     instrument 0
04    01     delta 1 tick (= 8.0 ms, §3.8)
05    B3     status 0xB0 ch3 (CC)
06    60     cc 0x60 repeat
07    00     offset lo
08    00     offset hi   (loop to reader offset 0)
09    00     count 0 (infinite)
0A    01     delta 1 tick
```

Tick trace (60 Hz SFX tick): tick 1 — drain: 0xA0 → voice bound (one-shot? no: flag 0x01 → sustained), delta 1 → stop; CC 0x60 → repeat set, delta read inside the switch, break; repeat applied → reader back to 0, delta 0 → re-drain 0xA0 (retrigger: `findMatchingOrFreeNote` matching-note path) and 0x60 again… steady state: one 0xA0 retrigger + one 0x60 per 2 ticks = **16 ms period**, note held across re-triggers.

**7.2 SFX: NRPN 0x14/0x1E loop ×3.** (Bytes: `B3 63 14 00 …` mark at offset 2; count via `B3 62 02 00 …` after `B3 63 10 00`; end via `B3 63 1E 00`.) Each of the three passes re-executes the marked range; on the third end-decision `repeatCounter == repeatCount` → stream falls through to EOT `FF 2F 00`, `soundEnded` → teardown when the last voice decays (`adsrVolume < 16`).

**7.3 BGM: 120 BPM, tpqn 480 → `samplesPerTick = 48000×60/(120×480) = 5 samples`.** Song (channel 0, instrument 0 assigned in ChannelInfo):

```
byte  value  event
00    F0     meta
01    51     tempo
02    78     tempo lo (120)
03    00     tempo hi
04    00     delta 0
05    90     key on ch0
06    45     note 69 (A4)
07    64     velocity 100
08    08     delta 8 ticks = 40 samples
09    80     key off ch0
0A    45     note 69
0B    00     velocity (skipped byte)
0C    10     delta 16 ticks = 80 samples
0D    B0     CC ch0
0E    63     NRPN MSB
0F    14     mark loop start (repeatPosition = next command)
10    00     delta 0
11    90     key on (re-trigger each pass)
12    45     note 69
13    64     velocity
14    04     delta 4 ticks = 20 samples
15    80     key off
16    45     note
17    00     (skipped)
18    04     delta 4 ticks
19    B0     CC ch0
1A    63     NRPN MSB
1B    1E     loop end (count 0x7F → infinite)
1C    00     delta 0
```

Trace: at load the whole list is pre-built (no runtime parsing); the tick executes `TempoChange` (delta 0 → immediate), KeyOn at sample 0 (voice: 40/128 velocity-ramp volume × chain, §4.3), KeyOff at sample 40, NRPN mark at 80, then the 20+20-sample loop body re-executes forever — `handleRepeat` zeroes `samplesToProcess` so the body re-drains with no gap. An EOT (0x2F) instead of the 0x1E would restart from command 0 only while `repeat` is set.

All three examples are **synthetic constructions** for illustrating the grammar; no claim is made that any retail stream has this shape.

## 8. Layer separation and compatibility classification

| ID | behavior | layer | classification |
|---|---|---|---|
| B1 | SFX 0x80/0x90 → `RuntimeException("No longer implemented")`; retail SFX inferred to be 0xA0-only | 4 (decompilation gap) | Behaviorally-equivalent (decompiled contract); original handler lost |
| B2 | SFX 0x40 sustain, NRPN 0x11/0x12/0x13 (`FUN_8004c690`/`FUN_8004c5e8`) → `assert false`; no-op with production asserts off; events still consumed | 4 (decompilation gap) | Behaviorally-equivalent (gap); original behavior Unresolved (Q-77) |
| B3 | BGM parser (`SequenceBuilder`) is reference-authored; throws at **load** on any byte outside its supported set (0xA0, 0x60, 0x40, 0x41, CC 0x62-without-supported-msb, other metas) | 4 (re-implementation) | Behaviorally-equivalent for retail BGM (Inferred: retail BGM uses only the supported set — the parser's complementarity with the SFX dispatch is the evidence) |
| B4 | SFX tempo pinned to (60, 480) during playback; 0x51 stored-then-clobbered (functionally dead); 8.0 ms/tick definitive | 1/3 (decompiled original) | Behaviorally-equivalent (decompiled contract); resolves Q-74's SFX leg |
| B5 | Two parallel SFX repeat mechanisms (CC 0x60; NRPN 0x14/0x1E); the music player implements only the NRPN one and rejects 0x60 | 3 (format) / 4 (music parser) | Format fact; which mechanism retail SFX/BGM actually use is Inferred from parser coverage (asset-validated-pending) |
| B6 | SFX instrument selection: event param2 → Sssqish 0x190 table via `setActiveSequence` (per-event); ChannelInfo 0x02 instrument written only by 0xC0, consumed only by the lost 0x80/0x90 | 3 (decompiled) | Behaviorally-equivalent (decompiled contract) |
| B7 | NRPN 0x04–0x0C ADSR overrides: **implemented** in SFX (live layer-word writes, no audible consumer) vs **unimplemented** in music (warn log) | 3/4 | Discrepancy documented; asymmetry is reference behavior |
| B8 | Music breath tables matched by FNV-1a hash to 8 analytic presets; unknown hash throws at load | 4 (assumption) | Behaviorally-equivalent **iff** all retail tables are among the 8 (asset-validated-pending) |
| B9 | Voice exhaustion: SFX silently drops notes (bound-gated steal, M6-01) vs music `throw "Voice pool overflow"` | 3 vs 4 | Asymmetry documented; original music-engine allocation Unresolved (no decompiled BGM sequencer exists) |
| B10 | Velocity ramp: SFX `VolumeRamp` subfile 1 (`ramp[velocity]`); music SSHD subfile 1 (128-byte table) — same role, per-SSHD | 3 (format) | Format fact (source-verified in both players) |
| B11 | `SoundbankDecoder` (dev tool) decodes shift as the raw nibble; all runtime decoders use `12 − nibble` | 4 (tooling) | Dev-tool discrepancy, no runtime impact (Q-75) |
| B12 | Custom MIDI container (u16 track count @0x0A, u16 tpqn @0x0C) and 18900 Hz console playback | 4 (tooling) | Tooling format, not a game format |
| B13 | `MidiToSssq` emits a trailing 0x00 byte; repack handling untraced | 4 (tooling) | Dev-tool quirk (Q-76) |
| B14 | Key-off expression: SFX via 0xA0 velocity 0 (note-targeted, `_0c`-gated) vs BGM via 0x80 / 0x90-velocity-0 (channel+note) | 3 (format) | Format fact (source-verified in both players) |

## 9. Register — items created/advanced by M6-02

- **Q-56 (advanced):** SSSQ 24-entry subfile-4 variant vs 16-entry base — the 24-entry "Sssqish" is the *exclusive* SFX sequence carrier (`getSequence` requires subfile 4; its absence is the logged "Failed to find sequence" case), the 16-entry standalone SSSQ is the BGM carrier. Remaining (asset-validated-pending): whether any retail SFX package ships subfile 4 without a usable PatchList entry (the Archangel case) and whether any BGM uses a 24-channel stream (none in-repo).
- **Q-74 (narrowed):** the SFX leg is settled — the tempo pair is pinned to (60, 480) for every delta read during playback (source reset in `sssqReadDeltaTime`), 0x51 is functionally dead in SFX, and the SFX tick rate is definitively **8.0 ms/tick** (M6-01's "plus in-stream 0x51 overrides" wording is corrected in place). The remaining discrepancy is the in-source *debug-log* formula (2.0833 ms/tick at 60/480), a log-only expression with no runtime consumer. The BGM leg stands corroborated (tempo = BPM; three in-repo consumers agree). Residual: whether the log formula reflects some other original consumer — unverifiable without original-engine documentation (M10), runtime impact: none identified.
- **Q-75 (new):** `SoundbankDecoder` (dev tool) uses the raw shift nibble; all runtime SPU-ADPCM decoders use `12 − nibble`. Dev-tool bug or music-bank-specific storage? No runtime impact (the tool is console-preview only). Needs: listening to the tool's output vs an XaTranscoder decode of the same samples — asset-validated-pending.
- **Q-76 (new):** `MidiToSssq` pads its output with a trailing 0x00 byte (a bare running-status byte the music parser would misread as a repeat of the last event). How the repack/pack step handled it is untraced. Dev-tooling only; needs the packer source or a repacked sample.
- **Q-77 (new):** SFX NRPN 0x11 (`FUN_8004c690`) and 0x12/0x13 (`FUN_8004c5e8`) are `assert false` stubs — original behavior unknown (decompilation gaps beyond M6-01 A7). If retail SFX uses these NRPNs, the reference silently ignores them (asserts off). Needs: decompilation completion or retail SFX bytes.
- **Q-78 (new):** 5-file music package layout — the in-repo arithmetic reads the bank count from file [1].u16(0) and shifts SSSQ/SSHD to [2]/[3] (§4.2), implying [1] = bank-count file; the 4-file layout has no such file. Needs: one retail 5-file package (asset-validated-pending).
- **M6-01 in-place correction:** §4.3/§9 wording "always 60/480 plus in-stream 0x51 overrides" → "always 60/480; the 0x51 handler is functionally dead during playback (stored value clobbered by the `sssqReadDeltaTime` reset on the same event)" — applied to `research/audio/01-audio-architecture.md` with this document.

Carried to M6-03 (XA/Opus/FMV audio): `XaPlayer` internals and the XA→Opus runtime path (M2-10 §3–4 first-pass), FMV-XA volume routing, Q-48/Q-47/Q-57 closures, and the music `Reverberizer`'s remaining DSP detail (the 10-config table is documented here; the SPU-side reverb network remains M4-03 territory).
