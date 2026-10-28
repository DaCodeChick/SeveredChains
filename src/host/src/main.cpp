/// @file main.cpp
/// @brief Thin host entry point: CLI parsing, path/config resolution and
/// subsystem wiring only (build-system.md §1). No engine logic here;
/// std::print is permitted in the host for CLI output (cpp.md §3).
#include "game/game.h"
#include "lod/app.h"
#include "lod/config.h"
#include "lod/fs.h"
#include "lod/platform.h"
#include "lod/resources.h"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

/// @brief Parsed command line. `std::nullopt` means "not given" so a flag can
/// override the config file without the file's value being lost.
struct CLIOptions
{
  std::optional<std::uint64_t> Ticks;     ///< --ticks N (0 = run until quit).
  std::optional<double> TickHz;           ///< --tick-hz F.
  std::optional<int> Width;               ///< --width N.
  std::optional<int> Height;              ///< --height N.
  std::optional<std::string> ConfigPath;  ///< --config PATH.
  std::string CrashDir;                   ///< --crash-dir D ("" = dataDir/crashes).
  std::optional<std::string> ResourceDir; ///< --resources D (else SC_RESOURCE_DIR / resolved).
  std::vector<std::string> InspectPaths;  ///< --inspect P (repeatable, root-relative).
  bool ValidateAssets = false;            ///< --validate-assets: scan + validate the tree.
  bool Windowed = false;                  ///< --windowed: real video driver.
  bool Headless = false;                  ///< --headless: force the dummy driver.
  bool NoConfigWrite = false;             ///< --no-config-write.
  bool Help = false;                      ///< --help.
  std::vector<std::string> Errors;        ///< Diagnostics; non-empty = bad usage.
};

void PrintUsage()
{
  std::println("lodsc — Severed Chains Native (application foundation)");
  std::println("usage: lodsc [options]");
  std::println("  --ticks <N>     sim ticks to run before exiting; 0 = run until quit");
  std::println("                  (default: 0 with --windowed, otherwise 1)");
  std::println("  --tick-hz <F>   fixed sim rate (default: config tick_hz)");
  std::println("  --windowed      use the real video driver (default: headless/dummy)");
  std::println("  --headless      force the dummy video driver (overrides --windowed)");
  std::println("  --width <N>     window width (overrides config)");
  std::println("  --height <N>    window height (overrides config)");
  std::println("  --config <P>    config file path (default: <configDir>/config.txt)");
  std::println("  --crash-dir <D> crash-log directory (default: <dataDir>/crashes)");
  std::println("  --no-config-write  do not create/rewrite the config file");
  std::println("  --resources <D>    resource root (overrides config resource_dir, which");
  std::println("                     overrides the resolved default / SC_RESOURCE_DIR)");
  std::println("  --validate-assets  discover and validate the resource tree, then exit");
  std::println("  --inspect <P>      validate one root-relative resource and exit (repeatable)");
  std::println("  --help          print this help and exit");
  std::println("");
  std::println("Directories honour SC_RESOURCE_DIR / SC_CONFIG_DIR / SC_DATA_DIR.");
  std::println("Asset exit codes: 0 ok, 4 root unusable, 5 a resource failed to validate.");
}

/// @brief Host exit codes for the asset commands (distinct from Status codes,
/// which the loop path returns directly).
inline constexpr int AssetExitOk = 0;
inline constexpr int AssetExitNoRoot = 4;
inline constexpr int AssetExitValidationFailed = 5;

/// @brief Parse an unsigned integer argument (full-range checked).
std::optional<std::uint64_t> ParseU64(std::string_view text)
{
  std::uint64_t value = 0;
  const std::string owned(text);
  const char* first = owned.data();
  const char* last = owned.data() + owned.size();
  const auto res = std::from_chars(first, last, value);
  if (res.ec != std::errc{} || res.ptr != last)
    return std::nullopt;
  return value;
}

/// @brief Parse a signed integer argument (window sizes are signed at the SDL
/// boundary, so a negative value must be rejected rather than narrowed).
std::optional<int> ParseI32(std::string_view text)
{
  int value = 0;
  const std::string owned(text);
  const char* first = owned.data();
  const char* last = owned.data() + owned.size();
  const auto res = std::from_chars(first, last, value);
  if (res.ec != std::errc{} || res.ptr != last)
    return std::nullopt;
  return value;
}

