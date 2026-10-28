/// @file archive.c
/// @brief MRG / DEFF table parsing in C17 (see archive.h for the verified
/// layouts). Index-only, allocation-free core; the opaque handles add ownership,
/// not behaviour (docs/architecture/psx-c-api.md §5).

#include "psx/archive.h"

#include "reader.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MRG_ENTRY_TABLE_OFFSET 0x8u
#define MRG_ENTRY_STRIDE 0x8u
#define DEFF_ENTRY_STRIDE 0x8u

/* --- shared internals ---------------------------------------------------- */

/// @brief MRG entry-table bounds + magic validation, shared by the count query and
/// the parse (so both agree on what a malformed header means).
/// @param src          Container bytes.
/// @param out_count    Receives the declared entry count.
/// @return PSX_OK, PSX_ERROR_TRUNCATED, PSX_ERROR_MRG_MAGIC, or
///         PSX_ERROR_MRG_TABLE_TRUNCATED.
static psx_error_t mrg_read_header(psx_bytes_t src, size_t* out_count)
{
  const psx_reader_t reader = psx_reader_from(src);
  if (!psx_reader_fits(reader, 0u, 8u))
    return PSX_ERROR_TRUNCATED;

  uint32_t magic = 0u;
  psx_error_t error = psx_reader_u32(reader, 0u, &magic);
  if (error != PSX_OK)
    return error;
  if (magic != PSX_ARCHIVE_MRG_MAGIC)
    return PSX_ERROR_MRG_MAGIC;

  uint32_t count = 0u;
  error = psx_reader_u32(reader, 4u, &count);
  if (error != PSX_OK)
    return error;

  /* Entry-table bounds, computed without overflowing for absurd counts. */
  if ((size_t)count > (src.size - MRG_ENTRY_TABLE_OFFSET) / MRG_ENTRY_STRIDE)
    return PSX_ERROR_MRG_TABLE_TRUNCATED;

  *out_count = (size_t)count;
  return PSX_OK;
}

