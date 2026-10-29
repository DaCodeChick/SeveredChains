/// @file main.c
/// @brief Pure C17 consumer of the psx shared library: compiled with a C compiler
/// only, linked against libpsx, exercising every documented entry point including
/// the malformed-input contract (testing.md §2.11, psx-c-api.md §8).
///
/// This file deliberately uses nothing from C++ and includes nothing but
/// <stdio.h>, <string.h> and "psx/psx.h". If it compiles and passes, the C API is
/// usable standalone — one of the migration's completion criteria.
///
/// @return Number of failed checks (0 = everything passed).

#include "psx/psx.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(Condition)                                                                                               \
  do                                                                                                                   \
  {                                                                                                                    \
    ++g_checks;                                                                                                        \
    if (!(Condition))                                                                                                  \
    {                                                                                                                  \
      ++g_failed;                                                                                                      \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #Condition);                                                    \
    }                                                                                                                  \
  } while (0)

/// @brief Compare two sizes with a readable message.
static void check_eq_u64(unsigned long long actual, unsigned long long expected, const char* what)
{
  ++g_checks;
  if (actual != expected)
  {
    ++g_failed;
    printf("  FAIL %s:%d: %s: got %llu, want %llu\n", __FILE__, __LINE__, what, actual, expected);
  }
}

/// @brief Compare error codes by name, so a wrong code names both sides.
static void check_eq_err(psx_error_t actual, psx_error_t expected, const char* what)
{
  ++g_checks;
  if (actual != expected)
  {
    ++g_failed;
    printf("  FAIL %s:%d: %s: got %s (%d), want %s (%d)\n", __FILE__, __LINE__, what, psx_error_name(actual), actual,
           psx_error_name(expected), expected);
  }
}

/* --- little-endian builders ---------------------------------------------- */

static size_t push_u32(uint8_t* buffer, size_t at, uint32_t value)
{
  buffer[at] = (uint8_t)(value & 0xffu);
  buffer[at + 1u] = (uint8_t)((value >> 8u) & 0xffu);
  buffer[at + 2u] = (uint8_t)((value >> 16u) & 0xffu);
  buffer[at + 3u] = (uint8_t)((value >> 24u) & 0xffu);
  return at + 4u;
}

static size_t push_u16(uint8_t* buffer, size_t at, uint16_t value)
{
  buffer[at] = (uint8_t)(value & 0xffu);
  buffer[at + 1u] = (uint8_t)((value >> 8u) & 0xffu);
  return at + 2u;
}

static size_t push_i16(uint8_t* buffer, size_t at, int16_t value)
{
  return push_u16(buffer, at, (uint16_t)value);
}

/* ========================================================================== */
static void test_version(void)
{
  printf("[ RUN  ] c.version_and_abi\n");
  CHECK(psx_abi_version() == PSX_ABI_VERSION);
  CHECK(PSX_ABI_IS_COMPATIBLE(psx_abi_version()));
  check_eq_u64(psx_version_major(), PSX_VERSION_MAJOR, "major");
  check_eq_u64(psx_version_minor(), PSX_VERSION_MINOR, "minor");
  check_eq_u64(psx_version_patch(), PSX_VERSION_PATCH, "patch");
  CHECK(strcmp(psx_version_string(), PSX_VERSION_STRING) == 0);
  printf("[  OK  ] c.version_and_abi\n");
}

/* ========================================================================== */
static void test_error_strings(void)
{
  printf("[ RUN  ] c.error_names_and_messages\n");
  /* These strings are a diagnostic contract shared with the C++ side and the logs. */
  CHECK(strcmp(psx_error_name(PSX_OK), "Ok") == 0);
  CHECK(strcmp(psx_error_name(PSX_ERROR_INVALID_ARGUMENT), "InvalidArgument") == 0);
  CHECK(strcmp(psx_error_name(PSX_ERROR_TRUNCATED), "Truncated") == 0);
  CHECK(strcmp(psx_error_name(PSX_ERROR_BPE_MAGIC), "BPEMagic") == 0);
  CHECK(strcmp(psx_error_name(PSX_ERROR_BPE_EXPANSION_OVERFLOW), "BPEExpansionOverflow") == 0);
  CHECK(strcmp(psx_error_name(PSX_ERROR_TIM_RECT_EXTENT), "TIMRectExtent") == 0);
  CHECK(strcmp(psx_error_name(PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE), "MRGEntryOutOfRange") == 0);
  CHECK(strcmp(psx_error_name(PSX_ERROR_DEFF_OFFSETS_BAD), "DEFFOffsetsBad") == 0);
  CHECK(strcmp(psx_error_name(12345), "Unknown") == 0);
  /* Out-of-range on purpose. psx_error_t is an enum whose enumerators are all
     non-negative, so C requires the cast for a negative value — the API still
     accepts it and still names it (psx-c-api.md §4). */
  CHECK(strcmp(psx_error_name((psx_error_t)-7), "Unknown") == 0);

  /* Every documented code must produce a name and a non-empty message. */
  const psx_error_t codes[] = {PSX_OK,
                               PSX_ERROR_INVALID_ARGUMENT,
                               PSX_ERROR_TRUNCATED,
                               PSX_ERROR_BPE_MAGIC,
                               PSX_ERROR_BPE_BLOCK_SIZE,
                               PSX_ERROR_BPE_OUTPUT_TOO_LARGE,
                               PSX_ERROR_BPE_SIZE_MISMATCH,
                               PSX_ERROR_BPE_DICTIONARY_OVERFLOW,
                               PSX_ERROR_BPE_EXPANSION_OVERFLOW,
                               PSX_ERROR_TIM_MAGIC,
                               PSX_ERROR_TIM_BPP,
                               PSX_ERROR_TIM_NO_IMAGES,
                               PSX_ERROR_TIM_SECTION_SIZE,
                               PSX_ERROR_TIM_RECT_EXTENT,
                               PSX_ERROR_TIM_SECTION_OUT_OF_RANGE,
                               PSX_ERROR_MRG_MAGIC,
                               PSX_ERROR_MRG_TABLE_TRUNCATED,
                               PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE,
                               PSX_ERROR_MRG_ENTRY_MISSING,
                               PSX_ERROR_DEFF_MAGIC,
                               PSX_ERROR_DEFF_TABLE_TRUNCATED,
                               PSX_ERROR_DEFF_OFFSETS_BAD,
                               PSX_ERROR_DEFF_ENTRY_MISSING};
  for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
  {
    CHECK(psx_error_name(codes[i]) != NULL);
    CHECK(psx_error_message(codes[i]) != NULL);
    CHECK(psx_error_message(codes[i])[0] != '\0');
  }

  /* Group classification, so a caller can tell "bad asset" from "bad buffer". */
  CHECK(psx_error_is_bpe(PSX_ERROR_BPE_BLOCK_SIZE));
  CHECK(!psx_error_is_bpe(PSX_ERROR_TIM_MAGIC));
  CHECK(psx_error_is_tim(PSX_ERROR_TIM_BPP));
  CHECK(!psx_error_is_tim(PSX_ERROR_MRG_MAGIC));
  CHECK(psx_error_is_mrg(PSX_ERROR_MRG_ENTRY_MISSING));
  CHECK(!psx_error_is_mrg(PSX_ERROR_DEFF_MAGIC));
  CHECK(psx_error_is_deff(PSX_ERROR_DEFF_OFFSETS_BAD));
  CHECK(!psx_error_is_deff(PSX_ERROR_TRUNCATED));
  CHECK(!psx_error_is_bpe(PSX_OK));
  printf("[  OK  ] c.error_names_and_messages\n");
}

