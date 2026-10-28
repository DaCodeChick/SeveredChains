/// @file app.cpp
/// @brief App lifecycle + fixed-timestep main loop (engine-design.md §2/§3):
/// events → clamped fixed-step sim → pacing sleep, with measured stats.
#include "lod/app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <thread>

namespace lod
{
namespace
{
constexpr std::string_view Marker = "app";

/// @brief Floating-point microsecond unit: the sim step (e.g. 1e6/60) is not an
/// integer number of microseconds, and truncating it every tick would drift the
/// sim clock. Only the *sleep* is rounded to whole microseconds.
using FPMicros = std::chrono::duration<double, std::micro>;

FPMicros ToFPMicros(std::chrono::steady_clock::duration D)
{
  return std::chrono::duration_cast<FPMicros>(D);
}

double Ms(FPMicros D)
{
  return D.count() / 1000.0;
}
} // namespace

std::chrono::steady_clock::time_point SystemClock::Now() const
{
  return std::chrono::steady_clock::now();
}

void SystemClock::Sleep(std::chrono::microseconds Us) const
{
  if (Us.count() > 0)
    std::this_thread::sleep_for(Us);
}

App::App(std::string crashLogDir, std::unique_ptr<Clock> clockImpl)
    : diag_(std::move(crashLogDir))
    , clockOwned_(clockImpl ? std::move(clockImpl) : std::make_unique<SystemClock>())
    , clockRef_(clockOwned_.get())
{
}

App::~App()
{
  Shutdown();
}

void App::AddSubsystem(std::unique_ptr<Subsystem> sub)
{
  subsystems_.push_back(std::move(sub));
}

void App::SetSessionMode(std::string_view mode)
{
  mode_ = std::string(mode);
}

StatusResult App::Initialize()
{
  if (auto r = diag_.Initialize(); !r)
  {
    log_.Error(Marker, "crash-log init failed: {}", StatusName(r.error()));
    return r;
  }
  diagnosticsUp_ = true;
  AppContext ctx{log_, diag_, mode_};
  for (auto& sub : subsystems_)
  {
    log_.Info(Marker, "initializing subsystem '{}'", sub->Name());
    if (auto r = sub->Initialize(ctx); !r)
    {
      log_.Error(Marker, "subsystem '{}' failed: {}", sub->Name(), StatusName(r.error()));
      diag_.NoteError(std::string("subsystem init failed: ") + std::string(sub->Name()));
      // Roll back in reverse order (error-handling.md §3).
      for (std::size_t i = initializedCount_; i-- > 0;)
        subsystems_[i]->Shutdown();
      initializedCount_ = 0;
      diag_.Shutdown();
      diagnosticsUp_ = false;
      return r;
    }
    ++initializedCount_;
  }
  log_.Info(Marker, "all {} subsystem(s) initialized", initializedCount_);
  return {};
}

FrameEvents App::PollFrameEvents()
{
  FrameEvents merged;
  // Only initialized subsystems are polled: an un-initialized one owns no
  // event queue, and Shutdown() must not be followed by polling either.
  for (std::size_t i = 0; i < initializedCount_; ++i)
    MergeFrameEvents(merged, subsystems_[i]->PollFrame());
  return merged;
}

void App::RequestQuit(bool quit)
{
  quitRequested_ = quit;
}

StatusResult App::RunMainLoop(const RunOptions& opts)
{
  if (!std::isfinite(opts.TickHz) || opts.TickHz <= 0.0)
  {
    log_.Error(Marker, "invalid TickHz {}", opts.TickHz);
    return std::unexpected(Status::InvalidArgument);
  }
  if (!std::isfinite(opts.MaxFrameDeltaSeconds) || opts.MaxFrameDeltaSeconds <= 0.0)
  {
    log_.Error(Marker, "invalid MaxFrameDeltaSeconds {}", opts.MaxFrameDeltaSeconds);
    return std::unexpected(Status::InvalidArgument);
  }
  if (opts.MaxTicksPerFrame == 0)
  {
    log_.Error(Marker, "invalid MaxTicksPerFrame 0");
    return std::unexpected(Status::InvalidArgument);
  }

  stats_ = {};

  const FPMicros tickDuration(1'000'000.0 / opts.TickHz);
  const FPMicros maxDelta(opts.MaxFrameDeltaSeconds * 1'000'000.0);

  auto previous = clockRef_->Now();
  FPMicros accumulator{};
  double deltaSum = 0.0;
  std::uint64_t simFrame = 0;

  while (!quitRequested_ && (!opts.MaxTicks || simFrame < *opts.MaxTicks))
  {
    // 1. Platform events. A quit request ends the loop after this frame's
    //    sim ticks, so the frame that produced the event is still simulated
    //    (gameplay-visible input in that event is not dropped).
    const FrameEvents events = PollFrameEvents();
    if (events.QuitRequested)
      quitRequested_ = true;
    if (opts.Verbose && events.Resized)
      log_.Info(Marker, "window resized to {}x{}", events.Width, events.Height);
    if (opts.Verbose && events.FocusChanged)
      log_.Info(Marker, "window focus {}", events.Focused ? "gained" : "lost");

    // 2. Fixed-step simulation with a clamped frame delta.
    const auto now = clockRef_->Now();
    auto delta = ToFPMicros(now - previous);
    previous = now;
    if (delta < FPMicros{})
      delta = FPMicros{}; // a non-monotonic clock must never rewind the sim
    const double rawDeltaMs = Ms(delta);
    stats_.MaxRawDeltaMs = std::max(stats_.MaxRawDeltaMs, rawDeltaMs);
    if (delta > maxDelta)
    {
      ++stats_.ClampedFrames;
      delta = maxDelta;
    }
    if (stats_.Frames == 0)
      stats_.FirstDeltaMs = Ms(delta);
    stats_.LastDeltaMs = Ms(delta);
    stats_.MaxDeltaMs = std::max(stats_.MaxDeltaMs, Ms(delta));
    deltaSum += delta.count();
    accumulator += delta;

    std::uint64_t ticksThisFrame = 0;
    // The tick budget bounds the frame too: a frame must never run past the
    // requested total (a fixed `--ticks N` demo is exact, not N+cap).
    const std::uint64_t budget = opts.MaxTicks ? *opts.MaxTicks - simFrame : std::numeric_limits<std::uint64_t>::max();
    while (ticksThisFrame < opts.MaxTicksPerFrame && ticksThisFrame < budget && accumulator >= tickDuration)
    {
      Tick(simFrame);
      ++simFrame;
      ++ticksThisFrame;
      accumulator -= tickDuration;
    }
    if (ticksThisFrame == opts.MaxTicksPerFrame && accumulator >= tickDuration)
      ++stats_.CappedFrames;
    stats_.Ticks += ticksThisFrame;

    // 3. Pace to the display: sleep until the next tick boundary would be due.
    //    Rounding *up* matters: truncating a sub-microsecond remainder to a
    //    zero-length sleep would make the loop spin without the clock ever
    //    advancing (a virtual clock would never reach the next tick). Ceil
    //    overshoots by at most 1us per frame (< 0.01% of a 60Hz frame).
    const FPMicros remaining = tickDuration - accumulator;
    if (remaining > FPMicros{})
      clockRef_->Sleep(std::chrono::ceil<std::chrono::microseconds>(remaining));
    ++stats_.Frames;
  }

  stats_.AvgDeltaMs = stats_.Frames > 0 ? (deltaSum / static_cast<double>(stats_.Frames)) / 1000.0 : 0.0;
  const double elapsedSeconds = deltaSum / 1'000'000.0;
  stats_.SimHz = elapsedSeconds > 0.0 ? static_cast<double>(stats_.Ticks) / elapsedSeconds : 0.0;
  if (opts.Verbose)
  {
    log_.Info(Marker,
              "main loop completed {} sim tick(s) in {} frame(s) (sim {:.2f}Hz, accepted frame delta first {:.2f}ms "
              "last {:.2f}ms max {:.2f}ms avg {:.2f}ms, raw max {:.2f}ms, {} clamped, {} capped)",
              stats_.Ticks, stats_.Frames, stats_.SimHz, stats_.FirstDeltaMs, stats_.LastDeltaMs, stats_.MaxDeltaMs,
              stats_.AvgDeltaMs, stats_.MaxRawDeltaMs, stats_.ClampedFrames, stats_.CappedFrames);
  }
  return {};
}

void App::Tick(std::uint64_t /*simFrame*/)
{
  // Foundation milestone: no game logic. The game layer overrides this hook.
}

void App::Shutdown()
{
  for (std::size_t i = initializedCount_; i-- > 0;)
  {
    log_.Info(Marker, "shutting down subsystem '{}'", subsystems_[i]->Name());
    subsystems_[i]->Shutdown();
  }
  initializedCount_ = 0;
  if (diagnosticsUp_)
  {
    diag_.Shutdown();
    diagnosticsUp_ = false;
  }
}

ExitCode App::Run(const RunOptions& opts)
{
  if (auto r = Initialize(); !r)
    return ExitCode::InitFailed;
  ExitCode code = ExitCode::Success;
  if (auto r = RunMainLoop(opts); !r)
  {
    log_.Error(Marker, "main loop failed: {}", StatusName(r.error()));
    code = ExitCode::RuntimeError;
  }
  Shutdown();
  return code;
}

} // namespace lod
