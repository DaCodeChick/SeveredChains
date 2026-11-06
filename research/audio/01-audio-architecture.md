# M6-01 — Audio Architecture: Lifecycle, Threading, Timing, and Routing

Status: Complete (M6-01, 2026-11-06). Reference: Severed-Chains @ `0c08c679b`, read-only. Scope: the *architecture* of the reference's audio subsystem — lifecycle and boot order, thread ownership and synchronization, clock domains and event-timing models, volume/routing domains, and data flow from assets to OpenAL output. The SSSQ event grammar, the SFX sequencer state machine, the music engine internals, the XA/Opus pipeline, and the MIDI tooling get their own documents (M6-02, M6-03). Evidence vocabulary: **source-verified**, **format-corroborated**, **experimentally verified**, **asset-validated** (*none exists — no such claim is made*), **Inferred**, **Unresolved**. Layers: (1) PSX platform, (3) LoD-specific, (4) reference-specific. Cross-references: R1 `research/platform/02-execution-model.md` §6 (thread model), M2 `research/assets/10-music-formats.md` (SSSQ/SSHD/XA container formats), M4-03 `research/psx/03-spu.md` (SPU voice model, corrected by §9 below), M4-04 `research/psx/04-xa-adpcm.md` (XA codec core), M5-04 `research/rendering/04-postprocessing.md` §4 (FMV audio hand-off).

## 1. Big picture: three pipelines, two SSSQ players, four threads

The reference routes all audio through one LWJGL OpenAL device hosted by a single **audio thread** (`AudioThread`), but *generates* samples on three different threads through three independent pipelines:

```
                        ┌──────────────────────────── audio thread (OpenAL host) ────────────────────────────┐
                        │  120 Hz minimum-pace loop; per source: canBuffer → tick; handleProcessedBuffers   │
                        └───────┬───────────────────────────────┬───────────────────────────────┬────────────┘
                                │ ticks (800 stereo float samples)  │ ticks (480 int16 samples)     │ recycles buffers only
                                ▼                                   ▼                               ▼
 ┌──────────────────── music ────────────┐    ┌──────── XA music (Opus) ────────┐    ┌──────── SFX / SPU ────────────┐
 │ SSSQ (music) + SoundFont + MSPB bank  │    │ XA/LODXA0n.XA/<track>.opus      │    │ SSHD (SSSQ + instruments +    │
 │        │  (core/audio/sequencer,      │    │        │  (opus/libopusfile     │    │   soundbank) + MSPB bank      │
 │        ▼   reference float engine,    │    │        ▼   native decode,       │    │        │  (game/sound,       │
 │  BackgroundMusic → Sequencer →        │    │  XaPlayer: op_read → ×vol →     │    │        ▼   decompiled        │
 │  24 float voices + Reverberizer       │    │  48 kHz int16 ring (8 buffers)  │    │  Sequencer.tick() → 24       │
 │  → 48 kHz float stereo (3 buffers)    │    │  → 48 kHz int16                 │    │  PlayingNote → SPU 24 voices │
 └───────────────────────────────────────┘    └─────────────────────────────────┘    │  → 44.1 kHz int16 (16        │
                                                                                     │     buffers via GenericSource)  │
                                                                                     └─────────────────────────────────┘
   game thread (scripts, state transitions, audio API)        SFX thread ("SFX", 60 Hz: Sequencer.tick + SPU.tick)
```

| # | Pipeline | Source format | Player | Generation thread | Render domain | OpenAL format/rate | Buffer ring |
|---|----------|---------------|--------|-------------------|---------------|--------------------|-------------|
| 1 | **Music (BGM)** | SSSQ + SoundFont + MSPB soundbank (5-file DRGN dir, M6-03 §2) | `core/audio/sequencer/Sequencer` (reference-authored float engine) | audio thread (its own `tick()`) | float, 48 kHz | `AL_FORMAT_STEREO32` (0x10011), 48000 Hz | 3 |
| 2 | **XA music (LODXA)** | Opus-in-Ogg (unpack-time transcode of retail XA, M2-10 §4) | `core/audio/opus/XaPlayer` | audio thread (its own `tick()`) | int16, 48 kHz | MONO16 or STEREO16, 48000 Hz | 8 |
| 3 | **SFX (SSSQ)** | SSHD-embedded SSSQ + instruments + MSPB soundbank | `game/sound/Sequencer` (decompiled original) + `core/spu/Spu` | **SFX thread** (60 Hz: `Sequencer.tick()` then `SPU.tick()`) | int16, 44.1 kHz | `AL_FORMAT_STEREO16`, 44100 Hz (via `GenericSource`) | 16 |
| 4 | **FMV audio** | retail XA sectors inside `.IKI` (decoded live, M4-04 §5) | `XaAdpcm.decode` → `GenericSource` | game thread (FMV loop, M5-04 §4) | int16, 37.8 kHz | STEREO16, 37800 Hz | 16 (per FMV) |

**The two-SSSQ-player split is the defining architectural fact of M6.** The same SSSQ format family is consumed by two different players with different event subsets (source-verified):

