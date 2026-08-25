#pragma once
#include <memory>
#include <optional>
#include <string>

#include "obf2/core/math.h"
#include "obf2/gfx/device.h"
#include "obf2/mesh/bf2_mesh.h"

namespace obf2::gfx {

// Геометрія, завантажена у відеопам'ять.
struct GpuMesh {
  SDL_GPUBuffer* vertices = nullptr;
  SDL_GPUBuffer* indices = nullptr;
  std::uint32_t indexCount = 0;
};

// Найпростіший рендер мешів: один пайплайн, без текстур, освітлення —
// один напрямлений джерело у шейдері. Задача цього класу — довести, що
// геометрія з архівів BF2 доїжджає до екрана; текстури й матеріали окремо.
class MeshRenderer {
 public:
  ~MeshRenderer();
  MeshRenderer(const MeshRenderer&) = delete;
  MeshRenderer& operator=(const MeshRenderer&) = delete;

  static std::unique_ptr<MeshRenderer> create(Device& device, std::string* error = nullptr);

  std::optional<GpuMesh> upload(const mesh::RenderMesh& source, std::string* error = nullptr);
  void release(GpuMesh& gpuMesh);

  // Повний прохід: очищення кольору й глибини плюс малювання меша.
  void render(const Frame& frame, const GpuMesh& gpuMesh, const Mat4& modelViewProjection,
              Color clearColor);

 private:
  MeshRenderer() = default;

  Device* device_ = nullptr;
  SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
};

}  // namespace obf2::gfx
