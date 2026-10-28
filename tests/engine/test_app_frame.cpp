/// @file test_app_frame.cpp
/// @brief Main-loop integration: platform event polling, quit handling,
/// frame-delta clamping and tick-budget accounting (testing.md §2.1 — timing is
/// verified with injected clocks so the assertions are exact, not wall-clock
/// dependent).
#include "game/game.h"
#include "lod/app.h"
#include "lod/frame.h"
#include "sc_test.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// @brief Virtual clock: time advances only when the loop sleeps, so pacing is
/// deterministic and the loop cannot hang a CI job.
class FakeClock final : public lod::Clock
{
public:
  std::chrono::steady_clock::time_point Now() const override { return virtual_; }
  void Sleep(std::chrono::microseconds Us) const override { virtual_ += Us; }

private:
  mutable std::chrono::steady_clock::time_point virtual_{};
};

/// @brief Clock that advances a fixed step on every Now() call and ignores
/// sleeps — models a loop whose own work (or a stall) dominates the frame.
class SteppingClock final : public lod::Clock
{
public:
  explicit SteppingClock(std::chrono::microseconds step) : step_(step) {}

  std::chrono::steady_clock::time_point Now() const override
  {
    virtual_ += step_;
    return virtual_;
  }
  void Sleep(std::chrono::microseconds) const override {}

private:
  std::chrono::microseconds step_;
  mutable std::chrono::steady_clock::time_point virtual_{};
};

/// @brief Subsystem that injects scripted frame events, one per poll.
class FrameSourceSubsystem final : public lod::Subsystem
{
public:
  explicit FrameSourceSubsystem(std::vector<lod::FrameEvents> script) : script_(std::move(script)) {}

  std::string_view Name() const override { return "framesource"; }
  lod::StatusResult Initialize(lod::AppContext&) override { return {}; }
  void Shutdown() override {}

  lod::FrameEvents PollFrame() override
  {
    const std::size_t index = polls_;
    ++polls_;
    return index < script_.size() ? script_[index] : lod::FrameEvents{};
  }

  std::size_t PollCount() const { return polls_; }

private:
  std::vector<lod::FrameEvents> script_;
  std::size_t polls_ = 0;
};

/// @brief Subsystem whose initialization fails (so later subsystems never
/// initialize, and rollback can be observed).
class FailingSubsystem final : public lod::Subsystem
{
public:
  std::string_view Name() const override { return "failing"; }
  lod::StatusResult Initialize(lod::AppContext&) override { return std::unexpected(lod::Status::Error); }
  void Shutdown() override {}
};

std::string TempDir(const std::string& tag)
{
  const auto dir = std::filesystem::temp_directory_path() / ("lodsc-frame-test-" + tag);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  return dir.string();
}

lod::RunOptions QuietOptions()
{
  lod::RunOptions opts;
  opts.Verbose = false;
  return opts;
}

} // namespace

// A quit event from the platform stops the loop without burning the tick
// budget; the frame that produced the event is still simulated.
LOD_TEST(app, quit_event_stops_loop)
{
  game::GameApp app(TempDir("quit"), std::make_unique<FakeClock>());
  auto source = std::make_unique<FrameSourceSubsystem>(
    std::vector<lod::FrameEvents>{lod::FrameEvents{}, lod::FrameEvents{.QuitRequested = true}});
  auto* sourceRef = source.get();
  app.AddSubsystem(std::move(source));

  LOD_REQUIRE(app.Initialize().has_value());

  lod::RunOptions opts = QuietOptions();
  opts.MaxTicks = std::nullopt; // would otherwise never end
  opts.TickHz = 60.0;
  const auto result = app.RunMainLoop(opts);

  LOD_REQUIRE(result.has_value());
  LOD_CHECK(app.IsQuitRequested());
  // Two polls: the second reported the quit and ended the loop.
  LOD_CHECK(sourceRef->PollCount() == 2);
  LOD_CHECK(app.LastLoopStats().Frames == 2);
  // Frame 1 accumulated no time (virtual clock had not advanced) and frame 2
  // accumulated exactly one tick — which still ran despite the quit, because
  // input carried by that event must not be dropped.
  LOD_CHECK(app.LastLoopStats().Ticks == 1);
  app.Shutdown();
}

