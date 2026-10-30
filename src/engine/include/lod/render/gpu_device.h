#pragma once
/// @file gpu_device.h
/// @brief Owns one SDL3 GPU device and reports what the host backend actually
/// supports (rendering-sdl3-gpu.md §2, testing.md §2.13).
///
/// The render layer talks to SDL3 GPU, not to a native graphics API, so there
/// is exactly one device type here and no per-backend implementation classes:
/// SDL3 selects Metal / Vulkan / D3D12 and this layer only *reports* which one
/// it got. That is a measurement hook, not a multi-backend framework — the
/// slice this belongs to is Metal-only and every non-Metal host either works
/// through the same SDL3 path or is reported as unsupported.

#include "lod/status.h"

#include <cstdint>
#include <string>
#include <string_view>

// Opaque SDL handle, declared at global scope so it names the same type SDL
// declares (same pattern as platform.h's SDL_Window forward declaration).
struct SDL_GPUDevice;

namespace lod::render
{

/// @brief Names of the GPU backends SDL3 3.4 can select.
///
/// `Unknown` is the value for a backend SDL reports with an unrecognized name;
/// it is not an error value, and callers must print BackendName() rather than
/// assume Metal.
enum class GPUDriver : unsigned char
{
  Unknown, ///< Some other driver name (reported verbatim by DriverName()).
  Metal,   ///< SDL3METAL backend, native Metal.
  Vulkan,  ///< Vulkan 1.0+ through a loader (needs MoltenVK or a native driver).
  Vulkan1, ///< Vulkan 1.0 baseline.
  Direct3D12,
  Direct3D11,
  OpenGLES2,
};

/// @brief Print name for a GPUDriver value.
/// @param Driver Any GPUDriver value.
/// @return Static string ("Metal", "Vulkan", "Unknown", ...).
/// @note Thread: any (pure function).
[[nodiscard]] std::string_view GPUDriverName(GPUDriver Driver);

/// @brief Maps SDL's driver string to a GPUDriver value.
/// @param Name Value returned by SDL_GetGPUDeviceDriver(), e.g. "metal".
/// @return The matching driver, or GPUDriver::Unknown.
/// @note Thread: any (pure function).
[[nodiscard]] GPUDriver GPUDriverFromName(std::string_view Name);

/// @brief Owns one SDL_GPUDevice handle.
///
/// Move-only. Destroying an instance releases the device (and therefore every
/// resource created from it), which is what makes the render layer's shutdown
/// order checkable: the device outlives the renderer that uses it, and the
/// renderer is destroyed first.
///
/// @note Thread: main thread only (SDL GPU devices are not thread-safe).
class GPUDevice
{
public:
  /// @brief Empty, not-yet-created device handle.
  GPUDevice() = default;

  /// @brief Releases the device if one is held. Idempotent.
  ~GPUDevice();

  GPUDevice(const GPUDevice&) = delete;
  GPUDevice& operator=(const GPUDevice&) = delete;

  GPUDevice(GPUDevice&& Other) noexcept;
  GPUDevice& operator=(GPUDevice&& Other) noexcept;

  /// @brief Creates the GPU device through SDL3's selected backend.
  ///
  /// Two measured facts drive this signature (plan findings F4/F5): a GPU
  /// device needs a *working video driver*, not necessarily a window, and the
  /// dummy video driver has no GPU backend at all. The caller therefore decides
  /// whether a window exists; this function does not create or require one.
  ///
  /// @param DebugRequests true asks SDL for the backend debug/validation layer
  ///        (Metal command validation); false is the shipping default.
  /// @param DriverName Optional forced driver ("metal", "vulkan"); empty lets
  ///        SDL choose, which is the normal path.
  /// @return Status::Ok and a usable Handle(), or
  ///         Status::GPUNoBackend when the host has no GPU backend (no
  ///         display, no driver), Status::GPUDeviceFailed when a backend
  ///         exists but device creation failed. The failing SDL message is
  ///         kept in LastError() for the log.
  /// @note Thread: main thread.
  [[nodiscard]] StatusResult Create(bool DebugRequests = false, std::string_view DriverName = {});

  /// @brief Releases the device if held and clears the cached queries.
  /// Safe to call more than once; safe to call when never created.
  /// @note Thread: main thread.
  void Destroy();

  /// @brief The SDL device handle, or nullptr before Create / after Destroy.
  /// @note Thread: main thread.
  [[nodiscard]] SDL_GPUDevice* Handle() const { return Device_; }

  /// @brief Whether a device is currently held.
  /// @note Thread: main thread.
  [[nodiscard]] bool Valid() const { return Device_ != nullptr; }

  /// @brief Backend name reported by SDL for the live device.
  /// @return Driver string, or empty when no device is held.
  /// @note Thread: main thread.
  [[nodiscard]] std::string_view DriverName() const { return driverName_; }

  /// @brief The categorized backend (see GPUDriverFromName).
  /// @note Thread: main thread.
  [[nodiscard]] GPUDriver Driver() const { return driver_; }

  /// @brief Whether the live device accepts MSL *source* (not only metallib).
  ///
  /// Measured on the development host: the Metal backend offers both
  /// SDL_GPU_SHADERFORMAT_MSL and METALLIB, so hand-written MSL text compiles
  /// at runtime with no shader toolchain installed. This is the flag the shader
  /// creation path checks.
  /// @note Thread: main thread.
  [[nodiscard]] bool AcceptsMetalSource() const { return acceptsMetalSource_; }

  /// @brief Raw SDL_GPUShaderFormats mask of the live device (diagnostics).
  /// @note Thread: main thread.
  [[nodiscard]] unsigned ShaderFormatMask() const { return formatMask_; }

  /// @brief Message from the last failing SDL call (empty when none).
  /// @note Thread: main thread.
  [[nodiscard]] const std::string& LastError() const { return lastError_; }

private:
  SDL_GPUDevice* Device_{nullptr};       ///< Sole owner of the device.
  std::string driverName_;               ///< Cached SDL driver name.
  unsigned formatMask_{0};               ///< Cached SDL_GPUShaderFormats.
  GPUDriver driver_{GPUDriver::Unknown}; ///< Cached driver category.
  bool acceptsMetalSource_{false};       ///< MSL source accepted.
  std::string lastError_;                ///< Last failing SDL message.
};

} // namespace lod::render
