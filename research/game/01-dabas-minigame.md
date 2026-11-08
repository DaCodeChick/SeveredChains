# M9-01 — The Dabas Minigame ("Mogul")

**Milestone:** M9 (Game Systems: Content and Rules), deliverable 1 of 7 (01 Dabas, 02 title, 03 submap, 04 world map, 05 battle, 06 characters & progression, 07 inventory & items). **Date:** 2026-11-08. **Approved starting point:** `5cbdd65`.

**Scope.** The Dabas minigame as embedded in the reference: the "Mogul" machine model (CPU semantics used, timers, SPU, display, memory map), the complete game rules (caves, digging, encounters, battles, bosses, Minitos, rewards, speech bubbles, death/completion), the save format (Save60 + tiles), host integration (entry point, reward mapping, save storage, crash isolation), and the script-vs-hardcode boundary (RQ7 for this system).

**Primary sources.**
- Dig-Dabas @ `4c1fbfe` (pinned, clean): `core/` (Cpu, Psr, Hardware, InterruptController, PowerControl, Random, Timers, Timer, Spu, MathHelper, FileData, InputType, Display, `memory/Method`), `game/` (Dabas.java 6029 lines — the dememulated game logic with `@Method` ROM addresses; DabasVars.java — the ROM memory map + `loadRom`; DabasInterface; Main/TestInterface — standalone presentation), `game/types/` (Save60, Sprite1c, SpriteData24, BattleData48, SoundStruct4c, SpeechBubble08, MonsterSounds08, Struct/Struct1c_2/Struct1c_3/Struct2/Struct_6, MenuData08).
- Severed-Chains @ `0c08c679b` (pinned, clean): `legend/game/dabas/Dabas.java` (host machine interface, 244 lines), `legend/game/dabas/DabasRewardsEvent.java`, `legend/game/DabasManager.java`, `legend/lodmod/config/DabasSaveDataConfig.java`, `legend/lodmod/LodConfig.java` (DABAS_SAVE_DATA, LOD id arrays), `legend/game/inventory/screens/DabasScreen.java` (the in-game wrapper, 452 lines), `legend/game/inventory/screens/MainMenuScreen.java` (entry button), `legend/game/scripting/GameVarParam.java` (the shared 192 item-id encoding, case 40/41).
- Git history of Dig-Dabas (non-destructive `git log`): the dememulation commit sequence.

**Validation.** All findings are **source-verified** (reference code / commit history) unless noted. The equivalence of the dememulated Java with the original `DABAS.BIN` machine code is established structurally (mirrored memory map, address annotations, host loads the retail file) but not instruction-by-instruction — that leg is **asset-validated-pending** (no disc images). No `asset-validated` claims. ROM *data values* (monster weights, boss depths, chest contents, pitch tables) are read from the disc at run time and are therefore not cited as constants; their layout is documented, their values are data (see §2.4, §6.8).

**Numbering.** Continues the milestone J/D series from the M8 documents (J16–J19, D1–D28 were M8); M9 starts fresh per milestone: J20–J24, D1–D13.

---

## 1. System overview and layers

Dabas is a Dig-Dug-style arcade minigame embedded in the game: the player digs through a tile cave, fights slimes, collects mushrooms/bones/coins, visits Minitos's lair for stat buffs, and descends past five bosses. It is **not PSX hardware and not a Fate-scripted system** — it is a self-contained machine running a binary (`DABAS.BIN` from the disc path `OHTA/MCX/DABAS.BIN`, R0 inventory; M2-08 §4 documents the fixed-offset blob format).

Layer attribution:

| Part | Layer | Basis |
|---|---|---|
| The minigame rules/content (caves, monsters, Minitos, bosses, rewards) | **3 — LoD-specific** | Exists in retail as the `DABAS.BIN` binary; in the reference it is the same binary's dememulated Java + its ROM data tables (D8) |
| The "Mogul" machine model (CPU flag semantics, 3 timers + RTC, DAC SPU, 32×32 1bpp display, 5 buttons) | **3, as modeled from the binary** — the model is exactly the interface the binary uses; the original arcade unit's physical hardware is not documented in-repo (D1) |
| The dememulation (Java translation, function tables, remixed PRNG) | **4 — reference-specific** | Dig-Dabas repository design (D1, D9, J20–J22) |
| Host integration (renderer takeover, input action mapping, reward granting, per-save config storage, crash-to-toast, i18n) | **4 — reference-specific** | `dabas/Dabas.java`, `DabasScreen.java` (D6, D7, D13) |

The plan's RQ6 for this system is answered in full below, and the three register items it carries are closed in §9 (Q-30, Q-31, Q-32).

---

## 2. The machine model ("Mogul")

### 2.1 De-emulation of the retail binary (D1, Q-30)

The reference does not *emulate* DABAS.BIN generically; it runs a **dememulation**: the disassembled game code translated to Java, with only the hardware interface the code actually uses retained as emulated peripherals.

Evidence chain (all source-verified):

1. **The host executes the retail file.** `legend/game/dabas/Dabas.java:76` — `this.dabas.start(Loader.resolve("OHTA/MCX/DABAS.BIN"))`. `Hardware.start` (Dig-Dabas `core/Hardware.java:57-64`) calls `DabasVars.loadRom(path)`, then the host callback `loadGame(memcardSave_2000580, memcardTiles_2000600)`, starts the timer thread, and runs `new Dabas(...).main()`.
2. **The memory map mirrors the binary.** Every `DabasVars` field is named with its virtual address; `loadRom` loads every constant table at a hardcoded ROM offset where **virtual address = 0x2000000 + ROM offset** (memcard save `0x2000580` ↔ ROM `0x580`, memcard tiles `0x2000600` ↔ ROM `0x600`, sprite pointer table `0x200d398` ↔ ROM `0xd398`, sequences `0x200f200` ↔ ROM `0xf200`). The method bodies carry `@Method(0x...)` annotations preserving the original addresses (code region `0x4000000+`: `main` @ `0x4000000`; data/ram region `0x2000000+`: `entry` @ `0x2000810`, `rand` @ `0x2000ab6`, …).
3. **The CPU model is ARMv4/Thumb flag semantics only.** `core/cpu/Cpu.java` is annotated `/** ARMv4 */` and models just the CPSR flags (N/Z/C/V, bits 31/30/29/28) and the flag-producing operations the translated code uses: `lslT`/`lsrT` (with the ARM "shift of 32 or more yields zero, carry = last shifted-out bit" rules, including the `right == 32` and `right == 0` cases), `cmpT`/`cmpA` (sub) and `cmnT`/`cmnA` (add) with signed-overflow and carry computation. There is **no register file and no instruction interpreter** — the arithmetic is inlined in the Java translation, the `Cpu` object only carries CPSR state (J20). The T/A method suffixes (T = Thumb, A = ARM) corroborate the ISA identification.
4. **The commit history records the dememulation.** Dig-Dabas history (newest first, abridged): `4c1fbfe` Implement next dig ← `a4065a5` Externalize save/load/quit ← `b15eae6` Refactor out graphics and audio ← … ← `34472fd` **Finish dememulation** ← `622a25f` **Dememulate everything but animations** ← `f11fbef` **More dememulation** ← `d74639e` **Dememulate remaining ram** ← … ← `490c615` **Dememulate** ← `fbbc971` **Yeet a lot of emulation stuff**. The project demonstrably started as an emulator and was dememulated into high-level code (R0 already noted this; M2-08 §4 documented the ROM layout).

Consequence: the "machine" is best understood as **the interface contract of DABAS.BIN**, not a physical device model. What the binary needs — and therefore what any faithful reimplementation must provide — is exactly: a frame tick (T0), half-rate game logic, 32×32 1bpp rows, 5 level-triggered buttons, a reprogrammable 3-timer set + BCD RTC, and a variable-rate mono 8-bit DAC. The original arcade unit's physical specifications (real CPU, real display) are external to all in-repo evidence and are not claimed.

### 2.2 Peripherals and memory map

Peripheral map (addresses from `core/Hardware.java:35-39` comments):

| Peripheral | Virtual address(s) | Behavior (source-verified) |
|---|---|---|
| `InterruptController` | `0xa00_0000` | Input mask: `tick()` rebuilds `interruptImmediate` from the held-button set; `getInput()` returns the mask. Level-triggered (no interrupt queue) |
| `Timers` (3×) | `0xa80_0000`, `0xb80_0000` | See §2.3. Runs on a dedicated `Thread("Timers")` loop calling each timer's `tick()` (J23) |
| `PowerControl` | `0xb00_0000`, `0xd80_0000` | `cpuFreq = 62_500 * (1 << (mode - 1 & 0xf))` Hz (modes 0..16; mode 0 and 16 both give ×32768). `modeWrite` logs the change and retimes all timers |
| `Spu` | `0xd80_0010` | Mono 8-bit, 44 100 Hz output, 100 ms buffer (`SAMPLES_PER_TICK = 4410`). `dataWrite(v)` writes one sample `(byte)(v >> 8)` at the current `sampleRate`; the SPU **upsamples by integer repetition** (`44100 / inFrequency` copies per sample) to fill 44.1 kHz. When the 4410-sample buffer fills, it flushes via `dabasInterface.bufferAudio`. `ctrlRead() = 0x1`; `ctrlWrite` is a deliberate no-op (audio always enabled in the dememulation) |
| Display | (RAM) | `int[32]` bitfields = 32×32, 1 bpp (`core/display/Display.java`). The host expands bit `b` of row `i` to RGB: `(b ^ 0x1) * 0xff` — **a set bit is a black pixel on white** (set-bit = "on"/drawn) |

