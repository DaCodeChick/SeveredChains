/// @file bpe.c
/// @brief BPE codec implementation in C17: the disc's adaptive byte-pair format,
/// with bounds checking added and every unchecked assumption of the original
/// turned into an explicit error. Layout, provenance and deviations:
/// docs/research/psx-formats.md §1.
///
/// Mapping from the original algorithm (kept deliberately close for
/// reviewability):
///   archive.readInt(0x4) != 0x1a455042   -> magic check, PSX_ERROR_BPE_MAGIC
///   new byte[archive.readInt(0)]         -> declared size, bounded by the caller's capacity
///   bytes_remaining_in_block > 0x800     -> PSX_ERROR_BPE_BLOCK_SIZE
///   dictionary build (key < 0x100 loop)  -> build_dictionary(), PSX_ERROR_BPE_DICTIONARY_OVERFLOW
///   unresolved_byte_list expansion       -> pending stack below, PSX_ERROR_BPE_EXPANSION_OVERFLOW
///   word alignment after each block      -> align4()

#include "psx/bpe.h"

#include "reader.h"

#include <stddef.h>
#include <stdint.h>

/// @brief Per-block dictionary: 256 keys, each a left byte and (for non-identity
/// keys) a right byte.
typedef struct bpe_dictionary
{
  uint8_t left_ch[0x100];
  uint8_t right_ch[0x100];
} bpe_dictionary_t;

/// @brief Capacity of the pending (LIFO) symbol stack.
///
/// A well-formed stream can never hold more pending symbols than the block still
/// owes, and a block owes at most PSX_BPE_MAX_BLOCK_SIZE bytes; the two pushes of
/// a single expansion are checked against that bound before they can exceed it.
/// The extra slots make the writes below provably in-bounds even for the transient
/// state between a push and the check.
#define BPE_PENDING_CAPACITY (PSX_BPE_MAX_BLOCK_SIZE + 4u)

/// @brief Reset a dictionary to the identity state every block starts from:
/// right table all zero, left[i] == i.
/// @param dictionary Dictionary to reset.
static void bpe_dictionary_reset(bpe_dictionary_t* dictionary)
{
  for (size_t i = 0; i < 0x100u; ++i)
  {
    dictionary->left_ch[i] = (uint8_t)i;
    dictionary->right_ch[i] = 0u;
  }
}

/// @brief Round an offset up to the next 4-byte boundary (the block cursor is
/// word-aligned between blocks).
/// @param offset Offset to round.
/// @return The next 4-byte aligned value (saturating-free: callers keep offsets
///         far below SIZE_MAX because every advance is bounds-checked first).
static size_t bpe_align4(size_t offset)
{
  const size_t rem = offset % 4u;
  return rem == 0u ? offset : offset + (4u - rem);
}

/// @brief Read one block's adaptive dictionary.
/// @param reader     Reader over the segment.
/// @param io_offset  in/out: first byte of the dictionary; advanced past it.
/// @param dictionary Dictionary to fill (already reset).
/// @return PSX_OK, PSX_ERROR_TRUNCATED, or PSX_ERROR_BPE_DICTIONARY_OVERFLOW.
static psx_error_t bpe_build_dictionary(psx_reader_t reader, size_t* io_offset, bpe_dictionary_t* dictionary)
{
  size_t offset = *io_offset;
  size_t key = 0;
  while (key < 0x100u)
  {
    /* A control byte >= 0x80 does not count pairs: it jumps the key forward and
       reads a single entry. Below 0x80 it is the number of sequential
       {left,right} pairs to read. */
    uint8_t control = 0u;
    psx_error_t error = psx_reader_u8(reader, offset, &control);
    if (error != PSX_OK)
      return error;
    offset += 1u;

    size_t pairs = control;
    if (control >= 0x80u)
    {
      /* The reference spelling is `key - 0x7f + byte_pairs_to_read` (Java ints, so
         the intermediate can go negative). control >= 0x80 makes the result
         key + (control - 0x7f) >= key + 1, which is both the same value and
         provably non-negative in unsigned arithmetic — and it guarantees the loop
         advances, so a malformed stream cannot spin here. */
      key += (size_t)control - 0x7fu;
      pairs = 0u;
    }

    if (key >= 0x100u)
    {
      /* The reference guards the entry loop with `if (key < 0x100)` and then
         exits, because the while condition is no longer satisfied. */
      *io_offset = offset;
      return PSX_OK;
    }

    for (size_t i = 0; i < pairs + 1u; ++i)
    {
      if (key >= 0x100u)
      {
        /* The reference writes dict_leftch[key] here and takes an
           ArrayIndexOutOfBounds; here it is an explicit malformed-input error. */
        *io_offset = offset;
        return PSX_ERROR_BPE_DICTIONARY_OVERFLOW;
      }

      uint8_t left = 0u;
      error = psx_reader_u8(reader, offset, &left);
      if (error != PSX_OK)
      {
        *io_offset = offset;
        return error;
      }
      offset += 1u;
      dictionary->left_ch[key] = left;

      if ((size_t)left != key)
      {
        uint8_t right = 0u;
        error = psx_reader_u8(reader, offset, &right);
        if (error != PSX_OK)
        {
          *io_offset = offset;
          return error;
        }
        offset += 1u;
        dictionary->right_ch[key] = right;
      }

      ++key;
    }
  }

  *io_offset = offset;
  return PSX_OK;
}

