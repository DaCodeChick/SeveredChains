/// @file test_tim.cpp
/// @brief TIM parser tests. The main fixture reproduces the structure measured
/// in the authentic `patches/replacements/chester.tim` from the reference
/// repository (see tim.h), so the synthetic bytes and the real asset are checked
/// against the same expectations.

#include "psx/cpp/psx.hpp"
#include "psx_test.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

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

void PushRect(bytes& out, std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h)
{
  for (std::int16_t v : {x, y, w, h})
  {
    const auto raw = static_cast<std::uint16_t>(v);
    out.push_back(static_cast<std::uint8_t>(raw & 0xffu));
    out.push_back(static_cast<std::uint8_t>((raw >> 8u) & 0xffu));
  }
}

/// @brief 4 bpp TIM with a 256-entry CLUT, laid out exactly like the authentic
/// asset: CLUT section 0x20c at 0x8, image section 0x100c at 0x214.
/// @param trailing Extra bytes after the declared image section.
bytes MakeChesterLikeTIM(std::size_t trailing = 0)
{
  bytes out;
  PushU32(out, 0x10); // id
  PushU32(out, 0x8);  // flags: depth 0 (4 bpp), CLUT present

  PushU32(out, 0x20c);                // CLUT section size (header 12 + 512 payload)
  PushRect(out, 0, 112, 16, 16);      // CLUT rect: 16 x 16 = 256 entries
  out.insert(out.end(), 0x200, 0x42); // 512 CLUT bytes (16-bit entries)

  PushU32(out, 0x100c);                // image section size (header 12 + 0x1000 payload)
  PushRect(out, 0, 0, 16, 112);        // 16 texels wide (4 bpp) x 112 rows = 3584 used
  out.insert(out.end(), 0x1000, 0x11); // 4096 image bytes
  out.insert(out.end(), trailing, 0x00);
  return out;
}

} // namespace

PSX_TEST(tim, parses_a_clut_image_with_measured_offsets)
{
  const bytes data = MakeChesterLikeTIM();

  PSX_CHECK(psx::tim::IsTIM(data));

  auto parsed = psx::tim::ParseTIM(data);
  PSX_CHECK(parsed.has_value());
  if (!parsed)
    return;

  PSX_CHECK(parsed->flags == 0x8u);
  PSX_CHECK(parsed->colorDepth == psx::tim::BPP::Four);
  PSX_CHECK(parsed->hasCLUT);
  PSX_CHECK(parsed->clutRect.x == 0 && parsed->clutRect.y == 112);
  PSX_CHECK(parsed->clutRect.w == 16 && parsed->clutRect.h == 16);
  PSX_CHECK(parsed->clut.size() == 0x200);
  PSX_CHECK(psx::tim::CLUTEntryCount(parsed->clutRect) == 256);
  PSX_CHECK(parsed->imageSectionOffset == 0x214);
  PSX_CHECK(parsed->imageRect.w == 16 && parsed->imageRect.h == 112);
  PSX_CHECK(parsed->imageData.size() == 0x1000);
  PSX_CHECK(parsed->trailingBytes == 0);
  // Borrowed spans must point into the caller's buffer, not a copy.
  PSX_CHECK(parsed->imageData.data() == data.data() + 0x220);
}

PSX_TEST(tim, reports_trailing_bytes_without_rejecting_them)
{
  // The authentic asset carries 12 bytes past its declared image section; the
  // reference reader ignores them, so this port must not treat them as damage.
  const bytes data = MakeChesterLikeTIM(12);
  auto parsed = psx::tim::ParseTIM(data);
  PSX_CHECK(parsed.has_value());
  PSX_CHECK(parsed && parsed->trailingBytes == 12);
  PSX_CHECK(parsed && parsed->imageData.size() == 0x1000);
}

PSX_TEST(tim, parses_image_only_tim_without_clut)
{
  bytes out;
  PushU32(out, 0x10);
  PushU32(out, 0x1); // depth 1 = 8 bpp, no CLUT flag
  PushU32(out, 12 + 64);
  PushRect(out, 0, 0, 16, 2); // 16 * 2 * 2 = 64 bytes needed, 64 declared
  out.insert(out.end(), 64, 0x7f);

  auto parsed = psx::tim::ParseTIM(out);
  PSX_CHECK(parsed.has_value());
  PSX_CHECK(parsed && !parsed->hasCLUT);
  PSX_CHECK(parsed && parsed->colorDepth == psx::tim::BPP::Eight);
  PSX_CHECK(parsed && parsed->clut.empty());
  PSX_CHECK(parsed && parsed->imageSectionOffset == 0x8);
  PSX_CHECK(parsed && parsed->imageData.size() == 64);
}