RAM (runtime, `DabasVars`): camera `0x200021c/0x220`, camera offsets `0x224/0x228`, cave origin `0x22c/0x230`, `ticks_0000234` (game-frame counter), `stateIndex_0000239` (top-level state, ubyte), input words `0x23a/0x23b` (`inputNow`/`inputHeld`, T0-sampled edges used by the death/completion menus) and `0x318-0x31b` (`input`/`pressedThisFrame`/`oldInput`/`releasedThisFrame`, the game-path words), `displayHp_000023c` (animated displayed HP; the 32-bit pixel mask is cached in the struct at `0x25c` with `monsterDisplayHp`/`monsterHpPixels` and a decoration time flag), speech-bubble words `0x240/0x242`, `_0000244` (boss-floor pattern counter), monster HP words `0x248/0x24c/0x250`, `gameStateIndex_0000258`, cave-traversal state `0x278` (player/minitos sprites, direction, `visitedCaveCount_14`, dig/fall counters), the 7-entry tile-action transition table `_0000290`, menu state `0x2b8`, battle data `0x2ac`/`0x6a4`, 5 sprite slots `0x5b8`, the live save copy `0x644`, render buffer `0x338` (32 u32 rows) + row pointer `0x320`, tile grid `tiles_00003b8` (**16 rows × 32 columns**, u8), memcard save `0x2000580` + memcard tiles `0x2000600` (16×32).

### 2.3 Timers, clocking, and the frame model (D2)

Three timers, each with a 16-bit **period** (reload) and 16-bit **mode** register. From `core/timers/Timer.java`:

- Active when `mode & 0x4` (bit 2). Divisor by `mode & 0x3`: **2** (0 or 3), **32** (1), **512** (2).
- `tickRate = 1e9 * divisor * period / cpuFreq` ns; the timer thread fires the callback once per elapsed tickRate, with a failsafe that skips ahead (warning log) when 3+ ticks behind (IDE-pause protection).
- **Timer 0** is the CPU/frame tick. Its callback is `handleInput` (`entry`, @ `0x2000810`), and `Timer.updateSpeed` pushes its Hz to the host: `if (index == 0) dabasInterface.setFps(getHz())` — the host caps the render loop to T0.
- **Timer 2** is the DAC pump (see §2.5). **Timer 1** is initialized (mode 0, reload `0xffff`) but no dememulated code reprograms it — recorded as unused in the translated code.

Game speed programming (`setGameSpeed`, @ `0x2000d06`): the packed u32 carries **upper 16 = frame divider, lower 16 = nominal rate** (javadoc: "the number of frames to wait between each game frame" / "the frames per second the game will run at"). T0 is set to `period = timerSpeeds_2008524[clockRatio] * divider / rate` (u16 ROM table, 9 entries — one per CPU speed), `mode = 0x6` (active, ÷512), and `timer0Ticks_0000314` / `lastFrameTickCount_08` reset. The absolute T0 Hz therefore depends on the ROM's `timerSpeeds` values (asset-validated-pending); the *structure* is what matters:

- `initSystem` (@ `0x20018a8`): CPU speed 5, game speed `(1 << 16) | 10` — 10 nominal ticks during load screens.
- `initGameAndRun` (@ `0x20019b0`): CPU speed 6, game speed `(1 << 16) | 60` — the main game; also `alarmVolume = 1`, `initSound`, `initSoundTimer`, `initAndChangeState(4)` (load menu), `ticks = 0`, `clearSprites`, monster HP defaults 32.
- Every state re-sets CPU speed 6 + 60 on entry (all `initState*` methods).

**Half-rate game logic.** `tickInput` (@ `0x2002c4c`) is the gate: it increments a counter and returns 1 on odd counts. `tickGame` executes the game state machine only when `tickInput() == 0` — i.e. **game logic runs at T0/2**, and `ticks_0000234` (the counter used for blink phases, animation cadence, and the time-indexed chest contents) counts *game frames* (T0/2 ticks). `tickBattle` calls `tickInput()` unconditionally (battle logic every game frame, return ignored). `tickMap` increments `ticks` itself and drives its 11-state menu machine every game frame (menu input reads the T0-sampled `pressedThisFrame` edges).

Boot sequence: `main` @ `0x4000000` → `initHardware` @ `0x4000098` (all timers mode 0 / reload `0xffff`, SPU ctrl 0, power mode 7) → `DoExecute` @ `0x4001890` (`SetCpuSpeed(7)`) → `entry` @ `0x2000810`: installs the T0 callback, seeds the RNG (`seed_00 = 1`), copies the ROM's `0x11658` 7-entry table to `_0000290` (the tile-action transition table), zeroes the render buffer/tiles/sprites/live-save, then `startGame` → `initSystem` → `initGameAndRun`.

### 2.4 ROM data tables (layout only — values are disc data)

`DabasVars.loadRom` reads the complete constant set at fixed offsets (full table = M2-08 §4 + this list). Sizes and types (source-verified):

| ROM offset | Field | Shape | Use |
|---|---|---|---|
| `0x840c` | `audioTimerReloadValues` | 132 × u16 | DAC pitch table (timer reload per pitch class) |
| `0x8524` | `timerSpeeds` | 9 × u16 | T0 period correction per CPU speed |
| `0x859c` | `playerVolumes` | 2 × u8 | Boot volumes |
| `0x85e8` / `0x8968` | `backgroundA` / `backgroundB` | 0x380 × u8 each | Two scrolling background layers (8-tick alternate) |
| `0x8ce8` | `speechBubbleTextPixels` | 17 × 8 u32 | Bubble text bitmaps |
| `0x8f08` / `0x9148` | bubble bg mask off/on | 9 × 16 u32 | Bubble backgrounds |
| `0x9388` | `tileAttributeActions` | 0x70 u8 | Tile attribute → action id |
| `0x93f8` | `mapTileTypes` | 0x70 u8 | Tile attribute → minimap type |
| `0xd198` | `caveTiles` | 7 × 48 u8 | The seven cave-type tile strips |
| `0xd2e8` | `speechBubbles` | 22 × `SpeechBubble08` | Bubble definitions (bg, text, var type/x) |
| `0xd398` | `spritePixels` | 44 × u32 ptr → ROM slice | Sprite animation bitmaps |
| `0xd470` | `bossDepths` (copy 1) | 5 × u32 | Next-boss depth (stats screen, §6.9) |
| `0xd674` | `chestContents` | 4 × 32 u8 | Chest item per chapter, indexed by `ticks & 0x1f` |
| `0xd6f4` | `caveTypeRolls` | 12 u8 | Random cave-type table |
| `0xd760` / `0xd788` | `caveChestIndices` / `monsterRewardChestIndices` | 20 u8 each | Random chest-behavior selectors |
| `0xdbbc` … `0xdcac` | player animations | slices | attack / crit-available / death / etc. |
| `0xe28c` | `monsterIdToSound` | 21 × u32 | Monster → sound-pair index |
| `0xe2e0` | `monsterSounds` | 3 × `MonsterSounds08` | attack/hurt sound ids |
| `0xe2f8` | `sprites_200e2f8` | 21 × `SpriteData24` | Combatant data: type, w/h, **hp (i16)**, **attack (i16)**, `_0c` (i16), **aggression (i16)**, 8-byte active-frame list, 3 animation ptrs |
| `0xe5ec` | `monsterWeights` | 15 × 25 u8 | Spawn weights per monster × depth modifier |
| `0xe763` / `0xe77b` | drift tables | 3 × 8 i8 each | Player/monster shuffle offsets by distance band |
| `0xe7f8` | `bossDepths` (copy 2) | 5 × u32 | Boss depth thresholds (§6.9) |
| `0xe80c` | `bossRewards` | 5 × u32 | Boss special-item rewards |
| `0xe820` | `_200e820` | 24 i8 | Completion-screen row selectors |
| `0xe894` | `menuBackgroundPixels` | 9 × 32 u32 | Menu panels |
| `0xed14` | `glyphPixels` | 29 × 8 u8 | Digits/heart/flag glyphs |
| `0xedfc` / `0xef5c` | help text masks | 11 × 8 u32 | Help strings |
| `0xf0bc` | `statsScreenAnimatedFlagGlyphs` | 8 u8 | Animated flag frames |
| `0xf0c4` | `loadGameScreenPixels` | 32 u32 | Load-menu pattern |
| `0xf200` | `sequences` | 47 × u32 ptr → ROM slice | The 47 sound sequences |
| `0x11658` | `_2011658` | 7 × u32 | Tile-action → game-state transition table (copied to `_0000290` at boot; indexed by action − 2, §6.2) |

`SpriteData24._10[8]` is the **attack-active frame list**: a battle action deals damage only when the current animation frame byte is in this list; a `-1` entry means "no active frame" (checked first, `FUN_2004f14` @ `0x2004f14`).

### 2.5 Audio: the DAC pump and sequencer (D5)

