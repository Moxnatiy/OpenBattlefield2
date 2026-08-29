// openbf2 — точка входу рушія.
//
//   openbf2 [--mod <шлях>] [--mesh <шлях у VFS>]   — один меш
//           [--object <ім'я шаблону>]              — зібрана техніка
//           [--level <ім'я рівня>]                 — рівень цілком
//           [--geom N] [--lod N] [--frames N] [--screenshot file.bmp]
//
// Дані гри читаються просто з архівів, як це робить fileManager у Refractor 2.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>

#include "obf2/core/math.h"
#include "obf2/core/path.h"
#include "obf2/core/platform.h"
#include "obf2/engine/engine.h"
#include "obf2/font/text.h"
#include "obf2/hud/render.h"
#include "obf2/game/controls.h"
#include "obf2/game/scene.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/level/gameplay.h"
#include "obf2/level/level.h"
#include "obf2/server/game_client.h"
#include <set>

#include "obf2/net/bf2_events.h"
#include "obf2/net/md5.h"
#include "obf2/net/bf2_protocol.h"
#include "obf2/net/udp.h"
#include "obf2/server/game_server.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/mesh/collision.h"
#include "obf2/mesh/skinning.h"
#include "obf2/server/collision_world.h"
#include "obf2/texture/dds.h"
#include "obf2/vfs/filesystem.h"

namespace {

struct Args {
  std::filesystem::path modDir = "Game Files/mods/bf2";
  std::string meshPath;  // порожньо -> звичайний запуск рушія
  std::string objectName;
  std::string levelName;
  int geometryIndex = -1;  // -1 = вибрати за кількістю lod-ів
  int lodIndex = 0;
  int frames = 0;  // 0 = крутитися, доки не закриють вікно
  std::string screenshot;
  std::string screen;  // menu | loading — для детермінованих знімків
  bool hosted = false; // --hosted: світ приходить від локального сервера
  std::optional<obf2::Vec3f> focus;  // куди дивиться камера
  float mouseX = -1.0f, mouseY = -1.0f;  // --mouse: поставити курсор для знімка
  bool verboseMenu = false;
  // --topdown: строго згори, +X праворуч, -Z вгору. Потрібно, щоб звіряти
  // орієнтацію світу з власною мінімапою рівня.
  bool topDown = false;
  std::string connectTo;      // --connect <хост[:порт]>: справжній сервер BF2
  // --probe: тільки розбір протоколу, без вікна. Без нього --connect
  // відкриває світ, як і належить клієнтові.
  bool probe = false;
  std::string connectPassword;
  std::string playerName = "OpenBF2";  // --name: під яким іменем заходимо
  std::string calibrate;               // --calibrate <файл>: зіставити номери шаблонів
  // --ordinal: номер рядка у файлах відбитків. Сервер обирає його при
  // завантаженні рівня; звідки його дізнається справжній клієнт — ще не
  // знайдено, тож поки задаємо руками.
  // -1 = взяти з блока, який присилає сервер.
  int ordinal = -1;
  int team = 1;                        // --team, --kit, --group: вибір при появі
  int kit = 0;
  int spawnGroup = 1;
  std::vector<std::string> animationPaths;  // --anim: можна кілька, вони змішуються
  std::string skeletonPath;    // --skeleton: .ske; типово скелет солдата
  int frame = 0;               // --frame: який кадр показати
  bool click = false;  // --click: одне натискання в позиції --mouse
  // --hud-screen <група>: показати екран, який зазвичай видно лише поки
  // тримають клавішу. Потрібно для знімків і для звірки очима.
  std::string hudScreenName;
  float distance = 0.0f;             // 0 = підібрати за габаритами
  // Гра зроблена під 4:3, і поки що ми тримаємося цього: 1600x1200 — це
  // рівно вдвічі більше за базові 800x600, тож HUD лягає без залишку.
  // Широкий екран буде окремою роботою.
  int width = 1600;
  int height = 1200;
};

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--mod" && i + 1 < argc) args.modDir = argv[++i];
    else if (flag == "--mesh" && i + 1 < argc) args.meshPath = argv[++i];
    else if (flag == "--object" && i + 1 < argc) args.objectName = argv[++i];
    else if (flag == "--level" && i + 1 < argc) args.levelName = argv[++i];
    else if (flag == "--geom" && i + 1 < argc) args.geometryIndex = std::atoi(argv[++i]);
    else if (flag == "--lod" && i + 1 < argc) args.lodIndex = std::atoi(argv[++i]);
    else if (flag == "--frames" && i + 1 < argc) args.frames = std::atoi(argv[++i]);
    else if (flag == "--screenshot" && i + 1 < argc) args.screenshot = argv[++i];
    else if (flag == "--screen" && i + 1 < argc) args.screen = argv[++i];
    else if (flag == "--hosted") args.hosted = true;
    else if (flag == "--verbose-menu") args.verboseMenu = true;
    else if (flag == "--topdown") args.topDown = true;
    else if (flag == "--connect" && i + 1 < argc) args.connectTo = argv[++i];
    else if (flag == "--probe") args.probe = true;
    else if (flag == "--width" && i + 1 < argc) args.width = std::atoi(argv[++i]);
    else if (flag == "--height" && i + 1 < argc) args.height = std::atoi(argv[++i]);
    else if (flag == "--hud-screen" && i + 1 < argc) args.hudScreenName = argv[++i];
    else if (flag == "--connect-password" && i + 1 < argc) args.connectPassword = argv[++i];
    else if (flag == "--name" && i + 1 < argc) args.playerName = argv[++i];
    else if (flag == "--calibrate" && i + 1 < argc) args.calibrate = argv[++i];
    else if (flag == "--ordinal" && i + 1 < argc) args.ordinal = std::atoi(argv[++i]);
    else if (flag == "--team" && i + 1 < argc) args.team = std::atoi(argv[++i]);
    else if (flag == "--kit" && i + 1 < argc) args.kit = std::atoi(argv[++i]);
    else if (flag == "--group" && i + 1 < argc) args.spawnGroup = std::atoi(argv[++i]);
    else if (flag == "--anim" && i + 1 < argc) args.animationPaths.emplace_back(argv[++i]);
    else if (flag == "--skeleton" && i + 1 < argc) args.skeletonPath = argv[++i];
    else if (flag == "--frame" && i + 1 < argc) args.frame = std::atoi(argv[++i]);
    else if (flag == "--click") args.click = true;
    else if (flag == "--mouse" && i + 2 < argc) {
      args.mouseX = static_cast<float>(std::atof(argv[++i]));
      args.mouseY = static_cast<float>(std::atof(argv[++i]));
    }
    else if (flag == "--dist" && i + 1 < argc) args.distance = static_cast<float>(std::atof(argv[++i]));
    else if (flag == "--focus" && i + 1 < argc) {
      // Формат як у грі: x/y/z
      obf2::con::Command command;
      command.args.emplace_back(argv[++i]);
      if (const auto point = command.argVec3(0)) {
        args.focus = obf2::Vec3f{point->x, point->y, point->z};
      }
    }
  }
  return args;
}

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// У .bundledmesh техніки кілька geom: вигляд із кабіни, зовнішній вигляд і
// уламки. Явного маркера у файлі немає, але є надійна ознака: **вигляд із
// кабіни має рівно один lod** — гравець завжди поруч, тож ланцюжок деталізації
// йому не потрібен, тоді як зовнішній вигляд має 3-4 рівні.
std::size_t pickGeometry(const obf2::mesh::Mesh& mesh, std::size_t lodIndex) {
  std::size_t best = 0;
  std::size_t bestLods = 0;
  std::size_t bestTriangles = 0;

  for (std::size_t g = 0; g < mesh.geometries.size(); ++g) {
    const auto& lods = mesh.geometries[g].lods;
    if (lodIndex >= lods.size()) continue;

    std::size_t triangles = 0;
    for (const auto& material : lods[lodIndex].materials) triangles += material.indexCount / 3;

    if (lods.size() > bestLods || (lods.size() == bestLods && triangles > bestTriangles)) {
      bestLods = lods.size();
      bestTriangles = triangles;
      best = g;
    }
  }
  return best;
}

std::optional<obf2::mesh::RenderMesh> loadMesh(obf2::FileSystem& files, const std::string& path,
                                               int geometryOverride, int lodIndex, bool verbose) {
  const std::string normalized = obf2::normalizeAssetPath(path);
  const auto kind = obf2::mesh::kindFromExtension(obf2::assetExtension(normalized));
  if (!kind) return std::nullopt;

  const auto bytes = files.read(normalized);
  if (!bytes) {
    if (verbose) std::fprintf(stderr, "меш не знайдено у VFS: %s\n", normalized.c_str());
    return std::nullopt;
  }

  std::string error;
  const auto parsed = obf2::mesh::load(*bytes, *kind, &error);
  if (!parsed) {
    if (verbose) std::fprintf(stderr, "не розібрано %s: %s\n", normalized.c_str(), error.c_str());
    return std::nullopt;
  }

  const std::size_t lod = static_cast<std::size_t>(std::max(0, lodIndex));
  const std::size_t geometry = geometryOverride >= 0 ? static_cast<std::size_t>(geometryOverride)
                                                     : pickGeometry(*parsed, lod);

  auto render = obf2::mesh::extract(*parsed, geometry, lod, &error);
  if (!render) {
    if (verbose) std::fprintf(stderr, "не розпаковано %s: %s\n", normalized.c_str(), error.c_str());
    return std::nullopt;
  }

  if (verbose) {
    std::printf("меш: %s\n  версія %u, geom %zu/%zu, lod %zu, вершин %zu, трикутників %zu, "
                "матеріалів %zu\n",
                normalized.c_str(), parsed->header.version, geometry, parsed->geometries.size(),
                lod, render->vertices.size(), render->indices.size() / 3, render->ranges.size());
  }
  return render;
}

// Ім'я геометрії з ObjectTemplate — це не шлях. Файл лежить поруч із .con,
// у підтеці meshes: objects/vehicles/land/apc_btr90/meshes/apc_btr90.bundledmesh.
std::string resolveGeometryPath(obf2::FileSystem& files, const std::string& templateFile,
                                const std::string& geometryName) {
  if (geometryName.empty()) return {};
  const std::string_view dir = obf2::assetParentDir(templateFile);
  for (const char* extension : {".staticmesh", ".bundledmesh", ".skinnedmesh"}) {
    for (const char* subdirectory : {"meshes/", ""}) {
      const std::string candidate =
          obf2::joinAssetPath(dir, std::string(subdirectory) + geometryName + extension);
      if (files.exists(candidate)) return candidate;
    }
  }
  return {};
}

// Меш зіткнень лежить поруч із видимим, у тій самій підтеці meshes.
std::string resolveCollisionPath(obf2::FileSystem& files, const std::string& templateFile,
                                 const std::string& name) {
  if (name.empty()) return {};
  const std::string_view dir = obf2::assetParentDir(templateFile);
  for (const char* subdirectory : {"meshes/", ""}) {
    const std::string candidate =
        obf2::joinAssetPath(dir, std::string(subdirectory) + name + ".collisionmesh");
    if (files.exists(candidate)) return candidate;
  }
  return {};
}

// Проганяє всі .con/.tweak гри через інтерпретатор і збирає реєстр шаблонів.
obf2::game::Registry buildRegistry(obf2::FileSystem& files) {
  obf2::game::Registry registry;
  std::vector<std::string> configs;
  for (auto& path : files.list()) {
    const std::string_view extension = obf2::assetExtension(path);
    if (extension == "con" || extension == "tweak") configs.push_back(std::move(path));
  }
  std::sort(configs.begin(), configs.end());
  configs.erase(std::unique(configs.begin(), configs.end()), configs.end());

  for (const auto& path : configs) {
    obf2::con::Interpreter interpreter(
        files, [&](const obf2::con::Command& command) { registry.feed(command); });
    interpreter.runFile(path);
  }
  return registry;
}

// Шаблон -> зібрана геометрія: дерево нащадків плюс розстановка частин
// BundledMesh за geometryPart.
// Додає до цілі меш, перетворений матрицею. Індекси зсуваються на вже
// наявні вершини, діапазони матеріалів — на вже наявні індекси.
void appendMesh(obf2::mesh::RenderMesh& target, const obf2::mesh::RenderMesh& source,
                const obf2::Mat4& transform) {
  const auto vertexBase = static_cast<std::uint32_t>(target.vertices.size());
  const auto indexBase = static_cast<std::uint32_t>(target.indices.size());

  for (const auto& vertex : source.vertices) {
    obf2::mesh::Vertex moved = vertex;
    const obf2::Vec3f at = transformPoint(
        transform, obf2::Vec3f{vertex.position.x, vertex.position.y, vertex.position.z});
    moved.position = {at.x, at.y, at.z};
    // Нормалі повертаємо без переносу: масштабу в цих трансформах немає,
    // тож звичайного множення на верхній блок 3x3 досить.
    const obf2::Vec3f n = transformDirection(
        transform, obf2::Vec3f{vertex.normal.x, vertex.normal.y, vertex.normal.z});
    moved.normal = {n.x, n.y, n.z};
    target.vertices.push_back(moved);
  }
  for (const auto index : source.indices) target.indices.push_back(index + vertexBase);
  for (auto range : source.ranges) {
    range.indexStart += indexBase;
    target.ranges.push_back(std::move(range));
  }
}

// Шаблон -> зібрана геометрія: дерево нащадків плюс розстановка частин
// BundledMesh за geometryPart.
//
// Якщо в кореня власного меша немає, збираємо об'єкт із мешів нащадків.
// Так влаштовані, скажімо, контрольні точки: сам шаблон без геометрії, а
// прапор приходить через `ObjectTemplate.addTemplate flagpole`.
std::optional<obf2::mesh::RenderMesh> buildObjectMesh(obf2::FileSystem& files,
                                                      const obf2::game::Registry& registry,
                                                      const std::string& templateName,
                                                      const Args& args, bool verbose) {
  const auto* root = registry.find(templateName);
  if (root == nullptr) return std::nullopt;

  const auto instance = obf2::game::flattenObject(registry, templateName);
  if (!instance) return std::nullopt;

  if (instance->geometryName.empty()) {
    obf2::mesh::RenderMesh merged;
    int added = 0;
    for (const auto& part : instance->parts) {
      if (part.geometryName.empty()) continue;
      const std::string path = resolveGeometryPath(files, part.file, part.geometryName);
      if (path.empty()) continue;
      const auto piece = loadMesh(files, path, args.geometryIndex, args.lodIndex, false);
      if (!piece) continue;
      appendMesh(merged, *piece, part.transform);
      ++added;
    }
    if (added == 0) return std::nullopt;
    // Межі рахуємо самі: меші прийшли з різних файлів, і кожен приніс
    // власні, у своїх координатах.
    if (!merged.vertices.empty()) {
      merged.bounds.min = merged.bounds.max = merged.vertices.front().position;
      for (const auto& vertex : merged.vertices) {
        merged.bounds.min.x = std::min(merged.bounds.min.x, vertex.position.x);
        merged.bounds.min.y = std::min(merged.bounds.min.y, vertex.position.y);
        merged.bounds.min.z = std::min(merged.bounds.min.z, vertex.position.z);
        merged.bounds.max.x = std::max(merged.bounds.max.x, vertex.position.x);
        merged.bounds.max.y = std::max(merged.bounds.max.y, vertex.position.y);
        merged.bounds.max.z = std::max(merged.bounds.max.z, vertex.position.z);
      }
    }
    if (verbose) {
      std::printf("  корінь без геометрії: зібрано з %d мешів нащадків, вершин %zu\n", added,
                  merged.vertices.size());
    }
    return merged;
  }

  const std::string path = resolveGeometryPath(files, root->file, instance->geometryName);
  if (path.empty()) return std::nullopt;

  auto render = loadMesh(files, path, args.geometryIndex, args.lodIndex, verbose);
  if (!render) return std::nullopt;

  const auto transforms = obf2::game::partTransformMap(*instance);
  const std::size_t moved = obf2::game::applyPartTransforms(*render, transforms);
  if (verbose) {
    std::printf("  вузлів у дереві: %zu, глибина: %d, циклів: %d\n"
                "  частин із трансформом: %zu, переставлено вершин: %zu з %zu\n",
                instance->parts.size(), instance->maxDepth, instance->cycles, transforms.size(),
                moved, render->vertices.size());
  }
  return render;
}

