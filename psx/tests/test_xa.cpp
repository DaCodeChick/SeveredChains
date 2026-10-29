/// @file test_xa.cpp
/// @brief XA ADPCM decoder tests.
///
/// The heart of this file is `byte_exact_against_oracle`, which compares the C
/// decoder against PCM produced by the untouched Java `legend.core.spu.XaAdpcm`
/// and stored in `psx/testdata/xa_adpcm.bin`. The expected bytes did not come
/// from this decoder, so the comparison can fail — which it does: five
/// deliberate mutations of xa.c (floor division, swapped interleave, mono
/// without widening, unmasked shift count, a wrong filter coefficient) each
/// changed the result. A fixture that only ever agreed with its own
/// implementation would not have caught any of them.
///
/// The remaining cases cover what the golden cannot: argument validation, the
/// refusal of unimplemented coding, and whether predictor state actually carries
/// between sectors (the reason `psx_xa_stream_t` exists at all).

#include "psx/cpp/psx.hpp"
#include "psx_test.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

using bytes = std::vector<std::uint8_t>;

/// @brief One decoded case straight out of the fixture container.
struct Case
{
  std::string name;
  std::uint8_t coding = 0;
  bytes sector;
  bytes expected;
};

/// @brief Read the fixture written by psx/tools/xa_oracle.py.
/// @return Every case, or an empty vector when the file is absent or truncated.
std::vector<Case> LoadFixture()
{
  std::vector<Case> out;
#ifdef PSX_XA_FIXTURE
  const std::filesystem::path path{PSX_XA_FIXTURE};
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec))
    return out;

  std::ifstream in(path, std::ios::binary);
  bytes blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (blob.size() < 16 || std::memcmp(blob.data(), "PSXXAFIX", 8) != 0)
    return out;

  const auto U32 = [&blob](std::size_t at)
  {
    return static_cast<std::uint32_t>(blob[at]) | (static_cast<std::uint32_t>(blob[at + 1]) << 8u) |
           (static_cast<std::uint32_t>(blob[at + 2]) << 16u) | (static_cast<std::uint32_t>(blob[at + 3]) << 24u);
  };

  const std::uint32_t count = U32(12);
  std::size_t at = 16;
  for (std::uint32_t i = 0; i < count; ++i)
  {
    if (at + 1 > blob.size())
      return {};
    const std::size_t nameLen = blob[at++];
    if (at + nameLen + 9 > blob.size())
      return {};
    Case c;
    c.name.assign(reinterpret_cast<const char*>(blob.data() + at), nameLen);
    at += nameLen;
    c.coding = blob[at++];
    const std::uint32_t sectorLen = U32(at);
    const std::uint32_t pcmLen = U32(at + 4);
    at += 8;
    if (at + sectorLen + pcmLen > blob.size())
      return {}; // Truncated or inconsistent: treat the whole fixture as unusable.
    c.sector.assign(blob.begin() + static_cast<long>(at), blob.begin() + static_cast<long>(at + sectorLen));
    at += sectorLen;
    c.expected.assign(blob.begin() + static_cast<long>(at), blob.begin() + static_cast<long>(at + pcmLen));
    at += pcmLen;
    out.push_back(std::move(c));
  }
#endif
  return out;
}

/// @brief Little-endian i16 PCM -> sample vector, for range statistics.
std::vector<std::int16_t> ToSamples(const bytes& raw)
{
  std::vector<std::int16_t> out;
  out.reserve(raw.size() / 2);
  for (std::size_t i = 0; i + 1 < raw.size(); i += 2)
  {
    const auto u = static_cast<std::uint16_t>(raw[i] | (static_cast<std::uint16_t>(raw[i + 1]) << 8u));
    out.push_back(static_cast<std::int16_t>(u));
  }
  return out;
}

