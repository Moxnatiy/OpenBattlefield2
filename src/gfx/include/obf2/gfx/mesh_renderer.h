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
    SDL_GPUTexture* texture = nullptr;   // базовий колір; nullptr -> заглушка
    SDL_GPUTexture* lightmap = nullptr;  // запечене освітлення; nullptr -> біла
    SDL_GPUTexture* detail = nullptr;    // дрібна структура, тайлиться
  };

  SDL_GPUBuffer* vertices = nullptr;
  SDL_GPUBuffer* indices = nullptr;
  std::vector<Range> ranges;
  std::vector<SDL_GPUTexture*> ownedTextures;

  // Обмежувальна сфера в локальних координатах меша — для відсікання
  // невидимого. Сфера, а не паралелепіпед: перевірка вчетверо дешевша,
  // а зайвих об'єктів пропускає одиниці.
  Vec3f boundsCenter;
  float boundsRadius = 0.0f;
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

  // Туман беремо з даних рівня (Sky.con). fogEnd == 0 вимикає його.
  struct Fog {
    Color color;
    float start = 0.0f;
    float end = 0.0f;
  };
  void setFog(const Fog& fog) { fog_ = fog; }

  // Кольори, якими множиться запечена лайтмапа терену (LightSettings.* рівня).
  // Скільки разів детейл-текстура повторюється на патч терену.
  void setDetailTiling(float tiles) { detailTiling_ = tiles; }

  void setTerrainLighting(Color sun, Color sky) {
    terrainSun_ = sun;
    terrainSky_ = sky;
  }

  void renderScene(const Frame& frame, const std::vector<DrawItem>& items,
                   const Mat4& viewProjection, Color clearColor);

  // Скільки примірників намальовано й скільки відсічено за останній кадр.
  int drawnLastFrame() const { return drawn_; }
  int culledLastFrame() const { return culled_; }

  // Прохід для інтерфейсу: без глибини, з альфа-змішуванням і без освітлення.
  // Координати вершин уже в NDC, тому матриця не потрібна.
  void renderOverlay(const Frame& frame, const std::vector<DrawItem>& items, Color clearColor);

 private:
  MeshRenderer() = default;
  SDL_GPUTexture* uploadTexture(const texture::Texture& source);

  Device* device_ = nullptr;
  SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
  SDL_GPUGraphicsPipeline* overlayPipeline_ = nullptr;
  SDL_GPUSampler* sampler_ = nullptr;
  SDL_GPUTexture* placeholder_ = nullptr;  // біла 1x1 для матеріалів без текстури
  Fog fog_;
  Color terrainSun_{1.0f, 1.0f, 1.0f, 1.0f};
  Color terrainSky_{0.6f, 0.7f, 0.9f, 1.0f};
  float detailTiling_ = 16.0f;
  int drawn_ = 0;
  int culled_ = 0;
};

}  // namespace obf2::gfx
