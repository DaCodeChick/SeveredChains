/// @file fs.cpp
/// @brief Runtime directory resolution + non-throwing file access. All
/// filesystem operations use the `std::error_code` overloads: the throwing
/// ones raise `std::filesystem_error`, which this codebase does not use for
/// control flow (cpp.md §4), and `-fno-exceptions` builds must compile too.
#include "lod/fs.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace lod::fs
{
namespace
{

/// @brief Read an environment variable as a view (empty when unset/empty).
std::string_view EnvValue(const char* name)
{
  const char* value = std::getenv(name);
  return value != nullptr ? std::string_view(value) : std::string_view();
}

/// @brief Environment override for a directory ("" treated as unset).
std::filesystem::path EnvDir(const char* name)
{
  const std::string_view value = EnvValue(name);
  return value.empty() ? std::filesystem::path() : std::filesystem::path(value);
}

/// @brief Per-user writable base (platform user-data location), empty when no
/// home directory is known — callers then fall back to the executable dir.
std::filesystem::path UserBaseDir()
{
  const std::filesystem::path home(EnvValue("HOME"));
  std::error_code ec;
  // Non-throwing overload: filesystem_error is never control flow here.
  if (home.empty() || !std::filesystem::is_directory(home, ec))
    return {};
#if defined(__APPLE__)
  return home / "Library" / "Application Support" / "SeveredChainsNative";
#else
  if (const auto xdg = EnvDir("XDG_DATA_HOME"); !xdg.empty())
    return xdg / "severedchains";
  return home / ".local" / "share" / "severedchains";
#endif
}

} // namespace

Paths ResolvePaths(const std::filesystem::path& exePath)
{
  Paths paths;
  const std::filesystem::path exeDir = exePath.parent_path();

  paths.ResourceDir = EnvDir("SC_RESOURCE_DIR");
  if (paths.ResourceDir.empty())
  {
    // Bundled layout: <bundle>/Contents/Resources sits above
    // <bundle>/Contents/MacOS; a flat layout uses the executable dir.
    const auto bundled = exeDir.parent_path() / "Resources";
    std::error_code ec;
    paths.ResourceDir = std::filesystem::is_directory(bundled, ec) ? bundled : exeDir;
  }

  const std::filesystem::path userBase = UserBaseDir();
  paths.ConfigDir = EnvDir("SC_CONFIG_DIR");
  if (paths.ConfigDir.empty())
    paths.ConfigDir = userBase.empty() ? exeDir : userBase;
  paths.DataDir = EnvDir("SC_DATA_DIR");
  if (paths.DataDir.empty())
    paths.DataDir = userBase.empty() ? exeDir : userBase;
  return paths;
}

Result<std::string> ReadFileText(const std::filesystem::path& path)
{
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec))
  {
    // Absent, a directory, or the stat itself failed: nothing to read.
    return std::unexpected(Status::NotFound);
  }
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    return std::unexpected(Status::IOOpen);
  const auto declaredSize = in.tellg();
  if (declaredSize < 0)
    return std::unexpected(Status::IORead);
  // Read the declared size, then trust gcount(): a file that shrank (or a
  // special file) yields a short read rather than garbage. These files are
  // small by contract (config, crash logs), so sizing up front is fine.
  std::string text(static_cast<std::size_t>(declaredSize), '\0');
  if (declaredSize > 0)
  {
    in.seekg(0, std::ios::beg);
    in.read(text.data(), static_cast<std::streamsize>(declaredSize));
    text.resize(static_cast<std::size_t>(in.gcount()));
    if (text.size() != static_cast<std::size_t>(declaredSize))
      return std::unexpected(Status::IORead);
  }
  return text;
}

Result<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path, std::uintmax_t maxBytes)
{
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec))
  {
    // Absent, a directory, or the stat itself failed: nothing to read.
    return std::unexpected(Status::NotFound);
  }

  const std::uintmax_t reportedSize = std::filesystem::file_size(path, ec);
  if (ec)
    return std::unexpected(Status::IORead);
  if (maxBytes != 0 && reportedSize > maxBytes)
    return std::unexpected(Status::IOTooLarge);

  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    return std::unexpected(Status::IOOpen);
  const auto declaredSize = in.tellg();
  if (declaredSize < 0)
    return std::unexpected(Status::IORead);
  if (maxBytes != 0 && static_cast<std::uintmax_t>(declaredSize) > maxBytes)
    return std::unexpected(Status::IOTooLarge);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(declaredSize));
  if (declaredSize > 0)
  {
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(declaredSize));
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    if (bytes.size() != static_cast<std::size_t>(declaredSize))
    {
      // The file shrank or the stream failed mid-read: hand back no partial
      // asset, since a truncated asset looks exactly like a malformed one.
      return std::unexpected(Status::IORead);
    }
  }
  return bytes;
}

Result<std::vector<std::uint8_t>> ReadFilePrefix(const std::filesystem::path& path, std::uintmax_t maxBytes)
{
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec))
    return std::unexpected(Status::NotFound);

  std::ifstream in(path, std::ios::binary);
  if (!in)
    return std::unexpected(Status::IOOpen);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(maxBytes));
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(maxBytes));
  // A short file is not an error here: the caller asked for "up to maxBytes".
  bytes.resize(static_cast<std::size_t>(in.gcount()));
  if (!in && !in.eof())
    return std::unexpected(Status::IORead);
  return bytes;
}

StatusResult WriteFileText(const std::filesystem::path& path, std::string_view text)
{
  std::error_code ec;
  if (path.has_parent_path() && !path.parent_path().empty())
  {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
      return std::unexpected(Status::IOWrite);
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out)
    return std::unexpected(Status::IOWrite);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.flush();
  out.close();
  if (!out.good())
    return std::unexpected(Status::IOWrite);
  return {};
}

} // namespace lod::fs