std::optional<double> ParseDouble(std::string_view text)
{
  double value = 0.0;
  const std::string owned(text);
  const char* first = owned.data();
  const char* last = owned.data() + owned.size();
  const auto res = std::from_chars(first, last, value);
  if (res.ec != std::errc{} || res.ptr != last)
    return std::nullopt;
  return value;
}

/// @brief Consume `--<name> <value>`; records an error when the value is
/// missing or malformed.
template <class Parser, class Field>
bool TakeValue(int& index, int argc, char** argv, std::string_view name, Parser parse, Field& field,
               std::vector<std::string>& errors)
{
  if (index + 1 >= argc)
  {
    errors.push_back(std::string(name) + " needs a value");
    return false;
  }
  const auto parsed = parse(argv[++index]);
  if (!parsed)
  {
    errors.push_back(std::string(name) + " has an invalid value");
    return false;
  }
  field = *parsed;
  return true;
}

CLIOptions ParseArgs(int argc, char** argv)
{
  CLIOptions cli;
  for (int i = 1; i < argc; ++i)
  {
    const std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h")
    {
      cli.Help = true;
    }
    else if (arg == "--windowed")
    {
      cli.Windowed = true;
    }
    else if (arg == "--headless")
    {
      cli.Headless = true;
    }
    else if (arg == "--no-config-write")
    {
      cli.NoConfigWrite = true;
    }
    else if (arg == "--ticks")
    {
      TakeValue(i, argc, argv, "--ticks", ParseU64, cli.Ticks, cli.Errors);
    }
    else if (arg == "--tick-hz")
    {
      TakeValue(i, argc, argv, "--tick-hz", ParseDouble, cli.TickHz, cli.Errors);
    }
    else if (arg == "--width")
    {
      TakeValue(i, argc, argv, "--width", ParseI32, cli.Width, cli.Errors);
    }
    else if (arg == "--height")
    {
      TakeValue(i, argc, argv, "--height", ParseI32, cli.Height, cli.Errors);
    }
    else if (arg == "--config")
    {
      TakeValue(
        i, argc, argv, "--config", [](std::string_view v) -> std::optional<std::string> { return std::string(v); },
        cli.ConfigPath, cli.Errors);
    }
    else if (arg == "--crash-dir")
    {
      TakeValue(
        i, argc, argv, "--crash-dir", [](std::string_view v) -> std::optional<std::string> { return std::string(v); },
        cli.CrashDir, cli.Errors);
    }
    else if (arg == "--resources")
    {
      TakeValue(
        i, argc, argv, "--resources", [](std::string_view v) -> std::optional<std::string> { return std::string(v); },
        cli.ResourceDir, cli.Errors);
    }
    else if (arg == "--validate-assets")
    {
      cli.ValidateAssets = true;
    }
    else if (arg == "--inspect")
    {
      std::string path;
      if (TakeValue(
            i, argc, argv, "--inspect", [](std::string_view v) -> std::optional<std::string> { return std::string(v); },
            path, cli.Errors))
      {
        cli.InspectPaths.push_back(std::move(path));
      }
    }
    else
    {
      cli.Errors.push_back(std::format("unknown argument '{}'", arg));
    }
  }
  return cli;
}

/// @brief Flush stdout so report lines keep their order relative to logger
/// output on stderr (stdout is block-buffered when piped, stderr is not).
void FlushReport()
{
  std::fflush(stdout);
}

/// @brief Print the root's findings (the same notes the logger received).
void PrintRootInfo(const lod::res::RootInfo& info)
{
  std::println("root: {}", info.Path.string());
  for (const std::string& note : info.Notes)
    std::println("  {}", note);
  FlushReport();
}