- The **game sequencer** (`legend/game/sound/Sequencer`, decompiled original logic) plays **SFX** sequences. It implements note-on/note-off as **0xa0 polyphonic-key-pressure events** (velocity ≠ 0 → note on, velocity 0 → note off, `Sequencer.java:193-330`, `keyOffMatchingNotes` 0x80048514), and its **0x80/0x90 handlers throw `RuntimeException("No longer implemented")`** (`Sequencer.java:347-353` `sssqHandleKeyOn`, `:772-774` `sssqHandleKeyOff`). Retail SFX SSSQ files therefore cannot contain 0x80/0x90 events (a hit would crash the SFX thread's tick, which is caught and logged as "Sound thread crashed!" — `Audio.java:1561-1563` — with the sequencer state left mid-tick; *Inferred* from the catch-all + working game).
- The **music sequencer** (`core/audio/sequencer/Sequencer`, reference-authored) plays **BGM** sequences. It implements **0x80/0x90 key off/on** (velocity 0 on 0x90 normalized to KeyOff — `SequenceBuilder.java:56-63`), per-channel program changes, and 1-byte-payload CCs, and it has **no 0xa0 handler** (unsupported command → exception, `SequenceBuilder.java:100-108`).

Both players are described structurally in M6-02 (SFX) and M6-03 (music engine).

## 2. Lifecycle

### 2.1 Boot order (source-verified, `GameEngine.java`)

| Step | Anchor | Action |
|------|--------|--------|
| 1 | `GameEngine.java:139` | `SPU = new Spu()` — 24 `Voice` objects, 512-entry cubic-interpolation weight table, 12-entry pitch table `sampleRates[i] = round(0x1000 × 2^(i/12))` (`Spu.java:46-62`), `muted = true` by default. |
| 2 | `GameEngine.java:140` | `AUDIO_THREAD = new AudioThread(true, 24, InterpolationPrecision.Double, PitchResolution.Quadruple, EffectsOverTimeGranularity.Double)` — stereo, 24 music voices; constructor defaults are **immediately overridden** by config at step 7. |
| 3 | `GameEngine.java:144` | `openalThread = new Thread(AUDIO_THREAD)` (not started yet). |
| 4 | `GameEngine.java:252` | `AUDIO_THREAD.init()` → open device (config `audio_device` → default → first, `AudioThread.java:219-235`), create OpenAL context, `addDefaultSources()`: **sequencer** (priority 3, `super(3)` — 3 buffers) and **XaPlayer** (8 buffers, `XaPlayer.java:36`); starts the 2 s default-device poll scheduler (`AudioThread.java:88-103`). |
| 5 | `GameEngine.java:253` | `setMusicPlayerVolume(MUSIC_VOLUME × MASTER_VOLUME)` — float 0..1 (config defaults 1.0, `VolumeConfigEntry.java:11`). |
| 6 | `GameEngine.java:254-256` | apply `music_interpolation_precision`, `music_pitch_resolution`, `music_effects_over_time_granularity` (config defaults: Double / Quadruple / Double — `MusicInterpolationPrecisionConfigEntry.java:13`, `MusicPitchResolutionConfigEntry.java:13`, `MusicEffectsOverTimeGranularityConfigEntry.java:13`). |
| 7 | `GameEngine.java:258` | `SPU.init()` → registers the SPU's `GenericSource(AL_FORMAT_STEREO16, 44100)` on the audio thread (`Spu.java:66-70`) and sets SPU `playerVolume = SFX_VOLUME × MASTER_VOLUME`. |
| 8 | `GameEngine.java:443` | `startSound()` → spawns the **SFX thread** (name "SFX", `Audio.java:1543-1548`) at title-screen init (after `loadMenuAssets()`, before the title demo FMV, `GameEngine.java:432-445`). |
| 9 | `Audio.initSound` (0x80019500, `Audio.java:154`) | `initSpu()` (upper bound 20, `ticksPerSecond_42 = 60`, unmute, note/queue reset), `sssqFadeIn(0, 0)`, `setMaxSounds(8)`, `sssqSetReverbType(3)`, `sssqSetReverbVolume(0x30, 0x30)`, `_800bd0f0 = 0`, per-sequence `_800bc9a8[i].soundFile_02 = 0xffff`-style reset, `sssqTempoScale_800bd100 = 0x100`. |

State: the SPU starts **muted** (`Spu.java:42`) and is unmuted by `initSpu` (`Spu.unmute()`, `Audio.java:1124`); music is silent until a package is loaded and started.

### 2.2 SFX thread (source-verified, `Audio.java:1539-1579`)

```
SOUND_TPS = 60; NANOS_PER_TICK = 1_000_000_000 / 60
loop:
  SEQUENCER.tick()     // game-level SSSQ sequencer (its own lock)
  SPU.tick()           // 735 stereo samples @ 44.1 kHz (Spu.class monitor)
  catch Throwable → log "Sound thread crashed!" (loop continues)
  if interval ≥ 3 × tick → catch-up skip (also covers IDE pause)
  sleep(remaining, in ms); time += NANOS_PER_TICK
```

`stopSound()` (line 1550) merely sets `soundRunning = false`; the thread exits at the next loop iteration. The SFX thread is the *only* caller of `SPU.tick()` (source-verified by census) and of the game-level `Sequencer.tick()`.

### 2.3 Runtime state transitions (source-verified)

- **Sound-file load/unload flags** `loadingAudioFiles_800bcf78` (`Audio.java:100`, `AtomicInteger`): one bit per audio asset group — `0x1` menu SFX, `0x2` submap, `0x4` battle cutscene, `0x8` battle character, `0x10` battle phase, `0x20` battle monster, `0x40` battle DEFF, `0x80` music, `0x4000` victory music, `0x8000` wmap destination, `0x10000` battle character attack. Set before an async load, cleared in the load callback; exposed to scripts (`scriptGetLoadedSoundFiles`, 0x8001ffc0).
- **`unloadSoundFile(type)`** (0x8001e29c, `Audio.java:552-666`) — the ten-way switch (in-source list: 0 menu / 1 players / 2 dragoon-spells-items / 3 monsters / 4 submap / 5 battle cutscene / 6 monster attack / 7 player attack / 8 music / 9 world map): each case unloads the matching `PlayableSound0c` via `sssqUnloadPlayableSound` (refuses with an error log if a voice still references it, `Audio.java:1149-1171`); type 8 calls `AUDIO_THREAD.unloadMusic()` and zeroes `_800bd0f0`; type 0 only logs a warning (menu sounds are never unloaded).
- **Reset paths:** `sssqResetStuff` (0x80020008, `Audio.java:810-818`) = `stopAndResetSoundsAndSequences` + unload 1,3,4,5,6,8 + `stopMusicSequence`; `FUN_80020308` (0x80020308) = same minus unload 8/music. `stopSoundsAndSequences(resetVoice)` (0x8004d91c) zeroes all voices' ADSR **and `adsrVolume = 0`** with an in-source GH#1719 comment: because the SPU now runs in lockstep, a finished voice's ADSR would otherwise keep occupying its slot until `adsrVolume < 0x10` (`sssqFreeFinishedNotes`), so an immediately-following sound (e.g. the battle-transition sound after piloting the Queen Fury on the world map, where LOD caps sounds at 8) would find no free voice. The commented-out `wasteSomeCycles(0x2L)` in the same method is the removed original timing padding.
- **Menu SFX backup/restore** (source-verified): opening the item menu does `copyPlayingSounds(queuedSounds, playingSoundsBackup_800bca78)` + `playMusicPackage(menuMusic, true)` (`SItem.java:783-789`); closing restores the queue (`SItem.java:807-808`). `copyPlayingSounds` (0x80020360, `Audio.java:874-886`) re-arms one-shot entries (`type 4 && _1c != 0` → `type 3`).
- **Music state** `_800bd0f0` (`Audio.java`): `0` = none, `2` = music loaded/active (set by `playMusicPackage`, `Audio.java:563-571`; `stopCurrentMusicSequence` only acts when `== 2`, `Audio.java:394-398`). `musicLoaded_800bd782` is a parallel plain-boolean flag.

### 2.4 Shutdown (source-verified)

`AUDIO_THREAD.destroy()` (`GameEngine.java:271`, `AudioThread.java:150-165`): shut down the device-poll scheduler; if the run loop is not active, destroy in place; otherwise set `running = false` + notify, and **busy-wait (`DebugHelper.sleep(1)`) until the audio thread has released the device** (`audioDevice == 0`). The run loop's exit path re-destroys internals and unloads any pending XA. Note the SFX thread has no shutdown call in the teardown path inspected here (it exits with the JVM; `stopSound` exists but its call sites are outside this milestone's scope — recorded as an observation, not a finding).

