#pragma once
/// @file subsystem.h
/// @brief Subsystem contract for the App lifecycle (engine-design.md §2):
/// ordered initialization, reverse-order shutdown, rollback on partial
/// failure (error-handling.md §3).

#include "lod/frame.h"
#include "lod/status.h"

#include <string_view>

namespace lod
{

class Logger;
class Diagnostics;

/// @brief Services handed to subsystems during Initialize.
struct AppContext
{
  Logger& Log;           ///< Process logger (cpp.md §3).
  Diagnostics& Diag;     ///< Crash-path diagnostics.
  std::string_view Mode; ///< "headless" for the foundation milestone.
};

/// @brief One engine subsystem in the App lifecycle.
/// Implementations own their state via RAII; Shutdown must be idempotent.
class Subsystem
{
public:
  virtual ~Subsystem() = default;

  /// @brief Stable marker for logging and failure reports.
  virtual std::string_view Name() const = 0;

  /// @brief Initialize this subsystem. On failure, App rolls back the
  ///        already-initialized subsystems in reverse order.
  /// @param ctx Services (logger, crash context, session mode) for this call.
  /// @return Status::Ok, or the failure code (logged by App and recorded in
  ///         the crash context).
  /// @note Thread: main thread, once, from App::Initialize.
  [[nodiscard]] virtual StatusResult Initialize(AppContext& ctx) = 0;

  /// @brief Release resources. Must tolerate being called once per successful
  ///        Initialize (and only then).
  /// @note Thread: main thread, from App::Shutdown / the destructor.
  virtual void Shutdown() = 0;

  /// @brief Drain per-frame OS/system events (engine-design.md §2:
  ///        `Platform::pollEvents()`), collapsed into one frame summary.
  /// @return Frame events for this frame; the default is "nothing happened"
  ///         (subsystems that own no OS event queue leave it alone).
  /// @note Thread: main thread, once per frame between Initialize and
  ///       Shutdown. Never called before Initialize or after Shutdown.
  /// @see lod::FrameEvents, App::PollFrameEvents
  [[nodiscard]] virtual FrameEvents PollFrame() { return {}; }
};

} // namespace lod
