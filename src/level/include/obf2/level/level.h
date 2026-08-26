#pragma once
// Завантаження рівня BF2.
//
// Рівень описує сам себе звичайними .con: Heightdata.con задає розмір і
// масштаб карти висот, Terrain.con — розбиття на патчі й імена текстур,
// StaticObjects.con — розстановку об'єктів. Тобто реверс тут не потрібен
// узагалі, достатньо інтерпретатора, який у нас уже є.
//
// Одна тонкість: Init.con рівня має дві гілки. Ігрова читає скомпільований
// terraindata.raw, редакторська — вихідні .raw карти висот. Ми йдемо
// редакторською (`v_arg1 = BF2Editor`), бо її формат повністю описаний
// даними, а скомпільований блоб довелося б розбирати реверсом.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::level {

struct HeightmapInfo {
  int size = 0;                  // 1025 — вузлів по стороні
  Vec3f scale{2.0f, 1.0f, 2.0f};  // світових одиниць на вузол / на одиницю висоти
  int bitResolution = 16;
  std::string dataPath;
  int clusterX = 0;
  int clusterY = 0;
};

struct TerrainInfo {
  int patchSize = 128;
  std::string colormapBase;
  std::string lightmapBase;
  std::string detailmapBase;
  float seaLevel = 0.0f;
  Vec3f waterColor{0.10f, 0.13f, 0.16f};  // renderer.waterColor з Water.con

  // Туман: Renderer.fogColor задано в діапазоні 0..255, а не 0..1.
  Vec3f fogColor{0.69f, 0.72f, 0.77f};
  float fogStart = 0.0f;
  float fogEnd = 0.0f;  // 0 = туману немає

  // Lightmanager.* із Sky.con — поки лише зберігаємо.
  Vec3f ambientColor{0.9f, 0.9f, 0.9f};
  Vec3f sunColor{1.0f, 1.0f, 1.0f};

  // LightSettings.TerrainSunColor / TerrainSkyColor — саме ними множиться
  // запечена лайтмапа терену. Значення бувають більші за 1: вони не лише
  // фарбують, а й підсвічують.
  Vec3f terrainSunColor{1.0f, 1.0f, 1.0f};
  Vec3f terrainSkyColor{0.6f, 0.7f, 0.9f};
};

// Дорога з CompiledRoads.con. Формат `.mesh` окремий від решти мешів:
// вершини лежать відносно точки `start`, а разом із позицією йдуть дві
// пари координат текстур і альфа для згасання на краях.
//
// Розкладка (за Project Dalian, engine/formats/mesh/bf2_road_mesh.cpp):
//   0  u32   версія
//   4  float3 start
//   16 float  довжина
//   20 float3 end
//   32 float3 misc
//   48 u32   кількість вершин   (заголовок — 52 байти)
//   далі   вершини по 32 байти: позиція, u/v, u1/v1, альфа
//   потім  u32 кількість індексів і самі індекси по 16 біт
struct Road {
  std::string templateName;
  std::string meshPath;
  Vec3f position;
  mesh::RenderMesh geometry;
};

std::optional<mesh::RenderMesh> loadRoadMesh(std::span<const std::byte> bytes,
                                             std::string* error = nullptr);

// Розстановка з StaticObjects.con: `Object.create` + absolutePosition/rotation.
struct StaticObject {
  std::string templateName;
  Vec3f position;
  Vec3f rotation;  // yaw/pitch/roll у градусах
  bool hasRotation = false;
};

struct Level {
  std::string name;
  TerrainInfo terrain;
  HeightmapInfo primary;
  std::vector<StaticObject> objects;
  std::vector<Road> roads;
  // Ім'я шаблону дороги -> шлях до текстури (з RoadTemplateTexture).
  std::unordered_map<std::string, std::string> roadTextures;

  // Висоти у світових одиницях, розмір size*size, рядки з півночі на південь.
  std::vector<float> heights;

  float heightAt(int x, int z) const {
    if (x < 0 || z < 0 || x >= primary.size || z >= primary.size) return 0.0f;
    return heights[static_cast<std::size_t>(z) * primary.size + x];
  }

  // Терен центрований на початку координат, як і позиції об'єктів.
  float worldX(int x) const { return (static_cast<float>(x) - halfExtent()) * primary.scale.x; }
  float worldZ(int z) const { return (static_cast<float>(z) - halfExtent()) * primary.scale.z; }
  float halfExtent() const { return static_cast<float>(primary.size - 1) * 0.5f; }
};

// Монтує server.zip і client.zip рівня у точку Levels/<name>.
bool mountLevel(FileSystem& files, const std::filesystem::path& modDir, std::string_view levelName,
                std::string* error = nullptr);

std::optional<Level> loadLevel(FileSystem& files, std::string_view levelName,
                               std::string* error = nullptr);

// Один патч терену: сітка patchSize x patchSize квадів зі своєю текстурою.
struct TerrainPatch {
  int column = 0;
  int row = 0;
  mesh::RenderMesh geometry;
  std::string colormap;  // шлях до .dds цього патча
  std::string lightmap;  // запечене освітлення того ж патча, якщо є
};

// Патчі, для яких у грі немає колормапи, повністю під водою — гра їх і не
// малює. Тому вони пропускаються, а море закриває водна площина.
std::vector<TerrainPatch> buildTerrainPatches(const Level& level, const FileSystem& files);

// Назва, яку рушій підставляє замість файлу текстури для водної поверхні:
// колір води задано числом у Water.con, а не картинкою.
inline constexpr const char* kWaterColorMap = "#waterColor";

mesh::RenderMesh buildWaterPlane(const Level& level);

}  // namespace obf2::level
