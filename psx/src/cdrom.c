/// @file cdrom.h implementation — CD-ROM XA sector geometry and header decode.
///
/// Every rule here traces to the reference repository at commit 3433e6f:
/// SectorHeader.java (header offsets and bit tests), IsoReader.java (sector
/// size, the 12-byte and 24-byte data offsets, readSectors), and
/// XaAdpcm.java (the 18 x 128-byte block loop that proves payload starts at
/// byte 24). Prose version: docs/research/psx-formats.md §7.

#include "psx/cdrom.h"

psx_error_t psx_sector_header_parse(psx_bytes_t sector, psx_sector_header_t* out_header)
{
  if (out_header == NULL || !psx_bytes_is_valid(sector))
    return PSX_ERROR_INVALID_ARGUMENT;

  /* Only the header region is read, so a caller may supply a 2352-byte sector
   * or anything at least as long as the header it wants decoded. */
  if (sector.size < PSX_CDROM_HEADER_SIZE || sector.data == NULL)
    return PSX_ERROR_TRUNCATED;

  out_header->channel = sector.data[PSX_CDROM_CHANNEL_OFFSET];
  out_header->submode = sector.data[PSX_CDROM_SUBMODE_OFFSET];
  out_header->coding = sector.data[PSX_CDROM_CODING_OFFSET];
  return PSX_OK;
}

psx_error_t psx_sector_classify(psx_sector_header_t header, psx_sector_kind_t* out_kind)
{
  if (out_kind == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;

  /* SectorHeader.Submode.getType() — the same four-way switch, with the
   * reference's `throw new IllegalArgumentException` replaced by a reported
   * error. A malformed sector must not be able to abort the host process. */
  switch (header.submode & PSX_CDROM_SUBMODE_TYPE)
  {
    case PSX_SECTOR_KIND_VIDEO:
      *out_kind = PSX_SECTOR_KIND_VIDEO;
      return PSX_OK;
    case PSX_SECTOR_KIND_AUDIO:
      *out_kind = PSX_SECTOR_KIND_AUDIO;
      return PSX_OK;
    case PSX_SECTOR_KIND_DATA:
      *out_kind = PSX_SECTOR_KIND_DATA;
      return PSX_OK;
    default:
      return PSX_ERROR_CDROM_BAD_SECTOR_TYPE;
  }
}

psx_error_t psx_sector_payload(psx_bytes_t sector, psx_bytes_t* out_payload)
{
  if (out_payload == NULL || !psx_bytes_is_valid(sector))
    return PSX_ERROR_INVALID_ARGUMENT;

  if (sector.size < PSX_CDROM_SECTOR_SIZE)
    return PSX_ERROR_TRUNCATED;

  /* Anything longer than one sector is a wrong argument, not a bigger sector.
   * Without this, passing a whole image by mistake reads sector 0 forever and
   * looks like a working decoder producing silence. */
  if (sector.size > PSX_CDROM_SECTOR_SIZE)
    return PSX_ERROR_INVALID_ARGUMENT;

  psx_sector_header_t header;
  const psx_error_t parsed = psx_sector_header_parse(sector, &header);
  if (parsed != PSX_OK)
    return parsed;

  const size_t payload_size =
    (header.submode & PSX_CDROM_SUBMODE_FORM) != 0u ? PSX_CDROM_FORM2_DATA_SIZE : PSX_CDROM_FORM1_DATA_SIZE;

  /* Defensive: unreachable for a full 2352-byte sector, both forms fit. Kept
   * because the payload size is derived from caller data, not from a constant. */
  if (payload_size > sector.size - PSX_CDROM_HEADER_SIZE)
    return PSX_ERROR_TRUNCATED;

  out_payload->data = sector.data + PSX_CDROM_HEADER_SIZE;
  out_payload->size = payload_size;
  return PSX_OK;
}

psx_error_t psx_sector_run_extent(psx_bytes_t image, size_t first_sector, size_t count, psx_sector_extent_t* out_extent)
{
  if (!psx_bytes_is_valid(image) || out_extent == NULL || count == 0u)
    return PSX_ERROR_INVALID_ARGUMENT;

  /* Both products are checked before they are computed. A run whose arithmetic
   * wraps is an argument error, not a truncated read that silently returns a
   * short (wrong) range. */
  if (first_sector > SIZE_MAX / PSX_CDROM_SECTOR_SIZE || count > SIZE_MAX / PSX_CDROM_SECTOR_SIZE)
    return PSX_ERROR_INVALID_ARGUMENT;

  const size_t offset = first_sector * PSX_CDROM_SECTOR_SIZE;
  const size_t length = count * PSX_CDROM_SECTOR_SIZE;

  if (offset >= image.size || length > image.size - offset)
    return PSX_ERROR_TRUNCATED;

  out_extent->offset = offset;
  out_extent->length = length;
  return PSX_OK;
}
