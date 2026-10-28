# Dig-Dabas Repository Research

**Repository:** `~/Documents/GitHub/Dig-Dabas`
**Commit:** `4c1fbfebced123d43282b6e3b08286336b6b05f0` (branch `main`,
2026-05-20, "Implement next dig")
**Artifact:** `org.legendofdragoon:mogul-dabas:0.0.1` (matches the version
consumed by Severed-Chains) — verified (build.gradle group `org.
legendofdragoon`, version `0.0.1`).
**License:** AGPL-3.0 (verified).
**Read-only reference — do not modify.**

## 1. What it is

**Mogul Dabas is a minigame** — a "digging" adventure (mushrooms, bones,
nuggets, coins, hearts, caves, chests, monsters, bosses, HP, speech
bubbles like "Food!"/"Item!"/"Help!") that runs on a **fictional handheld
virtual console** ("Dabas"). The original game ships the minigame's **ROM**
on the disc and its own in-game UI; Severed Chains (a) ports the handheld
console emulation (this repo) and (b) integrates it into the in-game menu
with a rewards flow ("Take"/"Discard"/"Next Dig").

Verified provenance signals (this is **original game content**, surfaced by
Severed Chains, not an SC-invented game):
- ROM loaded from the **player's disc**: `Loader.resolve("OHTA/MCX/
  DABAS.BIN")` (reference repo `legend/game/dabas/Dabas.java:76`) — a file
  in the retail disc tree, not a SC-bundled asset.
- Retail memory addresses in the host code: `dabasData_8011d7c0`,
  `SItem.dabasMenuGlyphs_80114228` (a retail glyph TMD for the menu).
- i18n: "Delete items from the **Pocket Station**" — the in-fiction
  handheld is the game's Pocket-Station-style companion.
- The in-game menu entry (`lod_core.ui.ingame_menu.mogul_dabas`) lives in
  the SC-reconstructed in-game menu (`MainMenuScreen`), so *access* to it
  is an SC feature; the minigame itself and its ROM are retail.

**Not in the reference repo:** nothing — the console + game logic live in
*this* repo (`mogul-dabas`), integrated by `legend/game/dabas/Dabas.java`
(a ~300-line host adapter) + `DabasManager` + `DabasScreen` +
`DabasRewardsEvent`.

**Scale:** 51 Java files, ~7k LOC. Standalone dev entry
(`org.legendofdragoon.dabas.game.Main`, own GLFW window, loads
`game.rom`) + the library entry (`Hardware.start(Path)`) used by the game.
Own deps: LWJGL 3.4.1 (only for the standalone window), mod-loader 4.2.5
(declared; **no imports found in main sources — vestigial**), log4j,
lmax-disruptor 3.4.4 (declared; check usage — **open Q-DAB-1**).

## 2. The virtual console (verified)

`org.legendofdragoon.dabas.core.Hardware` — the machine:

| Unit | Address (virtual) | Behavior |
|---|---|---|
| `InterruptController` | 0xa00_0000 | 5 buttons: UP/DOWN/LEFT/RIGHT/ACTION; edge tracking (pressed/released this frame); feeds `inputNow`/`inputHeld` |
| `Timers` (×3) | 0xa80_0000, 0xb80_0000 | 3 free-running 16-bit reload timers on a **dedicated real-time thread** (`new Thread(TIMERS, "Timers")`), speed scaled by power/clock; **RTC** = `LocalDateTime.now()` packed as BCD day-of-week/hour/minute/second (day 1=Sunday) — **wall-clock dependent** |
| `PowerControl` | 0xb00_0000, 0xd80_0000 | `SetCpuSpeed(mode 0..7)` — the ROM can downclock the CPU (timer scaling follows) |
| `Spu` | 0xd80_0010 | **not** a full SPU: a DAC — ROM writes single samples (u8) at a configurable input rate; the console upsamples to 44.1 kHz mono 8-bit and flushes 100 ms blocks (`SAMPLES_PER_TICK`) to the host via `DabasInterface.bufferAudio(byte[])` |
| `Cpu` | — | **ARMv4** core (`/** ARMv4 */`): CPSR with N/Z/C/V flags, `lslT/lsrT` (shifts, T=Thumb-style immediate/operand variants, A=full), `cmpT/cmpA/cmnT/cmnA` — flag-setting arithmetic helpers the game logic (below) uses |
| RAM | 0x0000200–0x0000xxx | 4 KB-ish working RAM (the `DabasVars` symbol table maps every byte — the "memory layout" of the handheld, decompiler-label style) |
| ROM | 0x4000000+ / 0x2000000+ | `DABAS.BIN` — code at 0x4000000+, data at 0x2000000+ (e.g. `audioTimerReloadValues_200840c`, tile/sprite/glyph data, `memcardSave_2000580`, `memcardTiles_2000600`) |
| Display | — | **32×32, 1 bit/pixel** framebuffer (`int[32]` rows, bit 0 = leftmost, inverted sense: `^ 0x1` in the host adapter) |

