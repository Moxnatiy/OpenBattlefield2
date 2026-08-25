#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/gfx/device.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/texture/dds.h"

namespace obf2::gfx {

// Геометрія, завантажена у відеопам'ять.
struct GpuMesh {
  // Один виклик малювання: діапазон індексів плюс його текстура.
  struct Range {
    std::uint32_t indexStart = 0;
    std::uint32_t indexCount = 0;
    SDL_GPUTexture* texture = nullptr;  // nullptr -> береться заглушка
  };

  SDL_GPUBuffer* vertices = nullptr;
  SDL_GPUBuffer* indices = nullptr;
  std::vector<Range> ranges;
  std::vector<SDL_GPUTexture*> ownedTextures;
};

// Рендер мешів з базовою текстурою. Матеріали BF2 мають до чотирьох слотів
// (`_c` базовий колір, `_de` детейл, `_deb` нормаль детейлу, `_di`/`_cr`
// бруд і тріщини) — тут використовується лише нульовий; решта чекає на
// повноцінний матеріальний конвеєр.
class MeshRenderer {
 public:
  // Як знайти й прочитати текстуру за іменем із матеріалу. Зроблено
  // колбеком, щоб gfx нічого не знав про VFS та архіви гри.
  using TextureResolver =
      std::function<std::optional<texture::Texture>(const std::string& mapName)>;

  ~MeshRenderer();
  MeshRenderer(const MeshRenderer&) = delete;
  MeshRenderer& operator=(const MeshRenderer&) = delete;

  static std::unique_ptr<MeshRenderer> create(Device& device, std::string* error = nullptr);

  std::optional<GpuMesh> upload(const mesh::RenderMesh& source, const TextureResolver& resolve,
                                std::string* error = nullptr);
  void release(GpuMesh& gpuMesh);

  // Повний прохід: очищення кольору й глибини плюс малювання всіх діапазонів.
  void render(const Frame& frame, const GpuMesh& gpuMesh, const Mat4& modelViewProjection,
              Color clearColor);

  // Один меш, поставлений у світ власною матрицею. Однакова геометрія
  // (а на рівні це сотні однакових будинків) вантажиться раз і малюється
  // стільки разів, скільки її розставили.
  struct DrawItem {
    const GpuMesh* mesh = nullptr;
    Mat4 transform;
  };

  void renderScene(const Frame& frame, const std::vector<DrawItem>& items,
                   const Mat4& viewProjection, Color clearColor);

 private:
  MeshRenderer() = default;
  SDL_GPUTexture* uploadTexture(const texture::Texture& source);

  Device* device_ = nullptr;
  SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
  SDL_GPUSampler* sampler_ = nullptr;
  SDL_GPUTexture* placeholder_ = nullptr;  // біла 1x1 для матеріалів без текстури
};

}  // namespace obf2::gfx
