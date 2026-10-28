/// @file test_platform.cpp
/// @brief Platform subsystem lifecycle and event translation (testing.md §2.1).
/// SDL events are injected with SDL_PushEvent — the only way to drive the real
/// queue without a window manager — so this TU is a deliberate SDL interop
/// boundary in the test layer too (cpp.md §9.1). Everything runs on the dummy
/// video driver: no display is required and no window ever appears.
#include "game/game.h"
#include "lod/app.h"
#include "lod/diagnostics.h"
#include "lod/log.h"
#include "lod/platform.h"
#include "sc_test.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace
{

std::string TempDir(const std::string& tag)
{
  const auto dir = std::filesystem::temp_directory_path() / ("lodsc-platform-test-" + tag);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  return dir.string();
}

/// @brief Logger + Diagnostics stand-ins so Platform can be driven without a
/// full App (what App would hand it at Initialize).
class PlatformFixture
{
public:
  explicit PlatformFixture(const std::string& tag) : diag_(TempDir(tag)), Ctx{log_, diag_, "headless"}
  {
    log_.SetMinLevel(lod::Level::Error); // keep test output clean
  }

  PlatformFixture(const PlatformFixture&) = delete;
  PlatformFixture& operator=(const PlatformFixture&) = delete;

private:
  lod::Logger log_;
  lod::Diagnostics diag_;

public:
  lod::AppContext Ctx; ///< Exactly what App hands a subsystem (declared last:
                       ///< it references the members above).
};

/// @brief Headless Platform options (dummy driver; CI-safe).
lod::Platform::Options HeadlessOptions()
{
  lod::Platform::Options options;
  options.Headless = true;
  options.Width = 640;
  options.Height = 480;
  options.Title = "lodsc test";
  return options;
}

/// @brief Push one window-scoped event onto the SDL queue.
void PushWindowEvent(SDL_EventType type, int data1 = 0, int data2 = 0)
{
  SDL_Event event{};
  event.window.type = type;
  event.window.data1 = data1;
  event.window.data2 = data2;
  SDL_PushEvent(&event);
}

void PushSimpleEvent(SDL_EventType type)
{
  SDL_Event event{};
  event.type = type;
  SDL_PushEvent(&event);
}

/// @brief Clock that advances on its own, so a loop that never sleeps still
/// terminates deterministically.
class AutoClock final : public lod::Clock
{
public:
  std::chrono::steady_clock::time_point Now() const override
  {
    virtual_ += std::chrono::milliseconds(20);
    return virtual_;
  }
  void Sleep(std::chrono::microseconds) const override {}

private:
  mutable std::chrono::steady_clock::time_point virtual_{};
};

} // namespace

LOD_TEST(platform, init_shutdown_is_idempotent)
{
  PlatformFixture fixture("idem-crash");
  lod::Platform platform(HeadlessOptions());

  LOD_CHECK(!platform.HasWindow());
  LOD_CHECK(platform.Handle() == nullptr);

  LOD_REQUIRE(platform.Initialize(fixture.Ctx).has_value());
  LOD_CHECK(platform.HasWindow());
  LOD_CHECK(platform.Handle() != nullptr);
  // Idempotent: a second Initialize is a no-op, not a second window.
  SDL_Window* first = platform.Handle();
  LOD_CHECK(platform.Initialize(fixture.Ctx).has_value());
  LOD_CHECK(platform.Handle() == first);

  platform.Shutdown();
  platform.Shutdown(); // idempotent
  LOD_CHECK(!platform.HasWindow());
  LOD_CHECK(platform.Handle() == nullptr);

  // Re-initialize after shutdown works (the video subsystem is re-acquirable).
  LOD_CHECK(platform.Initialize(fixture.Ctx).has_value());
  LOD_CHECK(platform.HasWindow());
  platform.Shutdown();
}

LOD_TEST(platform, destructor_releases_window)
{
  PlatformFixture fixture("dtor-crash");
  {
    lod::Platform platform(HeadlessOptions());
    LOD_REQUIRE(platform.Initialize(fixture.Ctx).has_value());
  } // ~Platform must release the window *and* the video subsystem
  LOD_CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
}

LOD_TEST(platform, bad_window_size_is_clamped_not_rejected)
{
  PlatformFixture fixture("clamps-crash");
  lod::Platform::Options options = HeadlessOptions();
  options.Width = 0;
  options.Height = -5;

  lod::Platform platform(options);
  const auto result = platform.Initialize(fixture.Ctx);
  LOD_CHECK(result.has_value());
  LOD_CHECK(platform.HasWindow());
  if (platform.HasWindow())
  {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(platform.Handle(), &width, &height);
    LOD_CHECK(width >= 1);
    LOD_CHECK(height >= 1);
  }
  platform.Shutdown();
}