Execution model: `game/Dabas.java` (6,029 lines) is the **hand-ported
game+console loop** — methods annotated `@Method(0x2000810)` etc. carry the
**original game code's labels** (Ghidra-style `LAB_4001ab4` comments in
bodies). `main()` → `initHardware()` (timer/SPU/power init) →
`DoExecute()` → `SetCpuSpeed(7)` → `entry()`: seeds RNG (`seed_00 = 1`,
copy of 0x138 bytes from ROM 0x20115c8), zeroes render buffer/tiles/
sprites, `startGame()`. The timer-0 tick drives `handleInput()` (edge
detection into `inputNow/inputHeld/pressedThisFrame/releasedThisFrame`).
So the port is **not** a generic ARM emulator: it's the *specific*
handheld's firmware behavior re-implemented as a deterministic
co-simulation (the ARM "CPU" is realized through the ported instruction
flow, not a fetch-decode loop over the ROM — the ROM is a *data* file of
the minigame's constants/tables/graphics, and the `@Method` functions are
the decompiled-and-reimplemented game logic that *consumed* that memory
layout).

> **Design note (verified vs proposed):** the Java implementation is a
> decompilation re-implementation, not a binary-level VM. The native port
> follows the same strategy (ported logic + data-driven tables), not an
> ARM emulator. This is a deliberate scope decision inherited from the
> reference — keep it; an ARMv4 emulator would be a PSX-project
> *non*-goal here (this console is not PS1 hardware — see psx-library.md
> boundary).

- **RNG:** `dabas.core.Random extends java.util.Random` (LCG) with
  `nextInt()` overridden by **mix32 (Stafford variant 4 mix64 high bits)**
  — the minigame's *extra* mixing on top of the LCG. Port exactly (golden
  sequence test) — note it is **distinct** from the main game's RNG
  (documented in source-architecture.md).
- **`DabasVars`** (559 lines): the complete labeled memory map (camera,
  cave X/Y, ticks, state machine indices, HP values, monster data,
  speech-bubble table with 21 indices, menu state, sprites, sound
  struct, timer tick counters, `memcardSave_2000580` (Save60),
  `memcardTiles_2000600` (32×32 tile grid)). These are the decompiler's
  symbol names — the native port keeps the same layout (it's the contract
  with the ROM data and the save format).
- **`FileData`** (328 lines): little-endian byte buffer reader/writer
  (readUByte/UShort/Short/Int/write... with `IntRef` offset cursor) —
  same pattern as the reference's `FileData` (duplicated class; the native
  engine already has an equivalent — reuse the engine's, don't duplicate).

## 3. Game types (verified)

`game/types/*` — the minigame's save/runtime structs (flat, field
offsets in names):
- **`Save60`** (0x60 = 96 bytes): `chapter_00` (ubyte, = main game
  chapter at "Next Dig"), `items_14[10]` (u16×10 item ids, 0=empty),
  `specialItem_2c` (u16 equipment id), `gold_34` (u32). — the "memcard
  save" the handheld reads/writes.
