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

  // Пробіл або Enter — як пропуск заставки в грі. Прапорець зчитується один
  // раз, щоб одне натискання не пропустило кілька роликів поспіль.
  bool consumeSkip();

  // nullopt — кадр пропущено (вікно згорнуте чи swapchain недоступний).
  std::optional<Frame> beginFrame();
  // Прохід, що лише очищає екран — коли малювати нема чого.
  void clear(const Frame& frame, Color color);
  void submit(const Frame& frame);

  // Дописує до кадру завантаження swapchain у пам'ять, відправляє його,
  // чекає завершення і зберігає результат у BMP. Замінює submit().
  // Потрібно, щоб перевіряти рендер автоматично, без людини перед екраном.
  bool submitAndSave(const Frame& frame, const char* path, std::string* error = nullptr);

  // Буфер глибини під розмір swapchain; перестворюється при зміні розміру
  // вікна. nullptr — не вдалося створити.
  SDL_GPUTexture* acquireDepthTarget(Uint32 width, Uint32 height);
  SDL_GPUTextureFormat colorFormat() const;
  SDL_GPUTextureFormat depthFormat() const { return depthFormat_; }

  std::string_view driver() const { return driver_; }
  SDL_Window* window() const { return window_; }
  SDL_GPUDevice* gpu() const { return gpu_; }

 private:
  Device() = default;

  SDL_Window* window_ = nullptr;
  SDL_GPUDevice* gpu_ = nullptr;
  std::string driver_;
  bool quit_ = false;
  bool skip_ = false;

  SDL_GPUTexture* depth_ = nullptr;
  SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
  Uint32 depthWidth_ = 0, depthHeight_ = 0;
};

}  // namespace obf2::gfx