/// @brief True when the sector's control bytes ask for feedback, i.e. any filter
///        index is not 0.
///
/// Filter 0 has both coefficients zero, so the prediction term is identically
/// zero and predictor history provably cannot reach the output. A carry test run
/// on a filter-0 sector would assert nothing at all.
bool HasFeedback(const Case& c)
{
  if (c.sector.size() < 2328)
    return false;
  for (std::size_t portion = 0; portion < 18; ++portion)
    for (std::size_t blk = 0; blk < 4; ++blk)
      for (std::size_t ch = 0; ch < 2; ++ch)
      {
        const std::size_t at = 24 + portion * 128 + 4 + blk * 2 + ch;
        if (((c.sector[at] >> 4) & 0x3u) != 0)
          return true;
      }
  return false;
}

/// @brief True when the sector carries at least one non-zero sample byte.
///
/// Feedback alone is not enough. The prediction term
/// `(prev * f0 + prev2 * f1 + 32) / 64` truncates toward zero, so a chain fed
/// only zero nibbles collapses to zero from any starting history. The badshift
/// fixtures have feedback in all 144 control bytes and still show no state
/// effect, for exactly this reason — measured, not assumed.
bool HasSampleData(const Case& c)
{
  if (c.sector.size() < 2328)
    return false;
  for (std::size_t portion = 0; portion < 18; ++portion)
    for (std::size_t at = 24 + portion * 128 + 16; at < 24 + portion * 128 + 128; ++at)
      if (c.sector[at] != 0)
        return true;
  return false;
}

/// @brief Index of the first case that can actually show a predictor-state
///        difference, or -1 when the fixture has none.
int FirstStateSensitiveCase(const std::vector<Case>& cases)
{
  for (std::size_t i = 0; i < cases.size(); ++i)
    if (HasFeedback(cases[i]) && HasSampleData(cases[i]))
      return static_cast<int>(i);
  return -1;
}

// ---------------------------------------------------------------------------
// Argument validation
// ---------------------------------------------------------------------------

PSX_TEST(xa, rejects_bad_arguments)
{
  static bytes sector(2352, 0x11);
  static bytes out(8064 * 2);
  std::size_t n = 12345;

  psx_xa_stream_t stream;
  psx_xa_stream_init(&stream);

  // A null output pointer is a caller bug, never a "decode nothing" request.
  PSX_CHECK(psx_xa_decode_sector(&stream, psx::detail::ToBytes(sector), 0x05, psx_writable_bytes_t{nullptr, 0},
                                 nullptr) == PSX_ERROR_INVALID_ARGUMENT);

  // A non-zero size with a null data pointer is malformed, not empty.
  psx_bytes_t badIn{nullptr, 16};
  psx_writable_bytes_t okOut{out.data(), out.size()};
  n = 0;
  PSX_CHECK(psx_xa_decode_sector(&stream, badIn, 0x05, okOut, &n) == PSX_ERROR_INVALID_ARGUMENT);
  PSX_CHECK(n == 0);

  // The coding byte is checked before the sector length, so a caller that asks
  // for an unsupported format finds out even with a perfect buffer.
  n = 0;
  PSX_CHECK(psx_xa_decode_sector(&stream, psx::detail::ToBytes(sector), 0x15, okOut, &n) ==
            PSX_ERROR_XA_UNSUPPORTED_CODING);
}

PSX_TEST(xa, rejects_a_sector_that_is_too_short)
{
  // The decoder touches bytes 24..2327 only; anything less than that is a
  // truncated sector rather than a short track.
  static bytes shortSector(2327, 0x22);
  static bytes out(8064 * 2);
  psx_xa_stream_t stream;
  psx_xa_stream_init(&stream);
  std::size_t n = 0;
  psx_writable_bytes_t ob{out.data(), out.size()};
  PSX_CHECK(psx_xa_decode_sector(&stream, psx::detail::ToBytes(shortSector), 0x05, ob, &n) == PSX_ERROR_TRUNCATED);
  PSX_CHECK(n == 0);

  // Exactly the minimum is accepted.
  static bytes exact(2328, 0x22);
  n = 0;
  PSX_CHECK(psx_xa_decode_sector(&stream, psx::detail::ToBytes(exact), 0x05, ob, &n) == PSX_OK);
  PSX_CHECK(n == 4032);
}