- `BattleData48`, `MenuData08`, `MonsterSounds08`, `SoundStruct4c`,
  `SpeechBubble08`, `Sprite1c`, `SpriteData24`, `Struct*` — battle/menu/
  sprite/sound state (all flat PODs; trivial to port).

## 4. Host integration (reference repo, verified)

`legend/game/dabas/Dabas.java` implements **`DabasInterface`** (the
console → host contract; 6 methods):
```
setTicker(Runnable)      // host supplies the per-frame tick (VM step)
setFps(int)              // host FPS limit change (minigame requests rates)
bufferAudio(byte[])      // 100 ms mono8 44.1 kHz blocks → audio thread
loadGame(Save60, int[][]) / saveGame(Save60, int[][])  // save slots
shutdown()               // stop console, restore host state
```
Host-side responsibilities (all in `Dabas` adapter, verified):
- **Renderer hijack for the minigame's duration:** saves
  render callback / FPS limit / projection size / render mode / clear
  color; forces `RenderMode.PERSPECTIVE`, 320×320 projection, black clear;
  per tick: copies the 32×32 1bpp `pixels[]` into a 32×32 RGBA texture
  (nearest-neighbor quad via `QuadBuilder`, 320-unit ortho scale) and
  renders it. Restore on `shutdown()`.
- **Input:** maps 5 game actions (`GENERAL_MOVE_UP/DOWN/LEFT/RIGHT`,
  `SMAP_INTERACT`) → `InputType.BUTTON_*` pressed/released on the
  `InterruptController`.
- **Audio:** `GenericSource(AL_FORMAT_MONO8, 44100)` on the music/SFX
  audio thread; blocks buffered when `canBuffer()`.
- **Save storage:** the minigame save lives in the **global config**
  under the `DABAS_SAVE_DATA` entry (a 0x260-byte blob: `Save60` + tile
  grid) — `DabasManager` (load/save) + the adapter's `loadGame/saveGame`
  (tiles from/to the same blob). `loadGame` is a **no-op unless the
  config entry exists** (first run = fresh).
- **Crash isolation:** the adapter wraps the VM tick in try/catch
  (`"Mogul Dabas crashed :("`) → notification + clean `shutdown()` —
  **the minigame's death does not kill the host** (a behavior the native
  port must preserve: an error result from the tick, not an exception).
- **Rewards flow:** `DabasScreen` (inventory menu) reads `Save60` →
  builds a `DabasRewardsEvent` (gold, up to 10 items, 1 special equipment
  item resolved via the equipment registry) which the game applies to the
  player's inventory; "Take" clears the handheld save (items→player),
  "Discard" wipes it, **"Next Dig"** stamps `chapter_00` = current chapter
  and starts a new expedition (the latest Dig-Dabas feature, 2026-05-20).
- `DabasScreen` also renders the retail menu glyphs
  (`dabasMenuGlyphs_80114228`) — the minigame's UI in the main game.

## 5. Integration recommendation (native)

**Decision: a separate C++23 library target (`loddabas`) inside the native
repo, linked into the game module. The build form is the user's choice
(CMake's built-in `BUILD_SHARED_LIBS`; the architecture prescribes nothing,
and nothing functionally *requires* any particular form). Not a plugin; not
part of `lodengine`; not part of the prefix-free `psx` library.**

Rationale:
1. **Parity with the reference architecture:** the Java project keeps it a
   separate artifact (`mogul-dabas`) integrated by a thin host adapter.
   The native equivalent is a separate *target* in one repo (we have no
   separate-repo requirement; one repo, many targets — psx (root subproject)
   / engine / game / dabas / tools).
2. **First-party retail content, not a mod** → it does not belong in the
   plugin system (plugins are third-party extension; also the plugin ABI
   is for external code with a stability contract — the minigame can
   evolve with the game).
