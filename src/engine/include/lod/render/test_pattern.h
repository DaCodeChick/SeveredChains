#pragma once
/// @file test_pattern.h
/// @brief The M2 test scene: what should be on screen, plus the CPU reference
/// that says what the GPU *must* produce for it (testing.md §2.13).
///
/// This header has no SDL and no GPU dependency on purpose. The scene is
/// geometry plus colours; the reference image is produced by a small
/// orientation-agnostic rasterizer over exactly the subset the slice uses
/// (flat-colour triangles, no blending, no depth, no texturing). The GPU path
/// then has to agree with it, which is what makes the readback comparison an
/// oracle rather than a tautology: a draw that never happened, a vertex buffer
/// that was never uploaded, or a wrong vertex layout all change the image.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lod::render
{

/// @brief Edge length of one test-pattern block, in pixels.
///
/// 16 is the size the project's sprite/tile presentation works in
/// (rendering-sdl3-gpu.md §3: PSX VRAM pages are 8x8 tiles, sprites are
/// composed of them), so the slice exercises the real granularity instead of an
/// arbitrary one.
inline constexpr int TestPatternBlockSize = 16;

/// @brief Pixels left empty on each side of a block's quad.
///
/// Half-pixel-class insets used to be a rasterizer trap; 0.75 is used instead
/// of 0.5 so that no pixel *centre* lands exactly on a primitive edge. With
/// every coordinate a multiple of 0.25 and the canvas under 1024 px wide, the
/// edge functions stay exact in float32, so the reference and the hardware
/// disagree nowhere by construction rather than by tolerance.
inline constexpr float TestPatternBlockInset = 0.75f;

/// @brief Distance from a pixel centre to a primitive edge below which the
/// sample is considered boundary-ambiguous (not oracle-decidable).
inline constexpr float TestPatternAmbiguityEpsilon = 0.0001f;

/// @brief One vertex of the test scene: position in pixel coordinates (top-left
/// of the target is 0,0; y grows downwards) plus a flat RGBA colour.
///
/// The colour is one packed uint32 in *memory* order R,G,B,A, which is what a
/// R8G8B8A8_UNORM buffer holds byte-wise. It is deliberately not read back as
/// a native uint32 anywhere: images travel as byte spans.
struct ColourVertex
{
  float X;            ///< Pixel-space x.
  float Y;            ///< Pixel-space y.
  std::uint32_t Rgba; ///< Memory-order RGBA (R in the low byte).
};

/// @brief One flat-colour triangle: all three vertices carry the same colour,
/// so the reference does not depend on the interpolator's precision.
struct ColourTriangle
{
  std::array<ColourVertex, 3> Vertices; ///< Counter-clockwise or clockwise; both fill.

  /// @brief The shared colour (index 0).
  /// @return Packed RGBA.
  [[nodiscard]] std::uint32_t Colour() const { return Vertices[0].Rgba; }
};

/// @brief A complete test scene for one canvas size.
struct TestPattern
{
  int Width = 0;                         ///< Canvas width (px).
  int Height = 0;                        ///< Canvas height (px).
  std::uint32_t ClearRgba = 0xFF0000FFu; ///< Blue: the colour under everything.
  int FieldColumns = 0;                  ///< 16px block columns in the block field.
  int FieldRows = 0;                     ///< 16px block rows in the block field.
  int FieldWidth = 0;                    ///< Block field width (px); triangle starts right of it.
  std::vector<ColourTriangle> Triangles; ///< Blocks (2 per block) then the overlay triangle.

  /// @brief Number of block quads in the field (block fills, not triangles).
  /// @return FieldColumns * FieldRows.
  [[nodiscard]] int BlockCount() const { return FieldColumns * FieldRows; }
};

/// @brief Builds the scene for a canvas: a 16x16 block field covering the left
/// two thirds, then one differently coloured triangle in the remaining band.
///
/// Both go through one triangle-list draw, i.e. the same path a sprite, a tile
/// layer and a piece of geometry would use. The triangle sits in its own band
/// with no overlap, so draw order inside the buffer cannot change the image —
/// overlapping flat primitives without depth or blending have no defined
/// write order, and the slice does not depend on one.
///
/// @param Width Canvas width in pixels (>= 32 to get both regions).
/// @param Height Canvas height in pixels (>= 32 to get both regions).
/// @return The scene; Triangles is empty if the canvas is too small to hold any.
/// @note Thread: any (pure function, no shared state).
[[nodiscard]] TestPattern BuildTestPattern(int Width, int Height);

/// @brief The reference image for a scene: RGBA8, Width*Height*4 bytes,
/// produced by the CPU rasterizer, not by the GPU.
///
/// @param pattern The scene to rasterize (TestPattern::Width/Height define the image).
/// @return RGBA8 bytes, or empty for a zero-sized scene.
/// @note Thread: any (pure function).
[[nodiscard]] std::vector<std::uint8_t> RasterizeReference(const TestPattern& pattern);

/// @brief Counts pixels whose centre is closer than
/// TestPatternAmbiguityEpsilon to a primitive edge.
///
/// Those are the samples where a hardware fill rule and the reference may
/// legitimately differ. The build is designed to have none, and the test says
/// so; if this ever becomes non-zero, the oracle has stopped being decidable
/// for that scene and the comparison stops being meaningful.
///
/// @param pattern The scene to probe.
/// @return Count of ambiguous samples (0 means every pixel is decidable).
/// @note Thread: any (pure function).
[[nodiscard]] int CountAmbiguousSamples(const TestPattern& pattern);

/// @brief GPU-side encoding of a test scene: structure-of-arrays vertex data.
///
/// Positions and colours are separate arrays rather than one interleaved
/// vertex. That is not a style choice: the two-array form maps to two vertex
/// buffer slots with pitches 8 and 4, whose element strides match the MSL types
/// exactly. An interleaved 12-byte vertex would need MSL to agree on a 12-byte
/// struct size, and MSL rounds a struct containing a float2 up to a 16-byte
/// stride — a silent vertex-position error on every draw.
struct PatternGeometry
{
  std::vector<float> Positions;      ///< 2 floats per vertex (x,y), pixel space.
  std::vector<std::uint8_t> Colours; ///< 4 bytes per vertex, memory-order RGBA.
  int VertexCount = 0;               ///< Vertices (Positions.size()/2 == Colours.size()/4).

  /// @brief Byte size of the position array.
  [[nodiscard]] std::size_t PositionBytes() const { return Positions.size() * sizeof(float); }
  /// @brief Byte size of the colour array.
  [[nodiscard]] std::size_t ColourBytes() const { return Colours.size(); }
  /// @brief Byte size of the whole vertex buffer payload.
  [[nodiscard]] std::size_t TotalBytes() const { return PositionBytes() + ColourBytes(); }
};

/// @brief Encodes a scene as one triangle-list vertex payload.
/// @param scene The scene to encode.
/// @return Geometry with exactly 3 vertices per triangle in the scene.
/// @note Thread: any (pure function).
[[nodiscard]] PatternGeometry BuildPatternGeometry(const TestPattern& scene);

/// @brief Row stride used for readback, in bytes.
///
/// Rounded up to 256 because that is the row alignment D3D12 requires of a
/// linear texture copy; paying it on Metal (where 320 px RGBA8 already lands on
/// 1280 bytes) keeps one stride rule instead of a per-backend special case.
/// @param Width Canvas width in pixels.
/// @return Width*4 rounded up to a multiple of 256.
[[nodiscard]] int ReadbackRowStride(int Width);

/// @brief Result of comparing one GPU readback against the CPU reference.
struct CompareReport
{
  int Compared = 0;        ///< Pixels compared.
  int Mismatched = 0;      ///< Pixels whose four bytes differ.
  int MaxChannelDelta = 0; ///< Largest per-channel difference seen.
  std::string Detail;      ///< First mismatches, as "x,y gpu=.. cpu=.."; empty when clean.

  /// @brief Whether the images agree byte-for-byte.
  /// @return true when no pixel differs.
  [[nodiscard]] bool Clean() const { return Mismatched == 0; }
};

/// @brief Compares a GPU readback against the reference for the same scene.
///
/// The comparison is exact, not tolerance-based: BuildTestPattern keeps every
/// coordinate on a 0.25 grid inside a canvas under 1024 px, which keeps the
/// edge functions exact in float32 and leaves no boundary-ambiguous sample
/// (CountAmbiguousSamples reports 0). A tolerance would hide a wrong draw
/// behind a fuzz window, which is the opposite of what this oracle is for.
///
/// @param GpuImage RGBA8 bytes read back from the GPU.
/// @param Reference RGBA8 bytes from RasterizeReference.
/// @param Width Canvas width in pixels.
/// @param Height Canvas height in pixels.
/// @return The report; Clean() is false when either image has the wrong size,
///         which is reported rather than silently truncated.
/// @note Thread: any (pure function).
[[nodiscard]] CompareReport CompareImages(std::span<const std::uint8_t> GpuImage,
                                          std::span<const std::uint8_t> Reference, int Width, int Height);

/// @brief Packs channel bytes into the memory-order uint32 used by ColourVertex.
/// @param R Red byte. @param G Green byte. @param B Blue byte. @param A Alpha byte.
/// @return R | G<<8 | B<<16 | A<<24.
/// @note Thread: any (constexpr).
[[nodiscard]] constexpr std::uint32_t PackRgba(std::uint8_t R, std::uint8_t G, std::uint8_t B, std::uint8_t A);

/// @brief The block field's greyscale level for one block, shared by the scene
/// builder and the reference rasterizer.
/// @param Column Block column (0-based).
/// @param Row Block row (0-based).
/// @return Grey byte value in [0x18, 0xF8); never equal to the clear colour or
///         to the overlay triangle's colour.
/// @note Thread: any (constexpr).
[[nodiscard]] constexpr std::uint8_t BlockGrey(int Column, int Row);

} // namespace lod::render