/// @brief `--inspect <path>`: validate individual resources and print what they are.
int InspectResources(const lod::res::ResourceManager& manager, const std::vector<std::string>& paths)
{
  int failures = 0;
  for (const std::string& raw : paths)
  {
    const std::filesystem::path relative(raw);
    auto inspected = manager.Inspect(relative);
    if (!inspected)
    {
      // The manager logged the specific cause (including the psx::Error name).
      std::println(stderr, "{}: FAILED ({})", relative.string(), std::string(lod::StatusName(inspected.error())));
      ++failures;
      continue;
    }
    std::println("{}: {}", inspected->RelPath.string(), inspected->Summary);
    FlushReport();
  }
  return failures == 0 ? AssetExitOk : AssetExitValidationFailed;
}

/// @brief `--validate-assets`: discover the tree, then validate every file
/// through the matching decoder. One file is resident at a time (Inspect()
/// releases its blob), so a full tree does not need full-tree memory.
int ValidateAssets(const lod::res::ResourceManager& manager)
{
  auto scan = manager.Scan();
  if (!scan)
  {
    std::println(stderr, "scan failed: {}", std::string(lod::StatusName(scan.error())));
    return AssetExitNoRoot;
  }

  FlushReport();
  std::println("discovered {} file(s), {} B in {} director(ies){}", scan->Entries.size(), scan->TotalBytes,
               scan->Directories, scan->Truncated ? " (scan truncated at the entry limit)" : "");

  std::map<std::string_view, std::size_t> byKind;
  std::vector<std::pair<std::filesystem::path, lod::Status>> failures;
  for (const lod::res::ResourceEntry& entry : scan->Entries)
  {
    auto inspected = manager.Inspect(entry.RelPath);
    if (!inspected)
    {
      failures.emplace_back(entry.RelPath, inspected.error());
      continue;
    }
    ++byKind[lod::res::KindName(inspected->Kind)];
  }

  for (const auto& [kind, count] : byKind)
    std::println("  {}: {}", kind, count);
  FlushReport();

  if (failures.empty())
  {
    std::println("validated {} resource(s), 0 failures", scan->Entries.size());
    return AssetExitOk;
  }

  constexpr std::size_t MaxReportedFailures = 20;
  std::println(stderr, "validated {} resource(s), {} failure(s):", scan->Entries.size(), failures.size());
  for (std::size_t i = 0; i < failures.size() && i < MaxReportedFailures; ++i)
    std::println(stderr, "  {}: {}", failures[i].first.string(), std::string(lod::StatusName(failures[i].second)));
  if (failures.size() > MaxReportedFailures)
    std::println(stderr, "  ... and {} more", failures.size() - MaxReportedFailures);
  FlushReport();
  return AssetExitValidationFailed;
}

} // namespace

