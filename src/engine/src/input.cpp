/// @file input.cpp
/// @brief Input state machine implementation (roadmap M1 "Input").
///
/// Behaviour is a faithful port of the reference platform input layer, down to
/// its quirks: unfocused windows still *process* events but every held action,
/// pressed set and axis value is wiped at the end of such a frame, so no
/// input is observable while the window is unfocused; degenerate deadzone
/// regions divide by zero and yield saturating/NaN results, which the `> 0`
/// test turns into silence. Both are pinned by tests rather than "fixed"
/// silently — the project reimplements behaviour, not just features.
#include "lod/input.h"

#include <algorithm>
#include <cmath>

namespace lod
{
namespace
{

/// @brief Half of the repeat interval (nanoseconds).
///
/// The reference splits its 100 ms repeat interval across the Repeat->Held and
/// Held->Repeat transitions, so a held action fires again every **100 ms**
/// (two 50 ms phases), not every 50 ms. Kept as written rather than
/// "rounded up": menu repeat cadence is gameplay timing.
constexpr std::uint64_t RepeatHalfNs = InputManager::RepeatIntervalNs / 2;

/// @brief Which half of an axis a value belongs to (reference
/// `InputAxisDirection.getDirection`, which indexes by the sign bit).
/// @param value Raw axis value.
/// @return Negative for a negative value, Positive otherwise (including 0).
[[nodiscard]] AxisDirection DirectionOf(std::int16_t value)
{
  return value < 0 ? AxisDirection::Negative : AxisDirection::Positive;
}

/// @brief Push a value into a frame-scoped set if it is not already in it.
/// @param set  Set to add to (mutated).
/// @param item Value to add (action id or packed source key).
template <class T>
void AddOnce(std::vector<T>& set, const T& item)
{
  if (std::find(set.begin(), set.end(), item) == set.end())
    set.push_back(item);
}

} // namespace

float ApplyDeadzone(std::int16_t rawAxis, const DeadzonePair& dz)
{
  const float magnitude = std::abs(static_cast<float>(rawAxis)) / AxisFullScale;
  const float span = std::abs(dz.Outer - dz.Inner);
  // A zero span (inner == outer) makes this ±inf or NaN. That is the reference
  // behaviour: inf clamps to full deflection, NaN fails the > 0 test below.
  // Reproducing it is deliberate; test `input.deadzone` pins both cases.
  const float shaped = (magnitude - dz.Inner) / span;

  if (!(shaped > 0.0f))
    return 0.0f;

  return std::min(1.0f, shaped) * (rawAxis > 0 ? 1.0f : -1.0f);
}

void InputManager::SetDeadzones(const DeadzonePair& menu, const DeadzonePair& movement)
{
  menuDz_ = menu;
  movementDz_ = movement;
}

void InputManager::RegisterAction(std::string actionID, bool useMovementDeadzone)
{
  ActionRuntime& runtime = actions_[actionID];
  runtime.UseMovementDeadzone = useMovementDeadzone;
}

void InputManager::SetBindings(std::vector<InputBinding> bindings)
{
  // Rebuild the table and the per-kind indexes together; the indexes hold
  // *indices*, never pointers, so they cannot dangle if the table is replaced.
  bindings_ = std::move(bindings);
  axisBindings_.clear();
  keyBindings_.clear();
  scanBindings_.clear();
  buttonBindings_.clear();

  for (std::size_t i = 0; i < bindings_.size(); ++i)
  {
    const std::uint32_t index = static_cast<std::uint32_t>(i);
    switch (bindings_[i].Kind)
    {
      case BindingKind::Axis:
        axisBindings_.push_back(index);
        break;
      case BindingKind::Key:
        keyBindings_.push_back(index);
        break;
      case BindingKind::Scan:
        scanBindings_.push_back(index);
        break;
      case BindingKind::Button:
        buttonBindings_.push_back(index);
        break;
    }
  }
}

void InputManager::Reset()
{
  actions_.clear();
  pressedThisFrame_.clear();
  repeatThisFrame_.clear();
  axesHeld_.clear();
  ignoredKeys_.clear();
  buttonsHeld_.clear();
  gamepads_.clear();
  lastPad_ = -1;
  lastClass_ = InputClass::Keyboard;
  focused_ = true;
}

void InputManager::BeginFrame(std::uint64_t nowNs)
{
  frameNs_ = nowNs;
  pressedThisFrame_.clear();
  repeatThisFrame_.clear();
}

void InputManager::EndFrame(bool focused, std::uint64_t nowNs)
{
  const bool allowed = focused || receiveOnInactive_;

  if (allowed)
  {
    // Repeat pass: a held action re-fires on the reference 500 ms delay +
    // 50 ms cadence. Iterated over every action, matching the reference, so a
    // repeat can fire even for an action whose source event arrived earlier.
    for (auto& [id, runtime] : actions_)
    {
      bool fired = false;

      // Time is compared as `now >= then + window` rather than
      // `now - then >= window`: identical while the clock is monotonic, and it
      // cannot wrap into a false "long time passed" if a host clock is
      // non-monotonic (a Windows QPC caveat).
      switch (runtime.Phase)
      {
        case ActionPhase::JustPressed:
          runtime.Phase = ActionPhase::Pressed;
          break;
        case ActionPhase::Pressed:
          runtime.Phase = ActionPhase::Delay;
          runtime.TimestampNs = nowNs;
          break;
        case ActionPhase::Delay:
          if (nowNs >= runtime.TimestampNs + RepeatDelayNs)
          {
            runtime.Phase = ActionPhase::Repeat;
            runtime.TimestampNs = nowNs;
            fired = true;
          }
          break;
        case ActionPhase::Repeat:
          if (nowNs >= runtime.TimestampNs + RepeatHalfNs)
          {
            runtime.Phase = ActionPhase::Held;
            runtime.TimestampNs = nowNs;
          }
          break;
        case ActionPhase::Held:
          if (nowNs >= runtime.TimestampNs + RepeatHalfNs)
          {
            runtime.Phase = ActionPhase::Repeat;
            runtime.TimestampNs = nowNs;
            fired = true;
          }
          break;
        case ActionPhase::Released:
          break;
      }

      if (fired)
        AddOnce(repeatThisFrame_, id);
    }
  }
  else
  {
    // Focus lost with background input disabled: drop everything, so a button
    // or key held across an alt-tab does not stay pressed after the refocus.
    pressedThisFrame_.clear();
    repeatThisFrame_.clear();
    ignoredKeys_.clear();
    axesHeld_.clear();
    buttonsHeld_.clear();

    for (auto& [id, runtime] : actions_)
    {
      if (runtime.Phase != ActionPhase::Released)
      {
        runtime.Phase = ActionPhase::Released;
        runtime.Axis = 0.0f;
      }
      runtime.AxisValues.clear();
      runtime.ButtonSources.clear();
    }
  }

  focused_ = focused;
}

void InputManager::OnKeyDown(std::uint32_t key, std::uint32_t scan, std::uint32_t mods, bool isRepeat)
{
  if (guard_ == SteamInputGuard::IgnoreWhenGamepadUsed && GamepadInputHeld())
  {
    // Remember the scancode so the matching key-up is swallowed too; without
    // that, a suppressed press leaves the action "held" in the binding layer.
    if (std::find(ignoredKeys_.begin(), ignoredKeys_.end(), scan) == ignoredKeys_.end())
      ignoredKeys_.push_back(scan);
    return;
  }

  lastClass_ = InputClass::Keyboard;

  // OS auto-repeat never re-triggers a binding (reference parity: repeat is
  // reported to listeners but does not fire actions).
  if (isRepeat)
    return;

  for (const std::uint32_t index : keyBindings_)
  {
    const InputBinding& binding = bindings_[index];
    if (binding.Code != key || (mods & binding.Mods) != binding.Mods)
      continue;
    TriggerPress(binding.ActionID);
  }

  for (const std::uint32_t index : scanBindings_)
  {
    const InputBinding& binding = bindings_[index];
    if (binding.Code != scan || (mods & binding.Mods) != binding.Mods)
      continue;
    TriggerPress(binding.ActionID);
  }
}

void InputManager::OnKeyUp(std::uint32_t key, std::uint32_t scan, std::uint32_t)
{
  if (guard_ == SteamInputGuard::IgnoreWhenGamepadUsed)
  {
    const auto ignored = std::find(ignoredKeys_.begin(), ignoredKeys_.end(), scan);
    if (ignored != ignoredKeys_.end())
    {
      // This key's press was suppressed, so its release must not fire either.
      ignoredKeys_.erase(ignored);
      return;
    }
  }

  lastClass_ = InputClass::Keyboard;

  for (const std::uint32_t index : keyBindings_)
  {
    const InputBinding& binding = bindings_[index];
    if (binding.Code == key)
      TriggerRelease(binding.ActionID);
  }

  for (const std::uint32_t index : scanBindings_)
  {
    const InputBinding& binding = bindings_[index];
    if (binding.Code == scan)
      TriggerRelease(binding.ActionID);
  }
}

void InputManager::OnButtonDown(std::uint32_t padID, std::uint32_t button)
{
  // Recorded before the focus gate (reference parity: the held count feeds the
  // Steam Input guard, which must stay correct while unfocused). Held sources
  // are a set rather than a counter, so a duplicate down cannot inflate it.
  const std::uint64_t sourceKey = ButtonSourceKey(padID, button);
  if (std::find(buttonsHeld_.begin(), buttonsHeld_.end(), sourceKey) == buttonsHeld_.end())
    buttonsHeld_.push_back(sourceKey);

  if (!InputAllowed())
    return;

  lastClass_ = InputClass::Gamepad;
  const bool known =
    std::any_of(gamepads_.begin(), gamepads_.end(), [&](const auto& pad) { return pad.first == padID; });
  lastPad_ = known ? static_cast<int>(padID) : -1;

  for (const std::uint32_t index : buttonBindings_)
  {
    const InputBinding& binding = bindings_[index];
    if (binding.Code == button)
      TriggerPress(binding.ActionID, sourceKey);
  }
}

void InputManager::OnButtonUp(std::uint32_t padID, std::uint32_t button)
{
  // Erase-if-present: a device removed mid-press must not drive the held set
  // negative, which would permanently disable keyboard input through the guard.
  std::erase(buttonsHeld_, ButtonSourceKey(padID, button));

  if (!InputAllowed())
    return;

  lastClass_ = InputClass::Gamepad;
  const bool known =
    std::any_of(gamepads_.begin(), gamepads_.end(), [&](const auto& pad) { return pad.first == padID; });
  lastPad_ = known ? static_cast<int>(padID) : -1;

  for (const std::uint32_t index : buttonBindings_)
  {
    const InputBinding& binding = bindings_[index];
    if (binding.Code == button)
      TriggerRelease(binding.ActionID);
  }
}

void InputManager::OnAxis(std::uint32_t padID, std::uint32_t axis, std::int16_t value)
{
  if (!InputAllowed())
    return;

  // The "is this axis deflected at all" gate uses the *smaller* of the two
  // inner deadzones: an action that reads a movement binding with a 0.2 inner
  // deadzone must still see a deflection that a menu action would ignore.
  const float minInner = std::min(menuDz_.Inner, movementDz_.Inner);
  const std::uint64_t axisKey = (static_cast<std::uint64_t>(padID) << 32) | axis;

  if (std::abs(static_cast<float>(value)) >= minInner * AxisFullScale)
  {
    lastClass_ = InputClass::Gamepad;
    const bool known =
      std::any_of(gamepads_.begin(), gamepads_.end(), [&](const auto& pad) { return pad.first == padID; });
    lastPad_ = known ? static_cast<int>(padID) : -1;

    if (std::find(axesHeld_.begin(), axesHeld_.end(), axisKey) == axesHeld_.end())
      axesHeld_.push_back(axisKey);
  }
  else
  {
    std::erase(axesHeld_, axisKey);
  }

  const AxisDirection direction = DirectionOf(value);

  for (const std::uint32_t index : axisBindings_)
  {
    const InputBinding& binding = bindings_[index];
    if (binding.Code != axis)
      continue;

    float shaped = 0.0f;
    if (binding.Direction == direction)
    {
      // Region follows the action: movement actions get the wide movement
      // region, everything else the narrow menu region (reference parity).
      ActionRuntime* runtime = FindRuntime(binding.ActionID);
      const DeadzonePair& region = runtime != nullptr && runtime->UseMovementDeadzone ? movementDz_ : menuDz_;
      shaped = ApplyDeadzone(value, region);
    }

    ApplyAxisSource(binding.ActionID, AxisSourceKey(padID, axis, direction), shaped);
  }
}

void InputManager::OnGamepadAdded(std::uint32_t padID, std::string name)
{
  if (std::any_of(gamepads_.begin(), gamepads_.end(), [&](const auto& pad) { return pad.first == padID; }))
    return;

  gamepads_.emplace_back(padID, std::move(name));
  lastPad_ = static_cast<int>(padID);
}

void InputManager::OnGamepadRemoved(std::uint32_t padID)
{
  std::erase_if(gamepads_, [&](const auto& pad) { return pad.first == padID; });

  if (lastPad_ == static_cast<int>(padID))
    lastPad_ = -1;

  // A yanked pad must not leave phantom state behind: its held-axis markers
  // go, and every action value sourced from that pad is recomputed (which
  // releases the action if no other source still drives it).
  std::erase_if(axesHeld_, [&](std::uint64_t key) { return PadOfSourceKey(key) == padID; });
  std::erase_if(buttonsHeld_, [&](std::uint64_t key) { return PadOfSourceKey(key) == padID; });

  for (auto& [id, runtime] : actions_)
  {
    const bool axesGone =
      std::erase_if(runtime.AxisValues, [&](const auto& entry) { return PadOfSourceKey(entry.first) == padID; }) != 0;
    const bool buttonsGone =
      std::erase_if(runtime.ButtonSources, [&](std::uint64_t key) { return PadOfSourceKey(key) == padID; }) != 0;

    if (!axesGone && !buttonsGone)
      continue;

    if (runtime.ButtonSources.empty() && runtime.AxisValues.empty())
    {
      // Nothing drives this action any more. The reference only clears axis
      // state here, which leaves a button held forever after a mid-press
      // unplug (and keeps the Steam Input guard muting the keyboard); this
      // boundary is device-fault recovery, not authored gameplay, so it
      // releases instead.
      runtime.Phase = ActionPhase::Released;
      runtime.Axis = 0.0f;
      std::erase(pressedThisFrame_, std::string(id));
      std::erase(repeatThisFrame_, std::string(id));
    }
    else if (axesGone)
    {
      ApplyAxisValue(id, CombineAxis(runtime));
    }
  }
}

bool InputManager::ActionPressed(std::string_view actionID) const
{
  return std::find(pressedThisFrame_.begin(), pressedThisFrame_.end(), actionID) != pressedThisFrame_.end();
}

bool InputManager::ActionRepeat(std::string_view actionID) const
{
  return std::find(repeatThisFrame_.begin(), repeatThisFrame_.end(), actionID) != repeatThisFrame_.end();
}

bool InputManager::ActionHeld(std::string_view actionID) const
{
  const auto it = actions_.find(actionID);
  return it != actions_.end() && it->second.Phase != ActionPhase::Released;
}

float InputManager::ActionAxis(std::string_view actionID) const
{
  const auto it = actions_.find(actionID);
  if (it == actions_.end())
    return 0.0f;

  return CombineAxis(it->second);
}

bool InputManager::KeyboardSuppressed() const
{
  return guard_ == SteamInputGuard::IgnoreWhenGamepadUsed && GamepadInputHeld();
}

float InputManager::CombineAxis(const ActionRuntime& runtime)
{
  float best = 0.0f;
  for (const auto& [source, value] : runtime.AxisValues)
  {
    static_cast<void>(source);
    if (std::abs(value) > std::abs(best))
      best = value;
  }
  return best;
}

InputManager::ActionRuntime& InputManager::Runtime(const std::string& actionID)
{
  return actions_[actionID];
}

InputManager::ActionRuntime* InputManager::FindRuntime(std::string_view actionID)
{
  const auto it = actions_.find(actionID);
  return it == actions_.end() ? nullptr : &it->second;
}

void InputManager::TriggerPress(std::string_view actionID, std::uint64_t sourceKey)
{
  const auto it = actions_.find(actionID);
  if (it == actions_.end())
    return;

  AddOnce(pressedThisFrame_, std::string(actionID));
  ActionRuntime& runtime = it->second;
  runtime.Phase = ActionPhase::JustPressed;
  runtime.Axis = 0.0f;
  if (sourceKey != NoSource)
    AddOnce(runtime.ButtonSources, sourceKey);
  // Timestamp latched from the frame clock (BeginFrame), which is what the
  // reference reads at press time. Tests drive it directly, so repeat timing
  // is deterministic instead of wall-clock dependent.
  runtime.TimestampNs = frameNs_;
}

void InputManager::TriggerRelease(std::string_view actionID)
{
  const auto it = actions_.find(actionID);
  if (it == actions_.end())
    return;

  ActionRuntime& runtime = it->second;
  runtime.Phase = ActionPhase::Released;
  runtime.Axis = 0.0f;
  runtime.ButtonSources.clear();
}

void InputManager::ApplyAxisValue(std::string_view actionID, float value)
{
  ActionRuntime* runtime = FindRuntime(actionID);
  if (runtime == nullptr)
    return;

  if (value != 0.0f)
  {
    if (runtime->Phase == ActionPhase::Released)
    {
      AddOnce(pressedThisFrame_, std::string(actionID));
      runtime->Phase = ActionPhase::JustPressed;
      runtime->TimestampNs = frameNs_;
    }
    runtime->Axis = value;
  }
  else if (runtime->Phase != ActionPhase::Released && runtime->Axis != 0.0f)
  {
    runtime->Phase = ActionPhase::Released;
    runtime->Axis = 0.0f;
  }
}

void InputManager::ApplyAxisSource(std::string_view actionID, std::uint64_t sourceKey, float value)
{
  ActionRuntime& runtime = Runtime(std::string(actionID));

  if (value == 0.0f)
    runtime.AxisValues.erase(sourceKey);
  else
    runtime.AxisValues[sourceKey] = value;

  ApplyAxisValue(actionID, CombineAxis(runtime));
}

} // namespace lod