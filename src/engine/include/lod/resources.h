#pragma once
/// @file resources.h
/// @brief Resource discovery, ownership and validation for the extracted game
/// tree (architecture/resource-management.md, research/asset-pipeline.md §1-§4).
///
/// The tree being read is the *extraction cache* the pipeline builds from the
/// player's discs — never the discs themselves (asset-pipeline.md §4 keeps
/// source, cache and mod overrides as three separate things). Its shape:
///
/// ```text
/// <Root>/version                 extraction format version ("5")
/// <Root>/SECT/DRGNn.BIN/<dir>/<file>   DRGN archive contents
/// <Root>/DRGNn.BIN             root MRG containers (sector-aligned offsets)
/// <Root>/SUBMAP/*.tim          submap assets pulled out of OVL/SMAP.OV_
/// <Root>/OVL/*.OV_             engine overlays
/// <Root>/XA/…                  CD audio (transcoded by the reference; not ours)
/// ```
///
/// Deliberately absent here, per the milestone's scope: caching, refcounts,
/// dependency graphs, async loading and mod-override stacking. Nothing consumes
/// them yet, and each one is easier to add honestly once its first real user
/// exists (asset-pipeline.md §4 lists them as later stages, not this one).
///
/// @note Thread: a ResourceManager instance is not synchronized. Decoders and
/// the free functions below are pure and thread-safe.

#include "lod/status.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lod
{
class Logger;
} // namespace lod

namespace psx
{
enum class Error : int;
} // namespace psx

namespace lod::res
{

/// @brief What was found where a resource root was expected.
///
/// a root is *recognized* when it carries `SECT/` — the marker every runtime
/// asset path hangs off (docs/research/asset-pipeline.md §2). The remaining
/// flags are reported rather than demanded, because a partially
/// unpacked tree is a real state an operator can be in and the diagnostics are
/// more useful than a single boolean.
struct RootInfo
{
  std::filesystem::path Path;     ///< Root as given, normalized.
  bool Exists = false;            ///< The path exists and is a directory.
  bool HasSectDir = false;        ///< `SECT/` present (the recognition marker).
  bool HasVersionFile = false;    ///< `version` present.
  std::string Version;            ///< Contents of `version`, trimmed ("" if absent).
  std::size_t DrgnBins = 0;       ///< `SECT/DRGN*.BIN` directories found.
  bool HasSubmapDir = false;      ///< `SUBMAP/` present.
  bool HasOvlDir = false;         ///< `OVL/` present.
  bool HasXADir = false;          ///< `XA/` present.
  std::vector<std::string> Notes; ///< Human-readable findings, in order.

  /// @brief Whether this looks like an extracted game tree.
  [[nodiscard]] bool LooksLikeGameRoot() const noexcept { return Exists && HasSectDir; }
};

/// @brief Inspect a directory as a resource root. Never creates anything.
///
/// "Absent" and "not a directory" are answers, not failures: they are reported
/// through RootInfo (`exists` false, plus notes), because the caller's job is to
/// tell the operator what was found. Only a directory that cannot be *listed* is
/// an error.
///
/// @param root Directory to examine (may be absent).
/// @return A populated RootInfo, or Status::IORead when the directory exists but
///         cannot be listed.
/// @see ResourceManager::Open for the "is this usable as a resource root" policy.
/// @note Thread: reads the filesystem; safe from any thread.
[[nodiscard]] Result<RootInfo> ValidateRoot(const std::filesystem::path& root);

/// @brief Coarse content classification, decided by the same magic checks the
/// extraction pipeline uses to pick a transformer.
enum class ResourceKind : std::uint8_t
{
  Raw,  ///< No known header.
  BPE,  ///< BPE-compressed segment (magic u32 at 0x4).
  MRG,  ///< MRG container (magic u32 at 0x0).
  DEFF, ///< DEFF container (magic u32 at 0x0).
  TIM,  ///< TIM texture image (id u32 at 0x0).
};

/// @brief Name for a ResourceKind ("raw", "bpe", "mrg", "deff", "tim").
[[nodiscard]] std::string_view KindName(ResourceKind kind) noexcept;

/// @brief Classify bytes by header only (no decoding).
/// @param bytes Resource contents.
/// @return The detected kind; ResourceKind::Raw when nothing matches.
/// @note Thread: pure function, no state — thread-safe.
[[nodiscard]] ResourceKind DetectKind(std::span<const std::uint8_t> bytes) noexcept;

/// @brief Maps a decoder error onto the engine's status taxonomy.
///
/// The mapping is deliberately coarse (five codes); the fine cause stays
/// available through psx::ErrorName() and is logged next to the status so
/// decoder granularity survives the subsystem boundary.
[[nodiscard]] Status ToStatus(psx::Error err) noexcept;

/// @brief Owns one resource's bytes. Move-only; Release() frees immediately and
/// the destructor does the same, so cleanup is deterministic.
class ResourceBlob
{
public:
  ResourceBlob() = default;
  ResourceBlob(const ResourceBlob&) = delete;
  ResourceBlob& operator=(const ResourceBlob&) = delete;
  ResourceBlob(ResourceBlob&&) noexcept = default;
  ResourceBlob& operator=(ResourceBlob&&) noexcept = default;
  ~ResourceBlob() = default;

