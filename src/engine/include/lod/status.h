#pragma once
/// @file status.h
/// @brief `lod::Status` — engine error codes (error-handling.md §2).
/// Recoverable failures propagate as `std::expected<T, Status>`; the engine
/// codebase does not use C++ exceptions for control flow (AGENTS.md, cpp.md §4).

#include <cstdint>
#include <expected>
#include <string_view>

namespace lod
{

/// @brief Status codes. Pre-beta: freely editable — no compatibility or
/// numbering guarantees yet.
enum class Status : std::int32_t
{
  Ok = 0,
  // generic
  Error,           ///< Unspecified (message on context).
  InvalidArgument, ///< Caller violated a precondition.
  NotFound,        ///< File/asset/handle not found.
  NotSupported,    ///< Recognized-but-unsupported feature/format.
  OutOfMemory,
  Timeout,
  AlreadyExists,
  WouldBlock,
  // io/asset domain (error-handling.md §2: 100s)
  IOOpen = 100, ///< Could not open the file.
  IORead,       ///< Opened but read failed.
  IOWrite,      ///< Write/flush failed.
  IOTooLarge,   ///< File exceeds the caller's size ceiling.
  // Decode domain (error-handling.md §2: 200s). Deliberately coarse: the fine
  // cause lives in psx::Error, which callers log alongside these codes so a
  // decoder's granularity survives the subsystem boundary.
  DecodeHeader = 200, ///< Bad magic / header field.
  DecodeTruncated,    ///< Structure ran past the end of the data.
  DecodeSizeMismatch, ///< Declared sizes disagree with the data present.
  DecodeUnsupported,  ///< Recognised format, unsupported variant.
  DecodeCorrupt,      ///< Structurally impossible content (bad rect, cycle,
                      ///< non-monotonic table, out-of-range entry).
  // remaining domains (error-handling.md §2): 300s gpu, 400s audio, 500s save,
  // 600s mod, 700s script — added with their subsystems.
};

/// @brief Human-readable name for a Status value (crash logs, tests).
/// @param Code Any Status value.
/// @return Static string, e.g. "NotFound"; "Unknown" for out-of-range values.
constexpr std::string_view StatusName(Status Code)
{
  switch (Code)
  {
    case Status::Ok:
      return "Ok";
    case Status::Error:
      return "Error";
    case Status::InvalidArgument:
      return "InvalidArgument";
    case Status::NotFound:
      return "NotFound";
    case Status::NotSupported:
      return "NotSupported";
    case Status::OutOfMemory:
      return "OutOfMemory";
    case Status::Timeout:
      return "Timeout";
    case Status::AlreadyExists:
      return "AlreadyExists";
    case Status::WouldBlock:
      return "WouldBlock";
    case Status::IOOpen:
      return "IOOpen";
    case Status::IORead:
      return "IORead";
    case Status::IOWrite:
      return "IOWrite";
    case Status::IOTooLarge:
      return "IOTooLarge";
    case Status::DecodeHeader:
      return "DecodeHeader";
    case Status::DecodeTruncated:
      return "DecodeTruncated";
    case Status::DecodeSizeMismatch:
      return "DecodeSizeMismatch";
    case Status::DecodeUnsupported:
      return "DecodeUnsupported";
    case Status::DecodeCorrupt:
      return "DecodeCorrupt";
  }
  return "Unknown";
}

/// @brief Result alias — engine-internal convention (error-handling.md §2:
/// std::expected adopted directly; no bespoke wrapper for internal code).
template <class T>
using Result = std::expected<T, Status>;

/// @brief Fire-and-forget operation result (no payload on success).
using StatusResult = std::expected<void, Status>;

} // namespace lod