- **Pump.** `setDacPumpTimer` @ `0x20080d0`: T2 callback = `handleTimer2Fiq`, reload `0x1000`, mode `0x4` (active, ÷2). On each pump tick one DAC sample is written.
- **Square wave.** `handleTimer2Fiq` @ `0x20083a4`: `negate ^= 0x7f`; if `negate == 0 && sample != 0`, `sample ^= 0xff00` (flip the high byte = sign of the amplitude); then `SPU.dataWrite(sample)`. The `SoundStruct4c.volume_38` field is documented in-source as "Current magnitude of audio wave (always positive, the FIQ callback negates it every other sample)". One period = two pump ticks, so the audible tone is a **±volume square wave at half the pump rate** (classic DAC-pump trick).
- **Sequencer.** `tickSoundSequence` @ `0x2008110` runs each game frame. Active sequence = `soundIndex` (1..46; 0 = off) → `sequences[soundIndex]`. Command set (source-verified):

  | Command | Meaning |
  |---|---|
  | `0x00`–`0x9f` | Set pitch: reprogram T2 with `audioTimerReloadValues[cmd - 12 + frequencyModifier + clockFrequencyModifier]` (T2 off → reload → on), then `SPU.setSampleRate(T2 Hz)` |
  | `0xa0` | End sequence |
  | `0xa1` | Set envelope ramp: hi = `min(1 << (param >> 4 & 0xf), 0x7f)`, lo = `min(1 << (param & 0xf), 0x7f)` |
  | `0xb0` | Key off (`volume = 0`, `negate = 0`) |
  | `0xb1` | Pointer setup — dememulation has the parameter read but the store removed (source-commented unused) |
  | `0xb2` | `frequencyModifier = param - 60` (transposes all pitches) |
  | `0xe0` | Set repeat: `repeatOffset = next command offset`, `repeatCount = param` (`0x7f` = forever) |
  | `0xef` | Do repeat: jump to `repeatOffset` unless `repeatCount` exhausted |
  | `0xf0` / `0xf1` | Set note volume / sequence volume |
  | `0xf3` (and any other unmapped) | `frequencyChangeTicks = param` (glide/hold length before the next command) |

- **Volume.** Per pump tick the envelope ramps toward/away from `0x7f` by the `0xa1` rates; `volume_38 = ramp * playerVolume * noteVolume * sequenceVolume >>> 13`.
- **Clock compensation.** `setSoundClockSpeed` @ `0x2008018`: `clockFrequencyModifier = (7 - clockSpeed) * 12` — 12 semitones per CPU-speed step, keeping pitches constant across the 2×-per-step frequency scale (consistent with `cpuFreq = 62500 * 2^(mode-1)`).
- **Enable policy.** `alarmVolume_0000256 ∈ {0, 1, 2}` with **2 = muted** (`playSound` no-ops, `initSound` disables the DAC). The in-game sound menu cycles 2 → 1 → 0 → 2 and re-applies `volumes_200d484[alarmVolume]`. `playSound(id)`: bit 7 (`0x100`) selects a clock-speed class (re-runs `initSoundTimer` on change); low byte = sequence id.
- **Known dememulation caveat.** In-source TODO at the pitch-command branch: "maybe the reason the music runs too fast is because the CPU speed limited how quickly this could run? It loops immediately so the frequency would keep getting overwritten before a sound could play" (`tickSoundSequence`). The original machine's pump/CPU interplay may have limited effective tempo; the dememulation runs the loop as fast as the game frame allows. Recorded as a reference-specific timing approximation (D5, layer 4).

### 2.6 RNG and RTC (D9, D10)

- `srand(seed)` @ `0x2000ae0` stores `seed_00` (RAM `0x2000200`) and `RANDOM.setSeed(seed)`, returning the old seed. `rand()` @ `0x2000ab6` returns `RANDOM.nextInt() >>> 16` — the **upper 16 bits** of the generator.
- The generator (`core/Random.java`) extends `java.util.Random` and overrides `nextInt()` with the **upper 32 bits of the Stafford variant-4 mix64** (zimbry 2011 bit-mixing, cited in-source). This is a deliberate dememulation choice: the retail machine's own PRNG is not preserved, so **minigame outcomes (monster rolls, cave rolls, chest picks) are distributionally modeled but not bit-identical to the original binary** (D9, layer 4). The observable seed sources are preserved:
  - boot: `seed = 1` (`entry`);
  - `srand(GetBcdTime())` at: battle entry (`initStateBattle` @ `0x2006184`), boss entry (`initStateBoss` @ `0x2006264`), and load-menu continue/new-game selection (`tickLoadGameMenu` @ `0x200765c`) — followed by one or two discard `rand()` calls.
- The seed word is **not part of Save60** — every machine entry re-seeds from the clock.
- **RTC.** `Timers.rtcTimeRead()`: BCD-packed `dow (Sunday = 1) << 24 | hh << 16 | mm << 8 | ss` from the host `LocalDateTime`. The stats screen draws it as a real-world clock (BCD digits) — arcade authenticity (D10).

---

## 3. Host integration (Severed-Chains side)

### 3.1 Entry and lifecycle (D13)

- **Entry point.** In-game menu button `lod_core.ui.ingame_menu.mogul_dabas` (`MainMenuScreen.java:104`) → `DabasScreen` (a `MenuScreen` on the `menuStack`).
- **The 11-stage wrapper state machine** (`DabasScreen.LoadingStage`, retail addresses in comments: save data `8011d7c0`, message box `8011dc90`, render `LAB_801031cc`/`LAB_80103390`):

  | Stage | Behavior |
  |---|---|
  | `INIT_0` | Fade in (2, 10), draw the Dabas menu glyphs + menu |
  | `LOAD_DATA_1` | `DabasManager.hasSave()`? If not: fresh `Save60`. If yes: parse rewards into host entries (§3.3) and post `DabasRewardsEvent` |
  | `DISPLAY_DATA_2` | Menu: **Take items / Discard items / Next dig** (i18n `lod.ui.dabas.menu_*`); escape (back action, no repeat) → `CLOSE_100` |
  | `START_GIVE_GOLD_3` → `GIVE_GOLD_4` → `FINISH_GIVE_GOLD_5` → `CONFIRM_GIVE_GOLD_6` | Gold hand-over: the menu-box shows Dabas gold vs player gold; while gold > 10 and no confirm, **10 gold transfers per stage-tick** with a tick sound every other tick; player gold capped at **99 999 999** |
  | `FADE_OUT_7` | Fade out + `sssqFadeOut(20)` |
  | `PLAY_8` | When the full-screen fade reaches `0xff`: **`new Dabas()`** — the minigame takes over; comment: "Dabas takes over so this won't run til the game is closed" |
  | `FADE_IN_9` | Fade in + `sssqFadeIn(20, 0x7f)` → back to `LOAD_DATA_1` (the loop repeats for the next dig) |
  | `CLOSE_100` | Unload the screen |

- **Machine takeover** (`dabas/Dabas.java`): on first `setTicker` the host saves renderer state (render callback, FPS limit, projection size, render mode, clear color), switches to `PERSPECTIVE` 320×320 with black clear, and maps **five engine input actions** to the machine buttons (M8-01 input actions, `lod` registry ids): `INPUT_ACTION_GENERAL_MOVE_UP/DOWN/LEFT/RIGHT` → up/down/left/right, `INPUT_ACTION_SMAP_INTERACT` → action (fire). Window close → `shutdown`.
- **Per-frame** (`tick()`): run the machine ticker (wrapped in `try/catch(Throwable)` — a machine crash shows a 5-second `lod.ui.dabas.crashed` toast and shuts the game down), then expand the 32×32 bitfield into a 320×320 ortho quad (bit set → black pixel, §2.2).
- **Audio.** One `GenericSource` on the engine audio thread, `AL_FORMAT_MONO8` at `Spu.SAMPLE_RATE` (44 100 Hz); `bufferAudio` copies each 4410-sample machine tick into the source if it can buffer.
- **Shutdown** (`shutdown()`): `dabas.stop()` (stops the timer thread), removes all input/close listeners, restores all saved renderer state (incl. `PLATFORM.setInputTickRate(oldFps)`), removes the audio source, deletes the display texture and quad.
- **Save/quit from inside the minigame:** the in-machine "Save & Exit" menu (`drawSaveAndExitMenu` @ `0x2006a8c`) does `saveGame()` + `shutdown()` on fire release — the host loop then returns to `LOAD_DATA_1` with the fresh save.

### 3.2 Save storage (D6, Q-32)

- Config entry `DABAS_SAVE_DATA` (`LodConfig.java:29`, registrar `lod`): `ConfigEntry<byte[]>`, **default = 0x260 zero bytes**, **`ConfigStorageLocation.SAVE`** (persisted in the per-save `.dsav` config tail — M7-02 §3), category `OTHER`, identity codec. So the minigame save is **per-campaign-save**, created on the first in-machine `saveGame()`.
- Wire layout (both writers verified): `Save60` (0x60 bytes, §4) + **512 tile bytes** (16 rows × 32 u8, row-major) = **0x260 bytes total**, all little-endian (via `MathHelper.get/set`, byte order low→high).
- `Dabas.loadGame` (host → machine, at `Hardware.start`): skipped entirely if the config entry is absent (`hasConfig` false = never saved); otherwise `Save60.load` + tiles. The tile arrays **exist inside the machine model** (`memcardTiles_2000600`, `int[16][32]`) — the host merely fills them; a first-time session starts all-zero (D7→D6, Q-32 closed).
- `Dabas.saveGame` (machine → host, `saveGame()` @ `0x2002bd0`): copies runtime state (camera, cave origin, player position) into `Save60`, `numberOfTimesSaved_04++`, mirrors the 16×32 tile grid, serializes to the 0x260 buffer, `CONFIG.setConfig`.
- `DabasManager.hasSave/load/save` is the screen-side view (Save60 only; tiles travel through the machine only). `numberOfTimesSaved == 0` is the in-machine "no game to continue" marker (`tickLoadGameMenu`).

