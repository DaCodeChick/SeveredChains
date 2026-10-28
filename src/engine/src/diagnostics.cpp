#include "lod/diagnostics.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace lod
{
namespace
{

// The terminate handler must be reachable without heap allocation beyond
// what already succeeded; a process-wide pointer set under App's ownership
// is the documented single instance (error-handling.md §4).
Diagnostics* activeDiagnostics = nullptr;

std::string TimestampString()
{
  const auto now = std::chrono::system_clock::now();
  const auto calendarTime = std::chrono::system_clock::to_time_t(now);
  std::tm buf{};
#if defined(_WIN32)
  localtime_s(&buf, &calendarTime);
#else
  localtime_r(&calendarTime, &buf);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &buf);
  return text;
}

void TerminateHandler()
{
  std::string context = "unknown";
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
  // Naming the uncaught exception needs try/catch — the one place project code
  // uses exceptions (fatal-logic boundary, error-handling.md §1: a last-resort
  // diagnostic, not control flow). Compiled only when the exception policy is
  // ON: with SC_ENABLE_EXCEPTIONS=OFF the handler still records the context and
  // writes the crash log, so the policy stays genuinely switchable.
  if (auto Ex = std::current_exception())
  {
    try
    {
      std::rethrow_exception(Ex);
    }
    catch (const std::exception& E)
    {
      context = E.what();
    }
    catch (...)
    {
      context = "non-std exception";
    }
  }
#endif
  if (activeDiagnostics != nullptr)
  {
    activeDiagnostics->NoteError(context);
    (void)activeDiagnostics->WriteCrashLogForTest(context);
  }
  std::abort();
}

} // namespace

Diagnostics::Diagnostics(std::string crashLogDir) : crashDir_(std::move(crashLogDir)) {}

Diagnostics::~Diagnostics()
{
  Shutdown();
}

StatusResult Diagnostics::Initialize()
{
  if (installed_)
    return {};
  std::error_code ec;
  if (!crashDir_.empty())
  {
    std::filesystem::create_directories(crashDir_, ec); // non-throwing overload
    if (ec)
      return std::unexpected(Status::NotFound);
  }
  activeDiagnostics = this;
  std::set_terminate(&TerminateHandler);
  installed_ = true;
  return {};
}

void Diagnostics::Shutdown()
{
  if (!installed_)
    return;
  std::set_terminate(nullptr);
  if (activeDiagnostics == this)
    activeDiagnostics = nullptr;
  installed_ = false;
}

void Diagnostics::NoteError(std::string_view context)
{
  lastContext_ = std::string(context);
}

Result<std::string> Diagnostics::WriteCrashLogForTest(std::string_view Reason)
{
  std::error_code ec;
  const auto name = std::string("lodsc-crash-") + TimestampString() + ".log";
  std::filesystem::path Path =
    crashDir_.empty() ? std::filesystem::path(name) : std::filesystem::path(crashDir_) / name;
  std::ofstream Out(Path, std::ios::app);
  if (!Out)
    return std::unexpected(Status::NotFound);
  Out << "=== Severed Chains Native crash log ===\n"
      << "time:    " << TimestampString() << "\n"
      << "reason:  " << Reason << "\n"
      << "context: " << lastContext_ << "\n";
  Out.close();
  if (!Out)
    return std::unexpected(Status::NotFound);
  crashLogPath_ = Path.string();
  return crashLogPath_;
}

} // namespace lod