// Прямокутник на весь екран у координатах NDC: з одиничною матрицею
// вершинний шейдер лишає їх як є.
//
// Нормаль ставимо рівно вздовж джерела світла з фрагментного шейдера —
// тоді півламбертів множник дорівнює одиниці й картинка виходить без
// затемнення. Тимчасовий трюк: щойно з'явиться окремий пайплайн для
// інтерфейсу, він стане непотрібним.
obf2::mesh::RenderMesh buildScreenQuad(const std::string& imagePath) {
  const obf2::Vec3f light = obf2::normalize(obf2::Vec3f{0.4f, 0.9f, 0.35f});
  const obf2::mesh::Vec3 normal{light.x, light.y, light.z};

  obf2::mesh::RenderMesh quad;
  quad.vertices = {
      obf2::mesh::Vertex{{-1.0f, -1.0f, 0.0f}, normal, {0.0f, 1.0f}},
      obf2::mesh::Vertex{{1.0f, -1.0f, 0.0f}, normal, {1.0f, 1.0f}},
      obf2::mesh::Vertex{{-1.0f, 1.0f, 0.0f}, normal, {0.0f, 0.0f}},
      obf2::mesh::Vertex{{1.0f, 1.0f, 0.0f}, normal, {1.0f, 0.0f}},
  };
  quad.indices = {0, 1, 2, 2, 1, 3};

  obf2::mesh::DrawRange range;
  range.indexCount = static_cast<std::uint32_t>(quad.indices.size());
  if (!imagePath.empty()) range.maps.push_back(imagePath);
  quad.ranges.push_back(std::move(range));
  return quad;
}

// Шрифт: пара .dif (метрики) + .dds (атлас) з Fonts_client.zip.
struct LoadedFont {
  obf2::font::Font font;
  std::string atlasPath;
  bool valid = false;
};

LoadedFont loadFont(obf2::FileSystem& files, const std::string& base) {
  LoadedFont out;
  const auto metrics = files.read(base + ".dif");
  if (!metrics) return out;

  const std::string text(reinterpret_cast<const char*>(metrics->data()), metrics->size());
  std::string error;
  auto parsed = obf2::font::parseDif(text, &error);
  if (!parsed) {
    std::fprintf(stderr, "шрифт %s: %s\n", base.c_str(), error.c_str());
    return out;
  }

  out.font = std::move(*parsed);
  out.atlasPath = base + ".dds";
  out.valid = files.exists(out.atlasPath);
  if (!out.valid) std::fprintf(stderr, "немає атласа шрифту: %s\n", out.atlasPath.c_str());
  return out;
}

// Тло головного меню. У грі меню — це Flash, який рушій крутить своїм
// програвачем; ассети до нього лежать звичайними PNG, і саме їх ми беремо.
std::string findMenuBackground(obf2::FileSystem& files) {
  for (const char* candidate : {
           "menu/external/flashmenu/images/background/background_2.png",
           "menu/external/flashmenu/images/background/background_3.png",
           "menu/external/flashmenu/images/background/background_1.png",
       }) {
    if (files.exists(candidate)) return candidate;
  }
  return {};
}

}  // namespace

// Одна сесія: меню або гра. Повертається код виходу; якщо з меню обрали
// рівень, його назва лягає в nextLevel і головний цикл заводить сесію
// наново — вже у грі.
// Усе, що ми знаємо про рівень: назва шаблона й де він стоїть.
// Сервер шле об'єкти без назв, лише номерами, тож впізнаємо їх за місцем.
struct KnownObject {
  std::string name;
  obf2::Vec3f position;
};

std::vector<KnownObject> buildKnownObjects(obf2::FileSystem& files, const std::string& levelName,
                                           std::string* error) {
  std::vector<KnownObject> known;
  if (const auto gameplay =
          obf2::level::loadGameplayObjects(files, levelName, "gpm_cq", 16, error)) {
    for (const auto& point : gameplay->controlPoints) {
      known.push_back({point.templateName, point.position});
    }
    for (const auto& spawner : gameplay->spawners) {
      known.push_back({spawner.templateName, spawner.position});
      // Спавнер видає різну техніку залежно від команди — усі варіанти
      // стоять на тому самому місці.
      for (const auto& [team, name] : spawner.templateByTeam) {
        (void)team;
        known.push_back({name, spawner.position});
      }
    }
  }
  // Статику теж: сервер шле й руйнівні речі на кшталт бочок і цистерн.
  if (const auto loaded = obf2::level::loadLevel(files, levelName, error)) {
    for (const auto& object : loaded->objects) {
      known.push_back({object.templateName, object.position});
    }
  }
  return known;
}

// Хто з відомих об'єктів стоїть на цьому місці. Допуск навмисно вузький:
// обидва боки беруть позицію з тих самих даних, тож збіг має бути точним,
// а ширший допуск почав би вигадувати відповідності.
const KnownObject* nearestKnown(const std::vector<KnownObject>& known, const obf2::Vec3f& at,
                               float tolerance = 2.0f) {
  const KnownObject* best = nullptr;
  float bestDistance = 0.0f;
  for (const auto& candidate : known) {
    const float d = length(candidate.position - at);
    if (!best || d < bestDistance) {
      best = &candidate;
      bestDistance = d;
    }
  }
  return best && bestDistance < tolerance ? best : nullptr;
}

// На якому кроці об'єкт губиться дорогою до екрана.
enum class DrawStage {
  Drawn,            // дійшов: геометрію зібрано
  NoTemplate,       // шаблона немає в реєстрі
  NoTree,           // дерево нащадків не зібралося
  NoGeometryName,   // ні в корені, ні в нащадках немає геометрії
  GeometryInChild,  // геометрія є, але в нащадка — ми беремо лише кореневу
  NoGeometryFile,   // назва є, а файла не знайшли
  NoMesh,           // файл є, а меш не прочитався
};

std::string_view drawStageName(DrawStage stage) {
  switch (stage) {
    case DrawStage::Drawn: return "намальовано";
    case DrawStage::NoTemplate: return "немає шаблона";
    case DrawStage::NoTree: return "не зібралося дерево";
    case DrawStage::NoGeometryName: return "геометрії немає ніде";
    case DrawStage::GeometryInChild: return "зібрано з нащадків";
    case DrawStage::NoGeometryFile: return "не знайдено файл";
    case DrawStage::NoMesh: return "меш не прочитався";
  }
  return "?";
}

// Проходить той самий шлях, що й `buildObjectMesh`, але каже, де саме
// зупинився. Без цього «об'єкта не видно» нічого не пояснює.
DrawStage checkDrawable(obf2::FileSystem& files, const obf2::game::Registry& registry,
                        const std::string& templateName, const Args& args) {
  const auto* root = registry.find(templateName);
  if (root == nullptr) return DrawStage::NoTemplate;

  const auto instance = obf2::game::flattenObject(registry, templateName);
  if (!instance) return DrawStage::NoTree;
  if (instance->geometryName.empty()) {
    // Дерево може нести геометрію не в корені: контрольна точка сама без
    // меша, а прапор приходить від `addTemplate flagpole`. Такі об'єкти
    // збираються з мешів нащадків.
    // Не просто шукаємо файл, а справді збираємо меш: інакше «зібрано»
    // означало б лише «схоже, мало б зібратися».
    const auto merged = buildObjectMesh(files, registry, templateName, args, false);
    if (merged && !merged->vertices.empty()) return DrawStage::GeometryInChild;
    return DrawStage::NoGeometryName;
  }

  const std::string path = resolveGeometryPath(files, root->file, instance->geometryName);
  if (path.empty()) return DrawStage::NoGeometryFile;

  if (!loadMesh(files, path, args.geometryIndex, args.lodIndex, false)) return DrawStage::NoMesh;
  return DrawStage::Drawn;
}

// Зіставляє номери шаблонів із назвами.
//
// Сервер шле об'єкти номером шаблона, а номер — це порядок створення
// (`ObjectTemplateManager::createTemplate`), тож із самого числа назви не
// дістати. Але позиції збігаються: рівень ми читаємо самі й знаємо, що і
// де стоїть, а сервер каже номери для тих самих місць. Звідси й таблиця.
//
// Файл із пакетами робить `tools/linuxded/capture.py --stage world --out`.
int runCalibrate(const Args& args, obf2::FileSystem& files) {
  if (args.levelName.empty()) {
    std::fprintf(stderr, "вкажіть рівень: --level <назва>\n");
    return 1;
  }
  std::string error;
  if (!obf2::level::mountLevel(files, args.modDir, args.levelName, &error)) {
    std::fprintf(stderr, "рівень не змонтовано: %s\n", error.c_str());
    return 1;
  }

  auto known = buildKnownObjects(files, args.levelName, &error);
  std::printf("рівень %s: відомих об'єктів %zu\n", args.levelName.c_str(), known.size());

  const auto packets = obf2::net::bf2::loadCapture(args.calibrate);
  if (packets.empty()) {
    std::fprintf(stderr, "у %s немає пакетів\n", args.calibrate.c_str());
    return 1;
  }

  std::map<std::uint32_t, std::string> mapping;
  std::map<std::uint32_t, obf2::Vec3f> unmatched;
  int fromServer = 0, matched = 0;
  for (const auto& packet : packets) {
    for (const auto& event : obf2::net::bf2::readEvents(packet)) {
      if (!event.object || !event.object->position) continue;
      ++fromServer;
      const auto& at = *event.object->position;

      const KnownObject* best = nearestKnown(known, at);
      if (best) {
        ++matched;
        mapping[event.object->templateId] = best->name;
      } else {
        unmatched[event.object->templateId] = at;
      }
    }
  }

  std::printf("об'єктів від сервера: %d, зіставлено: %d\n", fromServer, matched);
  for (const auto& [id, name] : mapping) {
    std::printf("  %6u  %s\n", id, name.c_str());
  }
  if (!unmatched.empty()) {
    std::printf("не впізнано %zu номерів:\n", unmatched.size());
    for (const auto& [id, at] : unmatched) {
      std::printf("  %6u  @ %.1f %.1f %.1f\n", id, at.x, at.y, at.z);
    }
  }
  return 0;
}

// Три хеші для перевірки вмісту.
//
// Перший — той, що сервер рахує сам при старті; порахувати його ми поки
// не вміємо, тож він передається прапорцем `--misc-hash`. Другий і
// третій читаються з файлів відбитків: архіви мода й сам рівень.
struct ContentHashes {
  std::array<std::byte, 16> misc{};
  std::array<std::byte, 16> archives{};
  std::array<std::byte, 16> level{};
};

std::optional<ContentHashes> contentHashes(obf2::FileSystem& files, const std::string& levelName,
                                           int ordinal) {
  ContentHashes out;


  // Перший хеш: MD5 по чотирьох файлах мода, саме в такому порядку.
  // Імена взято з `ChecksumContext::runMiscChecksum`.
  obf2::net::Md5 misc;
  for (const char* name : {"ClientArchives.con", "ServerArchives.con", "Init.con",
                           "GameLogicInit.con"}) {
    const auto data = files.read(name);
    if (!data) return std::nullopt;
    misc.update(*data);
  }
  out.misc = misc.finish();

  const auto readText = [&files](const std::string& path) -> std::string {
    const auto data = files.read(path);
    if (!data) return {};
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
  };

  const std::string archivesText = readText("std_archive.md5");
  const std::string levelText = readText("levels/" + levelName + "/archive.md5");
  const auto archives = obf2::net::bf2::readFingerprint(archivesText, ordinal);
  const auto level = obf2::net::bf2::readFingerprint(levelText, ordinal);
  if (!archives || !level) return std::nullopt;
  out.archives = *archives;
  out.level = *level;
  return out;
}

// Зв'язок зі справжнім сервером BF2, який живе разом із вікном: одне
// й те саме з'єднання спершу доводить рукостискання до кінця, а потім
// крутиться в кадровому циклі. Саме так робить і оригінал — сесія не
// закінчується на тому, що сервер нас прийняв.
struct RemoteWorld {
  RemoteWorld(const Args& a, obf2::FileSystem& f) : args(a), files(f) {}

  const Args& args;
  obf2::FileSystem& files;
  std::unique_ptr<obf2::net::UdpSocket> socket;
  std::uint8_t id = 0;


  // Далі тримаємо зв'язок: сервер шле пінги, і без відповіді він нас
  // відключить. Заразом рахуємо, що саме приходить.

  // Рівень беремо від сервера, як в оригіналі: він шле його блоком даних
  // типу 5 одразу після реєстрації. Далі стежимо, що з отриманих
  // об'єктів доходить до екрана — сервер каже номер і місце, місце дає
  // назву шаблона, а потім той самий шлях, що й у грі.
  std::vector<KnownObject> known;
  std::map<std::uint16_t, obf2::Vec3f> objects;  // номер -> де стоїть
  obf2::game::Registry registry;
  std::map<std::string, DrawStage> checked;
  std::map<DrawStage, int> stageCounts;
  obf2::net::bf2::DataBlockAssembler blocks;
  bool levelReady = false;

  // Кроки після рівня йдуть із паузами: мережеві події виконуються
  // наступним тактом, а перевірка вмісту — одразу, тож складати їх в
  // один пакет не можна.
  enum class Step { Level, Content, Database, Team, Kit, Group, Done };
  Step step = Step::Level;
  std::chrono::steady_clock::time_point lastStep = std::chrono::steady_clock::now();
  std::string levelName;
  int blockOrdinal = 0;
  int pings = 0, dataPackets = 0, other = 0, challenges = 0;
  int eventCount = 0, objectCount = 0;
  std::uint8_t lastServerSequence = 0;
  int ghostPackets = 0, ghostRecords = 0;
  std::set<std::uint16_t> ghostObjects;
  std::size_t dataBytes = 0;
  std::uint8_t sequence = 0;
  std::uint8_t batch = 0;
  bool answered = false;


