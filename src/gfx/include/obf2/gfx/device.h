#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>

namespace obf2::gfx {

struct WindowDesc {
  std::string title = "OpenBattlefield2";
  int width = 1280;
  int height = 720;
  bool resizable = true;
  bool debugDevice = false;  // шар валідації GPU-бекенда
};

struct Color {
  float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

// Кадр, готовий до запису команд. Живе рівно від beginFrame() до endFrame().
struct Frame {
  SDL_GPUCommandBuffer* commands = nullptr;
  SDL_GPUTexture* swapchain = nullptr;
  Uint32 width = 0;
  Uint32 height = 0;
};

// Вікно + GPU-пристрій.
//
// Свідомо на SDL_GPU, а не на OpenGL: на macOS GL заморожений на 4.1 і
// deprecated, а SDL_GPU лягає на Metal тут і на Vulkan/D3D12 на інших цілях —
// тобто один і той самий код працює на обох наших платформах без гілок.
// Шейдери за це доведеться платити компіляцією під кожен бекенд (MSL/SPIR-V).
class Device {
 public:
  ~Device();
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;

  static std::unique_ptr<Device> create(const WindowDesc& desc, std::string* error = nullptr);

  // false — користувач закрив вікно або натиснув Esc.
  bool pumpEvents();

  // nullopt — кадр пропущено (вікно згорнуте чи swapchain недоступний).
  std::optional<Frame> beginFrame();
  void endFrame(const Frame& frame, Color clear);

  std::string_view driver() const { return driver_; }
  SDL_Window* window() const { return window_; }
  SDL_GPUDevice* gpu() const { return gpu_; }

 private:
  Device() = default;

  SDL_Window* window_ = nullptr;
  SDL_GPUDevice* gpu_ = nullptr;
  std::string driver_;
  bool quit_ = false;
};

}  // namespace obf2::gfx
