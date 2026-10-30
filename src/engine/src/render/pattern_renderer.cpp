/// @file pattern_renderer.cpp
/// @brief The M2 vertical slice implementation (rendering-sdl3-gpu.md §2).
///
/// Everything GPU-facing lives here: the hand-written MSL, the pipeline state,
/// the frame sequence, and the readback. Each failure site gets its own Status
/// code (error-handling.md §2, 300s) so a CI log says which step of the frame
/// path stopped rather than just "rendering failed".

#include "lod/render/pattern_renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <string>

namespace lod::render
{
namespace
{

/// @brief Vertex stage: pixel-space position to clip space, plus a pass-through
/// of the vertex colour.
///
/// The canvas size is baked into the source because the slice has no uniform
/// path: a real renderer passes a per-frame camera/uniform buffer instead
/// (rendering-sdl3-gpu.md §3). Baking means a canvas resize recompiles the
/// shader, which is exactly the resource-recreation path the slice is meant to
/// exercise, and it keeps the shader free of a binding the test does not need.
///
/// The y flip is explicit: the test pattern is specified with y growing
/// downwards from the top-left, clip space has +y up.
std::string MakeVertexSource(int Width, int Height)
{
  return std::format(R"MSL(#include <metal_stdlib>
using namespace metal;
typedef struct {{ float4 position [[position]]; float4 color; }} V2F;
vertex V2F pattern_vertex(uint vid [[vertex_id]],
                          const device const float2* positions [[buffer(0)]],
                          const device const uchar4* colors [[buffer(1)]])
{{
  V2F out;
  const float2 p = positions[vid];
  const uchar4 c = colors[vid];
  out.position = float4(p.x * 2.0f / {0:.7f} - 1.0f, 1.0f - p.y * 2.0f / {1:.7f}, 0.5f, 1.0f);
  out.color = float4(c.x, c.y, c.z, c.w) / 255.0f;
  return out;
}}
)MSL",
                     static_cast<double>(Width), static_cast<double>(Height));
}

/// @brief Fragment stage: returns the interpolated colour.
///
/// Interpolated rather than flat because the triangle in the test scene carries
/// distinct corner colours, which is what makes the readback prove that
/// interpolation, not just coverage, ran.
[[nodiscard]] std::string MakeFragmentSource()
{
  return R"MSL(#include <metal_stdlib>
using namespace metal;
typedef struct { float4 position [[position]]; float4 color; } V2F;
fragment float4 pattern_fragment(V2F in [[stage_in]])
{
  return in.color;
}
)MSL";
}

/// @brief Small RAII holder for a mapped transfer buffer.
///
/// The slice maps once per frame and unmaps before returning; the guard exists
/// so an early `return unexpected(...)` between map and unmap cannot leak the
/// mapping (cpp.md §4: no manual pairing on an error path).
class MappedUpload
{
public:
  MappedUpload(SDL_GPUDevice* Device, SDL_GPUTransferBuffer* Buffer)
      : device_(Device)
      , buffer_(Buffer)
      , data_(static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(Device, Buffer, false)))
  {
  }

  ~MappedUpload()
  {
    if (data_ != nullptr)
      SDL_UnmapGPUTransferBuffer(device_, buffer_);
  }

  MappedUpload(const MappedUpload&) = delete;
  MappedUpload& operator=(const MappedUpload&) = delete;

  /// @brief The mapped host-visible pointer, or nullptr if the map failed.
  [[nodiscard]] std::uint8_t* Data() const { return data_; }

private:
  SDL_GPUDevice* device_{};
  SDL_GPUTransferBuffer* buffer_{};
  std::uint8_t* data_{};
};

} // namespace

PatternRenderer::~PatternRenderer()
{
  DestroySession();
}

