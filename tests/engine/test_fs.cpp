/// @file test_fs.cpp
/// @brief Runtime path resolution and file access error taxonomy
/// (testing.md §2.1: every error path is reachable and specific).
#include "lod/fs.h"
#include "lod/status.h"
#include "sc_test.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#  include <sys/stat.h>
#endif

namespace
{

/// @brief Write fixture bytes (tests own their fixture I/O; the engine's write
/// API is text-shaped and stays that way until a production caller needs more).
bool WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!bytes.empty())
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return out.good();
}

std::filesystem::path TempDir(const std::string& tag)
{
  const auto dir = std::filesystem::temp_directory_path() / ("lodsc-fs-test-" + tag);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

/// @brief Scoped environment override (restored on destruction) — path
/// resolution reads the process environment, so tests must not leak it.
class ScopedEnv
{
public:
  ScopedEnv(const char* name, const char* value) : name_(name)
  {
    if (const char* old = std::getenv(name); old != nullptr)
    {
      hadOldValue_ = true;
      oldValue_ = old;
    }
    Set(value);
  }

  ~ScopedEnv()
  {
    if (hadOldValue_)
      Set(oldValue_.c_str());
    else
      Set(nullptr);
  }

  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
  void Set(const char* value)
  {
#ifdef _WIN32
    if (value != nullptr)
      _putenv_s(Name.c_str(), value);
    else
      _putenv_s(Name.c_str(), "");
#else
    if (value != nullptr)
      ::setenv(name_.c_str(), value, 1);
    else
      ::unsetenv(name_.c_str());
#endif
  }

  std::string name_;
  std::string oldValue_;
  bool hadOldValue_ = false;
};

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b)
{
  std::error_code ec;
  // Lexical comparison is wrong for "/tmp/x" vs "/private/tmp/x" (macOS
  // symlinks /tmp), so compare canonical forms where the file exists.
  const auto ca = std::filesystem::weakly_canonical(a, ec);
  if (!ec)
  {
    const auto cb = std::filesystem::weakly_canonical(b, ec);
    if (!ec)
      return ca == cb;
  }
  return a == b;
}

} // namespace

LOD_TEST(fs, env_overrides_take_precedence)
{
  const auto dir = TempDir("env");
  const ScopedEnv resource("SC_RESOURCE_DIR", (dir / "res").string().c_str());
  const ScopedEnv config("SC_CONFIG_DIR", (dir / "cfg").string().c_str());
  const ScopedEnv data("SC_DATA_DIR", (dir / "data").string().c_str());

  const lod::fs::Paths paths = lod::fs::ResolvePaths("/opt/whatever/lodsc");
  LOD_CHECK(paths.ResourceDir == dir / "res");
  LOD_CHECK(paths.ConfigDir == dir / "cfg");
  LOD_CHECK(paths.DataDir == dir / "data");
}

LOD_TEST(fs, bundle_relative_resource_dir)
{
  const auto dir = TempDir("bundle");
  std::error_code ec;
  // <root>/Contents/MacOS/lodsc with <root>/Contents/Resources present.
  std::filesystem::create_directories(dir / "Contents" / "MacOS", ec);
  std::filesystem::create_directories(dir / "Contents" / "Resources", ec);
  const ScopedEnv resource("SC_RESOURCE_DIR", "");
  const ScopedEnv config("SC_CONFIG_DIR", "");
  const ScopedEnv data("SC_DATA_DIR", "");

  const lod::fs::Paths paths = lod::fs::ResolvePaths((dir / "Contents" / "MacOS" / "lodsc").string());
  LOD_CHECK(SamePath(paths.ResourceDir, dir / "Contents" / "Resources"));
}

