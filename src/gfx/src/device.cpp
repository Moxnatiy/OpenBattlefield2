#include "obf2/gfx/device.h"

namespace obf2::gfx {

Device::~Device() {
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
  device->window_ = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
  if (device->window_ == nullptr) return fail("SDL_CreateWindow");

  // Перелічуємо всі формати шейдерів, які вміємо постачати; SDL сам вибере
  // бекенд, доступний на цій платформі.
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
      case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_ESCAPE) quit_ = true;
        break;
      default:
        break;
    }
  }
  return !quit_;
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
    // Вікно згорнуте: буфер треба все одно віддати, інакше він протече.
    SDL_SubmitGPUCommandBuffer(frame.commands);
    return std::nullopt;
  }
  return frame;
}

void Device::endFrame(const Frame& frame, Color clear) {
  SDL_GPUColorTargetInfo target{};
  target.texture = frame.swapchain;
  target.clear_color = SDL_FColor{clear.r, clear.g, clear.b, clear.a};
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(frame.commands, &target, 1, nullptr);
  SDL_EndGPURenderPass(pass);
  SDL_SubmitGPUCommandBuffer(frame.commands);
}

}  // namespace obf2::gfx