int main(int argc, char** argv)
{
  const CLIOptions cli = ParseArgs(argc, argv);
  if (cli.Help)
  {
    PrintUsage();
    return 0;
  }
  if (!cli.Errors.empty())
  {
    for (const std::string& error : cli.Errors)
      std::println(stderr, "lodsc: {}", error);
    PrintUsage();
    return 2;
  }

  // 1. Runtime directories (env overrides → bundle → user data → exe dir).
  const std::filesystem::path exePath = argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path();
  const lod::fs::Paths paths = lod::fs::ResolvePaths(exePath);
  const std::filesystem::path configPath =
    cli.ConfigPath ? std::filesystem::path(*cli.ConfigPath) : paths.ConfigDir / lod::ConfigFileName;
  const std::filesystem::path crashDir =
    cli.CrashDir.empty() ? paths.DataDir / "crashes" : std::filesystem::path(cli.CrashDir);

  // 2. App (crash diagnostics need the directory before anything else runs).
  game::GameApp app(crashDir.string());

  // 2b. Asset commands are read-only filesystem operations: no window, no loop,
  // and no config file created on the way past.
  const bool assetCommand = cli.ValidateAssets || !cli.InspectPaths.empty();

  // 3. Configuration: file → CLI overrides → validate. A config that cannot be
  //    read is degraded to defaults, never fatal (error-handling.md §5).
  std::error_code ec;
  const bool configExists = std::filesystem::exists(configPath, ec);
  lod::Config config;
  bool configLoaded = true;
  if (auto loaded = lod::LoadConfig(configPath, app.GetLog()); loaded)
  {
    config = std::move(*loaded);
  }
  else
  {
    app.GetLog().Warn("config", "continuing with defaults ({}: {})", configPath.string(),
                      lod::StatusName(loaded.error()));
    config = {};
    configLoaded = false;
  }
  if (cli.Headless)
    config.Headless = true;
  else if (cli.Windowed)
    config.Headless = false;
  if (cli.TickHz)
    config.TickHz = *cli.TickHz;
  if (cli.Width)
    config.WindowWidth = *cli.Width;
  if (cli.Height)
    config.WindowHeight = *cli.Height;
  lod::ValidateConfig(config, app.GetLog());

  // First run creates the file so the user has something to edit. A config we
  // could not read is never rewritten here (that would destroy content the
  // user may still be able to repair), and --no-config-write keeps the tree
  // pristine for tests.
  if (!cli.NoConfigWrite && !assetCommand && !configExists && configLoaded)
  {
    if (auto saved = lod::SaveConfig(configPath, config, app.GetLog()); !saved)
    {
      app.GetLog().Warn("config", "defaults not persisted ({}): {}", lod::StatusName(saved.error()),
                        configPath.string());
    }
  }

  app.GetLog().Info("app", "config: {}Hz, {} mode, {}x{}, crash dir {}", config.TickHz,
                    config.Headless ? "headless" : "windowed", config.WindowWidth, config.WindowHeight,
                    crashDir.string());

  // 3b. Asset commands. Resource-root precedence is --resources > config
  //     resource_dir > resolved default (which itself honours SC_RESOURCE_DIR) —
  //     the same CLI > config > default order as every other setting.
  if (assetCommand)
  {
    std::filesystem::path resourceRoot = paths.ResourceDir;
    std::string_view rootSource = "resolved default";
    if (!config.ResourceDir.empty())
    {
      resourceRoot = std::filesystem::path(config.ResourceDir);
      rootSource = "config resource_dir";
    }
    if (cli.ResourceDir)
    {
      resourceRoot = std::filesystem::path(*cli.ResourceDir);
      rootSource = "--resources";
    }
    app.GetLog().Info("resources", "root {} (from {})", resourceRoot.string(), std::string(rootSource));

    lod::res::ResourceManager::Options assetOptions;
    assetOptions.Log = &app.GetLog();
    auto manager = lod::res::ResourceManager::Open(resourceRoot, assetOptions);
    if (!manager)
    {
      std::println(stderr, "resource root unusable ({}): {}", resourceRoot.string(),
                   std::string(lod::StatusName(manager.error())));
      return AssetExitNoRoot;
    }
    if (auto info = manager->Validate(); info)
      PrintRootInfo(*info);
    return cli.InspectPaths.empty() ? ValidateAssets(*manager) : InspectResources(*manager, cli.InspectPaths);
  }

  // 4. Platform subsystem (window + OS event queue). The session mode is
  //    declared to subsystems from the *resolved* config, not the raw flags.
  app.SetSessionMode(config.Headless ? "headless" : "windowed");
  lod::Platform::Options platformOptions;
  platformOptions.Headless = config.Headless;
  platformOptions.Width = config.WindowWidth;
  platformOptions.Height = config.WindowHeight;
  platformOptions.Title = config.WindowTitle;
  platformOptions.MenuDeadzone = config.MenuDeadzone;
  platformOptions.MovementDeadzone = config.MovementDeadzone;
  platformOptions.SteamGuard = config.SteamGuard;
  platformOptions.ReceiveInputOnInactive = config.ReceiveInputOnInactive;
  app.AddSubsystem(std::make_unique<lod::Platform>(std::move(platformOptions)));

  // 5. Loop budget: the tick rate is the sim clock (engine-design.md §3); a
  //    real window session runs until the window closes.
  lod::RunOptions runOptions;
  runOptions.TickHz = config.TickHz;
  if (cli.Ticks)
  {
    runOptions.MaxTicks = *cli.Ticks == 0 ? std::nullopt : std::optional<std::uint64_t>(*cli.Ticks);
  }
  else
  {
    // Bare invocation stays a lifecycle smoke test; a window stays open.
    runOptions.MaxTicks = config.Headless ? std::optional<std::uint64_t>(1) : std::nullopt;
  }

  // 6. Run: init (diagnostics → platform) → loop → reverse teardown.
  const auto code = app.Run(runOptions);
  return static_cast<int>(code);
}