### 3.3 Reward mapping (D7, Q-31)

The machine never knows host items: it stores raw ids in `Save60`. `DabasScreen` at `LOAD_DATA_1` maps them:

- **Item id encoding (shared with the script layer):** `id < 192` → equipment, host id = `LodMod.EQUIPMENT_IDS[id]`; `id >= 192` → consumable item, host id = `LodMod.ITEM_IDS[id - 192]` (192 equipment slots). The same boundary is the retail script-layer item space: `GameVarParam` set-case 41 (script-given inventory entries) uses the identical test (`val < 192` → equipment via `EQUIPMENT_IDS[val]`, else an `ItemStack` via `ITEM_IDS[val - 192]`). **Notably, the retail hand-off itself is script-mediated:** set-case 40's in-source decompiled comment reads "When given items on submaps (Sapphire Pin from Dabas, Psych Bomb from Savan), if you have a full inventory it'll set the dropped items to 1, add the item to the drops list, and open the 'Too Many Items' screen" — i.e. in the original, Dabas's special-item reward was granted through the script game-var drop path (var 40/41) with full-inventory overflow handling, whereas the reference routes the same hand-off through `DabasRewardsEvent` + direct grants (§7). This is the in-repo evidence that the minigame ids live in the **retail item-number space** (D7, source-verified; the actual id table values are lodmod registry data).
- `items_14[0..5]` (6 slots) → `event.rewards`; `specialItem_2c` (always equipment) → `event.specialReward`; `gold_34` → `event.gold`.
- **`DabasRewardsEvent`** (`extends Event`, not cancelable) is posted via `EVENTS.postEvent` — mutable public payload (`gold`, `rewards` list, `specialReward`), so **mods can alter, remove, or add rewards before they are displayed or granted** (M8-02 exact-class dispatch; the event is a domain co-located class, one of the 36 outside `modding/events/**`).
- Display: up to 6 reward icons + special item + gold on the Dabas menu. **Take items**: capacity check first — equipment list size + taken ≥ `0x100` (256) or item count + taken > `INVENTORY_SIZE_CONFIG` → `lod.ui.dabas.too_many_items` alert, nothing moves; otherwise each reward is granted by type (`ItemStack` → `gameState.items_2e9.give`, `Equipment` → `giveEquipment`, `Good` → `goods_19c.give(GAMEPLAY)`), the save is zeroed (items, special, gold), `DabasManager.save`, one of 6 random `take_response_*` boxes, then the gold hand-over flow. **Discard items**: clears items + special only (gold remains banked in the machine save), one of 10 random `discard_response_*` boxes. **Next dig**: stamps `chapter_00 = chapterIndex`, and on `completionType == 1` transitions it to `2` (clearing the pending special item); `bossesKilled == 5` also resets `numberOfTimesSaved` to 0; one of 14 random `next_dig_response_*` boxes, then the fade-out → play loop.
- Every action stamps `chapter_00` with the current host chapter index — the chapter is a host-supplied input to the machine's chest content (`chestContents[chapter][...]`).

### 3.4 i18n surface

All user-visible strings are mod i18n keys (M8-03 §4 model): `lod.ui.dabas.{title, acquired_items, special_items, acquired_gold, menu_take, menu_discard, menu_next_dig, menu_*_description, take_response_1..6, discard_response_1..10, next_dig_response_1..14, too_many_items, crashed}` plus the menu button `lod_core.ui.ingame_menu.mogul_dabas`. Inside the machine there are no i18n strings — all text is ROM bitmap glyphs (29-glyph font, 11 help masks, 17 bubble texts).

---

## 4. Save format (Save60 + tiles)

`Save60` (Dig-Dabas `game/types/Save60.java`; 24 little-endian u32 = 0x60 bytes). Field table with machine semantics:

| Offset | Field | Semantics (source-verified) |
|---|---|---|
| `0x00` | `chapter_00` | Host chapter index (stamped by the host on take/discard/next-dig; selects `chestContents` row) |
| `0x04` | `numberOfTimesSaved_04` | In-machine save counter; **0 = no continuable game**; reset by the host when 5 bosses are done (D13) |
| `0x08` / `0x0c` / `0x10` | `mushrooms_08` / `bones_0c` / `coins_10` | Consumables (cap 99 each, §6.4); Minitos currency |
| `0x14`…`0x2b` | `items_14[6]` | Reward item ids (retail encoding, §3.3); 0 = empty |
| `0x2c` | `specialItem_2c` | Boss special-item id (equipment encoding) |
| `0x30` | `power_30` | POW stat (cap 999); **player battle attack = `power / 5`** |
| `0x34` | `gold_34` | Machine gold (cap 9999 in-machine, 99 999 999 on host hand-over) |
| `0x38` | `bossesKilled_38` | Boss kills 0..5; indexes `bossDepths_200d470` (stats) and gates completion |
| `0x3c` | `completionType_3c` | 0 = not completed, 1 = completed (boss just beaten — next dig starts fresh from the boss floor), 2 = starting new dig (javadoc-verified) |
| `0x40` / `0x44` | `currentHp_40` / `maxHp_44` | HP (max cap 999; new game = 30/30) |
| `0x48` / `0x4c` | `playerX_48` / `playerY_4c` | Player position at save (px; x wraps 0..255) |
| `0x50` / `0x54` | `cameraX_50` / `cameraY_54` | Camera at save |
| `0x58` / `0x5c` | `caveX_58` / `caveY_5c` | Cave origin at save |