/// @brief Fill an MRG entry array from a validated container.
/// @param src            Container bytes (validated by mrg_read_header).
/// @param sector_aligned Non-zero to scale offsets by PSX_SECTOR_SIZE.
/// @param entries        Destination with `count` elements (pre-initialised to
///                       {0, 0, 0, -1} — see the note in the body).
/// @param count          Element count (from mrg_read_header).
/// @param out_view_count Receives how many entries alias another entry.
/// @return PSX_OK, PSX_ERROR_MRG_TABLE_TRUNCATED, or a read error.
static psx_error_t mrg_fill_entries(psx_bytes_t src, int sector_aligned, psx_mrg_entry_t* entries, size_t count,
                                    size_t* out_view_count)
{
  const psx_reader_t reader = psx_reader_from(src);

  /* The reference implementation's table starts out with parent = -1 for every
     slot, and pass 1 below reads earlier slots; initialising explicitly keeps the
     behaviour identical to a value-initialised C++ vector (uninitialised storage
     would make duplicate detection depend on stack garbage). */
  for (size_t i = 0; i < count; ++i)
  {
    entries[i].offset = 0u;
    entries[i].size = 0u;
    entries[i].virtual_size = 0u;
    entries[i].parent = -1;
  }

  size_t virtual_count = 0u;

  /* Pass 1: real entries (size != 0). A second entry at an already-seen offset
     aliases the first one. */
  for (size_t i = 0; i < count; ++i)
  {
    const size_t base = MRG_ENTRY_TABLE_OFFSET + i * MRG_ENTRY_STRIDE;
    uint32_t offset = 0u;
    uint32_t size = 0u;
    psx_error_t error = psx_reader_u32(reader, base, &offset);
    if (error == PSX_OK)
      error = psx_reader_u32(reader, base + 4u, &size);
    if (error != PSX_OK)
      return PSX_ERROR_MRG_TABLE_TRUNCATED;
    if (size == 0u)
      continue; /* resolved in pass 2 */

    for (size_t prev = 0; prev < i; ++prev)
    {
      const psx_mrg_entry_t earlier = entries[prev];
      if (earlier.parent < 0 && earlier.size != 0u && earlier.offset == (size_t)offset)
      {
        entries[i].offset = (size_t)offset;
        entries[i].size = (size_t)size;
        entries[i].virtual_size = (size_t)size;
        entries[i].parent = (int)prev;
        ++virtual_count;
        break;
      }
    }

    if (entries[i].parent >= 0)
      continue;

    /* Sector scaling: DRGN*.BIN tables store sector numbers. A hostile offset
       could overflow the multiplication, so it saturates instead — the entry then
       simply fails its range check when someone tries to slice it. */
    size_t start = (size_t)offset;
    if (sector_aligned)
    {
      /* Dead code on 64-bit hosts, live protection on 32-bit ones: a u32 sector
         number times 0x800 cannot overflow size_t there either way, but the
         saturating form keeps the reasoning identical on every target. */
      const size_t max_sector = SIZE_MAX / PSX_SECTOR_SIZE;
      const size_t sector = (size_t)offset;
      start = sector > max_sector ? SIZE_MAX : sector * PSX_SECTOR_SIZE;
    }
    entries[i].offset = start;
    entries[i].size = (size_t)size;
    entries[i].virtual_size = (size_t)size;
    entries[i].parent = -1;
  }

  /* Pass 2: virtual entries (size == 0) point at a real entry's offset
     (virtual-entry resolution). */
  for (size_t i = 0; i < count; ++i)
  {
    const size_t base = MRG_ENTRY_TABLE_OFFSET + i * MRG_ENTRY_STRIDE;
    uint32_t size = 0u;
    if (psx_reader_u32(reader, base + 4u, &size) != PSX_OK || size != 0u)
      continue; /* unreadable or non-zero size: not a virtual entry */

    uint32_t offset = 0u;
    const psx_error_t error = psx_reader_u32(reader, base, &offset);
    if (error != PSX_OK)
      return PSX_ERROR_MRG_TABLE_TRUNCATED;

    int parent = -1;
    for (size_t j = 0; j < count; ++j)
    {
      const psx_mrg_entry_t candidate = entries[j];
      if (candidate.parent < 0 && candidate.size != 0u && candidate.offset == (size_t)offset)
      {
        parent = (int)j;
        break;
      }
    }

    if (parent >= 0)
    {
      const psx_mrg_entry_t entry_parent = entries[(size_t)parent];
      entries[i].offset = entry_parent.offset;
      entries[i].size = entry_parent.size;
      entries[i].virtual_size = 0u;
      entries[i].parent = parent;
      ++virtual_count;
    }
    else
    {
      /* No parent: the reference keeps the raw offset with size 0, i.e. an empty
         entry. Preserved so an inspector can report it as such. */
      entries[i].offset = (size_t)offset;
      entries[i].size = 0u;
      entries[i].virtual_size = 0u;
      entries[i].parent = -1;
    }
  }

  *out_view_count = virtual_count;
  return PSX_OK;
}

/* --- sniffing ------------------------------------------------------------ */

int psx_archive_is_mrg(psx_bytes_t src)
{
  const psx_reader_t reader = psx_reader_from(src);
  if (!psx_reader_fits(reader, 0u, 8u))
    return 0;
  uint32_t magic = 0u;
  if (psx_reader_u32(reader, 0u, &magic) != PSX_OK)
    return 0;
  return magic == PSX_ARCHIVE_MRG_MAGIC;
}

int psx_archive_is_deff(psx_bytes_t src)
{
  const psx_reader_t reader = psx_reader_from(src);
  if (!psx_reader_fits(reader, 0u, 8u))
    return 0;
  uint32_t magic = 0u;
  if (psx_reader_u32(reader, 0u, &magic) != PSX_OK)
    return 0;
  return magic == PSX_ARCHIVE_DEFF_MAGIC;
}

/* --- MRG: caller-owned storage ------------------------------------------- */

psx_error_t psx_archive_mrg_entry_count(psx_bytes_t src, int sector_aligned, size_t* out_count)
{
  (void)sector_aligned; /* The count does not depend on scaling; the parameter is
                           kept so the query and the parse take the same arguments
                           and cannot drift apart. */
  if (out_count == NULL || !psx_bytes_is_valid(src))
    return PSX_ERROR_INVALID_ARGUMENT;
  return mrg_read_header(src, out_count);
}

