#include "lod/log.h"
#include "sc_test.h"

#include <vector>

// testing.md §2.1: log level filtering; cpp.md §3: line format.
LOD_TEST(log, level_filter)
{
  lod::Logger log;
  std::vector<std::string> lines;
  log.SetSink([&](std::string_view line) { lines.emplace_back(line); });
  log.SetMinLevel(lod::Level::Warning);

  log.Debug("test", "hidden {}", 1);
  log.Info("test", "also hidden");
  log.Warn("test", "visible warn");
  log.Error("test", "visible error");

  LOD_REQUIRE(lines.size() == 2);
  LOD_CHECK(lines[0].find("WARN ") != std::string::npos);
  LOD_CHECK(lines[0].find("visible warn") != std::string::npos);
  LOD_CHECK(lines[1].find("ERROR") != std::string::npos);
}

LOD_TEST(log, line_format)
{
  lod::Logger log;
  std::vector<std::string> lines;
  log.SetSink([&](std::string_view line) { lines.emplace_back(line); });
  log.SetMinLevel(lod::Level::Trace);

  log.Info("gfx", "device {} ready", 3);
  LOD_REQUIRE(lines.size() == 1);
  const std::string& line = lines[0];
  // `[   1234ms] INFO  [marker] message` (cpp.md §3)
  LOD_CHECK(line.rfind("[", 0) == 0);
  LOD_CHECK(line.find("ms] INFO  [gfx] device 3 ready") != std::string::npos);
}
