#include "obf2/gfx/device.h"

#include <algorithm>
#include <cmath>

namespace obf2::gfx {

Device::~Device() {
  if (gpu_ != nullptr && depth_ != nullptr) SDL_ReleaseGPUTexture(gpu_, depth_);
  if (gpu_ != nullptr) {
    if (window_ != nullptr) SDL_ReleaseWindowFromGPUDevice(gpu_, window_);
    SDL_DestroyGPUDevice(gpu_);
  }
  if (window_ != nullptr) SDL_DestroyWindow(window_);
  SDL_Quit();
}

std::unique_ptr<Device> Device::create(const WindowDesc& desc, std::string* error) {
  auto fail = [error](std::string message) -> std::unique_ptr<Device> {
    if (error) *error = std::move(message) + ": " + SDL_GetError();
    return nullptr;
  };

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return fail("SDL_Init");

  auto device = std::unique_ptr<Device>(new Device());

  SDL_WindowFlags flags = 0;
  if (desc.resizable) flags |= SDL_WINDOW_RESIZABLE;

  // We ask for what was requested, but no more than the screen leaves — and
  // **keeping the aspect**. That matters: the game is made for 4:3, and if the
  // window is quietly clipped to the screen's size the HUD slides with it.
  int width = desc.width;
  int height = desc.height;
  SDL_Rect usable{};
  const SDL_DisplayID display = SDL_GetPrimaryDisplay();
  if (display != 0 && SDL_GetDisplayUsableBounds(display, &usable) && usable.w > 0 &&
      usable.h > 0 && (width > usable.w || height > usable.h)) {
    const double scale = std::min(static_cast<double>(usable.w) / width,
                                  static_cast<double>(usable.h) / height);
    width = std::max(1, static_cast<int>(width * scale));
    height = std::max(1, static_cast<int>(height * scale));
  }

  device->window_ = SDL_CreateWindow(desc.title.c_str(), width, height, flags);
  if (device->window_ == nullptr) return fail("SDL_CreateWindow");

  // The system may trim the window once more — for a menu bar or a panel. The
  // aspect then drifts, and we need it intact, so we adjust it by hand: take the
  // largest rectangle of the required aspect that fits.
  int actualWidth = 0;
  int actualHeight = 0;
  SDL_GetWindowSize(device->window_, &actualWidth, &actualHeight);
  if (actualWidth > 0 && actualHeight > 0 && desc.width > 0 && desc.height > 0) {
    const double wanted = static_cast<double>(desc.width) / desc.height;
    const double got = static_cast<double>(actualWidth) / actualHeight;
    if (std::abs(wanted - got) > 0.001) {
      if (got > wanted) {
        actualWidth = static_cast<int>(actualHeight * wanted);
      } else {
        actualHeight = static_cast<int>(actualWidth / wanted);
      }
      SDL_SetWindowSize(device->window_, actualWidth, actualHeight);
    }
  }

  // We list every shader format we can supply; SDL picks the backend available
  // on this platform itself.
  const SDL_GPUShaderFormat formats =
      SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL;
  device->gpu_ = SDL_CreateGPUDevice(formats, desc.debugDevice, nullptr);
  if (device->gpu_ == nullptr) return fail("SDL_CreateGPUDevice");

  if (!SDL_ClaimWindowForGPUDevice(device->gpu_, device->window_)) {
    return fail("SDL_ClaimWindowForGPUDevice");
  }

  const char* driver = SDL_GetGPUDeviceDriver(device->gpu_);
  device->driver_ = driver != nullptr ? driver : "unknown";
  return device;
}

bool Device::pumpEvents() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_QUIT:
        quit_ = true;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        mouseDeltaX_ += event.motion.xrel;
        mouseDeltaY_ += event.motion.yrel;
        break;
      case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_ESCAPE) quit_ = true;
        if (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN) skip_ = true;
        break;
      default:
        break;
    }
  }
  return !quit_;
}

