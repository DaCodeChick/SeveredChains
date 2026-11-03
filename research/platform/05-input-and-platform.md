# Input and Platform

Status: Complete (R1). Repository: Severed-Chains @ `0c08c679b`.

Covers the platform abstraction, window lifecycle, the input model (actions/activations/bindings), dispatch order and state semantics, platform services, and the reference-vs-original boundary for input.

## 1. Platform abstraction

- **`PlatformManager`** (PlatformManager.java) — the service interface: `init/destroy`, window management (`addWindow`, `getLastWindow`, removal), action scheduling (`addAction`, `run` loop, `stop`), input tick (`tickInput`), input state queries (`isActionPressed/Repeat/Held`, `getAxis`), rumble, display enumeration, URL opening, clipboard paste, input-code naming.
- **`SdlPlatformManager`** — the real implementation (SDL3 via LWJGL: `SDL_INIT_VIDEO | SDL_INIT_GAMEPAD`, Windows raw-keyboard hint, background joystick events, `gamecontrollerdb.txt` mappings, opens all gamepads at init).
- **`NoopPlatformManager` / `NoopWindow`** — null implementations used by the build/test harness (the boot harness excluded from builds).

`addWindow` applies the fullscreen config (else centers) and the window minimum size is 320×240 (SdlWindow.java:141). `Verified`.

## 2. Window

`SdlWindow` (SdlWindow.java:107-143):

- Creates an OpenGL 3.3 core window; on failure falls back to OpenGLES 3.2; if neither, `RuntimeException`. Context creation failures are logged and retried on the fallback path.
- `SDL_GL_SetSwapInterval(0)` — no vsync (pacing is the Action timer; execution model §1).
- Registers the **render Action** (60 Hz default; rescheduled by `setFpsLimit`) whose tick is: `SDL_GL_SwapWindow` → `events().onDraw()` → `manager.clearPressed()`.
- Tracks `hasFocus`, `currentInputClass` (last interaction device), `shouldClose` (set by `close()`; the platform loop destroys the window at loop top).
- Window Events (WindowEvents.java) — callback lists, each list guarded by one lock: resize, focus gained/lost, key press/release (key, scancode, mods, repeat), button press/release, axis, text char, mouse move/press/release/scroll, input-class change, **input action pressed/released** (action, repeat), controller connected/disconnected, **draw**, **close**. All fire synchronously on the main thread.

## 3. Input model

### Actions

An **`InputAction`** is a semantic control (e.g., *confirm*, *move up*, *debug pause*), registered into `REGISTRIES.inputActions` by mods (core actions by `CoreMod`, retail control mappings by `LodMod`). Actions are the only thing the game and mods consume; physical controls are attached to actions via bindings.

### Activations (physical sources)

| Activation | Shape |
|---|---|
| `KeyInputActivation` | keycode + modifier mask |
| `ScancodeInputActivation` | physical scancode |
| `ButtonInputActivation` | gamepad button |
| `AxisInputActivation` | gamepad axis + direction (+ whether the action uses the movement or menu deadzone) |

### Bindings

- `InputBindings.BINDINGS`: map activation-class → list of `(action, activation)` (InputBindings.java).
- **`initBindings()`** (at every mod boot): resets platform action states, clears bindings, posts `RegisterDefaultInputBindingsEvent` so mods contribute defaults.
- **`loadBindings(CONFIG)`**: applies the user's saved overrides from the typed config entry `CONTROLLER_KEYBINDS_CONFIG` (per action → list of activations); `overwriteBindings` clears an action's existing bindings before adding.
- **`saveBindings`** writes the current effective bindings back to the config (options menu).

`Verified`.

### Per-action state (SdlPlatformManager)

| State | Lifetime | Semantics |
|---|---|---|
| `pressed` set | **one frame** — cleared by `clearPressed()` after every onDraw | "pressed this frame" (edge). `isActionPressed` |
| `actionStates` (per action: held, repeat, axis value) | until release / focus loss / re-boot | `isActionHeld`, `isActionRepeat`, `getAxis` |
| `axisActionStates` (per action: per-source axis values) | until the source returns to 0 or the gamepad disconnects | multi-gamepad merge = max |value| |