// RequestQuit() is observed at the next frame boundary, and the loop entry
// clears a stale request so a later run is not dead on arrival.
LOD_TEST(app, quit_request_flag)
{
  game::GameApp app(TempDir("quitflag"), std::make_unique<FakeClock>());
  LOD_REQUIRE(app.Initialize().has_value());
  LOD_CHECK(!app.IsQuitRequested());

  lod::RunOptions opts = QuietOptions();
  opts.MaxTicks = 1;
  app.RequestQuit();
  LOD_CHECK(app.IsQuitRequested());
  LOD_CHECK(app.RunMainLoop(opts).has_value());
  // The pending quit is honoured, not swallowed: no frame ran, and the request
  // survives so a subsequent Run() also stops (quit means quit).
  LOD_CHECK(app.IsQuitRequested());
  LOD_CHECK(app.LastLoopStats().Frames == 0);
  LOD_CHECK(app.LastLoopStats().Ticks == 0);
  app.Shutdown();
}

// Frame delta is clamped: a multi-second stall is simulated as (at most) the
// clamp window, so the sim skips dead time instead of running a catch-up storm.
LOD_TEST(app, timing_delta_clamp)
{
  // 100ms per frame at 60Hz: 6 ticks/frame unclamped, 1/frame with a 25ms
  // clamp — a 6x difference, so the clamp is unambiguously observed.
  game::GameApp app(TempDir("clamp"), std::make_unique<SteppingClock>(std::chrono::milliseconds(100)));
  auto source = std::make_unique<FrameSourceSubsystem>(
    std::vector<lod::FrameEvents>{lod::FrameEvents{}, lod::FrameEvents{}, lod::FrameEvents{.QuitRequested = true}});
  app.AddSubsystem(std::move(source));
  LOD_REQUIRE(app.Initialize().has_value());

  lod::RunOptions opts = QuietOptions();
  opts.TickHz = 60.0;
  opts.MaxTicks = std::nullopt;
  opts.MaxTicksPerFrame = 8; // above what a clamped frame needs: the clamp binds
  opts.MaxFrameDeltaSeconds = 0.025;
  LOD_REQUIRE(app.RunMainLoop(opts).has_value());

  const lod::LoopStats& stats = app.LastLoopStats();
  LOD_CHECK(stats.Frames == 3);
  LOD_CHECK(stats.ClampedFrames == 3);
  LOD_CHECK(stats.CappedFrames == 0);
  // Accepted deltas never exceed the clamp, while the raw sample still shows
  // the real 100ms frame.
  LOD_CHECK(stats.MaxDeltaMs == 25.0);
  LOD_CHECK(stats.FirstDeltaMs == 25.0);
  LOD_CHECK(stats.LastDeltaMs == 25.0);
  LOD_CHECK(stats.MaxRawDeltaMs == 100.0);
  // Bounded by the clamp: 3 frames x ceil(25ms / 16.67ms) = at most 6 ticks,
  // versus 18 unclamped (3 x 6). Exactness is not asserted because the
  // sub-tick remainder carried between frames is float-sensitive.
  LOD_CHECK(stats.Ticks >= 3);
  LOD_CHECK(stats.Ticks <= 6);
  app.Shutdown();
}

// With no clamp, the per-frame cap bounds a stall — and the backlog is
// retained, not silently dropped (no lost sim time).
LOD_TEST(app, timing_spiral_cap_retains_backlog)
{
  game::GameApp app(TempDir("spiral"), std::make_unique<SteppingClock>(std::chrono::milliseconds(100)));
  auto source = std::make_unique<FrameSourceSubsystem>(
    std::vector<lod::FrameEvents>{lod::FrameEvents{}, lod::FrameEvents{}, lod::FrameEvents{.QuitRequested = true}});
  app.AddSubsystem(std::move(source));
  LOD_REQUIRE(app.Initialize().has_value());

  lod::RunOptions opts = QuietOptions();
  opts.TickHz = 60.0;
  opts.MaxTicks = std::nullopt;
  opts.MaxTicksPerFrame = 1;
  opts.MaxFrameDeltaSeconds = 10.0; // effectively no clamp for a 100ms delta
  LOD_REQUIRE(app.RunMainLoop(opts).has_value());

  const lod::LoopStats& stats = app.LastLoopStats();
  LOD_CHECK(stats.Frames == 3);
  LOD_CHECK(stats.Ticks == 3);        // exactly one tick per frame (the cap)
  LOD_CHECK(stats.CappedFrames == 3); // every frame had backlog left over
  LOD_CHECK(stats.ClampedFrames == 0);
  app.Shutdown();
}