  // Рукостискання: запит, відповідь сервера, підтвердження.
  bool connect() {
    std::string host = args.connectTo;
    std::uint16_t port = 16567;  // типовий ігровий порт BF2
    if (const std::size_t colon = host.rfind(':'); colon != std::string::npos) {
      port = static_cast<std::uint16_t>(std::atoi(host.c_str() + colon + 1));
      host = host.substr(0, colon);
    }

    std::string error;
    socket = obf2::net::UdpSocket::connect(host, port, &error);
    if (!socket) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return false;
    }
    std::printf("під'єднання: %s\n", socket->describe().c_str());

    obf2::net::bf2::ConnectRequest request;
    request.password = args.connectPassword;
    // Сервер звіряє теку мода з власною (`GSModDirectory`), і при розбіжності
    // сам присилає свою — тому помилку видно одразу.
    request.modDirectory = "mods/bf2";

    if (!socket->send(obf2::net::bf2::writeConnectRequest(request))) {
      std::fprintf(stderr, "не вдалося надіслати запит\n");
      return false;
    }
    std::printf("  надіслано запит: протокол %#x, версія %#x\n", request.magic, request.version);

    const auto reply = socket->receive(2000);
    if (!reply) {
      std::fprintf(stderr, "  сервер мовчить\n");
      return false;
    }

    const auto packet = obf2::net::bf2::readPacket(*reply);
    if (!packet) {
      std::fprintf(stderr, "  прийшло %zu байтів, але це не пакет\n", reply->size());
      return false;
    }

    if (packet->denied) {
      std::printf("  відмова: %s\n",
                  std::string(obf2::net::bf2::denyReasonName(packet->denied->reason)).c_str());
      if (!packet->denied->modDirectory.empty()) {
        std::printf("  сервер хоче теку %s\n", packet->denied->modDirectory.c_str());
      }
      return false;
    }

    if (!packet->accept) {
      std::printf("  несподіваний пакет типу %d\n", static_cast<int>(packet->kind));
      return false;
    }

    std::printf("  ПРИЙНЯТО: з'єднання %d, час сервера %u мс, PunkBuster %s\n",
                packet->accept->connectionId, packet->accept->serverTime,
                packet->accept->punkBuster ? "увімкнено" : "вимкнено");

    // Рушій чекає підтвердження — лише після нього з'єднання стає робочим
    // (у `NetServer::_update` стан 1 -> 2).
    socket->send(obf2::net::bf2::writeShortPacket(obf2::net::bf2::PacketKind::ConnectAcceptAck,
                                                  packet->accept->connectionId));
    std::printf("  надіслано підтвердження\n");
    id = packet->accept->connectionId;
    return true;
  }

  // Один оберт: рухаємо ланцюжок появи і читаємо, що прийшло. Чекати
  // довго можна лише поза кадром — у кадрі це були б завмирання.
  void pump(int timeoutMs) {
      // Наступний крок ланцюжка — раз на кілька обертів, щоб сервер
      // устигав виконати попередній.
      // Пауза між кроками — за годинником, а не за обертами циклу:
      // мережеві події виконуються наступним тактом сервера, і якщо
      // надіслати наступний крок раніше, він застане старий стан.
      const auto now = std::chrono::steady_clock::now();
      if (levelReady && step != Step::Done &&
          now - lastStep >= std::chrono::seconds(3)) {
        lastStep = now;
        obf2::net::bf2::ExtendedHeader next;
        next.sequence = sequence++ & 0x3F;
        next.ack = lastServerSequence;
        next.ackBits = 0xFFFFFFFFu;

        switch (step) {
          case Step::Level:
            socket->send(obf2::net::bf2::writePostRemoteEvent(
                id, next, batch++, obf2::net::bf2::kNetworkCategory,
                obf2::net::bf2::kNetLoadComplete));
            std::printf("  крок: рівень завантажено\n");
            step = Step::Content;
            break;
          case Step::Content: {
            const auto hashes =
              contentHashes(files, levelName, args.ordinal < 0 ? blockOrdinal : args.ordinal);
            if (!hashes) {
              std::printf("  перевірку вмісту пропущено: %s\n", "немає відбитків");
              step = Step::Team;
              break;
            }
            const auto packet = obf2::net::bf2::writeContentCheckEvent(
                id, next, batch++, hashes->misc, hashes->archives, hashes->level);
            {
              std::string hex;
              for (std::size_t k = 0; k < std::min<std::size_t>(packet.size(), 20); ++k) {
                char pair[4];
                std::snprintf(pair, sizeof(pair), "%02x ", std::to_integer<int>(packet[k]));
                hex += pair;
              }
              std::printf("  пакет перевірки (%zu б): %s\n", packet.size(), hex.c_str());
            }
            socket->send(packet);
            const auto show = [](const std::array<std::byte, 16>& hash) {
              std::string out;
              for (const auto byte : hash) {
                char pair[3];
                std::snprintf(pair, sizeof(pair), "%02x", std::to_integer<int>(byte));
                out += pair;
              }
              return out;
            };
            std::printf("  крок: перевірка вмісту\n    %s\n    %s\n    %s\n",
                        show(hashes->misc).c_str(), show(hashes->archives).c_str(),
                        show(hashes->level).c_str());
            step = Step::Database;
            break;
          }
          case Step::Database:
            socket->send(obf2::net::bf2::writePostRemoteEvent(
                id, next, batch++, obf2::net::bf2::kNetworkCategory,
                obf2::net::bf2::kNetDatabaseComplete));
            std::printf("  крок: база гравців отримана\n");
            step = Step::Team;
            break;
          case Step::Team:
            socket->send(obf2::net::bf2::writePostRemoteEvent(
                id, next, batch++, obf2::net::bf2::kNetworkCategory,
                obf2::net::bf2::kNetSelectTeam, args.team));
            std::printf("  крок: команда %d\n", args.team);
            step = Step::Kit;
            break;
          case Step::Kit:
            socket->send(obf2::net::bf2::writePostRemoteEvent(
                id, next, batch++, obf2::net::bf2::kNetworkCategory,
                obf2::net::bf2::kNetSelectKit, args.kit));
            std::printf("  крок: набір %d\n", args.kit);
            step = Step::Group;
            break;
          case Step::Group:
            socket->send(obf2::net::bf2::writePostRemoteEvent(
                id, next, batch++, obf2::net::bf2::kNetworkCategory,
                obf2::net::bf2::kNetSelectSpawnGroup, args.spawnGroup));
            std::printf("  крок: місце появи %d\n", args.spawnGroup);
            step = Step::Done;
            break;
          case Step::Done: break;
        }
      }

      const auto more = socket->receive(timeoutMs);
      if (!more) return;
      const auto parsed = obf2::net::bf2::readPacket(*more);
      if (!parsed) return;
      if (parsed->extended) lastServerSequence = parsed->extended->sequence;

      switch (parsed->kind) {
        case obf2::net::bf2::PacketKind::PingRequest: {
          ++pings;
          obf2::net::bf2::ExtendedHeader header;
          header.sequence = sequence++ & 0x3F;
          if (parsed->extended) header.ack = parsed->extended->sequence;
          header.ackBits = 0xFFFFFFFFu;
          socket->send(obf2::net::bf2::writePingResponse(id, header,
                                                         parsed->pingTime.value_or(0)));
          break;
        }
        case obf2::net::bf2::PacketKind::Data: {
          ++dataPackets;
          dataBytes += more->size();

          if (const auto ghost = obf2::net::bf2::readGhostHeader(*more)) {
            ++ghostPackets;
            if (ghostPackets <= 3) {
              std::printf("  привиди: час %u, записів %u%s\n", ghost->time, ghost->records,
                          ghost->controlObjectState ? ", є стан керованого об'єкта" : "");
            }
            for (const auto& record : obf2::net::bf2::readGhostRecords(*more)) {
              ++ghostRecords;
              ghostObjects.insert(record.networkId);
            }
          }

          // Розбираємо всі події з пакета: за таблицею розмірів кожну
          // можна пропустити рівно на її довжину, тож незнайомі типи не
          // збивають розбір наступних.
          for (const auto& event : obf2::net::bf2::readEvents(*more)) {
            ++eventCount;
            if (event.block) {
              const auto done = blocks.feed(*event.block);
              if (done && done->first == obf2::net::bf2::kMapInfoBlock && !levelReady) {
                if (const auto info = obf2::net::bf2::parseMapInfo(done->second)) {
                  std::printf("  сервер грає %s, режим %s, розмір %d, перше число %u\n",
                              info->levelName.c_str(), info->gameMode.c_str(), info->size,
                              info->first);
                  // Номер виклику беремо з блока, коли його не задали
                  // руками: це єдине число, яке сервер тут присилає.
                  if (args.ordinal < 0) blockOrdinal = static_cast<int>(info->first);
                  std::string levelError;
                  if (!obf2::level::mountLevel(files, args.modDir, info->levelName, &levelError)) {
                    std::printf("  рівень не змонтовано: %s\n", levelError.c_str());
                  } else {
                    known = buildKnownObjects(files, info->levelName, &levelError);
                    registry = buildRegistry(files);
                    std::printf("  рівень прочитано: відомих об'єктів %zu\n", known.size());
                  }
                  levelReady = true;
                  levelName = info->levelName;
                }
              }
              continue;
            }
            if (event.object) {
              ++objectCount;
              if (!event.object->position) continue;
              const auto& at = *event.object->position;
              // Місця, які сервер нам назвав. З них беремо, звідки
              // дивитися: свого солдата ми ще не знаємо, а от прапори
              // сервер присилає одразу — і саме біля них гравець з'являється.
              objects[event.object->networkId] = at;
              if (known.empty()) {
                if (objectCount <= 3) {
                  std::printf("  об'єкт: шаблон %u, номер %u, позиція %.1f %.1f %.1f\n",
                              event.object->templateId, event.object->networkId, at.x, at.y, at.z);
                }
                continue;
              }
              const KnownObject* match = nearestKnown(known, at);
              if (match == nullptr) {
                std::printf("  не впізнано: шаблон %u @ %.1f %.1f %.1f\n",
                            event.object->templateId, at.x, at.y, at.z);
                continue;
              }

              auto found = checked.find(match->name);
              if (found == checked.end()) {
                const DrawStage stage = checkDrawable(files, registry, match->name, args);
                found = checked.emplace(match->name, stage).first;
                ++stageCounts[stage];
              }
              if (found->second != DrawStage::Drawn) {
                std::printf("  НЕ ВИДНО: %-44s %s\n", match->name.c_str(),
                            std::string(drawStageName(found->second)).c_str());
              }
            }
            if (event.player) {
              std::printf("  гравець: %s (номер %u, команда %u)\n",
                          event.player->name.c_str(), event.player->id, event.player->team);
            }
          }
          if (parsed->challenge) {
            ++challenges;
            if (!answered) {
              std::printf("  подія-виклик: %s, мод %s\n", parsed->challenge->challenge.c_str(),
                          parsed->challenge->modDirectory.c_str());

              obf2::net::bf2::ExtendedHeader header;
              header.sequence = sequence++ & 0x3F;
              if (parsed->extended) header.ack = parsed->extended->sequence;
              // Одиниці в масці означають «усе попереднє дійшло». Без цього
              // сервер вважає подію непідтвердженою й шле її знову й знову.
              header.ackBits = 0xFFFFFFFFu;
              socket->send(obf2::net::bf2::writeChallengeResponse(id, header, batch++));
              std::printf("  надіслано відповідь на виклик\n");
              answered = true;

              // Далі рушій чекає на блок із відомостями про клієнта: без
              // нього гравця не існує. Блок їде подіями — спершу заголовок
              // із типом і розміром, потім шматки.
              obf2::net::bf2::ClientInfo info;
              info.name = args.playerName;
              info.nameHash = obf2::net::bf2::clientInfoNameHash(info.name);
              const auto blob = obf2::net::bf2::buildClientInfo(info);

              const auto nextHeader = [&]() {
                obf2::net::bf2::ExtendedHeader next;
                next.sequence = sequence++ & 0x3F;
                if (parsed->extended) next.ack = parsed->extended->sequence;
                next.ackBits = 0xFFFFFFFFu;
                return next;
              };

              socket->send(obf2::net::bf2::writeDataBlockHeader(
                  id, nextHeader(), batch++, obf2::net::bf2::kClientInfoBlock,
                  static_cast<std::uint32_t>(blob.size())));
              for (std::size_t at = 0; at < blob.size(); at += 200) {
                const auto count = std::min<std::size_t>(200, blob.size() - at);
                socket->send(obf2::net::bf2::writeDataBlockChunk(
                    id, nextHeader(), batch++,
                    std::span<const std::byte>(blob.data() + at, count)));
              }
              std::printf("  надіслано ClientInfo: ім'я %s, %zu байтів\n",
                          info.name.c_str(), blob.size());

            }
          }
          break;
        }
        default:
          ++other;
          if (other <= 4) {
            std::printf("  інший пакет: тип %d\n", static_cast<int>(parsed->kind));
          }
          break;
      }
  }

  void report() {

    std::printf("  за 30 секунд: пінгів %d (на всі відповіли), пакетів даних %d (%zu байтів), "
                "інших %d\n",
                pings, dataPackets, dataBytes, other);
    // Якщо виклик прийшов один раз — сервер прийняв нашу відповідь. Поки
    // вона його не влаштовує, він шле виклик знову й знову.
    std::printf("  подій розібрано: %d, з них об'єктів світу: %d\n", eventCount, objectCount);
    std::printf("  пакетів із потоком привидів: %d, оновлень стану: %d, різних об'єктів: %zu\n",
                ghostPackets, ghostRecords, ghostObjects.size());
    if (!checked.empty()) {
      std::printf("  різних шаблонів: %zu\n", checked.size());
      for (const auto& [stage, count] : stageCounts) {
        std::printf("    %-24s %d\n", std::string(drawStageName(stage)).c_str(), count);
      }
    }
    std::printf("  викликів отримано: %d %s\n", challenges,
                challenges == 1 ? "(відповідь прийнято)" : "(відповідь не прийнято)");

  }

  void disconnect() {
    if (!socket) return;
    socket->send(obf2::net::bf2::writeShortPacket(
        obf2::net::bf2::PacketKind::Disconnect, id));
  }
};

// Текстовий режим: під'єднатися, покрутитися й розповісти, що прийшло.
// Вікна тут немає навмисно — це знаряддя для розбору протоколу.
int runProbe(const Args& args, obf2::FileSystem& files) {
  RemoteWorld remote(args, files);
  if (!remote.connect()) return 1;
  for (int i = 0; i < 90; ++i) remote.pump(500);
  remote.report();
  remote.disconnect();
  return 0;
}