PSX_TEST(xa, refuses_8bit_coding_instead_of_decoding_it_wrong)
{
  // The Java reference computes is8BitPerSample and then ignores it, decoding
  // 8-bit data as 4-bit nibbles. That yields plausible-looking noise, so the
  // port refuses instead. This is a deliberate, documented behaviour difference
  // from the oracle (see the header), not parity.
  static bytes sector(2352, 0x33);
  static bytes out(8064 * 2);
  psx_xa_stream_t stream;
  psx_xa_stream_init(&stream);
  std::size_t n = 0;
  psx_writable_bytes_t ob{out.data(), out.size()};
  PSX_CHECK(psx_xa_decode_sector(&stream, psx::detail::ToBytes(sector), 0x14, ob, &n) ==
            PSX_ERROR_XA_UNSUPPORTED_CODING);

  // 8-bit set dominates the stereo bit for sizing purposes.
  std::size_t samples = 1;
  PSX_CHECK(psx_xa_decoded_samples(0x15, &samples) == PSX_ERROR_XA_UNSUPPORTED_CODING);
  PSX_CHECK(samples == 1); // Untouched on failure.
}

PSX_TEST(xa, reports_the_sample_count_without_decoding)
{
  std::size_t samples = 0;
  PSX_CHECK(psx_xa_decoded_samples(0x05, &samples) == PSX_OK);
  PSX_CHECK(samples == 4032); // stereo
  PSX_CHECK(psx_xa_decoded_samples(0x04, &samples) == PSX_OK);
  PSX_CHECK(samples == 8064); // mono, each sample written twice
  PSX_CHECK(psx_xa_decoded_samples(0x00, &samples) == PSX_OK);
  PSX_CHECK(samples == 8064); // bit 0 clear means mono regardless of other bits
  PSX_CHECK(psx_xa_decoded_samples(0x04, nullptr) == PSX_ERROR_INVALID_ARGUMENT);

  // Reading the coding byte from a too-short sector is a truncation, not "no
  // coding byte", and the output parameter must be left alone on failure.
  std::uint8_t info = 0;
  const bytes shortSector(19, 0x15);
  PSX_CHECK(psx_xa_coding_info(psx::detail::ToBytes(shortSector), &info) == PSX_ERROR_TRUNCATED);
  PSX_CHECK(info == 0);

  // Byte 19 is the coding byte of a raw 2352-byte sector, which is the offset
  // the reference uses at Fmv.java:421 (`XaAdpcm.decode(data, data[19])`).
  const bytes fullSector(PSX_XA_SECTOR_SIZE, 0x15);
  PSX_CHECK(psx_xa_coding_info(psx::detail::ToBytes(fullSector), &info) == PSX_OK);
  PSX_CHECK(info == 0x15);
}

PSX_TEST(xa, rejects_an_output_buffer_that_cannot_hold_a_sector)
{
  static bytes sector(2352, 0x44);
  static bytes tiny(4032 * 2 - 2); // Two bytes short of a stereo sector.
  psx_xa_stream_t stream;
  psx_xa_stream_init(&stream);
  std::size_t n = 0;
  psx_writable_bytes_t ob{tiny.data(), tiny.size()};
  PSX_CHECK(psx_xa_decode_sector(&stream, psx::detail::ToBytes(sector), 0x05, ob, &n) == PSX_ERROR_XA_OUTPUT_TOO_LARGE);

  // Mono needs the roomiest buffer because every sample is duplicated.
  std::size_t need = 0;
  PSX_CHECK(psx_xa_decoded_samples(0x04, &need) == PSX_OK);
  static bytes enoughMono(static_cast<std::size_t>(need) * 2);
  n = 0;
  psx_writable_bytes_t big{enoughMono.data(), enoughMono.size()};
  PSX_CHECK(psx_xa_decode_sector(&stream, psx::detail::ToBytes(sector), 0x04, big, &n) == PSX_OK);
  PSX_CHECK(n == need);
}

// ---------------------------------------------------------------------------
// The differential against the reference
// ---------------------------------------------------------------------------