LOD_TEST(platform, poll_reports_no_events_when_idle)
{
  PlatformFixture fixture("idle-crash");
  lod::Platform platform(HeadlessOptions());
  LOD_REQUIRE(platform.Initialize(fixture.Ctx).has_value());

  // Window creation can deliver a focus event; drain anything pending, then
  // assert an empty queue yields the default snapshot.
  (void)platform.PollFrame();
  const lod::FrameEvents events = platform.PollFrame();
  LOD_CHECK(!events.QuitRequested);
  LOD_CHECK(!events.Resized);
  LOD_CHECK(!events.FocusChanged);

  platform.Shutdown();
}

LOD_TEST(platform, quit_and_close_events_map_to_quit)
{
  PlatformFixture fixture("quit-crash");
  lod::Platform platform(HeadlessOptions());
  LOD_REQUIRE(platform.Initialize(fixture.Ctx).has_value());
  (void)platform.PollFrame(); // drain creation events

  PushSimpleEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
  LOD_CHECK(platform.PollFrame().QuitRequested);

  // Events are drained, not sticky: the next poll starts clean.
  LOD_CHECK(!platform.PollFrame().QuitRequested);

  PushSimpleEvent(SDL_EVENT_QUIT);
  LOD_CHECK(platform.PollFrame().QuitRequested);

  platform.Shutdown();
}

LOD_TEST(platform, resize_focus_and_iconify_events)
{
  PlatformFixture fixture("resize-crash");
  lod::Platform platform(HeadlessOptions());
  LOD_REQUIRE(platform.Initialize(fixture.Ctx).has_value());
  (void)platform.PollFrame();

  PushWindowEvent(SDL_EVENT_WINDOW_RESIZED, 1024, 768);
  const lod::FrameEvents resized = platform.PollFrame();
  LOD_CHECK(resized.Resized);
  LOD_CHECK(resized.Width == 1024);
  LOD_CHECK(resized.Height == 768);

  // Multiple resizes in one frame collapse to the newest size.
  PushWindowEvent(SDL_EVENT_WINDOW_RESIZED, 800, 600);
  PushWindowEvent(SDL_EVENT_WINDOW_RESIZED, 900, 700);
  const lod::FrameEvents collapsed = platform.PollFrame();
  LOD_CHECK(collapsed.Resized);
  LOD_CHECK(collapsed.Width == 900);
  LOD_CHECK(collapsed.Height == 700);

  // An iconify (SDL reports size 0x0) must not look like a usable resize.
  PushWindowEvent(SDL_EVENT_WINDOW_MINIMIZED, 0, 0);
  const lod::FrameEvents iconified = platform.PollFrame();
  LOD_CHECK(!iconified.Resized);

  PushSimpleEvent(SDL_EVENT_WINDOW_FOCUS_LOST);
  const lod::FrameEvents lost = platform.PollFrame();
  LOD_CHECK(lost.FocusChanged);
  LOD_CHECK(!lost.Focused);

  PushSimpleEvent(SDL_EVENT_WINDOW_FOCUS_GAINED);
  const lod::FrameEvents gained = platform.PollFrame();
  LOD_CHECK(gained.FocusChanged);
  LOD_CHECK(gained.Focused);

  platform.Shutdown();
}

// The loop must terminate when the platform reports a quit even with no tick
// budget — this is the window-close path a user actually hits.
LOD_TEST(platform, quit_event_terminates_app_loop)
{
  game::GameApp app(TempDir("loopquit"), std::make_unique<AutoClock>());
  auto platform = std::make_unique<lod::Platform>(HeadlessOptions());
  auto* platformRef = platform.get();
  app.AddSubsystem(std::move(platform));
  LOD_REQUIRE(app.Initialize().has_value());
  LOD_CHECK(platformRef->HasWindow());

  PushSimpleEvent(SDL_EVENT_QUIT);

  lod::RunOptions opts;
  opts.MaxTicks = std::nullopt; // run until quit
  opts.Verbose = false;
  const auto result = app.RunMainLoop(opts);
  LOD_REQUIRE(result.has_value());
  LOD_CHECK(app.IsQuitRequested());
  LOD_CHECK(app.LastLoopStats().Frames == 1);

  app.Shutdown();
  LOD_CHECK(!platformRef->HasWindow());
  LOD_CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
}
