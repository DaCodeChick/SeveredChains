/// @file platform.cpp
/// @brief SDL3 platform/window implementation. This TU is the engine's only
/// SDL *video* interop boundary (cpp.md §9.1): SDL types and C strings are
/// converted to project types here and never leak upward.
#include "lod/platform.h"

#include "lod/diagnostics.h"
#include "lod/log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace lod
{
namespace
{

constexpr std::string_view Marker = "platform";

/// @brief Smallest window SDL3 accepts for a normal (non-popup) window.
/// Smaller requests are clamped instead of failing creation, so a bad config
/// value degrades to a usable window rather than a boot failure.
constexpr int SDLMinWindowSize = 3;

/// @brief Convert an SDL C string to std::string ("unknown" for null).
std::string SDLText(const char* Text)
{
  return Text != nullptr ? std::string(Text) : std::string("unknown");
}

/// @brief List the video drivers this SDL build knows about, for a failure
/// message that says what *was* available (error-handling.md §4: the failing
/// site reports the domain detail, not just a Status).
std::string VideoDriverList()
{
  std::string list;
  const int count = SDL_GetNumVideoDrivers();
  for (int i = 0; i < count; ++i)
  {
    if (!list.empty())
      list += ", ";
    list += SDLText(SDL_GetVideoDriver(i));
  }
  return list.empty() ? "none" : list;
}

} // namespace

void SDLWindowDeleter::operator()(SDL_Window* Window) const
{
  if (Window != nullptr)
    SDL_DestroyWindow(Window);
}

void SDLGamepadDeleter::operator()(SDL_Gamepad* Pad) const
{
  if (Pad != nullptr)
    SDL_CloseGamepad(Pad);
}

Platform::Platform(Options options) : opts_(std::move(options))
{
  // Input configuration is applied once here rather than re-read per frame: a
  // settings change rebuilds the platform (or calls the setters directly).
  inputState_.SetDeadzones(opts_.MenuDeadzone, opts_.MovementDeadzone);
  inputState_.SetSteamInputGuard(opts_.SteamGuard);
  inputState_.SetReceiveInputOnInactiveWindow(opts_.ReceiveInputOnInactive);
}

Platform::~Platform()
{
  Shutdown();
}

bool Platform::HasKeyboardFocus() const
{
  // A headless session has no desktop to lose focus to. Reporting "unfocused"
  // there would silently drop every input event on display-less hosts, so the
  // dummy driver reports "focused" instead.
  if (opts_.Headless)
    return true;
  if (window_ == nullptr)
    return false;
  return SDL_GetKeyboardFocus() == window_.get();
}

StatusResult Platform::Initialize(AppContext& ctx)
{
  log_ = &ctx.Log; // borrowed; cleared by Shutdown (the App outlives us)

  if (window_ != nullptr)
    return {}; // idempotent

  // Clamp here as well as in ValidateConfig: Platform is also constructed
  // directly (tests, tools), and SDL rejects a sub-minimum window.
  opts_.Width = std::max(opts_.Width, SDLMinWindowSize);
  opts_.Height = std::max(opts_.Height, SDLMinWindowSize);

  if (!sDLUp_)
  {
    if (opts_.Headless)
      SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
      sDLUp_ = true;
      gamepadUp_ = true;
    }
    else if (SDL_Init(SDL_INIT_VIDEO))
    {
      // The combined init failed but video alone works, so the gamepad
      // subsystem is what broke (no HID permission, no driver backend).
      // Degrade rather than refuse to start: a controller-less session is
      // usable, a session that will not boot is not. The loss is announced,
      // not swallowed.
      sDLUp_ = true;
      gamepadUp_ = false;
      const std::string detail =
        std::format("SDL_Init(GAMEPAD) failed: '{}' — continuing without controllers", SDLText(SDL_GetError()));
      ctx.Log.Warn(Marker, "{}", detail);
      ctx.Diag.NoteError(detail);
    }
    else
    {
      const std::string detail = std::format("SDL_Init(VIDEO) failed: '{}' (available video drivers: {})",
                                             SDLText(SDL_GetError()), VideoDriverList());
      ctx.Log.Error(Marker, "{}", detail);
      ctx.Diag.NoteError(detail);
      return std::unexpected(Status::Error);
    }
  }

  window_.reset(SDL_CreateWindow(opts_.Title.c_str(), opts_.Width, opts_.Height, SDL_WINDOW_RESIZABLE));
  if (window_ == nullptr)
  {
    const std::string detail =
      std::format("SDL_CreateWindow({}x{}) failed: '{}'", opts_.Width, opts_.Height, SDLText(SDL_GetError()));
    ctx.Log.Error(Marker, "{}", detail);
    ctx.Diag.NoteError(detail);
    Shutdown(); // release the partial acquisition (error-handling.md §3)
    return std::unexpected(Status::Error);
  }

  const char* driver = SDL_GetCurrentVideoDriver();
  ctx.Log.Info(Marker, "window {}x{} ready (driver: {})", opts_.Width, opts_.Height, SDLText(driver));
  return {};
}

