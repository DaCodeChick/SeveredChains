/* xa.c — PS1 XA 4-bit ADPCM sector decode (psx/xa.h).
 *
 * This is a transcription of the reference decoder, and two of its details are
 * load-bearing for sample-exact agreement, so they are written out explicitly
 * instead of being "tidied" into idiomatic C:
 *
 *  1. The prediction term uses truncating integer division, which both Java and
 *     C apply toward zero. A floor-dividing reformulation would differ on every
 *     negative prediction, so the plain `/` on a signed operand stays.
 *  2. `shift = 12 - (control & 0x0F)` reaches -3 when the control nibble is
 *     13..15. Java evaluates `t << shift` with the shift count masked to 5 bits
 *     and the product wrapping in 32 bits; C leaves a negative or over-wide
 *     shift undefined. The masked form is therefore computed in uint32_t so the
 *     result is defined here and equal there. For control nibbles 0..12 — the
 *     range real encoders emit — both readings already agree; the masking only
 *     matters for malformed data, where it keeps the two implementations
 *     bit-identical instead of merely "both wrong somehow".
 *
 * Byte layout is likewise the reference's, not the documented CD-ROM XA block
 * (which is an 8-byte header over 120 data bytes, with mono/stereo marked the
 * other way round in the coding byte). See the note at the top of psx/xa.h:
 * reproducing the reference here is a deliberate choice of oracle, and it is
 * wrong in a way that has not been checked against hardware.
 */

#include "psx/xa.h"

#include "psx/bytes.h"
#include "psx/error.h"

#include <stddef.h>
#include <stdint.h>

enum
{
  XA_HEADER = 24,
  XA_PORTIONS = 18,
  XA_PORTION_SIZE = 128,
  XA_BLOCKS = 4,
  XA_SAMPLES_PER_BLOCK = 28,

  /* Bytes the decoder actually touches: the 24-byte header is skipped, then 18
   * x 128 bytes of portions. The reference is handed a whole 2352-byte sector
   * and stops 24 bytes short of the end (EDC/ECC), so 2328 is the true
   * minimum rather than 2352. */
  XA_MIN_SECTOR = XA_HEADER + XA_PORTIONS * XA_PORTION_SIZE,

  XA_STEREO_SAMPLES = XA_PORTIONS * XA_BLOCKS * XA_SAMPLES_PER_BLOCK * 2u,
  XA_MONO_SAMPLES = XA_PORTIONS * XA_BLOCKS * XA_SAMPLES_PER_BLOCK * 2u * 2u
};

/* ADPCM prediction coefficients indexed by the 2-bit filter field. The
 * reference carries five entries per table; index 4 is unreachable because the
 * filter is masked to 0x30 >> 4, so only the reachable four are stored. */
static const int32_t XA_FILTER_POSITIVE[4] = {0, 60, 115, 98};
static const int32_t XA_FILTER_NEGATIVE[4] = {0, 0, -52, -55};

void psx_xa_stream_init(psx_xa_stream_t* stream)
{
  if (stream == NULL)
    return;
  for (size_t i = 0; i < 2; ++i)
  {
    stream->prev[i] = 0;
    stream->prev2[i] = 0;
  }
}

