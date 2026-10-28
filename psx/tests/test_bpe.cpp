/// @file test_bpe.cpp
/// @brief BPE codec tests. Fixtures are hand-derived from the reference format
/// docs/research/psx-formats.md §1 and documented byte by byte,
/// so they pin the *format* rather than the port's own output.
///
/// Shared fixture framing:
///   0x0 u32 decompressed size | 0x4 u32 magic 0x1a455042
///   then per block: u32 block size, dictionary, codes, padding to 4 bytes
///   finally u32 0 (end of stream).
/// An "identity dictionary" is written as two runs of 0x7f (read 0x7f + 1 = 128
/// entries each), each followed by the 128 bytes whose value equals their key, so
/// left[key] == key for all 256 keys and codes resolve to themselves.

#include "psx/cpp/psx.hpp"
#include "psx_test.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace
{

using bytes = std::vector<std::uint8_t>;

void PushU32(bytes& out, std::uint32_t V)
{
  out.push_back(static_cast<std::uint8_t>(V & 0xffu));
  out.push_back(static_cast<std::uint8_t>((V >> 8u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((V >> 16u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((V >> 24u) & 0xffu));
}

void AppendIdentityDictionary(bytes& out)
{
  for (std::size_t run = 0; run < 2; ++run)
  {
    out.push_back(0x7f);
    for (std::uint32_t i = 0; i < 0x80; ++i)
      out.push_back(static_cast<std::uint8_t>(run * 0x80u + i));
  }
}

/// @brief A single-block segment whose codes are literal bytes.
bytes MakeIdentitySegment(std::string_view literal)
{
  bytes out;
  PushU32(out, static_cast<std::uint32_t>(literal.size()));
  PushU32(out, psx::bpe::Magic);
  PushU32(out, static_cast<std::uint32_t>(literal.size()));
  AppendIdentityDictionary(out);
  for (char c : literal)
    out.push_back(static_cast<std::uint8_t>(c));
  while (out.size() % 4 != 0)
    out.push_back(0);
  PushU32(out, 0);
  return out;
}

} // namespace

PSX_TEST(bpe, identity_dictionary_stream_decodes_literally)
{
  const std::string_view text = "Severed Chains"sv;
  const bytes segment = MakeIdentitySegment(text);

  auto sniff = psx::bpe::IsBPE(segment);
  PSX_CHECK(sniff);

  auto declared = psx::bpe::DeclaredSize(segment);
  PSX_CHECK(declared.has_value());
  PSX_CHECK(declared.value_or(0) == text.size());

  auto out = psx::bpe::DecompressBPE(segment);
  PSX_CHECK(out.has_value());
  if (out)
  {
    PSX_CHECK(out->size() == text.size());
    PSX_CHECK(std::equal(out->begin(), out->end(), text.begin(), text.end()));
  }
}

PSX_TEST(bpe, dictionary_pair_expands_to_two_bytes)
{
  // Dictionary: key 0x00 -> left 0x41 ('A'), right 0x42 ('B') because the left
  // byte differs from the key, so a 0x00 code stands for "AB". Keys 0x01..0xff
  // are identity. Encoded as one run of 0x7f (128 entries: key 0 consumes two
  // bytes, keys 1..127 one each) then a second run for keys 128..255.
  bytes dict;
  dict.push_back(0x7f);
  dict.push_back(0x41); // leftch[0]  ('A' != 0 -> a right byte follows)
  dict.push_back(0x42); // rightch[0] ('B')
  for (std::uint32_t key = 1; key <= 0x7f; ++key)
    dict.push_back(static_cast<std::uint8_t>(key));
  dict.push_back(0x7f);
  for (std::uint32_t key = 0x80; key <= 0xff; ++key)
    dict.push_back(static_cast<std::uint8_t>(key));

  bytes segment;
  PushU32(segment, 5); // "ABAB" + "A"
  PushU32(segment, psx::bpe::Magic);
  PushU32(segment, 5);
  segment.insert(segment.end(), dict.begin(), dict.end());
  segment.insert(segment.end(), {0x00, 0x00, 0x41});
  while (segment.size() % 4 != 0)
    segment.push_back(0);
  PushU32(segment, 0);

  auto out = psx::bpe::DecompressBPE(segment);
  PSX_CHECK(out.has_value());
  if (out)
  {
    constexpr std::string_view expected = "ABABA"sv;
    PSX_CHECK(out->size() == expected.size());
    PSX_CHECK(std::equal(out->begin(), out->end(), expected.begin(), expected.end()));
  }
}

PSX_TEST(bpe, round_trips_through_the_tooling_encoder)
{
  const std::vector<std::size_t> sizes = {0, 1, 255, 0x800, 0x801, 5000};
  for (std::size_t size : sizes)
  {
    bytes raw(size);
    for (std::size_t i = 0; i < size; ++i)
      raw[i] = static_cast<std::uint8_t>((i * 31u + (i >> 8) * 7u) & 0xffu);

    auto packed = psx::bpe::CompressBPE(raw);
    PSX_CHECK(packed.has_value());
    if (!packed)
      continue;
    PSX_CHECK(psx::bpe::IsBPE(*packed));

    auto unpacked = psx::bpe::DecompressBPE(*packed);
    PSX_CHECK(unpacked.has_value());
    PSX_CHECK(unpacked.value_or(bytes{}) == raw);
  }
}

PSX_TEST(bpe, rejects_non_bpe_and_short_input)
{
  const bytes nope = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  PSX_CHECK(!psx::bpe::IsBPE(nope));
  PSX_CHECK(!psx::bpe::IsBPE({}));

  auto declared = psx::bpe::DeclaredSize(nope);
  PSX_CHECK(!declared && declared.error() == psx::Error::BPEMagic);

  const bytes shortBuf = {0x10, 0x00, 0x00};
  PSX_CHECK(!psx::bpe::IsBPE(shortBuf));
  auto shortDeclared = psx::bpe::DeclaredSize(shortBuf);
  PSX_CHECK(!shortDeclared && shortDeclared.error() == psx::Error::Truncated);

  auto decodeShort = psx::bpe::DecompressBPE(shortBuf);
  PSX_CHECK(!decodeShort && decodeShort.error() == psx::Error::Truncated);
}

PSX_TEST(bpe, rejects_oversized_block)
{
  bytes segment;
  PushU32(segment, 0x900);
  PushU32(segment, psx::bpe::Magic);
  PushU32(segment, 0x900); // over the 0x800 per-block ceiling
  auto out = psx::bpe::DecompressBPE(segment);
  PSX_CHECK(!out && out.error() == psx::Error::BPEBlockSize);
}

PSX_TEST(bpe, rejects_truncated_dictionary_and_data)
{
  bytes segment;
  PushU32(segment, 16);
  PushU32(segment, psx::bpe::Magic);
  PushU32(segment, 16);
  segment.push_back(0x7f); // dictionary claims 128 entries, none present
  auto dictCut = psx::bpe::DecompressBPE(segment);
  PSX_CHECK(!dictCut && dictCut.error() == psx::Error::Truncated);

  bytes withDict = segment;
  AppendIdentityDictionary(withDict); // dictionary complete, codes missing
  auto dataCut = psx::bpe::DecompressBPE(withDict);
  PSX_CHECK(!dataCut && dataCut.error() == psx::Error::Truncated);
}

PSX_TEST(bpe, rejects_declared_size_mismatch)
{
  // Blocks produce 14 bytes; the header promises 20. The reference zero-pads
  // and returns anyway — this port reports it instead (documented deviation).
  bytes segment = MakeIdentitySegment("short payload!"sv);
  segment[0] = 20;
  segment[1] = 0;
  segment[2] = 0;
  segment[3] = 0;
  auto out = psx::bpe::DecompressBPE(segment);
  PSX_CHECK(!out && out.error() == psx::Error::BPESizeMismatch);

  // And the other direction: blocks produce more than declared.
  bytes over = MakeIdentitySegment("much longer payload than declared"sv);
  PushU32(over, 0); // (harmless trailing bytes; header below is what matters)
  over[0] = 4;
  auto overOut = psx::bpe::DecompressBPE(over);
  PSX_CHECK(!overOut && overOut.error() == psx::Error::BPESizeMismatch);
}

PSX_TEST(bpe, rejects_self_referential_dictionary_instead_of_looping)
{
  // key 0x00 -> left 0x01, right 0x00; key 0x01 -> left 0x00, right 0x01.
  // Expanding 0x00 re-expands forever, so the guard must fire.
  bytes dict;
  dict.push_back(0x7f);
  dict.push_back(0x01); // leftch[0]  (!= 0 -> right follows)
  dict.push_back(0x00); // rightch[0]
  dict.push_back(0x00); // leftch[1]  (!= 1 -> right follows)
  dict.push_back(0x01); // rightch[1]
  for (std::uint32_t key = 2; key <= 0x7f; ++key)
    dict.push_back(static_cast<std::uint8_t>(key));
  dict.push_back(0x7f);
  for (std::uint32_t key = 0x80; key <= 0xff; ++key)
    dict.push_back(static_cast<std::uint8_t>(key));

  bytes segment;
  PushU32(segment, 1);
  PushU32(segment, psx::bpe::Magic);
  PushU32(segment, 1);
  segment.insert(segment.end(), dict.begin(), dict.end());
  segment.push_back(0x00);
  while (segment.size() % 4 != 0)
    segment.push_back(0);
  PushU32(segment, 0);

  auto out = psx::bpe::DecompressBPE(segment);
  PSX_CHECK(!out && out.error() == psx::Error::BPEExpansionOverflow);
}

PSX_TEST(bpe, honours_the_output_ceiling)
{
  const bytes segment = MakeIdentitySegment("some bytes to decompress"sv);
  auto limited = psx::bpe::DecompressBPE(segment, 4);
  PSX_CHECK(!limited && limited.error() == psx::Error::BPEOutputTooLarge);

  auto allowed = psx::bpe::DecompressBPE(segment, 1024);
  PSX_CHECK(allowed.has_value());
}

// The runner's main() lives in test_smoke.cpp (one runner per psx suite).
