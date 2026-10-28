#pragma once
/// @file diagnostics.h
/// @brief Crash-path diagnostics (error-handling.md §4): terminate handler +
/// crash log carrying version/commit/timestamp and last error context.

#include "lod/status.h"

#include <string>
#include <string_view>

namespace lod
{

/// @brief Installs the process terminate handler and crash-log plumbing.
/// One instance per process, owned by App (no singleton).
class Diagnostics
{
public:
  /// @param CrashLogDir Directory for crash logs ("" = current directory).
  explicit Diagnostics(std::string CrashLogDir);
  ~Diagnostics();

  Diagnostics(const Diagnostics&) = delete;
  Diagnostics& operator=(const Diagnostics&) = delete;

  /// @brief Install the terminate handler. Idempotent.
  /// @return Status::Ok, or NotFound if the crash directory cannot be used.
  [[nodiscard]] StatusResult Initialize();

  /// @brief Restore the previous terminate handler. Idempotent.
  void Shutdown();

  /// @brief Record context included in the next crash log (last message wins).
  /// @param Context Short description of the failing operation.
  void NoteError(std::string_view Context);

  /// @brief Path of the most recently written crash log (tests, reporting).
  const std::string& LastCrashLogPath() const { return crashLogPath_; }

  /// @brief Write a crash log for a deliberate test-triggered failure.
  /// @param Reason Human-readable cause recorded in the log.
  /// @return Full path of the written log, or NotFound on I/O failure.
  [[nodiscard]] Result<std::string> WriteCrashLogForTest(std::string_view Reason);

private:
  std::string crashDir_;
  std::string lastContext_;
  std::string crashLogPath_;
  bool installed_ = false;
};

} // namespace lod
