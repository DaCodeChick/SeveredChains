/// @file resources.cpp
/// @brief Resource discovery, ownership and validation (see resources.h).

#include "lod/resources.h"

#include "lod/fs.h"
#include "lod/log.h"
#include "psx/cpp/psx.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lod::res
{
namespace
{

constexpr std::string_view Marker = "resources";
constexpr std::string_view SectDir = "SECT";
constexpr std::string_view SubmapDir = "SUBMAP";
constexpr std::string_view OvlDir = "OVL";
constexpr std::string_view XADir = "XA";
constexpr std::string_view VersionFile = "version";

/// @brief Root-archive pattern `^DRGN(?:0|1|2[1234])?\.BIN$`, which is what
/// selects 0x800 sector-aligned MRG offsets (docs/research/psx-formats.md §2).
/// Matched against a single path component, case-insensitively: ISO 9660
/// Level-1 names are uppercase, but the extracted tree also lives on
/// case-insensitive filesystems, so case must not decide alignment.
[[nodiscard]] bool IsDrgnRootName(std::string_view name) noexcept
{
  auto equalIgnoreCase = [](std::string_view a, std::string_view b)
  {
    return a.size() == b.size() &&
           std::equal(
             a.begin(), a.end(), b.begin(), [](char l, char r)
             { return std::toupper(static_cast<unsigned char>(l)) == std::toupper(static_cast<unsigned char>(r)); });
  };

  // "DRGN" + optional digits + ".BIN"; the length guard comes first so short
  // names cannot underflow the suffix offset.
  if (name.size() < 8 || !equalIgnoreCase(name.substr(0, 4), "DRGN") ||
      !equalIgnoreCase(name.substr(name.size() - 4), ".BIN"))
  {
    return false;
  }
  const std::string_view digits = name.substr(4, name.size() - 8);
  return digits.empty() || equalIgnoreCase(digits, "0") || equalIgnoreCase(digits, "1") ||
         equalIgnoreCase(digits, "21") || equalIgnoreCase(digits, "22") || equalIgnoreCase(digits, "23") ||
         equalIgnoreCase(digits, "24");
}

/// @brief True when the first component of a root-relative path is a DRGN root
/// archive, i.e. its MRG offsets are sector numbers.
[[nodiscard]] bool IsSectorAlignedPath(const std::filesystem::path& relPath) noexcept
{
  auto it = relPath.begin();
  if (it == relPath.end())
    return false;
  // path::string() rather than native(): on Windows native() is wide, and these
  // names are ASCII ISO-9660 Level-1 by construction.
  return IsDrgnRootName(it->string());
}

/// @brief Trim ASCII whitespace: the `version` file holds a bare number written
/// verbatim, so a trailing newline is not part of the value.
[[nodiscard]] std::string Trim(std::string_view text)
{
  const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  std::size_t begin = 0;
  while (begin < text.size() && isSpace(text[begin]))
    ++begin;
  std::size_t end = text.size();
  while (end > begin && isSpace(text[end - 1]))
    --end;
  return std::string(text.substr(begin, end - begin));
}

/// @brief Log at Warning when a logger is configured.
void Warn(const ResourceManager::Options& opts, std::string_view message)
{
  if (opts.Log != nullptr)
    opts.Log->Warn(Marker, "{}", message);
}

/// @brief Log at Info when a logger is configured.
void Info(const ResourceManager::Options& opts, std::string_view message)
{
  if (opts.Log != nullptr)
    opts.Log->Info(Marker, "{}", message);
}

/// @brief Validate every MRG entry by slicing it, which is where an entry that
/// claims bytes the container does not have shows up.
[[nodiscard]] std::string DescribeMRG(const psx::archive::MRGTable& table, std::span<const std::uint8_t> buffer,
                                      bool sectorAligned, std::size_t& outOfRange)
{
  std::size_t readable = 0;
  std::uintmax_t payloadBytes = 0;
  outOfRange = 0;
  for (std::size_t i = 0; i < table.Count(); ++i)
  {
    if (auto slice = psx::archive::MRGEntryData(table, buffer, i); slice.has_value())
    {
      ++readable;
      payloadBytes += slice->size();
    }
    else
    {
      ++outOfRange;
    }
  }
  return std::format("MRG {} entries ({} virtual, {} readable, {} B payload{})", table.Count(), table.VirtualCount(),
                     readable, payloadBytes, sectorAligned ? ", sector-aligned offsets" : "");
}

/// @brief Validate every DEFF subfile by slicing it.
[[nodiscard]] std::string DescribeDEFF(const psx::archive::DEFFTable& table, std::span<const std::uint8_t> buffer,
                                       std::size_t& outOfRange)
{
  std::size_t readable = 0;
  std::uintmax_t payloadBytes = 0;
  outOfRange = 0;
  for (std::size_t i = 0; i < table.Count(); ++i)
  {
    if (auto slice = psx::archive::DEFFEntryData(table, buffer, i); slice.has_value())
    {
      ++readable;
      payloadBytes += slice->size();
    }
    else
    {
      ++outOfRange;
    }
  }
  return std::format("DEFF {} subfiles ({} readable, {} B payload)", table.Count(), readable, payloadBytes);
}

/// @brief bpp as it appears in reports.
[[nodiscard]] std::string_view BPPName(psx::tim::BPP depth) noexcept
{
  switch (depth)
  {
    case psx::tim::BPP::Four:
      return "4bpp";
    case psx::tim::BPP::Eight:
      return "8bpp";
    case psx::tim::BPP::Fifteen:
      return "15bpp";
    case psx::tim::BPP::TwentyFour:
      return "24bpp";
  }
  return "?bpp";
}

} // namespace

std::string_view KindName(ResourceKind kind) noexcept
{
  switch (kind)
  {
    case ResourceKind::Raw:
      return "raw";
    case ResourceKind::BPE:
      return "bpe";
    case ResourceKind::MRG:
      return "mrg";
    case ResourceKind::DEFF:
      return "deff";
    case ResourceKind::TIM:
      return "tim";
  }
  return "unknown";
}

ResourceKind DetectKind(std::span<const std::uint8_t> bytes) noexcept
{
  // Order mirrors the extraction pipeline: BPE is discriminated by a magic at
  // 0x4, the containers by a magic at 0x0.
  if (psx::bpe::IsBPE(bytes))
    return ResourceKind::BPE;
  if (psx::archive::IsMRG(bytes))
    return ResourceKind::MRG;
  if (psx::archive::IsDEFF(bytes))
    return ResourceKind::DEFF;
  if (psx::tim::IsTIM(bytes))
    return ResourceKind::TIM;
  return ResourceKind::Raw;
}

Status ToStatus(psx::Error err) noexcept
{
  using E = psx::Error;
  switch (err)
  {
    case E::InvalidArgument:
      return Status::InvalidArgument;
    case E::Truncated:
      return Status::DecodeTruncated;

    case E::BPEMagic:
      return Status::DecodeHeader;
    case E::BPEBlockSize:
    case E::BPEExpansionOverflow:
    case E::BPEDictionaryOverflow:
      return Status::DecodeCorrupt;
    case E::BPEOutputTooLarge:
      return Status::IOTooLarge;
    case E::BPESizeMismatch:
      return Status::DecodeSizeMismatch;

    case E::TIMMagic:
      return Status::DecodeHeader;
    case E::TIMBPP:
      return Status::DecodeUnsupported;
    case E::TIMNoImages:
    case E::TIMSectionOutOfRange:
      return Status::DecodeTruncated;
    case E::TIMSectionSize:
      return Status::DecodeSizeMismatch;
    case E::TIMRectExtent:
      return Status::DecodeCorrupt;

    case E::MRGMagic:
    case E::DEFFMagic:
      return Status::DecodeHeader;

    case E::MRGTableTruncated:
    case E::DEFFTableTruncated:
      return Status::DecodeTruncated;
    case E::MRGEntryOutOfRange:
    case E::DEFFOffsetsBad:
      return Status::DecodeCorrupt;
    case E::MRGEntryMissing:
    case E::DEFFEntryMissing:
      return Status::InvalidArgument;

    case E::XAUnsupportedCoding:
      // 8-bit XA is a recognised variant that has no decoder here, which is what
      // DecodeUnsupported means. It is not corruption: a valid 8-bit sector is
      // not a malformed 4-bit one, and reporting it as corrupt would invite a
      // retry or a discard of good data.
      return Status::DecodeUnsupported;
    case E::XAOutputTooLarge:
      return Status::IOTooLarge; // Same shape as BPEOutputTooLarge.
  }
  return Status::Error;
}

Result<RootInfo> ValidateRoot(const std::filesystem::path& root)
{
  RootInfo info;
  info.Path = root.lexically_normal();

  // "Absent" and "not a directory" are successful *queries* about the path, not
  // I/O failures: they come back as a populated RootInfo (exists = false, with
  // notes) so callers can report what they found instead of a bare error code.
  std::error_code ec;
  if (!std::filesystem::exists(info.Path, ec))
  {
    info.Notes.push_back(std::format("path does not exist: {}", info.Path.string()));
    return info;
  }
  if (!std::filesystem::is_directory(info.Path, ec))
  {
    info.Notes.push_back(std::format("path is not a directory: {}", info.Path.string()));
    return info;
  }
  info.Exists = true;

  std::filesystem::directory_iterator entries(info.Path, std::filesystem::directory_options::skip_permission_denied,
                                              ec);
  if (ec)
  {
    info.Notes.push_back(std::format("cannot list {}: {}", info.Path.string(), ec.message()));
    return std::unexpected(Status::IORead);
  }

  for (const auto& entry : entries)
  {
    const std::string name = entry.path().filename().string();
    const bool isDir = entry.is_directory(ec);
    if (isDir && name == SectDir)
    {
      info.HasSectDir = true;
      // Count the DRGN archives inside SECT/ — the level runtime paths index.
      std::filesystem::directory_iterator sect(entry.path(), std::filesystem::directory_options::skip_permission_denied,
                                               ec);
      if (!ec)
      {
        for (const auto& child : sect)
          if (child.is_directory(ec) && IsDrgnRootName(child.path().filename().string()))
            ++info.DrgnBins;
      }
    }
    else if (isDir && name == SubmapDir)
    {
      info.HasSubmapDir = true;
    }
    else if (isDir && name == OvlDir)
    {
      info.HasOvlDir = true;
    }
    else if (isDir && name == XADir)
    {
      info.HasXADir = true;
    }
    else if (!isDir && name == VersionFile)
    {
      info.HasVersionFile = true;
      // The version file is a handful of bytes; 64 is generous and keeps an
      // accidental huge file from being slurped during validation.
      if (auto contents = fs::ReadFileBytes(entry.path(), 64); contents.has_value())
        info.Version = Trim(std::string_view(reinterpret_cast<const char*>(contents->data()), contents->size()));
    }
  }

  info.Notes.push_back(info.HasSectDir ? std::format("SECT/ present ({} DRGN archive(s))", info.DrgnBins)
                                       : "SECT/ missing — not a game resource root");
  info.Notes.push_back(info.HasVersionFile ? std::format("version = '{}'", info.Version)
                                           : "version file missing (extraction may be incomplete)");
  info.Notes.push_back(std::format("optional dirs: SUBMAP {} OVL {} XA {}", info.HasSubmapDir ? "yes" : "no",
                                   info.HasOvlDir ? "yes" : "no", info.HasXADir ? "yes" : "no"));
  return info;
}

ResourceBlob::ResourceBlob(std::filesystem::path relPath, std::vector<std::uint8_t> contentsIn, ResourceKind kind)
    : path_(std::move(relPath)), contents_(std::move(contentsIn)), contentKind_(kind)
{
  if (contentKind_ == ResourceKind::Raw)
    contentKind_ = DetectKind(Bytes());
}

ResourceManager::ResourceManager(std::filesystem::path root, Options optsIn)
    : root_(std::move(root).lexically_normal()), opts_(optsIn)
{
}

Result<ResourceManager> ResourceManager::Open(std::filesystem::path root, Options optsIn)
{
  ResourceManager manager(std::move(root), optsIn);

  auto info = ValidateRoot(manager.root_);
  if (!info)
  {
    Warn(optsIn, std::format("cannot read resource root {}: {}", manager.root_.string(),
                             std::string(StatusName(info.error()))));
    return std::unexpected(info.error());
  }

  const bool gameRoot = info->LooksLikeGameRoot();
  for (const std::string& note : info->Notes)
    if (gameRoot)
      Info(optsIn, note);
    else
      Warn(optsIn, note);
  if (!gameRoot)
  {
    // Deliberately NotFound rather than a generic error: "this is not an
    // extracted game tree" is the answer an operator needs to see.
    return std::unexpected(Status::NotFound);
  }
  return manager;
}

Result<RootInfo> ResourceManager::Validate() const
{
  return ValidateRoot(root_);
}

bool ResourceManager::IsSafeRelativePath(const std::filesystem::path& relative) noexcept
{
  if (relative.empty() || relative.is_absolute())
    return false;
#if defined(_WIN32)
  // A drive or UNC root is absolute on some platforms but not reported by
  // is_absolute(); reject any root-name outright.
  if (!relative.root_name().empty())
    return false;
#endif
  for (const auto& part : relative)
    if (part == "..")
      return false;
  return true;
}

Result<ResourceBlob> ResourceManager::Load(const std::filesystem::path& relative) const
{
  if (!IsSafeRelativePath(relative))
  {
    Warn(opts_,
         std::format("rejected resource path (must be relative and stay inside the root): {}", relative.string()));
    return std::unexpected(Status::InvalidArgument);
  }

  const std::filesystem::path full = root_ / relative;
  auto bytes = fs::ReadFileBytes(full, opts_.MaxFileBytes);
  if (!bytes)
  {
    if (bytes.error() == Status::NotFound)
    {
      // Say what was looked for, and what is there instead when that helps.
      std::error_code ec;
      if (std::filesystem::is_directory(full, ec))
        Warn(opts_, std::format("resource '{}' is a directory, not a file", relative.string()));
      else
        Warn(opts_, std::format("resource not found: {}", full.string()));
    }
    else
    {
      Warn(opts_, std::format("resource read failed for '{}': {}", relative.string(), StatusName(bytes.error())));
    }
    return std::unexpected(bytes.error());
  }

  Info(opts_, std::format("loaded {} B as '{}' ({})", bytes->size(), relative.string(), KindName(DetectKind(*bytes))));
  return ResourceBlob(relative, std::move(*bytes));
}

Result<ScanResult> ResourceManager::Scan() const
{
  std::error_code ec;
  if (!std::filesystem::is_directory(root_, ec))
    return std::unexpected(Status::NotFound);

  ScanResult result;
  std::filesystem::recursive_directory_iterator walk(root_, std::filesystem::directory_options::skip_permission_denied,
                                                     ec);
  if (ec)
  {
    Warn(opts_, std::format("cannot walk {}: {}", root_.string(), ec.message()));
    return std::unexpected(Status::IORead);
  }

  for (; walk != std::filesystem::recursive_directory_iterator(); ++walk)
  {
    const std::filesystem::path& full = walk->path();
    if (walk->is_directory(ec))
    {
      ++result.Directories;
      continue;
    }
    if (walk->is_symlink(ec) || !walk->is_regular_file(ec))
      continue; // links are listed but never followed into the walk

    const std::uintmax_t size = std::filesystem::file_size(full, ec);
    if (ec)
    {
      // Unreadable metadata should not abort a discovery pass; record and move on.
      Warn(opts_, std::format("skipping '{}': size failed ({})", full.string(), ec.message()));
      ec.clear();
      continue;
    }

    std::filesystem::path relative = std::filesystem::relative(full, root_, ec);
    if (ec)
    {
      ec.clear();
      relative = full.filename();
    }

    ResourceKind kind = ResourceKind::Raw;
    if (opts_.MaxFileBytes == 0 || size <= opts_.MaxFileBytes)
    {
      // Sniffing needs the header only, so read a prefix rather than the whole
      // file (and rather than failing on files over the sniff size).
      if (auto head = fs::ReadFilePrefix(full, 16); head.has_value())
        kind = DetectKind(*head);
    }

    result.Entries.push_back(ResourceEntry{std::move(relative), size, kind});
    result.TotalBytes += size;

    if (result.Entries.size() >= opts_.ScanLimit)
    {
      result.Truncated = true;
      Warn(opts_, std::format("scan stopped at the {} entry limit", opts_.ScanLimit));
      break;
    }
  }

  std::sort(result.Entries.begin(), result.Entries.end(),
            [](const ResourceEntry& a, const ResourceEntry& b) { return a.RelPath < b.RelPath; });

  Info(opts_, std::format("scan: {} file(s), {} B, {} director(ies){}", result.Entries.size(), result.TotalBytes,
                          result.Directories, result.Truncated ? " (truncated)" : ""));
  return result;
}

Result<Inspection> ResourceManager::InspectBytes(std::filesystem::path relPath,
                                                 std::span<const std::uint8_t> bytes) const
{
  Inspection out;
  out.RelPath = std::move(relPath);
  out.Bytes = bytes.size();
  out.Kind = DetectKind(bytes);

  const auto fail = [&](std::string_view operation, psx::Error err) -> Result<Inspection>
  {
    const Status mapped = ToStatus(err);
    Warn(opts_, std::format("{} failed for '{}' ({} B): {} -> {}", operation, out.RelPath.string(), bytes.size(),
                            std::string(psx::ErrorName(err)), std::string(StatusName(mapped))));
    return std::unexpected(mapped);
  };

  switch (out.Kind)
  {
    case ResourceKind::TIM:
    {
      auto parsed = psx::tim::ParseTIM(bytes);
      if (!parsed)
        return fail("TIM parse", parsed.error());
      out.Summary =
        std::format("TIM {}, image {}x{} ({} B){}, clut {}{}", BPPName(parsed->colorDepth), parsed->imageRect.w,
                    parsed->imageRect.h, parsed->imageData.size(),
                    parsed->trailingBytes != 0 ? std::format(", {} B trailing", parsed->trailingBytes) : std::string(),
                    parsed->hasCLUT ? psx::tim::CLUTEntryCount(parsed->clutRect) : 0u,
                    parsed->hasCLUT ? std::format(" entries ({} B)", parsed->clut.size()) : std::string(" (none)"));
      break;
    }

    case ResourceKind::MRG:
    {
      const bool sectorAligned = IsSectorAlignedPath(out.RelPath);
      auto table = psx::archive::ParseMRG(bytes, sectorAligned);
      if (!table)
        return fail("MRG parse", table.error());
      std::size_t outOfRange = 0;
      out.Summary = DescribeMRG(*table, bytes, sectorAligned, outOfRange);
      if (outOfRange != 0)
      {
        // An entry that claims bytes the container does not have is a malformed
        // container, not a container with a quirk: report it as a failure.
        return fail("MRG entry ranges", psx::Error::MRGEntryOutOfRange);
      }
      break;
    }

    case ResourceKind::DEFF:
    {
      auto table = psx::archive::ParseDEFF(bytes);
      if (!table)
        return fail("DEFF parse", table.error());
      std::size_t outOfRange = 0;
      out.Summary = DescribeDEFF(*table, bytes, outOfRange);
      if (outOfRange != 0)
        return fail("DEFF subfile ranges", psx::Error::DEFFOffsetsBad);
      break;
    }

    case ResourceKind::BPE:
    {
      auto declared = psx::bpe::DeclaredSize(bytes);
      if (!declared)
        return fail("BPE header", declared.error());
      if (*declared > opts_.InspectDecompressLimit)
      {
        // Honest reporting: the header is valid, decompressing here would be a
        // multi-megabyte allocation the inspection pass has no reason to make.
        out.Summary = std::format("BPE {} B -> {} B declared (not decompressed: over the {} B inspect limit)",
                                  bytes.size(), *declared, opts_.InspectDecompressLimit);
        break;
      }
      auto inflated = psx::bpe::DecompressBPE(bytes, opts_.InspectDecompressLimit);
      if (!inflated)
        return fail("BPE decompress", inflated.error());
      out.Summary = std::format("BPE {} B -> {} B", bytes.size(), inflated->size());
      break;
    }

    case ResourceKind::Raw:
      out.Summary = std::format("raw {} B (no supported header)", bytes.size());
      break;
  }

  Info(opts_, std::format("inspected '{}': {}", out.RelPath.string(), out.Summary));
  return out;
}

Result<Inspection> ResourceManager::Inspect(const std::filesystem::path& relative) const
{
  auto blob = Load(relative);
  if (!blob)
    return std::unexpected(blob.error());
  auto result = InspectBytes(blob->RelPath(), blob->Bytes());
  // The blob owns the bytes the Inspection summarised; it is released here on
  // purpose, since the summary is plain data and nothing keeps a span alive.
  blob->Release();
  return result;
}

} // namespace lod::res
