#pragma once
/// @file fs.h
/// @brief Runtime directory resolution and explicit file access
/// (engine-design.md §5 resource layout; error-handling.md §2 io codes).
/// No hardcoded development paths: everything derives from the executable
/// location or SC_* environment overrides.

#include "lod/status.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lod::fs
{

/// @brief Resolved runtime directories.
struct Paths
{
  std::filesystem::path ResourceDir; ///< Read-only game resources.
  std::filesystem::path ConfigDir;   ///< User-editable settings.
  std::filesystem::path DataDir;     ///< Writable saves/crash logs.
};

/// @brief Resolve runtime directories. Precedence: SC_RESOURCE_DIR /
/// SC_CONFIG_DIR / SC_DATA_DIR overrides; otherwise bundle-relative defaults
/// derived from the executable path (`<bundle>/Contents/Resources` when
/// present), then the platform user-data location, then the executable
/// directory as the last fallback.
/// @param exePath Path of the running executable (argv[0] is sufficient).
/// @return Always-populated paths; existence is **not** verified (creating a
///         missing directory is the caller's job — see WriteFileText).
/// @note Thread: main thread (reads the process environment).
Paths ResolvePaths(const std::filesystem::path& exePath);

/// @brief Read a whole file as text (binary-safe: NUL bytes are preserved).
/// @param path File to read.
/// @return Contents (possibly empty), or Status::NotFound (absent, not a
///         regular file, or the stat failed), Status::IOOpen (exists but could
///         not be opened), Status::IORead (opened but the read failed or came
///         up short).
/// @note Thread: safe from any thread (no shared state).
[[nodiscard]] Result<std::string> ReadFileText(const std::filesystem::path& path);

/// @brief Write text, creating parent directories (no atomicity guarantee —
/// write-temp-then-rename arrives with the save system).
/// @param path Destination file.
/// @param text Bytes to write.
/// @return Status::Ok, or Status::IOWrite (directory creation, open, write,
///         flush, or close failed).
/// @note Thread: safe from any thread for distinct paths; concurrent writers
///       to the same path are undefined.
[[nodiscard]] StatusResult WriteFileText(const std::filesystem::path& path, std::string_view text);

/// @brief Read a whole file as bytes, refusing anything over maxBytes.
///
/// Asset loading must not be able to reserve an arbitrary amount of memory on
/// an untrusted tree, so the ceiling is part of the read (checked against the
/// reported size before the buffer is allocated, and again while streaming).
///
/// @param path    File to read.
/// @param maxBytes Largest accepted file size; 0 means "no ceiling".
/// @return The bytes (possibly empty), or Status::NotFound (absent or not a
///         regular file), Status::IOOpen (could not open), Status::IORead (read
///         failed or came up short), Status::IOTooLarge (over maxBytes).
/// @note Thread: safe from any thread (no shared state).
[[nodiscard]] Result<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path,
                                                              std::uintmax_t maxBytes = 0);

/// @brief Read at most maxBytes from the start of a file.
///
/// Distinct from ReadFileBytes: there the ceiling *rejects* an oversized file,
/// here it *truncates* the read. Header sniffing needs the latter — a 4 MiB
/// asset still has a readable first 16 bytes.
///
/// @param path     File to read.
/// @param maxBytes Maximum number of bytes to read (must be > 0).
/// @return Up to maxBytes bytes (fewer for a short file, empty for an empty
///         file), or Status::NotFound (absent or not a regular file),
///         Status::IOOpen (could not open), Status::IORead (read failed).
/// @note Thread: safe from any thread (no shared state).
[[nodiscard]] Result<std::vector<std::uint8_t>> ReadFilePrefix(const std::filesystem::path& path,
                                                               std::uintmax_t maxBytes);

} // namespace lod::fs
