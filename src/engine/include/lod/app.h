#pragma once
/// @file app.h
/// @brief App — process lifecycle owner (engine-design.md §2): subsystem
/// registry with ordered init + rollback, fixed-timestep main loop, orderly
/// shutdown. No singleton: main() constructs and owns it.

#include "lod/diagnostics.h"
#include "lod/frame.h"
#include "lod/log.h"
#include "lod/status.h"
#include "lod/subsystem.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lod
{

/// @brief Exit codes (error-handling.md §5 table; foundation uses these).
enum class ExitCode : int
{
  Success = 0,
  InitFailed = 1,
  RuntimeError = 2,
};

/// @brief Abstract clock so the main loop is testable without wall-clock
/// sleeps (measurements, not assumptions — AGENTS.md).
class Clock
{
public:
  virtual ~Clock() = default;
  virtual std::chrono::steady_clock::time_point Now() const = 0;
  virtual void Sleep(std::chrono::microseconds Us) const = 0;
};

/// @brief Real steady-clock implementation.
class SystemClock final : public Clock
{
public:
  std::chrono::steady_clock::time_point Now() const override;
  void Sleep(std::chrono::microseconds Us) const override;
};

/// @brief Main-loop options.
struct RunOptions
{
  double TickHz = 60.0;                      ///< Fixed sim rate (engine-design §3).
  std::optional<std::uint64_t> MaxTicks = 1; ///< Loop exits after this many ticks;
                                             ///< nullopt = run until quit is requested.
  std::uint64_t MaxTicksPerFrame = 4;        ///< Spiral-of-death cap per frame.
  double MaxFrameDeltaSeconds = 0.25;        ///< Frame-delta clamp: a stall longer than
                                             ///< this is simulated as this length, so the
                                             ///< sim skips the dead time instead of
                                             ///< running a catch-up storm.
  bool Verbose = true;                       ///< Log frame/focus/resize diagnostics.
};

/// @brief Measured results of one main-loop run (measure, don't assume).
/// first/last/max/avg describe the *accepted* (post-clamp) frame delta — the
/// time the sim actually advanced — while maxRawDeltaMs keeps the largest
/// pre-clamp sample so a stall remains visible in the log.
struct LoopStats
{
  std::uint64_t Frames = 0;        ///< Frame iterations (poll+sim+pace).
  std::uint64_t Ticks = 0;         ///< Sim ticks executed.
  std::uint64_t ClampedFrames = 0; ///< Frames whose delta hit the clamp.
  std::uint64_t CappedFrames = 0;  ///< Frames that hit MaxTicksPerFrame.
  double FirstDeltaMs = 0.0;       ///< Accepted delta of the first frame.
  double LastDeltaMs = 0.0;        ///< Accepted delta of the last frame.
  double MaxDeltaMs = 0.0;         ///< Largest accepted delta.
  double MaxRawDeltaMs = 0.0;      ///< Largest delta before clamping.
  double AvgDeltaMs = 0.0;         ///< Mean accepted delta over all frames
                                   ///< (the first frame's delta is the loop
                                   ///< entry-to-first-poll interval, ~0).
  double SimHz = 0.0;              ///< Sim ticks per second of accepted time.
};

/// @brief Owns logger, diagnostics, subsystems; runs the main loop.
class App
{
public:
  /// @param crashLogDir Where terminate-handler crash logs are written.
  /// @param clockImpl Optional clock injection (defaults to SystemClock).
  explicit App(std::string crashLogDir = {}, std::unique_ptr<Clock> clockImpl = nullptr);

  virtual ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  /// @brief Register a subsystem; order of registration = init order.
  /// @pre Not called after Initialize().
  void AddSubsystem(std::unique_ptr<Subsystem> Sub);

  /// @brief Bring up diagnostics, then subsystems in order.
  /// On failure: initialized subsystems shut down in reverse order.
  [[nodiscard]] StatusResult Initialize();

  /// @brief Run the fixed-timestep loop for RunOptions::MaxTicks sim ticks
  /// (or, when MaxTicks is nullopt, until quit is requested).
  ///
  /// Each frame: poll subsystem frame events (quit/resize/focus) → advance a
  /// fixed-step accumulator (frame delta clamped to
  /// RunOptions::MaxFrameDeltaSeconds, at most MaxTicksPerFrame ticks) → sleep
  /// until the next tick boundary.
  ///
  /// @param opts Rate, tick budget, clamp and pacing options.
  /// @return Status::Ok on clean completion, or Status::InvalidArgument for a
  ///         non-finite/non-positive TickHz or a non-positive
  ///         MaxTicksPerFrame/MaxFrameDeltaSeconds.
  /// @see LoopStats for the measurements of the run that just finished.
  [[nodiscard]] StatusResult RunMainLoop(const RunOptions& opts);

  /// @brief Per-tick hook the game layer overrides (foundation: no-op).
  virtual void Tick(std::uint64_t simFrame);

  /// @brief Poll every registered subsystem's frame events and merge them
  /// (App::RunMainLoop calls this once per frame; public so the game layer can
  /// pump events outside the loop, e.g. during a long modal load).
  /// @return Merged frame summary for this poll (error-handling.md: quit is
  ///         sticky, size/focus take the newest valid contribution).
  /// @note Thread: main thread only.
  [[nodiscard]] FrameEvents PollFrameEvents();

  /// @brief Request the main loop to stop at the next frame boundary.
  /// A request set during Initialize is honoured (the loop exits immediately);
  /// it is *not* cleared by RunMainLoop, because a quit request means "stop
  /// this session", not "skip the next loop".
  /// @param Quit false clears a previous request.
  /// @note Thread: main thread (an OS signal handler must not call this).
  void RequestQuit(bool Quit = true);

  /// @brief Whether a quit request is pending (set by a frame event or
  /// RequestQuit).
  [[nodiscard]] bool IsQuitRequested() const { return quitRequested_; }

  /// @brief Reverse-order subsystem shutdown + diagnostics shutdown.
  /// Idempotent; safe to call without Initialize().
  void Shutdown();

  /// @brief Run: Initialize + RunMainLoop + Shutdown. Maps to ExitCode.
  ExitCode Run(const RunOptions& opts);

  /// @brief Declare the session mode handed to subsystems in
  /// AppContext::Mode ("headless" or "windowed"). Set it before Initialize.
  /// @param mode Short mode tag; the default is "headless".
  /// @note Thread: main thread (configuration time, before Initialize).
  void SetSessionMode(std::string_view mode);

  /// @brief The declared session mode (see SetSessionMode).
  [[nodiscard]] std::string_view SessionMode() const { return mode_; }

  /// @brief Process logger.
  Logger& GetLog() { return log_; }

  /// @brief Measurements of the most recent RunMainLoop (zeros before any).
  [[nodiscard]] const LoopStats& LastLoopStats() const { return stats_; }

private:
  Logger log_;
  Diagnostics diag_;
  std::unique_ptr<Clock> clockOwned_;
  const Clock* clockRef_;
  std::vector<std::unique_ptr<Subsystem>> subsystems_;
  std::size_t initializedCount_ = 0; ///< How many are up (for rollback).
  bool diagnosticsUp_ = false;
  bool quitRequested_ = false;    ///< Sticky quit flag (frame event or host).
  std::string mode_ = "headless"; ///< Session tag handed to subsystems.
  LoopStats stats_;               ///< Last run's measurements.
};

} // namespace lod
