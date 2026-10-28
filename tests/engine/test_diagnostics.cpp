#include "lod/diagnostics.h"
#include "sc_test.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::string TempCrashDir(const char* tag)
{
  auto dir = std::filesystem::temp_directory_path() / ("lodsc-crash-test-" + std::string(tag));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  return dir.string();
}
} // namespace

// testing.md §2.1: crash-path test — terminate handler writes crash log to
// the temp directory. We exercise the same code path the terminate handler
// uses (WriteCrashLogForTest) without aborting the test runner.
LOD_TEST(diagnostics, crash_log)
{
  const std::string dir = TempCrashDir("write");
  lod::Diagnostics diag(dir);
  auto init = diag.Initialize();
  LOD_REQUIRE(init.has_value());

  diag.NoteError("fake failing op");
  auto path = diag.WriteCrashLogForTest("deliberate test failure");
  LOD_REQUIRE(path.has_value());

  std::error_code ec;
  LOD_REQUIRE(std::filesystem::exists(*path, ec));
  LOD_CHECK(std::filesystem::file_size(*path, ec) > 0);

  diag.Shutdown();
  std::filesystem::remove_all(dir, ec);
}

LOD_TEST(diagnostics, init_idempotent_and_bad_dir)
{
  const std::string dir = TempCrashDir("idem");
  lod::Diagnostics diag(dir);
  LOD_CHECK(diag.Initialize().has_value());
  LOD_CHECK(diag.Initialize().has_value()); // second call is a no-op
  diag.Shutdown();
  diag.Shutdown(); // idempotent

  // Unwritable path: an existing *file* as the crash dir must fail cleanly.
  auto blocker = std::filesystem::temp_directory_path() / "lodsc-crash-test-blocker";
  std::error_code ec;
  std::filesystem::remove_all(blocker, ec);
  std::filesystem::create_directories(blocker.parent_path(), ec);
  {
    std::ofstream out(blocker);
    out << "x";
  }
  lod::Diagnostics bad(blocker.string());
  auto r = bad.Initialize();
  LOD_REQUIRE(!r.has_value());
  LOD_CHECK(r.error() == lod::Status::NotFound);
  std::filesystem::remove(blocker, ec);
}