int runSession(const Args& args, obf2::FileSystem& files, std::string* nextLevel,
               RemoteWorld* remote = nullptr) {
  // --- підготовка сцени -------------------------------------------------

  // Унікальна геометрія окремо від розстановки: на рівні той самий будинок
  // трапляється десятками разів, і вантажити його в GPU щоразу немає сенсу.
  struct Scene {
    std::vector<obf2::mesh::RenderMesh> meshes;
    std::vector<std::pair<int, obf2::Mat4>> instances;  // індекс меша + трансформ
    obf2::Vec3f center;
    float radius = 1.0f;

    void add(obf2::mesh::RenderMesh&& geometry, const obf2::Mat4& transform) {
      meshes.push_back(std::move(geometry));
      instances.emplace_back(static_cast<int>(meshes.size()) - 1, transform);
    }
  } scene;

  std::optional<obf2::level::Level> level;
  obf2::game::Registry registry;
  // Сервер і клієнт живуть увесь час, а не лише під час завантаження: в
  // одиночній грі саме вони й рухають світ.
  std::unique_ptr<obf2::server::GameServer> hostedServer;
  std::unique_ptr<obf2::server::GameClient> hostedClient;
  std::uint32_t localSoldierId = 0;

  if (!args.levelName.empty()) {
    std::string error;
    if (!obf2::level::mountLevel(files, args.modDir, args.levelName, &error)) {
      std::fprintf(stderr, "не змонтовано рівень %s: %s\n", args.levelName.c_str(), error.c_str());
      return 1;
    }

    const auto started = std::chrono::steady_clock::now();
    level = obf2::level::loadLevel(files, args.levelName, &error);
    if (!level) {
      std::fprintf(stderr, "рівень не завантажено: %s\n", error.c_str());
      return 1;
    }
    std::printf("рівень: %s\n  карта висот %dx%d, масштаб %.4g/%.6g/%.4g, рівень моря %.1f\n",
                level->name.c_str(), level->primary.size, level->primary.size,
                level->primary.scale.x, level->primary.scale.y, level->primary.scale.z,
                level->terrain.seaLevel);
    std::printf("  статичних об'єктів: %zu, доріг: %zu\n", level->objects.size(),
                level->roads.size());

    auto patches = obf2::level::buildTerrainPatches(*level, files);
    std::printf("  патчів терену: %zu з %d (решта під водою, колормап немає)\n", patches.size(),
                ((level->primary.size - 1) / level->terrain.patchSize) *
                    ((level->primary.size - 1) / level->terrain.patchSize));
    for (auto& patch : patches) scene.add(std::move(patch.geometry), obf2::Mat4::identity());

    // Дороги: вершини лежать відносно точки початку, тому ставимо їх
    // абсолютною позицією з .con.
    int roadsPlaced = 0;
    for (auto& road : level->roads) {
      if (road.geometry.indices.empty()) continue;
      scene.add(std::move(road.geometry), obf2::translation(road.position));
      ++roadsPlaced;
    }
    std::printf("  доріг у сцені: %d\n", roadsPlaced);
    scene.add(obf2::level::buildWaterPlane(*level), obf2::Mat4::identity());

    registry = buildRegistry(files);
    std::printf("  реєстр: %zu шаблонів (%.1f с)\n", registry.size(), secondsSince(started));

    // --- одиночна гра = локальний сервер плюс клієнт ---
    //
    // У BF2 світом володіє сервер навіть офлайн, тому з --hosted розстановка
    // береться не з файлу рівня, а з пакетів, які прийшли від сервера через
    // петлю в пам'яті. Рендер малює те, що прийшло по мережі.
    std::vector<obf2::level::StaticObject> placement = level->objects;

    if (args.hosted) {
      obf2::server::ServerSettings serverSettings;
      serverSettings.levelName = level->name;
      // Поява — над центром карти, трохи вище рівня моря, щоб не опинитися
      // всередині гори.
      serverSettings.spawnPosition = obf2::Vec3f{-40.0f, level->terrain.seaLevel + 40.0f, -200.0f};

      // Константи руху беремо з даних гри, а не з голови: той самий файл,
      // який читає оригінал.
      obf2::engine::Console physicsConsole;
      serverSettings.physics.bind(physicsConsole);
      obf2::con::Interpreter physicsInterpreter(
          files, [&](const obf2::con::Command& c) { physicsConsole.execute(c); });
      physicsInterpreter.runFile("objects/soldiers/common/common.con");
      std::printf("  фізика: прискорення %.2f, гальмування %.2f, керування в повітрі %.2f\n",
                  serverSettings.physics.acceleration, serverSettings.physics.deceleration,
                  serverSettings.physics.airMovementFactor);
      // Квитки й налаштування раунду — з тих самих файлів, що читає гра:
      // GameLogicInit.con (стартові квитки) і Settings/ServerSettings.con.
      {
        obf2::engine::Console settingsConsole;
        settingsConsole.bind("gameLogic.setDefaultNumberOfTickets",
                             [&](const obf2::con::Command& command) {
                               const auto team = command.argInt(0);
                               const auto count = command.argInt(1);
                               if (team && count && *team >= 1 && *team <= 2) {
                                 serverSettings.defaultTickets[*team] = *count;
                               }
                             });
        settingsConsole.bind("sv.ticketRatio", [&](const obf2::con::Command& command) {
          serverSettings.ticketRatio = command.argFloat(0).value_or(serverSettings.ticketRatio);
        });
        settingsConsole.bind("sv.spawnTime", [&](const obf2::con::Command& command) {
          serverSettings.respawnDelay = command.argFloat(0).value_or(serverSettings.respawnDelay);
        });
        settingsConsole.bind("sv.numPlayersNeededToStart", [&](const obf2::con::Command& command) {
          serverSettings.playersNeededToStart =
              command.argInt(0).value_or(serverSettings.playersNeededToStart);
        });
        obf2::con::Interpreter settingsInterpreter(
            files, [&](const obf2::con::Command& c) { settingsConsole.execute(c); });
        settingsInterpreter.runFile("GameLogicInit.con");
        settingsInterpreter.runFile("Settings/ServerSettings.con");
        std::printf("  квитки: %d проти %d (ticketRatio %.0f%%), поява через %.0f с\n",
                    serverSettings.defaultTickets[1], serverSettings.defaultTickets[2],
                    serverSettings.ticketRatio, serverSettings.respawnDelay);
      }

      hostedServer = std::make_unique<obf2::server::GameServer>(serverSettings);
      obf2::server::GameServer& gameServer = *hostedServer;

      // Спершу статика рівня, потім ігрова логіка — саме в такому порядку
      // це робить рушій. Навпаки не можна: `loadWorld` починає з чистого
      // списку об'єктів і змела б прапори, які ставить `setGameplay`.
      gameServer.loadWorld(*level);

      // Ігрова логіка режиму: контрольні точки й спавнери техніки.
      std::string gameplayError;
      if (auto gameplay = obf2::level::loadGameplayObjects(files, level->name, "gpm_cq", 16,
                                                           &gameplayError)) {
        std::printf("  ігрова логіка: %zu контрольних точок, %zu спавнерів техніки, "
                    "%zu точок появи\n",
                    gameplay->controlPoints.size(), gameplay->spawners.size(),
                    gameplay->spawnPoints.size());
        for (const auto& point : gameplay->controlPoints) {
          std::printf("    точка %d \"%s\" радіус %.0f @ %.0f/%.0f/%.0f\n", point.id,
                      point.nameKey.c_str(), point.radius, point.position.x, point.position.y,
                      point.position.z);
        }
        gameServer.setGameplay(std::move(*gameplay));
      } else {
        std::printf("  ігрова логіка: %s\n", gameplayError.c_str());
      }

      // Рельєф для зіткнення з землею: без нього солдат падає без кінця.
      gameServer.setTerrain(&*level);

      // Геометрія зіткнень: для кожного статичного об'єкта беремо шар
      // солдата з .collisionmesh і переводимо у світові координати.
      auto collisionWorld = std::make_unique<obf2::server::CollisionWorld>();
      int withCollision = 0, withoutCollision = 0;
      std::unordered_map<std::string, std::shared_ptr<obf2::mesh::CollisionMesh>> collisionCache;

      // Техніка теж має зупиняти солдата: вона стоїть у світі сервера, а
      // не в статиці рівня, тому додаємо її окремо.
      std::vector<obf2::level::StaticObject> collisionObjects = level->objects;
      for (const auto& object : gameServer.objects()) {
        if (object.spawnerIndex < 0) continue;
        obf2::level::StaticObject vehicle;
        vehicle.templateName = object.templateName;
        vehicle.position = object.position;
        vehicle.rotation = object.rotation;
        vehicle.hasRotation = true;
        collisionObjects.push_back(std::move(vehicle));
      }

      for (const auto& object : collisionObjects) {
        auto cached = collisionCache.find(object.templateName);
        if (cached == collisionCache.end()) {
          std::shared_ptr<obf2::mesh::CollisionMesh> loaded;
          if (const auto* root = registry.find(object.templateName)) {
            // Ім'я меша зіткнень — окрема властивість шаблону.
            const std::string_view name = root->text("collisionMesh");
            if (!name.empty()) {
              const std::string path =
                  resolveCollisionPath(files, root->file, std::string(name));
              if (!path.empty()) {
                if (const auto bytes = files.read(path)) {
                  if (auto mesh = obf2::mesh::loadCollisionMesh(*bytes)) {
                    loaded = std::make_shared<obf2::mesh::CollisionMesh>(std::move(*mesh));
                  }
                }
              }
            }
          }
          cached = collisionCache.emplace(object.templateName, std::move(loaded)).first;
        }
        if (cached->second == nullptr) {
          ++withoutCollision;
          continue;
        }

        const auto* layer = cached->second->layer(obf2::mesh::ColType::Soldier);
        if (layer == nullptr) {
          ++withoutCollision;
          continue;
        }

        // Рослинність приходить готовою матрицею, решта — позиція з кутами.
        obf2::Mat4 transform = object.transform;
        if (!object.hasTransform) {
          transform = obf2::translation(object.position);
          if (object.hasRotation) {
            transform = transform * obf2::rotationYawPitchRoll(object.rotation.x, object.rotation.y,
                                                               object.rotation.z);
          }
        }
        collisionWorld->addLayer(*layer, transform);
        ++withCollision;
      }

      std::printf("  зіткнення: %d об'єктів, %zu трикутників у %zu комірках (без геометрії %d)\n",
                  withCollision, collisionWorld->triangleCount(), collisionWorld->cellCount(),
                  withoutCollision);
      gameServer.setCollision(std::move(collisionWorld));

      auto [clientSide, serverSide] = obf2::net::LoopbackConnection::createPair();
      gameServer.accept(std::move(serverSide));

      hostedClient = std::make_unique<obf2::server::GameClient>(std::move(clientSide),
                                                               std::string("player"));
      obf2::server::GameClient& client = *hostedClient;
      client.connect();

      // Світ великий і ріжеться на пакети, тому крутимо, доки надходять нові.
      std::size_t previous = 0;
      for (int step = 0; step < 4096; ++step) {
        gameServer.tick(1.0f / 60.0f);
        client.tick(1.0f / 60.0f);
        if (client.objects().size() == previous && step > 8) break;
        previous = client.objects().size();
      }

      std::printf("  локальний сервер: %s, гравців %zu, пакетів %lld/%lld\n",
                  std::string(obf2::server::clientStateName(client.state())).c_str(),
                  gameServer.playerCount(), gameServer.packetsSent(),
                  gameServer.packetsReceived());
      int flags = 0;
      for (const auto& [id, object] : client.objects()) {
        (void)id;
        if (object.templateName.rfind("CPNAME", 0) == 0) ++flags;
      }
      std::printf("  контрольних точок дійшло до клієнта: %d\n", flags);
      std::printf("  клієнт отримав об'єктів: %zu з %zu\n", client.objects().size(),
                  level->objects.size());

      // Солдата гравця в сцену не ставимо: ним ми граємо, а не дивимось.
      for (const auto& player : gameServer.players()) localSoldierId = player.soldierId;

      placement.clear();
      placement.reserve(client.objects().size());
      for (const auto& [id, object] : client.objects()) {
        if (id == localSoldierId) continue;
        obf2::level::StaticObject staticObject;
        staticObject.templateName = object.templateName;
        staticObject.position = object.position;
        staticObject.rotation = object.rotation;
        staticObject.hasRotation = true;
        placement.push_back(std::move(staticObject));
      }

      // Рослинність мережею не ходить — клієнт малює її сам за даними
      // рівня, як і оригінал. Матриця з .con несе і поворот, і масштаб.
      int overgrowth = 0;
      for (const obf2::level::StaticObject& object : level->objects) {
        if (!object.isOvergrowth) continue;
        placement.push_back(object);
        ++overgrowth;
      }
      std::printf("  рослинність із даних рівня: %d примірників\n", overgrowth);
    }

    std::unordered_map<std::string, int> meshIndexByTemplate;
    std::map<std::string, int> missing;
    int placed = 0;

    for (const auto& object : placement) {
      auto found = meshIndexByTemplate.find(object.templateName);
      if (found == meshIndexByTemplate.end()) {
        auto built = buildObjectMesh(files, registry, object.templateName, args, false);
        int index = -1;
        if (built) {
          scene.meshes.push_back(std::move(*built));
          index = static_cast<int>(scene.meshes.size()) - 1;
        }
        found = meshIndexByTemplate.emplace(object.templateName, index).first;
      }
      if (found->second < 0) {
        ++missing[object.templateName];
        continue;
      }

      obf2::Mat4 transform = object.transform;
      if (!object.hasTransform) {
        transform = obf2::translation(object.position);
        if (object.hasRotation) {
          transform = transform * obf2::rotationYawPitchRoll(object.rotation.x, object.rotation.y,
                                                             object.rotation.z);
        }
      }
      scene.instances.emplace_back(found->second, transform);
      ++placed;
    }

    std::printf("  унікальної геометрії: %zu, розставлено: %d, без геометрії: %zu шаблонів\n",
                scene.meshes.size() - patches.size(), placed, missing.size());
    int shown = 0;
    for (const auto& [name, count] : missing) {
      if (shown++ >= 5) break;
      std::printf("    без геометрії: %s (x%d)\n", name.c_str(), count);
    }

    std::printf("  освітлення терену: сонце %.2f/%.2f/%.2f, небо %.2f/%.2f/%.2f\n",
                level->terrain.terrainSunColor.x, level->terrain.terrainSunColor.y,
                level->terrain.terrainSunColor.z, level->terrain.terrainSkyColor.x,
                level->terrain.terrainSkyColor.y, level->terrain.terrainSkyColor.z);
    std::printf("  туман: %.0f..%.0f, колір %.2f/%.2f/%.2f\n", level->terrain.fogStart,
                level->terrain.fogEnd, level->terrain.fogColor.x, level->terrain.fogColor.y,
                level->terrain.fogColor.z);

    const float extent = level->halfExtent() * level->primary.scale.x;
    scene.center = obf2::Vec3f{0.0f, level->terrain.seaLevel, 0.0f};
    scene.radius = extent;
  }

  // --- решта режимів ----------------------------------------------------

  obf2::engine::Engine engine;
  const bool bootMode = args.levelName.empty() && args.objectName.empty() && args.meshPath.empty();
  obf2::hud::Builder menuHud;
  obf2::hud::Screen menuScreen;
  std::string requestedLevel;
  bool menuQuit = false;
  // Підсвітка кнопки: для кожної кнопки заздалегідь спечений прямокутник.
  std::unordered_map<const obf2::hud::Node*, int> hoverQuads;
  std::vector<const obf2::hud::Node*> menuQuadNodes;
  const obf2::hud::Node* lastHovered = nullptr;
  int introQuad = -1;
  int menuQuad = -1;
  int loadingQuad = -1;
  std::vector<int> menuTextQuads;
  std::vector<int> loadingTextQuads;

  if (bootMode) {
    engine.boot(files, args.modDir);
    const auto& settings = engine.settings();

    std::printf("запуск рушія\n  прочитано: ");
    for (const auto& file : engine.bootFiles()) std::printf("%s ", file.c_str());
    std::printf("\n  гравець: \"%s\", повноекранний: %d, поле зору: %.2f\n",
                settings.general.playerName.c_str(), settings.video.fullScreen ? 1 : 0,
                settings.video.fieldOfView);
    std::printf("  показувати заставку: %d, заставок знайдено: %zu\n",
                settings.general.viewIntroMovie ? 1 : 0, engine.movies().size());
    for (const auto& movie : engine.movies()) {
      std::printf("    %s (%.1f МБ)\n", movie.path.c_str(),
                  static_cast<double>(movie.sizeBytes) / (1024.0 * 1024.0));
    }

    const auto& console = engine.console();
    std::printf("  консоль: обробників %zu, виконано команд %lld, невідомих %lld\n",
                console.handlerCount(), console.executedCount(), console.unknownCount());
    int shown = 0;
    for (const auto& [name, count] : console.unknownCommands()) {
      if (shown++ >= 24) break;
      std::printf("    без обробника: %s (x%d)\n", name.c_str(), count);
    }

    // Команди, які виконують кнопки меню. Інтерфейс керує грою через
    // консоль — так само, як в оригіналі.
    engine.console().bind("openbf2.startLevel", [&](const obf2::con::Command& command) {
      const std::string_view level = command.argStr(0);
      requestedLevel = level.empty() && !engine.levels().empty()
                           ? engine.levels().front().directory
                           : std::string(level);
      std::printf("меню: запуск рівня %s\n", requestedLevel.c_str());
    });
    engine.console().bind("openbf2.quit", [&](const obf2::con::Command&) {
      std::printf("меню: вихід\n");
      menuQuit = true;
    });
    for (const char* stub : {"openbf2.multiplayer", "openbf2.options", "openbf2.bfhq",
                             "openbf2.community"}) {
      const std::string name = stub;
      engine.console().bind(name, [name](const obf2::con::Command&) {
        // Ці екрани ще не зроблені; кажемо про це прямо, а не мовчимо.
        std::printf("меню: \"%s\" ще не реалізовано\n", name.c_str());
      });
    }

    const std::string background = findMenuBackground(files);
    std::printf("  стан: %s, тло меню: %s\n",
                std::string(obf2::engine::stateName(engine.state())).c_str(),
                background.empty() ? "(немає)" : background.c_str());

    scene.meshes.push_back(buildScreenQuad("#000000"));
    introQuad = static_cast<int>(scene.meshes.size()) - 1;
    // Тло меню малює сам HUD (вузол Background у MainMenu.con), тож
    // окремий екранний прямокутник лишається тільки для заставок.
    (void)background;
    menuQuad = -1;

    // --- меню: описане тим самим hudBuilder, що й інтерфейс гри ---
    //
    // Наші власні ассети монтуємо поруч із ігровими: так меню лишається
    // даними, а не кодом, і його можна правити без перезбирання.
    files.mountDirectory("assets", "openbf2");

    obf2::con::Interpreter menuInterpreter(
        files, [&](const obf2::con::Command& command) { menuHud.feed(command); });
    menuInterpreter.runFile("openbf2/menu/MainMenu.con");

    // Список карт додаємо тими самими командами: кожна карта — кнопка,
    // яка запускає рівень.
    {
      float y = 308.0f;
      int index = 0;
      for (const auto& entry : engine.levels()) {
        if (y > 560.0f) break;
        obf2::con::Command create;
        create.path = {"hudBuilder", "createButtonNode"};
        create.lowerPath = "hudbuilder.createbuttonnode";
        create.args = {"MainMenu", "Level" + std::to_string(index), "40",
                       std::to_string(static_cast<int>(y)), "260", "13"};
        menuHud.feed(create);

        obf2::con::Command label;
        label.path = {"hudBuilder", "setTextNodeString"};
        label.lowerPath = "hudbuilder.settextnodestring";
        label.args = {entry.displayName};
        menuHud.feed(label);

        obf2::con::Command command;
        command.path = {"hudBuilder", "setButtonNodeConCmd"};
        command.lowerPath = "hudbuilder.setbuttonnodeconcmd";
        command.args = {"openbf2.startLevel", entry.directory};
        menuHud.feed(command);

        y += 15.0f;
        ++index;
      }
    }
    // Наше меню теж дерево: "MainMenu" — батько кнопок.
    menuHud.finish();
    std::printf("  меню: %zu вузлів у групі MainMenu\n",
                menuHud.group("MainMenu").size());

    // --- шрифт, локалізація, список карт ---
    const LoadedFont menuFont = loadFont(files, "Fonts/800/dynamicText_13");
    std::printf("  шрифт: %s, гліфів %zu, пар кернінгу %zu, атлас %dx%d\n",
                menuFont.valid ? menuFont.font.name.c_str() : "(немає)",
                menuFont.font.glyphCount(), menuFont.font.kerningCount(),
                menuFont.font.atlasWidth, menuFont.font.atlasHeight);
    std::printf("  локалізація: %zu рядків, рівнів: %zu\n", engine.lexicon().size(),
                engine.levels().size());

    if (menuFont.valid) {
      obf2::font::TextLayout layout;
      layout.screenWidth = 1280;
      layout.screenHeight = 720;

      auto addText = [&](std::string_view text, float x, float y, float scale) {
        layout.x = x;
        layout.y = y;
        layout.scale = scale;
        auto geometry = obf2::font::buildText(menuFont.font, text, layout, menuFont.atlasPath);
        if (geometry.indices.empty()) return -1;
        scene.meshes.push_back(std::move(geometry));
        return static_cast<int>(scene.meshes.size()) - 1;
      };

      // Меню малюється з дерева вузлів — тим самим шляхом, що й увесь
      // інтерфейс гри. Підписи проходять через лексикон BF2.
      obf2::hud::Context hudContext;
      hudContext.localize = [&](std::string_view key) { return engine.lexicon().text(key); };

      menuScreen.width = args.width;
      menuScreen.height = args.height;
      for (auto& piece : obf2::hud::buildGroup(menuHud, "MainMenu", menuFont.font,
                                               menuFont.atlasPath, menuScreen, hudContext)) {
        scene.meshes.push_back(std::move(piece.geometry));
        menuTextQuads.push_back(static_cast<int>(scene.meshes.size()) - 1);
        // Запам'ятовуємо, якому вузлу належить шматок: підсвітку треба
        // покласти саме під його підпис, а не поверх усього меню.
        menuQuadNodes.push_back(piece.node);
      }

      // Підсвітка кнопки під курсором. Геометрію печемо наперед на кожну
      // кнопку — так само, як текст: накладний пайплайн малює її як є.
      for (const obf2::hud::Node* node : menuHud.group("MainMenu")) {
        if (node->type != obf2::hud::NodeType::Button || node->command.empty()) continue;
        const obf2::hud::ScreenRect rect = obf2::hud::nodeRect(*node, menuScreen);
        scene.meshes.push_back(obf2::hud::buildRect(rect, menuScreen, "#20344a"));
        hoverQuads[node] = static_cast<int>(scene.meshes.size()) - 1;
      }

      // --- екран завантаження ---
      const auto* first = engine.levels().empty() ? nullptr : &engine.levels().front();
      if (first != nullptr) {
        const std::string image = first->loadImage.empty() ? std::string("#101418") : first->loadImage;
        scene.meshes.push_back(buildScreenQuad(image));
        loadingQuad = static_cast<int>(scene.meshes.size()) - 1;

        loadingTextQuads.push_back(addText(first->displayName, 64.0f, 520.0f, 2.0f));

        // Опис карти — той самий рядок лексикону, що показує гра
        // (locid із <briefing> у .desc).
        if (!first->briefingKey.empty()) {
          const std::string_view briefing = engine.lexicon().text(first->briefingKey);
          float y = 570.0f;
          for (const auto& line : obf2::font::wrapText(menuFont.font, briefing, 900.0f, 1.1f)) {
            if (y > 690.0f) break;
            loadingTextQuads.push_back(addText(line, 64.0f, y, 1.1f));
            y += 18.0f;
          }
        }
        loadingTextQuads.erase(std::remove(loadingTextQuads.begin(), loadingTextQuads.end(), -1),
                               loadingTextQuads.end());
      }
    }

    if (args.screen == "menu") engine.skipAllMovies();
    if (args.screen == "loading" && !engine.levels().empty()) {
      engine.skipAllMovies();
      engine.startLoading(engine.levels().front().directory);
    }
  } else if (args.levelName.empty()) {
    std::optional<obf2::mesh::RenderMesh> single;
    if (!args.objectName.empty()) {
      registry = buildRegistry(files);
      std::printf("реєстр: %zu шаблонів\n", registry.size());
      single = buildObjectMesh(files, registry, args.objectName, args, true);
      if (!single) {
        std::fprintf(stderr, "не вдалося зібрати об'єкт %s\n", args.objectName.c_str());
        return 1;
      }

      // Скелетна анімація: ставимо меш у позу з кліпів. Кліпів може бути
      // кілька — рушій змішує їх по кістках (зброя рухає верх тіла, рух —
      // ноги), і кожен торкається лише своїх кісток.
      if (!args.animationPaths.empty() && !single->skin.empty()) {
        const std::string skeletonPath =
            args.skeletonPath.empty()
                ? std::string("objects/soldiers/Common/Animations/3p_setup.ske")
                : args.skeletonPath;

        const auto skeletonBytes = files.read(skeletonPath);
        std::string skinError;
        const auto skeleton =
            skeletonBytes ? obf2::mesh::loadSkeleton(*skeletonBytes, &skinError) : std::nullopt;
        if (!skeleton) {
          std::fprintf(stderr, "скелет: %s\n", skinError.c_str());
        } else {
          std::vector<obf2::mesh::BoneAnimation> clips;
          for (const std::string& path : args.animationPaths) {
            const auto bytes = files.read(path);
            if (!bytes) {
              std::fprintf(stderr, "немає анімації %s\n", path.c_str());
              continue;
            }
            auto clip = obf2::mesh::loadBoneAnimation(*bytes, &skinError);
            if (!clip) {
              std::fprintf(stderr, "анімація %s: %s\n", path.c_str(), skinError.c_str());
              continue;
            }
            clips.push_back(std::move(*clip));
          }

          std::vector<obf2::mesh::PoseStage> stages;
          const auto frameIndex = static_cast<std::uint32_t>(args.frame < 0 ? 0 : args.frame);
          for (const auto& clip : clips) {
            // Кадр беремо по колу: кліпи різної довжини (ноги 16 кадрів,
            // зброя 36), а показуємо ми один момент.
            const std::uint32_t frame =
                clip.frameCount == 0 ? 0 : frameIndex % clip.frameCount;
            stages.push_back(obf2::mesh::PoseStage{&clip, frame, 1.0f});
            std::printf("  кліп: %zu доріжок, %u кадрів -> кадр %u\n", clip.boneIds.size(),
                        clip.frameCount, frame);
          }

          if (!stages.empty()) {
            const auto pose = obf2::mesh::poseSkeleton(*skeleton, stages);
            obf2::mesh::RenderMesh posed = *single;
            obf2::mesh::skinMesh(*single, pose, posed);
            single = std::move(posed);
          }
        }
      }
    } else {
      single = loadMesh(files, args.meshPath, args.geometryIndex, args.lodIndex, true);
      if (!single) return 1;
    }

    const obf2::Vec3f boundsMin{single->bounds.min.x, single->bounds.min.y, single->bounds.min.z};
    const obf2::Vec3f boundsMax{single->bounds.max.x, single->bounds.max.y, single->bounds.max.z};
    scene.center = (boundsMin + boundsMax) * 0.5f;
    scene.radius = std::max(0.001f, obf2::length(boundsMax - boundsMin) * 0.5f);
    scene.add(std::move(*single), obf2::Mat4::identity());
  }

  // --- GPU --------------------------------------------------------------

  obf2::gfx::WindowDesc desc;
  desc.title = "OpenBattlefield2";
  desc.width = args.width;
  desc.height = args.height;
  std::string error;
  auto device = obf2::gfx::Device::create(desc, &error);
  if (!device) {
    std::fprintf(stderr, "не вдалося створити пристрій: %s\n", error.c_str());
    return 1;
  }
  {
    int windowWidth = 0, windowHeight = 0;
    SDL_GetWindowSize(device->window(), &windowWidth, &windowHeight);
    std::printf("GPU-бекенд: %s | вікно %dx%d (просили %dx%d)\n",
                std::string(device->driver()).c_str(), windowWidth, windowHeight, args.width,
                args.height);
  }

  auto renderer = obf2::gfx::MeshRenderer::create(*device, &error);
  if (!renderer) {
    std::fprintf(stderr, "рендерер: %s\n", error.c_str());
    return 1;
  }

  if (level) {
    // У режимі мапи туман тільки заважає: з висоти він з'їдає весь рівень.
    const float fogEnd = args.topDown ? 0.0f : level->terrain.fogEnd;
    renderer->setFog(obf2::gfx::MeshRenderer::Fog{
        obf2::gfx::Color{level->terrain.fogColor.x, level->terrain.fogColor.y,
                         level->terrain.fogColor.z, 1.0f},
        level->terrain.fogStart, fogEnd});
    renderer->setTerrainLighting(
        obf2::gfx::Color{level->terrain.terrainSunColor.x, level->terrain.terrainSunColor.y,
                         level->terrain.terrainSunColor.z, 1.0f},
        obf2::gfx::Color{level->terrain.terrainSkyColor.x, level->terrain.terrainSkyColor.y,
                         level->terrain.terrainSkyColor.z, 1.0f});
  }

  int texturesLoaded = 0, texturesMissing = 0;
  std::unordered_map<std::string, std::optional<obf2::texture::Texture>> textureCache;
  auto resolveTexture =
      [&](const std::string& mapName) -> std::optional<obf2::texture::Texture> {
    // Імена, що починаються з '#', — не файли, а кольори: гра подекуди
    // задає колір числом (renderer.waterColor), а нам ще потрібні заливки
    // для екранів без зображення.
    if (level && mapName == obf2::level::kWaterColorMap) {
      const obf2::Vec3f color = level->terrain.waterColor;
      return obf2::texture::solidColor(color.x, color.y, color.z);
    }
    if (mapName.size() == 7 && mapName[0] == '#') {
      const auto channel = [&](std::size_t offset) {
        return static_cast<float>(std::stoi(mapName.substr(offset, 2), nullptr, 16)) / 255.0f;
      };
      return obf2::texture::solidColor(channel(1), channel(3), channel(5));
    }

    const std::string path = obf2::normalizeAssetPath(mapName);
    if (const auto cached = textureCache.find(path); cached != textureCache.end()) {
      return cached->second;
    }

    auto bytes = files.read(path);
    // У грі шляхи вказують на `.tga`, а в архівах лежить `.dds` — так,
    // наприклад, із картою рівня: BF2.exe просить
    // `Levels/%s/Hud/Minimap/ingameMap.tga`, а в client.zip є лише
    // `ingameMap.dds`. Тому стиснений варіант пробуємо тим самим шляхом.
    std::string swapped;
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".tga") == 0) {
      swapped = path.substr(0, path.size() - 4) + ".dds";
      if (!bytes) bytes = files.read(swapped);
    }
    if (!bytes) bytes = files.read(obf2::joinAssetPath("objects", path));
    if (!bytes && !swapped.empty()) bytes = files.read(obf2::joinAssetPath("objects", swapped));
    // Шляхи в HUD відлічуються від теки текстур інтерфейсу — так само, як
    // це видно в `nametags.setTexture Menu/HUD/Texture/...`.
    if (!bytes) bytes = files.read(obf2::joinAssetPath("menu/hud/texture", path));
    if (!bytes && !swapped.empty()) {
      bytes = files.read(obf2::joinAssetPath("menu/hud/texture", swapped));
    }
    if (!bytes) {
      ++texturesMissing;
      textureCache.emplace(path, std::nullopt);
      return std::nullopt;
    }

    std::string textureError;
    auto decoded = obf2::texture::loadImage(*bytes, &textureError);
    if (!decoded) {
      ++texturesMissing;
      if (texturesMissing <= 6) {
        std::printf("    текстура не читається: %s (%s)\n", path.c_str(), textureError.c_str());
      }
    } else {
      ++texturesLoaded;
    }
    textureCache.emplace(path, decoded);
    return decoded;
  };

  // --- ігровий HUD ---------------------------------------------------
  //
  // Той самий hudBuilder, що й меню, але дерево береться з файлів гри:
  // Global -> GlobalHud -> IngameHud -> десятки під-груп через `split`.
  obf2::hud::Builder ingameHud;
  std::vector<int> hudQuads;
  // Колір вузла за номером меша: setNodeColor у грі домножує текстуру, і
  // без нього жовті написи, підсвітка вкладок і кольорові смуги виходять
  // просто білими.
  std::map<int, obf2::hud::Color> hudTints;
  // Екрани, які видно, лише поки тримають клавішу: табло, рація, поява.
  // Геометрію печемо наперед — вона не змінюється, змінюється лише те,
  // чи малювати її цього кадру.
  struct KeyScreen {
    std::string group;
    std::string action;  // назва дії в ControlMap, не клавіша
    std::vector<int> quads;
  };
  std::vector<KeyScreen> keyScreens;
  obf2::game::ControlMap controls;
  std::map<std::string, bool> hudVariables;
  std::map<std::string, std::string> hudStrings;
  std::map<std::string, float> hudValues;
  // Прозорість плашок. Це не наша вигадка й не нуль: BF2.exe бере
  // прозорість із профілю гравця (GeneralSettings.setHUDTransparency /
  // setMinimapTransparency, типово 204), множить на 1/255 — константа
  // 0x8a4a64 — і кладе у змінні MenuBackgroundAlpha та MenuMapAlpha
  // (0x4b68d3 і 0x4b6907). Доти ми ставили нуль, і широкі плашки під
  // здоров'ям, витривалістю та набоями не малювалися зовсім.
  const std::map<std::string, float> hudAlpha = {
      {"MenuBackgroundAlpha",
       static_cast<float>(engine.settings().general.hudTransparency) / 255.0f},
      {"MenuMapAlpha",
       static_cast<float>(engine.settings().general.minimapTransparency) / 255.0f},
  };
  // Вузли, вміст яких змінюється в грі: підписи й смуги. Геометрію для них
  // перебудовуємо, але лише коли справді змінилося значення.
  struct DynamicNode {
    const obf2::hud::Node* node = nullptr;
    std::string shownText;
    float shownValue = -1.0f;
    obf2::gfx::GpuMesh mesh;
    bool valid = false;
  };
  std::vector<DynamicNode> hudDynamic;
  const LoadedFont hudFont = bootMode ? LoadedFont{} : loadFont(files, "Fonts/800/dynamicText_13");
  // Шрифти вузлів, за їхнім стилем. Вантажимо на вимогу: у HUD їх з
  // десяток, і читати всі 358 із архіву нема потреби.
  std::map<std::string, LoadedFont> hudFonts;
  obf2::hud::Screen hudScreen;
  if (!bootMode && hudFont.valid) {
    // Словник: без нього на HUD видно ключі («HUD_TEXT_MENU_SCORE_ROUNDSWON»)
    // замість тексту. У меню його вантажить boot, а в бою його ніхто не
    // вантажив — звідси й ключі на екрані.
    engine.loadLexicon(files);

    obf2::con::Interpreter hudInterpreter(
        files, [&](const obf2::con::Command& command) { ingameHud.feed(command); });
    hudInterpreter.runFile("Menu/HUD/HudSetup/HudSetupMain.con");
    // Зв'язати дерево: доти координати вузлів лишаються відносними до
    // батька, і HUD розсипається по екрану.
    ingameHud.finish();

    // Розкладка керування — з даних гри. Питаємо про дію, а яка це
    // клавіша, вирішує `Settings/Controls.con`.
    obf2::con::Interpreter controlInterpreter(
        files, [&](const obf2::con::Command& command) { controls.feed(command); });
    controlInterpreter.runFile("Settings/Controls.con");

    // Змінні показу: у даних це або стала 1/0, або назва стану інтерфейсу.
    // Невідому назву вважаємо вимкненою — інакше на екран одразу виїхали б
    // інтерфейс командира, табло й кабіни всієї техніки.
    // ReferenceCross — це вирівнювальний хрест розробників, у грі він
    // вимкнений; решта — базовий набір, який видно в бою.
    // ToggleScore — вкладка «Гравці» на табло. Що вона типова, видно в
    // бінарі: у поле прапорця (Scoreboard+0x365) є рівно один запис
    // сталої, `movb $0x1, 0x365(%esi)` за 0x7a48f7. Без неї тло лівої
    // панелі (team1_byScore.tga) не малюється зовсім.
    for (const char* on : {"ShowIngameHud", "PlayerHealthShow", "PlayerStaminaShow",
                           "PrimaryAmmoShow", "PrimaryAmmoBarShow", "MapShow", "MapMinSize",
                           "CPInterfaceEnabled", "ToggleScore"}) {
      hudVariables[on] = true;
    }

    // --- екран появи: сім класів -------------------------------------
    //
    // Вузли Kit0..Kit6 у HudElementsSpawn.con нічого не показують самі:
    // кожен висить на своїй змінній, а вміст приходить теж змінними —
    // KitName<N>String (ключ підпису) і KitIcon<N>Path (піктограма).
    // Порядок беремо з таблиці локалізації, де ключі йдуть саме так, як
    // на екрані гри:
    //
    //   HUD_TEXT_MENU_SPAWN_KIT_SPECIALFORCES  SNIPER  ASSAULT  SUPPORT
    //   ENGINEER  MEDIC  ANTITANK
    // Зброя в кожній панелі — теж змінна (KitWeaponIcon<N>Path). Яка
    // саме, видно з набору: у `Objects_server.zip` кожен `Kits/US/*.con`
    // перелічує свої шаблони, і рівно один із них має піктограму в
    // `Weapons/Icons/Hud/Selection`. Звідти й беремо.
    struct KitSlot {
      const char* nameKey;
      const char* icon;
      const char* weapon;
    };
    static const KitSlot kKits[] = {
        {"HUD_TEXT_MENU_SPAWN_KIT_SPECIALFORCES", "Ingame/Kits/Icons/kit_Specops.tga",
         "USRIF_M4.tga"},
        {"HUD_TEXT_MENU_SPAWN_KIT_SNIPER", "Ingame/Kits/Icons/kit_Sniper.tga", "USRIF_M24.tga"},
        {"HUD_TEXT_MENU_SPAWN_KIT_ASSAULT", "Ingame/Kits/Icons/kit_Light_Assault.tga",
         "USRIF_M203.tga"},
        {"HUD_TEXT_MENU_SPAWN_KIT_SUPPORT", "Ingame/Kits/Icons/kit_Heavy_Assault.tga",
         "USLMG_M249SAW.tga"},
        {"HUD_TEXT_MENU_SPAWN_KIT_ENGINEER", "Ingame/Kits/Icons/kit_Engineer.tga",
         "USRIF_Remington11-87.tga"},
        {"HUD_TEXT_MENU_SPAWN_KIT_MEDIC", "Ingame/Kits/Icons/kit_Medic.tga", "USRIF_M16a2.tga"},
        {"HUD_TEXT_MENU_SPAWN_KIT_ANTITANK", "Ingame/Kits/Icons/kit_ATAA.tga",
         "USRIF_MP5_A3.tga"},
    };
    // Вкладки команд угорі екрана появи. У даних гілка TeamSelectInfo
    // висить на Team1Selected, а всередині два блоки — Team1Selected і
    // Team2Selected.
    hudVariables["Team1Selected"] = args.team != 2;
    hudVariables["Team2Selected"] = args.team == 2;
    hudVariables["KitsShow"] = true;
    for (int slot = 0; slot < static_cast<int>(std::size(kKits)); ++slot) {
      const std::string index = std::to_string(slot);
      hudVariables["Kit" + index + "Show"] = true;
      hudStrings["KitName" + index + "String"] = kKits[slot].nameKey;
      hudStrings["KitIcon" + index + "Path"] = kKits[slot].icon;
      hudStrings["KitWeaponIcon" + index + "Path"] =
          std::string("Ingame/Weapons/Icons/Hud/Selection/") + kKits[slot].weapon;
      // Вибраний клас підсвічується окремою гілкою вузла.
      hudVariables["PlayerKitIcon" + index + "SelectShow"] = slot == args.kit;
    }

    obf2::hud::Context hudContext;
    // Картинку карти рівня задає не HUD: у BF2.exe для неї є шаблон
    // `Levels/%s/Hud/Minimap/ingameMap.tga`.
    if (!args.levelName.empty()) {
      hudContext.mapTexture = "Levels/" + args.levelName + "/Hud/Minimap/ingameMap.tga";
    }
    hudContext.localize = [&](std::string_view key) { return engine.lexicon().text(key); };
    // Шрифт кожного вузла — той, що названий у setTextNodeStyle. Шлях у
    // даних записаний як "Fonts/hudFontLocalBold_9.dif" (подекуди зі
    // зворотним слешем), а в архіві шрифти лежать двома наборами: у корені
    // й у теці `800`. Ми міряємо HUD базовими 800x600, тож беремо `800`,
    // а корінь лишається запасним.
    hudContext.fontFor = [&](std::string_view style) -> obf2::hud::FontRef {
      std::string key(style);
      for (char& c : key) {
        if (c == '\\') c = '/';
      }
      if (key.size() > 4 && key.compare(key.size() - 4, 4, ".dif") == 0) {
        key.resize(key.size() - 4);
      }
      const auto cached = hudFonts.find(key);
      if (cached != hudFonts.end()) {
        return obf2::hud::FontRef{cached->second.valid ? &cached->second.font : nullptr,
                                  cached->second.atlasPath};
      }
      const std::size_t slash = key.rfind('/');
      const std::string dir = slash == std::string::npos ? std::string() : key.substr(0, slash + 1);
      const std::string name = slash == std::string::npos ? key : key.substr(slash + 1);
      LoadedFont loaded = loadFont(files, dir + "800/" + name);
      if (!loaded.valid) loaded = loadFont(files, key);
      const auto placed = hudFonts.emplace(key, std::move(loaded)).first;
      return obf2::hud::FontRef{placed->second.valid ? &placed->second.font : nullptr,
                                placed->second.atlasPath};
    };
    hudContext.isVisible = [&](std::string_view variable) {
      if (variable == "1") return true;
      const auto found = hudVariables.find(std::string(variable));
      return found != hudVariables.end() && found->second;
    };
    hudContext.variableValue = [&](std::string_view variable) -> float {
      const auto found = hudValues.find(std::string(variable));
      return found == hudValues.end() ? 0.0f : found->second;
    };
    // Прозорість: знаємо поки одну змінну, зате важливу. Широкі плашки
    // під смугами здоров'я й набоїв (400x39, healthBackground.tga і
    // ammoBackground.tga) висять саме на ній, і в бою вона нульова — у
    // грі під смугами видно лише вузьку смужку загону, 142 одиниці.
    hudContext.variableAlpha = [&](std::string_view variable) -> std::optional<float> {
      const auto found = hudAlpha.find(std::string(variable));
      if (found == hudAlpha.end()) return std::nullopt;
      return found->second;
    };
    hudContext.variableText = [&](std::string_view variable) -> std::string_view {
      const auto found = hudStrings.find(std::string(variable));
      return found == hudStrings.end() ? std::string_view{} : std::string_view(found->second);
    };

    // HUD міряємо **справжнім** вікном, а не тим, що просили: екран міг
    // виявитися меншим, і вікно з'їхало б разом із запитом.
    hudScreen.width = args.width;
    hudScreen.height = args.height;
    SDL_GetWindowSize(device->window(), &hudScreen.width, &hudScreen.height);
    // Корінь — група Global (Global -> GlobalHud -> IngameHud і далі). Її
    // вузли задані в абсолютних 800x600, тож лягають правильно.
    //
    // Кутові шари (BottomLeftStatic, BottomRightAnimate, TopLayer ...) поки
    // не малюємо: у `.con` вони ніде не позиціонуються. Якір лежить у
    // файлах `MemeFile 2.0` — це дані, а не код, шукати в BF2.exe його не
    // треба (див. docs/formats/hud-meme.md). Поки він не розібраний, смуга
    // здоров'я їхала б на середину екрана.
    auto pieces = obf2::hud::buildTree(ingameHud, "Global", hudFont.font, hudFont.atlasPath,
                                       hudScreen, hudContext);

    // Кутові шари — окремі корені: у даних ніщо не веде до них із Global.
    // Розробники самі це описали в `Menu/HUD/HudSetup/Readme.txt`: є сім
    // ділянок, і вузли в них дістають координати **від лівого верхнього
    // кута ділянки**. Де ті кути — сказано в `Menu/Ingame`
    // (`tools/meme_read.py Ingame`):
    //
    //   BottomLeftAnimate   BfTransformNode 400x64   X<-BottomLeft_XPos   Y=563
    //     Next node -> TransformNode  X=-1  Y=563  400x64   (Static)
    //   BottomRightAnimate  BfTransformNode 600x100  X<-BottomRight_XPos  Y=497
    //     Next node -> TransformNode  X=401 Y=563  400x64   (Static)
    //
    // Y беремо просто звідти. X у «рухомих» шарів — це змінна, і в файлі
    // збережено відведене положення (-295 і 503): з ним вміст цілком за
    // краєм екрана, тобто це саме сховано. Висунуте положення — рівне з
    // нерухомим шаром по зовнішньому краю: ліворуч по лівому (-1),
    // праворуч по правому (401+400-600=201).
    //
    // Перевірка сходиться: обидві плашки лягають в одну й ту саму смугу
    // 561..600 і виступають за свій край дзеркально — ліва на 103, права
    // на 101.
    struct Layer {
      const char* group;
      float x;
      float y;
      obf2::hud::Anchor anchor;  // до якого краю тулиться на широкому екрані
    };
    for (const Layer& layer : {
             Layer{"BottomLeftAnimate", -1.0f, 563.0f, obf2::hud::Anchor::Left},
             Layer{"BottomLeftStatic", -1.0f, 563.0f, obf2::hud::Anchor::Left},
             // X цієї ділянки — виміряний, а не взятий із файлу. У файлі
             // лежить лише схований стан (BottomRight_XPos = 503) і пара
             // ToggleData 201/503; висунуте положення рахує вже дія під
             // час гри, тож із даних його не видно. Знімок кадру
             // оригіналу (Ctrl+Shift+D, див. docs/research/03-frame-dump.md)
             // дає 336.5, і три різні вузли сходяться на ньому:
             //
             //   BottomRightBar  301 -> 637.5     ShotSelect 449 -> 785.5
             //   безіменний 16x10 431 -> 767.5
             //
             // Значення стале в усіх трьох знятих кадрах, тобто це не
             // проміжок анімації. Раніше тут стояло 201 — плашка набоїв
             // від того сиділа на 135 пікселів лівіше, ніж в оригіналі.
             Layer{"BottomRightAnimate", 336.5f, 497.0f, obf2::hud::Anchor::Right},
             Layer{"BottomRightStatic", 401.0f, 563.0f, obf2::hud::Anchor::Right},
         }) {
      obf2::hud::Screen layerScreen = hudScreen;
      layerScreen.originX = layer.x;
      layerScreen.originY = layer.y;
      // На широкому екрані ділянка тримається свого краю — саме для
      // цього вона в грі й окрема.
      layerScreen.anchor = layer.anchor;
      auto layerPieces = obf2::hud::buildTree(ingameHud, layer.group, hudFont.font,
                                              hudFont.atlasPath, layerScreen, hudContext);
      if (layerPieces.empty()) continue;
      std::printf("  HUD: шар %-20s кут %.0f %.0f, шматків %zu\n", layer.group, layer.x,
                  layer.y, layerPieces.size());
      for (auto& piece : layerPieces) pieces.push_back(std::move(piece));
    }

    for (auto& piece : pieces) {
      scene.meshes.push_back(std::move(piece.geometry));
      const int index = static_cast<int>(scene.meshes.size()) - 1;
      hudQuads.push_back(index);
      hudTints.emplace(index, piece.tint);
    }

    // Кожен екран на клавішу відмикає рівно **одна** змінна — та, що
    // стоїть на його корені в даних гри:
    //
    //   Scoreboard -> ScoreboardShow      RadioRose -> RadioInterfaceShow
    //   SpawnMenu  -> SpawnShow           MapMenu   -> MapMenuShow
    //
    // Доти ми на цих екранах вважали ввімкненим усе незнайоме — і на табло
    // разом вилазили обидві вкладки, обидва набори підписів і блок даних
    // сервера, накладаючись один на одного. Правильно навпаки: правила ті
    // самі, що й у бою, плюс сама ця змінна.
    struct KeyScreenSetup {
      const char* group;
      const char* action;
      const char* gate;
      // Друга гілка, яку цей екран додає до себе. Карта живе в основному
      // дереві (MapSplit під IngameHud), а не всередині екрана появи, —
      // на екрані вона просто перемикається у велике подання.
      const char* extraRoot = nullptr;
      obf2::hud::MapView mapView = obf2::hud::MapView::Mini;
    };
    for (const KeyScreenSetup& setup : {
             KeyScreenSetup{"Scoreboard", "c_GIShowScoreboard", "ScoreboardShow"},
             KeyScreenSetup{"RadioRose", "c_GIRadioComm", "RadioInterfaceShow"},
             KeyScreenSetup{"SpawnMenu", "c_GIEnter", "SpawnShow", "MapSplit",
                            obf2::hud::MapView::Maxi},
             KeyScreenSetup{"MapMenu", "c_GIMapSize", "MapMenuShow"},
         }) {
      const char* const group = setup.group;
      const char* const action = setup.action;
      obf2::hud::Context keyContext = hudContext;
      const std::string gate = setup.gate;
      keyContext.isVisible = [&, gate](std::string_view variable) {
        if (variable == "1" || variable == gate) return true;
        const auto found = hudVariables.find(std::string(variable));
        return found != hudVariables.end() && found->second;
      };
      keyContext.variableValue = [&, gate](std::string_view variable) -> float {
        if (variable == gate) return 1.0f;
        const auto found = hudValues.find(std::string(variable));
        return found == hudValues.end() ? 0.0f : found->second;
      };
      if (setup.mapView != obf2::hud::MapView::Mini) ingameHud.setMapView(setup.mapView);
      auto built = obf2::hud::buildTree(ingameHud, group, hudFont.font, hudFont.atlasPath,
                                        hudScreen, keyContext);
      if (setup.extraRoot != nullptr) {
        for (auto& piece : obf2::hud::buildTree(ingameHud, setup.extraRoot, hudFont.font,
                                                hudFont.atlasPath, hudScreen, keyContext)) {
          built.push_back(std::move(piece));
        }
      }
      // Повертаємо мініатюру: основний HUD міряється саме нею.
      if (setup.mapView != obf2::hud::MapView::Mini) {
        ingameHud.setMapView(obf2::hud::MapView::Mini);
      }
      if (built.empty()) continue;
      KeyScreen screen;
      screen.group = group;
      screen.action = action;
      for (auto& piece : built) {
        scene.meshes.push_back(std::move(piece.geometry));
        const int index = static_cast<int>(scene.meshes.size()) - 1;
        screen.quads.push_back(index);
        hudTints.emplace(index, piece.tint);
      }
      std::printf("  HUD: екран %-12s на %s (%s), шматків %zu\n", group, action,
                  std::string(controls.key(action)).c_str(), screen.quads.size());
      keyScreens.push_back(std::move(screen));
    }

    // Живі вузли: усе, що бере значення зі змінної, яку ми вміємо заповнити.
    for (const auto& node : ingameHud.nodes()) {
      const bool ticketText = node.group == "TicketInfo" &&
                              (node.textVariable == "FriendlyTicketsString" ||
                               node.textVariable == "EnemyTicketsString");
      const bool cpBar = node.type == obf2::hud::NodeType::Bar &&
                         node.group == "CPInformationItems" && !node.valueVariable.empty();
      // Напис посеред екрана: поки раунд чекає на гравців, текст у ньому
      // з'являється і зникає, тож пекти його наперед не можна.
      const bool centreMessage = node.textVariable == "DisconnectMessage";
      if (!ticketText && !cpBar && !centreMessage) continue;
      hudDynamic.push_back(DynamicNode{&node, {}, -1.0f, {}, false});
    }

    // --hud-screen list: що саме лягло на екран. Без цього доводиться
    // здогадуватися, який вузол з'їхав.
    if (args.hudScreenName == "list") {
      for (const auto& piece : pieces) {
        if (piece.node == nullptr) continue;
        std::printf("    %-10s %-28s %-22s %6.0f %6.0f %5.0f %5.0f  %s\n",
                    std::string(obf2::hud::nodeTypeName(piece.node->type)).c_str(),
                    piece.node->name.c_str(), piece.node->group.c_str(), piece.node->x,
                    piece.node->y, piece.node->width, piece.node->height,
                    piece.texture.c_str());
      }
    }

    std::printf("  HUD: %zu вузлів у дереві, шматків до малювання %zu, живих підписів %zu\n",
                ingameHud.nodes().size(), pieces.size(), hudDynamic.size());

    // Команди, яких ми ще не вміємо. Гра — це потік команд, тож найкорисніше
    // бачити саме те, що прийшло й лишилося без обробника.
    if (ingameHud.unknownCommands() > 0) {
      std::printf("  HUD: без реалізації %lld команд, унікальних %zu\n",
                  ingameHud.unknownCommands(), ingameHud.unknownByName().size());
      int shown = 0;
      for (const auto& [name, count] : ingameHud.unknownByName()) {
        if (shown++ >= 8) break;
        std::printf("    немає обробника: %-40s x%d\n", name.c_str(), count);
      }
      if (ingameHud.unknownByName().size() > 8) {
        std::printf("    ... решта — command_audit\n");
      }
    }
  }

  std::vector<obf2::gfx::GpuMesh> gpuMeshes(scene.meshes.size());
  std::vector<bool> uploadedOk(scene.meshes.size(), false);

  long long triangles = 0;
  for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
    auto uploaded = renderer->upload(scene.meshes[i], resolveTexture, &error);
    if (!uploaded) continue;
    gpuMeshes[i] = *uploaded;
    uploadedOk[i] = true;
    triangles += static_cast<long long>(scene.meshes[i].indices.size() / 3);
  }

  std::vector<obf2::gfx::MeshRenderer::DrawItem> items;
  items.reserve(scene.instances.size());
  long long drawnTriangles = 0;
  for (const auto& [meshIndex, transform] : scene.instances) {
    if (meshIndex < 0 || !uploadedOk[static_cast<std::size_t>(meshIndex)]) continue;
    items.push_back(obf2::gfx::MeshRenderer::DrawItem{
        &gpuMeshes[static_cast<std::size_t>(meshIndex)], transform});
    drawnTriangles += static_cast<long long>(scene.meshes[static_cast<std::size_t>(meshIndex)]
                                                 .indices.size() / 3);
  }

  std::printf("у GPU: %zu унікальних мешів (%lld трикутників), %zu примірників "
              "(%lld трикутників на кадр)\n  текстур %d, не знайдено %d\n",
              gpuMeshes.size(), triangles, items.size(), drawnTriangles, texturesLoaded,
              texturesMissing);

  // --- цикл -------------------------------------------------------------

  if (args.focus) scene.center = *args.focus;
  const float distance =
      args.distance > 0.0f ? args.distance : scene.radius * (level ? 1.6f : 2.6f);
  const float eyeHeight = distance * (level && !args.focus ? 0.3f : 0.35f);

  // Кути огляду живуть між кадрами: миша дає лише зміщення.
  float yaw = 0.0f;
  float pitch = -10.0f;
  // У меню миша не захоплюється — інакше курсором не потрапиш у кнопку.
  if (hostedServer) device->setRelativeMouse(true);
  // Детермінований знімок меню: ставимо курсор туди, куди попросили.
  if (args.mouseX >= 0.0f) {
    SDL_WarpMouseInWindow(device->window(), args.mouseX, args.mouseY);
  }

  int frame = 0;
  while (device->pumpEvents()) {
    // Сервер шле пінги й чекає відповіді: якщо мовчати кадр за кадром,
    // він нас відключить. Тому зв'язок крутиться разом із картинкою, а
    // чекання тримаємо коротким — інакше це були б завмирання.
    if (remote != nullptr) remote->pump(1);

    auto acquired = device->beginFrame();
    if (!acquired) continue;

    // --- камера ---
    obf2::Vec3f eye;
    obf2::Vec3f lookTarget = scene.center;

    if (hostedServer && hostedClient) {
      // Гра від першої особи: ввід іде на сервер, сервер рухає солдата,
      // а камера стоїть там, куди його поставив сервер. Тобто картинка
      // залежить від сервера навіть в одиночній грі.
      const auto raw = device->readInput();
      constexpr float kMouseSensitivity = 0.15f;
      // Кут росте за годинниковою стрілкою (ліва система), тож рух миші
      // вправо має його **збільшувати**. З правостороннім конвеєром знак
      // був протилежний, і після переходу керування виявилося дзеркальним.
      yaw += raw.mouseDeltaX * kMouseSensitivity;
      pitch -= raw.mouseDeltaY * kMouseSensitivity;
      pitch = std::max(-89.0f, std::min(89.0f, pitch));

      obf2::net::PlayerInput input;
      input.moveForward = raw.moveForward;
      input.moveRight = raw.moveRight;
      input.sprint = raw.sprint;
      input.jump = raw.jump;
      input.fire = raw.fire;
      input.yaw = yaw;
      input.pitch = pitch;
      hostedClient->setInput(input);

      const float step = 1.0f / 60.0f;
      hostedClient->tick(step);
      hostedServer->tick(step);

      eye = hostedClient->interpolatedPosition(localSoldierId);
      eye.y += 1.7f;  // зріст солдата: камера на рівні очей

      constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
      const float yawRadians = yaw * kToRadians;
      const float pitchRadians = pitch * kToRadians;
      // Нульовий кут дивиться вздовж +Z — так само, як рахує сервер.
      lookTarget = eye + obf2::Vec3f{std::sin(yawRadians) * std::cos(pitchRadians),
                                     std::sin(pitchRadians),
                                     std::cos(yawRadians) * std::cos(pitchRadians)};
    } else if (args.topDown) {
      eye = obf2::Vec3f{scene.center.x, scene.center.y + distance, scene.center.z};
    } else if (level && level->hasBeforeSpawnCamera) {
      // Поки гравець не з'явився, камера стоїть там, де сказав рівень:
      //
      //   gameLogic.setBeforeSpawnCamera -50/185/-285 -16/-3/0
      //
      // (Levels/<рівень>/Init.con). Перша трійка — місце, друга — поворот
      // у градусах. Доти ми просто крутили камеру навколо центра карти,
      // і вигляд не мав нічого спільного з грою.
      constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
      eye = level->beforeSpawnCameraPos;
      const float yawRadians = level->beforeSpawnCameraRot.x * kToRadians;
      const float pitchRadians = level->beforeSpawnCameraRot.y * kToRadians;
      lookTarget = eye + obf2::Vec3f{std::sin(yawRadians) * std::cos(pitchRadians),
                                     std::sin(pitchRadians),
                                     std::cos(yawRadians) * std::cos(pitchRadians)};
    } else {
      const float angle = static_cast<float>(frame) / 60.0f * 0.6f;
      eye = obf2::Vec3f{scene.center.x + std::sin(angle) * distance, scene.center.y + eyeHeight,
                        scene.center.z + std::cos(angle) * distance};
    }

    const float aspect =
        acquired->height == 0
            ? 1.0f
            : static_cast<float>(acquired->width) / static_cast<float>(acquired->height);
    // Ближня площина. Для гри вона має бути маленькою: інакше все, що
    // ближче за неї, зникає — і крізь стіну, до якої підійшов упритул,
    // видно наскрізь. Для оглядача моделей камера й так далеко, тому там
    // лишаємо пропорційну — вона дає кращу точність глибини.
    const bool firstPerson = hostedServer != nullptr;
    const float nearPlane = firstPerson ? 0.1f : scene.radius * 0.002f + 0.05f;

    // Далекість беремо з даних рівня: за кінцем туману видимість нульова,
    // тож малювати далі немає сенсу (Dalian: fogStartEndAndBase 0/610).
    const float fogFar = level ? level->terrain.fogEnd : 0.0f;
    const float farPlane = firstPerson && fogFar > 1.0f ? fogFar : scene.radius * 40.0f;

    const obf2::Mat4 projection = obf2::perspective(1.05f, aspect, nearPlane, farPlane);
    // Згори «вгору екрана» має бути не Y (він збігся б із поглядом), а Z.
    // У лівій системі вправо йде cross(up, forward), тож із up = +Z
    // праворуч опиняється +X — рівно як на власній мінімапі рівня.
    const obf2::Vec3f up =
        args.topDown && !bootMode ? obf2::Vec3f{0.0f, 0.0f, 1.0f} : obf2::Vec3f{0.0f, 1.0f, 0.0f};
    const obf2::Mat4 view = obf2::lookAt(eye, lookTarget, up);

    if (bootMode) {
      engine.update(1.0f / 60.0f);
      if (device->consumeSkip()) engine.skipMovie();

      // --- взаємодія з меню ---
      obf2::gfx::Device::InputState menuInput = device->readInput();
      // --mouse задає курсор напряму: у знімку вікно може не мати фокуса,
      // і SDL тоді не віддає реальної позиції.
      if (args.mouseX >= 0.0f) {
        menuInput.mouseX = args.mouseX;
        menuInput.mouseY = args.mouseY;
        if (args.click && frame == 1) menuInput.clicked = true;
      }
      const obf2::hud::Node* hovered = nullptr;

      if (engine.state() == obf2::engine::State::MainMenu) {
        menuScreen.width = static_cast<int>(acquired->width);
        menuScreen.height = static_cast<int>(acquired->height);

        // Курсор SDL віддає в координатах вікна, а кадр може бути більшим
        // через масштаб екрана — переводимо.
        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(device->window(), &windowWidth, &windowHeight);
        const float scaleX = windowWidth > 0
                                 ? static_cast<float>(acquired->width) / static_cast<float>(windowWidth)
                                 : 1.0f;
        const float scaleY = windowHeight > 0
                                 ? static_cast<float>(acquired->height) / static_cast<float>(windowHeight)
                                 : 1.0f;

        hovered = obf2::hud::buttonAt(menuHud, "MainMenu", menuScreen, menuInput.mouseX * scaleX,
                                      menuInput.mouseY * scaleY);
        if (args.verboseMenu && hovered != nullptr && hovered != lastHovered) {
          std::printf("  меню: курсор на %s -> %s\n", hovered->name.c_str(),
                      hovered->command.c_str());
        }
        lastHovered = hovered;
        if (hovered != nullptr && menuInput.clicked) {
          // Кнопка виконує консольну команду — той самий шлях, що й у грі.
          if (!engine.console().executeLine(hovered->command)) {
            std::printf("меню: команда без обробника — %s\n", hovered->command.c_str());
          }
        }
      }

      const bool loading = engine.state() == obf2::engine::State::Loading;
      const int quad = engine.state() == obf2::engine::State::Intro ? introQuad
                       : loading                                    ? loadingQuad
                                                                    : menuQuad;
      const std::vector<int>& overlay = loading ? loadingTextQuads : menuTextQuads;

      std::vector<obf2::gfx::MeshRenderer::DrawItem> screen;
      auto push = [&](int index) {
        if (index < 0 || !uploadedOk[static_cast<std::size_t>(index)]) return;
        screen.push_back(obf2::gfx::MeshRenderer::DrawItem{
            &gpuMeshes[static_cast<std::size_t>(index)], obf2::Mat4::identity()});
      };
      push(quad);
      if (engine.state() != obf2::engine::State::Intro) {
        // Підсвітка лягає під підпис кнопки, під якою курсор: у групі є
        // й повноекранне тло, тож малювати її просто першою не можна.
        const int highlight = [&]() {
          if (hovered == nullptr) return -1;
          const auto found = hoverQuads.find(hovered);
          return found == hoverQuads.end() ? -1 : found->second;
        }();
        for (std::size_t i = 0; i < overlay.size(); ++i) {
          if (highlight >= 0 && !loading && i < menuQuadNodes.size() &&
              menuQuadNodes[i] == hovered) {
            push(highlight);
          }
          push(overlay[i]);
        }
      }
      renderer->renderOverlay(*acquired, screen, obf2::gfx::Color{0.0f, 0.0f, 0.0f, 1.0f});
    } else {
      renderer->renderScene(*acquired, items, projection * view,
                            obf2::gfx::Color{0.42f, 0.55f, 0.68f, 1.0f});

      // HUD іде другим проходом поверх готового кадру — без очищення цілі.
      if (!hudQuads.empty() || !hudDynamic.empty()) {
        // Живі значення: квитки беремо просто з сервера, бо в одиночній грі
        // він тут-таки, поруч. Клієнту вони приїдуть окремим пакетом, коли
        // з'явиться стан раунду в мережі.
        if (hostedServer != nullptr) {
          const int own = 1, enemy = 2;
          hudStrings["FriendlyTicketsString"] = std::to_string(hostedServer->tickets(own));
          hudStrings["EnemyTicketsString"] = std::to_string(hostedServer->tickets(enemy));

          // Смужки прапорів під мінімапою: скільки точок у кожної команди.
          const auto& points = hostedServer->controlPoints();
          int ours = 0, theirs = 0;
          for (const auto& point : points) {
            if (point.team == own) ++ours;
            else if (point.team == enemy) ++theirs;
          }
          const float total = points.empty() ? 1.0f : static_cast<float>(points.size());
          hudValues["FriendlyCPs"] = static_cast<float>(ours) / total;
          hudValues["EnemyCPs"] = static_cast<float>(theirs) / total;

          // Напис посеред екрана, поки раунд чекає на гравців. Вузол для
          // нього — `GameInfo DisconnectMessage 0 200 800 40` зі змінними
          // DisconnectMessage / DisconnectMessageActive; сам текст гра
          // складає в коді (BF2.exe, 0x466f75): бере ключ
          // HUD_STARTOFROUND_NRPLAYERSNEEDED і підставляє число замість
          // мітки #NROFPLAYERS#.
          const int missing = hostedServer->settings().playersNeededToStart -
                              static_cast<int>(hostedServer->playerCount());
          if (missing > 0) {
            std::string text(engine.lexicon().text("HUD_STARTOFROUND_NRPLAYERSNEEDED"));
            const std::string mark = "#NROFPLAYERS#";
            if (const std::size_t at = text.find(mark); at != std::string::npos) {
              text.replace(at, mark.size(), std::to_string(missing));
            }
            hudStrings["DisconnectMessage"] = text;
            hudVariables["DisconnectMessageActive"] = true;
          } else {
            hudVariables["DisconnectMessageActive"] = false;
          }
        }

        std::vector<obf2::gfx::MeshRenderer::DrawItem> hudItems;
        hudItems.reserve(hudQuads.size() + hudDynamic.size());

        // Табло, рація й екран появи — поверх усього, поки тримають
        // клавішу. Порядок такий самий, як у грі: спершу бойовий HUD.
        std::vector<int> extra;
        for (const KeyScreen& screen : keyScreens) {
          const std::string_view key = controls.key(screen.action);
          const bool forced = screen.group == args.hudScreenName;
          if (!forced && (key.empty() || !device->isKeyDown(key))) continue;
          extra.insert(extra.end(), screen.quads.begin(), screen.quads.end());
        }

        const auto pushHud = [&](int index) {
          if (index < 0 || !uploadedOk[static_cast<std::size_t>(index)]) return;
          obf2::gfx::MeshRenderer::DrawItem item{&gpuMeshes[static_cast<std::size_t>(index)],
                                                 obf2::Mat4::identity()};
          const auto tint = hudTints.find(index);
          if (tint != hudTints.end()) {
            item.tint[0] = tint->second.r;
            item.tint[1] = tint->second.g;
            item.tint[2] = tint->second.b;
            item.tint[3] = tint->second.a;
          }
          hudItems.push_back(item);
        };
        for (const int index : hudQuads) pushHud(index);
        for (const int index : extra) pushHud(index);

        for (DynamicNode& dynamic : hudDynamic) {
          const obf2::hud::Node& node = *dynamic.node;
          const bool isBar = node.type == obf2::hud::NodeType::Bar;

          std::string text;
          float value = 0.0f;
          if (isBar) {
            const auto found = hudValues.find(node.valueVariable);
            value = found == hudValues.end() ? 0.0f : found->second;
          } else {
            const auto found = hudStrings.find(node.textVariable);
            if (found != hudStrings.end()) text = found->second;
          }

          const bool changed = isBar ? std::abs(value - dynamic.shownValue) > 0.001f
                                     : text != dynamic.shownText;
          if (changed) {
            // Значення змінилося — перебудовуємо тільки цей вузол.
            if (dynamic.valid) renderer->release(dynamic.mesh);
            dynamic.valid = false;
            dynamic.shownValue = value;
            dynamic.shownText = text;

            obf2::hud::Node copy = node;
            obf2::hud::Context single;
            if (isBar) {
              single.variableValue = [&](std::string_view) { return value; };
            } else {
              copy.text = text;
              copy.textVariable.clear();
            }
            if (isBar || !text.empty()) {
              auto built =
                  obf2::hud::buildNode(copy, hudFont.font, hudFont.atlasPath, hudScreen, single);
              if (!built.empty()) {
                if (auto uploaded = renderer->upload(built.front().geometry, resolveTexture)) {
                  dynamic.mesh = *uploaded;
                  dynamic.valid = true;
                }
              }
            }
          }
          if (dynamic.valid) {
            hudItems.push_back(
                obf2::gfx::MeshRenderer::DrawItem{&dynamic.mesh, obf2::Mat4::identity()});
          }
        }

        renderer->renderOverlay(*acquired, hudItems, obf2::gfx::Color{}, false);
      }
    }

    const bool lastFrame = args.frames > 0 && frame + 1 >= args.frames;
    if (lastFrame && !args.screenshot.empty()) {
      if (device->submitAndSave(*acquired, args.screenshot.c_str(), &error)) {
        std::printf("знімок: %s\n", args.screenshot.c_str());
      } else {
        std::fprintf(stderr, "%s\n", error.c_str());
      }
    } else {
      device->submit(*acquired);
    }

    if (frame == 0 && !bootMode) {
      std::printf("відсікання: намальовано %d, відсічено %d з %zu примірників\n",
                  renderer->drawnLastFrame(), renderer->culledLastFrame(), items.size());
    }

    if (menuQuit) break;
    // Рівень з меню: закінчуємо сесію меню й повертаємо вибір нагору.
    if (!requestedLevel.empty()) break;

    ++frame;
    if (args.frames > 0 && frame >= args.frames) break;
  }

  for (auto& dynamic : hudDynamic) {
    if (dynamic.valid) renderer->release(dynamic.mesh);
  }
  for (auto& gpuMesh : gpuMeshes) renderer->release(gpuMesh);
  std::printf("кадрів намальовано: %d\n", frame);
  if (nextLevel != nullptr) *nextLevel = requestedLevel;
  return 0;
}