void PatternRenderer::DestroySession()
{
  if (device_ == nullptr)
  {
    pipeline_ = nullptr;
    canvasWidth_ = 0;
    canvasHeight_ = 0;
    return;
  }
  // Release order matters only in the sense that the device owns everything:
  // releasing every object before the device goes away is what makes the
  // teardown assertable (no leaked MTLResources in the Metal validation log).
  if (pipeline_ != nullptr)
    SDL_ReleaseGPUGraphicsPipeline(device_->Handle(), pipeline_);
  if (vertexShader_ != nullptr)
    SDL_ReleaseGPUShader(device_->Handle(), vertexShader_);
  if (fragmentShader_ != nullptr)
    SDL_ReleaseGPUShader(device_->Handle(), fragmentShader_);
  if (target_ != nullptr)
    SDL_ReleaseGPUTexture(device_->Handle(), target_);
  if (vertexBuffer_ != nullptr)
    SDL_ReleaseGPUBuffer(device_->Handle(), vertexBuffer_);
  if (uploadTransfer_ != nullptr)
    SDL_ReleaseGPUTransferBuffer(device_->Handle(), uploadTransfer_);
  if (readbackTransfer_ != nullptr)
    SDL_ReleaseGPUTransferBuffer(device_->Handle(), readbackTransfer_);

  pipelineFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
  pipeline_ = nullptr;
  vertexShader_ = nullptr;
  fragmentShader_ = nullptr;
  target_ = nullptr;
  vertexBuffer_ = nullptr;
  uploadTransfer_ = nullptr;
  readbackTransfer_ = nullptr;
  device_ = nullptr;
  canvasWidth_ = 0;
  canvasHeight_ = 0;
}

StatusResult PatternRenderer::CreateSession(GPUDevice& device, int Width, int Height, SDL_GPUTextureFormat Format,
                                            bool WithTarget)
{
  DestroySession();
  if (!device.Valid())
    return std::unexpected(Status::GPUNoBackend);
  if (Width <= 0 || Height <= 0)
    return std::unexpected(Status::InvalidArgument);

  device_ = &device;
  pipelineFormat_ = Format;

  // Shaders. Built from source text: the Metal backend compiles MSL at runtime,
  // so no shader toolchain is part of this path (plan finding F6).
  const std::string vertexSource = MakeVertexSource(Width, Height);
  const std::string fragmentSource = MakeFragmentSource();

  SDL_GPUShaderCreateInfo vertexInfo{};
  vertexInfo.code_size = vertexSource.size();
  vertexInfo.code = reinterpret_cast<const Uint8*>(vertexSource.data());
  vertexInfo.entrypoint = "pattern_vertex";
  vertexInfo.format = SDL_GPU_SHADERFORMAT_MSL;
  vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  vertexShader_ = SDL_CreateGPUShader(device.Handle(), &vertexInfo);
  if (vertexShader_ == nullptr)
  {
    DestroySession();
    return std::unexpected(Status::GPUShaderFailed);
  }

  SDL_GPUShaderCreateInfo fragmentInfo{};
  fragmentInfo.code_size = fragmentSource.size();
  fragmentInfo.code = reinterpret_cast<const Uint8*>(fragmentSource.data());
  fragmentInfo.entrypoint = "pattern_fragment";
  fragmentInfo.format = SDL_GPU_SHADERFORMAT_MSL;
  fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  fragmentShader_ = SDL_CreateGPUShader(device.Handle(), &fragmentInfo);
  if (fragmentShader_ == nullptr)
  {
    DestroySession();
    return std::unexpected(Status::GPUShaderFailed);
  }

  // One buffer holding [positions][colours]. VERTEX usage is required for the
  // bind, and the upload goes through a transfer buffer rather than a
  // persistent-mapped buffer (the latter is an optional device feature).
  const PatternGeometry probeGeometry = BuildPatternGeometry(BuildTestPattern(Width, Height));
  const std::size_t totalBytes = std::max<std::size_t>(probeGeometry.TotalBytes(), 64);

  SDL_GPUBufferCreateInfo bufferInfo{};
  bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  bufferInfo.size = static_cast<Uint32>(totalBytes);
  vertexBuffer_ = SDL_CreateGPUBuffer(device.Handle(), &bufferInfo);
  if (vertexBuffer_ == nullptr)
  {
    DestroySession();
    return std::unexpected(Status::GPUBufferFailed);
  }

  SDL_GPUTransferBufferCreateInfo uploadInfo{};
  uploadInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  uploadInfo.size = static_cast<Uint32>(totalBytes);
  uploadTransfer_ = SDL_CreateGPUTransferBuffer(device.Handle(), &uploadInfo);
  if (uploadTransfer_ == nullptr)
  {
    DestroySession();
    return std::unexpected(Status::GPUBufferFailed);
  }

  const std::size_t readbackBytes =
    static_cast<std::size_t>(ReadbackRowStride(Width)) * static_cast<std::size_t>(Height);
  SDL_GPUTransferBufferCreateInfo readbackInfo{};
  readbackInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  readbackInfo.size = static_cast<Uint32>(readbackBytes);
  readbackTransfer_ = SDL_CreateGPUTransferBuffer(device.Handle(), &readbackInfo);
  if (readbackTransfer_ == nullptr)
  {
    DestroySession();
    return std::unexpected(Status::GPUBufferFailed);
  }

  // Offscreen target. RGBA8 is chosen so the readback bytes are directly the
  // reference's bytes — no format conversion step that could be wrong. The
  // present path allocates nothing here: it renders into the backbuffer.
  if (WithTarget)
  {
    SDL_GPUTextureCreateInfo targetInfo{};
    targetInfo.type = SDL_GPU_TEXTURETYPE_2D;
    targetInfo.format = Format;
    targetInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    targetInfo.width = static_cast<Uint32>(Width);
    targetInfo.height = static_cast<Uint32>(Height);
    targetInfo.layer_count_or_depth = 1;
    targetInfo.num_levels = 1;
    targetInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    target_ = SDL_CreateGPUTexture(device.Handle(), &targetInfo);
    if (target_ == nullptr)
    {
      DestroySession();
      return std::unexpected(Status::GPUTextureFailed);
    }
  }

  if (auto pipeline = BuildPipeline(Format); !pipeline)
  {
    DestroySession();
    return pipeline;
  }

  canvasWidth_ = Width;
  canvasHeight_ = Height;
  return {};
}

