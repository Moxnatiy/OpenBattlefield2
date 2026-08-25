#include "obf2/gfx/device.h"

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
    return fail("буфер завантаження");
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

  // Читати результат можна лише після того, як GPU реально відпрацював.
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
    return fail("мапування знімка");
  }

  // Порядок байтів у swapchain залежить від бекенда, тож перекладаємо його
  // у формат SDL, а не припускаємо BGRA.
  SDL_PixelFormat pixelFormat = SDL_PIXELFORMAT_ARGB8888;
  switch (colorFormat()) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
      pixelFormat = SDL_PIXELFORMAT_ABGR8888;
      break;
    default:
      break;  // B8G8R8A8 та решта — ARGB8888 у little-endian порядку
  }

  SDL_Surface* surface =
      SDL_CreateSurfaceFrom(static_cast<int>(frame.width), static_cast<int>(frame.height),
                            pixelFormat, pixels, static_cast<int>(frame.width * 4));
  bool ok = surface != nullptr && SDL_SaveBMP(surface, path);
  const std::string saveError = ok ? std::string{} : SDL_GetError();
  if (surface != nullptr) SDL_DestroySurface(surface);

  SDL_UnmapGPUTransferBuffer(gpu_, transfer);
  SDL_ReleaseGPUTransferBuffer(gpu_, transfer);

  if (!ok && error) *error = "збереження знімка: " + saveError;
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
    // D32 є не скрізь; D24 як запасний варіант покриває решту.
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
