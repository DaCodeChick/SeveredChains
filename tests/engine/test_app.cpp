#include "game/game.h"
#include "lod/app.h"
#include "sc_test.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{

/// Deterministic clock: time advances only when the loop asks to sleep.
class FakeClock final : public lod::Clock
{
public:
  std::chrono::steady_clock::time_point Now() const override { return virtual_; }
  void Sleep(std::chrono::microseconds Us) const override { virtual_ += Us; }

private:
  mutable std::chrono::steady_clock::time_point virtual_{};
};

/// Records lifecycle events into a shared vector.
class SpySubsystem final : public lod::Subsystem
{
public:
  SpySubsystem(std::string name, std::vector<std::string>& trace, lod::Status failWith)
      : subName_(std::move(name)), events_(trace), fail_(FailOrDefault(failWith))
  {
  }

  std::string_view Name() const override { return subName_; }

  lod::StatusResult Initialize(lod::AppContext&) override
  {
    if (fail_.has_value())
      return std::unexpected(*fail_);
    events_.push_back("init:" + subName_);
    return {};
  }

  void Shutdown() override { events_.push_back("shutdown:" + subName_); }

private:
  static std::optional<lod::Status> FailOrDefault(lod::Status S)
  {
    if (S == lod::Status::Ok)
      return std::nullopt;
    return S;
  }

  std::string subName_;
  std::vector<std::string>& events_;
  std::optional<lod::Status> fail_;
};

std::string TempDir(const char* Tag)
{
  auto dir = std::filesystem::temp_directory_path() / ("lodsc-app-test-" + std::string(Tag));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  return dir.string();
}

} // namespace

// testing.md §2.1: init/deinit round-trip; deinit idempotent.
LOD_TEST(app, lifecycle_roundtrip)
{
  std::vector<std::string> events;
  game::GameApp app(TempDir("roundtrip"), std::make_unique<FakeClock>());
  app.AddSubsystem(std::make_unique<SpySubsystem>("a", events, lod::Status::Ok));
  app.AddSubsystem(std::make_unique<SpySubsystem>("b", events, lod::Status::Ok));

  LOD_REQUIRE(app.Initialize().has_value());
  app.Shutdown();
  app.Shutdown(); // idempotent: no double shutdown

  LOD_REQUIRE(events.size() == 4);
  LOD_CHECK(events[0] == "init:a");
  LOD_CHECK(events[1] == "init:b");
  LOD_CHECK(events[2] == "shutdown:b"); // reverse order
  LOD_CHECK(events[3] == "shutdown:a");
}

// testing.md §2.1: partial init failure → clean teardown + correct Status.
LOD_TEST(app, lifecycle_partial_failure)
{
  std::vector<std::string> events;
  game::GameApp app(TempDir("partial"), std::make_unique<FakeClock>());
  app.AddSubsystem(std::make_unique<SpySubsystem>("ok1", events, lod::Status::Ok));
  app.AddSubsystem(std::make_unique<SpySubsystem>("fails", events, lod::Status::NotFound));
  app.AddSubsystem(std::make_unique<SpySubsystem>("never", events, lod::Status::Ok));

  auto r = app.Initialize();
  LOD_REQUIRE(!r.has_value());
  LOD_CHECK(r.error() == lod::Status::NotFound);

  // Rollback: only initialized subsystems shut down, in reverse order; the
  // failing and later subsystems never shut down.
  LOD_REQUIRE(events.size() == 2);
  LOD_CHECK(events[0] == "init:ok1");
  LOD_CHECK(events[1] == "shutdown:ok1");

  // Shutdown on a rolled-back app is safe and adds nothing.
  app.Shutdown();
  LOD_CHECK(events.size() == 2);
}

// Fixed-timestep loop drives ticks deterministically (no wall-clock sleeps).
LOD_TEST(app, loop_tick_count)
{
  game::GameApp app(TempDir("loop"), std::make_unique<FakeClock>());
  LOD_REQUIRE(app.Initialize().has_value());

  lod::RunOptions opts;
  opts.MaxTicks = 10;
  opts.TickHz = 60.0;
  LOD_CHECK(app.RunMainLoop(opts).has_value());
  LOD_CHECK(app.SimFrameCount() == 10);

  app.Shutdown();
}

// Full Run() path: exit code mapping (error-handling.md §5).
LOD_TEST(app, lifecycle_exit_codes)
{
  std::vector<std::string> events;
  {
    game::GameApp app(TempDir("exitok"), std::make_unique<FakeClock>());
    app.AddSubsystem(std::make_unique<SpySubsystem>("a", events, lod::Status::Ok));
    LOD_CHECK(app.Run(lod::RunOptions{}) == lod::ExitCode::Success);
  }
  {
    game::GameApp app(TempDir("exitfail"), std::make_unique<FakeClock>());
    app.AddSubsystem(std::make_unique<SpySubsystem>("bad", events, lod::Status::OutOfMemory));
    LOD_CHECK(app.Run(lod::RunOptions{}) == lod::ExitCode::InitFailed);
  }
}