StatusResult PatternRenderer::BuildPipeline(SDL_GPUTextureFormat Format)
{
  // Pipeline state (shaders + vertex layout + target format) is rebuilt
  // together: in SDL3 GPU the target format is part of the pipeline, so a
  // canvas or format change is a pipeline change, not a state tweak.
  SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.vertex_shader = vertexShader_;
  pipelineInfo.fragment_shader = fragmentShader_;
  pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  static constexpr std::array<SDL_GPUVertexBufferDescription, 2> bufferDescriptions{
    SDL_GPUVertexBufferDescription{0, 8, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
    SDL_GPUVertexBufferDescription{1, 4, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0}};
  static constexpr std::array<SDL_GPUVertexAttribute, 2> attributes{
    SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0},
    SDL_GPUVertexAttribute{1, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, 0}};
  pipelineInfo.vertex_input_state.vertex_buffer_descriptions = bufferDescriptions.data();
  pipelineInfo.vertex_input_state.num_vertex_buffers = static_cast<Uint32>(bufferDescriptions.size());
  pipelineInfo.vertex_input_state.vertex_attributes = attributes.data();
  pipelineInfo.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());

  // CULLMODE_NONE and no depth: the scene has no overlapping primitives, so
  // the oracle must not depend on a winding convention or a depth test.
  pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  pipelineInfo.multisample_state.enable_mask = false;
  // No depth target at all: the pipeline has one colour target.

  SDL_GPUColorTargetDescription colorDescription{};
  colorDescription.format = Format;
  pipelineInfo.target_info.color_target_descriptions = &colorDescription;
  pipelineInfo.target_info.num_color_targets = 1;

  pipeline_ = SDL_CreateGPUGraphicsPipeline(device_->Handle(), &pipelineInfo);
  if (pipeline_ == nullptr)
    return std::unexpected(Status::GPUPipelineFailed);
  return {};
}

StatusResult PatternRenderer::UploadGeometry(SDL_GPUCommandBuffer* CommandBuffer, const TestPattern& scene)
{
  const PatternGeometry geometry = BuildPatternGeometry(scene);
  if (geometry.VertexCount == 0)
    return std::unexpected(Status::InvalidArgument);
  const std::size_t totalBytes = geometry.TotalBytes();

  // Map, copy, upload, unmap — all inside one copy pass. Skipping the copy pass
  // is the way this kind of slice silently renders an empty frame: the buffer
  // exists, the draw happens, and the data never arrives.
  MappedUpload mapped(device_->Handle(), uploadTransfer_);
  if (mapped.Data() == nullptr)
    return std::unexpected(Status::GPUBufferFailed);
  std::memcpy(mapped.Data(), geometry.Positions.data(), geometry.PositionBytes());
  std::memcpy(mapped.Data() + geometry.PositionBytes(), geometry.Colours.data(), geometry.ColourBytes());

  SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(CommandBuffer);
  if (copyPass == nullptr)
    return std::unexpected(Status::GPUPassFailed);

  SDL_GPUTransferBufferLocation source{};
  source.transfer_buffer = uploadTransfer_;
  source.offset = 0;
  SDL_GPUBufferRegion destination{};
  destination.buffer = vertexBuffer_;
  destination.offset = 0;
  destination.size = static_cast<Uint32>(totalBytes);
  SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);

  SDL_EndGPUCopyPass(copyPass);

  lastStats_.BytesUploaded = static_cast<int>(totalBytes);
  lastStats_.VertexCount = geometry.VertexCount;
  vertexPayloadBytes_ = totalBytes;
  return {};
}

