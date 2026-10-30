/// @file gpu_device.cpp
/// @brief SDL3 GPU device creation and backend reporting (rendering-sdl3-gpu.md
/// §2). The only place in the engine that calls SDL_CreateGPUDevice.

#include "lod/render/gpu_device.h"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <string>

namespace lod::render
{

std::string_view GPUDriverName(GPUDriver Driver)
{
  switch (Driver)
  {
    case GPUDriver::Metal:
      return "Metal";
    case GPUDriver::Vulkan:
      return "Vulkan";
    case GPUDriver::Vulkan1:
      return "Vulkan1";
    case GPUDriver::Direct3D12:
      return "Direct3D12";
    case GPUDriver::Direct3D11:
      return "Direct3D11";
    case GPUDriver::OpenGLES2:
      return "OpenGLES2";
    case GPUDriver::Unknown:
      return "Unknown";
  }
  return "Unknown";
}

GPUDriver GPUDriverFromName(std::string_view Name)
{
  // SDL spells its driver names in lower case ("metal", "vulkan", "opengles2",
  // "direct3d12", "direct3d11") and SDL_gpu.h also names the Metal render data
  // types SDL3METAL_*. Comparing case-insensitively keeps a host that reports
  // "METAL" (or a future rename) out of the Unknown bucket.
  auto Equals = [](std::string_view A, std::string_view B)
  {
    if (A.size() != B.size())
      return false;
    for (std::size_t i = 0; i < A.size(); ++i)
    {
      const char a = A[i] >= 'A' && A[i] <= 'Z' ? static_cast<char>(A[i] - 'A' + 'a') : A[i];
      if (a != B[i])
        return false;
    }
    return true;
  };
  if (Equals(Name, "metal"))
    return GPUDriver::Metal;
  if (Equals(Name, "vulkan"))
    return GPUDriver::Vulkan;
  if (Equals(Name, "vulkan1"))
    return GPUDriver::Vulkan1;
  if (Equals(Name, "direct3d12"))
    return GPUDriver::Direct3D12;
  if (Equals(Name, "direct3d11"))
    return GPUDriver::Direct3D11;
  if (Equals(Name, "opengles2"))
    return GPUDriver::OpenGLES2;
  return GPUDriver::Unknown;
}

GPUDevice::~GPUDevice()
{
  Destroy();
}

GPUDevice::GPUDevice(GPUDevice&& Other) noexcept
    : Device_(Other.Device_)
    , driverName_(std::move(Other.driverName_))
    , formatMask_(Other.formatMask_)
    , driver_(Other.driver_)
    , acceptsMetalSource_(Other.acceptsMetalSource_)
    , lastError_(std::move(Other.lastError_))
{
  Other.Device_ = nullptr;
  Other.acceptsMetalSource_ = false;
  Other.formatMask_ = 0;
  Other.driver_ = GPUDriver::Unknown;
}

GPUDevice& GPUDevice::operator=(GPUDevice&& Other) noexcept
{
  if (this != &Other)
  {
    Destroy();
    Device_ = Other.Device_;
    driverName_ = std::move(Other.driverName_);
    formatMask_ = Other.formatMask_;
    driver_ = Other.driver_;
    acceptsMetalSource_ = Other.acceptsMetalSource_;
    lastError_ = std::move(Other.lastError_);
    Other.Device_ = nullptr;
    Other.acceptsMetalSource_ = false;
    Other.formatMask_ = 0;
    Other.driver_ = GPUDriver::Unknown;
  }
  return *this;
}

StatusResult GPUDevice::Create(bool DebugRequests, std::string_view DriverName)
{
  Destroy();
  SDL_ClearError();

  // The development host offers two drivers (metal, vulkan) but only Metal is
  // functional here: SPIRV reports no support and neither MoltenVK nor a
  // Vulkan loader is installed. Nothing is *forced* by default — SDL's own
  // choice is the measurement — and a forced name is only ever a diagnostic
  // override passed in by the caller.
  const std::string forcedDriver = DriverName.empty() ? std::string{} : std::string(DriverName);
  const char* forced = forcedDriver.empty() ? nullptr : forcedDriver.c_str();
  Device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, DebugRequests, forced);
  if (Device_ == nullptr)
  {
    lastError_ = SDL_GetError();
    // Distinguishing the two cases matters for CI reporting: "no backend" is a
    // host capability (a display-less runner), while a failure after a backend
    // was found means our own request was wrong.
    const bool noBackend = lastError_.find("No supported SDL_GPU backend") != std::string::npos;
    return std::unexpected(noBackend ? Status::GPUNoBackend : Status::GPUDeviceFailed);
  }

  if (const char* name = SDL_GetGPUDeviceDriver(Device_); name != nullptr)
    driverName_ = name;
  driver_ = GPUDriverFromName(driverName_);
  formatMask_ = static_cast<unsigned>(SDL_GetGPUShaderFormats(Device_));
  acceptsMetalSource_ = (formatMask_ & SDL_GPU_SHADERFORMAT_MSL) != 0u;
  return {};
}

void GPUDevice::Destroy()
{
  if (Device_ != nullptr)
    SDL_DestroyGPUDevice(Device_);
  Device_ = nullptr;
  driverName_.clear();
  formatMask_ = 0;
  driver_ = GPUDriver::Unknown;
  acceptsMetalSource_ = false;
  lastError_.clear();
}

} // namespace lod::render