PSX_TEST(tim, rejects_bad_magic_and_short_buffers)
{
  bytes nope;
  PushU32(nope, 0x20); // wrong id
  PushU32(nope, 0x8);
  PSX_CHECK(!psx::tim::IsTIM(nope));
  auto badMagic = psx::tim::ParseTIM(nope);
  PSX_CHECK(!badMagic && badMagic.error() == psx::Error::TIMMagic);

  auto tooShort = psx::tim::ParseTIM({});
  PSX_CHECK(!tooShort && tooShort.error() == psx::Error::Truncated);

  bytes headerOnly = {0x10, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
  PSX_CHECK(psx::tim::IsTIM(headerOnly));
  auto noSection = psx::tim::ParseTIM(headerOnly);
  PSX_CHECK(!noSection && noSection.error() == psx::Error::TIMNoImages);
}

PSX_TEST(tim, rejects_unknown_colour_depth)
{
  bytes out;
  PushU32(out, 0x10);
  PushU32(out, 0x5); // depth code 5 is not one of the four BPP values
  PushU32(out, 12 + 64);
  PushRect(out, 0, 0, 16, 2);
  out.insert(out.end(), 64, 0);

  auto parsed = psx::tim::ParseTIM(out);
  PSX_CHECK(!parsed && parsed.error() == psx::Error::TIMBPP);
}

PSX_TEST(tim, rejects_section_sizes_smaller_than_their_header)
{
  bytes out;
  PushU32(out, 0x10);
  PushU32(out, 0x8); // CLUT present
  PushU32(out, 8);   // smaller than the 12-byte section header
  PushRect(out, 0, 0, 16, 16);
  auto parsed = psx::tim::ParseTIM(out);
  PSX_CHECK(!parsed && parsed.error() == psx::Error::TIMSectionSize);
}

PSX_TEST(tim, rejects_clut_rect_larger_than_its_section)
{
  bytes out;
  PushU32(out, 0x10);
  PushU32(out, 0x8);
  PushU32(out, 12 + 32);       // 32 bytes of palette...
  PushRect(out, 0, 0, 16, 16); // ...but the rect promises 256 x 2-byte entries
  out.insert(out.end(), 32, 0);

  auto parsed = psx::tim::ParseTIM(out);
  PSX_CHECK(!parsed && parsed.error() == psx::Error::TIMRectExtent);
}

PSX_TEST(tim, rejects_image_rect_larger_than_its_section)
{
  bytes out;
  PushU32(out, 0x10);
  PushU32(out, 0x0); // 4 bpp, no CLUT
  PushU32(out, 12 + 16);
  PushRect(out, 0, 0, 64, 64); // needs 64 * 2 * 64 = 8192 bytes
  out.insert(out.end(), 16, 0);

  auto parsed = psx::tim::ParseTIM(out);
  PSX_CHECK(!parsed && parsed.error() == psx::Error::TIMRectExtent);
}

PSX_TEST(tim, rejects_non_positive_rects)
{
  bytes out;
  PushU32(out, 0x10);
  PushU32(out, 0x0);
  PushU32(out, 12 + 64);
  PushRect(out, 0, 0, -16, 2);
  out.insert(out.end(), 64, 0);

  auto parsed = psx::tim::ParseTIM(out);
  PSX_CHECK(!parsed && parsed.error() == psx::Error::TIMRectExtent);
}

PSX_TEST(tim, rejects_sections_that_run_past_the_buffer)
{
  bytes out;
  PushU32(out, 0x10);
  PushU32(out, 0x0);
  PushU32(out, 12 + 0x10000); // claims 64 Ki of payload
  PushRect(out, 0, 0, 16, 2);
  out.insert(out.end(), 8, 0); // ...and delivers 8 bytes

  auto parsed = psx::tim::ParseTIM(out);
  PSX_CHECK(!parsed && parsed.error() == psx::Error::TIMSectionOutOfRange);

  // CLUT section that pushes the image section beyond the end.
  bytes withCLUT;
  PushU32(withCLUT, 0x10);
  PushU32(withCLUT, 0x8);
  PushU32(withCLUT, 0x20c);
  PushRect(withCLUT, 0, 0, 16, 16);
  withCLUT.insert(withCLUT.end(), 0x200, 0);
  auto noImage = psx::tim::ParseTIM(withCLUT);
  PSX_CHECK(!noImage && noImage.error() == psx::Error::TIMNoImages);
}

/// Authentic-asset check. Opt-in by design: ordinary automated runs must not
/// depend on a machine-specific asset location (Milestone 3 brief, §8), so the
/// case reports SKIP unless SC_PSX_ASSET_DIR points at a directory containing
/// chester.tim. Set it to the reference repository's patches/replacements
/// directory to exercise the real file.
PSX_TEST(tim, authentic_asset_matches_measured_layout)
{
  const char* dir = std::getenv("SC_PSX_ASSET_DIR");
  if (dir == nullptr || std::string_view(dir).empty())
  {
    std::printf("  SKIP SC_PSX_ASSET_DIR is not set (optional authentic-asset case)\n");
    return;
  }

  const std::filesystem::path file = std::filesystem::path(dir) / "chester.tim";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec))
  {
    std::printf("  SKIP %s not found\n", file.string().c_str());
    return;
  }

  std::ifstream in(file, std::ios::binary);
  bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  PSX_CHECK(!data.empty());

  auto parsed = psx::tim::ParseTIM(data);
  PSX_CHECK(parsed.has_value());
  if (!parsed)
  {
    std::printf("  authentic TIM rejected: %s\n", std::string(psx::ErrorName(parsed.error())).c_str());
    return;
  }
  PSX_CHECK(parsed->colorDepth == psx::tim::BPP::Four);
  PSX_CHECK(parsed->hasCLUT);
  PSX_CHECK(parsed->clutRect.y == 112 && parsed->clutRect.w == 16 && parsed->clutRect.h == 16);
  PSX_CHECK(parsed->clut.size() == 0x200);
  PSX_CHECK(parsed->imageSectionOffset == 0x214);
  PSX_CHECK(parsed->imageRect.w == 16 && parsed->imageRect.h == 112);
  PSX_CHECK(parsed->imageData.size() == 0xff4);
  PSX_CHECK(parsed->trailingBytes == 12);
}

// The runner's main() lives in test_smoke.cpp (one runner per psx suite).