  /// @brief Adopt bytes for a resource at the given root-relative path.
  /// @param relPath Path relative to the resource root (empty allowed).
  /// @param contents File bytes; storage is taken, not copied.
  /// @param kind Classification hint; defaults to a header sniff.
  ResourceBlob(std::filesystem::path relPath, std::vector<std::uint8_t> contents,
               ResourceKind kind = ResourceKind::Raw);

  /// @brief Root-relative path this blob was loaded from.
  [[nodiscard]] const std::filesystem::path& RelPath() const noexcept { return path_; }

  /// @brief Owned bytes (empty for an empty file or a released blob).
  [[nodiscard]] std::span<const std::uint8_t> Bytes() const noexcept
  {
    return std::span<const std::uint8_t>(contents_);
  }

  /// @brief Byte count.
  [[nodiscard]] std::size_t Size() const noexcept { return contents_.size(); }

  /// @brief Detected content kind.
  [[nodiscard]] ResourceKind Kind() const noexcept { return contentKind_; }

  /// @brief True when no bytes are held.
  [[nodiscard]] bool Empty() const noexcept { return contents_.empty(); }

  /// @brief Release the buffer immediately (idempotent).
  void Release() noexcept
  {
    contents_.clear();
    contents_.shrink_to_fit();
  }

private:
  std::filesystem::path path_;
  std::vector<std::uint8_t> contents_;
  ResourceKind contentKind_ = ResourceKind::Raw;
};

/// @brief One discovered file.
struct ResourceEntry
{
  std::filesystem::path RelPath;         ///< Path relative to the root.
  std::uintmax_t Size = 0;               ///< Byte size as reported by the filesystem.
  ResourceKind Kind = ResourceKind::Raw; ///< Header sniff (Raw when not sniffed).
};

/// @brief Result of a directory walk.
struct ScanResult
{
  std::vector<ResourceEntry> Entries; ///< Regular files, sorted by relative path.
  std::size_t Directories = 0;        ///< Directories visited.
  std::uintmax_t TotalBytes = 0;      ///< Sum of Entry::Size.
  bool Truncated = false;             ///< ScanLimit stopped the walk early.
};

/// @brief Validated description of one resource.
struct Inspection
{
  std::filesystem::path RelPath;         ///< Path relative to the root.
  std::uintmax_t Bytes = 0;              ///< Size of the file that was inspected.
  ResourceKind Kind = ResourceKind::Raw; ///< Detected kind.
  std::string Summary;                   ///< Format-specific facts, for logs and CLI output.
};

/// @brief Loads and validates resources from one root.
///
/// Ownership model: Load() returns a ResourceBlob that owns its bytes; the
/// manager keeps nothing, so releasing the blob releases the memory. Repeated
/// acquisition is therefore cheap and predictable (no cache, no handle table, no
/// dangling references — spans handed out by psx parsers are tied to the blob
/// the caller still holds).
class ResourceManager
{
public:
  /// @brief Operational limits. Defaults suit a full extracted tree.
  struct Options
  {
    std::uintmax_t MaxFileBytes = 64u * 1024u * 1024u;       ///< Per-file ceiling (0 = none).
    std::size_t ScanLimit = 50000u;                          ///< Max entries per Scan().
    std::size_t InspectDecompressLimit = 8u * 1024u * 1024u; ///< Inspect() will not
                                                             ///< inflate a BPE segment
                                                             ///< larger than this.
    Logger* Log = nullptr;                                   ///< Optional diagnostics sink.
  };

