/// @file test_config.cpp
/// @brief Config load/save/validate behaviour (testing.md §2.1: recovery
/// paths are first-class test targets, not just the happy path).
#include "lod/config.h"
#include "lod/fs.h"
#include "lod/log.h"
#include "lod/status.h"
#include "sc_test.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#  include <sys/stat.h>
#endif

namespace
{

/// @brief A config file plus the log lines its load produced.
struct LoadOutcome
{
  lod::Config Config;
  bool Ok = false;
  lod::Status Status = lod::Status::Ok;
  std::vector<std::string> Warnings;
  std::vector<std::string> Infos;
};

std::filesystem::path TempDir(const std::string& tag)
{
  const auto dir = std::filesystem::temp_directory_path() / ("lodsc-config-test-" + tag);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

/// @brief Write a config file body (test fixture creation).
void WriteFixture(const std::filesystem::path& path, std::string_view body)
{
  (void)lod::fs::WriteFileText(path, body);
}

/// @brief Load with the log captured, so recovery messages are assertable.
LoadOutcome CapturedLoad(const std::filesystem::path& path)
{
  LoadOutcome out;
  lod::Logger log;
  std::vector<std::string> lines;
  log.SetSink([&lines](std::string_view line) { lines.emplace_back(line); });
  log.SetMinLevel(lod::Level::Trace);

  auto loaded = lod::LoadConfig(path, log);
  out.Ok = loaded.has_value();
  out.Status = loaded.has_value() ? lod::Status::Ok : loaded.error();
  out.Config = loaded.value_or(lod::Config{});
  for (const std::string& line : lines)
    if (line.find("WARN ") != std::string::npos)
      out.Warnings.push_back(line);
    else if (line.find("INFO ") != std::string::npos)
      out.Infos.push_back(line);
  return out;
}

bool AnyContains(const std::vector<std::string>& lines, std::string_view needle)
{
  for (const std::string& line : lines)
    if (line.find(needle) != std::string::npos)
      return true;
  return false;
}

} // namespace

LOD_TEST(config, defaults_when_absent)
{
  const auto path = TempDir("absent") / "config.txt";
  const LoadOutcome out = CapturedLoad(path);

  LOD_REQUIRE(out.Ok);
  LOD_CHECK(out.Config.TickHz == 60.0);
  LOD_CHECK(out.Config.Headless == true);
  LOD_CHECK(out.Config.WindowWidth == 1280);
  LOD_CHECK(out.Config.WindowHeight == 720);
  LOD_CHECK(out.Config.WindowTitle == "Severed Chains");
  // An absent file is a normal first run: informational, not a warning.
  LOD_CHECK(AnyContains(out.Infos, "using defaults"));
  LOD_CHECK(out.Warnings.empty());
}

LOD_TEST(config, parses_every_key)
{
  const auto path = TempDir("parse") / "config.txt";
  WriteFixture(path,
               "# comment line\n"
               "\n"
               "tick_hz 30.5\n"
               "headless false\n"
               "window_width 1024\n"
               "window_height 768\n"
               "window_title My LoD Session\n"
               "resource_dir /home/player/games/lod/files\n");

  const LoadOutcome out = CapturedLoad(path);
  LOD_REQUIRE(out.Ok);
  LOD_CHECK(out.Config.TickHz == 30.5);
  LOD_CHECK(out.Config.Headless == false);
  LOD_CHECK(out.Config.WindowWidth == 1024);
  LOD_CHECK(out.Config.WindowHeight == 768);
  // The value is the rest of the line, so titles keep their spaces.
  LOD_CHECK(out.Config.WindowTitle == "My LoD Session");
  LOD_CHECK(out.Config.ResourceDir == "/home/player/games/lod/files");
  LOD_CHECK(out.Warnings.empty());
}

LOD_TEST(config, tolerant_of_comments_crlf_and_case)
{
  const auto path = TempDir("tolerant") / "config.txt";
  WriteFixture(path, "  # indented comment\r\n\r\nWINDOW_WIDTH 800\r\nHeadless yes\r\n");

  const LoadOutcome out = CapturedLoad(path);
  LOD_REQUIRE(out.Ok);
  LOD_CHECK(out.Config.WindowWidth == 800);
  LOD_CHECK(out.Config.Headless == true);
}

LOD_TEST(config, bad_values_keep_defaults_and_warn)
{
  const auto path = TempDir("bad") / "config.txt";
  WriteFixture(path,
               "tick_hz not-a-number\n"
               "headless maybe\n"
               "window_width 12.5\n"
               "window_height\n"
               "window_title\n"
               "some_future_key 42\n");

  const LoadOutcome out = CapturedLoad(path);
  // Malformed content is recovered per key, never an error.
  LOD_REQUIRE(out.Ok);
  LOD_CHECK(out.Config.TickHz == 60.0);
  LOD_CHECK(out.Config.Headless == true);
  LOD_CHECK(out.Config.WindowWidth == 1280);
  LOD_CHECK(out.Config.WindowHeight == 720);
  LOD_CHECK(out.Config.WindowTitle == "Severed Chains");

  LOD_CHECK(AnyContains(out.Warnings, "bad tick_hz"));
  LOD_CHECK(AnyContains(out.Warnings, "bad headless"));
  LOD_CHECK(AnyContains(out.Warnings, "bad window_width"));
  LOD_CHECK(AnyContains(out.Warnings, "bad window_height"));
  LOD_CHECK(AnyContains(out.Warnings, "bad window_title"));
  LOD_CHECK(AnyContains(out.Warnings, "unknown config key 'some_future_key'"));
}

LOD_TEST(config, out_of_range_values_are_clamped)
{
  lod::Logger log;
  std::vector<std::string> lines;
  log.SetSink([&lines](std::string_view line) { lines.emplace_back(line); });

  lod::Config config;
  config.TickHz = 0.0;
  config.WindowWidth = 1;
  config.WindowHeight = 999999;
  config.WindowTitle = "   ";
  lod::ValidateConfig(config, log);

  LOD_CHECK(config.TickHz == 60.0);
  LOD_CHECK(config.WindowWidth == lod::MinWindowSize);
  LOD_CHECK(config.WindowHeight == lod::MaxWindowSize);
  LOD_CHECK(config.WindowTitle == "Severed Chains");
  LOD_REQUIRE(lines.size() == 4);

  // Non-finite rates are rejected too (a NaN rate would hang the loop).
  lod::Config nan;
  nan.TickHz = 1.0 / 0.0;
  lod::ValidateConfig(nan, log);
  LOD_CHECK(nan.TickHz == 60.0);
}

LOD_TEST(config, tick_hz_nan_text_falls_back)
{
  const auto path = TempDir("nan") / "config.txt";
  WriteFixture(path, "tick_hz nan\n");
  const LoadOutcome out = CapturedLoad(path);
  LOD_REQUIRE(out.Ok);
  LOD_CHECK(out.Config.TickHz == 60.0);
}

LOD_TEST(config, round_trip_save_load)
{
  const auto dir = TempDir("roundtrip");
  const auto path = dir / "nested" / "deeper" / std::string(lod::ConfigFileName);

  lod::Logger log;
  std::vector<std::string> lines;
  log.SetSink([&lines](std::string_view line) { lines.emplace_back(line); });

  lod::Config original;
  original.TickHz = 59.94;
  original.Headless = false;
  original.WindowWidth = 1920;
  original.WindowHeight = 1080;
  original.WindowTitle = "Severed Chains — test session";
  original.ResourceDir = "/home/player/games/lod/files";

  const auto saved = lod::SaveConfig(path, original, log);
  LOD_REQUIRE(saved.has_value());
  // Parents are created on demand.
  LOD_CHECK(lines.empty());

  const LoadOutcome reloaded = CapturedLoad(path);
  LOD_REQUIRE(reloaded.Ok);
  LOD_CHECK(reloaded.Config.TickHz == original.TickHz);
  LOD_CHECK(reloaded.Config.Headless == original.Headless);
  LOD_CHECK(reloaded.Config.WindowWidth == original.WindowWidth);
  LOD_CHECK(reloaded.Config.WindowHeight == original.WindowHeight);
  LOD_CHECK(reloaded.Config.WindowTitle == original.WindowTitle);
  LOD_CHECK(reloaded.Config.ResourceDir == original.ResourceDir);

  // The file is human-editable text with the documented key names.
  const auto text = lod::fs::ReadFileText(path);
  LOD_REQUIRE(text.has_value());
  LOD_CHECK(text->find("headless false\n") != std::string::npos);
  LOD_CHECK(text->find("window_title Severed Chains — test session\n") != std::string::npos);
  LOD_CHECK(text->find("resource_dir /home/player/games/lod/files\n") != std::string::npos);
}

LOD_TEST(config, save_rejects_multiline_title)
{
  const auto path = TempDir("badtitle") / "config.txt";
  lod::Logger log;
  std::vector<std::string> lines;
  log.SetSink([&lines](std::string_view line) { lines.emplace_back(line); });

  lod::Config config;
  config.WindowTitle = "two\nlines";
  const auto saved = lod::SaveConfig(path, config, log);
  LOD_REQUIRE(!saved.has_value());
  LOD_CHECK(saved.error() == lod::Status::InvalidArgument);
  std::error_code ec;
  LOD_CHECK(!std::filesystem::exists(path, ec));
  LOD_CHECK(!lines.empty());
}

LOD_TEST(config, unreadable_file_reports_io_open)
{
#ifndef _WIN32
  const auto dir = TempDir("unreadable");
  const auto path = dir / "config.txt";
  WriteFixture(path, "window_width 640\n");
  ::chmod(path.c_str(), 0);

  const LoadOutcome out = CapturedLoad(path);
  LOD_CHECK(!out.Ok);
  LOD_CHECK(out.Status == lod::Status::IOOpen);
  LOD_CHECK(AnyContains(out.Warnings, "unreadable"));

  ::chmod(path.c_str(), 0600);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
#else
  // Windows has no chmod; the same path is covered by the IOOpen mapping in
  // fs::ReadFileText. Needs user confirmation on a Windows runner.
  LOD_CHECK(true);
#endif
}

LOD_TEST(config, validate_leaves_good_values_alone)
{
  lod::Logger log;
  std::vector<std::string> lines;
  log.SetSink([&lines](std::string_view line) { lines.emplace_back(line); });

  lod::Config config;
  config.TickHz = 240.0;
  config.WindowWidth = 3840;
  config.WindowHeight = 2160;
  config.WindowTitle = "  Spaced Title  ";
  lod::ValidateConfig(config, log);

  LOD_CHECK(config.TickHz == 240.0);
  LOD_CHECK(config.WindowWidth == 3840);
  LOD_CHECK(config.WindowHeight == 2160);
  // Surrounding whitespace is trimmed without a warning.
  LOD_CHECK(config.WindowTitle == "Spaced Title");
  LOD_CHECK(lines.empty());
}

LOD_TEST(config, resource_dir_defaults_empty_and_omitted_from_save)
{
  // "" means "use the resolved default", so a default config must not carry the
  // key at all — an empty written value would be noise the user has to decode.
  const auto dir = TempDir("resourcedir");
  const auto path = dir / "config.txt";
  LOD_CHECK(lod::Config{}.ResourceDir.empty());

  lod::Logger log;
  std::vector<std::string> lines;
  log.SetSink([&lines](std::string_view line) { lines.emplace_back(line); });
  LOD_REQUIRE(lod::SaveConfig(path, lod::Config{}, log).has_value());

  const auto text = lod::fs::ReadFileText(path);
  LOD_REQUIRE(text.has_value());
  LOD_CHECK(text->find("resource_dir") == std::string::npos);

  const LoadOutcome out = CapturedLoad(path);
  LOD_REQUIRE(out.Ok);
  LOD_CHECK(out.Config.ResourceDir.empty());
}

LOD_TEST(config, resource_dir_trims_and_recovers_from_multiline)
{
  const auto dir = TempDir("resourcedir-trim");
  WriteFixture(dir / "a.txt", "resource_dir   /tmp/padded/tree   \n");
  const LoadOutcome padded = CapturedLoad(dir / "a.txt");
  LOD_REQUIRE(padded.Ok);
  LOD_CHECK(padded.Config.ResourceDir == "/tmp/padded/tree");
  LOD_CHECK(padded.Warnings.empty());

  // A value that spans lines would corrupt the format on the next save, so it
  // degrades to unset (resolved default) with a warning rather than being kept.
  lod::Config multiLine;
  multiLine.ResourceDir = "/tmp/one\n/tmp/two";
  std::vector<std::string> lines;
  lod::Logger log;
  log.SetSink([&lines](std::string_view line) { lines.emplace_back(line); });
  lod::ValidateConfig(multiLine, log);
  LOD_CHECK(multiLine.ResourceDir.empty());
  LOD_REQUIRE(!lines.empty());
  LOD_CHECK(lines.front().find("resource_dir") != std::string::npos);

  // Saving must then succeed (no multi-line value left in the struct).
  LOD_REQUIRE(lod::SaveConfig(dir / "b.txt", multiLine, log).has_value());
}
