#pragma once
/// @file reader.h
/// @brief Internal bounds-checked little-endian reader for PSX binary data.
/// Not a public header: psx-library.md §5 keeps the public surface to
/// buffer-in / checked-result-out (docs/architecture/psx-c-api.md §3).
///
/// Every read is bounds-checked and overflow-safe, so untrusted input can only
/// produce a PSX_ERROR_* code, never an out-of-bounds access. The PS1 (R3000A) is
/// little-endian, so this reader states that order explicitly instead of
/// inheriting the host's (docs/research/psx-formats.md §5). Multi-byte fields are
/// assembled byte by byte, which is both alignment-independent and free of strict
/// aliasing.
///
/// @note Thread: value type, no shared state — thread-safe, reentrant.

#include "psx/bytes.h"
#include "psx/error.h"

#include <stddef.h>
#include <stdint.h>

/// @brief Non-owning cursor over a byte buffer with checked LE accessors.
typedef struct psx_reader
{
  const uint8_t* data; ///< Buffer start (NULL is legal when size is 0).
  size_t size;         ///< Buffer length.
} psx_reader_t;

/// @brief Wrap a buffer (may be empty; every accessor then fails cleanly).
/// @param bytes Bytes to read; not owned, must outlive the reader.
/// @return A reader over `bytes`.
static inline psx_reader_t psx_reader_from(psx_bytes_t bytes)
{
  psx_reader_t reader;
  reader.data = bytes.data;
  reader.size = bytes.size;
  return reader;
}

/// @brief True when [at, at + len) lies entirely inside the buffer.
/// @param reader The reader.
/// @param at     Start offset.
/// @param len    Length to test.
/// @return Non-zero when the range is addressable (overflow-safe: computed as
///         `at <= size && len <= size - at`, never `at + len`).
static inline int psx_reader_fits(psx_reader_t reader, size_t at, size_t len)
{
  return at <= reader.size && len <= reader.size - at;
}

/// @brief Read one byte.
/// @param reader The reader.
/// @param at     Offset.
/// @param out    Receives the byte; unmodified on failure.
/// @return PSX_OK or PSX_ERROR_TRUNCATED.
static inline psx_error_t psx_reader_u8(psx_reader_t reader, size_t at, uint8_t* out)
{
  if (!psx_reader_fits(reader, at, 1u))
    return PSX_ERROR_TRUNCATED;
  *out = reader.data[at];
  return PSX_OK;
}

/// @brief Read a little-endian u16.
/// @param reader The reader.
/// @param at     Offset (unaligned offsets are legal on the host).
/// @param out    Receives the value; unmodified on failure.
/// @return PSX_OK or PSX_ERROR_TRUNCATED.
static inline psx_error_t psx_reader_u16(psx_reader_t reader, size_t at, uint16_t* out)
{
  if (!psx_reader_fits(reader, at, 2u))
    return PSX_ERROR_TRUNCATED;
  *out = (uint16_t)((uint16_t)reader.data[at] | (uint16_t)((uint16_t)reader.data[at + 1u] << 8u));
  return PSX_OK;
}

/// @brief Read a little-endian signed i16 (TIM rects are signed).
/// @param reader The reader.
/// @param at     Offset.
/// @param out    Receives the value; unmodified on failure.
/// @return PSX_OK or PSX_ERROR_TRUNCATED.
static inline psx_error_t psx_reader_i16(psx_reader_t reader, size_t at, int16_t* out)
{
  uint16_t raw = 0u;
  const psx_error_t error = psx_reader_u16(reader, at, &raw);
  if (error != PSX_OK)
    return error;
  /* Deliberate narrowing: the PS1 stores these as two's-complement i16, and
     every toolchain targeted represents int16_t in two's complement (C23 makes
     it the standard; C17 leaves it implementation-defined but the value is
     round-tripped through the same width, so the observable result is the
     on-disc code). */
  *out = (int16_t)raw;
  return PSX_OK;
}

/// @brief Read a little-endian u32.
/// @param reader The reader.
/// @param at     Offset.
/// @param out    Receives the value; unmodified on failure.
/// @return PSX_OK or PSX_ERROR_TRUNCATED.
static inline psx_error_t psx_reader_u32(psx_reader_t reader, size_t at, uint32_t* out)
{
  if (!psx_reader_fits(reader, at, 4u))
    return PSX_ERROR_TRUNCATED;
  const uint32_t b0 = reader.data[at];
  const uint32_t b1 = reader.data[at + 1u];
  const uint32_t b2 = reader.data[at + 2u];
  const uint32_t b3 = reader.data[at + 3u];
  *out = b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u);
  return PSX_OK;
}