  /// @brief Construct over a root with default limits (use Open() normally).
  /// @param root Resource root path.
  explicit ResourceManager(std::filesystem::path root) : ResourceManager(std::move(root), Options{}) {}

  /// @brief Construct over a root without validating it (use Open() normally).
  /// @param root Resource root path.
  /// @param optsIn Limits and optional logger.
  ResourceManager(std::filesystem::path root, Options optsIn);

  /// @brief Construct and validate: the root must exist and look like a game tree.
  /// @param root Resource root path, validated with default limits.
  /// @return See the two-argument overload.
  [[nodiscard]] static Result<ResourceManager> Open(std::filesystem::path root)
  {
    return Open(std::move(root), Options{});
  }

  /// @param root Resource root path.
  /// @param opts Limits and optional logger.
  /// @return A manager, or Status::NotFound (missing / not a game tree — the
  ///         reason is logged), Status::IORead (directory could not be listed).
  [[nodiscard]] static Result<ResourceManager> Open(std::filesystem::path root, Options opts);

  /// @brief The root as given, normalized.
  [[nodiscard]] const std::filesystem::path& Root() const noexcept { return root_; }

  /// @brief Active limits.
  [[nodiscard]] const Options& GetOptions() const noexcept { return opts_; }

  /// @brief Validate the root and describe what was found.
  [[nodiscard]] Result<RootInfo> Validate() const;

  /// @brief Load one resource by root-relative path.
  ///
  /// The path is confined to the root: absolute paths, drive names and any `..`
  /// component are rejected before touching the filesystem, so a path built from
  /// game or mod data cannot read outside the tree.
  ///
  /// @param relative Root-relative path, e.g. `SECT/DRGN0.BIN/5546/1`.
  /// @return The owned bytes, or Status::InvalidArgument (unsafe/absolute path),
  ///         Status::NotFound (absent or a directory), Status::IOOpen,
  ///         Status::IORead, Status::IOTooLarge.
  /// @note Thread: safe from any thread; no shared mutable state.
  [[nodiscard]] Result<ResourceBlob> Load(const std::filesystem::path& relative) const;

  /// @brief Walk the root and list its files (sorted, deterministic).
  /// @return The scan result, or Status::NotFound / Status::IORead.
  /// @note Symlinks are not followed (a link loop cannot wedge the walk).
  [[nodiscard]] Result<ScanResult> Scan() const;

  /// @brief Load a resource and validate it through the matching psx parser.
  /// @param relative Root-relative path.
  /// @return An Inspection whose summary describes the parsed structure, or the
  ///         load failure's Status, or a Decode* status when the content does not
  ///         parse (the specific psx::Error is logged alongside it).
  /// @note Raw (headerless) resources inspect successfully as `raw`: they are
  ///       reported with their size only, which is honest — no parser claims
  ///       success over data it cannot interpret.
  [[nodiscard]] Result<Inspection> Inspect(const std::filesystem::path& relative) const;

  /// @brief Validate bytes already in memory (no filesystem involved).
  /// @param relPath Label for the Inspection / log lines.
  /// @param bytes  Resource contents.
  /// @return An Inspection, or a Decode* Status for unparsable content.
  /// @note Thread: pure apart from the optional logger — thread-safe.
  [[nodiscard]] Result<Inspection> InspectBytes(std::filesystem::path relPath,
                                                std::span<const std::uint8_t> bytes) const;

  /// @brief True when `relative` stays inside the root.
  /// @param relative Candidate path (may be relative or absolute).
  /// @note Thread: pure function, no state — thread-safe.
  [[nodiscard]] static bool IsSafeRelativePath(const std::filesystem::path& relative) noexcept;

private:
  std::filesystem::path root_;
  Options opts_;
};

} // namespace lod::res
