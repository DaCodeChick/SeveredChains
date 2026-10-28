#include "lod/log.h"

#include <cstdio>

namespace lod
{

namespace
{
constexpr std::string_view LevelTag(Level msgLevel)
{
  switch (msgLevel)
  {
    case Level::Trace:
      return "TRACE";
    case Level::Debug:
      return "DEBUG";
    case Level::Info:
      return "INFO ";
    case Level::Warning:
      return "WARN ";
    case Level::Error:
      return "ERROR";
  }
  return "?????";
}
} // namespace

Logger::Logger() : startTime_(std::chrono::steady_clock::now())
{
  sinkFn_ = [](std::string_view line)
  {
    // stderr fallback; fwrite is infallible from the caller's perspective
    // (diagnostic I/O failure is a terminate condition, not a Status).
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fputc('\n', stderr);
  };
}

void Logger::SetSink(Sink NewSink)
{
  const std::lock_guard lock(sinkMutex_);
  sinkFn_ = std::move(NewSink);
}

void Logger::Emit(Level msgLevel, std::string_view marker, std::string message, const std::source_location& loc)
{
  const auto elapsedMs = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime_).count());
  // cpp.md §3 line format: `[   1234ms] INFO  [marker] message`.
  // Formatting happens before taking the sink lock.
  const std::string line = std::format("[{:>7}ms] {} [{}] {} ({}:{})", elapsedMs, LevelTag(msgLevel), marker, message,
                                       loc.file_name(), loc.line());
  const std::lock_guard lock(sinkMutex_);
  if (sinkFn_)
    sinkFn_(line);
}

} // namespace lod