void PatternRenderer::DrawScene(SDL_GPURenderPass* Pass, const TestPattern& scene)
{
  const PatternGeometry geometry = BuildPatternGeometry(scene);

  SDL_GPUBufferBinding bindings[2]{};
  bindings[0].buffer = vertexBuffer_;
  bindings[0].offset = 0;
  bindings[1].buffer = vertexBuffer_;
  bindings[1].offset = static_cast<Uint32>(geometry.PositionBytes());

  SDL_BindGPUVertexBuffers(Pass, 0, bindings, 2);
  SDL_BindGPUGraphicsPipeline(Pass, pipeline_);

  SDL_GPUViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.w = static_cast<float>(canvasWidth_);
  viewport.h = static_cast<float>(canvasHeight_);
  viewport.min_depth = 0.0f;
  viewport.max_depth = 1.0f;
  SDL_SetGPUViewport(Pass, &viewport);

  SDL_Rect scissor{0, 0, canvasWidth_, canvasHeight_};
  SDL_SetGPUScissor(Pass, &scissor);

  SDL_DrawGPUPrimitives(Pass, static_cast<Uint32>(geometry.VertexCount), 1, 0, 1);
  lastStats_.DrawCount = 1;
}

StatusResult PatternRenderer::RenderOffscreen(const TestPattern& scene, std::vector<std::uint8_t>& Image,
                                              FrameStats* Stats)
{
  Image.clear();
  lastStats_ = {};
  if (!SessionActive() || scene.Width != canvasWidth_ || scene.Height != canvasHeight_)
    return std::unexpected(Status::InvalidArgument);

  SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_->Handle());
  if (commandBuffer == nullptr)
    return std::unexpected(Status::GPUCommandBufferFailed);

  if (auto upload = UploadGeometry(commandBuffer, scene); !upload)
  {
    SDL_CancelGPUCommandBuffer(commandBuffer);
    return upload;
  }

  SDL_GPUColorTargetInfo colorTarget{};
  colorTarget.texture = target_;
  colorTarget.mip_level = 0;
  colorTarget.layer_or_depth_plane = 0;
  colorTarget.clear_color = SDL_FColor{0.0f, 0.0f, 1.0f, 1.0f};
  colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
  colorTarget.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
  if (renderPass == nullptr)
  {
    SDL_CancelGPUCommandBuffer(commandBuffer);
    return std::unexpected(Status::GPUPassFailed);
  }
  DrawScene(renderPass, scene);
  SDL_EndGPURenderPass(renderPass);

  // Readback: texture -> transfer buffer in its own copy pass, then a fence on
  // the submission. Reading the map before the fence resolves is the other way
  // to get a plausible-looking but stale image.
  SDL_GPUCopyPass* downloadPass = SDL_BeginGPUCopyPass(commandBuffer);
  if (downloadPass == nullptr)
  {
    SDL_CancelGPUCommandBuffer(commandBuffer);
    return std::unexpected(Status::GPUPassFailed);
  }
  const int rowStride = ReadbackRowStride(canvasWidth_);

  SDL_GPUTextureRegion sourceRegion{};
  sourceRegion.texture = target_;
  sourceRegion.w = static_cast<Uint32>(canvasWidth_);
  sourceRegion.h = static_cast<Uint32>(canvasHeight_);
  sourceRegion.d = 1;
  SDL_GPUTextureTransferInfo destination{};
  destination.transfer_buffer = readbackTransfer_;
  destination.offset = 0;
  destination.pixels_per_row = static_cast<Uint32>(rowStride / 4);
  destination.rows_per_layer = static_cast<Uint32>(canvasHeight_);
  SDL_DownloadFromGPUTexture(downloadPass, &sourceRegion, &destination);
  SDL_EndGPUCopyPass(downloadPass);

  void* mapped = SDL_MapGPUTransferBuffer(device_->Handle(), readbackTransfer_, false);
  if (mapped == nullptr)
  {
    SDL_CancelGPUCommandBuffer(commandBuffer);
    return std::unexpected(Status::GPUReadbackFailed);
  }

  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
  if (fence == nullptr)
  {
    SDL_UnmapGPUTransferBuffer(device_->Handle(), readbackTransfer_);
    return std::unexpected(Status::GPUSubmitFailed);
  }
  const bool waited = SDL_WaitForGPUFences(device_->Handle(), true, &fence, 1);
  SDL_ReleaseGPUFence(device_->Handle(), fence);
  if (!waited)
  {
    SDL_UnmapGPUTransferBuffer(device_->Handle(), readbackTransfer_);
    return std::unexpected(Status::GPUSubmitFailed);
  }

  Image.resize(static_cast<std::size_t>(rowStride) * static_cast<std::size_t>(canvasHeight_));
  std::memcpy(Image.data(), mapped, Image.size());
  SDL_UnmapGPUTransferBuffer(device_->Handle(), readbackTransfer_);

  lastStats_.SwapchainPath = false;
  lastStats_.TargetWidth = canvasWidth_;
  lastStats_.TargetHeight = canvasHeight_;
  if (Stats != nullptr)
    *Stats = lastStats_;
  return {};
}