3. **Not PSX hardware** → the fictional ARMv4 handheld is *not* a
   PlayStation system; the psx library (psx-library.md) must not absorb
   it. (The PS1 itself runs this game *as data*; the VM is a game
   mechanic, like a character in a minigame box.)
4. **Same-process link:** the host adapter needs per-frame pixel/audio/
   input callbacks with zero IPC; a same-process library is the simplest
   correct boundary. The original task said "do not assume it requires a
   DLL" — respected: nothing functionally requires any particular build
   form; that choice is the user's (CMake `BUILD_SHARED_LIBS`).
5. **Testability:** the console has no SDL/Qt dependency (pixels are an
   `int[32]`, audio is a byte callback, time is injectable) — the native
   `loddabas` keeps that: pure C++23, driven by a host interface; a headless
   test harness (like the Java standalone `Main`/`TestInterface`) runs the
   ROM logic deterministically (see testing additions).

Proposed shape:
```
src/libs/dabas/
  include/dabas/
    hardware.hpp     // Console (timers, RTC, interrupts, power, DAC, framebuffer)
    host.hpp         // DabasHost interface (tick, fps, audio, save, shutdown)
    save.hpp         // Save60 + tile grid (byte-exact layout)
    rng.hpp          // LCG+mix32
    types.hpp        // BattleData48, Sprite1c, ... PODs
  src/
    console.cpp      // timer thread equivalent → **restructured**: fixed-step
                     //   ticks driven by the host tick (no wall-clock thread
                     //   in v1 — see Q-DAB-2; Java runs a real-time timer
                     //   thread, the native port should make it
                     //   deterministically host-driven where possible)
    game/            // the @Method-annotated logic port (states, caves,
                     //   battles, sprites, menus, RNG draws)
    vars.hpp         // the DabasVars memory map (kept as named struct fields)
```
Host adapter in `game/` (LoD module): the `Dabas` adapter (render
hijack, input mapping, audio source, config-blob save, crash→error
result), `DabasScreen` (menu), rewards event. Save blob stays in the
global config under the same `dabas_save_data` key, byte-identical layout
(0x260: Save60 + tiles) so Java-era saves/overrides interoperate.

## 6. Open questions

- **Q-DAB-1:** is `lmax-disruptor` actually used in Dig-Dabas sources (no
  import found in a first pass — verify; if unused, another vestigial
  dep).
- **Q-DAB-2:** timer threading. Java: real-time `Timers` thread with
  `DebugHelper.sleep(0)`; wall-clock RTC. For the native port: (a) keep a
  background timer thread (fidelity, non-deterministic), or (b) make timer
  ticks a fixed step of the host tick (deterministic, testable) with the
  "RTC" injected from the host clock. **Recommendation: (b)** — the
  minigame's timing is gameplay-critical but the reference's real-time
  thread is an implementation detail; a host-driven fixed step with a
  measured tick rate (parity window) is both more testable and equally
  faithful in feel. Needs owner sign-off because it changes the port's
  determinism character.
- **Q-DAB-3:** is `DABAS.BIN` identical across US/EU/JP discs? (The disc
  provides it; regional variants unknown — same class of gap as the region
  item in progress.md.)
- **Q-DAB-4:** the 0x260 save blob: confirm exact tile-grid byte count
  (32×32 grid, u8 per tile, row-major → 0x400? but the blob is 0x260 =
  608 = 0x60 (Save60) + 0x200 (512 = 32×16?). `memcardTiles_2000600` and
  the `int[][] tiles` in `saveGame` write `row: tile` per cell —
  `data.writeByte` per tile, `new FileData(new byte[0x260])` ⇒ 512 tiles ⇒
  the grid is 32×16, not 32×32 (the *display* is 32×32 but the *saved*
  tile map is 512 cells). Verify against `loadGame`'s `tiles` dimensions
  when the port starts (the Java `tiles` array shape comes from the VM's
  `memcardTiles` — 0x200 bytes). **This is a byte-exactness item for the
  save port.**
