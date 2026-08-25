#include "obf2/level/level.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "obf2/con/interpreter.h"
#include "obf2/core/path.h"

namespace obf2::level {
namespace {

// Розбирає список чисел через слеш. У .con так записані і вектори, і
// довші набори на кшталт fogStartEndAndBase з чотирьох значень.
// Повертає, скільки чисел прочитано.
std::size_t parseSlashList(std::string_view text, float* out, std::size_t capacity) {
  std::size_t count = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size() && count < capacity; ++i) {
    if (i != text.size() && text[i] != '/') continue;
    const std::string part(text.substr(start, i - start));
    start = i + 1;
    if (part.empty()) continue;

    char* end = nullptr;
    const float value = std::strtof(part.c_str(), &end);
    if (end == part.c_str()) continue;
    out[count++] = value;
  }
  return count;
}

// Збирач стану під час виконання .con рівня. Мова — потік команд, тому
// heightmap.* застосовуються до останнього heightmapcluster.addHeightmap,
// а Object.* — до останнього Object.create.
class LevelBuilder {
 public:
  explicit LevelBuilder(Level& level) : level_(level) {}

  void operator()(const con::Command& command) {
    const std::string& path = command.lowerPath;

    // --- Heightdata.con ---
    // `heightmapcluster.addHeightmap Heightmap 0 0` — нульовий аргумент це
    // ім'я, координати кластера йдуть за ним. Основна карта — (0,0), решта
    // вісім навколо неї це низькодетальне оточення на горизонті.
    if (path == "heightmapcluster.addheightmap") {
      pendingCluster_ = true;
      clusterX_ = command.argInt(1).value_or(9999);
      clusterY_ = command.argInt(2).value_or(9999);
      return;
    }
    if (path == "heightmapcluster.setseawaterlevel") {
      level_.terrain.seaLevel = command.argFloat(0).value_or(0.0f);
      return;
    }
    // Нас цікавить лише центральний фрагмент кластера (0,0) — це власне
    // ігрова карта. Решта вісім — низькодетальне оточення на горизонті.
    if (path == "heightmap.setsize" && isPrimary()) {
      level_.primary.size = command.argInt(0).value_or(0);
      level_.primary.clusterX = clusterX_;
      level_.primary.clusterY = clusterY_;
      return;
    }
    if (path == "heightmap.setscale" && isPrimary()) {
      if (const auto scale = command.argVec3(0)) {
        level_.primary.scale = Vec3f{scale->x, scale->y, scale->z};
      }
      return;
    }
    if (path == "heightmap.setbitresolution" && isPrimary()) {
      level_.primary.bitResolution = command.argInt(0).value_or(16);
      return;
    }
    if (path == "heightmap.loadheightdata" && isPrimary()) {
      level_.primary.dataPath = std::string(command.argStr(0));
      return;
    }

    // --- Terrain.con ---
    if (path == "terrain.patchsize") {
      level_.terrain.patchSize = command.argInt(0).value_or(128);
      return;
    }
    if (path == "terrain.colormapbasename") {
      level_.terrain.colormapBase = std::string(command.argStr(0));
      return;
    }
    // --- Sky.con ---
    if (path == "renderer.fogcolor") {
      if (const auto color = command.argVec3(0)) {
        // У файлі 0..255, нам потрібно 0..1.
        level_.terrain.fogColor = Vec3f{color->x / 255.0f, color->y / 255.0f, color->z / 255.0f};
      }
      return;
    }
    if (path == "renderer.fogstartendandbase") {
      // Тут ЧОТИРИ компоненти через слеш ("0.00/610.00/0.00/0.50"), тому
      // argVec3 не годиться — розбираємо самі. Потрібні лише перші дві.
      float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      if (parseSlashList(command.argStr(0), values, 4) >= 2) {
        level_.terrain.fogStart = values[0];
        level_.terrain.fogEnd = values[1];
      }
      return;
    }
    if (path == "lightsettings.terrainsuncolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.terrainSunColor = Vec3f{color->x, color->y, color->z};
      }
      return;
    }
    if (path == "lightsettings.terrainskycolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.terrainSkyColor = Vec3f{color->x, color->y, color->z};
      }
      return;
    }
    if (path == "lightmanager.ambientcolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.ambientColor = Vec3f{color->x, color->y, color->z};
      }
      return;
    }
    if (path == "lightmanager.suncolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.sunColor = Vec3f{color->x, color->y, color->z};
      }
      return;
    }

    if (path == "terrain.lightmapbasename") {
      level_.terrain.lightmapBase = std::string(command.argStr(0));
      return;
    }
    if (path == "terrain.detailmapbasename") {
      level_.terrain.detailmapBase = std::string(command.argStr(0));
      return;
    }

    // --- Water.con ---
    if (path == "renderer.watercolor") {
      if (const auto color = command.argVec3(0)) {
        level_.terrain.waterColor = Vec3f{color->x, color->y, color->z};
      }
      return;
    }

    // --- StaticObjects.con ---
    if (path == "object.create") {
      StaticObject object;
      object.templateName = std::string(command.argStr(0));
      level_.objects.push_back(std::move(object));
      return;
    }
    if (level_.objects.empty()) return;
    StaticObject& current = level_.objects.back();

    if (path == "object.absoluteposition") {
      if (const auto position = command.argVec3(0)) {
        current.position = Vec3f{position->x, position->y, position->z};
      }
      return;
    }
    if (path == "object.rotation") {
      if (const auto rotation = command.argVec3(0)) {
        current.rotation = Vec3f{rotation->x, rotation->y, rotation->z};
        current.hasRotation = true;
      }
      return;
    }
  }

 private:
  bool isPrimary() const { return pendingCluster_ && clusterX_ == 0 && clusterY_ == 0; }

  Level& level_;
  bool pendingCluster_ = false;
  int clusterX_ = 0;
  int clusterY_ = 0;
};

