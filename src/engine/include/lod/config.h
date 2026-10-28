#pragma once
/// @file config.h
/// @brief Runtime configuration for currently-live systems only (foundation
/// milestone): tick rate, window mode/size, headless, resource root. Simple "key value" text
/// format — no serialization framework. Malformed content is recovered
/// per-key (defaults + warning); the *caller* decides fatality of an I/O
/// failure (error-handling.md §5: "Config corrupt → recreate defaults").
///
/// The Tag-serialized `config.dcnf` registry of typed entries
/// (engine-design.md §5) arrives with the asset/config milestone; this file is
/// deliberately named differently (`config.txt`) so a future Tag reader is
/// never handed a text file under a Tag extension.

#include "lod/input.h"
#include "lod/log.h"
#include "lod/status.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace lod
{

/// @brief Name of the runtime config file inside the config directory
/// (lod::fs::Paths::configDir).
inline constexpr std::string_view ConfigFileName = "config.txt";

/// @brief Validation bounds — named constants, no magic numbers in logic
/// (cpp.md §6). MinWindowSize is the smallest window the render layers are
/// expected to work with; MaxWindowSize is the largest sane desktop window.
/// @{
inline constexpr double MinTickHz = 10.0;
inline constexpr double MaxTickHz = 1000.0;
inline constexpr int MinWindowSize = 320;
inline constexpr int MaxWindowSize = 16384;
/// @}

/// @brief Validated runtime settings.
struct Config
{
  double TickHz = 60.0;                                                ///< Fixed sim rate (engine-design.md §3).
  bool Headless = true;                                                ///< Dummy video driver (CI default).
  int WindowWidth = 1280;                                              ///< Window width in pixels (clamped on load).
  int WindowHeight = 720;                                              ///< Window height in pixels (clamped on load).
  std::string WindowTitle = "Severed Chains";                          ///< Window title (product name).
  std::string ResourceDir;                                             ///< Game resource root; "" = the resolved
                                                                       ///< default (lod::fs::ResolvePaths, which
                                                                       ///< honours SC_RESOURCE_DIR).
  DeadzonePair MenuDeadzone = DefaultMenuDeadzone;                     ///< Menu/UI stick region.
  DeadzonePair MovementDeadzone = DefaultMovementDeadzone;             ///< Movement stick region.
  SteamInputGuard SteamGuard = SteamInputGuard::IgnoreWhenGamepadUsed; ///< Steam Input workaround.
  bool ReceiveInputOnInactive = false;                                 ///< Feed input to an unfocused window.
};

/// @brief Load config from a "key value" file (`#` starts a comment).
///
/// Recovery policy — malformed content never fails the load: absent file →
/// defaults (Info log); unparseable value or bad type → that key keeps its
/// default with a Warning; unknown key → Warning (forward compatibility);
/// out-of-range value → clamped by ValidateConfig. Keys are matched
/// case-insensitively; the value is the rest of the line, so titles may
/// contain spaces.
///
/// @param path Config file location (may not exist).
/// @param log  Process logger for recovery messages.
/// @return Usable Config (defaults for anything unreadable), or
///         Status::IOOpen if a file that exists could not be opened for
///         reading — an environment problem the caller may still choose to
///         degrade from (defaults) or treat as fatal.
/// @note Thread: main thread (boot).
[[nodiscard]] Result<Config> LoadConfig(const std::filesystem::path& path, Logger& log);

/// @brief Persist config in the format LoadConfig reads (round-trip identity).
/// Parent directories are created. A title containing a newline is rejected:
/// it would corrupt the line-based format.
///
/// @param path   Destination file (typically configDir / ConfigFileName).
/// @param config Values to write.
/// @param log    Process logger for the failure message.
/// @return Status::Ok, or Status::IOWrite when the file cannot be written
///         (including a rejected title).
/// @note Thread: main thread (boot / shutdown).
[[nodiscard]] StatusResult SaveConfig(const std::filesystem::path& path, const Config& config, Logger& log);

/// @brief Clamp/validate values in place; warns on each correction.
/// @param config Settings to sanitize (mutated).
/// @param log    Process logger for correction messages.
/// @note Thread: main thread (boot).
void ValidateConfig(Config& config, Logger& log);

} // namespace lod