psx_error_t psx_xa_coding_info(psx_bytes_t sector, uint8_t* out_info)
{
  if (out_info == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  if (!psx_bytes_is_valid(sector))
    return PSX_ERROR_INVALID_ARGUMENT;
  if (sector.size <= (size_t)PSX_XA_CODING_INFO_OFFSET)
    return PSX_ERROR_TRUNCATED;
  *out_info = sector.data[PSX_XA_CODING_INFO_OFFSET];
  return PSX_OK;
}

psx_error_t psx_xa_decoded_samples(uint8_t coding_info, size_t* out_samples)
{
  if (out_samples == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  /* Bit 4 marks 8-bit samples. Neither this decoder nor the reference has an
   * 8-bit path, and decoding 8-bit data as nibbles would produce noise rather
   * than an error, so it is refused at the door. */
  if ((coding_info & PSX_XA_FLAG_8BIT) != 0u)
    return PSX_ERROR_XA_UNSUPPORTED_CODING;
  *out_samples = ((coding_info & PSX_XA_FLAG_STEREO) != 0u) ? (size_t)XA_STEREO_SAMPLES : (size_t)XA_MONO_SAMPLES;
  return PSX_OK;
}

/* Append one sample to the byte output, little-endian, advancing *io by two.
 * Kept byte-wise on purpose: see the note in psx_xa_decode_sector. */
static void put_sample(uint8_t* dst, size_t* io, int16_t v)
{
  const uint16_t u = (uint16_t)v;
  dst[(*io)++] = (uint8_t)(u & 0xFFu);
  dst[(*io)++] = (uint8_t)(u >> 8u);
}

/* Decode one 28-sample nibble run, mirroring XaAdpcm.decodeNibbles.
 *
 * `control` indexes the shift/filter byte, `data` the first sample byte; both
 * are offsets from the start of the sector. `nibble` 0 takes the low nibble of
 * each sample byte and 1 the high nibble, which is how XA interleaves a stereo
 * pair. `prev`/`prev2` are the channel's predictor taps and are updated in
 * place so a following run continues the same filter chain.
 *
 * Writing straight through to `out` rather than returning a list keeps the
 * function allocation-free; the caller has already proved it has room for
 * XA_SAMPLES_PER_BLOCK entries. */
static void decode_nibbles(const uint8_t* sector, size_t control, size_t data, unsigned nibble, int16_t* prev,
                           int16_t* prev2, int16_t* out)
{
  const uint8_t control_byte = sector[control];
  const int32_t shift = (int32_t)(12 - (control_byte & 0x0F));
  const uint32_t shift_bits = ((uint32_t)shift) & 31u;
  const uint32_t filter = ((uint32_t)control_byte & 0x30u) >> 4;

  const int32_t f0 = XA_FILTER_POSITIVE[filter];
  const int32_t f1 = XA_FILTER_NEGATIVE[filter];

  for (int i = 0; i < XA_SAMPLES_PER_BLOCK; ++i)
  {
    /* One byte carries two samples: low nibble then high nibble, 4 bytes apart
     * per block (the reference's `position + 16 + blk + i * 4`). */
    const uint32_t packed = (uint32_t)sector[data + (size_t)i * 4u];
    uint32_t nibble_value = (packed >> (nibble * 4u)) & 0x0Fu;

    /* Sign-extend the 4-bit two's-complement sample. Equivalent to the
     * reference's `value << 28 >> 28`, without relying on the sign behaviour of
     * >> on a negative operand. */
    if (nibble_value > 7u)
      nibble_value -= 16u;

    const int32_t t = (int32_t)nibble_value;

    /* (t << shift) wrapping, then the prediction term with truncating divide —
     * see note 1 and 2 at the top of the file. */
    const uint32_t shifted = (uint32_t)t << shift_bits;
    const int32_t prediction = (int32_t)(((int32_t)*prev * f0) + ((int32_t)*prev2 * f1) + 32) / 64;
    const int32_t sum = (int32_t)(uint32_t)((uint32_t)(int32_t)shifted + (uint32_t)prediction);

    int32_t clamped = sum;
    if (clamped < -32768)
      clamped = -32768;
    if (clamped > 32767)
      clamped = 32767;

    out[i] = (int16_t)clamped;
    *prev2 = *prev;
    *prev = (int16_t)clamped;
  }
}

psx_error_t psx_xa_decode_sector(psx_xa_stream_t* stream, psx_bytes_t sector, uint8_t coding_info,
                                 psx_writable_bytes_t out, size_t* out_samples)
{
  if (out_samples == NULL)
    return PSX_ERROR_INVALID_ARGUMENT;
  *out_samples = 0;

  if (!psx_bytes_is_valid(sector) || !psx_writable_bytes_is_valid(out))
    return PSX_ERROR_INVALID_ARGUMENT;

  size_t needed = 0;
  const psx_error_t count_result = psx_xa_decoded_samples(coding_info, &needed);
  if (count_result != PSX_OK)
    return count_result;

  if (sector.size < (size_t)XA_MIN_SECTOR)
    return PSX_ERROR_TRUNCATED;
  if (out.capacity < needed * 2u)
    return PSX_ERROR_XA_OUTPUT_TOO_LARGE;

  /* A NULL stream decodes from the zero state and does not write history back,
   * which is the one-shot case. A non-NULL stream is read *and* updated, so the
   * caller can chain sectors of one track. */
  psx_xa_stream_t local;
  psx_xa_stream_init(&local);
  if (stream != NULL)
    local = *stream;

  const int stereo = (coding_info & PSX_XA_FLAG_STEREO) != 0u;

  /* Byte cursor into `out.data`. Samples are written two bytes at a time rather
   * than through an int16_t* cast over the buffer: `psx_writable_bytes_t` is a
   * byte range by contract, and reinterpreting it as int16_t* is both a C
   * constraint violation and strict-aliasing UB. Little-endian also matches what
   * the SPU voice mixer consumes downstream. */
  size_t w = 0;

  /* One block's worth of decoded samples, staged here before being widened to
   * the interleaved pairs the caller expects. The reference builds two lists and
   * joins them afterwards; staging is the same sequence without the allocation.
   * Both modes fill all 56 entries: stereo as 28 left then 28 right, mono as the
   * low-nibble run then the high-nibble run. */
  int16_t scratch[XA_SAMPLES_PER_BLOCK * 2];

  for (int portion = 0; portion < XA_PORTIONS; ++portion)
  {
    const size_t base = (size_t)XA_HEADER + (size_t)portion * (size_t)XA_PORTION_SIZE;

    for (int block = 0; block < XA_BLOCKS; ++block)
    {
      const size_t control = base + 4u + (size_t)block * 2u;
      const size_t data = base + 16u + (size_t)block;

      if (stereo)
      {
        /* Left takes the low nibble and advances the left chain; right takes the
         * high nibble and advances the right chain. The reference concatenates
         * all lefts and all rights and interleaves them at the end; doing it per
         * block writes the same sequence, because the two chains advance at the
         * same rate and so stay index-aligned. */
        decode_nibbles(sector.data, control, data, 0u, &local.prev[0], &local.prev2[0], &scratch[0]);
        decode_nibbles(sector.data, control + 1u, data, 1u, &local.prev[1], &local.prev2[1],
                       &scratch[XA_SAMPLES_PER_BLOCK]);

        for (int i = 0; i < XA_SAMPLES_PER_BLOCK; ++i)
        {
          put_sample(out.data, &w, scratch[i]);
          put_sample(out.data, &w, scratch[XA_SAMPLES_PER_BLOCK + i]);
        }
      }
      else
      {
        /* Mono drives one shared left-chain filter through both nibbles, then
         * widens each sample to a pair. Sharing the chain across nibble 0 and
         * nibble 1 is what the reference does, so it is reproduced here. */
        decode_nibbles(sector.data, control, data, 0u, &local.prev[0], &local.prev2[0], &scratch[0]);
        decode_nibbles(sector.data, control + 1u, data, 1u, &local.prev[0], &local.prev2[0],
                       &scratch[XA_SAMPLES_PER_BLOCK]);

        for (int i = 0; i < XA_SAMPLES_PER_BLOCK * 2; ++i)
        {
          put_sample(out.data, &w, scratch[i]);
          put_sample(out.data, &w, scratch[i]);
        }
      }
    }
  }

  *out_samples = w / 2u;
  if (stream != NULL)
    *stream = local;
  return PSX_OK;
}