bool Device::isKeyDown(std::string_view name) const {
  // The key names are taken as they are in `Settings/Controls.con`. Only the
  // ones that differ from an SDL scancode's name are translated; the rest match
  // once the prefix is removed.
  if (name.rfind("IDKey_", 0) != 0) return false;
  const std::string_view key = name.substr(6);

  static const std::pair<std::string_view, SDL_Scancode> kNamed[] = {
      {"Tab", SDL_SCANCODE_TAB},
      {"Enter", SDL_SCANCODE_RETURN},
      {"Space", SDL_SCANCODE_SPACE},
      {"Escape", SDL_SCANCODE_ESCAPE},
      {"Backspace", SDL_SCANCODE_BACKSPACE},
      {"Delete", SDL_SCANCODE_DELETE},
      {"Insert", SDL_SCANCODE_INSERT},
      {"Home", SDL_SCANCODE_HOME},
      {"End", SDL_SCANCODE_END},
      {"PageUp", SDL_SCANCODE_PAGEUP},
      {"PageDown", SDL_SCANCODE_PAGEDOWN},
      // The game calls CapsLock by its old Windows name.
      {"Capital", SDL_SCANCODE_CAPSLOCK},
      {"Grave", SDL_SCANCODE_GRAVE},
      {"Add", SDL_SCANCODE_KP_PLUS},
      {"LeftShift", SDL_SCANCODE_LSHIFT},
      {"RightShift", SDL_SCANCODE_RSHIFT},
      {"LeftCtrl", SDL_SCANCODE_LCTRL},
      {"RightCtrl", SDL_SCANCODE_RCTRL},
      {"LeftAlt", SDL_SCANCODE_LALT},
      {"RightAlt", SDL_SCANCODE_RALT},
      {"ArrowUp", SDL_SCANCODE_UP},
      {"ArrowDown", SDL_SCANCODE_DOWN},
      {"ArrowLeft", SDL_SCANCODE_LEFT},
      {"ArrowRight", SDL_SCANCODE_RIGHT},
      {"PrintScreen", SDL_SCANCODE_PRINTSCREEN},
  };

  SDL_Scancode code = SDL_SCANCODE_UNKNOWN;
  for (const auto& [named, scancode] : kNamed) {
    if (named == key) {
      code = scancode;
      break;
    }
  }
  if (code == SDL_SCANCODE_UNKNOWN) {
    // Letters, digits and the F keys are named the same — SDL knows them by name.
    code = SDL_GetScancodeFromName(std::string(key).c_str());
  }
  if (code == SDL_SCANCODE_UNKNOWN) return false;

  const bool* keys = SDL_GetKeyboardState(nullptr);
  return keys != nullptr && keys[code];
}

Device::InputState Device::readInput() {
  InputState state;
  const bool* keys = SDL_GetKeyboardState(nullptr);
  if (keys != nullptr) {
    // The layout as in the game: W/S forward and back, A/D sideways, Shift to run.
    if (keys[SDL_SCANCODE_W]) state.moveForward += 1.0f;
    if (keys[SDL_SCANCODE_S]) state.moveForward -= 1.0f;
    if (keys[SDL_SCANCODE_D]) state.moveRight += 1.0f;
    if (keys[SDL_SCANCODE_A]) state.moveRight -= 1.0f;
    state.sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    state.jump = keys[SDL_SCANCODE_SPACE];
  }

  float mouseX = 0.0f;
  float mouseY = 0.0f;
  const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouseX, &mouseY);
  state.fire = (buttons & SDL_BUTTON_LMASK) != 0;
  state.mouseX = mouseX;
  state.mouseY = mouseY;

  // A click is precisely the transition from released to pressed, otherwise one
  // press would fire every frame.
  state.clicked = state.fire && !mouseWasDown_;
  mouseWasDown_ = state.fire;

  // The movement accumulated over the frame is handed out once and cleared.
  state.mouseDeltaX = mouseDeltaX_;
  state.mouseDeltaY = mouseDeltaY_;
  mouseDeltaX_ = 0.0f;
  mouseDeltaY_ = 0.0f;
  return state;
}

void Device::setRelativeMouse(bool enabled) {
  if (window_ != nullptr) SDL_SetWindowRelativeMouseMode(window_, enabled);
}

bool Device::consumeSkip() {
  const bool pressed = skip_;
  skip_ = false;
  return pressed;
}

std::optional<Frame> Device::beginFrame() {
  Frame frame;
  frame.commands = SDL_AcquireGPUCommandBuffer(gpu_);
  if (frame.commands == nullptr) return std::nullopt;

  if (!SDL_WaitAndAcquireGPUSwapchainTexture(frame.commands, window_, &frame.swapchain,
                                             &frame.width, &frame.height)) {
    SDL_SubmitGPUCommandBuffer(frame.commands);
    return std::nullopt;
  }
  if (frame.swapchain == nullptr) {
    // The window is minimised: the buffer still has to be handed back, or it leaks.
    SDL_SubmitGPUCommandBuffer(frame.commands);
    return std::nullopt;
  }
  return frame;
}