LOD_TEST(fs, flat_layout_and_user_dir_fallbacks)
{
  const auto dir = TempDir("flat");
  const ScopedEnv resource("SC_RESOURCE_DIR", "");
  const ScopedEnv config("SC_CONFIG_DIR", "");
  const ScopedEnv data("SC_DATA_DIR", "");

  // No bundle Resources next to the executable → resource dir is its own dir.
  const lod::fs::Paths paths = lod::fs::ResolvePaths((dir / "bin" / "lodsc").string());
  LOD_CHECK(SamePath(paths.ResourceDir, dir / "bin"));

  // With a known HOME the writable dirs land in the user-data location; with
  // no HOME at all they fall back to the executable directory.
  const auto fakeHome = dir / "home";
  std::error_code ec;
  std::filesystem::create_directories(fakeHome, ec);
  {
    const ScopedEnv home("HOME", fakeHome.string().c_str());
    const lod::fs::Paths withHome = lod::fs::ResolvePaths((dir / "bin" / "lodsc").string());
    LOD_CHECK(withHome.ConfigDir != dir / "bin");
    LOD_CHECK(!withHome.ConfigDir.empty());
    LOD_CHECK(withHome.DataDir == withHome.ConfigDir);
  }
  {
    const ScopedEnv home("HOME", "");
    const lod::fs::Paths noHome = lod::fs::ResolvePaths((dir / "bin" / "lodsc").string());
    LOD_CHECK(SamePath(noHome.ConfigDir, dir / "bin"));
    LOD_CHECK(SamePath(noHome.DataDir, dir / "bin"));
  }
}

LOD_TEST(fs, read_missing_file_is_not_found)
{
  const auto result = lod::fs::ReadFileText(TempDir("missing") / "nope.txt");
  LOD_REQUIRE(!result.has_value());
  LOD_CHECK(result.error() == lod::Status::NotFound);
}

LOD_TEST(fs, read_directory_is_not_found)
{
  const auto dir = TempDir("dir");
  const auto result = lod::fs::ReadFileText(dir);
  LOD_REQUIRE(!result.has_value());
  LOD_CHECK(result.error() == lod::Status::NotFound);
}

LOD_TEST(fs, write_creates_parents_and_roundtrips)
{
  const auto dir = TempDir("write");
  const auto path = dir / "a" / "b" / "c.txt";
  constexpr std::string_view body = "line one\nline two\n";

  const auto written = lod::fs::WriteFileText(path, body);
  LOD_REQUIRE(written.has_value());

  const auto read = lod::fs::ReadFileText(path);
  LOD_REQUIRE(read.has_value());
  LOD_CHECK(*read == std::string(body));

  // Rewrite truncates: a shorter body must not leave the old tail behind.
  LOD_REQUIRE(lod::fs::WriteFileText(path, "short").has_value());
  const auto reread = lod::fs::ReadFileText(path);
  LOD_REQUIRE(reread.has_value());
  LOD_CHECK(*reread == "short");
}

LOD_TEST(fs, binary_safe_roundtrip)
{
  const auto path = TempDir("binary") / "blob.bin";
  const std::string body = std::string("head\0tail\0\xff\x01", 12);

  LOD_REQUIRE(lod::fs::WriteFileText(path, body).has_value());
  const auto read = lod::fs::ReadFileText(path);
  LOD_REQUIRE(read.has_value());
  LOD_CHECK(read->size() == body.size());
  LOD_CHECK(*read == body);
}

LOD_TEST(fs, empty_file_reads_as_empty)
{
  const auto path = TempDir("empty") / "empty.txt";
  LOD_REQUIRE(lod::fs::WriteFileText(path, "").has_value());
  const auto read = lod::fs::ReadFileText(path);
  LOD_REQUIRE(read.has_value());
  LOD_CHECK(read->empty());
}

LOD_TEST(fs, write_into_unusable_parent_is_io_write)
{
  const auto dir = TempDir("blocked");
  // A regular file where a directory would have to be: create_directories fails.
  const auto blocker = dir / "blocker";
  LOD_REQUIRE(lod::fs::WriteFileText(blocker, "x").has_value());

  const auto result = lod::fs::WriteFileText(blocker / "child.txt", "y");
  LOD_REQUIRE(!result.has_value());
  LOD_CHECK(result.error() == lod::Status::IOWrite);
}

LOD_TEST(fs, unreadable_file_is_io_open)
{
#ifndef _WIN32
  const auto dir = TempDir("openfail");
  const auto path = dir / "secret.txt";
  LOD_REQUIRE(lod::fs::WriteFileText(path, "content").has_value());
  ::chmod(path.c_str(), 0);

  const auto read = lod::fs::ReadFileText(path);
  LOD_CHECK(!read.has_value());
  if (!read.has_value())
    LOD_CHECK(read.error() == lod::Status::IOOpen);
  ::chmod(path.c_str(), 0600);
#else
  // chmod-based permission tests are POSIX; Windows coverage of IOOpen needs
  // a Windows runner (needs user confirmation there).
  LOD_CHECK(true);
#endif
}