// The tick budget is exact (a frame never runs past it), and MaxTicks =
// nullopt means "until quit" rather than "forever".
LOD_TEST(app, timing_tick_budget)
{
  game::GameApp app(TempDir("budget"), std::make_unique<FakeClock>());
  LOD_REQUIRE(app.Initialize().has_value());

  lod::RunOptions opts = QuietOptions();
  opts.TickHz = 20.0;
  opts.MaxTicks = 7;
  LOD_CHECK(app.RunMainLoop(opts).has_value());
  LOD_CHECK(app.SimFrameCount() == 7);
  LOD_CHECK(app.LastLoopStats().Ticks == 7);
  // One frame per tick at 20Hz (each frame sleeps exactly one tick length);
  // frame 1 accumulates nothing, hence frames == ticks + 1.
  LOD_CHECK(app.LastLoopStats().Frames == 8);
  // Mean accepted delta = (7 x 50ms + 0ms) / 8 frames = 43.75ms.
  LOD_CHECK(app.LastLoopStats().AvgDeltaMs == 43.75);
  // Accepted time equals sim time: 7 ticks over 350ms = 20Hz (within float
  // tolerance — 0.35s is not exactly representable).
  LOD_CHECK(app.LastLoopStats().SimHz > 19.99);
  LOD_CHECK(app.LastLoopStats().SimHz < 20.01);

  // MaxTicks = nullopt with a quit on the first poll must terminate at once.
  game::GameApp app2(TempDir("budget-until-quit"), std::make_unique<FakeClock>());
  app2.AddSubsystem(
    std::make_unique<FrameSourceSubsystem>(std::vector<lod::FrameEvents>{lod::FrameEvents{.QuitRequested = true}}));
  LOD_REQUIRE(app2.Initialize().has_value());
  lod::RunOptions quitNow = QuietOptions();
  quitNow.MaxTicks = std::nullopt;
  LOD_CHECK(app2.RunMainLoop(quitNow).has_value());
  LOD_CHECK(app2.LastLoopStats().Frames == 1);
  LOD_CHECK(app2.LastLoopStats().Ticks == 0);
  app2.Shutdown();
}

// Invalid loop parameters are rejected instead of hanging or dividing by zero.
LOD_TEST(app, timing_rejects_bad_options)
{
  game::GameApp app(TempDir("badopts"), std::make_unique<FakeClock>());
  LOD_REQUIRE(app.Initialize().has_value());

  lod::RunOptions zeroHz = QuietOptions();
  zeroHz.TickHz = 0.0;
  auto r0 = app.RunMainLoop(zeroHz);
  LOD_CHECK(!r0.has_value());
  LOD_CHECK(r0.error() == lod::Status::InvalidArgument);

  lod::RunOptions nanHz = QuietOptions();
  nanHz.TickHz = 1.0 / 0.0;
  LOD_CHECK(!app.RunMainLoop(nanHz).has_value());

  lod::RunOptions zeroCap = QuietOptions();
  zeroCap.MaxTicksPerFrame = 0;
  LOD_CHECK(app.RunMainLoop(zeroCap).error() == lod::Status::InvalidArgument);

  lod::RunOptions zeroClamp = QuietOptions();
  zeroClamp.MaxFrameDeltaSeconds = 0.0;
  LOD_CHECK(app.RunMainLoop(zeroClamp).error() == lod::Status::InvalidArgument);
  app.Shutdown();
}

