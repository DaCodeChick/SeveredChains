/// @file error.c
/// @brief Error names and diagnostics for the C core.
///
/// The identifier strings are a diagnostic contract: the engine logs them and the
/// tests assert them, and they are exactly the strings the pre-migration C++
/// `psx::ErrorName` produced (psx/tests/test_smoke.cpp pins them). Unknown codes
/// stay reportable as "Unknown" rather than becoming a crash or a NULL pointer.

#include "psx/error.h"

const char* psx_error_name(psx_error_t error)
{
  switch (error)
  {
    case PSX_OK:
      return "Ok";
    case PSX_ERROR_INVALID_ARGUMENT:
      return "InvalidArgument";
    case PSX_ERROR_TRUNCATED:
      return "Truncated";

    case PSX_ERROR_BPE_MAGIC:
      return "BPEMagic";
    case PSX_ERROR_BPE_BLOCK_SIZE:
      return "BPEBlockSize";
    case PSX_ERROR_BPE_OUTPUT_TOO_LARGE:
      return "BPEOutputTooLarge";
    case PSX_ERROR_BPE_SIZE_MISMATCH:
      return "BPESizeMismatch";
    case PSX_ERROR_BPE_DICTIONARY_OVERFLOW:
      return "BPEDictionaryOverflow";
    case PSX_ERROR_BPE_EXPANSION_OVERFLOW:
      return "BPEExpansionOverflow";

    case PSX_ERROR_TIM_MAGIC:
      return "TIMMagic";
    case PSX_ERROR_TIM_BPP:
      return "TIMBPP";
    case PSX_ERROR_TIM_NO_IMAGES:
      return "TIMNoImages";
    case PSX_ERROR_TIM_SECTION_SIZE:
      return "TIMSectionSize";
    case PSX_ERROR_TIM_RECT_EXTENT:
      return "TIMRectExtent";
    case PSX_ERROR_TIM_SECTION_OUT_OF_RANGE:
      return "TIMSectionOutOfRange";

    case PSX_ERROR_MRG_MAGIC:
      return "MRGMagic";
    case PSX_ERROR_MRG_TABLE_TRUNCATED:
      return "MRGTableTruncated";
    case PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE:
      return "MRGEntryOutOfRange";
    case PSX_ERROR_MRG_ENTRY_MISSING:
      return "MRGEntryMissing";

    case PSX_ERROR_DEFF_MAGIC:
      return "DEFFMagic";
    case PSX_ERROR_DEFF_TABLE_TRUNCATED:
      return "DEFFTableTruncated";
    case PSX_ERROR_DEFF_OFFSETS_BAD:
      return "DEFFOffsetsBad";
    case PSX_ERROR_DEFF_ENTRY_MISSING:
      return "DEFFEntryMissing";

    /* --- XA (170..199) --- */
    case PSX_ERROR_XA_UNSUPPORTED_CODING:
      return "XAUnsupportedCoding";
    case PSX_ERROR_XA_OUTPUT_TOO_LARGE:
      return "XAOutputTooLarge";

    default:
      return "Unknown";
  }
}

const char* psx_error_message(psx_error_t error)
{
  switch (error)
  {
    case PSX_OK:
      return "no error";
    case PSX_ERROR_INVALID_ARGUMENT:
      return "invalid argument: a required pointer was null, or a buffer/capacity contradicted its size";
    case PSX_ERROR_TRUNCATED:
      return "input ended before the structure being read did";

    case PSX_ERROR_BPE_MAGIC:
      return "not a BPE segment: u32 at 0x4 is not \"BPE\\x1a\"";
    case PSX_ERROR_BPE_BLOCK_SIZE:
      return "malformed BPE: a block declares more than the 0x800-byte limit";
    case PSX_ERROR_BPE_OUTPUT_TOO_LARGE:
      return "BPE output too large: the declared size exceeds the caller's capacity";
    case PSX_ERROR_BPE_SIZE_MISMATCH:
      return "malformed BPE: the blocks did not produce exactly the declared size";
    case PSX_ERROR_BPE_DICTIONARY_OVERFLOW:
      return "malformed BPE: the block dictionary wrote past key 0xff";
    case PSX_ERROR_BPE_EXPANSION_OVERFLOW:
      return "malformed BPE: a self-referential dictionary cannot fit the block";

    case PSX_ERROR_TIM_MAGIC:
      return "not a TIM: u32 at 0x0 is not 0x10";
    case PSX_ERROR_TIM_BPP:
      return "unsupported TIM colour depth: flags & 7 is not one of 0..3";
    case PSX_ERROR_TIM_NO_IMAGES:
      return "truncated TIM: the buffer ends before a required section header or rect";
    case PSX_ERROR_TIM_SECTION_SIZE:
      return "malformed TIM: a section length is smaller than its own 12-byte header";
    case PSX_ERROR_TIM_RECT_EXTENT:
      return "malformed TIM: a rect is empty or promises more bytes than its section declares";
    case PSX_ERROR_TIM_SECTION_OUT_OF_RANGE:
      return "malformed TIM: a section's declared payload runs past the buffer";

    case PSX_ERROR_MRG_MAGIC:
      return "not an MRG: u32 at 0x0 is not \"MRG\\x1a\"";
    case PSX_ERROR_MRG_TABLE_TRUNCATED:
      return "truncated MRG: the entry table does not fit the container";
    case PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE:
      return "malformed MRG: an entry points outside the container";
    case PSX_ERROR_MRG_ENTRY_MISSING:
      return "no such MRG entry: the index is out of range";

    case PSX_ERROR_DEFF_MAGIC:
      return "not a DEFF: u32 at 0x0 is not \"DEFF\"";
    case PSX_ERROR_DEFF_TABLE_TRUNCATED:
      return "truncated DEFF: the subfile table does not fit the container";
    case PSX_ERROR_DEFF_OFFSETS_BAD:
      return "malformed DEFF: subfile starts are not monotonic inside the container";
    case PSX_ERROR_DEFF_ENTRY_MISSING:
      return "no such DEFF subfile: the index is out of range or the range does not fit";

    case PSX_ERROR_XA_UNSUPPORTED_CODING:
      return "unsupported XA coding: the coding byte marks 8-bit samples, which have no decoder here";
    case PSX_ERROR_XA_OUTPUT_TOO_LARGE:
      return "XA output too small: the destination cannot hold this sector's samples";

    default:
      return "unrecognised psx error code";
  }
}

int psx_error_is_bpe(psx_error_t error)
{
  return error >= PSX_ERROR_BPE_MAGIC && error <= PSX_ERROR_BPE_EXPANSION_OVERFLOW;
}

int psx_error_is_tim(psx_error_t error)
{
  return error >= PSX_ERROR_TIM_MAGIC && error <= PSX_ERROR_TIM_SECTION_OUT_OF_RANGE;
}

int psx_error_is_mrg(psx_error_t error)
{
  return error >= PSX_ERROR_MRG_MAGIC && error <= PSX_ERROR_MRG_ENTRY_MISSING;
}

int psx_error_is_deff(psx_error_t error)
{
  return error >= PSX_ERROR_DEFF_MAGIC && error <= PSX_ERROR_DEFF_ENTRY_MISSING;
}

int psx_error_is_xa(psx_error_t error)
{
  return error >= PSX_ERROR_XA_UNSUPPORTED_CODING && error <= PSX_ERROR_XA_OUTPUT_TOO_LARGE;
}