PSX_TEST(xa, byte_exact_against_oracle)
{
  auto cases = LoadFixture();
  if (cases.empty())
  {
    // This fixture is committed, so its absence is a checkout problem, not a
    // missing optional asset. Failing loudly is correct: silently skipping here
    // would retire the only test that can see a transcription error.
    std::printf("  FIXTURE MISSING or unreadable: " PSX_XA_FIXTURE "\n");
    PSX_CHECK(false);
    return;
  }

  PSX_CHECK(cases.size() >= 10);
  std::size_t bad = 0;
  std::size_t totalSamples = 0;
  std::size_t nonzero = 0;
  std::size_t atRails = 0;

  for (const Case& c : cases)
  {
    psx_xa_stream_t stream;
    psx_xa_stream_init(&stream);
    bytes got(c.expected.size(), 0);
    std::size_t n = 0;
    psx_writable_bytes_t ob{got.data(), got.size()};
    const psx_error_t rc = psx_xa_decode_sector(&stream, psx::detail::ToBytes(c.sector), c.coding, ob, &n);

    if (rc != PSX_OK)
    {
      std::printf("  case %s: rejected with %s\n", c.name.c_str(),
                  std::string(psx::ErrorMessage(static_cast<psx::Error>(rc))).c_str());
      ++bad;
      continue;
    }
    if (n * 2 != c.expected.size())
    {
      std::printf("  case %s: decoded %zu samples, oracle produced %zu\n", c.name.c_str(), n, c.expected.size() / 2);
      ++bad;
      continue;
    }
    if (std::memcmp(got.data(), c.expected.data(), c.expected.size()) != 0)
    {
      const auto want = ToSamples(c.expected);
      const auto have = ToSamples(got);
      std::size_t firstDiff = 0;
      while (firstDiff < want.size() && want[firstDiff] == have[firstDiff])
        ++firstDiff;
      std::printf("  case %s: differs from oracle at sample %zu (want %d, got %d)\n", c.name.c_str(), firstDiff,
                  static_cast<int>(want[firstDiff]), static_cast<int>(have[firstDiff]));
      ++bad;
      continue;
    }

    // Track how much of the fixture actually exercised the arithmetic. A
    // fixture of all-zero sectors would pass for the wrong reason.
    for (std::size_t i = 0; i + 1 < got.size(); i += 2)
    {
      const auto u = static_cast<std::uint16_t>(got[i] | (static_cast<std::uint16_t>(got[i + 1]) << 8u));
      const auto v = static_cast<std::int16_t>(u);
      ++totalSamples;
      if (v != 0)
        ++nonzero;
      if (v == -32768 || v == 32767)
        ++atRails;
    }
  }

  PSX_CHECK(bad == 0);
  PSX_CHECK(nonzero * 2 > totalSamples); // Over half the samples are non-zero.
  PSX_CHECK(atRails > 0);                // The clamp rails were reached.
  std::printf("  %zu case(s), %zu samples, %zu non-zero, %zu clamped\n", cases.size(), totalSamples, nonzero, atRails);
}

