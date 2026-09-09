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
  bool debugDevice = false;  // the GPU backend's validation layer
};

struct Color {
  float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

// A frame ready to record commands into. Lives exactly from beginFrame() to endFrame().
struct Frame {
  SDL_GPUCommandBuffer* commands = nullptr;
  SDL_GPUTexture* swapchain = nullptr;
  Uint32 width = 0;
  Uint32 height = 0;
};

// The window plus the GPU device.
//
// Deliberately on SDL_GPU rather than OpenGL: on macOS GL is frozen at 4.1 and
// deprecated, while SDL_GPU maps onto Metal here and onto Vulkan/D3D12 on the
// other targets — so the same code works on both of our platforms without branches.
// The price is shaders: they have to be compiled for each backend (MSL/SPIR-V).
class Device {
 public:
  ~Device();
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;

  static std::unique_ptr<Device> create(const WindowDesc& desc, std::string* error = nullptr);

  // false means the user closed the window or pressed Esc.
  bool pumpEvents();

  // Space or Enter — like skipping an intro in the game. The flag is read once,
  // so that one press does not skip several movies in a row.
  bool consumeSkip();

  // Keyboard and mouse state for driving the player. The engine does nothing
  // with it itself — it is raw material for ControlMap and for the input sent to the server.
  struct InputState {
    float moveForward = 0.0f;  // -1..1
    float moveRight = 0.0f;
    float mouseDeltaX = 0.0f;  // pixels per frame
    float mouseDeltaY = 0.0f;
    bool sprint = false;
    bool fire = false;
    bool jump = false;

    // The cursor's absolute position in window pixels — for the menu, where the
    // mouse is not captured.
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool clicked = false;  // the left button was just pressed
  };
  InputState readInput();

  // Whether a key is down, named the way the game writes it: "IDKey_Tab",
  // "IDKey_Q". The name comes from `ControlMap`, that is from the data, so no
  // layout is baked in here — only the translation of a name into an SDL scancode.
  bool isKeyDown(std::string_view name) const;

  // Mouse capture: without it looking around runs into the window's edges.
  void setRelativeMouse(bool enabled);

  // nullopt means the frame was skipped (the window is minimised or the swapchain is unavailable).
  std::optional<Frame> beginFrame();
  // A pass that only clears the screen — when there is nothing to draw.
  void clear(const Frame& frame, Color color);
  void submit(const Frame& frame);

  // Appends a download of the swapchain into memory to the frame, submits it,
  // waits for completion and saves the result as a BMP. Replaces submit().
  // Needed to check the renderer automatically, without a person at the screen.
  bool submitAndSave(const Frame& frame, const char* path, std::string* error = nullptr);

  // A depth buffer the size of the swapchain; recreated when the window is
  // resized. nullptr means it could not be created.
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
  bool mouseWasDown_ = false;
  float mouseDeltaX_ = 0.0f;
  float mouseDeltaY_ = 0.0f;

  SDL_GPUTexture* depth_ = nullptr;
  SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
  Uint32 depthWidth_ = 0, depthHeight_ = 0;
};

}  // namespace obf2::gfx
