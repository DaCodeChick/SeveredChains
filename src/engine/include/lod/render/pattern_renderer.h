#pragma once
/// @file pattern_renderer.h
/// @brief The M2 vertical slice: one test scene, one vertex buffer, one
/// pipeline, one triangle-list draw, read back and compared against the CPU
/// reference (rendering-sdl3-gpu.md §2, testing.md §2.13).
///
/// This is deliberately *not* the batch renderer described in
/// rendering-sdl3-gpu.md §3: there is no model pool, no per-frame arena, no
/// PSX VRAM texture staging and no multi-pass target chain. It is the smallest
/// thing that proves the device → shader → pipeline → buffer-upload → render
/// pass → readback → present chain works on Metal, and that proves it with an
/// oracle the CI can run without a display.

#include "lod/render/gpu_device.h"
#include "lod/render/test_pattern.h"
#include "lod/status.h"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

struct SDL_Window;

namespace lod::render
{

/// @brief Owns the GPU objects the test slice needs for one canvas size.
///
/// Lifetime: CreateSession before any render call; DestroySession (or
/// destruction) before the device goes away. The renderer keeps no pointer to
/// AppContext and registers nothing: the host owns it directly, which keeps the
/// render layer's dependency set at gpu -> platform (engine-design.md §3).
///
/// @note Thread: main thread only (SDL GPU is not thread-safe and every method
/// submits work).
class PatternRenderer
{
public:
  /// @brief Diagnostic counters for one frame.
  struct FrameStats
  {
    int VertexCount = 0;        ///< Vertices submitted in the draw.
    int DrawCount = 0;          ///< Draw calls issued (1 for the slice).
    int BytesUploaded = 0;      ///< Vertex bytes pushed through the copy pass.
    bool SwapchainPath = false; ///< True for the window/present path.
    int TargetWidth = 0;        ///< Canvas width the resources were built for.
    int TargetHeight = 0;       ///< Canvas height the resources were built for.
  };

  PatternRenderer() = default;
  ~PatternRenderer();
  PatternRenderer(const PatternRenderer&) = delete;
  PatternRenderer& operator=(const PatternRenderer&) = delete;

  /// @brief Builds shaders, pipeline, render target and buffers for one canvas.
  ///
  /// This is also the resize path: calling it again rebuilds every
  /// size-dependent object from scratch. A session serves exactly one target
  /// format, because in SDL3 GPU the target format is part of the pipeline
  /// state — the offscreen path uses RGBA8, the present path passes whatever
  /// SDL_GetGPUSwapchainTextureFormat reports.
  ///
  /// @param device Live device; it must outlive the renderer.
  /// @param Width Canvas width in pixels.
  /// @param Height Canvas height in pixels.
  /// @param Format Render-target format the pipeline is built for.
  /// @param WithTarget false builds no offscreen texture (the present path
  ///        renders into the backbuffer and has nothing to allocate here).
  /// @return Status::Ok, or GPUShaderFailed / GPUPipelineFailed /
  ///         GPUTextureFailed / GPUBufferFailed, naming the step that stopped.
  ///         Failure tears the session back down; no half-built state is left.
  /// @note Thread: main thread.
  [[nodiscard]] StatusResult CreateSession(GPUDevice& device, int Width, int Height,
                                           SDL_GPUTextureFormat Format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                           bool WithTarget = true);

  /// @brief Releases every GPU object. Idempotent.
  /// @note Thread: main thread.
  void DestroySession();

  /// @brief Whether a session is live.
  /// @note Thread: main thread.
  [[nodiscard]] bool SessionActive() const { return pipeline_ != nullptr; }

  /// @brief Renders the scene into the offscreen RGBA8 target, submits, waits
  /// on the fence and downloads the image.
  ///
  /// The CI-runnable path: needs a working GPU *driver*, no window, and no
  /// display server beyond what the driver itself needs.
  ///
  /// @param scene The scene to draw.
  /// @param Image Output, resized to ReadbackRowStride(Width)*Height bytes and
  ///        filled with the readback. Contents are undefined on failure.
  /// @param Stats Optional out: frame counters for this call.
  /// @return Status::Ok, or GPUCommandBufferFailed / GPUBufferFailed /
  ///         GPUPassFailed / GPUSubmitFailed / GPUReadbackFailed.
  /// @note Thread: main thread; blocks on the fence. This is a verification
  /// path, not a frame loop.
  [[nodiscard]] StatusResult RenderOffscreen(const TestPattern& scene, std::vector<std::uint8_t>& Image,
                                             FrameStats* Stats = nullptr);

  /// @brief Renders the scene into the window's backbuffer and presents.
  ///
  /// Dev-machine path: requires a live window on a real video driver, and a
  /// session built for that window's swapchain format and pixel size. A failed
  /// or null swapchain acquire is reported, never skipped.
  ///
  /// @param scene The scene to draw.
  /// @param Window Target window, already claimed for the device.
  /// @param Stats Optional out: frame counters for this call.
  /// @return Status::Ok when the frame was submitted and presented.
  /// @note Thread: main thread.
  [[nodiscard]] StatusResult RenderPresent(const TestPattern& scene, SDL_Window* Window, FrameStats* Stats = nullptr);

  /// @brief Counters from the most recent render call.
  /// @note Thread: main thread.
  [[nodiscard]] const FrameStats& LastStats() const { return lastStats_; }

private:
  /// @brief Builds the pipeline for a target format.
  [[nodiscard]] StatusResult BuildPipeline(SDL_GPUTextureFormat Format);

  /// @brief Encodes the scene and uploads it through a copy pass.
  /// @param CommandBuffer Open command buffer (not owned).
  /// @param scene The scene to encode and upload.
  /// @return Status::Ok once the upload is recorded; GPUBufferFailed when the
  ///         staging buffer, the map, or the buffer itself is unavailable.
  [[nodiscard]] StatusResult UploadGeometry(SDL_GPUCommandBuffer* CommandBuffer, const TestPattern& scene);

  /// @brief Records the single draw into a begun render pass.
  /// @param Pass Open render pass (not owned).
  /// @param scene Supplies the vertex count.
  void DrawScene(SDL_GPURenderPass* Pass, const TestPattern& scene);

  GPUDevice* device_{nullptr};                ///< Non-owning; must outlive the renderer.
  SDL_GPUShader* vertexShader_{};             ///< Owned once a session is live.
  SDL_GPUShader* fragmentShader_{};           ///< Owned once a session is live.
  SDL_GPUGraphicsPipeline* pipeline_{};       ///< Owned once a session is live.
  SDL_GPUTexture* target_{};                  ///< Offscreen RGBA8 target.
  SDL_GPUBuffer* vertexBuffer_{};             ///< Interleaved-free vertex payload.
  SDL_GPUTransferBuffer* uploadTransfer_{};   ///< Upload staging.
  SDL_GPUTransferBuffer* readbackTransfer_{}; ///< Download staging.
  int canvasWidth_{0};                        ///< Canvas width the session was built for (0 = none).
  int canvasHeight_{0};                       ///< Canvas height the session was built for (0 = none).
  SDL_GPUTextureFormat pipelineFormat_{SDL_GPU_TEXTUREFORMAT_INVALID}; ///< Format the pipeline targets.
  std::size_t vertexPayloadBytes_{0};                                  ///< Byte size of the encoded vertex payload.
  FrameStats lastStats_;                                               ///< Last frame's counters.
};

} // namespace lod::render