bool loadHeights(FileSystem& files, Level& level, std::string* error) {
  const int size = level.primary.size;
  if (size <= 1) {
    if (error) *error = "у Heightdata.con немає розміру карти висот";
    return false;
  }

  const auto bytes = files.read(level.primary.dataPath);
  if (!bytes) {
    if (error) *error = "не знайдено карту висот: " + level.primary.dataPath;
    return false;
  }

  const std::size_t samples = static_cast<std::size_t>(size) * size;
  const std::size_t bytesPerSample = level.primary.bitResolution == 16 ? 2 : 1;
  if (bytes->size() < samples * bytesPerSample) {
    if (error) {
      *error = "карта висот менша за оголошений розмір: " + std::to_string(bytes->size()) +
               " байт замість " + std::to_string(samples * bytesPerSample);
    }
    return false;
  }

  level.heights.resize(samples);
  const auto* raw = reinterpret_cast<const std::uint8_t*>(bytes->data());
  for (std::size_t i = 0; i < samples; ++i) {
    std::uint32_t value = 0;
    if (bytesPerSample == 2) {
      value = static_cast<std::uint32_t>(raw[i * 2]) |
              (static_cast<std::uint32_t>(raw[i * 2 + 1]) << 8);
    } else {
      value = raw[i];
    }
    level.heights[i] = static_cast<float>(value) * level.primary.scale.y;
  }
  return true;
}

}  // namespace

bool mountLevel(FileSystem& files, const std::filesystem::path& modDir, std::string_view levelName,
                std::string* error) {
  const std::string mountPoint = "Levels/" + std::string(levelName);
  const std::filesystem::path dir = modDir / "Levels" / std::string(levelName);

  int mounted = 0;
  for (const char* archive : {"server.zip", "client.zip"}) {
    std::string archiveError;
    if (files.mountArchive(dir / archive, mountPoint, &archiveError)) {
      ++mounted;
    } else if (error != nullptr && error->empty()) {
      *error = archiveError;
    }
  }
  return mounted > 0;
}

std::optional<Level> loadLevel(FileSystem& files, std::string_view levelName, std::string* error) {
  Level level;
  level.name = std::string(levelName);

  const std::string base = "Levels/" + std::string(levelName);
  LevelBuilder builder(level);
  con::Interpreter interpreter(files, [&](const con::Command& command) { builder(command); });

  // Порядок як в Init.con рівня. Аргумент BF2Editor вмикає редакторську
  // гілку — саме ту, що читає вихідні .raw замість скомпільованого блоба.
  const std::vector<std::string> editorArgs{"BF2Editor"};
  interpreter.runFile(base + "/Heightdata.con");
  interpreter.runFile(base + "/Terrain.con", editorArgs);
  interpreter.runFile(base + "/StaticObjects.con", editorArgs);
  interpreter.runFile(base + "/Water.con");
  interpreter.runFile(base + "/Sky.con", editorArgs);

  if (!loadHeights(files, level, error)) return std::nullopt;
  return level;
}

