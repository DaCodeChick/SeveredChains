/// @file config.cpp
/// @brief "key value" runtime config reader/writer (config.h). Line-oriented,
/// comment-tolerant, and total: every malformed input maps to a documented
/// recovery (default + Warning) rather than an error, so a corrupt config can
/// never prevent startup — only an unreadable file reports a Status.
#include "lod/config.h"

#include "lod/fs.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lod
{
namespace
{

constexpr std::string_view Marker = "config";

/// @brief Config keys (lowercase; the file format's own namespace).
/// @{
constexpr std::string_view KeyTickHz = "tick_hz";
constexpr std::string_view KeyHeadless = "headless";
constexpr std::string_view KeyWindowWidth = "window_width";
constexpr std::string_view KeyWindowHeight = "window_height";
constexpr std::string_view KeyWindowTitle = "window_title";
constexpr std::string_view KeyResourceDir = "resource_dir";
constexpr std::string_view KeyMenuDeadzoneInner = "menu_deadzone_inner";
constexpr std::string_view KeyMenuDeadzoneOuter = "menu_deadzone_outer";
constexpr std::string_view KeyMovementDeadzoneInner = "movement_deadzone_inner";
constexpr std::string_view KeyMovementDeadzoneOuter = "movement_deadzone_outer";
constexpr std::string_view KeySteamInputGuard = "steam_input_guard";
constexpr std::string_view KeyReceiveInputInactive = "receive_input_on_inactive_window";
/// @}

constexpr std::string_view SpaceChars = " \t\r\n";

std::string_view Trim(std::string_view text)
{
  const auto begin = text.find_first_not_of(SpaceChars);
  if (begin == std::string_view::npos)
    return {};
  const auto end = text.find_last_not_of(SpaceChars);
  return text.substr(begin, end - begin + 1);
}

/// @brief ASCII-lowercase copy (keys are matched case-insensitively; the file
/// format is ASCII, so a locale-independent fold is the correct one).
std::string Lower(std::string_view text)
{
  std::string out;
  out.reserve(text.size());
  for (const char c : text)
    out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
  return out;
}

std::optional<double> ParseDouble(std::string_view text)
{
  const std::string owned(text);
  double value = 0.0;
  const char* first = owned.data();
  const char* last = owned.data() + owned.size();
  const auto res = std::from_chars(first, last, value);
  if (res.ec != std::errc{} || res.ptr != last)
    return std::nullopt;
  return value;
}

std::optional<int> ParseInt(std::string_view text)
{
  const std::string owned(text);
  int value = 0;
  const char* first = owned.data();
  const char* last = owned.data() + owned.size();
  const auto res = std::from_chars(first, last, value);
  if (res.ec != std::errc{} || res.ptr != last)
    return std::nullopt;
  return value;
}

/// @brief Boolean grammar for the config format: true/false, 1/0, yes/no,
/// on/off (case-insensitive).
std::optional<bool> ParseBool(std::string_view text)
{
  const std::string key = Lower(text);
  if (key == "true" || key == "1" || key == "yes" || key == "on")
    return true;
  if (key == "false" || key == "0" || key == "no" || key == "off")
    return false;
  return std::nullopt;
}

/// @brief Steam Input guard names for the config file.
/// @return The guard the text names, or nullopt when it names neither.
std::optional<SteamInputGuard> ParseGuard(std::string_view text)
{
  const std::string key = Lower(text);
  if (key == "none" || key == "off" || key == "false" || key == "0")
    return SteamInputGuard::Off;
  if (key == "ignore_when_gamepad_used" || key == "gamepad" || key == "true" || key == "1")
    return SteamInputGuard::IgnoreWhenGamepadUsed;
  return std::nullopt;
}

/// @brief Inverse of ParseGuard, for saving.
std::string_view GuardName(SteamInputGuard guard)
{
  return guard == SteamInputGuard::Off ? "none" : "ignore_when_gamepad_used";
}

/// @brief One "key value" line of the default-valued key table.
struct LineView
{
  std::string_view Key;
  std::string_view Value;
};

/// @brief Split a line into key + value at the first whitespace run. The value
/// is the remainder of the line (spaces preserved) so titles may contain
/// spaces; the only truncation is trailing whitespace.
std::optional<LineView> SplitLine(std::string_view line)
{
  const std::string_view body = Trim(line);
  if (body.empty() || body.front() == '#')
    return std::nullopt;
  const auto gap = body.find_first_of(" \t");
  if (gap == std::string_view::npos)
    return LineView{body, {}};
  return LineView{body.substr(0, gap), Trim(body.substr(gap))};
}

/// @brief True when `value` is a non-empty single line (safe to embed in the
/// line-based format).
bool IsSingleLine(std::string_view value)
{
  return !value.empty() && value.find_first_of("\r\n") == std::string_view::npos;
}

/// @brief Split text into lines ('\n' separated, '\r' tolerated). Config files
/// are tiny, so materialising the view list is cheaper than an iterator.
std::vector<std::string_view> SplitLines(std::string_view text)
{
  std::vector<std::string_view> lines;
  std::size_t start = 0;
  while (true)
  {
    const auto nl = text.find('\n', start);
    lines.push_back(text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
    if (nl == std::string_view::npos)
      break;
    start = nl + 1;
  }
  return lines;
}

std::string Serialize(const Config& config)
{
  return std::format(
           "# Severed Chains Native — runtime configuration\n"
           "# Lines are 'key value'; '#' starts a comment.\n"
           "{} {:.6g}\n"
           "{} {}\n"
           "{} {}\n"
           "{} {}\n"
           "{} {}\n"
           "{} {:.6g}\n"
           "{} {:.6g}\n"
           "{} {:.6g}\n"
           "{} {:.6g}\n"
           "{} {}\n"
           "{} {}\n",
           KeyTickHz, config.TickHz, KeyHeadless, config.Headless ? "true" : "false", KeyWindowWidth,
           config.WindowWidth, KeyWindowHeight, config.WindowHeight, KeyWindowTitle, config.WindowTitle,
           KeyMenuDeadzoneInner, config.MenuDeadzone.Inner, KeyMenuDeadzoneOuter, config.MenuDeadzone.Outer,
           KeyMovementDeadzoneInner, config.MovementDeadzone.Inner, KeyMovementDeadzoneOuter,
           config.MovementDeadzone.Outer, KeySteamInputGuard, GuardName(config.SteamGuard), KeyReceiveInputInactive,
           config.ReceiveInputOnInactive ? "true" : "false")
         // The key is omitted rather than written empty: an absent resource_dir and an
         // empty one mean the same thing, and the file stays readable.
         + (config.ResourceDir.empty() ? std::string() : std::format("{} {}\n", KeyResourceDir, config.ResourceDir));
}

} // namespace

void ValidateConfig(Config& config, Logger& log)
{
  if (!std::isfinite(config.TickHz) || config.TickHz < MinTickHz || config.TickHz > MaxTickHz)
  {
    log.Warn(Marker, "tick_hz {} out of range [{}, {}] — using default {}", config.TickHz, MinTickHz, MaxTickHz,
             Config{}.TickHz);
    config.TickHz = Config{}.TickHz;
  }
  if (config.WindowWidth < MinWindowSize || config.WindowWidth > MaxWindowSize)
  {
    log.Warn(Marker, "window_width {} out of range [{}, {}] — clamped", config.WindowWidth, MinWindowSize,
             MaxWindowSize);
    config.WindowWidth = std::clamp(config.WindowWidth, MinWindowSize, MaxWindowSize);
  }
  if (config.WindowHeight < MinWindowSize || config.WindowHeight > MaxWindowSize)
  {
    log.Warn(Marker, "window_height {} out of range [{}, {}] — clamped", config.WindowHeight, MinWindowSize,
             MaxWindowSize);
    config.WindowHeight = std::clamp(config.WindowHeight, MinWindowSize, MaxWindowSize);
  }
  // Deadzone regions: a region outside 0..1 or with the outer edge at/inside
  // the inner edge is unusable (it either reads as constant full deflection or
  // divides by zero — see input.h), so it is replaced by the default pair
  // rather than clamped into something the player did not ask for.
  const auto repairRegion = [&](DeadzonePair& region, std::string_view name, const DeadzonePair& fallback)
  {
    const bool outOfRange = !std::isfinite(region.Inner) || !std::isfinite(region.Outer) || region.Inner < 0.0f ||
                            region.Inner > 1.0f || region.Outer < 0.0f || region.Outer > 1.0f;
    const bool noSpan = region.Outer <= region.Inner;
    if (!outOfRange && !noSpan)
      return;
    log.Warn(Marker, "{} is not a usable deadzone region (inner {} outer {}) — using defaults ({} {})", name,
             region.Inner, region.Outer, fallback.Inner, fallback.Outer);
    region = fallback;
  };
  repairRegion(config.MenuDeadzone, "menu_deadzone", Config{}.MenuDeadzone);
  repairRegion(config.MovementDeadzone, "movement_deadzone", Config{}.MovementDeadzone);

  // A resource root is either a usable single-line path or unset (""), which
  // means "use the resolved default". A multi-line value would corrupt the
  // format on the next save, so it degrades to unset rather than being written.
  if (const std::string_view trimmed = Trim(config.ResourceDir); !trimmed.empty() && !IsSingleLine(trimmed))
  {
    log.Warn(Marker, "resource_dir spans multiple lines — falling back to the resolved default");
    config.ResourceDir.clear();
  }
  else
  {
    config.ResourceDir = std::string(trimmed);
  }
  // An empty title is legal to SDL but useless; a multi-line title would
  // corrupt the format on the next save. Both fall back to the default.
  if (const std::string_view trimmed = Trim(config.WindowTitle); !IsSingleLine(trimmed))
  {
    log.Warn(Marker, "window_title is empty or spans multiple lines — using default '{}'", Config{}.WindowTitle);
    config.WindowTitle = Config{}.WindowTitle;
  }
  else if (trimmed.size() != config.WindowTitle.size())
  {
    config.WindowTitle = std::string(trimmed);
  }
}

Result<Config> LoadConfig(const std::filesystem::path& path, Logger& log)
{
  Config config;
  auto text = fs::ReadFileText(path);
  if (!text)
  {
    switch (text.error())
    {
      case Status::NotFound:
        // Absent (or replaced by a directory): first run, or a deleted file.
        log.Info(Marker, "no config at '{}' — using defaults", path.string());
        ValidateConfig(config, log);
        return config;
      case Status::IOOpen:
      case Status::IORead:
        // The file is there but unreadable: report it, the caller decides
        // whether defaults are acceptable (error-handling.md §5).
        log.Warn(Marker, "config '{}' unreadable: {}", path.string(), StatusName(text.error()));
        return std::unexpected(text.error());
      default:
        log.Warn(Marker, "config '{}' read failed: {}", path.string(), StatusName(text.error()));
        return std::unexpected(text.error());
    }
  }

  std::size_t lineNumber = 0;
  for (std::string_view rawLine : SplitLines(*text))
  {
    ++lineNumber;
    const auto line = SplitLine(rawLine);
    if (!line)
      continue;
    const std::string key = Lower(line->Key);

    // Each key applies its parsed value or keeps the default it already has;
    // a bad value never discards the rest of the file.
    if (key == KeyTickHz)
    {
      if (const auto value = ParseDouble(line->Value))
      {
        config.TickHz = *value;
      }
      else
      {
        log.Warn(Marker, "{}:{}: bad tick_hz '{}' — keeping {}", path.string(), lineNumber, line->Value,
                 Config{}.TickHz);
      }
    }
    else if (key == KeyHeadless)
    {
      if (const auto value = ParseBool(line->Value))
      {
        config.Headless = *value;
      }
      else
      {
        log.Warn(Marker, "{}:{}: bad headless '{}' — keeping {}", path.string(), lineNumber, line->Value,
                 Config{}.Headless ? "true" : "false");
      }
    }
    else if (key == KeyWindowWidth)
    {
      if (const auto value = ParseInt(line->Value))
      {
        config.WindowWidth = *value;
      }
      else
      {
        log.Warn(Marker, "{}:{}: bad window_width '{}' — keeping {}", path.string(), lineNumber, line->Value,
                 Config{}.WindowWidth);
      }
    }
    else if (key == KeyWindowHeight)
    {
      if (const auto value = ParseInt(line->Value))
      {
        config.WindowHeight = *value;
      }
      else
      {
        log.Warn(Marker, "{}:{}: bad window_height '{}' — keeping {}", path.string(), lineNumber, line->Value,
                 Config{}.WindowHeight);
      }
    }
    else if (key == KeyWindowTitle)
    {
      if (IsSingleLine(Trim(line->Value)))
        config.WindowTitle = std::string(Trim(line->Value));
      else
        log.Warn(Marker, "{}:{}: bad window_title — keeping '{}'", path.string(), lineNumber, config.WindowTitle);
    }
    else if (key == KeyResourceDir)
    {
      if (IsSingleLine(Trim(line->Value)))
        config.ResourceDir = std::string(Trim(line->Value));
      else
        log.Warn(Marker, "{}:{}: bad resource_dir — keeping the resolved default", path.string(), lineNumber);
    }
    else if (key == KeyMenuDeadzoneInner || key == KeyMenuDeadzoneOuter || key == KeyMovementDeadzoneInner ||
             key == KeyMovementDeadzoneOuter)
    {
      const auto value = ParseDouble(line->Value);
      const bool outer = key == KeyMenuDeadzoneOuter || key == KeyMovementDeadzoneOuter;
      DeadzonePair& region = key.starts_with("menu") ? config.MenuDeadzone : config.MovementDeadzone;
      if (value)
        (outer ? region.Outer : region.Inner) = static_cast<float>(*value);
      else
        log.Warn(Marker, "{}:{}: bad {} '{}' — keeping the default region", path.string(), lineNumber, line->Key,
                 line->Value);
    }
    else if (key == KeySteamInputGuard)
    {
      if (const auto value = ParseGuard(line->Value))
        config.SteamGuard = *value;
      else
        log.Warn(Marker, "{}:{}: bad {} '{}' — keeping '{}'", path.string(), lineNumber, line->Key, line->Value,
                 GuardName(Config{}.SteamGuard));
    }
    else if (key == KeyReceiveInputInactive)
    {
      if (const auto value = ParseBool(line->Value))
        config.ReceiveInputOnInactive = *value;
      else
        log.Warn(Marker, "{}:{}: bad {} '{}' — keeping {}", path.string(), lineNumber, line->Key, line->Value,
                 Config{}.ReceiveInputOnInactive ? "true" : "false");
    }
    else
    {
      // Unknown keys are kept-but-ignored: an older/newer writer must not
      // invalidate the settings it shares with us.
      log.Warn(Marker, "{}:{}: unknown config key '{}' — ignored", path.string(), lineNumber, line->Key);
    }
  }

  ValidateConfig(config, log);
  return config;
}

StatusResult SaveConfig(const std::filesystem::path& path, const Config& config, Logger& log)
{
  if (!IsSingleLine(config.WindowTitle))
  {
    log.Warn(Marker, "refusing to write config: window_title is empty or spans multiple lines");
    return std::unexpected(Status::InvalidArgument);
  }
  if (auto r = fs::WriteFileText(path, Serialize(config)); !r)
  {
    log.Warn(Marker, "could not write config to '{}': {}", path.string(), StatusName(r.error()));
    return r;
  }
  return {};
}

} // namespace lod
