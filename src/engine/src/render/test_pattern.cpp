/// @file test_pattern.cpp
/// @brief Scene construction and the CPU reference rasterizer for the M2 slice
/// (testing.md §2.13). Pure computation: no SDL, no GPU, no filesystem.

#include "lod/render/test_pattern.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <format>

namespace lod::render
{

constexpr std::uint32_t PackRgba(std::uint8_t R, std::uint8_t G, std::uint8_t B, std::uint8_t A)
{
  return static_cast<std::uint32_t>(R) | (static_cast<std::uint32_t>(G) << 8) | (static_cast<std::uint32_t>(B) << 16) |
         (static_cast<std::uint32_t>(A) << 24);
}

constexpr std::uint8_t BlockGrey(int Column, int Row)
{
  // Two independent strides so a horizontal and a vertical neighbour never
  // share a level: a single-direction gradient would make a whole-row shift
  // invisible to the comparison.
  return static_cast<std::uint8_t>(0x18 + ((Column * 5 + Row * 3) * 11) % 0xE0);
}

namespace
{

/// @brief Memory-order RGBA for the overlay triangle (pure red: no block level
/// and no clear colour is pure red, so a wrong-primitive read cannot be missed).
constexpr std::uint32_t OverlayRgba = PackRgba(0xF0, 0x00, 0x00, 0xFF);

/// @brief Two-dimensional cross product used for coverage.
[[nodiscard]] float Cross(float Ax, float Ay, float Bx, float By)
{
  return Ax * By - Ay * Bx;
}

/// @brief Appends one quad as two triangles, all four vertices carrying the
/// same colour (a flat block fill, which is what a tile or a sprite quad is).
void AppendQuad(TestPattern& scene, float x0, float y0, float x1, float y1, std::uint32_t rgba)
{
  const ColourVertex tl{x0, y0, rgba};
  const ColourVertex tr{x1, y0, rgba};
  const ColourVertex br{x1, y1, rgba};
  const ColourVertex bl{x0, y1, rgba};
  scene.Triangles.push_back({{tl, tr, br}});
  scene.Triangles.push_back({{tl, br, bl}});
}

/// @brief Coverage test for one pixel centre of one triangle.
/// @return The smallest absolute edge-function value, or -1 when the sample is
///         outside the triangle (the value's magnitude then has no meaning for
///         the ambiguity test).
[[nodiscard]] float Coverage(const ColourTriangle& tri, float px, float py)
{
  const auto& a = tri.Vertices[0];
  const auto& b = tri.Vertices[1];
  const auto& c = tri.Vertices[2];

  const float abx = b.X - a.X;
  const float aby = b.Y - a.Y;
  const float bcx = c.X - b.X;
  const float bcy = c.Y - b.Y;
  const float cax = a.X - c.X;
  const float cay = a.Y - c.Y;

  // Twice the signed area: its sign says which winding the triangle has, and a
  // near-zero area means the primitive is degenerate (no pixels either way).
  const float area = Cross(abx, aby, c.X - a.X, c.Y - a.Y);
  if (std::fabs(area) < 1e-6f)
    return -1.0f;

  const float e0 = Cross(abx, aby, px - a.X, py - a.Y);
  const float e1 = Cross(bcx, bcy, px - b.X, py - b.Y);
  const float e2 = Cross(cax, cay, px - c.X, py - c.Y);

  const bool inside = area > 0.0f ? (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) : (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f);
  if (!inside)
    return -1.0f;
  return std::min({std::fabs(e0), std::fabs(e1), std::fabs(e2)});
}

} // namespace

TestPattern BuildTestPattern(int Width, int Height)
{
  TestPattern scene;
  scene.Width = Width;
  scene.Height = Height;
  scene.ClearRgba = PackRgba(0x00, 0x00, 0xFF, 0xFF);
  if (Width < 32 || Height < 32)
    return scene; // too small to hold either region: an empty scene, not a partial one

  // The block field takes the left two thirds, rounded to whole 16px blocks so
  // a clipped column at the field edge cannot produce a half-size block.
  scene.FieldWidth = std::min(Width, (Width * 2) / 3);
  scene.FieldWidth -= scene.FieldWidth % TestPatternBlockSize;
  scene.FieldColumns = scene.FieldWidth / TestPatternBlockSize;
  scene.FieldRows = Height / TestPatternBlockSize;

  for (int row = 0; row < scene.FieldRows; ++row)
  {
    for (int col = 0; col < scene.FieldColumns; ++col)
    {
      const float x0 = col * TestPatternBlockSize + TestPatternBlockInset;
      const float y0 = row * TestPatternBlockSize + TestPatternBlockInset;
      const float x1 = col * TestPatternBlockSize + TestPatternBlockSize - TestPatternBlockInset;
      const float y1 = row * TestPatternBlockSize + TestPatternBlockSize - TestPatternBlockInset;
      const std::uint8_t grey = BlockGrey(col, row);
      AppendQuad(scene, x0, y0, x1, y1, PackRgba(grey, grey, grey, 0xFF));
    }
  }

  // The overlay triangle lives in the band right of the field. Every component
  // is a multiple of 0.25 (see the header) and the band is at least 32px wide,
  // so it never overlaps a block: no primitive overlap, therefore no dependence
  // on undefined write order inside a draw call.
  const int bandWidth = Width - scene.FieldWidth;
  if (bandWidth >= 32)
  {
    const float left = scene.FieldWidth + 8.25f;
    const float right = Width - 8.75f;
    const float apexY = (Height * 3) / 8 + 0.25f;
    if (right > left + 8.0f)
    {
      const std::uint32_t rgba = OverlayRgba;
      scene.Triangles.push_back(
        {{ColourVertex{left, 8.5f, rgba}, ColourVertex{right, apexY, rgba}, ColourVertex{left, Height - 8.25f, rgba}}});
    }
  }
  return scene;
}

std::vector<std::uint8_t> RasterizeReference(const TestPattern& scene)
{
  if (scene.Width <= 0 || scene.Height <= 0)
    return {};

  std::vector<std::uint8_t> image(static_cast<std::size_t>(scene.Width) * static_cast<std::size_t>(scene.Height) * 4);
  const std::uint32_t clear = scene.ClearRgba;
  for (std::size_t i = 0; i < image.size(); i += 4)
  {
    image[i] = static_cast<std::uint8_t>(clear & 0xFFu);
    image[i + 1] = static_cast<std::uint8_t>((clear >> 8) & 0xFFu);
    image[i + 2] = static_cast<std::uint8_t>((clear >> 16) & 0xFFu);
    image[i + 3] = static_cast<std::uint8_t>((clear >> 24) & 0xFFu);
  }

  auto put = [&](int x, int y, std::uint32_t rgba)
  {
    const std::size_t offset =
      (static_cast<std::size_t>(y) * static_cast<std::size_t>(scene.Width) + static_cast<std::size_t>(x)) * 4;
    image[offset] = static_cast<std::uint8_t>(rgba & 0xFFu);
    image[offset + 1] = static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
    image[offset + 2] = static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
    image[offset + 3] = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
  };

  for (const ColourTriangle& tri : scene.Triangles)
  {
    int minX = scene.Width;
    int minY = scene.Height;
    int maxX = 0;
    int maxY = 0;
    for (const ColourVertex& v : tri.Vertices)
    {
      minX = std::min(minX, static_cast<int>(std::floor(v.X)));
      minY = std::min(minY, static_cast<int>(std::floor(v.Y)));
      maxX = std::max(maxX, static_cast<int>(std::ceil(v.X)));
      maxY = std::max(maxY, static_cast<int>(std::ceil(v.Y)));
    }
    minX = std::max(minX, 0);
    minY = std::max(minY, 0);
    maxX = std::min(maxX, scene.Width - 1);
    maxY = std::min(maxY, scene.Height - 1);

    for (int y = minY; y <= maxY; ++y)
    {
      for (int x = minX; x <= maxX; ++x)
        if (Coverage(tri, x + 0.5f, y + 0.5f) >= 0.0f)
          put(x, y, tri.Colour());
    }
  }
  return image;
}

int CountAmbiguousSamples(const TestPattern& scene)
{
  if (scene.Width <= 0 || scene.Height <= 0)
    return 0;

  int ambiguous = 0;
  for (const ColourTriangle& tri : scene.Triangles)
  {
    for (int y = 0; y < scene.Height; ++y)
    {
      for (int x = 0; x < scene.Width; ++x)
      {
        const float d = Coverage(tri, x + 0.5f, y + 0.5f);
        if (d >= 0.0f && d < TestPatternAmbiguityEpsilon)
          ++ambiguous;
      }
    }
  }
  return ambiguous;
}

CompareReport CompareImages(std::span<const std::uint8_t> GpuImage, std::span<const std::uint8_t> Reference, int Width,
                            int Height)
{
  CompareReport report;
  const std::size_t expected = static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4;
  if (Width <= 0 || Height <= 0)
  {
    report.Detail = "non-positive canvas size";
    return report;
  }
  if (GpuImage.size() != expected || Reference.size() != expected)
  {
    report.Mismatched = Width * Height;
    report.Detail = "size mismatch: gpu=" + std::to_string(GpuImage.size()) +
                    " cpu=" + std::to_string(Reference.size()) + " expected=" + std::to_string(expected);
    return report;
  }

  report.Compared = Width * Height;
  for (int y = 0; y < Height; ++y)
  {
    for (int x = 0; x < Width; ++x)
    {
      const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(Width) + static_cast<std::size_t>(x)) * 4;
      int worst = 0;
      for (int channel = 0; channel < 4; ++channel)
        worst = std::max(worst, std::abs(static_cast<int>(GpuImage[offset + channel]) -
                                         static_cast<int>(Reference[offset + channel])));
      if (worst == 0)
        continue;
      ++report.Mismatched;
      report.MaxChannelDelta = std::max(report.MaxChannelDelta, worst);
      if (report.Detail.size() < 400)
        report.Detail += std::format("{},{} gpu={},{},{},{} cpu={},{},{},{}  ", x, y,
                                     static_cast<int>(GpuImage[offset]), static_cast<int>(GpuImage[offset + 1]),
                                     static_cast<int>(GpuImage[offset + 2]), static_cast<int>(GpuImage[offset + 3]),
                                     static_cast<int>(Reference[offset]), static_cast<int>(Reference[offset + 1]),
                                     static_cast<int>(Reference[offset + 2]), static_cast<int>(Reference[offset + 3]));
    }
  }
  return report;
}

PatternGeometry BuildPatternGeometry(const TestPattern& scene)
{
  PatternGeometry geometry;
  geometry.VertexCount = static_cast<int>(scene.Triangles.size()) * 3;
  geometry.Positions.reserve(static_cast<std::size_t>(geometry.VertexCount) * 2);
  geometry.Colours.reserve(static_cast<std::size_t>(geometry.VertexCount) * 4);
  for (const ColourTriangle& tri : scene.Triangles)
  {
    for (const ColourVertex& v : tri.Vertices)
    {
      geometry.Positions.push_back(v.X);
      geometry.Positions.push_back(v.Y);
      geometry.Colours.push_back(static_cast<std::uint8_t>(v.Rgba & 0xFFu));
      geometry.Colours.push_back(static_cast<std::uint8_t>((v.Rgba >> 8) & 0xFFu));
      geometry.Colours.push_back(static_cast<std::uint8_t>((v.Rgba >> 16) & 0xFFu));
      geometry.Colours.push_back(static_cast<std::uint8_t>((v.Rgba >> 24) & 0xFFu));
    }
  }
  return geometry;
}

int ReadbackRowStride(int Width)
{
  return (Width * 4 + 255) / 256 * 256;
}

} // namespace lod::render