psx_error_t psx_archive_mrg_parse(psx_bytes_t src, int sector_aligned, psx_mrg_entry_t* entries, size_t entry_capacity,
                                  psx_mrg_view_t* out_view)
{
  if (out_view == NULL || !psx_bytes_is_valid(src))
    return PSX_ERROR_INVALID_ARGUMENT;

  size_t count = 0u;
  const psx_error_t error = mrg_read_header(src, &count);
  if (error != PSX_OK)
    return error;
  if (count > entry_capacity || (count > 0u && entries == NULL))
    return PSX_ERROR_INVALID_ARGUMENT;
  if (count == 0u)
  {
    out_view->entries = NULL;
    out_view->count = 0u;
    out_view->virtual_count = 0u;
    return PSX_OK;
  }

  size_t virtual_count = 0u;
  const psx_error_t filled = mrg_fill_entries(src, sector_aligned, entries, count, &virtual_count);
  if (filled != PSX_OK)
    return filled;

  out_view->entries = entries;
  out_view->count = count;
  out_view->virtual_count = virtual_count;
  return PSX_OK;
}

psx_error_t psx_mrg_view_at(psx_mrg_view_t view, size_t index, psx_mrg_entry_t* out_entry)
{
  if (out_entry == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  if (index >= view.count)
    return PSX_ERROR_MRG_ENTRY_MISSING;
  *out_entry = view.entries[index];
  return PSX_OK;
}

psx_error_t psx_mrg_view_slice(psx_mrg_view_t view, psx_bytes_t container, size_t index, psx_bytes_t* out_data)
{
  if (out_data == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  psx_mrg_entry_t entry;
  const psx_error_t error = psx_mrg_view_at(view, index, &entry);
  if (error != PSX_OK)
    return error;
  if (entry.size == 0u)
  {
    /* Empty entries (virtual with no parent) legitimately have no bytes. */
    out_data->data = NULL;
    out_data->size = 0u;
    return PSX_OK;
  }
  const psx_reader_t reader = psx_reader_from(container);
  if (!psx_bytes_is_valid(container) || !psx_reader_fits(reader, entry.offset, entry.size))
    return PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE;
  out_data->data = container.data + entry.offset;
  out_data->size = entry.size;
  return PSX_OK;
}

/* --- MRG: opaque handle -------------------------------------------------- */

/// @brief Heap-owned MRG table. Internal layout: only the accessors below touch
/// it, so the public type stays opaque.
struct psx_mrg_table
{
  psx_mrg_view_t view;
  psx_mrg_entry_t* storage; /* malloc'd; view.entries aliases it. */
};

psx_error_t psx_mrg_table_parse(psx_bytes_t src, int sector_aligned, psx_mrg_table_t** out_table)
{
  if (out_table == NULL || !psx_bytes_is_valid(src))
    return PSX_ERROR_INVALID_ARGUMENT;

  size_t count = 0u;
  const psx_error_t error = mrg_read_header(src, &count);
  if (error != PSX_OK)
    return error;

  psx_mrg_table_t* table = (psx_mrg_table_t*)malloc(sizeof(psx_mrg_table_t));
  if (table == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  table->storage = NULL;
  table->view.entries = NULL;
  table->view.count = 0u;
  table->view.virtual_count = 0u;

  if (count > 0u)
  {
    /* count is bounded by (src.size - 8) / 8 by mrg_read_header, so this
       multiplication cannot wrap on any realistic host; the guard keeps it
       provably safe rather than argued. */
    if (count > SIZE_MAX / sizeof(psx_mrg_entry_t))
    {
      free(table);
      return PSX_ERROR_INVALID_ARGUMENT;
    }
    table->storage = (psx_mrg_entry_t*)malloc(count * sizeof(psx_mrg_entry_t));
    if (table->storage == NULL)
    {
      free(table);
      return PSX_ERROR_INVALID_ARGUMENT;
    }

    size_t virtual_count = 0u;
    const psx_error_t filled = mrg_fill_entries(src, sector_aligned, table->storage, count, &virtual_count);
    if (filled != PSX_OK)
    {
      /* Deterministic cleanup on partial failure: nothing is leaked and no
         half-built handle escapes this function. */
      free(table->storage);
      free(table);
      return filled;
    }
    table->view.entries = table->storage;
    table->view.count = count;
    table->view.virtual_count = virtual_count;
  }

  *out_table = table;
  return PSX_OK;
}

void psx_mrg_table_destroy(psx_mrg_table_t* table)
{
  if (table == NULL)
    return;
  free(table->storage);
  free(table);
}

size_t psx_mrg_table_count(const psx_mrg_table_t* table)
{
  return table != NULL ? table->view.count : 0u;
}

size_t psx_mrg_table_virtual_count(const psx_mrg_table_t* table)
{
  return table != NULL ? table->view.virtual_count : 0u;
}

psx_error_t psx_mrg_table_view(const psx_mrg_table_t* table, psx_mrg_view_t* out_view)
{
  if (out_view == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  if (table == NULL)
  {
    out_view->entries = NULL;
    out_view->count = 0u;
    out_view->virtual_count = 0u;
    return PSX_OK;
  }
  *out_view = table->view;
  return PSX_OK;
}

/* --- DEFF: caller-owned storage ------------------------------------------ */

/// @brief DEFF header validation + subfile count (shared by query and parse).
/// @param src       Container bytes.
/// @param out_count Receives the declared subfile count.
/// @return PSX_OK, PSX_ERROR_TRUNCATED, PSX_ERROR_DEFF_MAGIC, or
///         PSX_ERROR_DEFF_TABLE_TRUNCATED.
static psx_error_t deff_read_header(psx_bytes_t src, size_t* out_count)
{
  const psx_reader_t reader = psx_reader_from(src);
  if (!psx_reader_fits(reader, 0u, 8u))
    return PSX_ERROR_TRUNCATED;

  uint32_t magic = 0u;
  psx_error_t error = psx_reader_u32(reader, 0u, &magic);
  if (error != PSX_OK)
    return error;
  if (magic != PSX_ARCHIVE_DEFF_MAGIC)
    return PSX_ERROR_DEFF_MAGIC;

  uint16_t count = 0u;
  error = psx_reader_u16(reader, 6u, &count);
  if (error != PSX_OK)
    return error;

  /* The table holds `count` starts at 8 + i*8 + 4, so the last one needs the
     buffer to reach 8 + (count-1)*8 + 4 + 4. */
  if (count > 0u)
  {
    const size_t last_field = 8u + ((size_t)count - 1u) * DEFF_ENTRY_STRIDE + 4u;
    if (!psx_reader_fits(reader, last_field, 4u))
      return PSX_ERROR_DEFF_TABLE_TRUNCATED;
  }

  *out_count = (size_t)count;
  return PSX_OK;
}

/// @brief Fill a DEFF subfile array from a validated container.
/// @param src    Container bytes (validated by deff_read_header).
/// @param entries Destination with `count` elements.
/// @param count  Element count (from deff_read_header).
/// @return PSX_OK, PSX_ERROR_DEFF_TABLE_TRUNCATED, or PSX_ERROR_DEFF_OFFSETS_BAD.
static psx_error_t deff_fill_entries(psx_bytes_t src, psx_deff_entry_t* entries, size_t count)
{
  const psx_reader_t reader = psx_reader_from(src);

  size_t written = 0u;
  size_t previous = 0u;
  int have_previous = 0;
  for (size_t i = 0; i < count; ++i)
  {
    uint32_t start = 0u;
    const psx_error_t error = psx_reader_u32(reader, 8u + i * DEFF_ENTRY_STRIDE + 4u, &start);
    if (error != PSX_OK)
      return PSX_ERROR_DEFF_TABLE_TRUNCATED;
    if (have_previous && (size_t)start < previous)
      return PSX_ERROR_DEFF_OFFSETS_BAD;
    if (have_previous)
    {
      entries[written].offset = previous;
      entries[written].size = (size_t)start - previous;
      ++written;
    }
    previous = (size_t)start;
    have_previous = 1;
  }

  if (count > 0u)
  {
    if (previous > src.size)
      return PSX_ERROR_DEFF_OFFSETS_BAD;
    /* The final subfile runs to the end of the container. */
    entries[written].offset = previous;
    entries[written].size = src.size - previous;
    ++written;
  }
  return PSX_OK;
}

psx_error_t psx_archive_deff_subfile_count(psx_bytes_t src, size_t* out_count)
{
  if (out_count == NULL || !psx_bytes_is_valid(src))
    return PSX_ERROR_INVALID_ARGUMENT;
  return deff_read_header(src, out_count);
}

psx_error_t psx_archive_deff_parse(psx_bytes_t src, psx_deff_entry_t* entries, size_t entry_capacity,
                                   psx_deff_view_t* out_view)
{
  if (out_view == NULL || !psx_bytes_is_valid(src))
    return PSX_ERROR_INVALID_ARGUMENT;

  size_t count = 0u;
  const psx_error_t error = deff_read_header(src, &count);
  if (error != PSX_OK)
    return error;
  if (count > entry_capacity || (count > 0u && entries == NULL))
    return PSX_ERROR_INVALID_ARGUMENT;
  if (count == 0u)
  {
    out_view->entries = NULL;
    out_view->count = 0u;
    return PSX_OK;
  }

  const psx_error_t filled = deff_fill_entries(src, entries, count);
  if (filled != PSX_OK)
    return filled;

  out_view->entries = entries;
  out_view->count = count;
  return PSX_OK;
}

psx_error_t psx_deff_view_at(psx_deff_view_t view, size_t index, psx_deff_entry_t* out_entry)
{
  if (out_entry == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  if (index >= view.count)
    return PSX_ERROR_DEFF_ENTRY_MISSING;
  *out_entry = view.entries[index];
  return PSX_OK;
}

psx_error_t psx_deff_view_slice(psx_deff_view_t view, psx_bytes_t container, size_t index, psx_bytes_t* out_data)
{
  if (out_data == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  psx_deff_entry_t entry;
  const psx_error_t at = psx_deff_view_at(view, index, &entry);
  if (at != PSX_OK)
    return at;
  const psx_reader_t reader = psx_reader_from(container);
  if (!psx_bytes_is_valid(container) || !psx_reader_fits(reader, entry.offset, entry.size))
  {
    /* Preserved from the C++ API: a DEFF range that does not fit reports
       DEFF_ENTRY_MISSING rather than a dedicated out-of-range code. Changing it
       would be an ABI break for no behavioural gain; the message spells it out. */
    return PSX_ERROR_DEFF_ENTRY_MISSING;
  }
  out_data->data = container.data + entry.offset;
  out_data->size = entry.size;
  return PSX_OK;
}

/* --- DEFF: opaque handle ------------------------------------------------- */

/// @brief Heap-owned DEFF table (see psx_mrg_table for the same pattern).
struct psx_deff_table
{
  psx_deff_view_t view;
  psx_deff_entry_t* storage;
};

psx_error_t psx_deff_table_parse(psx_bytes_t src, psx_deff_table_t** out_table)
{
  if (out_table == NULL || !psx_bytes_is_valid(src))
    return PSX_ERROR_INVALID_ARGUMENT;

  size_t count = 0u;
  const psx_error_t error = deff_read_header(src, &count);
  if (error != PSX_OK)
    return error;

  psx_deff_table_t* table = (psx_deff_table_t*)malloc(sizeof(psx_deff_table_t));
  if (table == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  table->storage = NULL;
  table->view.entries = NULL;
  table->view.count = 0u;

  if (count > 0u)
  {
    if (count > SIZE_MAX / sizeof(psx_deff_entry_t))
    {
      free(table);
      return PSX_ERROR_INVALID_ARGUMENT;
    }
    table->storage = (psx_deff_entry_t*)malloc(count * sizeof(psx_deff_entry_t));
    if (table->storage == NULL)
    {
      free(table);
      return PSX_ERROR_INVALID_ARGUMENT;
    }
    const psx_error_t filled = deff_fill_entries(src, table->storage, count);
    if (filled != PSX_OK)
    {
      free(table->storage);
      free(table);
      return filled;
    }
    table->view.entries = table->storage;
    table->view.count = count;
  }

  *out_table = table;
  return PSX_OK;
}

void psx_deff_table_destroy(psx_deff_table_t* table)
{
  if (table == NULL)
    return;
  free(table->storage);
  free(table);
}

size_t psx_deff_table_count(const psx_deff_table_t* table)
{
  return table != NULL ? table->view.count : 0u;
}

psx_error_t psx_deff_table_view(const psx_deff_table_t* table, psx_deff_view_t* out_view)
{
  if (out_view == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  if (table == NULL)
  {
    out_view->entries = NULL;
    out_view->count = 0u;
    return PSX_OK;
  }
  *out_view = table->view;
  return PSX_OK;
}