int main(int argc, char** argv) {
  Args args = parseArgs(argc, argv);

  obf2::FileSystem files;
  std::vector<std::string> mountErrors;
  int mounted = 0;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    mounted += obf2::mountArchivesFromCon(files, args.modDir, args.modDir / list, &mountErrors);
  }
  files.mountDirectory(args.modDir);

  std::printf("OpenBattlefield2 | %s/%s\n", OBF2_PLATFORM_NAME, OBF2_ARCH_NAME);
  std::printf("мод: %s | архівів: %d\n", args.modDir.string().c_str(), mounted);
  for (const auto& e : mountErrors) std::printf("  [mount] %s\n", e.c_str());

  if (!args.calibrate.empty()) return runCalibrate(args, files);

  // Під'єднання до справжнього сервера — окремий режим: тут не потрібні
  // ні вікно, ні рівень.
  if (args.probe) return runProbe(args, files);

  // Гра на справжньому сервері: спершу доводимо рукостискання до того
  // місця, де сервер каже, який рівень він грає, і аж тоді відкриваємо
  // вікно — рівень нам призначає він, а не ми.
  if (!args.connectTo.empty()) {
    RemoteWorld remote(args, files);
    if (!remote.connect()) return 1;
    for (int i = 0; i < 40 && !remote.levelReady; ++i) remote.pump(500);
    if (!remote.levelReady) {
      std::fprintf(stderr, "сервер не сказав, який рівень він грає\n");
      remote.disconnect();
      return 1;
    }
    args.levelName = remote.levelName;

    // Дочекатися хоч кількох об'єктів: сервер шле їх слідом за рівнем, і
    // перший же прапор каже, куди дивитися. Інакше камера стоїть там,
    // де ми її поставили самі, — тобто ніде.
    for (int i = 0; i < 20 && remote.objects.size() < 4; ++i) remote.pump(200);
    if (!args.focus && !remote.objects.empty()) {
      const obf2::Vec3f at = remote.objects.begin()->second;
      std::printf("камера: біля об'єкта %u (%.0f %.0f %.0f)\n",
                  remote.objects.begin()->first, at.x, at.y, at.z);
      args.focus = at;
      if (args.distance <= 0.0f) args.distance = 60.0f;
    }

    const int code = runSession(args, files, nullptr, &remote);
    remote.report();
    remote.disconnect();
    return code;
  }

  // Меню й гра — це дві сесії поспіль: обраний у меню рівень просто
  // заводить наступну з іншими аргументами.
  while (true) {
    std::string nextLevel;
    const int code = runSession(args, files, &nextLevel);
    if (code != 0 || nextLevel.empty()) return code;

    std::printf("меню -> гра: %s\n", nextLevel.c_str());
    args.levelName = nextLevel;
    args.hosted = true;
    args.screen.clear();
  }
}