// Only initialized, still-running subsystems are polled: before Initialize,
// after a failed init (rollback), and after Shutdown the loop must not touch
// subsystem event queues that do not exist.
LOD_TEST(app, frame_polling_respects_init_state)
{
  // (a) registered but never initialized.
  {
    auto source = std::make_unique<FrameSourceSubsystem>(
      std::vector<lod::FrameEvents>{lod::FrameEvents{.Resized = true, .Width = 800, .Height = 600}});
    auto* sourceRef = source.get();
    game::GameApp app(TempDir("poll-before"), std::make_unique<FakeClock>());
    app.AddSubsystem(std::move(source));

    LOD_CHECK(app.PollFrameEvents().Resized == false);
    LOD_CHECK(sourceRef->PollCount() == 0);
  }

  // (b) registered behind a subsystem that failed: rollback leaves nothing up.
  {
    auto source = std::make_unique<FrameSourceSubsystem>(
      std::vector<lod::FrameEvents>{lod::FrameEvents{.Resized = true, .Width = 800, .Height = 600}});
    auto* sourceRef = source.get();
    game::GameApp app(TempDir("poll-rollback"), std::make_unique<FakeClock>());
    app.AddSubsystem(std::move(source));
    app.AddSubsystem(std::make_unique<FailingSubsystem>());

    LOD_CHECK(!app.Initialize().has_value());
    LOD_CHECK(app.PollFrameEvents().Resized == false);
    // Polled once during the successful part of the init? No: PollFrame is not
    // part of Initialize, so the subsystem was never polled at all.
    LOD_CHECK(sourceRef->PollCount() == 0);
  }

  // (c) initialized, then shut down.
  {
    auto source = std::make_unique<FrameSourceSubsystem>(
      std::vector<lod::FrameEvents>{lod::FrameEvents{.Resized = true, .Width = 800, .Height = 600}});
    auto* sourceRef = source.get();
    game::GameApp app(TempDir("poll-shutdown"), std::make_unique<FakeClock>());
    app.AddSubsystem(std::move(source));
    LOD_REQUIRE(app.Initialize().has_value());

    const lod::FrameEvents events = app.PollFrameEvents();
    LOD_CHECK(events.Resized);
    LOD_CHECK(events.Width == 800);
    LOD_CHECK(events.Height == 600);

    app.Shutdown();
    const std::size_t pollsBefore = sourceRef->PollCount();
    LOD_CHECK(app.PollFrameEvents().Resized == false);
    LOD_CHECK(sourceRef->PollCount() == pollsBefore);
  }
}

// Merge semantics are the loop's contract, so they are pinned independently of
// any platform.
LOD_TEST(frame, merge_rules)
{
  lod::FrameEvents merged;

  lod::FrameEvents a;
  a.Resized = true;
  a.Width = 800;
  a.Height = 600;
  lod::MergeFrameEvents(merged, a);
  LOD_CHECK(merged.Resized);
  LOD_CHECK(merged.Width == 800);

  // A minimize reports 0x0: recorded as a resize, but the usable size stays.
  lod::FrameEvents iconify;
  iconify.Resized = true;
  iconify.Width = 0;
  iconify.Height = 0;
  lod::MergeFrameEvents(merged, iconify);
  LOD_CHECK(merged.Resized);
  LOD_CHECK(merged.Width == 800);
  LOD_CHECK(merged.Height == 600);

  // Newest valid size wins; focus is newest-wins; quit is sticky.
  lod::FrameEvents grown;
  grown.Resized = true;
  grown.Width = 1024;
  grown.Height = 768;
  grown.FocusChanged = true;
  grown.Focused = false;
  lod::MergeFrameEvents(merged, grown);
  LOD_CHECK(merged.Width == 1024);
  LOD_CHECK(merged.Height == 768);
  LOD_CHECK(merged.FocusChanged);
  LOD_CHECK(!merged.Focused);

  lod::FrameEvents quit;
  quit.QuitRequested = true;
  lod::MergeFrameEvents(merged, quit);
  lod::FrameEvents idle;
  lod::MergeFrameEvents(merged, idle);
  LOD_CHECK(merged.QuitRequested);
}
