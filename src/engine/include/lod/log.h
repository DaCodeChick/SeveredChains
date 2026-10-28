#pragma once
/// @file log.h
/// @brief `lod::Logger` — the process's single diagnostic sink (cpp.md §3).
/// Leveled, tagged ("marker"), timestamped; std::format-based (printf family
/// is prohibited). Line format: `[   1234ms] INFO  [marker] message`.

#include <chrono>
#include <cstdint>
#include <format>
#include <functional>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>

namespace lod
{

/// @brief Log levels, ascending severity.
enum class Level : std::uint8_t
{
  Trace,
  Debug,
  Info,
  Warning,
  Error
};

/// @brief Thread-safe leveled logger. Format happens before the sink lock.
class Logger
{
public:
  /// @brief Sink callback; receives one fully formatted line (no newline).
  using Sink = std::function<void(std::string_view)>;

  Logger();

  /// @brief Set the minimum level emitted (below this, messages are dropped).
  void SetMinLevel(Level Min) { minLevel_ = Min; }

  /// @brief Replace the output sink. Default sink writes to stderr.
  /// @param newSink Invoked per formatted line; must not throw (diagnostic
  ///        I/O failure is a terminate condition — keep sinks infallible).
  void SetSink(Sink newSink);

  /// @brief Formatted log entry (literal format strings only).
  /// @param msgLevel Severity; dropped below the configured minimum.
  /// @param marker Subsystem tag, e.g. "app".
  /// @param fmt   std::format string (compile-checked literal).
  template <class... Args>
  void Log(Level msgLevel, std::string_view marker, std::format_string<Args...> fmt, Args&&... argsPack)
  {
    if (msgLevel < minLevel_)
      return;
    Emit(msgLevel, marker, std::format(fmt, std::forward<Args>(argsPack)...), std::source_location::current());
  }

  /// @brief Trace convenience (see Log).
  template <class... Args>
  void Trace(std::string_view marker, std::format_string<Args...> fmt, Args&&... A)
  {
    Log(Level::Trace, marker, fmt, std::forward<Args>(A)...);
  }
  /// @brief Debug convenience (see Log).
  template <class... Args>
  void Debug(std::string_view marker, std::format_string<Args...> fmt, Args&&... A)
  {
    Log(Level::Debug, marker, fmt, std::forward<Args>(A)...);
  }
  /// @brief Info convenience (see Log).
  template <class... Args>
  void Info(std::string_view marker, std::format_string<Args...> fmt, Args&&... A)
  {
    Log(Level::Info, marker, fmt, std::forward<Args>(A)...);
  }
  /// @brief Warning convenience (see Log).
  template <class... Args>
  void Warn(std::string_view marker, std::format_string<Args...> fmt, Args&&... A)
  {
    Log(Level::Warning, marker, fmt, std::forward<Args>(A)...);
  }
  /// @brief Error convenience (see Log).
  template <class... Args>
  void Error(std::string_view marker, std::format_string<Args...> fmt, Args&&... A)
  {
    Log(Level::Error, marker, fmt, std::forward<Args>(A)...);
  }

private:
  void Emit(Level msgLevel, std::string_view marker, std::string Message, const std::source_location& Loc);

  std::mutex sinkMutex_;
  Sink sinkFn_;
  Level minLevel_ = Level::Info;
  std::chrono::steady_clock::time_point startTime_;
};

} // namespace lod