std::vector<TerrainPatch> buildTerrainPatches(const Level& level, const FileSystem& files) {
  std::vector<TerrainPatch> patches;
  const int size = level.primary.size;
  const int patchSize = level.terrain.patchSize > 0 ? level.terrain.patchSize : 128;
  if (size <= 1) return patches;

  // 1025 вузлів = 1024 квади = 8 патчів по 128. Останній вузол спільний
  // із сусіднім патчем, інакше між ними лишалися б щілини.
  const int patchCount = (size - 1) / patchSize;

  for (int row = 0; row < patchCount; ++row) {
    for (int column = 0; column < patchCount; ++column) {
      TerrainPatch patch;
      patch.column = column;
      patch.row = row;

      char name[64];
      std::snprintf(name, sizeof(name), "%02dx%02d.dds", column, row);
      patch.colormap = level.terrain.colormapBase + name;

      // Для патчів під водою колормапи в грі просто немає — вона їх і не
      // малює. Пропускаємо, море закриє водна площина.
      if (!files.exists(patch.colormap)) continue;

      const int x0 = column * patchSize;
      const int z0 = row * patchSize;
      const int vertexCount = patchSize + 1;

      patch.geometry.vertices.reserve(static_cast<std::size_t>(vertexCount) * vertexCount);
      for (int z = 0; z < vertexCount; ++z) {
        for (int x = 0; x < vertexCount; ++x) {
          const int gx = x0 + x;
          const int gz = z0 + z;

          mesh::Vertex vertex;
          vertex.position = {level.worldX(gx), level.heightAt(gx, gz), level.worldZ(gz)};

          // Нормаль із сусідніх вузлів: центральна різниця по обох осях.
          const float left = level.heightAt(gx - 1, gz);
          const float right = level.heightAt(gx + 1, gz);
          const float up = level.heightAt(gx, gz - 1);
          const float down = level.heightAt(gx, gz + 1);
          const Vec3f normal = normalize(Vec3f{left - right, 2.0f * level.primary.scale.x,
                                               up - down});
          vertex.normal = {normal.x, normal.y, normal.z};

          // Кольорова мапа натягується на патч цілком.
          vertex.uv[0] = static_cast<float>(x) / static_cast<float>(patchSize);
          vertex.uv[1] = static_cast<float>(z) / static_cast<float>(patchSize);

          patch.geometry.vertices.push_back(vertex);
        }
      }

      patch.geometry.indices.reserve(static_cast<std::size_t>(patchSize) * patchSize * 6);
      for (int z = 0; z < patchSize; ++z) {
        for (int x = 0; x < patchSize; ++x) {
          const std::uint32_t topLeft = static_cast<std::uint32_t>(z * vertexCount + x);
          const std::uint32_t topRight = topLeft + 1;
          const std::uint32_t bottomLeft = topLeft + static_cast<std::uint32_t>(vertexCount);
          const std::uint32_t bottomRight = bottomLeft + 1;

          // Обхід проти годинникової — той самий, що й у мешах гри.
          patch.geometry.indices.push_back(topLeft);
          patch.geometry.indices.push_back(bottomLeft);
          patch.geometry.indices.push_back(topRight);

          patch.geometry.indices.push_back(topRight);
          patch.geometry.indices.push_back(bottomLeft);
          patch.geometry.indices.push_back(bottomRight);
        }
      }

      // Запечене освітлення того ж патча — другий шар текстур.
      if (!level.terrain.lightmapBase.empty()) {
        const std::string candidate = level.terrain.lightmapBase + name;
        if (files.exists(candidate)) patch.lightmap = candidate;
      }

      mesh::DrawRange range;
      range.indexStart = 0;
      range.indexCount = static_cast<std::uint32_t>(patch.geometry.indices.size());
      range.maps.push_back(patch.colormap);
      if (!patch.lightmap.empty()) {
        range.maps.push_back(patch.lightmap);
        range.lightmapInSecondSlot = true;
      }
      patch.geometry.ranges.push_back(std::move(range));

      patches.push_back(std::move(patch));
    }
  }
  return patches;
}

mesh::RenderMesh buildWaterPlane(const Level& level) {
  const float extent = level.halfExtent() * level.primary.scale.x;
  const float y = level.terrain.seaLevel;

  mesh::RenderMesh water;
  water.vertices = {
      mesh::Vertex{{-extent, y, -extent}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
      mesh::Vertex{{extent, y, -extent}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
      mesh::Vertex{{-extent, y, extent}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
      mesh::Vertex{{extent, y, extent}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
  };
  water.indices = {0, 2, 1, 1, 2, 3};
  water.bounds = mesh::Aabb{{-extent, y, -extent}, {extent, y, extent}};

  mesh::DrawRange range;
  range.indexCount = static_cast<std::uint32_t>(water.indices.size());
  range.maps.push_back(kWaterColorMap);
  water.ranges.push_back(std::move(range));
  return water;
}

}  // namespace obf2::level
