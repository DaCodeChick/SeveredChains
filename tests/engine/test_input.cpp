/// @file test_input.cpp
/// @brief Input state machine: deadzone curve, binding dispatch, repeat timing,
/// focus/background gating, device removal. Pure logic — no window, no SDL, no
/// controller — which is the point of keeping input.cpp SDL-free (testing.md
/// §2.1: the quirks are as much the test targets as the happy paths, because
/// they are *preserved* reference behaviour, not accidents).
#include "lod/input.h"
#include "sc_test.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{

using namespace lod;

/// @brief Build a binding quickly (fixture noise kept out of the cases).
InputBinding MakeBinding(BindingKind kind, std::uint32_t code, std::string action,
                         AxisDirection direction = AxisDirection::None, std::uint32_t mods = 0)
{
  InputBinding binding;
  binding.Kind = kind;
  binding.Code = code;
  binding.ActionID = std::move(action);
  binding.Direction = direction;
  binding.Mods = mods;
  return binding;
}

} // namespace

// ---------------------------------------------------------------------------
// Deadzone curve
// ---------------------------------------------------------------------------

LOD_TEST(input, deadzone)
{
  constexpr DeadzonePair menu{0.5f, 0.6f};
  constexpr DeadzonePair movement{0.2f, 0.9f};

  // Inside the region: silence. 10000/32767 = 0.305, below the menu threshold.
  LOD_CHECK(ApplyDeadzone(10000, menu) == 0.0f);
  LOD_CHECK(ApplyDeadzone(-10000, menu) == 0.0f);

  // At the outer edge the axis saturates: 0.6 of full scale on the menu region.
  const float atOuter = ApplyDeadzone(static_cast<std::int16_t>(0.6f * 32767.0f), menu);
  LOD_CHECK(atOuter > 0.999f && atOuter <= 1.0f);

  // Past the outer edge it stays saturated (clamped, not > 1).
  const float pastOuter = ApplyDeadzone(32767, menu);
  LOD_CHECK(pastOuter == 1.0f);

  // Mid-region scaling: (0.55 - 0.5) / 0.1 = 0.5 of full deflection.
  const float mid = ApplyDeadzone(static_cast<std::int16_t>(0.55f * 32767.0f), menu);
  LOD_CHECK(mid > 0.49f && mid < 0.56f);

  // Negative half mirrors the positive half exactly (sign, not magnitude loss).
  const float negMid = ApplyDeadzone(static_cast<std::int16_t>(-0.55f * 32767.0f), menu);
  LOD_CHECK(negMid < -0.49f && negMid > -0.56f);

  // Movement region is much wider: 0.305 of full scale is already past the
  // 0.2 inner edge, and the span is 0.7 rather than 0.1.
  const float moveMid = ApplyDeadzone(10000, movement);
  LOD_CHECK(moveMid > 0.14f && moveMid < 0.16f);

  // Full negative deflection.
  LOD_CHECK(ApplyDeadzone(-32768, movement) == -1.0f);

  // Reference quirk, pinned: a degenerate region (inner == outer) divides by
  // zero. Above the threshold that is +inf, which clamps to 1.0; at or below
  // it the quotient is negative or NaN, and both read as silence. These are
  // real axis values, not hand-built floats: 32767 * 0.5 is not an integer,
  // so 16383/16384 straddle the edge. Pinned because the project reproduces
  // the reference expression rather than a branch-shaped approximation.
  constexpr DeadzonePair degenerate{0.5f, 0.5f};
  LOD_CHECK(ApplyDeadzone(32767, degenerate) == 1.0f);
  LOD_CHECK(ApplyDeadzone(16384, degenerate) == 1.0f); // just above -> +inf
  LOD_CHECK(ApplyDeadzone(16383, degenerate) == 0.0f); // just below -> -inf
  LOD_CHECK(ApplyDeadzone(0, degenerate) == 0.0f);

  // The NaN branch with an input that actually reaches it: a threshold equal
  // to the magnitude of raw 16384 makes the numerator exactly zero, so the
  // shape is 0/0 and the `> 0` test is what keeps it quiet.
  constexpr float kHalfScale = 16384.0f / 32767.0f;
  constexpr DeadzonePair threshold{kHalfScale, kHalfScale};
  LOD_CHECK(ApplyDeadzone(16384, threshold) == 0.0f);
  LOD_CHECK(ApplyDeadzone(-16384, threshold) == 0.0f);

  // A fully open region (0..1) passes the raw deflection through.
  constexpr DeadzonePair open{0.0f, 1.0f};
  const float passthrough = ApplyDeadzone(16384, open);
  LOD_CHECK(passthrough > 0.49f && passthrough < 0.51f);
}