LOD_TEST(fs, write_under_non_directory_parent_fails)
{
  // ENOTDIR, not EACCES: this must fail for root as well as for a normal user,
  // so the test is valid in a container running as root.
  const auto dir = TempDir("enotdir");
  const auto blocker = dir / "not-a-dir";
  LOD_REQUIRE(lod::fs::WriteFileText(blocker, "x").has_value());

  const auto result = lod::fs::WriteFileText(blocker / "nested" / "file.txt", "y");
  LOD_CHECK(!result.has_value());
  if (!result.has_value())
    LOD_CHECK(result.error() == lod::Status::IOWrite);
}

LOD_TEST(fs, read_file_bytes_roundtrips_binary_and_empty)
{
  const auto dir = TempDir("bytes");
  const auto path = dir / "asset.bin";
  const std::vector<std::uint8_t> payload = {0x00, 0xff, 0x10, 0x00, 0x7f, 0x80, 0x00};
  LOD_REQUIRE(WriteBytes(path, payload));

  auto read = lod::fs::ReadFileBytes(path);
  LOD_REQUIRE(read.has_value());
  LOD_CHECK(*read == payload);

  const auto emptyPath = dir / "empty.bin";
  LOD_REQUIRE(WriteBytes(emptyPath, {}));
  auto empty = lod::fs::ReadFileBytes(emptyPath);
  LOD_REQUIRE(empty.has_value());
  LOD_CHECK(empty->empty());
}

LOD_TEST(fs, read_file_bytes_separates_absent_from_directory)
{
  const auto dir = TempDir("bytes taxonomy");
  LOD_CHECK(lod::fs::ReadFileBytes(dir / "absent.bin").error() == lod::Status::NotFound);
  LOD_CHECK(lod::fs::ReadFileBytes(dir).error() == lod::Status::NotFound);
}

LOD_TEST(fs, read_file_bytes_rejects_files_over_the_ceiling)
{
  const auto dir = TempDir("bytes ceiling");
  const auto path = dir / "big.bin";
  LOD_REQUIRE(WriteBytes(path, std::vector<std::uint8_t>(4096, 0x5a)));

  auto capped = lod::fs::ReadFileBytes(path, 1024);
  LOD_CHECK(!capped.has_value());
  if (!capped.has_value())
    LOD_CHECK(capped.error() == lod::Status::IOTooLarge);
  // Exactly at the ceiling is allowed; a ceiling of 0 means "no ceiling".
  LOD_CHECK(lod::fs::ReadFileBytes(path, 4096).has_value());
  LOD_CHECK(lod::fs::ReadFileBytes(path, 0).has_value());
}

LOD_TEST(fs, read_file_prefix_truncates_rather_than_rejecting)
{
  const auto dir = TempDir("prefix");
  const auto path = dir / "head.bin";
  std::vector<std::uint8_t> payload;
  for (std::uint8_t i = 0; i < 64; ++i)
    payload.push_back(i);
  LOD_REQUIRE(WriteBytes(path, payload));

  auto head = lod::fs::ReadFilePrefix(path, 16);
  LOD_REQUIRE(head.has_value());
  LOD_REQUIRE(head->size() == 16);
  LOD_CHECK(std::equal(head->begin(), head->end(), payload.begin()));

  // Asking for more than the file holds yields the whole file, not an error.
  auto whole = lod::fs::ReadFilePrefix(path, 4096);
  LOD_REQUIRE(whole.has_value());
  LOD_CHECK(*whole == payload);

  const auto emptyPath = dir / "empty.bin";
  LOD_REQUIRE(WriteBytes(emptyPath, {}));
  auto empty = lod::fs::ReadFilePrefix(emptyPath, 16);
  LOD_REQUIRE(empty.has_value());
  LOD_CHECK(empty->empty());

  LOD_CHECK(lod::fs::ReadFilePrefix(dir / "absent.bin", 16).error() == lod::Status::NotFound);
  LOD_CHECK(lod::fs::ReadFilePrefix(dir, 16).error() == lod::Status::NotFound);
}