StatusResult PatternRenderer::RenderPresent(const TestPattern& scene, SDL_Window* Window, FrameStats* Stats)
{
  lastStats_ = {};
  if (device_ == nullptr || Window == nullptr)
    return std::unexpected(Status::InvalidArgument);

  if (!SDL_ClaimWindowForGPUDevice(device_->Handle(), Window))
    return std::unexpected(Status::GPUDeviceFailed);

  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(Window, &width, &height);
  if (width <= 0 || height <= 0)
    return std::unexpected(Status::InvalidArgument);

  // The session must already match the window: format and pixel size. A
  // mismatch is a host bug (it kept a session built for another canvas), and
  // rebuilding mid-frame here would hide it, so it is reported.
  const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device_->Handle(), Window);
  if (!SessionActive() || width != canvasWidth_ || height != canvasHeight_ || swapchainFormat != pipelineFormat_)
    return std::unexpected(Status::InvalidArgument);

  SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_->Handle());
  if (commandBuffer == nullptr)
    return std::unexpected(Status::GPUCommandBufferFailed);

  if (auto upload = UploadGeometry(commandBuffer, scene); !upload)
  {
    SDL_CancelGPUCommandBuffer(commandBuffer);
    return upload;
  }

  SDL_GPUTexture* swapchainTexture = nullptr;
  Uint32 textureWidth = 0;
  Uint32 textureHeight = 0;
  const bool acquired =
    SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, Window, &swapchainTexture, &textureWidth, &textureHeight);
  if (!acquired || swapchainTexture == nullptr)
  {
    // Reported, never skipped: a frame that never acquired a target is exactly
    // the silently-empty case the oracle is here to catch.
    SDL_CancelGPUCommandBuffer(commandBuffer);
    return std::unexpected(acquired ? Status::GPUSubmitFailed : Status::GPUCommandBufferFailed);
  }

  SDL_GPUColorTargetInfo colorTarget{};
  colorTarget.texture = swapchainTexture;
  colorTarget.clear_color = SDL_FColor{0.0f, 0.0f, 1.0f, 1.0f};
  colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
  colorTarget.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, nullptr);
  if (renderPass == nullptr)
  {
    SDL_CancelGPUCommandBuffer(commandBuffer);
    return std::unexpected(Status::GPUPassFailed);
  }
  DrawScene(renderPass, scene);
  SDL_EndGPURenderPass(renderPass);

  if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
    return std::unexpected(Status::GPUSubmitFailed);

  lastStats_.SwapchainPath = true;
  lastStats_.TargetWidth = static_cast<int>(textureWidth);
  lastStats_.TargetHeight = static_cast<int>(textureHeight);
  if (Stats != nullptr)
    *Stats = lastStats_;
  return {};
}

} // namespace lod::render