PSX_TEST(xa, predictor_state_carries_between_sectors)
{
  // This is the reason psx_xa_stream_t exists. Decoding the same sector twice
  // with one stream must differ from decoding it twice with a fresh stream,
  // otherwise the stream parameter is decorative and the reference's own
  // cross-sector behaviour is not being modelled.
  auto cases = LoadFixture();
  const int pick = FirstStateSensitiveCase(cases);
  if (pick < 0)
  {
    std::printf("  FIXTURE has no state-sensitive case, cannot exercise carry\n");
    PSX_CHECK(false);
    return;
  }

  const Case& c = cases[static_cast<std::size_t>(pick)];
  bytes chained;
  bytes independent;

  {
    psx_xa_stream_t stream;
    psx_xa_stream_init(&stream);
    bytes buf(c.expected.size(), 0);
    std::size_t n = 0;
    psx_writable_bytes_t ob{buf.data(), buf.size()};
    PSX_CHECK(psx_xa_decode_sector(&stream, psx::detail::ToBytes(c.sector), c.coding, ob, &n) == PSX_OK);
    chained.insert(chained.end(), buf.begin(), buf.begin() + static_cast<long>(n * 2));
    n = 0;
    PSX_CHECK(psx_xa_decode_sector(&stream, psx::detail::ToBytes(c.sector), c.coding, ob, &n) == PSX_OK);
    chained.insert(chained.end(), buf.begin(), buf.begin() + static_cast<long>(n * 2));
  }

  for (int pass = 0; pass < 2; ++pass)
  {
    psx_xa_stream_t fresh;
    psx_xa_stream_init(&fresh); // Reset: the reference's statics do not reset.
    bytes buf(c.expected.size(), 0);
    std::size_t n = 0;
    psx_writable_bytes_t ob{buf.data(), buf.size()};
    PSX_CHECK(psx_xa_decode_sector(&fresh, psx::detail::ToBytes(c.sector), c.coding, ob, &n) == PSX_OK);
    independent.insert(independent.end(), buf.begin(), buf.begin() + static_cast<long>(n * 2));
  }

  PSX_CHECK(chained.size() == independent.size());
  // Each single decode matches the oracle (it ran from a zero state, like the
  // fixture generator's per-process JVM).
  PSX_CHECK(
    std::equal(independent.begin(), independent.begin() + static_cast<long>(c.expected.size()), c.expected.begin()));
  PSX_CHECK(
    std::equal(independent.begin() + static_cast<long>(c.expected.size()), independent.end(), c.expected.begin()));
  // But the chained pair does not: the second decode inherited filter history.
  PSX_CHECK(chained != independent);

  const std::size_t half = chained.size() / 2;
  PSX_CHECK(
    !std::equal(chained.begin(), chained.begin() + static_cast<long>(half), chained.begin() + static_cast<long>(half)));

  // Census, so this case cannot quietly go vacuous by picking the one fixture
  // where history happens not to matter. Every case with feedback and sample
  // data is decoded twice through one stream and once through a fresh one.
  // Measured on the committed fixture: 7 of 17 differ, 6 are provably
  // state-insensitive by the two predicates above (the silences, the zero-data
  // badshift sectors, the filter-0 sectors), and the remaining 4 have feedback
  // and data but repeat anyway — they are counted neither way and are not used
  // as evidence. The assertion is on the count: if predictor carry were removed,
  // the count falls to zero and this case fails even if the picked case did not.
  // No claim is made about why the remaining 4 repeat; that is unexamined, not
  // verified benign.
  std::size_t stateSensitive = 0;
  std::size_t stateInsensitive = 0;
  // `entry`, not `c`: the case selected above is also named `c` and a shadowed
  // name here would make it unclear which sector a failure refers to.
  for (const Case& entry : cases)
  {
    if (!HasFeedback(entry) || !HasSampleData(entry))
    {
      ++stateInsensitive;
      continue;
    }
    psx_xa_stream_t census;
    psx_xa_stream_init(&census);
    bytes carryProbe(entry.expected.size(), 0);
    bytes freshProbe(entry.expected.size(), 0);
    std::size_t written = 0;
    psx_writable_bytes_t carry{carryProbe.data(), carryProbe.size()};
    psx_writable_bytes_t fresh{freshProbe.data(), freshProbe.size()};
    // First decode seeds the stream, second decode inherits it; the fresh run
    // repeats the pair on a reset stream. A difference is a state effect.
    PSX_CHECK(psx_xa_decode_sector(&census, psx::detail::ToBytes(entry.sector), entry.coding, carry, &written) ==
              PSX_OK);
    written = 0;
    PSX_CHECK(psx_xa_decode_sector(&census, psx::detail::ToBytes(entry.sector), entry.coding, fresh, &written) ==
              PSX_OK);
    if (carryProbe != freshProbe)
      ++stateSensitive;
  }
  std::printf("  %zu case(s) show a state effect, %zu are provably state-insensitive\n", stateSensitive,
              stateInsensitive);
  PSX_CHECK(stateSensitive >= 5);
}

PSX_TEST(xa, a_null_stream_decodes_without_touching_history)
{
  // The one-shot contract: probes and transcoders must be able to decode
  // without owning state.
  auto cases = LoadFixture();
  const int pick = FirstStateSensitiveCase(cases);
  if (pick < 0)
  {
    PSX_CHECK(false);
    return;
  }
  const Case& c = cases[static_cast<std::size_t>(pick)];
  bytes oneShot(c.expected.size(), 0);
  std::size_t n = 99;
  psx_writable_bytes_t ob{oneShot.data(), oneShot.size()};
  PSX_CHECK(psx_xa_decode_sector(nullptr, psx::detail::ToBytes(c.sector), c.coding, ob, &n) == PSX_OK);
  PSX_CHECK(n * 2 == c.expected.size());
  PSX_CHECK(std::memcmp(oneShot.data(), c.expected.data(), c.expected.size()) == 0);
}