/* ========================================================================== */
/// @brief Build a segment whose block dictionary tries to write past key 0xff.
///
/// 127 control bytes of 0x80 advance the key by 2 each (jump + one entry), landing
/// on key 254; a control byte of 0x02 then writes keys 254 and 255 and overruns on
/// its third entry.
/// @param buffer Destination (at least 320 bytes).
/// @return Segment length.
static size_t build_bpe_dictionary_overflow(uint8_t* buffer)
{
  size_t at = push_u32(buffer, 0u, 0x800u); /* declared output size */
  at = push_u32(buffer, at, PSX_BPE_MAGIC); /* magic */
  at = push_u32(buffer, at, 0x800u);        /* block size */

  size_t key = 0u;
  for (int i = 0; i < 127; ++i)
  {
    buffer[at++] = 0x80u; /* jump the key by 1, then write one entry */
    key += 1u;
    buffer[at++] = (uint8_t)key; /* left == key: identity entry, one byte */
    key += 1u;
  }
  buffer[at++] = 0x02u;          /* two more pairs -> three entries */
  buffer[at++] = (uint8_t)key++; /* key 254 */
  buffer[at++] = (uint8_t)key++; /* key 255 */
  return at;                     /* the third entry has nowhere to go */
}

static void test_bpe(void)
{
  printf("[ RUN  ] c.bpe_round_trip_and_rejects\n");

  static const char text[] = "SEVERED CHAINS - BPE FROM PURE C";
  psx_bytes_t raw;
  raw.data = (const uint8_t*)text;
  raw.size = sizeof(text) - 1u;

  size_t needed = 0u;
  check_eq_err(psx_bpe_compressed_size(raw.size, &needed), PSX_OK, "compressed_size");

  uint8_t segment[2560];
  psx_writable_bytes_t out;
  out.data = segment;
  out.capacity = sizeof(segment);
  size_t written = 0u;
  check_eq_err(psx_bpe_compress(raw, out, &written), PSX_OK, "compress");
  check_eq_u64(written, needed, "compress wrote exactly the announced size");

  psx_bytes_t packed;
  packed.data = segment;
  packed.size = written;

  /* The tooling encoder must produce something the format checks agree on. */
  CHECK(psx_bpe_is_bpe(packed));
  size_t declared = 0u;
  check_eq_err(psx_bpe_declared_size(packed, &declared), PSX_OK, "declared_size");
  check_eq_u64(declared, raw.size, "declared size");

  uint8_t back[256];
  out.data = back;
  out.capacity = sizeof(back);
  written = 0u;
  check_eq_err(psx_bpe_decompress(packed, out, &written), PSX_OK, "decompress");
  check_eq_u64(written, raw.size, "decompressed size");
  CHECK(memcmp(back, text, raw.size) == 0);

  /* Multi-block framing: more than PSX_BPE_MAX_BLOCK_SIZE bytes must survive the
     block loop and the 4-byte block padding. */
  static uint8_t big[PSX_BPE_MAX_BLOCK_SIZE + 733u];
  for (size_t i = 0; i < sizeof(big); ++i)
    big[i] = (uint8_t)(i * 7u + 3u);
  size_t bigNeeded = 0u;
  check_eq_err(psx_bpe_compressed_size(sizeof(big), &bigNeeded), PSX_OK, "big compressed_size");
  static uint8_t bigSegment[PSX_BPE_MAX_BLOCK_SIZE * 2 + 4096u];
  out.data = bigSegment;
  out.capacity = sizeof(bigSegment);
  written = 0u;
  check_eq_err(psx_bpe_compress((psx_bytes_t){big, sizeof(big)}, out, &written), PSX_OK, "big compress");
  check_eq_u64(written, bigNeeded, "big compress wrote exactly the announced size");
  static uint8_t bigBack[sizeof(big)];
  out.data = bigBack;
  out.capacity = sizeof(bigBack);
  written = 0u;
  check_eq_err(psx_bpe_decompress((psx_bytes_t){bigSegment, bigNeeded}, out, &written), PSX_OK, "big decompress");
  check_eq_u64(written, sizeof(big), "big round trip length");
  CHECK(memcmp(bigBack, big, sizeof(big)) == 0);

  /* --- malformed input: exact codes, never a success --------------------- */
  check_eq_err(psx_bpe_declared_size((psx_bytes_t){NULL, 0u}, &declared), PSX_ERROR_TRUNCATED, "empty input");
  CHECK(!psx_bpe_is_bpe((psx_bytes_t){NULL, 0u}));
  CHECK(!psx_bpe_is_bpe((psx_bytes_t){(const uint8_t*)"abc", 3u}));

  const uint8_t notBpe[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  check_eq_err(psx_bpe_declared_size((psx_bytes_t){notBpe, sizeof(notBpe)}, &declared), PSX_ERROR_BPE_MAGIC,
               "bad magic");

  /* Capacity below the declared size is the ceiling contract. */
  out.data = back;
  out.capacity = 4u;
  written = 0u;
  check_eq_err(psx_bpe_decompress(packed, out, &written), PSX_ERROR_BPE_OUTPUT_TOO_LARGE, "capacity ceiling");

  /* Block header over the 0x800 limit. */
  uint8_t oversized[16];
  size_t at = push_u32(oversized, 0u, 0x100u);
  at = push_u32(oversized, at, PSX_BPE_MAGIC);
  at = push_u32(oversized, at, PSX_BPE_MAX_BLOCK_SIZE + 1u);
  push_u32(oversized, at, 0u);
  static uint8_t bigBack2[0x800];
  out.data = bigBack2;
  out.capacity = sizeof(bigBack2);
  written = 0u;
  check_eq_err(psx_bpe_decompress((psx_bytes_t){oversized, sizeof(oversized)}, out, &written), PSX_ERROR_BPE_BLOCK_SIZE,
               "oversized block");

  /* Dictionary that runs past key 0xff. */
  static uint8_t dictOverflow[512];
  const size_t dictLength = build_bpe_dictionary_overflow(dictOverflow);
  written = 0u;
  check_eq_err(psx_bpe_decompress((psx_bytes_t){dictOverflow, dictLength}, out, &written),
               PSX_ERROR_BPE_DICTIONARY_OVERFLOW, "dictionary overflow");

  /* Truncated dictionary: a control byte with no entries behind it. */
  uint8_t truncated[64];
  at = push_u32(truncated, 0u, 4u);
  at = push_u32(truncated, at, PSX_BPE_MAGIC);
  at = push_u32(truncated, at, 4u);
  truncated[at++] = 0x10u; /* wants 17 entries, none follow */
  out.data = back;
  out.capacity = sizeof(back);
  written = 0u;
  check_eq_err(psx_bpe_decompress((psx_bytes_t){truncated, at}, out, &written), PSX_ERROR_TRUNCATED,
               "truncated dictionary");

  /* Declared size that the blocks never fill. */
  uint8_t mismatch[32];
  at = push_u32(mismatch, 0u, 99u); /* claims 99 bytes ... */
  at = push_u32(mismatch, at, PSX_BPE_MAGIC);
  at = push_u32(mismatch, at, 0u); /* ... but declares no blocks */
  push_u32(mismatch, at, 0u);
  out.data = back;
  out.capacity = sizeof(back);
  written = 0u;
  check_eq_err(psx_bpe_decompress((psx_bytes_t){mismatch, at + 4u}, out, &written), PSX_ERROR_BPE_SIZE_MISMATCH,
               "declared size mismatch");

  /* Blocks that overrun the declared destination. */
  uint8_t overrun[32];
  at = push_u32(overrun, 0u, 2u);
  at = push_u32(overrun, at, PSX_BPE_MAGIC);
  at = push_u32(overrun, at, 8u); /* block bigger than the declared size */
  push_u32(overrun, at, 0u);
  out.data = back;
  out.capacity = sizeof(back);
  written = 0u;
  check_eq_err(psx_bpe_decompress((psx_bytes_t){overrun, at + 4u}, out, &written), PSX_ERROR_BPE_SIZE_MISMATCH,
               "block overruns declared size");

  /* Zero-length payload is legal and must not require a destination pointer. */
  uint8_t emptySegment[32];
  at = push_u32(emptySegment, 0u, 0u);
  at = push_u32(emptySegment, at, PSX_BPE_MAGIC);
  at = push_u32(emptySegment, at, 0u);
  push_u32(emptySegment, at, 0u);
  written = 12345u;
  out.data = NULL;
  out.capacity = 0u;
  check_eq_err(psx_bpe_decompress((psx_bytes_t){emptySegment, at + 4u}, out, &written), PSX_OK, "empty payload");
  check_eq_u64(written, 0u, "empty payload wrote nothing");

  /* --- argument validation ---------------------------------------------- */
  out.data = back;
  out.capacity = sizeof(back);
  check_eq_err(psx_bpe_decompress(packed, out, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_written");
  check_eq_err(psx_bpe_decompress((psx_bytes_t){NULL, 16u}, out, &written), PSX_ERROR_INVALID_ARGUMENT,
               "null data with size");
  check_eq_err(psx_bpe_declared_size(packed, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_size");
  check_eq_err(psx_bpe_compressed_size(0u, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_size (compress)");
  check_eq_err(psx_bpe_compressed_size(0x100000000ull, &needed), PSX_ERROR_BPE_OUTPUT_TOO_LARGE, "raw beyond u32");
  out.data = NULL;
  out.capacity = 0u;
  written = 0u;
  check_eq_err(psx_bpe_compress(raw, out, &written), PSX_ERROR_BPE_OUTPUT_TOO_LARGE, "compress with no capacity");
  check_eq_err(psx_bpe_compress(raw, out, NULL), PSX_ERROR_INVALID_ARGUMENT, "compress null out_written");
  printf("[  OK  ] c.bpe_round_trip_and_rejects\n");
}

/* ========================================================================== */

/// @brief Build a 4bpp TIM with a 16x16 CLUT and an imageW x imageH image.
/// @param buffer  Destination (at least 2 KiB for the shapes used below).
/// @param flags   Value for the flags word.
/// @param withClut Non-zero to include the CLUT section.
/// @param imageW  Image rect width (texels).
/// @param imageH  Image rect height (texels).
/// @return Built length.
static size_t build_tim(uint8_t* buffer, uint32_t flags, int withClut, int16_t imageW, int16_t imageH)
{
  size_t at = push_u32(buffer, 0u, PSX_TIM_MAGIC);
  at = push_u32(buffer, at, flags);
  if (withClut)
  {
    at = push_u32(buffer, at, PSX_TIM_SECTION_HEADER_BYTES + 256u * 2u);
    at = push_i16(buffer, at, 0);
    at = push_i16(buffer, at, 112);
    at = push_i16(buffer, at, 16);
    at = push_i16(buffer, at, 16);
    for (size_t i = 0; i < 256u * 2u; ++i)
      buffer[at++] = (uint8_t)i;
  }
  const size_t payload = (size_t)imageW * 2u * (size_t)imageH;
  at = push_u32(buffer, at, PSX_TIM_SECTION_HEADER_BYTES + (uint32_t)payload);
  at = push_i16(buffer, at, 0);
  at = push_i16(buffer, at, 0);
  at = push_i16(buffer, at, imageW);
  at = push_i16(buffer, at, imageH);
  for (size_t i = 0; i < payload; ++i)
    buffer[at++] = (uint8_t)(i & 0xffu);
  return at;
}

/// @brief Offset of the image section for the shapes build_tim() produces.
/// @param withClut Non-zero when the TIM carries a CLUT.
/// @return Byte offset of the image section header.
static size_t tim_image_section_offset(int withClut)
{
  return withClut ? 8u + PSX_TIM_SECTION_HEADER_BYTES + 512u : 8u;
}

static void test_tim(void)
{
  printf("[ RUN  ] c.tim_parse_and_rejects\n");

  static uint8_t buffer[8192];
  const size_t length = build_tim(buffer, 0x8u /* CLUT present, 4bpp */, 1, 16, 16);
  psx_bytes_t tim;
  tim.data = buffer;
  tim.size = length;

  CHECK(psx_tim_is_tim(tim));
  psx_tim_image_t image;
  check_eq_err(psx_tim_parse(tim, &image), PSX_OK, "parse valid TIM");
  check_eq_u64(image.flags, 0x8u, "flags preserved");
  check_eq_u64((unsigned long long)image.color_depth, PSX_TIM_BPP_4, "depth");
  CHECK(image.has_clut != 0);
  check_eq_u64((uint16_t)image.clut_rect.y, 112u, "clut rect y");
  check_eq_u64((uint16_t)image.clut_rect.w, 16u, "clut rect w");
  check_eq_u64(psx_tim_clut_entry_count(image.clut_rect), 256u, "CLUT entries");
  check_eq_u64(image.clut.size, 512u, "CLUT payload borrowed length");
  check_eq_u64((uint16_t)image.image_rect.w, 16u, "image rect w");
  check_eq_u64(image.image_data.size, 16u * 2u * 16u, "image payload length");
  check_eq_u64(image.image_section_offset, tim_image_section_offset(1), "image section offset");
  CHECK(image.image_data.data == buffer + image.image_section_offset + PSX_TIM_SECTION_HEADER_BYTES);
  check_eq_u64(image.trailing_bytes, 0u, "no trailing bytes");

  /* Borrowing, not copying: the payload aliases the caller's buffer. */
  buffer[image.image_section_offset + PSX_TIM_SECTION_HEADER_BYTES] = 0xa5u;
  check_eq_u64(image.image_data.data[0], 0xa5u, "image_data aliases the caller buffer");
  buffer[image.image_section_offset + PSX_TIM_SECTION_HEADER_BYTES] = 0x00u;

  /* Trailing padding is reported, not rejected. */
  tim.size = length + 12u;
  check_eq_err(psx_tim_parse(tim, &image), PSX_OK, "parse padded TIM");
  check_eq_u64(image.trailing_bytes, 12u, "trailing bytes reported");
  tim.size = length;

  /* No CLUT: depth 8bpp, image section right after the header. */
  const size_t bare = build_tim(buffer, 0x1u /* 8bpp, no CLUT */, 0, 8, 8);
  tim.size = bare;
  check_eq_err(psx_tim_parse(tim, &image), PSX_OK, "parse CLUT-less TIM");
  CHECK(image.has_clut == 0);
  check_eq_u64((unsigned long long)image.color_depth, PSX_TIM_BPP_8, "8bpp");
  check_eq_u64(image.image_section_offset, tim_image_section_offset(0), "image section at 0x8");

  /* 24bpp declares no verified extent rule, so a small payload is accepted. */
  const size_t deep = build_tim(buffer, 0x8u | 0x3u, 1, 16, 16);
  tim.size = deep;
  check_eq_err(psx_tim_parse(tim, &image), PSX_OK, "24bpp is not extent-checked");
  check_eq_u64((unsigned long long)image.color_depth, PSX_TIM_BPP_24, "24bpp");

  /* --- malformed input --------------------------------------------------- */
  check_eq_err(psx_tim_parse((psx_bytes_t){NULL, 0u}, &image), PSX_ERROR_TRUNCATED, "empty TIM");
  const uint8_t zeros[16] = {0};
  check_eq_err(psx_tim_parse((psx_bytes_t){zeros, sizeof(zeros)}, &image), PSX_ERROR_TIM_MAGIC, "bad magic");
  CHECK(!psx_tim_is_tim((psx_bytes_t){zeros, sizeof(zeros)}));

  size_t n = build_tim(buffer, 0x8u | 0x5u, 1, 16, 16); /* unsupported depth code 5 */
  check_eq_err(psx_tim_parse((psx_bytes_t){buffer, n}, &image), PSX_ERROR_TIM_BPP, "unsupported depth");

  n = build_tim(buffer, 0x8u, 1, 16, 16);
  push_u32(buffer, 8u, PSX_TIM_SECTION_HEADER_BYTES - 1u); /* CLUT section too small */
  check_eq_err(psx_tim_parse((psx_bytes_t){buffer, n}, &image), PSX_ERROR_TIM_SECTION_SIZE, "tiny section");

  n = build_tim(buffer, 0x8u, 1, 16, 16);
  push_i16(buffer, 0xcu + 4u, 0); /* CLUT rect width 0 */
  check_eq_err(psx_tim_parse((psx_bytes_t){buffer, n}, &image), PSX_ERROR_TIM_RECT_EXTENT, "zero-width CLUT rect");

  n = build_tim(buffer, 0x8u, 1, 16, 16);
  push_i16(buffer, tim_image_section_offset(1) + 4u + 4u, 64); /* image rect wider than its payload */
  check_eq_err(psx_tim_parse((psx_bytes_t){buffer, n}, &image), PSX_ERROR_TIM_RECT_EXTENT, "image rect too large");

  n = build_tim(buffer, 0x8u, 1, 16, 16);
  check_eq_err(psx_tim_parse((psx_bytes_t){buffer, n - 16u}, &image), PSX_ERROR_TIM_SECTION_OUT_OF_RANGE,
               "payload past buffer");

  n = build_tim(buffer, 0x8u, 1, 16, 16);
  check_eq_err(psx_tim_parse((psx_bytes_t){buffer, tim_image_section_offset(1)}, &image), PSX_ERROR_TIM_NO_IMAGES,
               "no image section");

  /* A CLUT whose payload cannot cover its rect. */
  n = build_tim(buffer, 0x8u, 1, 16, 16);
  push_i16(buffer, 0xcu + 6u, 128); /* CLUT rect 16x128 -> 2048 entries, 512 bytes present */
  check_eq_err(psx_tim_parse((psx_bytes_t){buffer, n}, &image), PSX_ERROR_TIM_RECT_EXTENT, "CLUT payload too small");

  /* Argument validation, and the init helper's NULL tolerance. */
  check_eq_err(psx_tim_parse(tim, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_image");
  check_eq_err(psx_tim_parse((psx_bytes_t){NULL, 64u}, &image), PSX_ERROR_INVALID_ARGUMENT, "null data with size");
  psx_tim_image_init(NULL);
  psx_tim_image_init(&image);
  check_eq_u64(image.image_section_offset, 0u, "image_init zeroes");
  CHECK(image.image_data.data == NULL);
  check_eq_u64(psx_tim_clut_entry_count((psx_tim_rect_t){0, 0, -4, 4}), 0u, "negative rect has no entries");
  printf("[  OK  ] c.tim_parse_and_rejects\n");
}

/* ========================================================================== */

/// @brief Build an MRG container.
/// @param buffer      Destination.
/// @param entries     Pairs of {offset,size} as stored on disc.
/// @param count       Entry count.
/// @param payloadBytes Bytes of payload following the table.
/// @return Built length.
static size_t build_mrg(uint8_t* buffer, const uint32_t* entries, size_t count, size_t payloadBytes)
{
  size_t at = push_u32(buffer, 0u, PSX_ARCHIVE_MRG_MAGIC);
  at = push_u32(buffer, at, (uint32_t)count);
  for (size_t i = 0; i < count; ++i)
  {
    at = push_u32(buffer, at, entries[i * 2u]);
    at = push_u32(buffer, at, entries[i * 2u + 1u]);
  }
  for (size_t i = 0; i < payloadBytes; ++i)
    buffer[at++] = (uint8_t)(0x40u + (i % 64u));
  return at;
}

static void test_mrg(void)
{
  printf("[ RUN  ] c.mrg_parse_slice_and_handles\n");

  static uint8_t buffer[4096];
  /* Entry 0 owns [0,4), entry 1 owns [4,12), entry 2 is virtual (size 0) pointing
     at entry 1's offset, entry 3 duplicates entry 0's offset. */
  const uint32_t table[] = {0u, 4u, 4u, 8u, 4u, 0u, 0u, 4u};
  const size_t length = build_mrg(buffer, table, 4u, 16u);
  psx_bytes_t container;
  container.data = buffer;
  container.size = length;

  CHECK(psx_archive_is_mrg(container));
  CHECK(!psx_archive_is_deff(container));

  size_t count = 0u;
  check_eq_err(psx_archive_mrg_entry_count(container, 0, &count), PSX_OK, "entry_count");
  check_eq_u64(count, 4u, "entry count");

  psx_mrg_entry_t entries[4];
  psx_mrg_view_t view;
  check_eq_err(psx_archive_mrg_parse(container, 0, entries, 4u, &view), PSX_OK, "parse");
  check_eq_u64(view.count, 4u, "view count");
  check_eq_u64(view.virtual_count, 2u, "two aliases (virtual entry + duplicate offset)");

  psx_mrg_entry_t entry;
  check_eq_err(psx_mrg_view_at(view, 0u, &entry), PSX_OK, "at 0");
  CHECK(!psx_mrg_entry_is_virtual(&entry));
  check_eq_u64(entry.offset, 0u, "entry 0 offset");
  check_eq_u64(entry.size, 4u, "entry 0 size");
  check_eq_u64(entry.virtual_size, 4u, "entry 0 stored size");
  check_eq_err(psx_mrg_view_at(view, 2u, &entry), PSX_OK, "at 2");
  CHECK(psx_mrg_entry_is_virtual(&entry));
  check_eq_u64((unsigned long long)entry.parent, 1u, "virtual entry resolves to entry 1");
  check_eq_u64(entry.size, 8u, "virtual entry inherits its parent's size");
  check_eq_u64(entry.virtual_size, 0u, "virtual entry stored size stays 0");
  check_eq_err(psx_mrg_view_at(view, 9u, &entry), PSX_ERROR_MRG_ENTRY_MISSING, "index out of range");

  psx_bytes_t slice;
  check_eq_err(psx_mrg_view_slice(view, container, 0u, &slice), PSX_OK, "slice 0");
  check_eq_u64(slice.size, 4u, "slice 0 length");
  /* Entry offsets are absolute inside the container (as they are in a real DRGN
     archive, where they land past the table); the parser slices them verbatim. */
  CHECK(slice.data == buffer + 0u);
  check_eq_err(psx_mrg_view_slice(view, container, 9u, &slice), PSX_ERROR_MRG_ENTRY_MISSING, "slice out of range");
  check_eq_err(psx_mrg_view_slice(view, (psx_bytes_t){buffer, 8u}, 1u, &slice), PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE,
               "entry beyond a truncated container");

  /* Sector scaling: the same table read as a DRGN*.BIN root scales offsets. */
  check_eq_err(psx_archive_mrg_parse(container, 1, entries, 4u, &view), PSX_OK, "parse sector-aligned");
  check_eq_err(psx_mrg_view_at(view, 1u, &entry), PSX_OK, "at 1 (aligned)");
  check_eq_u64(entry.offset, 4u * PSX_SECTOR_SIZE, "sector-scaled offset");
  check_eq_err(psx_mrg_view_slice(view, container, 1u, &slice), PSX_ERROR_MRG_ENTRY_OUT_OF_RANGE,
               "sector-scaled entry no longer fits the container");

  /* Empty container (0 entries) must not need storage. Uses its own buffer so the
     container under test stays intact for the handle checks below. */
  static uint8_t spare[64];
  const size_t emptyLength = build_mrg(spare, table, 0u, 0u);
  check_eq_err(psx_archive_mrg_parse((psx_bytes_t){spare, emptyLength}, 0, NULL, 0u, &view), PSX_OK, "parse empty MRG");
  check_eq_u64(view.count, 0u, "empty view count");

  /* --- opaque handle: create, read, slice, destroy ----------------------- */
  psx_mrg_table_t* handle = NULL;
  check_eq_err(psx_mrg_table_parse(container, 0, &handle), PSX_OK, "handle parse");
  CHECK(handle != NULL);
  check_eq_u64(psx_mrg_table_count(handle), 4u, "handle count");
  check_eq_u64(psx_mrg_table_virtual_count(handle), 2u, "handle virtual count");
  psx_mrg_view_t handleView;
  check_eq_err(psx_mrg_table_view(handle, &handleView), PSX_OK, "handle view");
  check_eq_u64(handleView.count, 4u, "handle view count");
  check_eq_err(psx_mrg_view_slice(handleView, container, 1u, &slice), PSX_OK, "handle slice");
  check_eq_u64(slice.size, 8u, "handle slice size");
  psx_mrg_table_destroy(handle);
  /* Destroying NULL is legal; a null handle answers with zeros, not a crash. */
  psx_mrg_table_destroy(NULL);
  check_eq_u64(psx_mrg_table_count(NULL), 0u, "null handle count is 0");
  check_eq_u64(psx_mrg_table_virtual_count(NULL), 0u, "null handle virtual count is 0");
  check_eq_err(psx_mrg_table_view(NULL, &handleView), PSX_OK, "null handle view");
  check_eq_u64(handleView.count, 0u, "null handle yields an empty view");

  /* --- malformed input --------------------------------------------------- */
  const uint8_t notMrg[8] = {'N', 'O', 'P', 'E', 0, 0, 0, 0};
  check_eq_err(psx_archive_mrg_entry_count((psx_bytes_t){notMrg, sizeof(notMrg)}, 0, &count), PSX_ERROR_MRG_MAGIC,
               "bad magic");
  check_eq_err(psx_archive_mrg_parse((psx_bytes_t){notMrg, sizeof(notMrg)}, 0, entries, 4u, &view), PSX_ERROR_MRG_MAGIC,
               "parse bad magic");
  check_eq_err(psx_archive_mrg_entry_count((psx_bytes_t){NULL, 0u}, 0, &count), PSX_ERROR_TRUNCATED, "empty MRG");

  /* Count that cannot possibly fit the buffer. */
  uint8_t huge[16];
  size_t at = push_u32(huge, 0u, PSX_ARCHIVE_MRG_MAGIC);
  at = push_u32(huge, at, 0xdeadbeefu);
  check_eq_err(psx_archive_mrg_entry_count((psx_bytes_t){huge, at}, 0, &count), PSX_ERROR_MRG_TABLE_TRUNCATED,
               "absurd count");

  /* Table that fits the count field but not the buffer. */
  uint8_t clipped[24];
  at = push_u32(clipped, 0u, PSX_ARCHIVE_MRG_MAGIC);
  at = push_u32(clipped, at, 3u);
  at = push_u32(clipped, at, 0u);
  at = push_u32(clipped, at, 4u);
  check_eq_err(psx_archive_mrg_entry_count((psx_bytes_t){clipped, at}, 0, &count), PSX_ERROR_MRG_TABLE_TRUNCATED,
               "table past buffer");

  /* Capacity too small is the caller's mistake, not a format error. */
  const size_t filled = build_mrg(buffer, table, 4u, 16u);
  check_eq_err(psx_archive_mrg_parse((psx_bytes_t){buffer, filled}, 0, entries, 2u, &view), PSX_ERROR_INVALID_ARGUMENT,
               "capacity below count");
  check_eq_err(psx_archive_mrg_parse((psx_bytes_t){buffer, filled}, 0, NULL, 4u, &view), PSX_ERROR_INVALID_ARGUMENT,
               "null entries with count");
  check_eq_err(psx_archive_mrg_entry_count(container, 0, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_count");
  check_eq_err(psx_mrg_view_slice(view, container, 0u, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_data");
  check_eq_err(psx_mrg_view_at(view, 0u, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_entry");
  check_eq_err(psx_mrg_table_parse(container, 0, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_table");
  check_eq_err(psx_mrg_table_view(NULL, NULL), PSX_ERROR_INVALID_ARGUMENT, "null handle + null out_view");
  printf("[  OK  ] c.mrg_parse_slice_and_handles\n");
}

/* ========================================================================== */

/// @brief Build a DEFF container: magic, two unused bytes, u16 count at 0x6, then
/// 8-byte slots carrying each subfile start at +4.
/// @param buffer       Destination.
/// @param starts       Subfile start offsets as stored on disc.
/// @param count        Subfile count.
/// @param payloadBytes Bytes of payload following the table.
/// @return Built length.
static size_t build_deff(uint8_t* buffer, const uint32_t* starts, size_t count, size_t payloadBytes)
{
  size_t at = push_u32(buffer, 0u, PSX_ARCHIVE_DEFF_MAGIC);
  buffer[at++] = 0u;
  buffer[at++] = 0u;
  at = push_u16(buffer, 6u, (uint16_t)count); /* returns 8: the table starts at 0x8 */
  for (size_t i = 0; i < count; ++i)
  {
    at = push_u32(buffer, at, 0u);
    at = push_u32(buffer, at, starts[i]);
  }
  for (size_t i = 0; i < payloadBytes; ++i)
    buffer[at++] = (uint8_t)(0x20u + (i % 32u));
  return at;
}

static void test_deff(void)
{
  printf("[ RUN  ] c.deff_parse_slice_and_handles\n");

  static uint8_t buffer[4096];
  const uint32_t starts[] = {32u, 40u, 60u};
  const size_t length = build_deff(buffer, starts, 3u, 32u);
  psx_bytes_t container;
  container.data = buffer;
  container.size = length;

  CHECK(psx_archive_is_deff(container));
  CHECK(!psx_archive_is_mrg(container));

  size_t count = 0u;
  check_eq_err(psx_archive_deff_subfile_count(container, &count), PSX_OK, "subfile_count");
  check_eq_u64(count, 3u, "subfile count");

  psx_deff_entry_t entries[3];
  psx_deff_view_t view;
  check_eq_err(psx_archive_deff_parse(container, entries, 3u, &view), PSX_OK, "parse");
  check_eq_u64(view.count, 3u, "view count");

  psx_deff_entry_t entry;
  check_eq_err(psx_deff_view_at(view, 0u, &entry), PSX_OK, "at 0");
  check_eq_u64(entry.offset, 32u, "subfile 0 offset");
  check_eq_u64(entry.size, 8u, "subfile 0 size");
  check_eq_err(psx_deff_view_at(view, 2u, &entry), PSX_OK, "at 2");
  check_eq_u64(entry.offset, 60u, "last subfile offset");
  check_eq_u64(entry.size, length - 60u, "last subfile runs to the end");
  check_eq_err(psx_deff_view_at(view, 7u, &entry), PSX_ERROR_DEFF_ENTRY_MISSING, "index out of range");

  psx_bytes_t slice;
  check_eq_err(psx_deff_view_slice(view, container, 1u, &slice), PSX_OK, "slice 1");
  check_eq_u64(slice.size, 20u, "slice 1 size");
  CHECK(slice.data == buffer + 40u);

  /* --- opaque handle ----------------------------------------------------- */
  psx_deff_table_t* handle = NULL;
  check_eq_err(psx_deff_table_parse(container, &handle), PSX_OK, "handle parse");
  check_eq_u64(psx_deff_table_count(handle), 3u, "handle count");
  psx_deff_view_t handleView;
  check_eq_err(psx_deff_table_view(handle, &handleView), PSX_OK, "handle view");
  check_eq_err(psx_deff_view_slice(handleView, container, 0u, &slice), PSX_OK, "handle slice");
  check_eq_u64(slice.size, 8u, "handle slice size");
  psx_deff_table_destroy(handle);
  psx_deff_table_destroy(NULL);
  check_eq_u64(psx_deff_table_count(NULL), 0u, "null handle count");
  check_eq_err(psx_deff_table_view(NULL, &handleView), PSX_OK, "null handle view");
  check_eq_u64(handleView.count, 0u, "null handle yields an empty view");

  /* Zero subfiles: valid, and needs no storage. */
  const size_t emptyLength = build_deff(buffer, starts, 0u, 8u);
  check_eq_err(psx_archive_deff_parse((psx_bytes_t){buffer, emptyLength}, NULL, 0u, &view), PSX_OK, "parse empty DEFF");
  check_eq_u64(view.count, 0u, "empty view");

  /* --- malformed input --------------------------------------------------- */
  const uint8_t notDeff[8] = {'N', 'O', 'P', 'E', 0, 0, 0, 0};
  check_eq_err(psx_archive_deff_subfile_count((psx_bytes_t){notDeff, sizeof(notDeff)}, &count), PSX_ERROR_DEFF_MAGIC,
               "bad magic");
  check_eq_err(psx_archive_deff_subfile_count((psx_bytes_t){NULL, 0u}, &count), PSX_ERROR_TRUNCATED, "empty DEFF");

  uint8_t clipped[16];
  size_t at = push_u32(clipped, 0u, PSX_ARCHIVE_DEFF_MAGIC);
  clipped[at++] = 0u;
  clipped[at++] = 0u;
  at = push_u16(clipped, 6u, 4u);
  check_eq_err(psx_archive_deff_subfile_count((psx_bytes_t){clipped, at}, &count), PSX_ERROR_DEFF_TABLE_TRUNCATED,
               "table past buffer");

  /* Non-monotonic starts, through both entry points. */
  const uint32_t backwards[] = {60u, 40u};
  const size_t badLength = build_deff(buffer, backwards, 2u, 32u);
  check_eq_err(psx_archive_deff_parse((psx_bytes_t){buffer, badLength}, entries, 2u, &view), PSX_ERROR_DEFF_OFFSETS_BAD,
               "backwards subfile start");
  handle = NULL; /* proof that a failed parse leaves the caller's pointer alone */
  check_eq_err(psx_deff_table_parse((psx_bytes_t){buffer, badLength}, &handle), PSX_ERROR_DEFF_OFFSETS_BAD,
               "handle path rejects the same input");
  CHECK(handle == NULL);

  /* Last start beyond the container. */
  const uint32_t beyond[] = {4000u};
  const size_t beyondLength = build_deff(buffer, beyond, 1u, 8u);
  check_eq_err(psx_archive_deff_parse((psx_bytes_t){buffer, beyondLength}, entries, 1u, &view),
               PSX_ERROR_DEFF_OFFSETS_BAD, "start beyond container");

  /* A range that does not fit reports DEFF_ENTRY_MISSING (documented C++ parity). */
  const size_t okLength = build_deff(buffer, starts, 3u, 32u);
  check_eq_err(psx_archive_deff_parse((psx_bytes_t){buffer, okLength}, entries, 3u, &view), PSX_OK, "re-parse");
  check_eq_err(psx_deff_view_slice(view, (psx_bytes_t){buffer, 36u}, 1u, &slice), PSX_ERROR_DEFF_ENTRY_MISSING,
               "subfile beyond truncated container");

  check_eq_err(psx_archive_deff_subfile_count(container, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_count");
  check_eq_err(psx_archive_deff_parse(container, entries, 3u, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_view");
  check_eq_err(psx_archive_deff_parse(container, entries, 1u, &view), PSX_ERROR_INVALID_ARGUMENT,
               "capacity below count");
  check_eq_err(psx_deff_view_at(view, 0u, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_entry");
  check_eq_err(psx_deff_view_slice(view, container, 0u, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_data");
  check_eq_err(psx_deff_table_parse(container, NULL), PSX_ERROR_INVALID_ARGUMENT, "null out_table");
  check_eq_err(psx_deff_table_view(NULL, NULL), PSX_ERROR_INVALID_ARGUMENT, "null handle + null out_view");
  printf("[  OK  ] c.deff_parse_slice_and_handles\n");
}

/* --- XA ADPCM -------------------------------------------------------------- */

/* Deterministic nibble filler (xorshift32). No libc RNG and no fixture file:
   the C consumer has to reproduce byte for byte on every host, which is the
   only way it can assert a *difference* as well as a match. */
static uint32_t g_xa_rng = 0x12345678u;

static uint8_t xa_next_byte(void)
{
  g_xa_rng ^= g_xa_rng << 13u;
  g_xa_rng ^= g_xa_rng >> 17u;
  g_xa_rng ^= g_xa_rng << 5u;
  return (uint8_t)(g_xa_rng & 0xffu);
}

/// @brief Fill a sector whose predictor is actually live.
///
/// Control bytes carry filter index 2 (coefficients 115 / -52). Without feedback
/// the prediction term is identically zero, so a test that compares "carried
/// history" against "fresh history" would pass even if the decoder ignored its
/// own state. Every state-sensitive check below depends on this helper, and each
/// one asserts a *difference*, so a silent loss of feedback fails loudly.
static void xa_build_sector(uint8_t* sector, size_t len)
{
  for (size_t i = 0; i < len; ++i)
    sector[i] = xa_next_byte();
  for (size_t p = 0; p < 18u && 24u + p * 128u + 128u <= len; ++p)
  {
    const size_t base = 24u + p * 128u;
    for (size_t k = 4u; k < 12u; ++k)
      sector[base + k] = (uint8_t)0x24; /* shift field 4, filter index 2 */
  }
}

static void test_xa(void)
{
  printf("[ RUN  ] c.xa_decode_and_rejects\n");

  static uint8_t sector[2352];
  static uint8_t out[16128];
  static uint8_t one_shot[16128];
  static uint8_t again[16128];
  static uint8_t carried[16128];
  size_t written = 0;
  uint8_t info = 0;
  size_t samples = 0;

  /* --- coding_info: reads byte 19, and refuses a sector that has no byte 19. */
  memset(sector, 0, sizeof(sector));
  sector[19] = (uint8_t)0x05;
  check_eq_err(psx_xa_coding_info((psx_bytes_t){sector, sizeof(sector)}, &info), PSX_OK, "coding_info reads byte 19");
  CHECK(info == 0x05u);
  info = 0;
  check_eq_err(psx_xa_coding_info((psx_bytes_t){sector, sizeof(sector)}, NULL), PSX_ERROR_INVALID_ARGUMENT,
               "coding_info refuses a null out");
  check_eq_err(psx_xa_coding_info((psx_bytes_t){sector, 19u}, &info), PSX_ERROR_TRUNCATED,
               "coding_info refuses a sector without byte 19");
  CHECK(info == 0u); /* documented: unmodified on failure */

  /* --- size the destination from the header, not from a worst case. -------- */
  check_eq_err(psx_xa_decoded_samples(0x05u, &samples), PSX_OK, "stereo size query");
  check_eq_u64(samples, 4032ull, "a stereo sector is 4032 interleaved samples");
  check_eq_err(psx_xa_decoded_samples(0x04u, &samples), PSX_OK, "mono size query");
  check_eq_u64(samples, 8064ull, "a mono sector widens to 8064, duplicated L=R");
  samples = 0;
  check_eq_err(psx_xa_decoded_samples(0x15u, &samples), PSX_ERROR_XA_UNSUPPORTED_CODING,
               "8-bit coding is refused, not decoded as 4-bit nibbles");
  CHECK(samples == 0u);
  check_eq_err(psx_xa_decoded_samples(0x05u, NULL), PSX_ERROR_INVALID_ARGUMENT, "size query refuses a null out");

  xa_build_sector(sector, sizeof(sector));

  /* --- one-shot decode: NULL stream means no state, in either direction. --- */
  check_eq_err(
      psx_xa_decode_sector(NULL, (psx_bytes_t){sector, sizeof(sector)}, 0x05u, (psx_writable_bytes_t){out, sizeof(out)},
                           &written),
      PSX_OK, "one-shot decode");
  check_eq_u64(written, 4032ull, "one-shot wrote a full stereo sector");
  memcpy(one_shot, out, written * 2u);

  /* --- rejection paths, each isolated so one cannot mask another. ---------- */
  check_eq_err(
      psx_xa_decode_sector(NULL, (psx_bytes_t){sector, sizeof(sector)}, 0x05u, (psx_writable_bytes_t){out, sizeof(out)},
                           NULL),
      PSX_ERROR_INVALID_ARGUMENT, "decode refuses a null out_samples");
  check_eq_err(
      psx_xa_decode_sector(NULL, (psx_bytes_t){sector, 2327u}, 0x05u, (psx_writable_bytes_t){out, sizeof(out)},
                           &written),
      PSX_ERROR_TRUNCATED, "2327 bytes is one short of the minimum");
  check_eq_err(
      psx_xa_decode_sector(NULL, (psx_bytes_t){sector, 2328u}, 0x05u, (psx_writable_bytes_t){out, sizeof(out)},
                           &written),
      PSX_OK, "2328 bytes is exactly the minimum");
  check_eq_err(
      psx_xa_decode_sector(NULL, (psx_bytes_t){sector, sizeof(sector)}, 0x15u, (psx_writable_bytes_t){out, sizeof(out)},
                           &written),
      PSX_ERROR_XA_UNSUPPORTED_CODING, "decode refuses 8-bit coding");
  check_eq_err(
      psx_xa_decode_sector(NULL, (psx_bytes_t){sector, sizeof(sector)}, 0x05u, (psx_writable_bytes_t){out, 16u},
                           &written),
      PSX_ERROR_XA_OUTPUT_TOO_LARGE, "decode refuses an undersized destination");

  /* --- independence: the reference keeps these taps in process-wide statics.
     A second stream must not see the first one's tail, or two tracks would
     cross-contaminate. This is the property the per-stream design exists for, so
     it is checked from C as well as from C++. ---------------------------------*/
  psx_xa_stream_t primed;
  psx_xa_stream_t clean;
  psx_xa_stream_init(&primed);
  psx_xa_stream_init(&clean);
  CHECK(primed.prev[0] == 0 && primed.prev[1] == 0 && primed.prev2[0] == 0 && primed.prev2[1] == 0);

  check_eq_err(
      psx_xa_decode_sector(&primed, (psx_bytes_t){sector, sizeof(sector)}, 0x05u,
                           (psx_writable_bytes_t){carried, sizeof(carried)}, &written),
      PSX_OK, "first decode through a stream");
  CHECK(memcmp(carried, one_shot, written * 2u) == 0); /* zero history == one-shot */

  check_eq_err(
      psx_xa_decode_sector(&clean, (psx_bytes_t){sector, sizeof(sector)}, 0x05u,
                           (psx_writable_bytes_t){again, sizeof(again)}, &written),
      PSX_OK, "a fresh stream is unaffected by a used one");
  CHECK(memcmp(again, one_shot, written * 2u) == 0);

  /* And the primed stream must now differ, otherwise none of the above proves
     anything: identical results would mean the taps are never read. */
  check_eq_err(
      psx_xa_decode_sector(&primed, (psx_bytes_t){sector, sizeof(sector)}, 0x05u,
                           (psx_writable_bytes_t){carried, sizeof(carried)}, &written),
      PSX_OK, "second decode through the primed stream");
  CHECK(memcmp(carried, one_shot, written * 2u) != 0);
  CHECK(primed.prev[0] != 0 || primed.prev[1] != 0);

  /* --- classification and names. ------------------------------------------- */
  CHECK(psx_error_is_xa(PSX_ERROR_XA_UNSUPPORTED_CODING));
  CHECK(psx_error_is_xa(PSX_ERROR_XA_OUTPUT_TOO_LARGE));
  CHECK(!psx_error_is_xa(PSX_ERROR_TRUNCATED));
  CHECK(!psx_error_is_xa(PSX_OK));
  CHECK(psx_error_name(PSX_ERROR_XA_OUTPUT_TOO_LARGE)[0] != '\0');
  CHECK(psx_error_message(PSX_ERROR_XA_UNSUPPORTED_CODING)[0] != '\0');

  printf("[  OK  ] c.xa_decode_and_rejects\n");
}

/* ========================================================================== */
int main(void)
{
  printf("psx C17 consumer: ABI %u, version %s\n", psx_abi_version(), psx_version_string());

  test_version();
  test_error_strings();
  test_bpe();
  test_tim();
  test_mrg();
  test_deff();
  test_xa();

  printf("%d check(s), %d failed\n", g_checks, g_failed);
  return g_failed;
}
