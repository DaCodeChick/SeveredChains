#pragma once
/// @file input.h
/// @brief Input vocabulary, binding table and action state machine (roadmap
/// M1 "Input": keyboard scancodes, gamepad buttons/axes, the deadzone model,
/// focus/background gating, key repeat).
///
/// **SDL-free by design.** Nothing here includes SDL: the platform layer
/// translates OS event types into the injection calls below (cpp.md §9.1), so
/// the whole state machine is unit-testable without a display, a window, or a
/// connected controller (testing.md §2.1). The numeric code space of
/// `Binding::code` / the injection calls is the *platform* code space (SDL
/// keycodes, scancodes, gamepad button and axis indices today); it is a
/// platform detail on purpose — rebinding data will name controls by string,
/// not by number, when the rebind UI exists.
///
/// **Delivery is pull-based.** Actions are read with the query API once per
/// frame rather than pushed through a listener bus, because there is no
/// listener consumer yet (AGENTS.md §4: no speculative infrastructure). The
/// frame-scoped queries *are* the delivery contract; the game layer reads them
/// during its ticks.
///
/// @note Thread: main thread only (the frame loop). No internal locking; the
///       state is per-frame and never crosses a thread boundary.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lod
{

/// @brief Which device family last produced input.
///
/// Mirrors the reference "input class" used to decide whether menus should
/// draw keyboard/mouse or controller prompts. It is a *sticky* per-frame
/// classification, not a per-event one.
enum class InputClass
{
  Keyboard, ///< Keyboard or mouse produced the last input.
  Gamepad,  ///< A gamepad produced the last input.
};

/// @brief Half-axis direction for axis bindings.
///
/// An action may bind to one *half* of an axis ("left on the stick"), which is
/// why bindings carry a direction and why one axis can feed two actions.
enum class AxisDirection
{
  None,     ///< Not an axis binding (key/button bindings use this).
  Negative, ///< Fires on the negative half of the axis.
  Positive, ///< Fires on the positive half of the axis.
};

/// @brief Kind of physical activation a binding watches.
enum class BindingKind
{
  Key,    ///< Keyboard keycode (platform keycode space).
  Scan,   ///< Keyboard scancode (layout independent; recommended for rebinding).
  Button, ///< Gamepad button (platform gamepad button space).
  Axis,   ///< Gamepad axis (platform gamepad axis space) with a direction.
};

/// @brief Steam Input workaround mode (reference `IgnoreSteamInputMode`).
///
/// Steam Input can emit a synthetic keyboard event alongside the controller
/// event, which double-fires actions. `IgnoreWhenGamepadUsed` suppresses
/// keyboard input while controller buttons/axes are held — including the
/// matching key-up, which is remembered in the ignored-key set so a suppressed
/// press cannot leave a stuck action.
///
/// The reference's third mode (filter by a virtual keyboard device id) is
/// commented out upstream and is therefore not implemented here either.
enum class SteamInputGuard
{
  Off,                   ///< No filtering.
  IgnoreWhenGamepadUsed, ///< Drop keyboard input while gamepad input is held.
};

/// @brief One inner/outer deadzone pair (a "deadzone region").
///
/// Inside `inner` the axis reads zero; past `outer` it saturates; between the
/// two the deflection is rescaled. The reference keeps two regions — one for
/// menu navigation, one for movement — because a stick used for d-pad-style
/// menu input needs a much larger inner deadzone than one used for walking.
struct DeadzonePair
{
  /// @brief Fraction of full scale (0..1) that still reads as zero.
  float Inner = 0.5f;
  /// @brief Fraction of full scale (0..1) at which the axis reaches 1.0.
  float Outer = 0.6f;
};

/// @brief Reference deadzone constants (the four registry defaults).
inline constexpr DeadzonePair DefaultMenuDeadzone{0.5f, 0.6f};
/// @copydoc DefaultMenuDeadzone
inline constexpr DeadzonePair DefaultMovementDeadzone{0.2f, 0.9f};

/// @brief Full-scale divisor used by the reference axis math (`0x7fff`).
///
/// SDL reports axes as signed 16-bit; the reference normalises by 32767 rather
/// than 32768, which is preserved exactly rather than "fixed".
inline constexpr float AxisFullScale = 32767.0f;

/// @brief Apply the reference deadzone curve to one raw axis value.
///
/// `value = min(1, (|raw| / 32767 - inner) / |outer - inner|)`, and the value
/// is kept only when that is `> 0`, sign-signed by `signum(raw)`.
///
/// Degenerate regions are *behaviour*, not a bug to hide: when `inner ==
/// outer` the divisor is zero, so the quotient is ±inf (clamped to 1) or NaN
/// when the numerator is also zero, and NaN fails the `> 0` test. That is what
/// the reference computes and this function reproduces it bit-for-bit; a test
/// pins the behaviour so a future "cleanup" cannot silently change it.
///
/// @param rawAxis Raw axis value in [-32768, 32767] (SDL/gamepad range).
/// @param dz      Region to apply.
/// @return Signed deflection in [-1, 1]; 0 inside the deadzone.
[[nodiscard]] float ApplyDeadzone(std::int16_t rawAxis, const DeadzonePair& dz);

/// @brief One named gameplay/UI intent that physical controls drive.
struct InputAction
{
  std::string ID;                   ///< Stable id, e.g. "confirm", "move.run".
  bool UseMovementDeadzone = false; ///< true = movement region, false = menu region.
};

/// @brief One physical activation mapped to an action.
struct InputBinding
{
  BindingKind Kind = BindingKind::Key;           ///< What the binding watches.
  std::uint32_t Code = 0;                        ///< Platform code of the key/button/axis.
  std::uint32_t Mods = 0;                        ///< Required modifier mask (0 = none).
  AxisDirection Direction = AxisDirection::None; ///< Axis half (axis bindings only).
  std::string ActionID;                          ///< Action this activation drives.
};

/// @brief Phases of the reference repeat state machine.
enum class ActionPhase
{
  Released,    ///< Not active.
  JustPressed, ///< Pressed this frame (first query after the press).
  Pressed,     ///< Held, past the initial press, before the repeat delay.
  Delay,       ///< Held, waiting out the repeat delay.
  Repeat,      ///< A repeat fired.
  Held,        ///< Held between repeats.
};

/// @brief Input state for one frame: registry, bindings, held devices, actions.
///
/// Lifetime: one per app, owned by the platform layer. `BeginFrame` clears the
/// frame-scoped press set; event injection updates action state; `EndFrame`
/// runs the repeat pass (or clears everything when the window may not receive
/// input). Values mirror the reference implementation exactly, including its
/// quirks, because gameplay timing (menu navigation, addition timing) depends
/// on them.
class InputManager final
{
public:
  /// @brief Repeat delay/interval (nanoseconds), reference values.
  static constexpr std::uint64_t RepeatDelayNs = 500'000'000ULL;
  /// @copydoc RepeatDelayNs
  static constexpr std::uint64_t RepeatIntervalNs = 100'000'000ULL;

  // -- configuration -------------------------------------------------------

  /// @brief Replace the two deadzone regions.
  /// @param menu    Region for menu/UI actions (`useMovementDeadzone == false`).
  /// @param movement Region for movement actions.
  void SetDeadzones(const DeadzonePair& menu, const DeadzonePair& movement);

  /// @brief Set the Steam Input guard mode.
  void SetSteamInputGuard(SteamInputGuard mode) { guard_ = mode; }

  /// @brief Allow/disallow input while the window is unfocused.
  ///
  /// Reference parity: `receive_input_on_inactive_window`, default off. When
  /// off, an unfocused window drops input *and* clears held state, so a
  /// controller button held while alt-tabbing does not stay pressed forever.
  void SetReceiveInputOnInactiveWindow(bool allow) { receiveOnInactive_ = allow; }

  /// @brief Register (or re-register) an action by id.
  /// @param actionID          Stable action id.
  /// @param useMovementDeadzone Which deadzone region the action uses.
  void RegisterAction(std::string actionID, bool useMovementDeadzone);

  /// @brief Replace the whole binding table.
  /// @param bindings Bindings to use; entries with an unknown actionID are
  ///                 still stored (a rebind may arrive before the action).
  void SetBindings(std::vector<InputBinding> bindings);

  /// @brief Drop all state and bindings (config reload, device rescan).
  void Reset();

  // -- frame boundaries ----------------------------------------------------

  /// @brief Start a frame: clears the frame-scoped pressed/repeat sets and
  /// latches the frame time used by the repeat state machine.
  /// @param nowNs Monotonic time in nanoseconds (injected for determinism).
  void BeginFrame(std::uint64_t nowNs);

  /// @brief End a frame: run the repeat pass, or clear state if input is off.
  ///
  /// @param focused  Whether the window currently has focus.
  /// @param nowNs    Monotonic time in nanoseconds (injected so tests are
  ///                 deterministic). Must not go backwards; a rewind is
  ///                 treated as "no time passed".
  void EndFrame(bool focused, std::uint64_t nowNs);

  // -- injection (called by the platform layer) ----------------------------

  /// @brief Keyboard press.
  /// @param key   Platform keycode (0 when unmapped).
  /// @param scan  Platform scancode.
  /// @param mods  Modifier mask active for this event.
  /// @param isRepeat True for an OS auto-repeat of a held key (never
  ///                 re-triggers bindings; reference parity).
  void OnKeyDown(std::uint32_t key, std::uint32_t scan, std::uint32_t mods, bool isRepeat);

  /// @brief Keyboard release (also removes a suppressed key from the ignore set).
  void OnKeyUp(std::uint32_t key, std::uint32_t scan, std::uint32_t mods);

  /// @brief Gamepad button press.
  /// @param padID Platform id of the pad that reported the button.
  /// @param button Platform gamepad button index.
  void OnButtonDown(std::uint32_t padID, std::uint32_t button);

  /// @brief Gamepad button release. Clamped at zero held buttons.
  void OnButtonUp(std::uint32_t padID, std::uint32_t button);

  /// @brief Gamepad axis motion.
  /// @param padID Platform id of the pad.
  /// @param axis  Platform gamepad axis index.
  /// @param value Raw axis value in [-32768, 32767].
  void OnAxis(std::uint32_t padID, std::uint32_t axis, std::int16_t value);

  /// @brief A gamepad was connected (name kept for logging/overlay only).
  /// @param padID Platform id.
  /// @param name  Device name (may be empty).
  void OnGamepadAdded(std::uint32_t padID, std::string name);

  /// @brief A gamepad was disconnected: releases that pad's held buttons and
  ///        axes so a yanked controller cannot leave an action stuck.
  void OnGamepadRemoved(std::uint32_t padID);

  // -- queries -------------------------------------------------------------

  /// @brief Was the action pressed during this frame (initial press only)?
  [[nodiscard]] bool ActionPressed(std::string_view actionID) const;

  /// @brief Did a repeat fire for the action this frame?
  ///
  /// @note The reference's `isActionRepeat` is the *union* of the press state
  /// and a fired repeat. A consumer that needs that exact reading ORs it with
  /// ActionPressed(); the two are kept apart because a first press and a
  /// cadence repeat mean different things outside menu navigation.
  [[nodiscard]] bool ActionRepeat(std::string_view actionID) const;

  /// @brief Is the action currently held (any phase but Released)?
  [[nodiscard]] bool ActionHeld(std::string_view actionID) const;

  /// @brief Combined axis value driving the action, [-1, 1].
  ///
  /// Multiple sources (two pads, stick + trigger) combine by **largest
  /// magnitude**, never by sum — two half-deflected sticks do not add to a
  /// full one (reference parity).
  [[nodiscard]] float ActionAxis(std::string_view actionID) const;

  /// @brief Sticky last-device classification for prompt styling.
  [[nodiscard]] InputClass LastInputClass() const { return lastClass_; }

  /// @brief Is any controller input currently held (buttons or axes)?
  ///
  /// This is the condition the Steam Input guard filters keyboard input on.
  /// It reads the held *source* sets rather than an event counter, so a device
  /// that vanishes cannot leave it stuck true (see OnGamepadRemoved).
  [[nodiscard]] bool GamepadInputHeld() const { return !buttonsHeld_.empty() || !axesHeld_.empty(); }

  /// @brief Number of connected pads (0 before the first plug-in event).
  [[nodiscard]] std::size_t GamepadCount() const { return gamepads_.size(); }

  /// @brief Id of the pad that produced input last, or -1 when none.
  [[nodiscard]] int LastGamepadID() const { return lastPad_; }

  /// @brief Is keyboard input currently suppressed by the guard?
  ///
  /// Exposed for the debug overlay and tests; it is not a gameplay query.
  [[nodiscard]] bool KeyboardSuppressed() const;

  /// @brief Record a pointer (mouse motion / button / wheel) event.
  ///
  /// Only the prompt-style classification happens today: raw pointer delivery
  /// to a UI layer is deferred until a consumer exists (AGENTS.md §4 forbids
  /// speculative infrastructure). The classification still matters because it
  /// decides whether on-screen prompts draw key or controller glyphs.
  ///
  /// @note Thread: main thread (frame loop).
  void NotePointerInput() { lastClass_ = InputClass::Keyboard; }

private:
  /// @brief Per-action runtime state (phase, axis, repeat clock).
  struct ActionRuntime
  {
    ActionPhase Phase = ActionPhase::Released;
    float Axis = 0.0f;
    std::uint64_t TimestampNs = 0;
    bool UseMovementDeadzone = false;
    /// @brief Per-source axis values, keyed by packed (pad, axis, direction).
    /// Combined by largest magnitude when read (reference parity).
    std::unordered_map<std::uint64_t, float> AxisValues;
    /// @brief Packed (pad, button) keys of the buttons currently holding this
    /// action. Needed so unplugging one pad can release only what *that* pad
    /// was driving.
    std::vector<std::uint64_t> ButtonSources;
  };

  /// @brief Transparent string hash so the per-frame action queries can look
  /// up by `std::string_view` without building a temporary key. Both overloads
  /// delegate to the same hash — a mismatch would silently break lookups.
  struct StringHash
  {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
    [[nodiscard]] std::size_t operator()(const std::string& text) const { return (*this)(std::string_view(text)); }
  };

  /// @brief Action id -> runtime, with transparent lookup.
  using ActionMap = std::unordered_map<std::string, ActionRuntime, StringHash, std::equal_to<>>;

  /// @brief Pack one axis source into one key (one key per pad axis, as in the reference).
  [[nodiscard]] static std::uint64_t AxisSourceKey(std::uint32_t padID, std::uint32_t axis, AxisDirection direction)
  {
    return (static_cast<std::uint64_t>(padID) << 32) | (static_cast<std::uint64_t>(axis) << 1) |
           static_cast<std::uint64_t>(direction);
  }

  /// @brief Pack one button source into one key (same (pad, index) shape as
  /// AxisSourceKey; the two key spaces never share a container).
  [[nodiscard]] static std::uint64_t ButtonSourceKey(std::uint32_t padID, std::uint32_t button)
  {
    return (static_cast<std::uint64_t>(padID) << 32) | static_cast<std::uint64_t>(button);
  }

  /// @brief Extract the pad id out of a packed source key (as in the reference); used when a pad is unplugged.
  [[nodiscard]] static std::uint32_t PadOfSourceKey(std::uint64_t key) { return static_cast<std::uint32_t>(key >> 32); }

  /// @brief Largest-magnitude value in one action's source map (0 when empty).
  [[nodiscard]] static float CombineAxis(const ActionRuntime& runtime);

  [[nodiscard]] ActionRuntime& Runtime(const std::string& actionID);
  [[nodiscard]] ActionRuntime* FindRuntime(std::string_view actionID);
  /// @brief Source key for a press that has no trackable device source (a
  /// keyboard key: unplugging a pad must not release it).
  static constexpr std::uint64_t NoSource = ~std::uint64_t{0};

  void TriggerPress(std::string_view actionID, std::uint64_t sourceKey = NoSource);
  void TriggerRelease(std::string_view actionID);
  /// @brief Record/replace one axis source's contribution to an action, then
  /// re-derive the action's press/hold/release state from the combined value.
  void ApplyAxisSource(std::string_view actionID, std::uint64_t sourceKey, float value);
  void ApplyAxisValue(std::string_view actionID, float value);
  [[nodiscard]] bool InputAllowed() const { return focused_ || receiveOnInactive_; }

  DeadzonePair menuDz_ = DefaultMenuDeadzone;         ///< Menu/UI region.
  DeadzonePair movementDz_ = DefaultMovementDeadzone; ///< Movement region.
  SteamInputGuard guard_ = SteamInputGuard::IgnoreWhenGamepadUsed;
  bool receiveOnInactive_ = false;

  std::vector<InputBinding> bindings_; ///< Active binding table.
  /// @brief Indices into `Bindings`, grouped by kind for the hot path. Indices
  /// rather than pointers: replacing the table cannot dangle an index.
  /// @{
  std::vector<std::uint32_t> axisBindings_;
  std::vector<std::uint32_t> keyBindings_;
  std::vector<std::uint32_t> scanBindings_;
  std::vector<std::uint32_t> buttonBindings_;
  /// @}

  ActionMap actions_;                         ///< Action id -> state.
  std::vector<std::string> pressedThisFrame_; ///< Action ids, frame scope.
  std::vector<std::string> repeatThisFrame_;  ///< Action ids, frame scope.

  std::vector<std::uint64_t> axesHeld_;    ///< Held (pad, axis) pairs.
  std::vector<std::uint32_t> ignoredKeys_; ///< Guard-suppressed keys.
  /// @brief Held (pad, button) pairs rather than a bare count: releasing a
  /// whole pad needs to know which buttons *it* was holding, and a set cannot
  /// drift negative the way an event counter can.
  std::vector<std::uint64_t> buttonsHeld_;

  std::vector<std::pair<std::uint32_t, std::string>> gamepads_; ///< (id, name).
  int lastPad_ = -1;                                            ///< Last pad that gave input.
  InputClass lastClass_ = InputClass::Keyboard;                 ///< Prompt-style class.
  bool focused_ = true;                                         ///< Focus state this frame.
  std::uint64_t frameNs_ = 0;                                   ///< Time latched at BeginFrame.
};

} // namespace lod