// ---------------------------------------------------------------------------
// Key / scancode bindings
// ---------------------------------------------------------------------------

LOD_TEST(input, keys)
{
  InputManager input;
  input.RegisterAction("confirm", false);
  input.RegisterAction("back", false);
  input.SetBindings({MakeBinding(BindingKind::Key, 88, "confirm"), // "X" keycode
                     MakeBinding(BindingKind::Scan, 27, "back", AxisDirection::None, 1)});

  input.BeginFrame(1'000);
  input.OnKeyDown(88, 17, 0, false);
  LOD_CHECK(input.ActionPressed("confirm"));
  LOD_CHECK(input.ActionHeld("confirm"));
  LOD_CHECK(input.ActionAxis("confirm") == 0.0f); // a button-like action has no axis
  LOD_CHECK(input.LastInputClass() == InputClass::Keyboard);

  input.OnKeyUp(88, 17, 0);
  LOD_CHECK(!input.ActionHeld("confirm"));
  input.EndFrame(true, 1'000);

  // Pressed is frame-scoped: the next frame starts clean.
  input.BeginFrame(2'000);
  LOD_CHECK(!input.ActionPressed("confirm"));
  input.EndFrame(true, 2'000);

  // Modifiers: the scancode binding requires modifier bit 1 to be present.
  input.BeginFrame(3'000);
  input.OnKeyDown(0, 27, 0, false);
  LOD_CHECK(!input.ActionPressed("back"));
  input.OnKeyDown(0, 27, 1, false);
  LOD_CHECK(input.ActionPressed("back"));
  input.EndFrame(true, 3'000);

  // OS auto-repeat never re-triggers a binding (only the first press fires).
  input.BeginFrame(4'000);
  input.OnKeyDown(88, 17, 0, false);
  LOD_CHECK(input.ActionPressed("confirm"));
  input.EndFrame(true, 4'000);
  input.BeginFrame(5'000);
  input.OnKeyDown(88, 17, 0, true);
  LOD_CHECK(!input.ActionPressed("confirm"));
  LOD_CHECK(input.ActionHeld("confirm")); // still held, just not re-pressed
  input.EndFrame(true, 5'000);
}

// ---------------------------------------------------------------------------
// Gamepad buttons
// ---------------------------------------------------------------------------

LOD_TEST(input, buttons)
{
  InputManager input;
  input.RegisterAction("jump", false);
  input.SetBindings({MakeBinding(BindingKind::Button, 1, "jump")});
  input.OnGamepadAdded(3, "Test Pad");
  input.BeginFrame(1'000);

  input.OnButtonDown(3, 1);
  LOD_CHECK(input.ActionPressed("jump"));
  LOD_CHECK(input.LastInputClass() == InputClass::Gamepad);
  LOD_CHECK(input.LastGamepadID() == 3);
  input.OnButtonUp(3, 1);
  LOD_CHECK(!input.ActionHeld("jump"));
  LOD_CHECK(!input.GamepadInputHeld());
  input.EndFrame(true, 1'000);

  // An unknown pad must not become the "last pad" (no phantom controller).
  // The button is released before leaving the frame because it is a *live
  // source* for "jump": a later pad removal must not be blamed for it.
  input.BeginFrame(2'000);
  input.OnButtonDown(9, 1);
  LOD_CHECK(input.LastGamepadID() == -1);
  input.OnButtonUp(9, 1);
  LOD_CHECK(!input.ActionHeld("jump"));
  input.EndFrame(true, 2'000);

  // Device removal releases what that pad was holding, and disarms the Steam
  // Input guard: a pulled controller must not leave the keyboard suppressed.
  input.OnGamepadAdded(4, "Second Pad");
  input.BeginFrame(3'000);
  input.OnButtonDown(4, 1);
  LOD_CHECK(input.ActionHeld("jump"));
  input.OnGamepadRemoved(4);
  LOD_CHECK(!input.ActionHeld("jump"));
  LOD_CHECK(input.LastGamepadID() == -1);
  LOD_CHECK(!input.GamepadInputHeld());
  input.EndFrame(true, 3'000);

  // Unbalanced releases afterwards are no-ops (the held set is a set, not a
  // counter that could go negative and mute the keyboard forever).
  input.BeginFrame(4'000);
  input.OnButtonUp(4, 1);
  input.OnButtonUp(4, 1);
  LOD_CHECK(!input.GamepadInputHeld());
  input.EndFrame(true, 4'000);

  // Two pads driving one action: unplugging one keeps the other's press.
  input.OnGamepadAdded(5, "Pad A");
  input.OnGamepadAdded(6, "Pad B");
  input.BeginFrame(5'000);
  input.OnButtonDown(5, 1);
  input.OnButtonDown(6, 1);
  input.OnGamepadRemoved(5);
  LOD_CHECK(input.ActionHeld("jump"));
  input.OnButtonUp(6, 1);
  LOD_CHECK(!input.ActionHeld("jump"));
  input.EndFrame(true, 5'000);
}

// ---------------------------------------------------------------------------
// Axis bindings and deadzones
// ---------------------------------------------------------------------------

LOD_TEST(input, axis)
{
  InputManager input;
  input.RegisterAction("menu.right", false); // menu region 0.5..0.6
  input.RegisterAction("move.right", true);  // movement region 0.2..0.9
  input.SetBindings({MakeBinding(BindingKind::Axis, 0, "menu.right", AxisDirection::Positive),
                     MakeBinding(BindingKind::Axis, 0, "move.right", AxisDirection::Positive)});
  input.OnGamepadAdded(1, "Stick Pad");

  // Inside the menu deadzone but past the movement one: the movement action
  // fires, the menu action does not. This is exactly why two regions exist.
  input.BeginFrame(1'000);
  input.OnAxis(1, 0, 10000);
  LOD_CHECK(!input.ActionPressed("menu.right"));
  LOD_CHECK(input.ActionPressed("move.right"));
  const float partial = input.ActionAxis("move.right");
  LOD_CHECK(partial > 0.14f && partial < 0.16f);
  LOD_CHECK(input.GamepadInputHeld());
  input.EndFrame(true, 1'000);

  // Returning to the deadzone releases the action.
  input.BeginFrame(2'000);
  input.OnAxis(1, 0, 0);
  LOD_CHECK(!input.ActionHeld("move.right"));
  LOD_CHECK(input.ActionAxis("move.right") == 0.0f);
  input.EndFrame(true, 2'000);

  // Direction split: the negative half must not drive a positive-half action,
  // and a negative binding sees the mirrored value.
  input.SetBindings({MakeBinding(BindingKind::Axis, 0, "menu.right", AxisDirection::Negative)});
  input.BeginFrame(3'000);
  input.OnAxis(1, 0, 20000);
  LOD_CHECK(!input.ActionHeld("menu.right"));
  input.OnAxis(1, 0, -20000);
  LOD_CHECK(input.ActionPressed("menu.right"));
  LOD_CHECK(input.ActionAxis("menu.right") == -1.0f);
  input.EndFrame(true, 3'000);

  // Two sources combine by largest magnitude, never by sum: a second pad at
  // 0.3 must not add to the first pad's 0.6 to make a fake full deflection.
  input.SetBindings({MakeBinding(BindingKind::Axis, 0, "move.right", AxisDirection::Positive)});
  input.OnGamepadAdded(2, "Other Pad");
  input.BeginFrame(4'000);
  input.OnAxis(1, 0, 20000);
  const float firstPad = input.ActionAxis("move.right");
  input.OnAxis(2, 0, 12000);
  const float bothPads = input.ActionAxis("move.right");
  LOD_CHECK(bothPads >= firstPad);
  LOD_CHECK(bothPads < 1.0f);
  LOD_CHECK(bothPads < firstPad + 0.25f); // not a sum of the two deflections
  input.EndFrame(true, 4'000);

  // Pulling the pad that carries the larger value falls back to the other one.
  input.BeginFrame(5'000);
  input.OnGamepadRemoved(1);
  const float afterUnplug = input.ActionAxis("move.right");
  LOD_CHECK(afterUnplug > 0.0f && afterUnplug < firstPad);
  input.OnGamepadRemoved(2);
  LOD_CHECK(input.ActionAxis("move.right") == 0.0f);
  LOD_CHECK(!input.ActionHeld("move.right"));
  input.EndFrame(true, 5'000);

  // Gate: below the smaller inner deadzone the axis is not even considered
  // "held" (min inner of the two regions is the movement region's 0.2).
  input.BeginFrame(6'000);
  input.OnAxis(1, 0, 0);
  LOD_CHECK(!input.GamepadInputHeld());
  input.EndFrame(true, 6'000);
}

// ---------------------------------------------------------------------------
// Steam Input guard
// ---------------------------------------------------------------------------

LOD_TEST(input, steamGuard)
{
  InputManager input;
  input.RegisterAction("confirm", false);
  input.SetBindings({MakeBinding(BindingKind::Scan, 27, "confirm"), MakeBinding(BindingKind::Button, 1, "confirm")});

  input.OnGamepadAdded(1, "Steam Deck");

  // A held gamepad button suppresses keyboard input (Steam Input emits a
  // synthetic key alongside the controller event; without this the action
  // double-fires).
  input.BeginFrame(1'000);
  input.OnButtonDown(1, 1);
  LOD_CHECK(input.KeyboardSuppressed());
  input.OnKeyDown(41, 27, 0, false);
  LOD_CHECK(!input.ActionRepeat("confirm")); // no repeat either
  LOD_CHECK(input.ActionPressed("confirm")); // pressed once, by the button
  input.EndFrame(true, 1'000);

  // After the button is released, keyboard input works again and the
  // suppressed key's release is swallowed (so no stuck action).
  input.BeginFrame(2'000);
  input.OnButtonUp(1, 1);
  LOD_CHECK(!input.KeyboardSuppressed());
  input.OnKeyUp(41, 27, 0);
  input.EndFrame(true, 2'000);

  input.BeginFrame(3'000);
  input.OnKeyDown(41, 27, 0, false);
  LOD_CHECK(input.ActionPressed("confirm"));
  input.EndFrame(true, 3'000);

  // Guard off: the sequence is delivered even while the pad is held. Both are
  // released inside the frame so the next block starts from a clean pad.
  input.SetSteamInputGuard(SteamInputGuard::Off);
  input.BeginFrame(4'000);
  input.OnButtonDown(1, 1);
  input.OnKeyDown(41, 27, 0, false);
  LOD_CHECK(!input.KeyboardSuppressed());
  LOD_CHECK(input.ActionPressed("confirm"));
  input.OnButtonUp(1, 1);
  input.OnKeyUp(41, 27, 0);
  input.EndFrame(true, 4'000);

  // A held *axis* also suppresses the keyboard, and only while deflected
  // past the deadzone gate.
  input.SetSteamInputGuard(SteamInputGuard::IgnoreWhenGamepadUsed);
  input.BeginFrame(5'000);
  input.OnAxis(1, 0, 30000);
  input.OnKeyDown(41, 27, 0, false);
  LOD_CHECK(input.GamepadInputHeld());
  LOD_CHECK(!input.ActionPressed("confirm"));
  input.EndFrame(true, 5'000);

  input.BeginFrame(6'000);
  input.OnAxis(1, 0, 100); // inside the deadzone: no longer "held"
  // The key is released before it is pressed again: while a suppressed key
  // stays in the ignore set, a further down for it is dropped as well.
  input.OnKeyUp(41, 27, 0);
  input.OnKeyDown(41, 27, 0, false);
  LOD_CHECK(!input.GamepadInputHeld());
  LOD_CHECK(input.ActionPressed("confirm"));
  input.EndFrame(true, 6'000);
}

// ---------------------------------------------------------------------------
// Focus gating
// ---------------------------------------------------------------------------

LOD_TEST(input, focus)
{
  InputManager input;
  input.RegisterAction("confirm", false);
  input.RegisterAction("move.right", true);
  input.SetBindings({MakeBinding(BindingKind::Button, 1, "confirm"),
                     MakeBinding(BindingKind::Axis, 0, "move.right", AxisDirection::Positive)});
  input.OnGamepadAdded(1, "Pad");

  // Focused: delivered.
  input.BeginFrame(1'000);
  input.OnButtonDown(1, 1);
  input.EndFrame(true, 1'000);
  input.BeginFrame(2'000);
  LOD_CHECK(input.ActionHeld("confirm"));
  input.OnButtonUp(1, 1);
  input.EndFrame(true, 2'000);

  // Unfocused with background input disabled (the default): controller input
  // is not delivered at all, and anything already held is released at the end
  // of the frame so an alt-tab cannot leave a button stuck.
  input.BeginFrame(3'000);
  input.OnButtonDown(1, 1);
  input.OnAxis(1, 0, 30000);
  input.EndFrame(false, 3'000);
  LOD_CHECK(!input.ActionHeld("confirm"));
  LOD_CHECK(!input.ActionPressed("confirm"));
  LOD_CHECK(input.ActionAxis("move.right") == 0.0f);
  LOD_CHECK(!input.GamepadInputHeld());

  // Background input allowed: the same events are delivered while unfocused,
  // and a button that is *still held* stays held — the focus gate only wipes
  // state on frames where input is not allowed.
  input.SetReceiveInputOnInactiveWindow(true);
  input.BeginFrame(4'000);
  input.OnButtonDown(1, 1);
  input.EndFrame(false, 4'000);
  LOD_CHECK(input.ActionHeld("confirm"));
  input.BeginFrame(5'000);
  LOD_CHECK(input.ActionHeld("confirm"));

  // Turning background input back off mid-hold clears it on that frame's
  // EndFrame: this is the alt-tab path the wipe exists for.
  input.SetReceiveInputOnInactiveWindow(false);
  input.EndFrame(false, 5'000);
  LOD_CHECK(!input.ActionHeld("confirm"));
  LOD_CHECK(!input.GamepadInputHeld());
}

// ---------------------------------------------------------------------------
// Repeat timing
// ---------------------------------------------------------------------------

LOD_TEST(input, repeat)
{
  InputManager input;
  input.RegisterAction("menu.down", false);
  input.SetBindings({MakeBinding(BindingKind::Scan, 26, "menu.down")});

  // Hold the key for 5 seconds of simulated time at 16ms per frame and
  // record which frames reported a repeat. Delay 500ms, then the 100ms
  // cadence (two 50ms phases), quantised to the 16ms frame.
  std::vector<double> fireMs;
  std::uint64_t now = 0;
  bool firstFrame = true;

  while (now < 5'000'000'000ULL)
  {
    input.BeginFrame(now);
    if (firstFrame)
    {
      input.OnKeyDown(0, 26, 0, false);
      firstFrame = false;
    }
    input.EndFrame(true, now);

    if (input.ActionRepeat("menu.down"))
      fireMs.push_back(static_cast<double>(now) / 1'000'000.0);
    now += 16'000'000ULL;
  }

  // Nothing repeats inside the 500ms delay, and the machine is never
  // "repeating" before the first delay elapses.
  for (const double ms : fireMs)
    LOD_CHECK(ms > 500.0);

  // It does repeat (a one-shot would produce zero fires), and the cadence is
  // the reference's 100ms interval, quantised up to the 16ms frame (two 50ms
  // phase transitions, each landing on a frame boundary).
  LOD_REQUIRE(fireMs.size() > 20);
  std::size_t gapsInRange = 0;
  for (std::size_t i = 1; i < fireMs.size(); ++i)
  {
    const double gap = fireMs[i] - fireMs[i - 1];
    if (gap >= 90.0 && gap <= 140.0)
      ++gapsInRange;
  }
  LOD_CHECK(gapsInRange >= fireMs.size() - 2);

  // Lifting the key stops repeats immediately.
  input.BeginFrame(now);
  input.OnKeyUp(0, 26, 0);
  input.EndFrame(true, now);
  input.BeginFrame(now + 16'000'000ULL);
  input.EndFrame(true, now + 16'000'000ULL);
  LOD_CHECK(!input.ActionHeld("menu.down"));
  LOD_CHECK(!input.ActionRepeat("menu.down"));
}

LOD_TEST(input, reset)
{
  InputManager input;
  input.RegisterAction("confirm", false);
  input.SetBindings({MakeBinding(BindingKind::Button, 1, "confirm")});
  input.OnGamepadAdded(1, "Pad");
  input.BeginFrame(1'000);
  input.OnButtonDown(1, 1);
  input.OnAxis(1, 0, 20000);
  LOD_REQUIRE(input.ActionHeld("confirm"));
  LOD_REQUIRE(input.GamepadInputHeld());

  input.Reset();
  LOD_CHECK(!input.ActionHeld("confirm"));
  LOD_CHECK(!input.ActionPressed("confirm"));
  LOD_CHECK(!input.GamepadInputHeld());
  LOD_CHECK(input.GamepadCount() == 0);
  LOD_CHECK(input.LastGamepadID() == -1);
  LOD_CHECK(input.LastInputClass() == InputClass::Keyboard);

  // After a reset the table is empty, so nothing fires until it is rebuilt.
  input.BeginFrame(2'000);
  input.OnButtonDown(1, 1);
  LOD_CHECK(!input.ActionPressed("confirm"));
  input.EndFrame(true, 2'000);
}