void Device::clear(const Frame& frame, Color color) {
  SDL_GPUColorTargetInfo target{};
  target.texture = frame.swapchain;
  target.clear_color = SDL_FColor{color.r, color.g, color.b, color.a};
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(frame.commands, &target, 1, nullptr);
  SDL_EndGPURenderPass(pass);
}

void Device::submit(const Frame& frame) { SDL_SubmitGPUCommandBuffer(frame.commands); }

bool Device::submitAndSave(const Frame& frame, const char* path, std::string* error) {
  auto fail = [&](const char* what) {
    if (error) *error = std::string(what) + ": " + SDL_GetError();
    return false;
  };

  const Uint32 bytes = frame.width * frame.height * 4;
  SDL_GPUTransferBufferCreateInfo transferInfo{};
  transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  transferInfo.size = bytes;
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_, &transferInfo);
  if (transfer == nullptr) {
    SDL_SubmitGPUCommandBuffer(frame.commands);
    return fail("transfer buffer");
  }

  SDL_GPUTextureRegion region{};
  region.texture = frame.swapchain;
  region.w = frame.width;
  region.h = frame.height;
  region.d = 1;

  SDL_GPUTextureTransferInfo destination{};
  destination.transfer_buffer = transfer;
  destination.pixels_per_row = frame.width;
  destination.rows_per_layer = frame.height;

  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(frame.commands);
  SDL_DownloadFromGPUTexture(copy, &region, &destination);
  SDL_EndGPUCopyPass(copy);

  // The result can only be read once the GPU has actually finished.
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(frame.commands);
  if (fence == nullptr) {
    SDL_ReleaseGPUTransferBuffer(gpu_, transfer);
    return fail("fence");
  }
  SDL_WaitForGPUFences(gpu_, true, &fence, 1);
  SDL_ReleaseGPUFence(gpu_, fence);

  void* pixels = SDL_MapGPUTransferBuffer(gpu_, transfer, false);
  if (pixels == nullptr) {
    SDL_ReleaseGPUTransferBuffer(gpu_, transfer);
    return fail("mapping the screenshot");
  }

  // The swapchain's byte order depends on the backend, so we translate it into
  // an SDL format rather than assuming BGRA.
  SDL_PixelFormat pixelFormat = SDL_PIXELFORMAT_ARGB8888;
  switch (colorFormat()) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
      pixelFormat = SDL_PIXELFORMAT_ABGR8888;
      break;
    default:
      break;  // B8G8R8A8 and the rest — ARGB8888 in little-endian order
  }

  SDL_Surface* surface =
      SDL_CreateSurfaceFrom(static_cast<int>(frame.width), static_cast<int>(frame.height),
                            pixelFormat, pixels, static_cast<int>(frame.width * 4));
  bool ok = surface != nullptr && SDL_SaveBMP(surface, path);
  const std::string saveError = ok ? std::string{} : SDL_GetError();
  if (surface != nullptr) SDL_DestroySurface(surface);

  SDL_UnmapGPUTransferBuffer(gpu_, transfer);
  SDL_ReleaseGPUTransferBuffer(gpu_, transfer);

  if (!ok && error) *error = "saving the screenshot: " + saveError;
  return ok;
}

SDL_GPUTextureFormat Device::colorFormat() const {
  return SDL_GetGPUSwapchainTextureFormat(gpu_, window_);
}

SDL_GPUTexture* Device::acquireDepthTarget(Uint32 width, Uint32 height) {
  if (depth_ != nullptr && depthWidth_ == width && depthHeight_ == height) return depth_;

  if (depth_ != nullptr) {
    SDL_ReleaseGPUTexture(gpu_, depth_);
    depth_ = nullptr;
  }
  if (depthFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID) {
    // D32 is not everywhere; D24 as a fallback covers the rest.
    for (const SDL_GPUTextureFormat candidate :
         {SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM}) {
      if (SDL_GPUTextureSupportsFormat(gpu_, candidate, SDL_GPU_TEXTURETYPE_2D,
                                       SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        depthFormat_ = candidate;
        break;
      }
    }
    if (depthFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID) return nullptr;
  }

  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = depthFormat_;
  info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  info.width = width;
  info.height = height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;

  depth_ = SDL_CreateGPUTexture(gpu_, &info);
  depthWidth_ = width;
  depthHeight_ = height;
  return depth_;
}

}  // namespace obf2::gfx