void Platform::Shutdown()
{
  pads_.clear();   // closes every open pad through SDLGamepadDeleter; idempotent
  window_.reset(); // deleter is null-safe; idempotent by construction
  inputState_.Reset();
  log_ = nullptr;
  if (sDLUp_)
  {
    SDL_QuitSubSystem(gamepadUp_ ? SDL_INIT_VIDEO | SDL_INIT_GAMEPAD : SDL_INIT_VIDEO);
    sDLUp_ = false;
  }
  gamepadUp_ = false;
}

FrameEvents Platform::PollFrame()
{
  FrameEvents events;
  // One poll is one frame of input: the state machine opens, drains the whole
  // queue into it, and closes with this frame's focus state and clock. Doing
  // the boundary here (rather than in the loop) keeps the SDL boundary in one
  // file and makes the "one frame, one input snapshot" rule structural.
  const std::uint64_t nowNs = SDL_GetTicksNS();
  inputState_.BeginFrame(nowNs);

  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    switch (event.type)
    {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        events.QuitRequested = true;
        break;
      case SDL_EVENT_WINDOW_RESIZED:
        events.Resized = true;
        events.Width = event.window.data1;
        events.Height = event.window.data2;
        break;
      case SDL_EVENT_WINDOW_MINIMIZED:
      case SDL_EVENT_WINDOW_RESTORED:
        // A minimize reports size 0x0; reporting it as a resize would tell the
        // later render layer to allocate a zero-sized target. The state change
        // is carried by focus events instead, so the size change is dropped.
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        events.FocusChanged = true;
        events.Focused = false;
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        events.FocusChanged = true;
        events.Focused = true;
        break;

      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
      {
        const SDL_KeyboardEvent& key = event.key;
        const std::uint32_t code = static_cast<std::uint32_t>(key.key);
        const std::uint32_t scan = static_cast<std::uint32_t>(key.scancode);
        const std::uint32_t mods = static_cast<std::uint32_t>(key.mod);
        if (key.down)
          inputState_.OnKeyDown(code, scan, mods, key.repeat);
        else
          inputState_.OnKeyUp(code, scan, mods);
        break;
      }

      case SDL_EVENT_GAMEPAD_ADDED:
      {
        const std::uint32_t id = static_cast<std::uint32_t>(event.gdevice.which);
        if (pads_.contains(id))
          break; // a remap/re-add for a pad we already track

        // SDL only delivers axis/button events for a pad that has been opened.
        if (SDL_Gamepad* pad = SDL_OpenGamepad(id))
        {
          const std::string name = SDLText(SDL_GetGamepadName(pad));
          pads_.emplace(id, SDLGamepadPtr{pad});
          inputState_.OnGamepadAdded(static_cast<std::uint32_t>(id), name);
          if (log_ != nullptr)
            log_->Info(Marker, "gamepad connected: {}", name);
        }
        else if (log_ != nullptr)
        {
          // Open failure means this pad does nothing at all; say so instead of
          // leaving the player to guess why the controller is dead.
          log_->Warn(Marker, "could not open gamepad id {}: {}", id, SDLText(SDL_GetError()));
        }
        break;
      }

      case SDL_EVENT_GAMEPAD_REMOVED:
      {
        const std::uint32_t id = static_cast<std::uint32_t>(event.gdevice.which);
        const std::size_t closed = pads_.erase(id);
        inputState_.OnGamepadRemoved(static_cast<std::uint32_t>(id));
        if (closed != 0 && log_ != nullptr)
          log_->Info(Marker, "gamepad id {} disconnected", id);
        break;
      }

      case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // Only translate pads we opened. A pad that failed to open (or an
        // event for a device that vanished mid-frame) must not feed the state
        // machine: the reference's Steam Input guard keys off "gamepad input
        // held", so a phantom pad would also mute the keyboard.
        if (pads_.contains(static_cast<std::uint32_t>(event.gaxis.which)))
          inputState_.OnAxis(static_cast<std::uint32_t>(event.gaxis.which),
                             static_cast<std::uint32_t>(event.gaxis.axis), event.gaxis.value);
        break;

      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (pads_.contains(static_cast<std::uint32_t>(event.gbutton.which)))
          inputState_.OnButtonDown(static_cast<std::uint32_t>(event.gbutton.which),
                                   static_cast<std::uint32_t>(event.gbutton.button));
        break;

      case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (pads_.contains(static_cast<std::uint32_t>(event.gbutton.which)))
          inputState_.OnButtonUp(static_cast<std::uint32_t>(event.gbutton.which),
                                 static_cast<std::uint32_t>(event.gbutton.button));
        break;

      case SDL_EVENT_MOUSE_MOTION:
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
      case SDL_EVENT_MOUSE_WHEEL:
        // Classification only: pointer *delivery* waits for a UI consumer
        // (input.h). The class still drives prompt style.
        inputState_.NotePointerInput();
        break;

      default:
        break;
    }
  }

  inputState_.EndFrame(HasKeyboardFocus(), nowNs);
  return events;
}

} // namespace lod
