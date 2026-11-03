# Execution Model and Update Ordering

Status: Complete (R1). Repository: Severed-Chains @ `0c08c679b`.

This document describes *how a frame happens*: the platform loop, the frame-driver ordering inside it, the game loop's internal phases, input processing order, timing model, audio update timing, and which execution-order properties are original (decompiled from the ROM) versus reference-implementation architecture.

## 1. The platform loop (frame driver)

`RenderEngine.run()` (RenderEngine.java:1229) does `window.show()` and then `PLATFORM.run()`; the loop call is wrapped in try/catch — if the loop throws, the window is closed and the exception propagates to the top-level crash handler (audit F-14). The actual loop is `PlatformManager.run()` (PlatformManager.java:74-99):

```
running = true
while (running):
    for each window: if shouldClose() → destroy, remove
    nextAction = null
    for each action (registration order):
        action.tick()                      // runs the action if its timer is due
        track the action with the earliest next-run time
    sleep(max(0, nextAction.nanosUntilNextRun() / 1e6))   // ms-granularity (DebugHelper.sleep = Thread.sleep; audit F-05)
```

**Actions** (Action.java) are fixed-period timers: `nanosPerTick = 1e9 / expectedFps`; `tick()` runs the runnable when `nextRunTime <= now`; if the action is more than two periods late it *skips ahead* (`nextRunTime = now + nanosPerTick`, logged as "running behind, skipping ticks to catch up") rather than running a burst. `Verified`.

Two actions matter:

1. **Input action** — registered as a `PlatformManager` field initializer (`private final Action input = this.addAction(new Action(this::tickInput, 60))`), so it enters the action list at construction, **before** any window render action (created later in `SdlWindow`'s constructor), and ticks **before** the render action on every loop iteration (list order). Its rate is updated every frame by the game (see §5).
2. **Render action per window** — created in `SdlWindow`'s constructor (`new Action(this::tick, 60)`); `SdlWindow.setFpsLimit(hz)` reschedules it.

`SdlWindow.tick()` (SdlWindow.java:451-455) is the frame presentation + game frame:

```
SDL_GL_SwapWindow(window)      // present the backbuffer filled during the PREVIOUS frame
events().onDraw()              // run the onDraw callback chain (the renderer's frame handler)
manager.clearPressed()         // clear one-frame input state (see input doc)
```

SDL swap interval is 0 (`SdlWindow` ctor: `SDL_GL_SetSwapInterval(0)`) — **there is no vsync; frame pacing is entirely the software Action timer**. `Verified`.

Consequence: the presented frame at time *t* was rendered during the onDraw of the previous iteration; game logic for frame *t+1* runs after the present. Presentation adds one frame of latency relative to a swap-after-render design. This ordering is a reference-implementation property, not original ROM behavior (the ROM swapped from inside its vblank handler).

## 2. The onDraw frame handler (RenderEngine.init, RenderEngine.java:586-795)

Executed on the main thread, inside `SdlWindow.tick()`, once per due render action. Phase order (as written):

1. **Flush frame-start tasks** — `RenderEngine.addTask(runnable)` (a synchronized deque) is the documented "run at the start of the next frame" queue (RenderEngine.java:464); all queued tasks run here.
2. **Deferred resize** — if `resizeRenderBuffers` was set by `onResize` (window resize or resolution-config change), the double render buffers (color + shared depth texture) are rebuilt now.
3. **`pre()`** (only when `frameSkipIndex == 0`) — set viewport (full, or centered 4:3 when `legacyMode` rendering + forced-4:3 widescreen config), set projection mode 3D, clear color+depth.
4. **`EVENTS.clearStaleRefs()`** — purge dead mod event listener bindings (Mod-Loader).
5. **Pause toggle** (debugger input): on resume — reset batches, delete marked-dead `Obj`s/`Texture`s, reset scissor; on pause — `renderFrame()` once (the frozen frame re-renders the current callback output).
6. **Frame advance** (debugger input): delete marked objects/textures, reset scissor, advance render-buffer index, reset batches, `renderFrame()`.
7. **Normal frame** (if not paused):
   1. Set `ignoreQueues` on every model/ortho pool of every batch: **false** when `frameSkipIndex == 0`, **true** otherwise (models queued during a skipped turbo frame are discarded).
   2. **`renderFrame()`** (RenderEngine.java:850): reset the CLUT-animation uniform index → **`renderCallback.run()`** (this is `gameLoop` once the game has booted — the entire game tick happens *inside* this call) → upload the CLUT-animation uniform.
   3. `GameOverlay.drawNotifications()` / `drawFps()` (reference-implementation HUD).
8. **Main render pass** (only when `legacyMode == 0`, the default in releases; Tab-cycled debug switch: 0 = OpenGL rendering, 1 = legacy rendering, 2 = VRAM rendering — see §7):
   - bind `renderBuffers[renderBufferIndex]`; when `frameSkipIndex == 0`: clear; then for each extra batch and the main batch, `renderBatch`:
     - if `needsSorting`: sort the perspective pool (by model-view depth `m32 + screenspaceOffset.z`, descending, then insertion sequence) and the ortho pool (same depth key, then sequence);
     - `api.initBatch(batch)`; clear depth;
     - **3D opaque pool** (backface culling on) → **2D opaque pool** → **3D translucent pool** → **2D translucent pool**;
     - per model: transforms are uploaded in 128-entry uniform-block chunks (`transforms2Buffer`/`lightBuffer`); per-model depth comparator, scissor, then per-layer render plus the PSX-style translucency passes (first pass renders the opaque pixels of translucent primitives; second pass renders `HALF_B_PLUS_HALF_F`, `B_PLUS_F`, `B_MINUS_F`, `B_PLUS_QUARTER_F` with backface culling off). Note: the fourth pass sets the GL blend state to `B_PLUS_F` before rendering `B_PLUS_QUARTER_F` (reference alias or bug — audit F-13, register Q-37). Also: before the main pass, if the current state is Battle, the `battle_tmd` shader is bound with a battle-colour uniform (source comment: "Gross hack bro" — audit F-18).
   - **Post pass**: depth test off, unbind FBO, clear the window, bind the screen (post) shader, configure CRT/scanlines/grille/pixelate/roll/noise/static/aberration/brightness/discolour/warp/vignette/bloom from config, the turn-order UI bounds (from the TurnOrderMod, when present), then draw the post quad (full-window) sampling `renderTextures[renderBufferIndex]`, unbind.
9. **Post-frame bookkeeping** (if not paused): when `frameSkipIndex == 0` — reset batches, delete marked objects/textures, reset scissor; then advance `frameSkipIndex` (mod speed multiplier) and advance `renderBufferIndex` once every `speedMultiplier` frames when the frame-skip option is on, else every frame; update `vsyncCount += 60.0 * speedMultiplier / fpsLimit` (virtual 60 Hz frame counter, §5); rolling FPS over a window of `fpsLimit` frames (window title in dev builds).
10. **Shader reload** (debugger) and **`handleMovement()`** (freecam camera; writes into `worldToScreenMatrix` — a game-visible global).

The frame-skip/turbo interaction (steps 7.1, 9) is `Verified` as mechanism; its *intent* (logic runs every frame, rendering survives one frame per multiplier, queued models of skipped frames are dropped) is `Inferred` — see register Q-36.

## 3. The game loop (`gameLoop`, Scus94491BpeSegment.java:137-194, `@Method(0x80011e1cL)`)

The decompiled per-frame body, invoked as the render callback. Phase order (source order), with what each phase is allowed to observe:

| # | Phase (line) | Behavior |
|---|---|---|
| 0 | `endFrame()` (143) | PSX display end-of-frame: set the "draw pixels" mask-bit commands (first + last ordering-table entry), run `syncFrame_8004dd3c` (no-op by default; swapped to `syncFrame_reinit` for one frame when the graphics mode changes), run `swapDisplayBuffer_8004dd40` (PSX double-buffer display swap). **The display swap happens at frame start** in the decompiled ordering — the frame renders into the *other* display buffer. |
| 1 | `GPU.startFrame()` (145) | Upload dirty VRAM to the GPU texture; resize the ordering table if configured size changed. |
| 2 | timestamp (147-149) | If `currentEngineState != null && state.advancesTime()`: `gameState.timestamp_a0 += vsyncMode` (timestamp unit = 1/60 s of game time). |
| 3 | pacing (151-153) | `frames = max(1, vsyncMode)`; `hz = 60 / frames * speedMultiplier`; `RENDERER.window().setFpsLimit(hz)`; `PLATFORM.setInputTickRate(hz)`. |
| 4 | `loadQueuedOverlay()` (155) | Perform a queued engine-state transition (EngineStates.java:47): menu stack reset, previous-state bookkeeping, `vsyncMode = 2`, destroy/construct/init/readSave, per-state script-native table, render mode + projections, `EngineStateChangeEvent`, rich presence. |
| 5 | `renderUi()` (157) | UI renderables (SItem/Menus: cursor, HUD pieces). |
| 6 | `currentEngineState.tick()` (159-161) | Base: compute analogue magnitude/angle from the four registered movement-axis actions (`PLATFORM.getAxis`). Subclasses tick gameplay **before scripts run**. |
| 7 | `EVENTS.postEvent(RENDER_EVENT)` (163) | Per-frame mod hook (RenderEvent). |
| 8 | `loadAndRenderMenus()` (165) | Menu screens: async file loads, draw. |
| 9 | `uploadRenderables()` (167) | Upload pending menu/UI renderable textures (marked "TODO temporary until everything is moved over to controls"). |
| 10 | `SCRIPTS.tick()` (169) | Script VM frame: every `framesPerTick` frames (1 in normal play) → `executeScriptFrame` + tickers + renderers; returns whether scripts ticked. |
| 11 | `currentEngineState.postScriptTick(ticked)` (171-173) | State hook **after** scripts (e.g., reacting to script-side state changes). |
| 12 | `tickAndRenderTransitionIntoBattle()` (175) | SBtld: battle transition timing (sets `vsyncMode = 1` during the transition). |
| 13 | `handleFullScreenEffects()` (176) | Fade/black effects (decompiled `FullScreenEffects`). |
| 14 | `startQueuedSounds()` (179) | SPU: key-on queued SFX voices. |
| 15 | `handleTextboxAndText()` + `renderTextboxes()` (182-183) | Textbox state machine + text rendering (scroll speeds normalized by `tickMultiplier`). |
| 16 | `currentEngineState.overlayTick()` (185-187) | "Runs after everything else is rendered" (per interface javadoc). |
| 17 | `FUN_80020ed8()` (189) | Submap transition tracking (EngineStates.java:99): resets `submapFullyLoaded`; on state change, sets the submap-transition-mode global `_800bd7b0` (2 = normal entry, 9 = from title, -4 = from battle + `transitioningFromCombatToSubmap`, 3 = from world map) and the title-origin flag `_800bd7ac`. `_800bd7b0` is script-visible as game variable 67 (`GameVarParam`). |
| 18 | `tickCount_800bb0fc++` (190) | Global frame counter (original ROM global). |
| 19 | `GPU.endFrame()` (192) | GPU model tick: render the ordering-table z-queues (highest z down), present the PSX display (legacy/VRAM modes), flip the display buffer. |
| 20 | `DISCORD.tick()` (194) | Rich presence frame update. |

Phase order is `Verified` (decompiled source order) — all 21 calls (phases 0-20) are statements inside the decompiled `gameLoop` body. The *services* of phases 0-19 are ROM code; phase 20 (`DISCORD.tick`) is a call to a *reference* service placed inside the preserved ROM frame body (the original ROM had no rich presence); the wrapping (render-callback invocation, task flush, post pass, bookkeeping) is reference architecture (audit F-12).

### Input ordering note (cross-cutting)

Game-state input dispatch (`inputActionPressed`/`inputActionReleased`) happens **outside** `gameLoop` — during the platform's input tick, which precedes the render tick in the same loop iteration (see §4 and the input doc). In the original ROM, the game polled the pad inside its frame body; the reference moved polling into SDL. The *game-side consumption* of input is therefore: state sees action-pressed/released events between frames, and one-frame `pressed` state during the frame, cleared after onDraw. `Verified` for the reference; the original's in-frame polling is `Inferred` (decompiled code no longer contains the pad polling loop — it was replaced by the action-abstraction hooks `EngineState.getInputsPressed/Repeat/Held` and the `scriptInputActionPressed/Held` natives).

## 4. Input processing order

Inside the input action (`tickInput`, SdlPlatformManager.java:269-684), under `INPUT_LOCK`:

1. **Event pump** — `while (SDL_PollEvent)`: window (close/focus/resize), key down/up (raw `onKeyPress`/`onKeyRelease` + keycode bindings → action press (only on first press, mod-mask matched) / action release + scancode bindings → action press/release + `InputPressedEvent`/`InputReleasedEvent`), text input (per-char `onChar`, suppressed while gamepad is held), mouse motion/press/release/scroll (raw window events; sets input class KEYBOARD), gamepad add/remove/remap, gamepad axis (deadzone conversion — separate menu/movement inner/outer deadzones, per-action deadzone mode; multi-gamepad axis merging by max |value|; axis-action press/release), gamepad button (raw + bindings → action press/release + events).
2. **Repeats** (after the pump, still in the same tick) — for each action state in repeat state (focused window only): `onInputActionPressed(action, repeat=true)` + `InputPressedEvent(repeat)`.
3. **Focus loss** (unfocused window) — release every held action, clear pressed/ignored/axes/buttons.
4. **Rumble lerp** update (time-based intensity interpolation).
5. **`clearActionStates`** flag → wipe all action state (set by `InputBindings.initBindings` during mod re-boot).

Then, after the render action of the same iteration has run (present + onDraw + gameLoop), `clearPressed()` clears the one-frame pressed set. So within one iteration the order is: **input events → (action callbacks incl. game state) → present previous frame → game tick (sees one-frame input) → clear one-frame input**. `Verified`.

## 5. Timing model

**Base rate.** `vsyncMode_8007a3b8` (Graphics.java:71, field default 1) selects the frame rate: `hz = 60 / max(1, vsyncMode) * speedMultiplier` (vsyncMode 1 → 60 Hz, 2 → 30 Hz, 3 → 20 Hz). **The rate is per-context, set by decompiled ROM code at many sites** — full census of all 19 non-debug write sites (`Verified`):

| Rate | Context | Write site(s) |
|---|---|---|
| 60 Hz (1) | **Submap exploration** — first tick of the submap's INIT stage | SMap.java:4088 (with `SCRIPTS.setFramesPerTick(2)`, `resizeDisplay(384, 240)`) |
| 60 Hz (1) | **World map exploration** — at map init; restored after the map screens close | WMap.java:707 (`initWmap`), WMap.java:681 (`restoreMapOnExitMenu_`) |
| 60 Hz (1) | **Credits** — at credits init | Credits.java:239 |
| 60 Hz (1) | **Battle transition** (SBtld) | SBtld.java:359 |
| 20 Hz (3) | **Battle** — at battle start / first frame | Battle.java:2040, 2361 |
| 30 Hz (2) | **Title, game over** | Ttle.java:285, 609, 652; GameOver.java:145 |
| 30 Hz (2) | **World map screens / wmap exit** (screens open, transition to another state, quit to title) | WMap.java:1021 (`deallocate`), 797, 830 |
| 30 Hz (2) | **Every engine-state transition** (forced at the frame-top transition; the entering state's first tick may immediately reassign) | EngineStates.java:56 |
| 30 Hz (2) | ROM init | Scus94491BpeSegment.java:624 (`gameInit`), 640 (`preload`) |
| any | **Script-writable** (game variable 25) | GameVarParam.java:245 |
| any | Debug tool (debugger UI) | DebuggerController.java:293 |

The actual structure: **exploration contexts (submap, world map) and credits run at 60 Hz; battle at 20 Hz; title, game over, and world-map menu screens at 30 Hz**, with the transition-forced 30 Hz as the in-between value. The R1 claim that "30 Hz is the common case (submap/title/world map)" was built from the transition/default sites alone and is wrong (audit F-01). Pacing note: `setFpsLimit` runs at phase 3, *before* the transition (phase 4) and the state's tick (phase 6), so any rate change takes effect from the next frame; the transition's forced 30 Hz value is overwritten in the same frame by the entering state's first tick for SMap/WMap/Credits/SBtld (→ 60 Hz) and battle (→ 20 Hz), and persists for title/game over (→ 30 Hz). Consistency cross-check: `tickMultiplier()` records the original per-state rates — default 2 (`EngineState.java:89`, javadoc: "world map was 20FPS, we multiplied it by 3 to bring it to 60FPS"), WMap → 3 (WMap.java:465), Battle → 1 (Battle.java:704) — and reference rate = original rate × multiplier holds for every state (submap 30×2=60, wmap 20×3=60, battle 20×1=20, credits 30×2=60). The implied *original* rates are `Inferred`; the reference rates above are `Verified` (all write sites are decompiled ROM code).

**Timestamp.** `GameState52c.timestamp_a0` advances by `vsyncMode` per frame when the state `advancesTime()` (all states do, by default) — mechanism `Verified`. One timestamp unit is interpreted as 1/60 s of game time, so at 30 Hz it advances 2 per frame, at 20 Hz 3, at 60 Hz 1, keeping game time real-time at every rate — `Inferred` from the 60 Hz base consistency (no repo evidence conclusively pins the unit). Game logic and scripts use this for time.

**Frame pacing.** Software timers (Action, §1): the input and render actions are rescheduled to `hz` *every frame* from inside `gameLoop` (phase 3). If the machine is slow, actions skip ticks (catch-up) instead of bursting — **game logic runs at most once per presented frame; under sustained slowness the game runs slower than its nominal rate, while the audio threads keep real-time rate** (decoupling → potential long-term desync under load; `Inferred` consequence of the verified mechanisms). There is no vsync and no accumulator: this is "fixed-step-per-frame, variable frame time" — a **frame-locked (not fixed-timestep) simulation**.

**Speed multiplier.** `Config.gameSpeedMultiplier` (1..16, `config.conf`; runtime controls: speed up/down clamp 1..16, toggle restores the saved value, turbo sets 3). It multiplies both the real frame rate and the virtual-frame counter, so a multiplier of *m* runs the simulation at *m*× real time: `hz = 30m`, `vsyncCount += 60m/hz = 2` per frame → 2m virtual 60 Hz frames per real second… (i.e., m× game speed). It also modulates the render-buffer/skip indexing (§2 steps 7.1, 9). `Verified` for the formulas; the turbo rendering intent is `Inferred` (Q-36).

**`vsyncCount`.** A double, incremented per rendered frame by `60 * multiplier / fpsLimit`; exposed as `getVsyncCount()` (int). Used by the CRT shader (time) and as a fallback RNG seed (`simpleRand` seeds from it when the LCG seed's low bits are 0). `Verified`.

**`tickMultiplier()`.** Per engine state (default 2; WMap and Battle override). The javadoc says the original world map ran at 20 FPS and was "multiplied by 3 to bring it to 60 FPS" — i.e., it records the *original* state frame rate relative to the 60 Hz base. It is used only to normalize textbox scroll speeds and a few per-`tickCount` cadences (e.g., WMap every `4 * tickMultiplier` ticks, SMap max rotation `PI / (6 * tickMultiplier)`). It does **not** drive the frame rate (which is `vsyncMode`). `Verified` for usage; the historical frame-rate claim is taken from source comments (`Inferred` as history).

## 6. Audio update timing

| Thread | Rate | What runs |
|---|---|---|
| SFX (Audio.soundLoop, Audio.java:1543-1575) | 60 Hz (catch-up skip after 3× period) | `SEQUENCER.tick()` (game-level BGM state machine: command stream, fades, volume-over-time) + `SPU.tick()` (24-voice model: per sample slot — edge keyOn/keyOff, noise gen, ADSR, per-voice volume, reverb sum, master volume, clamp → `SAMPLES_PER_TICK` stereo pairs) → `source.bufferOutput` when the OpenAL source can buffer |
| OPEN_AL (AudioThread.run, AudioThread.java:292-361) | 120 Hz, except a tight loop while the core sequencer can buffer | For each audio source (core sequencer = SSSQ music synthesis; XaPlayer = Opus decode; SPU's GenericSource): `canBuffer`/`tick`/`handleProcessedBuffers` under per-source locks (all inside the AudioThread monitor); device-disconnect check; sleeps to the 120 Hz period only when the sequencer cannot buffer |

Cross-thread synchronization: **every** SPU control method (`keyOn`, `keyOff`, `clearKeyOn`, main/reverb volumes, reverb mode/config, noise mode/frequency, mute) is `synchronized(Spu.class)` — the same monitor `Spu.tick()` holds on the SFX thread — so there is **no data race** on the control words (audit F-02; the R1 claim of an unlocked write was a misread of the declaration). Edge semantics: control calls OR-accumulate the `keyOn`/`keyOff` level words; `tick()` consumes and clears them once per tick. `setPlayerVolume` (a float) is the only unsynchronized SPU write. `synchronized(this)` on AudioThread for all control calls from the main thread; per-`AudioSource` monitors. `Verified`.

Consequences: SFX (SPU) and music (sequencer) are independent real-time streams; the 60 Hz SPU tick and the 30/20/60 Hz frame rate are **not phase-locked** to the render loop (independent timers). Any coupling between game logic and sound is via queued state (queued sounds started at frame phase 14), not shared clocks. `Verified` for the mechanism; the "no phase lock" statement is a direct consequence (`Verified`).

## 7. Rendering paths (`legacyMode`)

`RenderEngine.legacyMode` (RenderEngine.java:104; Tab/Shift+Tab in dev builds) selects the presentation pipeline:

- **0 — "OpenGL rendering" (default in releases)**: the full pipeline of §2 step 8 — double FBO render buffers, sorted model pools, PSX translucency passes, post-process chain. The PSX GPU model's ordering-table commands are *still* ticked every frame (`Gpu.endFrame` → `tick()`, Gpu.java:189-215) and rendered from the z-queues; they coexist with the model-pool path (game code decides per object which path it uses — e.g., `tmd/Renderer.java:46`, `combat/effects/DeffTmdRenderer14.java:85`, `combat/SEffe.java:325`, `combat/particles/TmdParticle.java:116`, `Graphics.java:145` all branch on `legacyMode`).
- **1 — "legacy rendering"**: the GPU model uploads its software-rasterized display buffer to a texture each frame and draws it as a quad (`Gpu.tick`, Gpu.java:191-193); projections are a plain perspective/ortho pair (RenderBatch.java:117).
- **2 — "VRAM rendering"**: `Gpu.drawVram()` presents the VRAM texture directly (Gpu.java:194-195).

Which objects are drawn by the PSX command stream versus the modern model pools in mode 0, and whether mode 0's output is behaviorally faithful to the PSX pipeline, is an M4 question (register Q-37). `Verified` for the mechanism; classification deferred.

## 8. Scheduling and event dispatch summary

- **Scheduling constructs**: fixed-period Actions (input, render); `Async` fixed thread pool (file I/O, ad-hoc); audio 120 Hz loop + SFX 60 Hz loop; 2 s device poll; `RenderEngine.addTask` (frame-start queue); Unpacker (startup only, virtual-thread-based — M2).
- **Event systems** (two, deliberately separate):
  - **WindowEvents** (per window, callback lists under one lock): fired synchronously on the main thread during `tickInput`/`tick` — raw input, resize, focus, draw, close.
  - **Mod-Loader EventManager**: typed events, listeners discovered by reflection from loaded mods, priority-ordered (descending), per-listener error isolation (non-critical: log + `GameOverlay` notification; critical: `EventHandlerException` crash), process-wide static "active mod" attribution during delivery (`ModContainer` statics, last-writer-wins; effectively main-thread-only because all delivery happens on the main thread — audit F-04), cancellable events (`shouldPropagate`), stale-binding purge per frame (`clearStaleRefs`, onDraw step 4). `Verified`.
- **One-frame vs held state**: input `pressed` (one frame, cleared after onDraw) vs `held`/`repeat` (persists until release/focus loss) vs axis values (continuous).

## 9. Essential vs reference execution-order requirements

**Original (decompiled; must be preserved for behavioral fidelity):**
- The `gameLoop` phase order (0-19, §3), including: display swap at frame start; timestamp advance before pacing recompute; state transition processing before any other phase; `state.tick()` before `SCRIPTS.tick()` before `postScriptTick`; SPU queued sounds and textboxes after scripts; overlay tick last; `GPU.endFrame` after overlay tick; `tickCount` increment.
- Per-context `vsyncMode` rates (60 Hz submap/wmap/credits/battle-transition / 20 Hz battle / 30 Hz title, game over, wmap screens — §5 write-site census) and the 60 Hz timestamp unit.
- The PSX double-buffered display model (draw into the buffer not currently displayed).
- Engine-state transition semantics (queued, applied at frame top, with menu reset and `vsyncMode` re-fix to 2).

**Reference implementation (may be restructured):**
- The SDL/platform loop itself (actions, swap-before-onDraw, software pacing, no vsync).
- Input pumping between frames and the action-abstraction dispatch (replaces original pad polling).
- Task flush, stale-ref purge, post-process pass, frame-skip/turbo indexing, `vsyncCount` bookkeeping, Discord tick, `clearPressed`.
- Thread layout (SFX/OpenAL/audio-poll threads) — the original had no OS threads for these (single-threaded PSX).

## 10. Open items

- Q-36 (turbo/frame-skip intent), Q-37 (render-path faithfulness; extended by audit F-13 — the `B_PLUS_QUARTER_F` translucency pass sets the `B_PLUS_F` blend state — and F-18, the battle-colour shader hack), Q-38 (which states use LEGACY vs PERSPECTIVE `RenderMode` — resolved in `04-engine-states.md` §8: only SUBMAP and TITLE), Q-06 (per-phase invariants beyond ordering), Q-03/FUN_80020ed8 full semantics (now mostly resolved: values and consumers identified — kept open for the consumer-side effects of `_800bd7b0` in SMap).