### 2.5 Device hot-plug and reconnect (layer 4, source-verified)

The audio thread re-opens the device on: default-device change (2 s poll, `AudioThread.java:88-103`), configured-device switch, and OpenAL `ALC_EXT_disconnect` device loss (checked every loop iteration, `AudioThread.java:279-292`). `reinit()` (`AudioThread.java:110-135`) preserves per-source active state and reloads a pending XA file after the context is rebuilt. Pure layer-4 resilience; the original had none of this (no host).

## 3. Threading and synchronization

### 3.1 Thread table (source-verified)

| Thread | Name | Pace | Owns/generates | Main entry |
|--------|------|------|----------------|------------|
| Game | (main) | frame-limited (M1) | all game state, scripts, audio API calls, SFX queue drain, FMV XA decode | `Scus94491BpeSegment` tick → `startQueuedSounds()` (`Scus94491BpeSegment.java:178`) |
| SFX | "SFX" | 60 Hz fixed-point | game SSSQ sequencer + SPU voice rendering | `Audio.soundLoop` (`Audio.java:1554`) |
| Audio | (default) | ≥120 Hz min-pace; source ticks gated by OpenAL buffer availability | OpenAL device, music sequencer ticks, XaPlayer ticks, buffer recycling | `AudioThread.run` (`AudioThread.java:247-311`) |
| Poll | (single-thread scheduler) | 2 s | default-device discovery | `AudioThread.init` (`AudioThread.java:88`) |

The audio thread's loop (source-verified): under the **AudioThread monitor**, for each source under the **source monitor** — `canBuffer()` → `tick()` → `handleProcessedBuffers()`; then, *only if the music sequencer cannot buffer*, sleep to a 120 Hz minimum interval (`AudioThread.java:294-307`, `nanosPerTick = 1e9/120`, `AudioThread.java:57`). Consequence: a source is **tick-suppressed** (not dropped) when its OpenAL ring is full; for the music sequencer the ring holds 3 × 800-sample (16.7 ms) buffers ≈ 50 ms, so under a healthy device the sequencer ticks at exactly 60 Hz (each 800-sample buffer is drained every 16.7 ms). `bufferOutput` on a full ring discards silently (`AudioSource.java:150-163`) — that discard path is what M4-04 §5 relied on for the FMV's independent buffering.

### 3.2 Lock table (source-verified)

| Lock | Acquired by | Protects |
|------|-------------|----------|
| `AudioThread` monitor (`synchronized(this)`) | audio thread (entire source-tick loop); every game-thread `AUDIO_THREAD.*` music control call (`loadBackgroundMusic`, `setSequenceVolume`, `fadeIn/Out`, `start/stopSequence`, `setMainVolume`, `setReverb*`, quality changes, `loadXa/stopXa`, …, `AudioThread.java:320-460`) | music sequencer state, XaPlayer control flow, source list, device state |
| source monitor (`synchronized(source)` per `AudioSource`) | audio thread (tick/handleProcessedBuffers); game thread via `SPU.tick()`'s `source.bufferOutput`/`canBuffer` (`Spu.java:167-169`); `AudioSource.bufferOutput` itself | one OpenAL source's ring index + buffer queue |
| game `Sequencer.lock` (`waitForLock`, `Sequencer.java:42-46`) | SFX thread inside `tick()` (whole tick, `Sequencer.java:50`); game thread in `startRegularSound`/`startPitchShiftedSound`/`stopSoundSequence`/`stopSoundsAndSequences` (`Audio.java:1403-1413`, `1301-1379`) | the 24 `SequenceData124` slots, the 24 `PlayingNote66` slots, SFX-side note lifecycle |
| `Spu.class` monitor | SFX thread inside `SPU.tick()` (whole tick, `Spu.java:85`); every SPU control method — `setMainVolume`, `keyOn/keyOff`, `clearKeyOn`, `setNoiseMode/Frequency`, `setReverbMode/Volume/setReverb`, `mute/unmute`, `enable/disableReverb` (`Spu.java:372-483`); game thread via those same calls; nested inside `Sequencer.tick()` → `sssqFreeFinishedNotes` (`Sequencer.java:1428-1431`) | SPU edge words, main/reverb volumes, voice registers read/written by the tick, voice `adsrVolume` read by note freeing |
| `XaPlayer.playbackLock` | audio thread (`tick`); game thread (`loadXa`, `unloadOpusFile`) | Opus file handle + PCM buffer |

Lock ordering observed: `Sequencer.lock` → `Spu.class` (SFX thread tick; game-thread stop paths). No path acquires `Sequencer.lock` while holding `Spu.class`, and no path takes the AudioThread monitor under either — **no cycle** (source-verified).

