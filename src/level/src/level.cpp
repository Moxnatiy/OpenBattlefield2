#include "obf2/level/level.h"

#include <cstdio>
#include <algorithm>
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
    //   gameLogic.setTeamName 1 "CH"
    if (path == "gamelogic.setteamname") {
      const int team = command.argInt(0).value_or(-1);
      if (team >= 0 && team <= 2) level_.teamNames[team] = std::string(command.argStr(1));
      return;
    }
    // Камеру екрана появи задає сам рівень:
    //   gameLogic.setBeforeSpawnCamera -50/185/-285 -16/-3/0
    // Обидві трійки записані одним словом через скісну риску — місце і
    // поворот у градусах.
    if (path == "gamelogic.setbeforespawncamera") {
      const auto triple = [&](std::size_t i, Vec3f& out) {
        if (i >= command.args.size()) return false;
        const std::string& text = command.args[i];
        float v[3] = {0.0f, 0.0f, 0.0f};
        std::size_t at = 0;
        for (float& part : v) {
          const std::size_t slash = text.find('/', at);
          part = std::strtof(text.substr(at, slash - at).c_str(), nullptr);
          if (slash == std::string::npos) break;
          at = slash + 1;
        }
        out = Vec3f{v[0], v[1], v[2]};
        return true;
      };
      if (triple(0, level_.beforeSpawnCameraPos) && triple(1, level_.beforeSpawnCameraRot)) {
        level_.hasBeforeSpawnCamera = true;
      }
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

    // --- RoadTemplate: текстури доріг ---
    // Визначення лежать у Roads/Splines/*.con за редакторською гілкою.
    if (path == "roadtemplate.setname") {
      roadTemplateName_ = std::string(command.argStr(0));
      return;
    }
    if (path == "roadtemplatetexture.settexturefile" && !roadTemplateName_.empty()) {
      // Перша текстура шаблону — основна; наступні це шари змішування.
      if (level_.roadTextures.find(roadTemplateName_) == level_.roadTextures.end()) {
        level_.roadTextures.emplace(roadTemplateName_, std::string(command.argStr(0)) + ".dds");
      }
      return;
    }

    // --- CompiledRoads.con ---
    // Дороги теж починаються з object.create, але далі йде loadMesh —
    // саме він і відрізняє їх від звичайної розстановки.
    if (path == "object.geometry.loadmesh") {
      if (!level_.objects.empty()) {
        Road road;
        road.templateName = level_.objects.back().templateName;
        road.meshPath = std::string(command.argStr(0));
        level_.roads.push_back(std::move(road));
        // Прибираємо з розстановки: це дорога, а не статичний об'єкт.
        level_.objects.pop_back();
        pendingRoad_ = true;
      }
      return;
    }

    // --- StaticObjects.con ---
    if (path == "object.create") {
      pendingRoad_ = false;
      StaticObject object;
      object.templateName = std::string(command.argStr(0));
      level_.objects.push_back(std::move(object));
      return;
    }
    // Дорога вже забрала свій object.create, тому далі працюємо або з нею,
    // або з останнім статичним об'єктом.
    if (level_.objects.empty() && !pendingRoad_) return;
    static StaticObject dummy;
    StaticObject& current = level_.objects.empty() ? dummy : level_.objects.back();

    if (path == "object.absoluteposition") {
      if (const auto position = command.argVec3(0)) {
        if (pendingRoad_ && !level_.roads.empty()) {
          level_.roads.back().position = Vec3f{position->x, position->y, position->z};
        } else {
          current.position = Vec3f{position->x, position->y, position->z};
        }
      }
      return;
    }
    if (path == "object.absolutetransformation") {
      // Чотири групи по чотири числа: три рядки повороту з масштабом і
      // рядок переносу. Формат той самий, що в редакторі: [x/y/z/w].
      float values[16] = {};
      int count = 0;
      for (const std::string& argument : command.args) {
        // Числа розділені скісними, а вся група взята в дужки — беремо
        // просто всі числа підряд.
        const char* cursor = argument.c_str();
        while (*cursor != '\0' && count < 16) {
          if (*cursor == '[' || *cursor == ']' || *cursor == '/') { ++cursor; continue; }
          char* end = nullptr;
          const float value = std::strtof(cursor, &end);
          if (end == cursor) { ++cursor; continue; }
          values[count++] = value;
          cursor = end;
        }
      }
      if (count >= 16) {
        for (int i = 0; i < 16; ++i) current.transform.m[i] = values[i];
        current.hasTransform = true;
        current.position = Vec3f{values[12], values[13], values[14]};
      }
      return;
    }
    if (path == "object.isovergrowth") {
      current.isOvergrowth = command.argBool(0).value_or(false);
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
  bool pendingRoad_ = false;
  std::string roadTemplateName_;
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
  interpreter.runFile(base + "/Init.con");
  interpreter.runFile(base + "/Heightdata.con");
  interpreter.runFile(base + "/Terrain.con", editorArgs);
  interpreter.runFile(base + "/StaticObjects.con", editorArgs);
  interpreter.runFile(base + "/Water.con");
  interpreter.runFile(base + "/Sky.con", editorArgs);
  interpreter.runFile(base + "/CompiledRoads.con", editorArgs);
  // Рослинність із зіткненнями: справжні примірники з точними матрицями.
  // Оригінал малює її окремою системою, але моделі й місця — ті самі.
  interpreter.runFile(base + "/Overgrowth/OvergrowthCollision.con", editorArgs);

  // Шаблони доріг: у них лежать текстури, і вони живуть в об'єктах гри,
  // а не в рівні.
  for (const std::string& path : files.list("objects/roads/splines")) {
    if (assetExtension(path) == "con") interpreter.runFile(path, editorArgs);
  }

  // Геометрію доріг читаємо окремо: у .con лежать лише шляхи до файлів.
  for (Road& road : level.roads) {
    const auto bytes = files.read(road.meshPath);
    if (!bytes) continue;
    if (auto geometry = loadRoadMesh(*bytes)) {
      road.geometry = std::move(*geometry);
      // Текстуру беремо за іменем шаблону з CompiledRoads.con.
      const auto texture = level.roadTextures.find(road.templateName);
      if (texture != level.roadTextures.end() && !road.geometry.ranges.empty()) {
        road.geometry.ranges[0].maps.push_back(texture->second);
      }
    }
  }

  if (!loadHeights(files, level, error)) return std::nullopt;
  return level;
}

std::optional<mesh::RenderMesh> loadRoadMesh(std::span<const std::byte> bytes,
                                             std::string* error) {
  auto fail = [error](const char* why) -> std::optional<mesh::RenderMesh> {
    if (error) *error = why;
    return std::nullopt;
  };

  constexpr std::size_t kHeaderBytes = 52;
  constexpr std::size_t kVertexStride = 32;
  if (bytes.size() < kHeaderBytes + 4) return fail("файл замалий для дороги");

  auto readU32 = [&](std::size_t at) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
  };
  auto readFloat = [&](std::size_t at) {
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
  };

  const std::uint32_t vertexCount = readU32(48);
  const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * kVertexStride;
  if (vertexCount == 0 || kHeaderBytes + vertexBytes + 4 > bytes.size()) {
    return fail("вершини дороги обірвано");
  }

  // Позиції у файлі відносні до start; зсув додає вже той, хто ставить
  // дорогу у світ, разом із absolutePosition.
  mesh::RenderMesh out;
  out.vertices.resize(vertexCount);
  for (std::uint32_t i = 0; i < vertexCount; ++i) {
    const std::size_t at = kHeaderBytes + static_cast<std::size_t>(i) * kVertexStride;
    mesh::Vertex& vertex = out.vertices[i];
    vertex.position = {readFloat(at), readFloat(at + 4), readFloat(at + 8)};
    // Дорога лежить на землі, тож нормаль угору — окремої в файлі немає.
    vertex.normal = {0.0f, 1.0f, 0.0f};
    vertex.uv[0] = readFloat(at + 12);
    vertex.uv[1] = readFloat(at + 16);
  }

  const std::size_t indexOffset = kHeaderBytes + vertexBytes;
  const std::uint32_t indexCount = readU32(indexOffset);
  if (indexOffset + 4 + static_cast<std::size_t>(indexCount) * 2 > bytes.size()) {
    return fail("індекси дороги обірвано");
  }

  out.indices.resize(indexCount);
  for (std::uint32_t i = 0; i < indexCount; ++i) {
    std::uint16_t index = 0;
    std::memcpy(&index, bytes.data() + indexOffset + 4 + i * 2, sizeof(index));
    if (index >= vertexCount) return fail("індекс дороги за межами буфера");
    out.indices[i] = index;
  }

  mesh::Aabb bounds{};
  for (std::size_t i = 0; i < out.vertices.size(); ++i) {
    const auto& p = out.vertices[i].position;
    if (i == 0) {
      bounds.min = bounds.max = p;
    } else {
      bounds.min = {std::min(bounds.min.x, p.x), std::min(bounds.min.y, p.y),
                    std::min(bounds.min.z, p.z)};
      bounds.max = {std::max(bounds.max.x, p.x), std::max(bounds.max.y, p.y),
                    std::max(bounds.max.z, p.z)};
    }
  }
  out.bounds = bounds;

  mesh::DrawRange range;
  range.indexCount = indexCount;
  out.ranges.push_back(std::move(range));
  return out;
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

      // Детейл-мапа: колормапа має лише ~2 тексели на метр, тому зблизька
      // терен без неї виглядає розмитим.
      if (!level.terrain.detailmapBase.empty()) {
        char detailName[64];
        std::snprintf(detailName, sizeof(detailName), "%02dx%02d_1.dds", column, row);
        const std::string candidate = level.terrain.detailmapBase + detailName;
        if (files.exists(candidate)) patch.detailmap = candidate;
      }

      mesh::DrawRange range;
      range.indexStart = 0;
      range.indexCount = static_cast<std::uint32_t>(patch.geometry.indices.size());
      range.maps.push_back(patch.colormap);
      if (!patch.lightmap.empty()) {
        range.maps.push_back(patch.lightmap);
        range.lightmapInSecondSlot = true;
      }
      if (!patch.detailmap.empty()) range.maps.push_back(patch.detailmap);
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

namespace obf2::level {

float Level::groundHeightAt(const Vec3f& position) const {
  if (heights.empty()) return 0.0f;

  const float half = halfExtent();
  const float gx = position.x / primary.scale.x + half;
  const float gz = position.z / primary.scale.z + half;

  const int x0 = static_cast<int>(std::floor(gx));
  const int z0 = static_cast<int>(std::floor(gz));
  const float tx = gx - static_cast<float>(x0);
  const float tz = gz - static_cast<float>(z0);

  const float h00 = heightAt(x0, z0);
  const float h10 = heightAt(x0 + 1, z0);
  const float h01 = heightAt(x0, z0 + 1);
  const float h11 = heightAt(x0 + 1, z0 + 1);

  const float top = h00 + (h10 - h00) * tx;
  const float bottom = h01 + (h11 - h01) * tx;
  return top + (bottom - top) * tz;
}

}  // namespace obf2::level