Deadzones: separate **menu** and **movement** inner/outer pairs (config: `MENU_INNER/OUTER_DEADZONE_CONFIG`, `MOVEMENT_INNER/OUTER_DEADZONE_CONFIG`); an action picks the pair via `useMovementDeadzone`; raw axis values (±0x7fff) are linearly remapped between inner/outer and clamped to 1.0. `Verified`.

### Input class and focus

- `setWindowInputClass` records KEYBOARD vs GAMEPAD per window and fires `InputClassChanged` on change (used e.g. by FMV skip-text handling to know which device's input counts).
- Gamepad and text input are processed only when the window has focus, **or** when `RECEIVE_INPUT_ON_INACTIVE_WINDOW_CONFIG` is on. On focus loss: all held actions are released (events fired) and all input state is cleared.
- **Steam Input workaround** (config `IGNORE_STEAM_INPUT_MODE_CONFIG`): while any gamepad button/axis is held, spurious keyboard events from Steam Input are swallowed (`ignoredKeys` tracks the swallowed scancodes until key-up; `buttonsHeld`/`axesHeld` counters gate it). `Verified` (workaround for a known Steam behavior, per source comments).

### Rumble

`rumble(intensity, ms)` / `rumble(big, small, ms)` targets the last-active gamepad (switching gamepads stops the previous); `adjustRumble` starts a time-based intensity lerp updated each input tick; `stopRumble` halts. The game's rumble system (`legend.game.Rumble`, driven by scripts — `scriptStartRumble`, `scriptStartRumbleMode`, dampener natives) feeds these. `Verified`.

## 4. Dispatch order (summary — full analysis in execution model §4)

```
input tick (hz, before render tick each iteration):
  SDL event pump (window/key/text/mouse/gamepad)
    key/button press  → raw window events + action press (edge, non-repeat only)
                       → onInputActionPressed(action, false) [→ game state, → RenderEngine handlers]
                       → action state press + InputPressedEvent [mod listeners]
    release           → onInputActionReleased + state release + InputReleasedEvent
    axis motion       → deadzone conversion → axis-action press/release/continuation
  repeats (held actions) → onInputActionPressed(action, true) + InputPressedEvent(repeat)
  focus loss → release all, clear all
  rumble lerp update
  [render tick: swap → onDraw → gameLoop (sees one-frame pressed + held states) → clearPressed]
```

Two consumer families receive action events **immediately during the input tick** (not during gameLoop): the engine state (via `bindRendererEvents`, execution model §3 note) and mod listeners (via `InputPressedEvent`/`InputReleasedEvent`). RenderEngine also consumes actions directly (freecam, fullscreen, speed/turbo, pause, frame advance, wireframe, shader reload — mostly reference-implementation conveniences; the speed/turbo actions mutate `Config.gameSpeedMultiplier`, which gameLoop reads next frame). `Verified`.

## 5. Game-side input consumption

- `EngineState.tick()` (base) computes the analogue stick vector from the four registered movement-axis actions each frame (execution model §3 phase 6).
- `EngineState.getInputsPressed/Repeat/Held` return state-derived bitmasks; `scriptInputActionPressed`/`scriptInputActionHeld` natives expose them to Fate scripts (native table entries 891/892).
- Original-LOD input semantics (pad bits: direction, L/R triggers, start/select, per-character button mappings) are represented by the retail action set registered by `LodMod`; the decompiled ROM's pad-polling code is gone, replaced by these hooks. The mapping of original pad bits to actions is an M9 detail (inventory/input content).

## 6. Reference vs original boundary

| Aspect | Layer |
|---|---|
| Action/activation/binding abstraction, gamepad support, keyboard, deadzones, rebinds, rumble lerp, input-class tracking, Steam workaround | **Reference implementation** |
| Semantic control set (confirm/cancel/move/interact/… and their in-game meanings) | **LoD-specific** (retail controls, registered by lodmod) |
| PSX pad hardware model (4-way + 4 buttons + triggers, no axes, no repeat hardware) | **PSX platform** (original; not directly modeled — the reference models *semantic* input instead) |
| When input is sampled relative to the frame (between frames in the reference; in-frame pad polling in the original) | Reference vs original (see execution model §9) |

## 7. Open items

- The full retail input action inventory (IDs, defaults, per-screen remapping rules) — M9.
- Whether `getInputsPressed` bitmasks preserve the original pad-bit layout (scripts may depend on specific bit positions) — M3/M9 cross-check.