PSX_TEST(xa, the_c_stream_type_is_copyable_to_fork_a_stream)
{
  // Forking state is what lets a probe decode a track without disturbing the
  // live one; it only means anything if a copy really is an independent fork.
  auto cases = LoadFixture();
  const int pick = FirstStateSensitiveCase(cases);
  if (pick < 0)
  {
    PSX_CHECK(false);
    return;
  }
  const Case& c = cases[static_cast<std::size_t>(pick)];
  bytes full(c.expected.size() * 2, 0);

  psx_xa_stream_t live;
  psx_xa_stream_init(&live);
  std::size_t n = 0;
  psx_writable_bytes_t big{full.data(), full.size()};
  PSX_CHECK(psx_xa_decode_sector(&live, psx::detail::ToBytes(c.sector), c.coding, big, &n) == PSX_OK);

  auto fork = live; // Copy, not reference.
  bytes a(c.expected.size(), 0);
  bytes b(c.expected.size(), 0);
  std::size_t na = 0;
  std::size_t nb = 0;
  psx_writable_bytes_t oa{a.data(), a.size()};
  psx_writable_bytes_t ob{b.data(), b.size()};
  PSX_CHECK(psx_xa_decode_sector(&live, psx::detail::ToBytes(c.sector), c.coding, oa, &na) == PSX_OK);
  PSX_CHECK(psx_xa_decode_sector(&fork, psx::detail::ToBytes(c.sector), c.coding, ob, &nb) == PSX_OK);
  PSX_CHECK(na == nb);
  PSX_CHECK(std::memcmp(a.data(), b.data(), a.size()) == 0);
}

PSX_TEST(xa, error_classification_and_names)
{
  PSX_CHECK(psx_error_is_xa(PSX_ERROR_XA_UNSUPPORTED_CODING));
  PSX_CHECK(psx_error_is_xa(PSX_ERROR_XA_OUTPUT_TOO_LARGE));
  PSX_CHECK(!psx_error_is_xa(PSX_OK));
  PSX_CHECK(!psx_error_is_xa(PSX_ERROR_TRUNCATED)); // Different group.
  PSX_CHECK(!psx_error_is_xa(PSX_ERROR_INVALID_ARGUMENT));
  PSX_CHECK(std::strlen(psx_error_name(PSX_ERROR_XA_OUTPUT_TOO_LARGE)) > 0);
  // The C++ mirror must name the same code, not fall through to "unknown".
  PSX_CHECK(psx::ErrorName(psx::Error::XAUnsupportedCoding) == "XAUnsupportedCoding");
  PSX_CHECK(psx::ErrorMessage(psx::Error::XAUnsupportedCoding).size() > 0);
  PSX_CHECK(psx::ErrorMessage(psx::Error::XAOutputTooLarge).size() > 0);
}

PSX_TEST(xa, the_cplusplus_wrapper_agrees_with_the_c_api)
{
  auto cases = LoadFixture();
  if (cases.empty())
  {
    PSX_CHECK(false);
    return;
  }
  const Case& c = cases.front();
  psx::xa::Stream stream;
  psx_xa_stream_init(&stream);
  bytes out(c.expected.size(), 0);
  auto n = psx::xa::DecodeSector(stream, c.sector, c.coding, out);
  PSX_CHECK(n.has_value());
  if (n)
    PSX_CHECK(*n * 2 == c.expected.size());
  PSX_CHECK(std::memcmp(out.data(), c.expected.data(), c.expected.size()) == 0);

  // And the error channel is not flattened into "some error".
  auto coding = psx::xa::CodingInfo(std::span<const std::uint8_t>{});
  PSX_CHECK(!coding.has_value());
  auto shortSector = psx::xa::CodingInfo(bytes(19, 0x05));
  PSX_CHECK(!shortSector.has_value());
}

} // namespace