int psx_bpe_is_bpe(psx_bytes_t src)
{
  const psx_reader_t reader = psx_reader_from(src);
  if (!psx_reader_fits(reader, 0u, 8u))
    return 0;
  uint32_t magic = 0u;
  if (psx_reader_u32(reader, 4u, &magic) != PSX_OK)
    return 0;
  return magic == PSX_BPE_MAGIC;
}

psx_error_t psx_bpe_declared_size(psx_bytes_t src, size_t* out_size)
{
  if (out_size == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  if (!psx_bytes_is_valid(src))
    return PSX_ERROR_INVALID_ARGUMENT;

  const psx_reader_t reader = psx_reader_from(src);
  if (!psx_reader_fits(reader, 0u, 8u))
    return PSX_ERROR_TRUNCATED;

  uint32_t magic = 0u;
  psx_error_t error = psx_reader_u32(reader, 4u, &magic);
  if (error != PSX_OK)
    return error;
  if (magic != PSX_BPE_MAGIC)
    return PSX_ERROR_BPE_MAGIC;

  uint32_t declared = 0u;
  error = psx_reader_u32(reader, 0u, &declared);
  if (error != PSX_OK)
    return error;
  *out_size = (size_t)declared;
  return PSX_OK;
}

psx_error_t psx_bpe_decompress(psx_bytes_t src, psx_writable_bytes_t out, size_t* out_written)
{
  if (out_written == NULL || !psx_bytes_is_valid(src) || !psx_writable_bytes_is_valid(out))
    return PSX_ERROR_INVALID_ARGUMENT;

  size_t declared = 0u;
  psx_error_t error = psx_bpe_declared_size(src, &declared);
  if (error != PSX_OK)
    return error;
  if (declared > out.capacity)
    return PSX_ERROR_BPE_OUTPUT_TOO_LARGE;
  if (declared > 0u && out.data == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;

  const psx_reader_t reader = psx_reader_from(src);
  bpe_dictionary_t dictionary;
  uint8_t pending[BPE_PENDING_CAPACITY];
  size_t pending_count = 0u;
  size_t offset = 8u;
  size_t produced = 0u;

  for (;;)
  {
    uint32_t block_header = 0u;
    error = psx_reader_u32(reader, offset, &block_header);
    if (error != PSX_OK)
      return error;
    offset += 4u;

    const size_t block_size = block_header;
    if (block_size == 0u)
      break; /* "0x00000000 indicates that there are no further blocks" */
    if (block_size > PSX_BPE_MAX_BLOCK_SIZE)
      return PSX_ERROR_BPE_BLOCK_SIZE;
    if (produced > declared || block_size > declared - produced)
    {
      /* Blocks would overrun the declared destination (the reference would walk
         off the end of `dest`). */
      return PSX_ERROR_BPE_SIZE_MISMATCH;
    }

    bpe_dictionary_reset(&dictionary);
    size_t dictionary_offset = offset;
    error = bpe_build_dictionary(reader, &dictionary_offset, &dictionary);
    offset = dictionary_offset;
    if (error != PSX_OK)
      return error;

    size_t remaining = block_size;
    pending_count = 0u;

    /* One code per pass into a LIFO of unresolved bytes; expanding a code pushes
       right then left so the left byte resolves first. */
    while (remaining > 0u)
    {
      uint8_t code = 0u;
      error = psx_reader_u8(reader, offset, &code);
      if (error != PSX_OK)
        return error;
      offset += 1u;
      if (pending_count >= BPE_PENDING_CAPACITY)
        return PSX_ERROR_BPE_EXPANSION_OVERFLOW;
      pending[pending_count++] = code;

      while (pending_count > 0u)
      {
        const uint8_t symbol = pending[pending_count - 1u];
        pending_count -= 1u;

        if (symbol == dictionary.left_ch[symbol])
        {
          out.data[produced++] = symbol;
          remaining -= 1u;
        }
        else
        {
          if (pending_count + 2u > BPE_PENDING_CAPACITY)
            return PSX_ERROR_BPE_EXPANSION_OVERFLOW;
          pending[pending_count++] = dictionary.right_ch[symbol];
          pending[pending_count++] = dictionary.left_ch[symbol];
          /* Every pending symbol resolves to at least one output byte, so a
             well-formed stream can never hold more pending symbols than the block
             still owes. Exceeding that means the dictionary maps a code to a
             cycle, which no valid segment can produce. */
          if (pending_count > remaining)
            return PSX_ERROR_BPE_EXPANSION_OVERFLOW;
        }
      }
    }

    offset = bpe_align4(offset);
  }

  if (produced != declared)
    return PSX_ERROR_BPE_SIZE_MISMATCH;
  *out_written = produced;
  return PSX_OK;
}

psx_error_t psx_bpe_compressed_size(size_t raw_size, size_t* out_size)
{
  if (out_size == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  if (raw_size > 0xFFFFFFFFu)
    return PSX_ERROR_BPE_OUTPUT_TOO_LARGE;

  /* Identity dictionary: two runs of 0x7f, each followed by the 128 bytes whose
     value equals their key, so left[key] == key for all 256 keys. */
  const size_t identity_dictionary_bytes = 2u * (1u + 0x80u);
  size_t length = 8u;
  for (size_t consumed = 0u; consumed < raw_size;)
  {
    const size_t chunk =
      (PSX_BPE_MAX_BLOCK_SIZE < raw_size - consumed) ? (size_t)PSX_BPE_MAX_BLOCK_SIZE : (raw_size - consumed);
    const size_t block_bytes = 4u + identity_dictionary_bytes + chunk;
    if (length > SIZE_MAX - block_bytes)
    {
      /* Only reachable on a 32-bit size_t with a near-4 GiB input; refusing beats
         reporting a size that wrapped. */
      return PSX_ERROR_BPE_OUTPUT_TOO_LARGE;
    }
    length = bpe_align4(length + block_bytes);
    consumed += chunk;
  }
  if (length > SIZE_MAX - 4u)
    return PSX_ERROR_BPE_OUTPUT_TOO_LARGE;
  *out_size = length + 4u;
  return PSX_OK;
}

/// @brief Append a little-endian u32 to the output stream.
/// @param out      Destination.
/// @param io_write in/out cursor; advanced by 4 on success.
/// @param value    Value to append.
/// @return PSX_OK or PSX_ERROR_BPE_OUTPUT_TOO_LARGE (capacity exhausted).
static psx_error_t bpe_push_u32(psx_writable_bytes_t out, size_t* io_write, uint32_t value)
{
  if (*io_write + 4u > out.capacity)
    return PSX_ERROR_BPE_OUTPUT_TOO_LARGE;
  out.data[*io_write] = (uint8_t)(value & 0xffu);
  out.data[*io_write + 1u] = (uint8_t)((value >> 8u) & 0xffu);
  out.data[*io_write + 2u] = (uint8_t)((value >> 16u) & 0xffu);
  out.data[*io_write + 3u] = (uint8_t)((value >> 24u) & 0xffu);
  *io_write += 4u;
  return PSX_OK;
}

/// @brief Append one identity dictionary (literal pass-through) to the stream.
/// @param out        Destination.
/// @param io_write   in/out cursor.
/// @return PSX_OK or PSX_ERROR_BPE_OUTPUT_TOO_LARGE.
static psx_error_t bpe_append_identity_dictionary(psx_writable_bytes_t out, size_t* io_write)
{
  for (size_t run = 0; run < 2u; ++run)
  {
    if (*io_write + 1u + 0x80u > out.capacity)
      return PSX_ERROR_BPE_OUTPUT_TOO_LARGE;
    out.data[(*io_write)++] = 0x7fu; /* read 0x7f + 1 = 128 entries */
    for (size_t i = 0; i < 0x80u; ++i)
      out.data[(*io_write)++] = (uint8_t)(run * 0x80u + i);
  }
  return PSX_OK;
}

psx_error_t psx_bpe_compress(psx_bytes_t raw, psx_writable_bytes_t out, size_t* out_written)
{
  if (out_written == NULL || !psx_bytes_is_valid(raw) || !psx_writable_bytes_is_valid(out))
    return PSX_ERROR_INVALID_ARGUMENT;

  size_t required = 0u;
  psx_error_t error = psx_bpe_compressed_size(raw.size, &required);
  if (error != PSX_OK)
    return error;
  if (required > out.capacity)
    return PSX_ERROR_BPE_OUTPUT_TOO_LARGE;
  if (required > 0u && out.data == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;

  size_t write = 0u;
  error = bpe_push_u32(out, &write, (uint32_t)raw.size);
  if (error != PSX_OK)
    return error;
  error = bpe_push_u32(out, &write, PSX_BPE_MAGIC);
  if (error != PSX_OK)
    return error;

  size_t consumed = 0u;
  while (consumed < raw.size)
  {
    const size_t chunk =
      ((size_t)PSX_BPE_MAX_BLOCK_SIZE < raw.size - consumed) ? (size_t)PSX_BPE_MAX_BLOCK_SIZE : (raw.size - consumed);
    error = bpe_push_u32(out, &write, (uint32_t)chunk);
    if (error != PSX_OK)
      return error;
    error = bpe_append_identity_dictionary(out, &write);
    if (error != PSX_OK)
      return error;
    for (size_t i = 0; i < chunk; ++i)
      out.data[write++] = raw.data[consumed + i];
    consumed += chunk;
    write = bpe_align4(write); /* block padding, exactly as the framing requires */
  }

  error = bpe_push_u32(out, &write, 0u); /* end of stream */
  if (error != PSX_OK)
    return error;

  *out_written = write;
  return PSX_OK;
}