### 3.3 Coverage and residual races

- **The M4-03/F-02 model holds and is confirmed complete for SPU state**: *every* SPU control method is `synchronized(Spu.class)`, including the edge-word writers `keyOn`/`keyOff`/`clearKeyOn` (`Spu.java:396-423`). All game-thread SPU writes are therefore serialized with the SFX thread's `SPU.tick()` edge snapshot. This document deliberately preserves the corrected R1 audit F-02 position: **there is no SPU control-state data race**, and no new one is claimed.
- **Residual benign races (layer-4 consequences of the reference's thread split; the original processed sound in-line with the 60 Hz game tick):**
  - `AudioSource.canBuffer()` reads `bufferIndex` without the source monitor (`AudioSource.java:117-121`), from the SFX thread (`Spu.java:167`) and the audio thread alike. Worst case: one stale buffer decision (a discarded or repeated 16.7 ms chunk); `bufferOutput` re-checks under the monitor.
  - Several `Audio.*` game methods mutate `soundEnv_800c6630` (fade flags, `reverbType_34`, `pitchShifted_22` staging, `sssqTempoScale_800bd100`) **without** the `Sequencer.lock`, while the SFX thread's `tick()` reads them under the lock (e.g. `sssqFadeIn`/`sssqFadeOut`, `Audio.java:1236-1280`; `sssqSetReverbType/Volume`, `Audio.java:1144-1176`). These are single-field boolean/int writes from the original single-threaded model — visibility is not guaranteed but the failure mode is at most one 60 Hz tick of stale state. Recorded, not "fixed" (research only).
  - Music-load callbacks run on the unpacker's I/O thread (async `loadDrgnDir(...).thenAccept`, `Audio.java:695-722`) and write the plain fields `musicLoaded_800bd782` / `_800bd0f0` there; the load flag itself is an `AtomicInteger`. Same one-tick-stale class.
- **Original timing padding removed:** the in-source `wasteSomeCycles` comment (`Audio.java:1377`) marks where the original wasted cycles before edge writes; the reference relies on the monitor + lockstep instead (GH#1719, §2.3).

## 4. Clock domains and event timing

### 4.1 Clock domain table (source-verified)

| Domain | Rate | Anchor | Notes |
|--------|------|--------|-------|
| Audio-thread min-pace | 120 Hz | `AudioThread.java:57` | floor only; actual source tick rate is OpenAL-drain-gated (§3.1) |
| Music sequencer output | 800 stereo float samples per `tick()` = 60 Hz at 48 kHz | `Sequencer.java:86` (`outputBuffer = (ENGINE_SAMPLE_RATE/60) × 2`), `:93-116` | `ENGINE_SAMPLE_RATE = 48000` (`Constants.java:13`) |
| Music effects/modulation granularity | `scale` iterations per tick, `800/scale` samples each (always 800 total) | `EffectsOverTimeGranularity.java` (`samples = 48000/(60×scale)`) | Retail(0)=per-sample, Double(1)=400×2, Quadruple(2)=200×4 … Duotrigintuple(5)=25×32; ramps/modulation run at `60×scale` Hz |
| Music event timing | **sample-accurate**: `samplesPerTick = 2,880,000 / (tempo × tpqn)` | `BackgroundMusic.java:208-210`, `:184` | see §4.2 |
| SFX thread | 60 Hz fixed | `Audio.java:1539-1540` | `SOUND_TPS = 60` |
| SFX event timing | **60 Hz-quantized**: delta varint → game ticks via `msPerTick` (§4.3) | `Sequencer.sssqReadDeltaTime` (0x8004a5e0, `Sequencer.java:1333-1426`) | |
| SPU rendering | 735 stereo int16 samples per tick = 60 Hz at 44.1 kHz | `Spu.java:27-28` (`BASE_SAMPLE_RATE/60`), `:84-170` | `BASE_SAMPLE_RATE = 44100` (`Constants.java:12`) |
| XaPlayer (Opus XA) | 480 int16 samples per tick = 10 ms at 48 kHz | `XaPlayer.java:38`, `:60` | 8-buffer ring ≈ 80 ms |
| FMV XA (retail XA, live decode) | 2016 samples/sector per lane at 37.8 kHz, buffered per render tick | M4-04 §5, M5-04 §4.4 | drop-on-full via `GenericSource` (16 buffers) |

### 4.2 Music: sample-accurate tempo mapping (source-verified)

`BackgroundMusic.setTempo`: `samplesPerTick = (48000 × 60) / (tempo × tickPerQuarterNote)` — i.e. *samples/minute ÷ (beats/minute × ticks/beat)*, which is the exact MIDI mapping **iff the SSSQ `tempo` field (u16) is beats-per-minute**. Corroboration for the unit: the dev MIDI tooling converts the same field with `60_000_000 / tempo` (µs/quarter out, BPM in) — `MidiWriter.java:56` (initial tempo), `:118` (in-stream 0x51 events) — and `MidiToSssq.java:87` inverts it. The header tempo (`sssq.readUShort(0x4)`) is applied at construction (`BackgroundMusic.java:83`); in-stream 0x51 events (`TempoChange`) re-apply it live.

### 4.3 SFX: 60 Hz-quantized tick conversion (source-verified, with a registered discrepancy)

For each SSSQ delta-time varint (up to 4 × 7 bits, `Sequencer.java:1337-1352`), the game sequencer converts to 60 Hz game ticks (0x8004a700-0x8004a7e4):

```
if soundPlaying: tempo = 60, ticksPerQuarterNote = 480      // SFX default, re-set every read
deltaFixed10   = delta × 10                                  // "1-decimal fixed-point" (0.1 ms)
msPerTick      = tempo × tpqn × 10 / (ticksPerSecond × 60)   // 0.1 ms, ticksPerSecond = 60
time           = remainder + deltaFixed10
remainder      = time % msPerTick
if remainder × 2 > msPerTick → ticks = (time − remainder + msPerTick)/msPerTick, longTick = true
else                     → ticks = (time − remainder)/msPerTick
```

With the 60/480 default this gives **`msPerTick = 80` (0.1 ms) = 8.0 ms per SSSQ tick** — 1 SSSQ tick = 1/8 game tick (0.48 game ticks per SSSQ tick, ±1 game-tick rounding via the `longTick` carry).

**Discrepancy (registered as Q-74):** the in-source debug log uses a *different* mapping — `delta ms = deltaTime / (tempo × tpqn / 60_000)` (`Sequencer.java:1355-1358`), i.e. 1 tick = `60000/(tempo×tpqn)` ms = **2.0833 ms** at 60/480 — which is *identical* to the music engine's BPM mapping (§4.2). The conversion code and the log therefore disagree by a factor of `tempo²/937.5` (3.84× at the default). If the SSSQ tempo unit is BPM (MIDI-tool-corroborated), the conversion code — not the log — is the one that doesn't fit the unit; if the original engine's unit is whatever the conversion code implements (1 tick = `tempo/7.5` ms at tps 60/tpqn 480), then the MIDI tool's BPM assumption is the reference author's (possibly wrong) interpretation. **Unresolvable without retail SSSQ bytes** — both sides are source-verified; the original behavior is Unresolved (Q-74). The music and SFX paths consume the *same* header/stream tempo field with *different* mappings, and the SFX path never reads the header tempo (always 60/480; the in-stream 0x51 override is **functionally dead** — the handler stores the value into `tempo_108`, and the same event's trailing `sssqReadDeltaTime` immediately clobbers the pair back to (60, 480) while `soundPlaying`, which is set at sequence load — M6-02 §3.8).

### 4.4 Ramps, fades, and quantization (source-verified)

- **SFX note ramps:** CC-driven volume/pan/portamento times scale as `value × 4 × ticksPerSecond/60` game ticks (`Sequencer.java:1010-1015` portamento, `:1055-1060` volume, `:1098-1103` pan) — with tps 60, 4 game ticks per CC time unit. Ramps are linear interpolations executed per 60 Hz tick in `sssqTickEffectsOverTime` (0x800470fc).
- **SFX main-volume fade** (`handleFadeInOut`, 0x8004b2c4, `Sequencer.java:1562-1599`): shared counter `sssqFadeCurrent_8005a1ce`; fade-in `vol = fadeInVol × current/fadeTime`, fade-out symmetric; the fade *completes* on the tick where `current == fadeTime` **without setting the final volume**, so a fade-in ends at `fadeInVol × (fadeTime−1)/fadeTime` (one step short of full) until the next volume command — an observed decompiled quirk, source-verified. The **retail fade-out bug is deliberately preserved**: `sssqFadeOut` (0x8004cd50, `Audio.java:1257-1280`) only forwards to the music side when `fadeTime > 1`, with the in-source comment "Retail bug: … fade out over 1 tick doesn't fade out at all. This was breaking music after Lenus 2. See GH#1623" — the SFX side (`handleFadeInOut`) has no such guard.
- **Music fades/ramps:** per effect-iteration (scaled by granularity, §4.1); `setMainVolume`/`setSequenceVolume` *cancel* in-flight fades/changes ("can happen when running at high speed", `Sequencer.java:305-309`, `:392-396`); `fadeOut` on an inactive source zeros the engine volumes immediately.
- **Key-on/off latency:** SFX edges are 60 Hz-quantized (≤16.7 ms, M4-05 S2); music key events apply at the sample-accurate command boundary.
- **`sssqTempoScale_800bd100`** (0x8001b118 script setter, 0x8001b0f0 getter, init `0x100`, `Audio.java:164`): **written but never read anywhere in the repository** — a dead state variable; the original's SSSQ tempo scaling is not implemented in the reference (source-verified by census).

## 5. Volume and routing

### 5.1 Volume domains (source-verified)

| Domain | Width | Where | Set by |
|--------|-------|-------|--------|
| SPU main volume | 15-bit (bit-14 sign convention) | `Spu.mainVolumeL/R`, applied `× mainVolume >> 15` per channel after clamp (`Spu.java:162-166`) | game `setMainVolume` (0x8004c894): 8-bit in, bit 7 → `(v<<7)+0x7fff` else `v<<7` (`Audio.java:1179-1192`) |
| Music main volume | 8-bit (bit 7 = full scale) | `Sequencer.engineVolumeL/R`: `l ≥ 0x80 ? 1 : l/256` (`core …/Sequencer.java:305-310`) | the same script call passes the *raw 8-bit* values to `AUDIO_THREAD.setMainVolume` (`Audio.java:437-441` `scriptSetMainVolume`, which also clears both fade flags) — one script write, two domains |
| Sequence (per-song) volume | 7-bit (0..127) | music: `backgroundMusic.volume` (float `v/128`); SFX: `sssq.volume_00` + per-channel `volume_0e = volume_03 × volume >> 7` rescale (`Audio.java:1200-1231`) | `setSequenceVolume` (music), `setSoundSequenceVolume` (per `PlayableSound`, 0x8004cb0c), default 40 on `playMusicPackage` (`Audio.java:565`) |
| Per-voice SFX volume | 15-bit (+sweep bit) | `Voice.volumeLeft/Right` (M4-03 §3.2) | `calculateNoteVolume` (0x8004ae94): `((chan × instr × vel × layer >> 14) × panVol[L/R] >> 7)`, sweep-packed `(_4a << 8) | (vol >> 7)` when `layer._0a != 0` |
| Per-voice music volume | float | `Voice.volumeLeft/Right` = `chan.adjustedVolume × instr.volume × layer.volume × velocityVolume` × pan pair (`core …/Voice.java:196-215`) | key-on + `updateVolume` on CCs |
| Reverb send volume | SPU: per-voice 15-bit through the same volume registers; music: float send per voice | `Spu.reverbOutputVolumeL/R` (set `×256` from the 0..127 game values, `Audio.java:1178`); music `reverb.setVolume(l/128, r/128)` | `sssqSetReverbVolume` (0x8004c558) drives **both** sides; type via `sssqSetReverbType` (0x8004c494): SPU `setReverbMode(0)+enableReverb+setReverb(config)` + music `setReverb(type)`; type 0 clears SPU key-on edges, zeroes reverb, disables |
| Host player volumes | float 0..1 | `Spu.playerVolume`, `Sequencer.playerVolume`, `XaPlayer.playerVolume` | config `MASTER_VOLUME × {MUSIC,SFX}` (`GameEngine.java:253`, `Spu.java:68`, `XaPlayer.java:42`) |

Nine ROM reverb presets: `reverbConfigs_80059f7c` (`Audio.java:62-76`), ROM locations 0xfb28…0xfc8c, 32-field `ReverbConfig` each; `initSound` selects **type 3** at volume 0x30/0x30.

### 5.2 Pan (source-verified)

- **SFX:** `calculatePan(4, 0)` at note-on uses the *layer* pan; CC-driven pan changes merge three pans through `panMergeTable[panMergeTable[layerPan + instrumentPan] + channelPan]` (128-entry table, `Sequencer.java:852-905`) — the table is identity clamped at 0x7F **except index 0x67, which holds 0x5B instead of the monotonic value** (an in-data anomaly; effect bounded to one merge step, source-verified, no determination made). `getPanVolume` indexes `panVolumeTable[128]` (L = `0x7F − value`, R = `value`; linear 0x02 steps to 0x78, flat 0x80 after, `Sequencer.java:917-953`).
- **Music:** `LookupTables.getPan(channel.pan, instrument.pan, layer.pan)` three-way merge (M6-03).
- Note: the SFX note-on path passes `calculatePan(4, 0)` — the constant-4 branch, i.e. **layer pan only**; the three-way merge is reached only when the CC path passes a non-4 value (in the inspected handlers, the CC pan *change* writes go straight to the note's ramp state — the merge table is exercised from the CC pan handler's volume computation, `Sequencer.java:1040-1105`; exact retail reachability is `Inferred`).

### 5.3 Routing anomalies (layer 4, source-verified)

- **LODXA music plays under the SFX volume knob**: `XaPlayer.playerVolume = SFX_VOLUME × MASTER_VOLUME` (`XaPlayer.java:42`). Retail XA tracks (the game's "music" audio on submaps/battles in the original) are therefore mixed with SFX, not with BGM, in the reference. `Inferred` intentional (no in-source comment); noted for the M6 report.
- **FMV audio (live XA) plays under the FMV knob** (`FMV_VOLUME × MASTER`, M4-04 §5 / M5-04 §4.4) — a third knob.
- **BGM plays under the MUSIC knob** plus its 7-bit sequence volume (default 40/128 ≈ 0.31) and the 8-bit main volume.

## 6. Data flow

### 6.1 Asset → runtime map (source-verified; formats per M2-10, M4-03/04)

| Retail asset | Reference consumer | Entry point |
|--------------|--------------------|-------------|
| `SECT/DRGN0.BIN/5815 + i×5/…` (5-file music dirs), `5850 + ch×5/…` (wmap) | `BackgroundMusic` (songId file, bank-count file, SSSQ, SSHD, MSPB bank; multi-bank concat from `SECT/DRGN0.BIN/{id+i}` when the SSHD's `soundBankSize_04` exceeds the primary bank, `BackgroundMusic.java:38-58`) | `Audio.loadMusicPackage` / `loadWmapMusic` / battle `FUN_8001fb44` |
| `SECT/DRGN0.BIN/5739/{0..3}` (menu SFX: 2-byte unknown, 0x64-byte index table of 50 `{patch,sequence}` pairs, SSHD, MSPB) | `SoundFile`/`PlayableSound0c` | `Audio.loadMenuSounds` (0x8001e5ec) |
| per-state SFX (submap/battle/wmap/BENT) — SSHD + soundbank MRGs | same SFX path | `loadSoundFile`-family (M6-02) |
| `XA/LODXA0n.XA/<track>.opus` (unpack-time Opus transcodes) | `XaPlayer` | `Audio.playXaAudio` (0x8002c984, `Audio.java:904-930`) — stage 3: `AUDIO_THREAD.loadXa(Loader.loadFileSync("XA/LODXA0%d.XA/%d.opus"))`, `_800bf0cf = 4`; `stopXaAudio` |
| `.IKI` FMV AUDIO sectors (retail XA) | `XaAdpcm.decode(data, data[19])` → 37.8 kHz shorts → `GenericSource(STEREO16, 37800)` | M4-04 §5, M5-04 §4.4 |

**SSHD container** (`Sshd.java`): magic `SShd` @ 0x0c, `sshdSize_00`, `soundBankSize_04`, 28 subfile offsets @ 0x10 (−1 = absent); subfile 1 = `VolumeRamp` (0x80-byte velocity table @ +2), 2 = `WaveformList` (breath waveforms, 0x40 bytes each), 3 = `PatchList` (patch → sequence lists, relative offsets), 4 = **SFX SSSQ** (via `Sssqish`), 0 = same shape as 4 (unused in inspected paths).

**The two SSSQ header shapes** (source-verified):

| | Music SSSQ (standalone file) | SFX SSSQ (SSHD subfile 4) |
|---|---|---|
| Parsed by | core `BackgroundMusic` (raw reads) + dev `Sssq` (magic `SSsq` @ 0x0c) | `Sssqish` |
| Channel table | **16** × 0x10 @ 0x10 | **24** × 0x10 @ 0x10 (`Sssqish.java:18-20`) |
| Extra | — | `Entry2List` instrument table @ **0x190** (u16 count, s16 entry offsets, `Instrument` = 10-byte header + `InstrumentLayer10`[16-byte]s; `Sssqish.java:26-63`) |
| Event stream | @ 0x110 | after the instrument table |

`ChannelInfo` (0x10, both shapes): instrumentIndex @ 0x2 (s8, −1 none), volume @ 0x3, pan @ 0x4 (0x40 center), modulation @ 0x9, pitchBend @ 0xa, _0b, breath @ 0xc, volume(2) @ 0xe.

### 6.2 SFX pipeline detail

`playSound` → `queuedSounds_800bd110` (game thread) → per-frame `startQueuedSounds` (0x8001a4e8, `Audio.java:216-263`; delays in *game ticks*, scaled by `tickMultiplier` at script entry, `Audio.java:333-335`) → `startQueuedSound` (regular or pitch-shifted) → **`SEQUENCER.waitForLock(loadSoundIntoSequencer)`** — the game thread *blocks on the SFX thread's lock* until a free `SequenceData124` slot (0..23) is assigned (0x80048d44) → the SFX thread's next `tick()`s the sequence, events flow through the 0xa0 handler → `findMatchingOrFreeNote` (§8.1 below) → voice registers set → deferred `keyOn` edge word → applied at the next SPU sample slot → 44.1 kHz int16 → 16-buffer ring → OpenAL. On success the (soundFile, soundIndex, bent) triple is recorded in `_800bc9a8[sequenceIndex]` (`SpuStruct08`, `Audio.java:264-293`) for the per-sound stop paths.

## 7. Worked traces

### 7.1 BGM start — world-map destination music (source-verified)

1. Engine state sets the destination chapter → `Audio.loadWmapMusic(chapter)` (0x8001f708, `Audio.java:775-797`): dedup check `AUDIO_THREAD.getSongId() × 5 + 5815 == 5850 + chapter × 5` (song ids are global package numbers, e.g. wmap chapter c → id 7+c); otherwise `unloadSoundFile(8)` (stop + `_800bd0f0 = 0`), set flag `0x80`, async `loadDrgnDir(0, 5850 + c×5)`.
2. I/O callback `musicPackageLoadedCallback` (0x8001dabc, `Audio.java:512-518`): `playMusicPackage(new BackgroundMusic(files, fileIndex), true)` + clear flag.
3. `playMusicPackage` (0x8001e29c region, `Audio.java:563-571`) under the AudioThread monitor: `loadBackgroundMusic` (swap in the command stream + instrument/velocity/breath tables), `setSequenceVolume(40)` (cancels in-flight fades/changes), `startSequence` (unpause + activate source).
4. Audio thread: sequencer `tick()` drains the command ring sample-accurately; each 0x90 event allocates a music voice (free → low-priority → oldest, `core …/Sequencer.java:141-159`) and renders float samples through the per-voice reverb send; end-of-track rewinds (repeat) or stops (`endOfTrack`, `Sequencer.java:272-276`).
5. Victory variant: `Battle.FUN_8001fb44` loads the battle package (volume 40/128) *and* a separate `victoryType` dir; `createVictoryMusic(victoryFiles)` re-uses the battle package's breath controls, velocity ramp and sound font with the victory SSSQ (`BackgroundMusic.java:110-131`); played later via `AUDIO_THREAD.loadBackgroundMusic(victoryMusic)` (`Battle.java:1465`).

### 7.2 SFX — menu sound with delay and repeat (source-verified)

1. `Audio.playMenuSound(40)` (0x80023870, `Audio.java:888-894`) → `playSound(menuSoundFile, 40, 0, 0)` → the 13-arg `playSound` fills a `QueuedSound28` (type 3; `soundFile.indices_08[40]` → `{patchIndex, sequenceIndex}`) and enqueues on `queuedSounds_800bd110`.
2. Each frame (game thread): `startQueuedSounds` decrements `initialDelay_24`; at 0 → `startQueuedSound` → no pitch-shift params (−1) → `startRegularSound` → **`waitForLock`** → `loadSoundIntoSequencer` finds the first `!soundLoaded` `SequenceData124` slot, stages the `SssqReader` on the sequence bytes, hand-offs one-shot `reverbEnabled_23`/`pitchShifted_22` staging from `soundEnv`, returns the slot; the queue entry is removed (or kept as a repeat marker when `repeatDelayTotal_20 != 0`).
3. SFX thread, same or next 60 Hz tick: `Sequencer.tick()` walks slot 0..23; for this slot, `while(deltaTime == 0)`: read event (4-byte peek: command/param0/param1/param2-instrument), dispatch, read delta varint. First 0xa0 (note, velocity, instrument) → instrument layer lookup via `startingKeyPosition_06` → `findMatchingOrFreeNote` → full `PlayingNote66` population (channel/instrument/velocity/layer volume chain, pan volumes, breath/modulation flags, layer noise/reverb flags) → voice pitch via `calculateSampleRate` (12-entry `sampleRates` table, ±octave shifts, 64ths-of-note math, `Sequencer.java:807-820`), `voice.data = soundbank FileData`, `voice.startAddress = layer.soundOffset_04`, ADSR registers from the layer → deferred `keyOn` bit.
4. End of channel loop: `SPU.setNoiseMode`/`setReverbMode` words, then `SPU.keyOff(edge)` / `SPU.keyOn(edge)` (accumulated across all 24 channels in `soundEnv.keyOn_3a/keyOff_3e`), then `sssqTickEffectsOverTime` (modulation/breath waveform, portamento glide, volume/pan ramps) and `handleFadeInOut`.
5. SPU tick: 735 sample slots; the edge is applied at the first slot (M4-03 §2); the voice decodes soundbank ADPCM, applies ADSR × note volume × main volume, reverb send for reverb-flagged voices; 16-buffer 44.1 kHz ring → OpenAL.
6. Note off: 0xa0 with velocity 0 → `keyOffMatchingNotes` (matches same playableSound + nextCommand + noteNumber + `_0c == 1`, preferring the same sequence) → deferred `keyOff` + `finished_08`. The voice is freed in `sssqFreeFinishedNotes` (0x8004a8b8) **only once `adsrVolume < 0x10`** (1/4096), which also decrements the priority bookkeeping and the `_0d` used-note counter.

### 7.3 XA track playback (source-verified)

`playXaAudio(stage, archiveIndex, fileIndex)` (0x8002c984, `Audio.java:904-930`): guarded `0 < fileIndex < 32`, `stage < 5`; stage 3 → `AUDIO_THREAD.loadXa("XA/LODXA0%d.XA/%d.opus")` + `_800bf0cf = 4`; stage 2 just sets `_800bf0cf = 4` (load already pending elsewhere); other stages no-op. `XaPlayer.loadXa` opens the Opus file in memory, reads channel count (mono/stereo format switch), requires ≥ 4 buffers (40 ms) of PCM, pre-queues 4 buffers, plays; each audio-thread tick reads one 480-sample frame, scales by `playerVolume` (SFX×MASTER), and unloads at EOF (`XaPlayer.java:52-129`). `stopXaAudio` → `stopXa` (clear pending + stop + unload, `AudioThread.java:397-402`).

## 8. Reference-specific enhancements and compatibility classification

| # | Item | Layer | Classification | Notes |
|---|------|-------|----------------|-------|
| A1 | OpenAL device hosting, 3/8/16-buffer rings, tick suppression on full ring | 4 | Behaviorally-equivalent hosting | Original: SPU hardware DMA; the *observable* contract is the per-domain sample output (M4-05 framework) |
| A2 | **Opus replaces retail XA for LODXA music** (unpack-time transcode, M2-10 §4) | 4 | Replacement (format-only at runtime) | Original played XA via SPU DMA; no `asset-validated` comparison exists |
| A3 | **Float music engine** (`core/audio/sequencer`) vs the decompiled SFX sequencer | 4 | Re-implementation (behavioral mirror) | Structural parallelism is in-source (`Voice.java` field comments cite `playingNote.*`/`voice.*`), but the music engine runs sample-accurate float at 48 kHz where the original rendered all SSSQ through the 44.1 kHz SPU domain — *timing and output are not claimed sample-identical to retail* |
| A4 | Music quality configs (interpolation Quarter..Quadruple, pitch Quarter..Octuple, granularity Retail..Duotrigintuple; defaults Double/Quadruple/Double) | 4 | Enhancement | "Retail" enum values are the reference author's nominal original (no external PSX/LoD doc consulted — M10 worklist) |
| A5 | Device hot-plug / `ALC_EXT_disconnect` reconnect | 4 | Enhancement | §2.5 |
| A6 | SFX 0x80/0x90 handlers → `RuntimeException("No longer implemented")` | 4 (decompilation gap) | Documented gap | Retail SFX SSSQ must use 0xa0 (§1); M6-02 inventories all unimplemented handlers |
| A7 | SFX sustain CC (0x40) and NRPN 0x11/0x12/0x13 → `assert false` | 4 (decompilation gap) | Documented gap | `Sequencer.java:1129-1131`, `:1642-1647` |
| A8 | Retail 1-tick fade-out bug preserved (GH#1623); lockstep `adsrVolume = 0` fix (GH#1719); `wasteSomeCycles` removed | 4 | Bug-preservation / re-timing fix | §4.4, §2.3 |
| A9 | `sssqTempoScale_800bd100` dead (settable, never read) | 4 | Gap | §4.4 |
| A10 | `panMergeTable[0x67] = 0x5B` anomaly | 4 (data) | Anomaly, undetermined | §5.2 |
| A11 | LODXA under the SFX knob; FMV under the FMV knob; BGM under the MUSIC knob | 4 | Routing choice | §5.3 |
| A12 | `Sssq` (16-channel, magic-checked) parses *music* files; `Sssqish` (24-channel) parses SFX subfiles | 3 (format fact, source-verified) | Documented | §6.1 |
| A13 | Dev MIDI tooling (`MidiWriter` SSSQ→MIDI, `MidiToSssq` MIDI→SSSQ, `SoundbankDecoder`, `SoundfontWriter`) | 4 | Tooling | BPM↔µs convention documented here; full treatment M6-02 |
| A14 | Music-load callbacks write game state from the I/O thread | 4 | Concurrency consequence | §3.3 |

## 9. Register and cross-document impact

- **Q-68 (M4-03 §7) — updated, item (b) resolved in the reference:** M4-03 §1's claim that "there is no stealing in the reference — when all 24 are used, a new note fails to allocate" and the S3 row's "24 voices 1:1 bound … no stealing" are **corrected in place** by M6: note allocation is *dynamic* in `Sequencer.findMatchingOrFreeNote` (0x80048000, "This code has been verified", `Sequencer.java:600-691`) with four phases — (1) **matching-note retrigger**: a used note with the same `minKeyRange` and same `playableSound` is reused (monophonic-layer retrigger), with the priority bookkeeping (`_0a`) decremented around it; (2) **priority-steal when the used-note count `_0d` reaches `playingSoundsUpperBound_03`** (20 from `initSpu`, 8 after `initSound`'s `setMaxSounds(8)`): the used note with the lowest priority whose `maxKeyRange` allows the new note is stolen, otherwise the note is **dropped** (returns −1; the 0xa0 handler then advances the reader and logs nothing — silent note drop); (3) **round-robin free scan** over `voiceIndex_10`; (4) fallback to finished/unused notes by lowest priority when all 24 are in use. Q-68(a) sweep gap, (c) reverb network, and (d) 60 Hz edge quantization stand unchanged (M4-03 text updated to point here).
- **Q-74 (new): SSSQ tempo unit and SFX tick-quantization mapping.** The SFX conversion code (1 tick = `tempo×tpqn/(tps×60)` ms; 8.0 ms at the 60/480 default), the in-source debug log (1 tick = `60000/(tempo×tpqn)` ms; 2.0833 ms), and the MIDI tooling (tempo = BPM, `60_000_000/BPM` ↔ µs) cannot all be right together (§4.3). Needs retail SSSQ bytes (a tempo value + an audible duration) or original-engine documentation — asset-validated-pending / external (M10).
- **Q-22/Q-23** (external PSX SPU/XA): unchanged, still M10-external; this document adds no layer-1 claims.
- **M4-03 §1 line 42, §7 Q-68 line, M4-05 S3 row:** corrected in place (voice allocation; see the M6-01 commit).
- **Carried to M6-02:** SSSQ event grammar per family (payload widths, continuation rule, delta varint placement, per-event instrument byte, note-targeted CCs, 0x60/NRPN repeat machinery), the full unimplemented-handler inventory, `SoundFile` load paths for the nine non-menu types, and a worked retail-shaped command-stream example (synthetic — no retail bytes exist).
- **Carried to M6-03:** music engine internals (LookupTables, Reverberizer, Fading, AdsrEnvelope, VoiceCounter fixed-point, VoicePriority), `BackgroundMusic` multi-bank assembly, XA→Opus transcode (Q-47/Q-57), XaPlayer vs FMV-XA volume routing, and Q-48/Q-69 closure.
