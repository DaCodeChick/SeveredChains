#pragma once
/// @file platform.h
/// @brief SDL3-backed window/platform subsystem (engine-design.md §2
/// Platform::init). SDL usage is confined to the .cpp; headless mode uses
/// SDL's dummy video driver so the lifecycle works on display-less hosts.

#include "lod/frame.h"
#include "lod/input.h"
#include "lod/status.h"
#include "lod/subsystem.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

// Opaque SDL handles, declared at global scope so they name the *same* types
// the SDL headers declare (a re-declaration inside a namespace would be a
// different type and would not match the SDL function signatures).
struct SDL_Window;
struct SDL_Gamepad;

namespace lod
{

/// @brief Non-owning deleter for an SDL_Window handle (cpp.md §5: a C resource
/// is owned by exactly one RAII type through a unique_ptr with a named
/// deleter). SDL_Window is an opaque C type, so std::make_unique cannot apply;
/// the handle from SDL_CreateWindow is adopted with reset().
struct SDLWindowDeleter
{
  /// @param Window Handle to destroy; null is a no-op (idempotent release).
  void operator()(SDL_Window* Window) const;
};

/// @brief Owning handle for one SDL_Window.
using SDLWindowPtr = std::unique_ptr<SDL_Window, SDLWindowDeleter>;

/// @brief Non-owning deleter for an SDL_Gamepad handle (same RAII pattern as
/// SDLWindowPtr: exactly one owner, released through the SDL call).
struct SDLGamepadDeleter
{
  /// @param Pad Handle to close; null is a no-op.
  void operator()(SDL_Gamepad* Pad) const;
};

/// @brief Owning handle for one opened SDL gamepad.
using SDLGamepadPtr = std::unique_ptr<SDL_Gamepad, SDLGamepadDeleter>;

/// @brief Owns the SDL video + gamepad subsystems, one top-level window and
/// the input state machine fed from the OS event queue.
class Platform final : public Subsystem
{
public:
  struct Options
  {
    bool Headless = true;                                                ///< true = dummy video driver (CI/tests).
    int Width = 1280;                                                    ///< Initial window width (pixels).
    int Height = 720;                                                    ///< Initial window height (pixels).
    std::string Title = "Severed Chains";                                ///< Window title (product name).
    DeadzonePair MenuDeadzone = DefaultMenuDeadzone;                     ///< Menu/UI deadzone region.
    DeadzonePair MovementDeadzone = DefaultMovementDeadzone;             ///< Movement deadzone region.
    SteamInputGuard SteamGuard = SteamInputGuard::IgnoreWhenGamepadUsed; ///< Steam Input workaround.
    bool ReceiveInputOnInactive = false; ///< Deliver input while unfocused (config key).
  };

  explicit Platform(Options options);
  ~Platform() override;

  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  std::string_view Name() const override { return "platform"; }

  /// @brief SDL_Init(VIDEO) + window creation. Idempotent.
  /// On failure the acquired SDL state is released before returning.
  /// @param ctx Logger/crash-context services from App (borrowed for the call).
  /// @return Status::Ok, or Status::Error when SDL init or window creation
  ///         failed (the failing SDL call and its message go to the log and to
  ///         the crash context).
  /// @note Thread: main thread.
  [[nodiscard]] StatusResult Initialize(AppContext& ctx) override;

  /// @brief Destroy window and quit the video subsystem. Idempotent.
  void Shutdown() override;

  /// @brief Drain the event queue into one compact frame snapshot, translating
  /// input events into the InputManager on the way.
  ///
  /// One call is one frame: the input state machine opens (`BeginFrame`),
  /// consumes everything drained from the queue, and closes (`EndFrame`) with
  /// this frame's focus state and clock. Queries made after this call therefore
  /// see a complete frame of input.
  ///
  /// @return Merged events for this poll (quit/resize/focus); the default
  ///         snapshot when the queue is drained or no window exists.
  /// @note Thread: main thread (the SDL event queue is main-thread only).
  [[nodiscard]] FrameEvents PollFrame() override;

  [[nodiscard]] bool HasWindow() const { return window_ != nullptr; }

  /// @brief The input state machine fed by this platform layer.
  ///
  /// Owned here because the OS event queue is drained here; the game and UI
  /// layers read actions through it (input.h: delivery is pull-based).
  ///
  /// @note Thread: main thread (frame loop only).
  [[nodiscard]] InputManager& Input() { return inputState_; }

  /// @copydoc Input()
  [[nodiscard]] const InputManager& Input() const { return inputState_; }

  /// @brief Non-owning SDL window handle for the render layer (cpp.md §9.1
  /// escape hatch). Owned by this Platform; valid between Initialize and
  /// Shutdown, null otherwise.
  [[nodiscard]] SDL_Window* Handle() const { return window_.get(); }

private:
  /// @brief Whether the window currently accepts input.
  ///
  /// Headless sessions have no desktop to lose focus to, so they report
  /// "focused": a dummy-driver host that reported otherwise would silently
  /// disable all input on every CI run. With a real driver, focus means the
  /// window owns the keyboard focus right now.
  [[nodiscard]] bool HasKeyboardFocus() const;

  Options opts_;
  SDLWindowPtr window_;    ///< Unique owner of the SDL_Window (null = none).
  bool sDLUp_ = false;     ///< We hold one SDL init reference.
  bool gamepadUp_ = false; ///< We hold one SDL_INIT_GAMEPAD reference.
  /// @brief Open pads, keyed by SDL_JoystickID (which is an unsigned 32-bit
  /// id, so the key type matches it exactly instead of narrowing through int).
  std::unordered_map<std::uint32_t, SDLGamepadPtr> pads_;
  InputManager inputState_; ///< Input state for this platform.
  Logger* log_ = nullptr;   ///< Borrowed from AppContext (never owned).
};

} // namespace lod