Plus the **16×32 u8 tile grid** (512 bytes) immediately after = the full cave state (the game's world *is* the grid — `loadSave` @ `0x2007550` restores save + tiles + camera and re-enters state 0).

---

## 5. Game flow: top-level states

Nine top-level states (`stateInitializers_20085a0` / `stateTickers_20085c4`, indices in `stateIndex_0000239`):

| # | Init | Ticker | Meaning |
|---|---|---|---|
| 0 | `initStateGame` | `tickGame` | Main dig game (sub-states §6) |
| 1 | `initStateBattle` | `tickBattle` | Slime battle (monster roll, §6.7) |
| 2 | `initStateBoss` | `tickBattle` | Boss battle (same ticker, §6.9) |
| 3 | `initStateMap` | `tickMap` | Pause menu (11 states, §6.10) |
| 4 | `initStateLoadGameMenu` | `tickLoadGameMenu` | New game / continue |
| 5 | `initStateDeath` | `tickDeath` | Death cutscene → continue (§6.11) |
| 6 | `initStateCompletedGame` | `tickCompletedGame` | Post-final-boss celebration + menu |
| 7 | `initCompletedAllBosses` | `tickCompletedAllBosses` | "All bosses defeated" scroll |
| 8 | `FUN_20065d0` | `tickCompletedGame` | Post-boss (non-final) interstitial menu |

Load-menu logic (`tickLoadGameMenu` @ `0x200765c`): `numberOfTimesSaved == 0` → immediate new game (RNG reseeded from the RTC). Otherwise a two-option screen (default = continue): **new game** clears the save, sets `power = maxHp = hp = 30`, generates the starting cave (type 5 at row 0), pre-fills floor rows 0x20/0x40/0x60, sets row 4 (except column 3) to attribute 25, places the camera, enters state 0; **continue** branches on `completionType`: 1 → `loadCompletedSave` (cave type 6, state 6), 2 → `startNewDig` (resumes at the last boss floor, §6.9), else `loadSave`.

---

## 6. Game rules (the dememulated game code)

### 6.1 World model (D8)

- The world is the **16×32 u8 tile grid** (256 px wide × 128 px tall visible window); **x wraps** (player x `0xff` → `0`; camera wrap uses a `+244` offset — `moveCamera` @ `0x2002260`: `cameraX = playerX + offsetX + 244` when `playerX < 12`, else `playerX + offsetX - 12`).
- Tile attribute (u8) → **action id** via `tileAttributeActions_2009388` (112 entries); `getTileAction(x, y)` @ `0x20022f4`. The 24-entry `tileActionCallbacks_200d700` (IntSupplier table, addresses `0x2002d28`+): 0/1/9 = fall (`state 17`), 6 = heal dig (state 26), 7 = gold/gravel dig (state 27), 18 = enter lair (`rollCaveType(8)`), 19 = enter lair (`rollCaveType(0x50)`), 21 = lair with item (sets tile 0x19 below the player, state 31), 22 = **boss** (when `cameraOffsetY >= 10`: player to x 128, cave origin set, `initAndChangeState(2)`), the rest no-op (return 0).
- `runTileAction` @ `0x2002e14` runs only when player x is 8-aligned; it also eases `cameraOffsetY` toward 10 when the action ≥ 16 (deep floor) and back to 0 otherwise.
- **Background:** two 0x380-byte layers alternate every 8 game frames (bit 3 of `ticks`); `drawBackground` @ `0x2001a84` composites five 8-px tile strips per row using `lsrT`/`lslT` with the ARM 32+-shift rules (the de-emulator comment: "Shift methods are necessary because ARM shift of 32+ always yields 0"). Tile offset = `(attr & 0xf0) * 8 + (attr & 0xf)` into the layer.
- **HP bars:** player bar at row 0: `displayHp` eases toward `currentHp` by `(maxHp + 2) / 32` per frame — rising plays sound 5 only in top-level state 0 with game state > 6, falling is silent; the bar is a cached 32-bit mask `lsrT(0xffffffff, 32 - displayHp * 32 / maxHp)` and **blinks off for half the 4-frame phase (bit 2 of `ticks`) whenever the mask is below 0xff (i.e. not full)**. Monster bar at rows 30/31, step `monsterMaxHp / 32`.

### 6.2 Player movement and digging (D8)

- Player speed: **1 px per game frame** (states 12/13 = walk left/right; `FUN_20025c4`/`FUN_2002624` refill floor rows as the camera descends). The full game-state table has **67 entries** (`gameStates_200d7a4[67]`, indexed by `gameStateIndex_0000258`): 0–10 the opening descent/boot sequence (states 3–7 walk-in + bubbles, 8–9 animations, 10 the recovery animation), 11 idle input, 12/13 walk, 14/15/16 dig left/right/down, 17 fall, 18/19 landings, 20 "No", 21–27 pickups, 28–30 boulder aftermath, 31–63 Minitos lair, 64–66 death revive, and the state is returned to 10/11 after every sub-sequence.
- Input is sampled per T0 (`handleInput`): `inputNow` = newly-pressed-this-game-frame (XOR edge). In the main idle state (`handlePlayerInput` @ `0x20033b4`, state 11): left/right press sets direction + mirror flag and resolves the destination tile's action via `getGameStateTransitionForTileAttribute` @ `0x2003188` (gravel on the destination tile is collapsed first, below):
  - action 0 or 9 → walk (state 12/13) or fall (17);
  - action 1 → dig (state 14/15/16);
  - action 2..8 → **ROM transition table** `_0000290[action - 2]` (the 7-entry table copied from ROM `0x11658` at boot — the retail tile-behavior table);
  - other (non-zero) → state 20 (the "No!" bubble state for blocked moves; bubble 17).
- **Digging** (states 14/15/16 — left / right / down; the down-dig also spawns a boulder sprite 0x2a above the player on animation restart): the dig animation runs and a 6-frame counter (`_11`) starts; per frame the counter decrements (`FUN_2003654`) and at 0 the target tile is cleared to attribute 0, a collapse sprite 0x29 is spawned (dealloc-after-anim), and sound 7 plays; when the player's dig animation loop ends, `FUN_2003574` @ `0x2003574` resolves the result: (1) if the tile **above** the dug tile has attribute 6 (boulder soil) → boulder sprite 0x29 spawns above and **state 28**; (2) else a phase gate — `(ticks & 0xf) != 0` (15/16 of digs) → state 10; (3) else if the action of the tile below, either side, is ≥ 16 (no support) → state 10; (4) else with probability **`rand & 0x30 != 0` (3/16)** a dust sprite 0x29 spawns (dealloc-after-anim) → **state 30**; (5) else the dug tile becomes attribute **0x4a** → state 10. States 28/29/30 resolve back to 10.
- **Gravel** (action 9, `handleFallingInGravel`): checked on the destination tile both when moving from idle (state 11) and while falling (state 17) — if the destination's action is 9, the action is reset to 0, the tile attribute is cleared (0), and a collapse sprite 0x29 is spawned (dealloc-after-anim).
- **Falling** (state 17, `FUN_20037e4` @ `0x20037e4`): 1 px/frame descent with a fall counter (`_13`); whenever the player's y is 8-aligned, the tile below is evaluated: gravel collapses (above); action 10 plays sound 2 and is cleared; every 32 px of descent the floor is regenerated 88 px below (`FUN_200264c(y + 88, 1)`, §6.3) and the companion sprite (`sprite_0c`) is cleared; if the tile below's action is 0 the fall continues, else landing: fall distance `_13 > 8` → **state 19** (hard landing / boulder hit), else **state 18** (ground impact); both resolve back to 10.
- **Pickups from digging:** coins (state 25: +1 coin, sound 1, bubble 3); mushroom/bone dig (state 21): 50/50 by `rand & 0x8` — +1 mushroom (sound 23, bubble 1) or +1 bone (sound 14, bubble 2); the item-dig variant (state 22) gives **+5** of the rolled type (bubble var 5). **Heal dig** (state 26, tile action 6): heals `maxHp / 10` (bubble 5, sound 5). **Gold dig** (state 27, tile action 7): +1 machine gold (bubble 4, sound 6). Stat caps: consumables **99**, `maxHp`/`power` **999**, machine gold **9999** (`increasePlayer*` @ `0x2002834`+).
- **Boulders** (sprites 0x29/0x2a): fall 1 px/frame; on landing they become tile 0x45 (heavy) or solid, with a bounce animation selected by `ticks & 7`.

### 6.3 Caves (D8)

- **Cave types:** 7 strips of 48 tiles (`caveTiles_200d198`), plus types used directly for the start (5), completion (6), and death-resume (3). `generateCave(y, type, x)` @ `0x2002314` writes 4 rows × 12 tiles at the cave origin and sets the flanking columns solid (attributes > 3 become 1).
- **Rolling a cave** (`rollCaveType` @ `0x2002ca4`): `roll = rand() % 10`; `visitedCaveCount` (Struct_6, resets on a Minitos visit) counts non-Minitos caves since the last one — when it reaches 4 the next roll is forced to 0; `roll == 0` resets the counter. `type = caveTypeRolls_200d6f4[roll]` (12-entry table; rolls 0–9 used). **Type 4 triggers an immediate battle** (`initAndChangeState(1)`). Minitos's lair is the forced type (cave tiles type 0, per the `rollCaveType` call sites with offsets 8/0x50 entering the lair).
- **Floor generation** (`FUN_200264c` @ `0x200264c`, called every 32 px of descent and on new game / next dig / death-resume): for the new row `r4 = y / 8`, if `r4` is **unsigned less than** `bossDepths_200d470[bossesKilled] - 8` (explicit ARM `cmpT` + carry check), the row is a **boss-floor pattern row** selected by the per-floor cycle counter `_0000244` (incremented per row): 0 → floor + boss wall 0x4f, 1 → floor only, 2 → floor + boss wall 0x42, ≥3 → plain floor. Otherwise the normal floor is written; when the caller flag ≠ 0 (deeper rows), additionally **5–20 decoration actions** (`(rand & 0xf) + 5`) are applied, each picked through the ROM selector `_200d45c[20]` into a 5-entry action table (`_200d448`), the previous row's attribute-6 (cave-mouth) tiles are re-sealed to 1, and — time-flagged (`_000025c._14`, alternated by `ticks & 1`) — a **Minitos cave (tile type 0)** is generated at a random x (`(rand >>> 4) % 18` + 1) on the new row. Below the boss depth the normal floor continues; the boss-trigger tile action (22, §6.1) starts the boss battle on the boss floor.

### 6.4 Treasure chests (D8)

Five chest behaviors (`treasureChests_200d774`), selected by `rand() % 20` through the ROM selector tables:

| Behavior | Effect |
|---|---|
| `treasureChestOneGold` / `FiveGold` / `TenGold` | +1/+5/+10 gold, gold-count bubble, sound 6 |
| `treasureChestHealOrOneGold` | Full heal if below max (bubble 7, sound 5), else +1 gold |
| `treasureChestItem` | First free `items_14` slot gets `chestContents_200d674[chapter][ticks & 0x1f]` — **the item depends on the game-frame time of opening** (32 time-slices per chapter); all 6 full → falls back to +5 gold. Bubble 8, sound 26 |

Open sites: lair chests (`openCaveChest` state 23, selector `caveChestIndices[20]`) and post-monster-reward chests (`openMonsterRewardChest` state 24, selector `monsterRewardChestIndices[20]`); both play the item-dig animation, set the surrounding tiles to chest-open attributes (0x1b/0x3b), and resolve on animation loop.

### 6.5 Speech bubbles (D8)

22 bubble definitions (`speechBubbles_200d2e8`); 17 text bitmaps (`speechBubbleTextPixels`) plus 9 background bitmaps each. Each = background index + Y, text index + X/Y, optional numeric var (type 1–2: 2 digits, 3: 3 digits, 4: 4 digits, 5: second text line). Rendered with a 1-px bounce (bit 3 of `ticks`) and the var from `speechBubbleVar_0000242` (count-down values during Minitos trades, stat values during buffs). Code-verified index usage (assignment sites in `Dabas.java`):

| # | Usage |
|---|---|
| 0 | Off (clear; `handleAndDrawSpeechBubbles` draws only when index ≠ 0) |
| 1 / 2 | Mushroom count / bone count (dig +1) |
| 3 / 4 | Coin count (dig coin) / gold count (gravel +1) |
| 5 | Heart count (heal dig; var = `maxHp/10`) |
| 6 | Gold count (treasure chest +1/+5/+10; var = amount) |
| 7 | "Food!" (chest full heal) |
| 8 | "Item!" (chest item) |
| 9 | "Hello!" (Minitos greeting) |
| 10 | "Bye!" (Minitos parting) |
| 11 | Heart count (Minitos max-HP buff; var = maxHp) |
| 12 | Pow (Minitos power buff; var = power) |
| 13 / 14 / 15 | Mushroom / bone / coin count (Minitos consumption, counting down) |
| 16 | Coin count (Minitos coins finished) |
| 17 | "No!" (blocked move) |
| 18 | "Help!" (death cutscene) |
| 19 | **Never assigned in the dememulated code** (the `DabasVars` javadoc says "Good luck") |
| 20 | "Special item" (final-boss victory) |
| 21 | "Let's go!" (opening descent) |

**Discrepancy note:** the `DabasVars` javadoc enumerates 21 bubbles (against 22 array slots) and its list is shifted relative to code usage from index 5 onward (e.g. javadoc 6 = "Food!" vs code 6 = gold count; javadoc 20 = "Let's go!" vs code 21). This is a dememulator documentation inconsistency; the ROM's own bubble table is the authority for which glyphs are drawn (asset-validated-pending).

### 6.6 Minitos's lair (D8)

Minitos (sprite type 9) appears in the forced cave (the roll forced to 0 after 4 non-forced cave visits, or a natural roll 0 — which cave-type slot maps to the lair is ROM data, asset-validated-pending; the lair caves are tile type 0, per the floor-decoration code and the counter-reset semantics). The lair sequence (game states 31–63, addresses `0x2003ef8`+):

1. **Entry (state 31):** the player's idle animation plays while the camera eases in (`cameraOffsetX` → 12); then Minitos is allocated at `(caveX + 80, caveY + 16)` → state 32, where Minitos plays the lair sound (`playSound(0x100 | 15)` — the clock-speed-class sound, reused throughout the lair and death scene) and walks toward the player → 33.
2. **Greeting (state 33):** when the player's greeting animation loop ends: Minitos's animation is set, and if `currentHp < maxHp` the player plays the heal animation and **gets a full heal** (`currentHp = maxHp`) → state 34 (bubble cleared); if HP is already full, bubble 9 ("Hello!") instead.
3. **Trade dispatch (state 34, `FUN_200402c`):** checks run in the order coins → bones → mushrooms, **each overwriting the previous**, so the effective priority is **mushroom > bone > coin**: mushrooms → 35, bones → 37, coins → 53, none held → 61 (straight to parting).
   - **Mushrooms → max HP**: per mushroom, every 4 ticks `mushrooms--`, `maxHp += 1` (stops at cap 999, consuming the rest), and `currentHp = maxHp` **after each increment** (sound 3). Count shown as a count-down bubble (13).
   - **Bones → power**: same cadence, `power += 1` (cap 999), no heal (bubble 12/14).
   - **Coins → gold**: first `coins *= 5` (state 53, bubble 16), then per coin every 4 ticks `coins--`, `gold += 1` (cap 9999 consumes the rest, sound 6) (bubble 15/16).
4. **Parting (state 61, `FUN_2004a58`):** bubble 10 ("Bye!"), Minitos leaves, camera eases back, play resumes (states 62/63 → 10 → idle 11).
5. **Death revive** reuses the lair machinery (states 64–66): **64** (`tryAgainAfterDeath`): `currentHp = 0`, Minitos allocated at player + 24, lair sound; **65**: Minitos finishes its animation, `currentHp = maxHp`, `displayHp = 0`; **66**: waits until the displayed HP bar has refilled (`currentHp <= displayHp`), plays the lair sound, then falls into the parting state 61 (→ 62 → 63 → 10 → 11).

### 6.7 Slime battles (D8)

Entry (`initStateBattle` @ `0x2006184`): RNG reseeded from the RTC (+2 discards); **monster selection by depth-weighted table** — `depthModifier = (y - 24) / 8 / 100` (floor row ÷ 100, bounded by the 25-column table — the effective max depth is therefore data-limited by the ROM's `bossDepths`, asset-validated-pending); walk `monsterWeights[15][modifier]` accumulating until `totalWeight > rand & 0xff`; `monsterIndex = selected + 1` (index 0 is the player's own combatant data in `sprites_200e2f8`).

Combatant setup (`battleStateInit` @ `0x2004f5c`): battle ticks reset; camera bounds set from the cave origin (±32/±64, negated when mirrored); **battle start sound 30** plays; the player's battle sprite deallocates the map sprite.

- **Player attack = `power_30 / 5`** (integer division); player positioned at `playerX + 6` (mirrored variant reflected); sprite data = `sprites_200e2f8[0]` (index 0 is the player's own combatant record); attack reach `_09` and sprite offset `_08` derived from the record's data fields.
- **Monster attack = `spriteData.attack / 3`**; monster at player + 30 (0x1e); `monsterMaxHp = monsterHp = spriteData.hp`; attack/hurt sounds via `monsterIdToSound` → `monsterSounds`.

Actions (player tickers, 7-entry table @ `0x200e794`; monster tickers, 3-entry @ `0x200e7b0`):

| Player action | Behavior |
|---|---|
| 0 idle | Fire press → attack (1, sound 22); any direction → guard (3, sound 12); else every 32 battle ticks: **crit chance = `rand & 0xff <= 0xd` (14/256 ≈ 5.5%)** → crit-available (4, sound 25, exclamation sprite). Position drifts by `_200e763[band][rand & 7]` on battle ticks ≡ 0/5/10 (mod 16), band = far(>6 px)/mid/close |
| 1 attack | Attack animation; guard can be triggered mid-attack |
| 3 guard | If the monster is mid-attack on an active frame, the guard animation stretches (10 ticks) — **guarding negates damage** |
| 4 crit available | Fire → trigger (5); direction → guard; animation loop expires → idle |
| 5 trigger crit | Walk right 1 px/tick until reaching the monster → 6 |
| 6 crit attack | Same as attack but **damage × 2** |

Damage resolution (`FUN_2005648` @ `0x2005648`, every fight frame): a hit lands only when the attacker is on an **attack-active animation frame** (the `SpriteData24._10[8]` list) *and* the range check passes (player: `player._09 + player._00 >= monster._00`; monster: `monster._09 + monster._00 <= player._00`). Monster hit → `monsterAction = 2` (stunned), `monsterHp = max(0, monsterHp - damage)`, hurt sound, monster knocked **+2 px**; player hit → if guarding (action 3): sound 31 and **5 px pushback only** (no damage); else the player is stunned (action 2), sound 18, **5 px knockback**, and `currentHp = max(0, currentHp - monsterAttack)`. A separation rule applies whenever the positions cross (±1 px apart per frame). If `monsterHp` reaches 0: non-boss (`_02 & 0x4 == 0`) → death sprite 0x2b (16×16), sound 29, **battle state 4**; boss → player sprite frozen, **battle state 9**. If `currentHp` reaches 0: sound 28, **battle state 6**.

Monster AI (3 tickers: idle / attack / counter-attack): the attack flag `_16` is set (a) with probability 2/256 on every 16th battle tick, or (b) when the monster is within 20 px and `spriteData._0c > rand & 0xff` (per-monster approach threshold); the attack fires when the idle animation reaches its ready frame (frame byte `0xff` at offset +4). After an attack the monster may immediately re-attack (up to 2 follow-ups, each 50/50). Counter-attack state (2) attacks when `aggression_0e > rand & 0xff` (per-monster counter probability). The monster drifts via `_200e77b` on the same 0/5/10 (mod 16) cadence as the player, bands far (>6 px) / mid / close (<3 px).

Battle state machine (15 states @ `0x200e7bc`, `battleStates_200e7bc`): **0** `battleStateInit` → **1** intro (camera x eases to the monster's center; the player walks in 1 px per 2 battle ticks while more than 8 px behind) → **2** approach (camera eases to `monster - 4` every 2 ticks; both enter when the gap ≤ 8 px) → **3** **fight** (player + monster action tickers, `FUN_2005648` damage, sprite placement, camera = midpoint) → on monster death: non-boss → **4** (death sprite 0x2b 16×16 + sound 29, set by the damage resolver; the death animation plays) → **5** walk out (camera eases back to the player) → `FUN_2004d24` (deallocates battle sprites, re-allocates the map player at the exit position, sets tile **0x4c** on the battle row — the lair mouth is dug open — and `initAndChangeState(0)`); boss (`_02 & 0x4`) → **9** boss death (phase animation `animPtr_18[2]`; after 90 ticks → death sprite 0x2b, sound 29) → **10** (on death-anim loop end: player celebration sprite + trophy sprite 0x28 (16×8) allocated) → **11** (after 120 ticks: sound 26) → **12** (bubble 20 "Special item"; after 120 ticks: bubble cleared, victory player sprite) → **13** (`FUN_2005e34`: after 32 ticks — `bossesKilled_38++`, `specialItem_2c = bossRewardItem_0e`, `completionType_3c = 1`, **`saveGame()` auto-save**, battle data reset) → **14** (64-tick wait, sound 38 at 64; on fire: `bossesKilled >= 5` → top-level state 7, else top-level state 8). On player death: **6** (death animation `_200dbcc` + blink flag, toggled by `_02 & 0x2` every 20 ticks) → **8** (skip: 7 loops the death animation) → **8** "Dying" (camera eases to the player while sprites freeze) → `FUN_2004e50` (deallocates battle sprites, re-allocates the map player in the exit pose, **`initAndChangeState(5)`** — the death cutscene, §6.11). `tickBattle` @ `0x2006004` runs the state machine once per frame (per-state `_01` counter + 0x80 flag), then swaps the ticker to a one-shot battle-draw closure (sprite placement, `drawBackground` unless `_02 & 0x8`, `invertColours` when the death-blink flag is set, both HP bars, speech bubbles) — J22.

### 6.8 Bosses and completion (D11)

- **Five boss slots.** `initStateBoss` @ `0x2006264`: `bossIndex` = first `i < 4` with `(cameraY - 32) / 8 <= bossDepths_200e7f8[i]`; if none (below all four), the loop exits with `bossIndex = 4`. `monsterIndex = bossIndex + 16` → **combatant sprites 16–20 are the five bosses**; reward = `bossRewards_200e80c[bossIndex]` (u32 → `bossRewardItem_0e`, an i16).
- Boss battles reuse `tickBattle` with `_02 = 5` (mirrored layout off, background off) and a 1-frame gate before entering state 2.
- **Completion states (top-level 6/7/8).** After the final-boss victory walk (battle state 14) on fire: `bossesKilled >= 5` → **state 7** (`initCompletedAllBosses`: `playSound(20)`; a 32-frame **white scroll** — each frame the render buffer shifts up one row over the bottom white rows — then the **21-row completion panel**, each row's height from the parabolic select math at `FUN_20066e4` @ `0x20066e4` (90-frame cadence per row, ARM `cmnT` ≥ 16 clamp) drawn by `FUN_2006630`); fire → **state 6**. Non-final bosses → **state 8** (`FUN_20065d0`): interstitial menu — panel 7 (player sprite, `FUN_2006318`), after 120 ticks or fire → panel 8 (`FUN_2006474`, the save-&-exit prompt, `FUN_2006374`), where a second fire press → **`shutdown()`** (exit to the host rewards screen; the boss-kill auto-save is already on disk) while 120 ticks or the first fire returns to state 6. **State 6** (`initStateCompletedGame`, also entered by loading a `completionType == 1` save): background panel 0 + victory sprite, `FUN_20063c0` — after 120 ticks or fire → back to state 8. States 6 and 8 thus ping-pong until the player exits the machine.
- **Host-side completion loop** (`DabasScreen.nextDig`): `completionType 1 → 2` starts the next dig from the boss floor (`startNewDig` @ `0x2007484`: camera at `bossDepths[bossesKilled - 1] << 3`, floor pre-filled, item tile 0x19 placed, state 0 / game state 1); when `bossesKilled == 5` the host resets `numberOfTimesSaved` so the machine treats the campaign as finished.

### 6.9 Stats screen and depth (D8)

`drawStats` @ `0x2007250` shows: POW (3-digit), gold (4-digit), **depth = `playerY / 8 - 3`** (4-digit), **next boss depth = `bossDepths_200d470[bossesKilled]`** (4-digit), the animated flag (8 frames over 8-tick steps), and the **real-world clock** (BCD `dow hh mm ss` + colon glyph). This is the machine's original arcade-clock feature, fed by the host RTC (D10).

### 6.10 Pause menu (game state 3)

11 states (@ `0x200e868`): open (scroll-in, sound 37) → map panel → 6 submenu panels (map+HP, save & exit, sound, items, consumables, stats) cycled by right/left (sound 13, scroll 4 px/frame) → close (scroll-out → `initAndChangeState(0)`). Panels are 32-row u32 backgrounds (9 ROM panels); help strings (11 masks) animate in; the minimap overlays the player position (flashing 2 px dot) and caves (`mapTileTypes` 0/1). **Save & exit** (fire release): `saveGame()` + `shutdown()` → host returns to `LOAD_DATA_1`. Sound menu cycles `alarmVolume` 2 → 1 → 0 → 2.

### 6.11 Death and continue (D8)

Player HP 0 in battle → top-level state 5 (`initStateDeath` → `tickDeath` @ `0x2007ec4`): a **12-state sub-table** (`_200f188[12]`, indices 0–11 in `gameStateIndex_0000258`; HP bar + bubbles drawn while < 9). Sequence: **0** player sprite swapped for the death sprite (16×8); **1** bubble 18 ("Help!") + lair sound, camera eases to the player; **2** on animation end, Minitos is allocated at player + 24 (into both the lair slot and a second sprite slot for the carry); **3–4** Minitos walks over and turns (lair sound); **5** the second Minitos approaches; **6** the carry animation begins (`_11 = 70`); **7** carry loop; **8** the trio walks off-screen (1 px per 2 ticks) for 70 ticks, at which point the **resume depth is computed**: `sp = power / 30`; if `sp < 2` → `caveY = (ticks & 7) + 7`, else `caveY = sp * 100 + (ticks & 0xf) - 139` capped at `bossDepths[bossesKilled] - 20`, then `caveY = ((caveY & ~3) + 2) * 8` px and `caveX = playerY & ~7` — the **deeper the player's POW, the deeper the resume** (the power-scaled resume depth); **9** menu panel 6 scrolls in (2 px/frame) with the depth number (same `caveX/8 - 3` formula as the stats screen); **10** 60-tick hold, then the depth number **counts down by 8 px per frame** (sound 8 each step) from the death row to the power-scaled resume depth; **11** on fire (`FUN_2007d80` @ `0x2007d80`): the camera is realigned to the cave origin (`caveX & ~0x1f`), four floor rows are regenerated (y, y+32, y+64, y+96), the resume cave (type 3) is generated at the camera row, tile attribute 25 is placed at `(caveX + 40, caveY + 24)` (the power-scaled caveY), **consumables are halved** (`mushrooms /= 2; bones /= 2; coins /= 2` — the continue penalty), then the resume is staged with a deliberate double transition: `gameStateIndex = 64` (the main game's Minitos-carry revive sequence, §6.6) **and** `changeState(3)` (the map/pause state initialized to its close-out scroll, `menuStateIndex = 10`), so the menu scroll-out doubles as the return-to-game transition. The revive sequence (64/65/66 → parting 61 → 62 → 63) eases the camera offsets home and hands control back to idle input (10 → 11). *Dememulation quirk preserved in-source:* sub-state 1 contains an explicitly commented "variable is uninitialized in retail" branch — the dememulator kept the original's uninit behavior rather than normalizing it.

---

## 7. Script-vs-hardcode boundary (RQ7, D12)

| Layer | Where Dabas lives | Evidence |
|---|---|---|
| Fate scripts | **Nowhere in the reference** — no script references the minigame; no native script function touches it (the 592-slot catalog, M3-06, has no Dabas entry). **In retail the reward hand-off crossed the script layer:** the decompiled `GameVarParam` set-case 40 comment names "Sapphire Pin from Dabas" as a var-40/41 inventory-drop with full-inventory overflow handling (§3.3) — the machine itself still ran the binary | Absence source-verified across `scripting/` + `game/dabas/`; the retail hand-off via the in-source decompiled comment (source-verified wording; the retail script that posted it is not in-repo) |
| Engine-native (retail) | In the original, the Dabas machine is part of the retail executable's embedded content (`OHTA/MCX/DABAS.BIN` on the disc); the host menu entry is retail UI (`LAB_801031cc` decompiled address) | M2-08 §4; `DabasScreen` address comments |
| Reference mechanism | The dememulated machine (Dig-Dabas) + the host interface class (`dabas/Dabas`), renderer/audio/input takeover, crash isolation | §2, §3.1 |
| Registry/data (mod surface) | Input actions (5, `lod` registry, M8-01); `DABAS_SAVE_DATA` config entry (`lod` registrar, byte[] / SAVE location); item/equipment id tables (`LodMod.ITEM_IDS` / `EQUIPMENT_IDS`, retail item space); 20+ i18n keys (`lod.ui.dabas.*`) | M7-02, M8-01/03, §3 |
| Event (mod surface) | **`DabasRewardsEvent`** — the single mod hook of the system: mutable rewards/specialReward/gold posted before display and granting (M8-02 exact-class dispatch; domain-co-located class) | §3.3 |
| Policy (reference choices) | Per-save (not global) storage location of the minigame save; the 320×320 upscale; reward grant paths (`items_2e9`/`giveEquipment`/`goods_19c` with `GoodsSource.GAMEPLAY`); capacity checks (256 equipment / config item cap); gold hand-over cadence and 99 999 999 cap; crash-to-toast | §3 |

Boundary conclusion: **the minigame is a mechanism/content island** — everything inside the machine is binary+ROM-data, and everything around it (entry, save, rewards, input, text) is ordinary registry/config/event/i18n surface. The only script involvement is the retail reward hand-off (var 40/41 drop path with the "Sapphire Pin" full-inventory behavior), which the reference replaced with the direct `DabasRewardsEvent` + grant path; the 192-boundary item encoding is shared between the two, so a port that re-threads rewards through the script layer would still land on the same ids. A Rust reimplementation may either (a) re-implement the dememulated rules directly (this document + the dememulated Java are the full behavioral spec) or (b) retain the binary with a thin machine shim — both preserve the observable contract: 0x260-byte per-save blob, 5 buttons, 32×32 1bpp, 44.1 kHz mono 8-bit, reward ids in the retail item space, one mutable rewards event. Do **not** move machine rules into the host engine "for convenience": the reward ids, caps, and halving rules are machine-internal policy with host-observable effects only through the save blob and the event.

---

## 8. Rust-rewrite considerations (research notes, not design)

1. **The observable contract is small and closed:** frame tick (T0, host-set fps), half-rate logic, 32 rows of 32-bit pixels, 5 level buttons, one 44.1 kHz mono 8-bit stream, one 0x260-byte save. Any faithful port is defined by this interface, not by the Java.
2. **Determinism:** the retail PRNG is not preserved by the reference (D9, mix64 remix of a host RNG); only the *seed sources* (boot = 1; RTC BCD at entry points) and the *distribution tables* (monster weights 15×25, drift tables, 3/16 dust, 14/256 crit, `rand % 10` cave rolls, `ticks & 0x1f` chest time-slicing) are reproducible facts. A Rust port should keep the same seed sources and table-driven distributions; bit-exact parity with the original arcade machine is unattainable from in-repo evidence (asset-validated-pending, and arguably impossible — the retail PRNG is unknown in-repo).
3. **Data ownership:** the save blob is owned by the host (per-save config tail, M7-02) — the machine is a pure function of (blob, tile grid, input, clock). The 16×32 grid *is* the world state; there is no separate world representation.
4. **Timing:** the two-clock structure (game frame = T0/2; DAC pump = T2 reprogrammed per note) must be preserved as a logical contract; absolute Hertz are ROM-data-dependent (`timerSpeeds`, `audioTimerReloadValues`) — asset-validated-pending.
5. **The in-source music-timing TODO** (§2.5) is a known reference-side approximation — if the Rust port keeps the binary, that approximation travels with the dememulation, not the binary.
6. **Save compatibility:** the 0x260 blob (Save60 + tiles, little-endian) is the only persisted artifact; a port that changes its layout breaks per-save Dabas progress silently (no version field).

---

## 9. Register updates and classification

**Register (M9-01 closes three items):**
- **Q-30 closed** (→ this document §2.1): the "Mogul" machine is a **dememulation of the retail `DABAS.BIN`** under the binary's own memory map (virtual = 0x2000000 + ROM offset; code @ 0x4000000+), with ARMv4/Thumb flag semantics as the only CPU model — evidence: the host loads the retail file directly, `DabasVars.loadRom`'s fixed-offset mirror, the `@Method` address annotations, the `Cpu` "ARMv4" flag-only model, and the dememulation commit history. The binary's interface (the execution constraints it imposes) is fully documented (§2); the original arcade unit's physical hardware remains external to in-repo evidence (not a new register item — no in-repo question depends on it).
- **Q-31 closed** (→ §3.3): rewards map `Save60` ids through the retail 192-boundary item encoding (`< 192` equipment, `>= 192` items) into host registry entries, flow through the mutable `DabasRewardsEvent` (the mod hook), and are granted via the three inventory paths with the documented capacity checks; the retail side granted the special item through the script game-var 40/41 drop path (decompiled `GameVarParam` comment, "Sapphire Pin from Dabas").
- **Q-32 closed** (→ §3.2): the `int[][]` tiles are machine-model-owned (`memcardTiles_2000600`, 16×32), filled by the host from the `DABAS_SAVE_DATA` per-save config tail (0x260-byte blob = Save60 + 512 tile bytes); first-time sessions start all-zero because `loadGame` is skipped when the config entry is absent.

**Classification (M9 series, D1–D13; layer: 1 PSX / 3 LoD / 4 reference):**

| # | Item | Layer | Confidence |
|---|---|---|---|
| D1 | The "Mogul" machine is a dememulation of the retail `DABAS.BIN` (mirrored memory map, `@Method` addresses, ARMv4/Thumb flag-only CPU, git-history dememulation sequence); the host executes the retail file | 3/4 | source-verified (binary↔Java equivalence instruction-level: asset-validated-pending) |
| D2 | Frame model: T0 input tick (Hz → host fps), game logic at T0/2, `ticks` = game frames; T2 DAC pump; T1 unused; `setGameSpeed` packed divider/rate with ROM `timerSpeeds` correction | 4 | source-verified (absolute Hz: ROM-data-dependent) |
| D3 | 5-button input contract (fire 0x1, right 0x2, left 0x4, down 0x8, up 0x10), level-mask + per-frame edge detection; host maps 5 engine input actions | 4 | source-verified |
| D4 | 32×32 1bpp display (int[32] rows), set-bit = drawn (black-on-white in the host's 320×320 upscale) | 4 | source-verified |
| D5 | Audio: variable-rate mono 8-bit → 44.1 kHz integer upsample; square wave via alternate sign negation; 47-sequence command set (pitch/0xa0–0xf3); 132-entry u16 pitch table via timer reload; (7-speed)×12 semitone clock compensation; the in-source music-tempo TODO (reference approximation) | 4 | source-verified |
| D6 | Save contract: Save60 (24 LE u32) + 16×32 u8 tiles = 0x260 blob; `DABAS_SAVE_DATA` config (default 0x260 zeros, SAVE location, per-save); `numberOfTimesSaved == 0` = no game; auto-save after boss kills; save & exit = save + shutdown | 4 | source-verified (blob bytes: asset-validated-pending) |
| D7 | Reward contract: retail 192-boundary item encoding (shared with script var 41), 6 item slots + special + gold, mutable `DabasRewardsEvent`, three grant paths, capacity checks (256 equipment / config item cap), 10-gold hand-over, 99 999 999 host gold cap | 4 | source-verified |
| D8 | Gameplay rules as documented in §6 (movement 1 px/frame, dig 6-frame counter, 3/16 dust, 15/16 phase-gated empty dig, 50/50 mushroom/bone by `rand & 0x8`, +5 item dig, heal `maxHp/10`, stat caps 99/999/9999, cave rolls `rand % 10` with 4-visit Minitos guarantee, chest `ticks & 0x1f` time-slicing, player attack `power/5`, monster attack `attack/3`, crit 14/256 per 32-tick idle window, ×2 crit, guard negates damage with 5 px pushback, active-frame gating, 2/256 + 20 px weighted monster attacks, depth-weighted monster table, death halving, power-scaled resume depth `sp*100 + (ticks & 0xf) - 139` capped at the next boss floor) | 3 | source-verified from the dememulated code (retail-binary identity: asset-validated-pending) |
| D9 | RNG: seed sources boot-1 / RTC-BCD at entry points; `rand()` = upper 16 bits of the Stafford-mix64-remixed host RNG — a reference reimplementation of the original PRNG, so outcomes are distributionally modeled, not bit-identical to retail | 4 | source-verified (dememulation choice, in-source citation) |
| D10 | RTC: BCD Sunday=1 pack of the host wall clock; used as RNG seed and drawn on the stats screen (arcade-clock feature) | 3/4 | source-verified |
| D11 | Boss model: 5 slots (depth-table indices 0–3 → monsters 16–19; below-all fallback index 4 → monster 20), `bossRewards[5]`, auto-save per kill, `completionType 1 → 2` next-dig transition, `bossesKilled >= 5` → all-complete state, host reset of `numberOfTimesSaved` at 5 | 3 | source-verified (depth/reward values: ROM data) |
| D12 | Script boundary: zero Fate/native-script involvement in the reference; in retail the reward hand-off crossed the script layer via the game-var 40/41 drop path ("Sapphire Pin from Dabas" full-inventory behavior, per the in-source decompiled `GameVarParam` comment); mod surface = 5 input actions + 1 config entry + id tables + i18n + `DabasRewardsEvent` only | 3/4 | source-verified (retail script leg: the posting script is not in-repo — the comment is the evidence) |
| D13 | Host lifecycle: 11-stage `DabasScreen` machine, renderer-state save/restore, PLAY_8 takeover, crash-to-toast (5 s), save-&-exit inside the machine, per-campaign save binding | 4 | source-verified |

**Java-mechanism table (J20–J24 — observable contract vs incidental implementation):**

| # | Mechanism | Observable consequence | Incidental (not a reference requirement) |
|---|---|---|---|
| J20 | Function-reference tables (`Runnable[]`/`Consumer[]`/`IntSupplier[]` arrays) mirroring the ROM jump tables; `@Method(0x...)` address annotations | State-machine transitions are data tables indexed by state/action ids; the address map documents the dememulation | Java functional interfaces, annotation retention |
| J21 | Dememulated PRNG: `java.util.Random` subclass overriding `nextInt()` with Stafford mix64 | Seedable, 16-bit outputs, seed = RTC BCD; **not** the retail PRNG sequence (D9) | The specific mix function and Java base generator |
| J22 | One-shot ticker swap (`tickBattle`/`tickCompletedAllBosses` replace `currentState` with a draw closure for one frame, then restore) | A battle/completion frame renders exactly once per state-machine step | The Runnable-swap idiom |
| J23 | Dedicated `Thread("Timers")` running the 3-timer tick loop against `System.nanoTime`, with 3×-behind catch-up | Input sampling and the DAC pump run independently of the render loop; the host render loop is capped to T0 Hz via `setFps` | The thread, `DebugHelper.sleep(0)` yield |
| J24 | `DabasInterface` callback contract (`setTicker`, `setFps`, `bufferAudio`, `loadGame`, `saveGame`, `shutdown`) | The host/machine boundary — the six host obligations enumerated in §3 | The Java interface; the standalone `Main`/`TestInterface` presentation (GLFW/OpenGL + Java Sound) |

---

## 10. Cross-references

- M2-08 §4 — DABAS.BIN fixed-offset blob format (this document's §2.4 extends it with types/sizes and use).
- M2-01 / R0 inventory — `OHTA/MCX/DABAS.BIN` disc path; Dig-Dabas as a pinned reference repository.
- M7-02 §3 — `ConfigStorageLocation.SAVE` (per-save config tail) — the storage substrate for `DABAS_SAVE_DATA`.
- M8-01 — input-action registry (the 5 mapped actions); M8-02 — `DabasRewardsEvent` dispatch semantics (exact-class, mutable payload); M8-03 — i18n key model, `lod` registrar census (the id tables and config entry).
- M3-06 — the 592-slot native script catalog contains no Dabas functions (D12); `GameVarParam` set-cases 40/41 (the shared 192-boundary item encoding and the retail Dabas special-item hand-off, §3.3/§7).
- Upcoming M9-07 (inventory) will re-document the reward-grant paths (`items_2e9`, `giveEquipment`, `goods_19c`) from the inventory side.
