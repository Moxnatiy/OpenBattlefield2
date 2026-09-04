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
#include "obf2/hud/spawn.h"
#include "obf2/hud/states.h"
#include "obf2/game/controls.h"
#include "obf2/game/scene.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/level/gameplay.h"
#include "obf2/level/level.h"
#include "obf2/server/game_client.h"
#include "obf2/server/physics.h"
#include "obf2/server/soldier_move.h"
#include <set>

#include "obf2/net/bf2_events.h"
#include "obf2/net/bf2_join.h"
#include "obf2/net/md5.h"
#include "obf2/net/bf2_protocol.h"
#include "obf2/net/udp.h"
#include "obf2/server/game_server.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/mesh/collision.h"
#include "obf2/mesh/primitives.h"
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
  // --own-box: намалювати заповнювач і на місці власного солдата. Сам по
  // собі він не потрібен у грі, але без нього заповнювач чужих солдатів
  // ніяк не перевірити на порожньому сервері.
  bool showOwnBox = false;
  std::string connectTo;      // --connect <хост[:порт]>: справжній сервер BF2
  // --probe: тільки розбір протоколу, без вікна. Без нього --connect
  // відкриває світ, як і належить клієнтові.
  bool probe = false;
  // --no-content: пропустити перевірку вмісту. Потрібне, щоб з'ясувати,
  // чи саме вона змушує сервер розірвати з'єднання.
  bool skipContent = false;
  bool skipDatabase = false;
  bool startSimulation = false;
  bool blockReady = false;
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
  // --hud-rects: виписати прямокутники всіх намальованих вузлів. Формат
  // такий самий, як у дампі кадру оригіналу, щоб їх можна було звірити
  // (tools/hud_coverage.py).
  bool hudRects = false;
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
    else if (flag == "--own-box") args.showOwnBox = true;
    else if (flag == "--connect" && i + 1 < argc) args.connectTo = argv[++i];
    else if (flag == "--probe") args.probe = true;
    else if (flag == "--no-content") args.skipContent = true;
    else if (flag == "--no-database") args.skipDatabase = true;
    else if (flag == "--start-sim") args.startSimulation = true;
    else if (flag == "--block-ready") args.blockReady = true;
    else if (flag == "--width" && i + 1 < argc) args.width = std::atoi(argv[++i]);
    else if (flag == "--height" && i + 1 < argc) args.height = std::atoi(argv[++i]);
    else if (flag == "--hud-screen" && i + 1 < argc) args.hudScreenName = argv[++i];
    else if (flag == "--hud-rects") args.hudRects = true;
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
// Константи руху солдата з даних гри. Потрібні обом шляхам: і власному
// серверу, і передбаченню руху на справжньому — інакше наше передбачення
// розходилося б із тим, що рахує сервер.
obf2::server::PhysicsConstants loadPhysics(obf2::FileSystem& files) {
  obf2::server::PhysicsConstants constants;
  obf2::engine::Console console;
  constants.bind(console);
  obf2::con::Interpreter interpreter(files,
                                     [&](const obf2::con::Command& c) { console.execute(c); });
  interpreter.runFile("objects/soldiers/common/common.con");
  return constants;
}

// Колізійний світ із розставлених об'єктів.
//
// Потрібен обом шляхам: власному серверу — щоб рухати тіла, і клієнту на
// справжньому сервері — щоб передбачення руху не провалювалося крізь
// підлогу будівлі. Доки він жив усередині гілки `--hosted`, клієнт знав
// лише терен.
std::unique_ptr<obf2::server::CollisionWorld> buildCollisionWorld(
    obf2::FileSystem& files, const obf2::game::Registry& registry,
    const std::vector<obf2::level::StaticObject>& objects) {
  auto world = std::make_unique<obf2::server::CollisionWorld>();
  int withCollision = 0, withoutCollision = 0;
  std::unordered_map<std::string, std::shared_ptr<obf2::mesh::CollisionMesh>> cache;

  for (const auto& object : objects) {
    auto cached = cache.find(object.templateName);
    if (cached == cache.end()) {
      std::shared_ptr<obf2::mesh::CollisionMesh> loaded;
      if (const auto* root = registry.find(object.templateName)) {
        // Ім'я меша зіткнень — окрема властивість шаблону.
        const std::string_view name = root->text("collisionMesh");
        if (!name.empty()) {
          const std::string path = resolveCollisionPath(files, root->file, std::string(name));
          if (!path.empty()) {
            if (const auto bytes = files.read(path)) {
              if (auto mesh = obf2::mesh::loadCollisionMesh(*bytes)) {
                loaded = std::make_shared<obf2::mesh::CollisionMesh>(std::move(*mesh));
              }
            }
          }
        }
      }
      cached = cache.emplace(object.templateName, std::move(loaded)).first;
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
    world->addLayer(*layer, transform);
    ++withCollision;
  }

  std::printf("  зіткнення: %d об'єктів, %zu трикутників у %zu комірках (без геометрії %d)\n",
              withCollision, world->triangleCount(), world->cellCount(), withoutCollision);
  return world;
}

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
  std::chrono::steady_clock::time_point lastKeepAlive = std::chrono::steady_clock::now();

  // Порядок приєднання живе окремо і має власний тест: усі правила про
  // паузи, очікування завантаження й зупинку на екрані появи — там
  // (`obf2/net/bf2_join.h`). Тут лишається тільки складання пакетів.
  obf2::net::bf2::JoinSequence join;
  std::string levelName;
  int blockOrdinal = 0;
  int pings = 0, dataPackets = 0, other = 0, challenges = 0;
  int eventCount = 0, objectCount = 0;
  std::vector<obf2::net::bf2::CreateSpawnGroup> spawnGroups;
  std::uint8_t lastServerSequence = 0;
  int ghostPackets = 0, ghostRecords = 0;
  int ghostFlagSet = 0, ghostFlagClear = 0, ghostUnparsed = 0;
  int ghostControlled = 0;
  int controlStates = 0;
  std::set<std::uint16_t> ghostObjects;
  std::size_t dataBytes = 0;
  std::uint8_t sequence = 0;
  std::uint8_t batch = 0;
  bool answered = false;


  // Гравець натиснув DONE і показав на прапор. Номер групи тут ще не
  // знаємо навмисно: групи приходять подіями після завантаження рівня, а
  // кнопку можна натиснути й раніше. Тому запам'ятовуємо **місце**, а
  // номер добираємо в мить відсилання — коли перелік уже точно є.
  void askSpawn(int team, int kit, float worldX, float worldZ, float worldSize) {
    chosenX = worldX;
    chosenZ = worldZ;
    chosenWorld = worldSize;
    havePoint = true;
    join.ask(obf2::net::bf2::JoinChoice{team, kit, 0});
  }

  // Те саме, але номер групи задано прямо. Це для безголового запуску:
  // там екрана появи немає, і номер приходить із командного рядка.
  void askSpawnGroup(int team, int kit, int group) {
    havePoint = false;
    directGroup = group;
    join.ask(obf2::net::bf2::JoinChoice{team, kit, group});
  }

  // Номер групи для обраного місця. Нуль — місця не обрали або сервер
  // про свої групи ще не сказав.
  std::uint16_t chosenGroupId(float* away = nullptr) const {
    if (!havePoint) return static_cast<std::uint16_t>(directGroup);
    return obf2::net::bf2::nearestSpawnGroup(spawnGroups, chosenX, chosenZ, chosenWorld, away);
  }

  int directGroup = 0;

  bool havePoint = false;
  float chosenX = 0.0f, chosenZ = 0.0f, chosenWorld = 2048.0f;
  // Ввід, який шлемо серверу. Тримаємо його тут, бо шле його цикл
  // кадрів, а складає — той, хто читає клавіатуру й мишу.
  obf2::net::bf2::PlayerAction action;
  std::uint32_t actionTick = 0;
  std::chrono::steady_clock::time_point lastAction = std::chrono::steady_clock::now();

  // Передбачення власного руху.
  //
  // Сервер не шле нам позицію нашого ж солдата щотакту — лише зрідка
  // виправляє (`PlayerControlObjectNetworkable::predict`). Якщо чекати
  // тих виправлень, рух виглядає як ривки раз на кілька десятих секунди,
  // а між ними солдат стоїть у повітрі там, де його лишило попереднє.
  // Тому рух рахуємо самі — тією ж фізикою, що й наш сервер, — а
  // виправлення від сервера приймаємо як істину.
  obf2::server::BodyState body;
  obf2::server::SwimState swim;
  obf2::server::TickAccumulator tick;
  bool bodyReady = false;
  int corrections = 0;
  float correctionSum = 0.0f;
  float correctionMax = 0.0f;
  obf2::Vec3f correctionAxis{};
  const obf2::level::Level* terrain = nullptr;
  const obf2::server::CollisionWorld* collision = nullptr;
  obf2::server::PhysicsConstants physics;
  float maxSpeed = 3.9f;  // phy-soldier-run-speed; заповнюється з даних гри

  // Виправлення від сервера: ставимо тіло туди, де його бачить сервер.
  void correct(const obf2::Vec3f& position) {
    // Наскільки ми розійшлися з сервером. Це міра якості передбачення:
    // поки розходження дрібне, різкої підміни місця не видно, і
    // згладжувати нема чого.
    if (bodyReady) {
      const obf2::Vec3f delta = position - body.position;
      const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
      correctionSum += distance;
      correctionMax = std::max(correctionMax, distance);
      // Окремо по осях: рівне розходження по одній осі — це не хиба
      // передбачення, а зсув точки відліку. Змішувати їх в одну довжину
      // означає не побачити, що саме розійшлося.
      correctionAxis.x += std::abs(delta.x);
      correctionAxis.y += delta.y;  // зі знаком: важливо, ми вище чи нижче
      correctionAxis.z += std::abs(delta.z);
      ++corrections;
      if (corrections <= 8 && terrain != nullptr) {
        std::printf("  виправлення: сервер %.2f %.2f %.2f, ми %.2f %.2f %.2f, "
                    "земля %.2f (dy %.2f)\n",
                    position.x, position.y, position.z, body.position.x, body.position.y,
                    body.position.z, terrain->groundHeightAt(position), delta.y);
      }
    }
    body.position = position;
    if (!bodyReady) body.velocity = obf2::Vec3f{};
    bodyReady = true;
  }

  // Один крок передбачення. `yaw` — куди дивиться гравець.
  void predict(float step, float yawDegrees) {
    if (!bodyReady || terrain == nullptr) return;
    constexpr float kToRadians = 3.14159265358979323846f / 180.0f;
    const float yaw = yawDegrees * kToRadians;

    // Осі в потоці дій — це ±99; нам потрібен напрямок 0..1. Вбік солдат
    // ходить віссю рискання: окремої осі для кроку вбік рушій не має.
    const float scale = 1.0f / static_cast<float>(obf2::net::bf2::kAxisFull);
    const float forward = static_cast<float>(action.axes[obf2::net::bf2::kAxisThrottle]) * scale;
    const float strafe = static_cast<float>(action.axes[obf2::net::bf2::kAxisYaw]) * scale;
    // Нульовий кут дивиться вздовж +Z — так само, як рахує сервер, тож
    // «вправо» це кут плюс 90 градусів.
    obf2::Vec3f wish{std::sin(yaw) * forward + std::cos(yaw) * strafe, 0.0f,
                     std::cos(yaw) * forward - std::sin(yaw) * strafe};
    const float magnitude = std::sqrt(wish.x * wish.x + wish.z * wish.z);
    if (magnitude > 1.0f) wish = wish * (1.0f / magnitude);

    const bool sprint = (action.buttons & obf2::net::bf2::kButtonSprint) != 0;
    const bool jump = (action.buttons & obf2::net::bf2::kButtonAction) != 0;
    const float speed = sprint ? physics.sprintSpeed : maxSpeed;

    // Рух — тією самою функцією, що й на сервері: земля, вода, стіни.
    // Поки в клієнта була власна скорочена копія, він знав лише висоту
    // землі — і солдат проходив крізь об'єкти.
    //
    // І тільки цілими тактами по 1/30 с (`WorldPref::mTickTime`). Крок
    // завдовжки з кадр робив рух і стрибок різними на 60 і на 120 кадрах.
    const int ticks = tick.take(step);
    for (int i = 0; i < ticks; ++i) {
      obf2::server::moveSoldier(body, swim, wish, speed, jump, physics, terrain, collision,
                                obf2::server::kTickTime);
    }
  }

  // Відіслати поточний ввід. Оригінал робить це тридцять разів на
  // секунду й кладе в пакет три останні набори — на випадок втрати.
  void sendActions() {
    if (socket == nullptr || ourSoldier == 0) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastAction < std::chrono::milliseconds(33)) return;
    lastAction = now;

    obf2::net::bf2::ExtendedHeader header;
    header.sequence = sequence++ & 0x3F;
    header.ack = lastServerSequence;
    header.ackBits = 0xFFFFFFFFu;
    // Три однакові набори — так само, як оригінал: пакет може загубитися,
    // і сусідній привезе те саме.
    obf2::net::bf2::PlayerActions stream;
    stream.tick = static_cast<std::int32_t>(actionTick++);
    stream.actions.assign(3, action);
    socket->send(obf2::net::bf2::writePlayerActions(id, header, stream));
  }

  // Наш номер гравця й команда — із `CreatePlayerEvent` за іменем.
  int ourPlayer = -1;
  int ourTeam = 0;
  // Об'єкти, створені сервером уже в грі: чужі солдати й техніка. Місце
  // тут — те, з яким об'єкт **створено**. Рухаються вони записами потоку
  // привидів, а їх ми ще не розбираємо, тож заповнювач стоятиме там, де
  // об'єкт з'явився. Це борг, і його видно на екрані.
  std::map<std::uint16_t, obf2::Vec3f> dynamicObjects;
  // Опорна точка для стиснених векторів: її дає стан керованого об'єкта,
  // і відносно неї пакуються місця всіх об'єктів у потоці привидів.
  obf2::Vec3f compressionReference;
  int positionUpdates = 0;

  // Об'єкт, яким ми керуємо. Його називає `EnterVehicleEvent`.
  std::uint16_t ourSoldier = 0;
  // Об'єкт, про який сервер шле стан керованого об'єкта. До появи це не
  // солдат, а камера екрана появи.
  std::uint16_t controlObject = 0;
  // Сервер сказав `NEPlayerSpawned`. Після цього керований об'єкт — уже
  // солдат: до появи солдата не існує, і `getSoldier` у рушії поверне
  // порожньо (0x445e01).
  bool playerSpawned = false;

  // Де зараз наш солдат. Порожньо — ще не з'явилися.
  std::optional<obf2::Vec3f> soldierPosition() const {
    if (ourSoldier == 0) return std::nullopt;
    // Показуємо передбачене місце, а не останнє виправлення: між
    // виправленнями минають десяті секунди, і без передбачення рух
    // виглядав би ривками.
    if (bodyReady) return body.position;
    const auto found = objects.find(ourSoldier);
    if (found == objects.end()) return std::nullopt;
    return found->second;
  }

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

  // Підтримати розмову, поки ми зайняті чимось довгим. Завантаження
  // рівня триває близько одинадцяти секунд, і весь цей час ми не
  // відповідали на пінги — сервер устигав нас відключити ще до того, як
  // ми казали `NELoadComplete`. Кроки ланцюжка тут не рухаємо навмисно:
  // потрібні лише пінги.
  //
  // Викликати не частіше, ніж раз на пів секунди: сокет неблокуючий, але
  // сам виклик усе одно коштує, а вантаження й так повільне.
  void keepAlive() {
    if (socket == nullptr) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastKeepAlive < std::chrono::milliseconds(500)) return;
    lastKeepAlive = now;
    for (int i = 0; i < 8; ++i) {
      const auto more = socket->receive(0);
      if (!more) break;
      const auto parsed = obf2::net::bf2::readPacket(*more);
      if (!parsed) continue;
      if (parsed->extended) lastServerSequence = parsed->extended->sequence;
      if (parsed->kind != obf2::net::bf2::PacketKind::PingRequest) continue;
      obf2::net::bf2::ExtendedHeader header;
      header.sequence = sequence++ & 0x3F;
      header.ack = lastServerSequence;
      header.ackBits = 0xFFFFFFFFu;
      socket->send(obf2::net::bf2::writePingResponse(id, header,
                                                     parsed->pingTime.value_or(0)));
      ++pings;
    }
  }

  // Один оберт: рухаємо ланцюжок появи і читаємо, що прийшло. Чекати
  // довго можна лише поза кадром — у кадрі це були б завмирання.
  // Один оберт розмови: відіслати те, що назріло, і **розібрати один
  // пакет**. Повертає, чи пакет був, — бо викликати це треба доти, доки
  // черга не спорожніє. Черга сокета не зникає сама: якщо за кадр брати
  // з неї один пакет, а сервер шле більше, вона росте, і ми дивимося на
  // світ таким, яким він був кілька секунд тому.
  bool pump(int timeoutMs) {
      // Коли слати наступний крок, вирішує JoinSequence — усі правила
      // про паузи й очікування там, разом із тестом. Тут лишається
      // скласти пакет і відзвітувати, що ми його відіслали.
      const auto now = std::chrono::steady_clock::now();
      if (const auto todo = join.next(now)) {
        obf2::net::bf2::ExtendedHeader next;
        next.sequence = sequence++ & 0x3F;
        next.ack = lastServerSequence;
        next.ackBits = 0xFFFFFFFFu;

        const auto event = [&](std::uint32_t number) {
          socket->send(obf2::net::bf2::writePostRemoteEvent(
              id, next, batch++, obf2::net::bf2::kNetworkCategory, number));
        };
        const auto eventWith = [&](std::uint32_t number, std::uint32_t value) {
          socket->send(obf2::net::bf2::writePostRemoteEvent(
              id, next, batch++, obf2::net::bf2::kNetworkCategory, number, value));
        };
        const auto& choice = join.choice();
        bool sent = true;

        switch (*todo) {
          case obf2::net::bf2::JoinStep::Level:
            event(obf2::net::bf2::kNetLoadComplete);
            std::printf("  крок: рівень завантажено\n");
            break;
          case obf2::net::bf2::JoinStep::Content: {
            const int ordinal = args.ordinal < 0 ? blockOrdinal : args.ordinal;
            const auto hashes = contentHashes(files, levelName, ordinal);
            if (!hashes) {
              std::printf("  перевірку вмісту пропущено: немає відбитків\n");
              break;
            }
            socket->send(obf2::net::bf2::writeContentCheckEvent(
                id, next, batch++, hashes->misc, hashes->archives, hashes->level));
            const auto show = [](const std::array<std::byte, 16>& hash) {
              std::string out;
              for (const auto byte : hash) {
                char pair[3];
                std::snprintf(pair, sizeof(pair), "%02x", std::to_integer<int>(byte));
                out += pair;
              }
              return out;
            };
            std::printf("  крок: перевірка вмісту, номер виклику %d\n    %s\n    %s\n    %s\n",
                        ordinal, show(hashes->misc).c_str(), show(hashes->archives).c_str(),
                        show(hashes->level).c_str());
            break;
          }
          case obf2::net::bf2::JoinStep::Database:
            event(obf2::net::bf2::kNetDatabaseComplete);
            std::printf("  крок: база гравців отримана\n");
            break;
          case obf2::net::bf2::JoinStep::Simulation:
            event(obf2::net::bf2::kNetStartSimulation);
            std::printf("  крок: почати відлік\n");
            break;
          case obf2::net::bf2::JoinStep::Team:
            eventWith(obf2::net::bf2::kNetSelectTeam, static_cast<std::uint32_t>(choice.team));
            std::printf("  крок: команда %d\n", choice.team);
            break;
          case obf2::net::bf2::JoinStep::Kit:
            eventWith(obf2::net::bf2::kNetSelectKit, static_cast<std::uint32_t>(choice.kit));
            std::printf("  крок: набір %d\n", choice.kit);
            break;
          case obf2::net::bf2::JoinStep::Group: {
            float away = 0.0f;
            const std::uint16_t wire = chosenGroupId(&away);
            eventWith(obf2::net::bf2::kNetSelectSpawnGroup, wire);
            std::printf("  крок: місце появи %u (за %.0f м, груп у переліку %zu)\n", wire, away,
                        spawnGroups.size());
            break;
          }
          case obf2::net::bf2::JoinStep::Ready:
          case obf2::net::bf2::JoinStep::Done:
            sent = false;
            break;
        }
        if (sent) join.commit(now);
      }

      const auto more = socket->receive(timeoutMs);
      if (!more) return false;
      const auto parsed = obf2::net::bf2::readPacket(*more);
      if (!parsed) return true;
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

          if (const auto flag = obf2::net::bf2::ghostFlag(*more)) {
            if (*flag) ++ghostFlagSet; else ++ghostFlagClear;
          } else {
            ++ghostUnparsed;
          }
          // Стан керованого об'єкта. З нього ми поки беремо **лише**
          // номер об'єкта: трійка чисел у ньому — опорна точка стиснення,
          // а не місце (див. bf2_events.h).
          if (const auto state = obf2::net::bf2::readControlObjectState(*more)) {
            if (controlStates < 3) {
              std::printf("  стан керованого: опора %.1f %.1f %.1f (лічильник %d, об'єкт %u)\n",
                          state->compressionReference.x, state->compressionReference.y,
                          state->compressionReference.z, state->counter, state->networkId);
            }
            ++controlStates;
            // Опорна точка стиснення на весь подальший потік.
            compressionReference = state->compressionReference;
            // Номер керованого об'єкта сервер каже прямо. Але керований
            // об'єкт — не завжди солдат: до появи це камера екрана появи
            // (на Dalian номер 257 із місцем `setBeforeSpawnCamera`).
            // Рушій розрізняє їх викликом `getSoldier` одразу після
            // `getObject`, а ми його ще не вміємо — тому номер поки лише
            // звіряємо з подією посадки, а не заміняємо ним її.
            if (state->networkId != controlObject) {
              controlObject = state->networkId;
              std::printf("  керований об'єкт: %u%s\n", controlObject,
                          (ourSoldier != 0 && controlObject != ourSoldier) ? " (не наш солдат!)"
                                                                          : "");
            }
            // Після появи керований об'єкт — це і є наш солдат. Подія
            // посадки каже те саме, але вона одна на всю гру й може не
            // дійти; а без номера ми не шлемо потоку дій — і тоді сервер
            // перестає слати нам стан, бо йому нема на що відповідати.
            if (playerSpawned && controlObject != 0 && ourSoldier != controlObject) {
              ourSoldier = controlObject;
              std::printf("  наш солдат за станом керованого об'єкта: %u\n", ourSoldier);
            }
            // Виправляти місце звідси нема чим: справжнє їде далі, у
            // стані самого об'єкта, а його ми ще не розбираємо. Доки не
            // розберемо, рух рахує тільки передбачення — від точки, яку
            // сервер назвав при створенні солдата. Це борг, а не рішення.
            // Тільки після `NEPlayerSpawned`: до появи солдата в нас
            // немає, а подія посадки трапляється й чужа.
            if (!bodyReady && playerSpawned && ourSoldier != 0) {
              const auto born = objects.find(ourSoldier);
              if (born != objects.end()) {
                correct(born->second);
                std::printf("  тіло поставлено на %.1f %.1f %.1f (місце створення солдата)\n",
                            born->second.x, born->second.y, born->second.z);
              }
            }
          }
          if (const auto ghost = obf2::net::bf2::readGhostHeader(*more)) {
            ++ghostPackets;
            // Прапорець «є стан керованого об'єкта» — це найпряміша
            // ознака, що сервер дав нам солдата: він означає, що в пакеті
            // їде стан саме того об'єкта, яким ми керуємо.
            if (ghost->controlObjectState) ++ghostControlled;
            if (ghostPackets <= 3) {
              std::printf("  привиди: час %u, записів %u%s\n", ghost->time, ghost->records,
                          ghost->controlObjectState ? ", є стан керованого об'єкта" : "");
            }
            for (const auto& record : obf2::net::bf2::readGhostRecords(*more,
                                                                      compressionReference)) {
              ++ghostRecords;
              ghostObjects.insert(record.networkId);
              // Об'єкт зник — вид 3.
              if (record.kind == 3) {
                dynamicObjects.erase(record.networkId);
                continue;
              }
              // Місце з оновлення стану. Тепер заповнювачі не стоять там,
              // де об'єкт створено, а їдуть за ним.
              if (record.position && record.networkId != ourSoldier) {
                dynamicObjects[record.networkId] = *record.position;
                ++positionUpdates;
              }
            }
          }

          // Розбираємо всі події з пакета: за таблицею розмірів кожну
          // можна пропустити рівно на її довжину, тож незнайомі типи не
          // збивають розбір наступних.
          for (const auto& event : obf2::net::bf2::readEvents(*more)) {
            ++eventCount;
            if (event.block) {
              const auto done = blocks.feed(*event.block);
              // Дослід: підтвердити зібраний блок подією NEDataBlockReady.
              // Справжній клієнт це, схоже, робить — сервер веде свій
              // облік того, що клієнт уже отримав.
              if (done && args.blockReady) {
                obf2::net::bf2::ExtendedHeader ack;
                ack.sequence = sequence++ & 0x3F;
                ack.ack = lastServerSequence;
                ack.ackBits = 0xFFFFFFFFu;
                socket->send(obf2::net::bf2::writePostRemoteEvent(
                    id, ack, batch++, obf2::net::bf2::kNetworkCategory,
                    obf2::net::bf2::kNetDataBlockReady,
                    static_cast<std::int32_t>(done->first)));
                std::printf("  блок %u зібрано, підтверджено\n", done->first);
              }
              // Блок 2 — справжній MapInfo. З нього беремо номер виклику:
              // сервер кидає його при завантаженні рівня і саме з тим
              // рядком відбитків звіряє нашу перевірку вмісту.
              if (done && done->first == obf2::net::bf2::kMapInfoNetBuffer) {
                if (const auto net = obf2::net::bf2::parseMapInfoNetBuffer(done->second)) {
                  std::printf("  сервер: місць %d, командир %s, номер виклику %d\n",
                              net->maxPlayers, net->commanderEnabled ? "є" : "нема",
                              net->challengeOrdinal);
                  if (args.ordinal < 0) blockOrdinal = net->challengeOrdinal;
                }
              }
              if (done && done->first == obf2::net::bf2::kMapInfoBlock && !levelReady) {
                if (const auto info = obf2::net::bf2::parseMapInfo(done->second)) {
                  std::printf("  сервер грає %s, режим %s, розмір %d, перше число %u\n",
                              info->levelName.c_str(), info->gameMode.c_str(), info->size,
                              info->first);
                  // Номер виклику сюди більше не лізе: він у блоці 2,
                  // а перше число блока 5 — це щось інше.
                  std::string levelError;
                  if (!obf2::level::mountLevel(files, args.modDir, info->levelName, &levelError)) {
                    std::printf("  рівень не змонтовано: %s\n", levelError.c_str());
                  } else {
                    known = buildKnownObjects(files, info->levelName, &levelError);
                    registry = buildRegistry(files);
                    std::printf("  рівень прочитано: відомих об'єктів %zu\n", known.size());
                  }
                  levelReady = true;
                  join.setLevelReady();
                  join.setSkipContent(args.skipContent);
                  join.setSkipDatabase(args.skipDatabase);
                  join.setSkipSimulation(!args.startSimulation);
                  levelName = info->levelName;
                }
              }
              continue;
            }
            if (event.remote) {
              const auto& remote = *event.remote;
              if (remote.category == obf2::net::bf2::kNetworkCategory) {
                std::printf("  сервер: подія %u%s\n", remote.number,
                            remote.value ? (" = " + std::to_string(*remote.value)).c_str() : "");
                if (remote.number == obf2::net::bf2::kNetPlayerSpawned) {
                  playerSpawned = true;
                  std::printf("  ГРАВЕЦЬ З'ЯВИВСЯ\n");
                }
              }
              continue;
            }
            if (event.spawnGroup) {
              const auto& group = *event.spawnGroup;
              spawnGroups.push_back(group);
              // Розмір світу беремо з рівня, бо саме ним сервер пакує
              // місце (GLSWorldSizeX/Z).
              const float worldSize = 2048.0f;
              std::printf(
                  "  група появи: номер %u, команда %u, мережевий %u, прапорці %d%d%d, "
                  "місце %.0f %.0f\n",
                  group.id, group.team, group.networkId, group.flag1 ? 1 : 0, group.flag2 ? 1 : 0,
                  group.flag3 ? 1 : 0,
                  obf2::net::bf2::spawnGroupWorldPos(group.worldX, worldSize),
                  obf2::net::bf2::spawnGroupWorldPos(group.worldZ, worldSize));
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
                // Об'єкт, якого немає в розстановці рівня, — це щось
                // живе: солдат іншого гравця або техніка, яку сервер
                // створив уже в грі. Ким саме він є, ми ще не знаємо:
                // зіставлення номера шаблона з іменем не розібране. Тому
                // запам'ятовуємо місце й показуємо заповнювачем.
                dynamicObjects[event.object->networkId] = at;
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
              // Свій номер гравця дізнаємося за іменем: сервер складає
              // його як «тег клану + пробіл + ім'я», тож порівнюємо
              // хвостом, а не цілим рядком.
              const std::string& name = event.player->name;
              const std::string& want = args.playerName;
              if (ourPlayer < 0 && !want.empty() && name.size() >= want.size() &&
                  name.compare(name.size() - want.size(), want.size(), want) == 0) {
                ourPlayer = static_cast<int>(event.player->id);
                ourTeam = static_cast<int>(event.player->team);
                std::printf("  це ми: номер %d, команда %d\n", ourPlayer, ourTeam);
              }
            }
            // Хто чим керує. Солдат у BF2 «займається» як техніка, і
            // саме цією подією сервер каже, який об'єкт наш.
            if (event.enter) {
              if (ourPlayer >= 0 && static_cast<int>(event.enter->player) == ourPlayer) {
                ourSoldier = event.enter->object;
                std::printf("  наш об'єкт: %u\n", ourSoldier);
              }
            }
            if (event.exitPlayer && ourPlayer >= 0 &&
                static_cast<int>(*event.exitPlayer) == ourPlayer) {
              ourSoldier = 0;
              std::printf("  ми вийшли з об'єкта\n");
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
            std::printf("  інший пакет: тип %d%s\n", static_cast<int>(parsed->kind),
                        parsed->kind == obf2::net::bf2::PacketKind::Disconnect
                            ? " (Disconnect!)" : "");
            // Розрив — не «інший пакет», а відповідь сервера. Показуємо
            // байти цілком: причина, якщо вона там є, лежить у них.
            if (parsed->kind == obf2::net::bf2::PacketKind::Disconnect) {
              std::string hex;
              std::string text;
              for (std::size_t k = 0; k < more->size() && k < 64; ++k) {
                char pair[4];
                const int byte = std::to_integer<int>((*more)[k]);
                std::snprintf(pair, sizeof(pair), "%02x ", byte);
                hex += pair;
                text += (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
              }
              std::printf("    крок на цю мить: %s, байтів %zu\n    %s\n    %s\n",
                          obf2::net::bf2::joinStepName(join.step()), more->size(), hex.c_str(),
                          text.c_str());
            }
          }
          break;
      }
      return true;
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
    std::printf("  прапорець привидів: стоїть %d, знято %d, не дочитали %d\n", ghostFlagSet,
                ghostFlagClear, ghostUnparsed);
    std::printf("  пакетів зі станом керованого об'єкта: %d, розібрано %d\n",
                ghostControlled, controlStates);
    if (corrections > 0) {
      const float n = static_cast<float>(corrections);
      std::printf("  виправлень від сервера: %d, розходження в середньому %.2f м, найбільше %.2f м\n",
                  corrections, correctionSum / n, correctionMax);
      std::printf("    по осях: x %.2f, y %.2f (зі знаком), z %.2f\n", correctionAxis.x / n,
                  correctionAxis.y / n, correctionAxis.z / n);
    }
    std::printf("  об'єктів, створених у грі (чужі солдати й техніка): %zu, "
                "оновлень місця з потоку привидів: %d\n",
                dynamicObjects.size(), positionUpdates);
    if (ourSoldier != 0) {
      std::printf("  наш об'єкт %u у записах привидів: %s\n", ourSoldier,
                  ghostObjects.count(ourSoldier) ? "є" : "немає");
    }
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
  // У пробі вантажити нема чого: вікна немає, сцени немає.
  remote.join.setClientLoaded();
  // Екрана появи теж немає, тож вибір задає командний рядок, і робимо
  // його одразу.
  remote.askSpawnGroup(args.team, args.kit, args.spawnGroup);
  // --frames тут задає, скільки обертів слухати: для коротких дослідів
  // (чи не розірве нас сервер на перевірці вмісту) вистачає тридцяти.
  const int loops = args.frames > 0 ? args.frames : 90;
  for (int i = 0; i < loops; ++i) remote.pump(500);
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
  // Геометрія зіткнень для передбачення руху на справжньому сервері.
  // Своїм сервером володіє він сам, а тут вона наша.
  std::unique_ptr<obf2::server::CollisionWorld> remoteCollision;
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

    if (remote != nullptr) remote->keepAlive();
    registry = buildRegistry(files);
    std::printf("  реєстр: %zu шаблонів (%.1f с)\n", registry.size(), secondsSince(started));
    if (remote != nullptr) remote->keepAlive();

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
        // Екран появи в нас є, тож обхід для безголових запусків тут не
        // потрібен: солдат з'явиться лише після DONE, як у рушії.
        serverSettings.spawnOnJoin = false;
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

      gameServer.setCollision(buildCollisionWorld(files, registry, collisionObjects));

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
      // Розставляння — найдовша частина завантаження. Поки воно триває,
      // сервер має чути, що ми живі.
      if (remote != nullptr) remote->keepAlive();
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
  // Копія контексту для перебудови живих вузлів у циклі малювання:
  // сам hudContext живе у блоці завантаження рівня.
  obf2::hud::Context hudDynamicContext;
  // Стан екрана появи та його геометрія. Він єдиний перебудовується на
  // ходу: вміст залежить від вибраного класу, команди й вкладки.
  int selectedKit = 0;
  int selectedTeam = 1;
  bool membersTab = false;
  bool spawnRequested = false;
  // Вибране місце появи — номер кружечка в spawnContext.spawnMarkers.
  //
  // Типово вибраний перший свій — інакше DONE не мав би чого слати, а
  // подія NESelectSpawnGroup із нулем для сервера означає «не обрано»
  // (`Player::getSpawnGroup() > 0`). **Джерело не знайдене:** яку саме
  // групу гра підставляє типово, ми не реверсили. Борг.
  int selectedSpawn = 0;
  // Номер контрольної точки для кожного кружечка, у тому ж порядку.
  // Саме його чекає сервер: у рушії гравець шле не координати, а номер
  // групи, і група — це набір точок одного прапора
  // (docs/functions/spawn.md).
  std::vector<int> spawnMarkerPoints;
  bool spawnDirty = false;
  struct OwnedPiece {
    obf2::gfx::GpuMesh mesh;
    obf2::hud::Color tint;
  };
  std::vector<OwnedPiece> spawnPieces;
  // Бойовий HUD: не запечений назавжди, а перебудовний — його змінні
  // рушій пише щокадру (0x78d0f0), а не раз при старті рівня.
  std::vector<OwnedPiece> ingamePieces;
  std::function<std::vector<obf2::hud::DrawPiece>()> buildIngamePieces;
  std::function<void()> rebuildIngame;
  bool hudDirty = false;
  bool ingameReported = false;
  std::function<void(bool, bool)> updateHudVariables;
  std::function<void(int)> applyHudState;
  // Що робить DONE. У власній грі це прямий запит до нашого сервера, у
  // мережевій — три події рушія поспіль (NESelectTeam, NESelectKit,
  // NESelectSpawnGroup, docs/functions/network-events.md).
  std::function<void(int team, int kit, int group)> requestSpawn;
  // Рухомі кутові ділянки. У `Menu/Ingame` їхнє X — це не стала, а
  // змінна графа, і у файлі збережене саме **сховане** положення:
  // BottomLeft_XPos = -295, BottomRight_XPos = 503. Показане для правої
  // теж є там же — BottomRight_oldXPos = 201. Веде їх
  // SetVariableSineAction зі швидкістю 600.
  //
  // Через це в оригіналі за екраном появи не видно широкої плашки під
  // здоров'ям: вузол BottomLeftBar (400x39, healthBackGround.tga) не має
  // жодної змінної показу, його ховає саме від'їзд ділянки.
  float bottomLeftX = -295.0f;
  float bottomRightX = 503.0f;
  float bottomLeftTarget = -295.0f;
  float bottomRightTarget = 503.0f;
  // Поява й зникнення вузлів у часі — те, чим у грі керує граф MemeFile
  // (див. obf2/hud/animation.h).
  obf2::hud::Animator hudAnimator;
  std::chrono::steady_clock::time_point lastAnimationTick = std::chrono::steady_clock::now();
  std::function<void()> rebuildSpawn;
  // Ці двоє потрібні перебудові, а вона викликається з циклу малювання —
  // тобто вже поза блоком завантаження рівня. Тримати їх усередині не
  // можна: посилання в лямбді стало б висячим.
  std::function<void()> applySpawnState;
  obf2::hud::Context spawnContext;
  // Той самий випадок, що й зі spawnContext: бойовий HUD тепер
  // перебудовується з циклу кадрів, а лямбда тримає контекст посиланням.
  // Поки він був місцевим у блоці налаштування, після виходу з блока
  // rebuildIngame читав уже мертву пам'ять — шлях до картинки карти
  // приходив сміттям, і мінікарта не малювалася.
  obf2::hud::Context hudContext;
  // Точки захоплення рівня — з них щоразу перебудовуються позначки на
  // карті: прапорець стоїть на кожній, а кружечок вибору місця появи —
  // лише на своїй.
  std::vector<obf2::level::ControlPoint> hudControlPoints;
  // Екрани, які видно, лише поки тримають клавішу: табло, рація, поява.
  // Геометрію печемо наперед — вона не змінюється, змінюється лише те,
  // чи малювати її цього кадру.
  struct KeyScreen {
    std::string group;
    std::string action;  // назва дії в ControlMap, не клавіша
    std::vector<int> quads;
    // Стан HUD, якому цей екран належить (docs/functions/hud-states.md).
    // Екран появи — стан 1, і в грі його **не тримають клавішею**: він
    // стоїть, доки гравець не з'явився. Табло — стан 9, і воно справді
    // на клавіші.
    int state = -1;
    bool heldByKey = true;
  };
  std::vector<KeyScreen> keyScreens;
  obf2::game::ControlMap controls;
  obf2::hud::VariableMap hudVariables;
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

    // Таблиця станів і похідні змінні живуть у `obf2/hud/states.h`
    // разом зі своїм тестом: там і сама таблиця з BF2.exe, і адреси
    // тих місць, що пишуть кожну змінну.
    applyHudState = [&](int state) {
      if (obf2::hud::applyState(hudVariables, state)) hudDirty = true;
    };

    // Бойовий HUD — це стан 0.
    applyHudState(0);

    // ToggleScore — вкладка «Гравці» на табло. Що вона типова, видно в
    // бінарі: у поле прапорця (Scoreboard+0x365) є рівно один запис
    // сталої, `movb $0x1, 0x365(%esi)` за 0x7a48f7.
    hudVariables["ToggleScore"] = true;

    // Джерело не знайдене: CPInterfaceEnabled (поле 0xa8 об'єкта HUD)
    // пишуть у грі багато місць, і котре з них наше — ще не з'ясовано.
    // Без нього не видно смуги точок захоплення. Борг.
    hudVariables["CPInterfaceEnabled"] = true;

    // --- екран появи: сім класів -------------------------------------
    //
    // Вузли Kit0..Kit6 у HudElementsSpawn.con нічого не показують самі:
    // кожен висить на своїй змінній, а вміст приходить теж змінними —
    // KitName<N>String (ключ підпису) і KitIcon<N>Path (піктограма).
    // Самий перелік — у `obf2/hud/spawn.h`.
    const auto& kKits = obf2::hud::spawnKits();

    // --- бекенд екрана появи ---------------------------------------
    //
    // Кнопка в HUD не має власної логіки: вона виконує консольну команду
    // з `setButtonNodeConCmd` (docs/functions/hud-commands.md). Для цього
    // екрана їх сім, і ось вони. Стан тримаємо тут-таки, а зміна вимагає
    // перебудови — геометрію ми печемо наперед.
    selectedKit = args.kit;
    selectedTeam = args.team == 2 ? 2 : 1;
    {
      obf2::engine::Console& console = engine.console();
      console.bind("spawnManager.setPlayerKit", [&](const obf2::con::Command& command) {
        selectedKit = command.argInt(0).value_or(selectedKit);
        spawnDirty = true;
      });
      console.bind("spawnManager.setPlayerTeam", [&](const obf2::con::Command& command) {
        selectedTeam = command.argInt(0).value_or(selectedTeam);
        spawnDirty = true;
      });
      console.bind("SpawnManager.toggleMembers", [&](const obf2::con::Command& command) {
        membersTab = command.argInt(0).value_or(0) != 0;
        spawnDirty = true;
      });
      console.bind("hudManager.setDone", [&](const obf2::con::Command& command) {
        spawnRequested = command.argInt(0).value_or(1) != 0;
        if (!spawnRequested) return;
        // Номер групи появи — це номер контрольної точки обраного
        // кружечка. Нуль означає «будь-яка своя», як і в сервері.
        const int group =
            selectedSpawn >= 0 && selectedSpawn < static_cast<int>(spawnMarkerPoints.size())
                ? spawnMarkerPoints[static_cast<std::size_t>(selectedSpawn)]
                : 0;
        std::printf("  екран появи: DONE — команда %d, набір %d, точка %d\n", selectedTeam,
                    selectedKit, group);
        if (requestSpawn) requestSpawn(selectedTeam, selectedKit, group);
      });
      console.bind("hudItems.setBool", [&](const obf2::con::Command& command) {
        // `hudItems.setBool <ім'я> <0|1>` — так інтерфейс вмикає свої ж
        // прапорці, зокрема SetSpawnPoint.
        if (command.args.size() >= 2) {
          hudVariables[std::string(command.argStr(0))] = command.argInt(1).value_or(0) != 0;
          spawnDirty = true;
        }
      });
      // Ці дві ще не мають за чим працювати, але команду треба з'їсти —
      // інакше консоль вважатиме її невідомою.
      console.bind("spawnManager.selectNextUnlock", [](const obf2::con::Command&) {});
      console.bind("spawnManager.commitSuicide", [](const obf2::con::Command&) {});
      console.bind("sound.playSound", [](const obf2::con::Command&) {});
    }

    // Похідні змінні HUD. У грі їх пише не список при старті, а дві
    // функції щокадру, і кожна тут названа своєю адресою:
    //
    //   0x466930 — розмір карти і те, що з нього випливає;
    //   0x78d0f0 — бойовий набір за поточним гравцем.
    //
    // Саме тому в оригіналі за екраном появи не видно смуг здоров'я й
    // набоїв: гравця ще немає, і 0x78d2d9 гасить увесь набір.
    updateHudVariables = [&](bool hasPlayer, bool mapFullSize) {
      if (obf2::hud::applyDerived(hudVariables, obf2::hud::WorldView{hasPlayer, mapFullSize})) {
        hudDirty = true;
      }
      // Куди їдуть рухомі ділянки. Сховані кінці — з `Menu/Ingame`:
      // -295 ліворуч і 503 праворуч. Праворуч висунуте положення
      // **виміряне** зі знімка кадру оригіналу — 336.5 (три вузли
      // сходяться, див. таблицю шарів нижче).
      //
      // Ліворуч висунуте положення **не виміряне**. Раніше тут стояло
      // -1, узяте з нерухомого шару BottomLeftStatic у тому ж файлі, —
      // це була помилка: до рухомого шару воно стосунку не має. При -1
      // плашка healthBackGround (400x39, у вузлі зсунута на -103)
      // тягнеться до x = 296, тобто накриває ще й місце під
      // транспортні смуги, і на екрані виглядає як у техніці. У файлі
      // ліворуч збережене лише сховане положення, обидва поля -295, тож
      // узяти правильне число нізвідки. Лишаємо -1 і чекаємо на
      // вимірювання.
      //
      // Джерело не знайдене і для того, **хто** вмикає від'їзд: у графі
      // це BottomLeft_nextXPos і BottomRight_direction, прив'язані до
      // полів об'єкта HUD (0x789480, шаблон «BottomLeft» + ім'я вузла),
      // але місце запису ще не знайдене. Поки веземо їх за тією ж
      // умовою, що й сам бойовий HUD.
      bottomLeftTarget = hasPlayer ? -1.0f : -295.0f;
      bottomRightTarget = hasPlayer ? 336.5f : 503.0f;
    };

    // Що робить DONE. Шляхи два, і обидва однаково «справжні»:
    //
    //   * власна гра — прямий запит до нашого сервера. Він поводиться
    //     як рушій: солдат з'являється лише коли обране місце появи;
    //   * справжній сервер BF2 — три події поспіль, NESelectTeam,
    //     NESelectKit, NESelectSpawnGroup. Порядок і паузи між ними
    //     перевірені на оригінальному сервері
    //     (docs/functions/network-events.md).
    requestSpawn = [&](int team, int kit, int group) {
      if (hostedServer != nullptr && !hostedServer->players().empty()) {
        hostedServer->requestSpawn(hostedServer->players().front().id, team, kit, group);
        return;
      }
      if (remote != nullptr) {
        // Серверу треба назвати **його** номер групи появи, а не наш
        // номер контрольної точки: на Dalian сервер шле 515..518, тоді як
        // у даних рівня прапори мають 401..404, і пов'язані вони ніяк.
        // Спільне в них тільки місце, тож передаємо місце — а номер
        // добере сам зв'язок, коли надійде час слати. Раніше ми добирали
        // його тут-таки і на швидкому натисканні отримували нуль: групи
        // приходять подіями вже після завантаження рівня.
        const obf2::level::ControlPoint* chosen = nullptr;
        for (const auto& point : hudControlPoints) {
          if (point.id == group) { chosen = &point; break; }
        }
        if (chosen == nullptr) {
          std::printf("  екран появи: точки %d немає в переліку рівня\n", group);
          return;
        }
        std::printf("  екран появи: точка %d (%s) на %.0f %.0f\n", group,
                    chosen->nameKey.c_str(), chosen->position.x, chosen->position.z);
        remote->askSpawn(team, kit, chosen->position.x, chosen->position.z,
                         hudContext.mapWorldSize);
        return;
      }
      std::printf("  екран появи: сервера немає, поява лише закриває екран\n");
    };

    // Назва сторони команди приходить із самого рівня:
    //   gameLogic.setTeamName 1 "CH"
    // Для Dalian_plant це CH і US — саме в такому порядку, тобто перша
    // команда китайська. По всіх 22 рівнях набір назв рівно CH, EU, MEC,
    // US, і теки значків у Menu_client.zip звуться так само.
    const auto teamName = [&](int team) -> std::string {
      if (!level || team < 0 || team > 2) return {};
      return level->teamNames[team];
    };
    // Перетворення назви сторони в ключі й шляхи живуть у
    // `obf2/hud/spawn.h` разом зі своїм тестом.
    const auto teamLabel = [&](int team) { return obf2::hud::armyLabelKey(teamName(team)); };
    const auto teamFlagIcon = [&](int team) { return obf2::hud::teamFlagIcon(teamName(team)); };

    // Вкладки команд угорі екрана появи. У даних гілка TeamSelectInfo
    // висить на Team1Selected, а всередині два блоки — Team1Selected і
    // Team2Selected.
    // Змінні екрана появи залежать від його стану, тож тримаємо їх в
    // одному місці й перераховуємо після кожної команди.
    applySpawnState = [&]() {
      // Стан HUD тут не чіпаємо: його ставить кадр за поточним станом
      // гри. Раніше ми ставили тут стан 1 — і після DONE екран появи
      // вмикав себе назад щоразу, коли перебудовувався.
      // Позначки карти залежать від команди, тож складаємо їх щоразу.
      // Прапорець стоїть на кожній точці, а кружечок вибору місця появи
      // — лише там, де точку тримає **наша** команда: у даних рівня
      // Dalian_plant це видно прямо, ObjectTemplate.team дає 1 для
      // powerplant, 2 для constructionsite, а reactors і mainentrance
      // нейтральні. З'явитися на чужій чи нічийній не можна.
      spawnContext.mapMarkers.clear();
      spawnContext.spawnMarkers.clear();
      spawnMarkerPoints.clear();
      for (const auto& point : hudControlPoints) {
        obf2::hud::Context::MapMarker marker;
        marker.worldX = point.position.x;
        marker.worldZ = point.position.z;
        marker.label = point.nameKey;
        marker.texture = obf2::hud::controlPointIcon(point.team == 0 ? "" : teamName(point.team));
        spawnContext.mapMarkers.push_back(std::move(marker));
        if (point.team == selectedTeam) {
          const bool chosen =
              static_cast<int>(spawnContext.spawnMarkers.size()) == selectedSpawn;
          spawnContext.spawnMarkers.push_back(
              obf2::hud::Context::SpawnMarker{point.position.x, point.position.z, chosen});
          spawnMarkerPoints.push_back(point.id);
        }
      }
      hudVariables["Team1Selected"] = selectedTeam != 2;
      hudVariables["Team2Selected"] = selectedTeam == 2;
      // Підписи й прапорці вкладок. У даних вони на змінних
      // Team1NameString / Team1FlagIconPathString (HudElementsSpawn.con),
      // а в бінарі їх заповнює одна й та сама 0x787260.
      for (int team = 1; team <= 2; ++team) {
        const std::string index = std::to_string(team);
        hudStrings["Team" + index + "NameString"] =
            std::string(engine.lexicon().text(teamLabel(team)));
        hudStrings["Team" + index + "FlagIconPathString"] = teamFlagIcon(team);
      }
      // Та сама функція ставить і пару «своя/чужа»: перший її аргумент —
      // команда гравця, другий — протилежна.
      hudStrings["FriendlyFlagIconPathString"] = teamFlagIcon(selectedTeam);
      hudStrings["EnemyFlagIconPathString"] = teamFlagIcon(selectedTeam == 2 ? 1 : 2);
      // Джерело не знайдене: у таблиці станів MapFullSizeAndSpawnShow
      // немає, а хто її вмикає в грі — ще не знайдено. Без неї кнопок
      // DONE і SUICIDE не видно. Борг.
      hudVariables["MapFullSizeAndSpawnShow"] = true;
      // KitsShow / MembersShow перемикає SpawnManager.toggleMembers —
      // це реверснута команда з hud-commands.md.
      hudVariables["KitsShow"] = !membersTab;
      hudVariables["MembersShow"] = membersTab;
      // Вибраний клас — наслідок spawnManager.setPlayerKit, теж
      // реверснутої команди.
      for (int slot = 0; slot < 7; ++slot) {
        hudVariables["PlayerKitIcon" + std::to_string(slot) + "SelectShow"] = slot == selectedKit;
      }
    };
    applySpawnState();
    for (int slot = 0; slot < static_cast<int>(std::size(kKits)); ++slot) {
      const std::string index = std::to_string(slot);
      // Джерело не знайдене: у грі Kit<N>Show вмикає логіка набору за
      // тим, які класи доступні. Ми вмикаємо всі сім. Борг.
      hudVariables["Kit" + index + "Show"] = true;
      hudStrings["KitName" + index + "String"] = kKits[slot].nameKey;
      hudStrings["KitIcon" + index + "Path"] = kKits[slot].icon;
      hudStrings["KitWeaponIcon" + index + "Path"] =
          std::string("Ingame/Weapons/Icons/Hud/Selection/") + kKits[slot].weapon;
      // Підсвітку вибраного ставить applySpawnState.
    }

    // Картинку карти рівня задає не HUD: у BF2.exe для неї є шаблон
    // `Levels/%s/Hud/Minimap/ingameMap.tga`.
    if (!args.levelName.empty()) {
      hudContext.mapTexture = "Levels/" + args.levelName + "/Hud/Minimap/ingameMap.tga";
    }
    // Гра показує не всю картинку рівня, а квадрат навколо бойової зони.
    // Правило знято з дампу кадру оригіналу: сторона квадрата — 1.2 від
    // більшого боку зони, центр — її центр. Для Dalian_plant 16 це дає
    // u 0.2694..0.7430 і v до 0.7677, а в оригіналі там 0.2703, 0.7431 і
    // 0.7678 — збіг до третього знака.
    //
    // Картинка орієнтована так, що z росте вгору: v = (1024 - z) / 2048.
    const auto hudGameplay =
        level ? obf2::level::loadGameplayObjects(files, level->name, "gpm_cq", 16)
              : std::optional<obf2::level::GameplayObjects>{};
    if (hudGameplay && !hudGameplay->combatArea.empty()) {
      float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
      hudGameplay->combatArea.bounds(minX, maxX, minZ, maxZ);
      const float world = static_cast<float>(level ? level->primary.size - 1 : 1024) *
                          (level ? level->primary.scale.x : 2.0f);
      const float half = std::max(maxX - minX, maxZ - minZ) * 0.5f * 1.2f;
      const float centerX = (minX + maxX) * 0.5f;
      const float centerZ = (minZ + maxZ) * 0.5f;
      const auto toU = [&](float x) { return (x + world * 0.5f) / world; };
      const auto toV = [&](float z) { return (world * 0.5f - z) / world; };
      hudContext.mapU0 = toU(centerX - half);
      hudContext.mapU1 = toU(centerX + half);
      hudContext.mapV0 = toV(centerZ + half);
      hudContext.mapV1 = toV(centerZ - half);
      hudContext.mapWorldSize = world;
      std::printf("  карта: бойова зона %.0f..%.0f / %.0f..%.0f, видно u %.3f..%.3f v %.3f..%.3f\n",
                  minX, maxX, minZ, maxZ, hudContext.mapU0, hudContext.mapU1, hudContext.mapV0,
                  hudContext.mapV1);

      // Точки захоплення: місце, команда і ключ назви беремо з рівня —
      // рівно те саме, що бачить сервер. Самі позначки складає
      // applySpawnState, бо вони залежать від команди гравця.
      hudControlPoints = hudGameplay->controlPoints;
      // Основний HUD запікається один раз, тож прапорці для його
      // мінікарти складаємо тут-таки. Кружечків вибору там немає — вони
      // лише на екрані появи.
      for (const auto& point : hudControlPoints) {
        obf2::hud::Context::MapMarker marker;
        marker.worldX = point.position.x;
        marker.worldZ = point.position.z;
        marker.label = point.nameKey;
        marker.texture = obf2::hud::controlPointIcon(point.team == 0 ? "" : teamName(point.team));
        hudContext.mapMarkers.push_back(std::move(marker));
      }
      std::printf("  карта: точок захоплення %zu\n", hudControlPoints.size());
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
      // Частина шрифтів лежить у мовних теках, а не в корені: наприклад
      // StandardTextBold_15 є тільки як English/StandardTextBold_15.
      // Тому пробуємо чотири місця — мовне й загальне, кожне з `800`
      // (набір для 800x600) і без нього.
      // Порядок саме такий: спершу мовна тека, і **без** підтеки `800`.
      // Знімок кадру оригіналу на 800x600 показує підпис класу шириною
      // 79.2 при висоті 11, а набір із `800` дав би 60.3 на 9 — бо там
      // кегль 13 проти 16 у корені мовної теки. Тобто `800` призначений
      // не для 800x600, як здавалося з назви.
      LoadedFont loaded = loadFont(files, dir + "English/" + name);
      if (!loaded.valid) loaded = loadFont(files, dir + "English/800/" + name);
      if (!loaded.valid) loaded = loadFont(files, key);
      if (!loaded.valid) loaded = loadFont(files, dir + "800/" + name);
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

    hudDynamicContext = hudContext;

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
    // --hud-rects: той самий формат, що й у дампі кадру оригіналу.
    const auto reportRects = [&](const char* where,
                                 const std::vector<obf2::hud::DrawPiece>& pieces) {
      if (!args.hudRects) return;
      for (const obf2::hud::DrawPiece& piece : pieces) {
        if (piece.node == nullptr || piece.geometry.vertices.empty()) continue;
        // Габарит рахуємо з самої геометрії, а не з прямокутника вузла:
        // так підписи зіставні з дампом оригіналу, де теж стоїть обвід
        // намальованого рядка, а не рамка вузла.
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& vertex : piece.geometry.vertices) {
          const float px = (vertex.position.x + 1.0f) * 0.5f * hudScreen.width;
          const float py = (1.0f - vertex.position.y) * 0.5f * hudScreen.height;
          x0 = std::min(x0, px);
          y0 = std::min(y0, py);
          x1 = std::max(x1, px);
          y1 = std::max(y1, py);
        }
        std::printf("RECT %-14s %-30s %7.1f %7.1f %7.1f %7.1f %-46s [%s]\n", where,
                    piece.node->name.c_str(), x0, y0, x1 - x0, y1 - y0,
                    piece.texture.c_str(), piece.node->showVariable.c_str());
      }
    };

    // Бойовий HUD теж перебудовний. У грі його змінні пише не один раз
    // при старті рівня, а щокадру — див. docs/functions/hud-states.md,
    // розділ про об'єкт HUD: 0x78d0f0 бере поточного гравця і або вмикає
    // PlayerHealthShow (0x78d154), або гасить весь набір (0x78d2d9).
    // Тому дерево доводиться складати наново, а не пекти назавжди.
    buildIngamePieces = [&]() {
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
             Layer{"BottomLeftAnimate", bottomLeftX, 563.0f, obf2::hud::Anchor::Left},
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
             Layer{"BottomRightAnimate", bottomRightX, 497.0f, obf2::hud::Anchor::Right},
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
      reportRects(layer.group, layerPieces);
      if (!ingameReported) {
        std::printf("  HUD: шар %-20s кут %.0f %.0f, шматків %zu\n", layer.group, layer.x,
                    layer.y, layerPieces.size());
      }
      for (auto& piece : layerPieces) pieces.push_back(std::move(piece));
    }

    reportRects("Global", pieces);
    return pieces;
    };

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
      int state = -1;
      bool heldByKey = true;
    };
    for (const KeyScreenSetup& setup : {
             KeyScreenSetup{"Scoreboard", "c_GIShowScoreboard", "ScoreboardShow", nullptr,
                            obf2::hud::MapView::Mini, 9, true},
             KeyScreenSetup{"RadioRose", "c_GIRadioComm", "RadioInterfaceShow"},
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
      // Звіт складаємо ДО повернення подання: інакше вузол карти вже
      // мірявся б мініатюрою, хоча в екран запеклася велика.
      if (!built.empty()) reportRects(group, built);
      // Повертаємо мініатюру: основний HUD міряється саме нею.
      if (setup.mapView != obf2::hud::MapView::Mini) {
        ingameHud.setMapView(obf2::hud::MapView::Mini);
      }
      if (built.empty()) continue;
      KeyScreen screen;
      screen.group = group;
      screen.action = action;
      screen.state = setup.state;
      screen.heldByKey = setup.heldByKey;
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

    // Екран появи будуємо окремо й тримаємо його меші при собі: після
    // кожної команди (вибір класу, команди, вкладки) він перебудовується,
    // а решта екранів лишається запеченою назавжди.
    spawnContext = hudContext;
    spawnContext.isVisible = [&](std::string_view variable) {
      if (variable == "1" || variable == "SpawnShow") return true;
      const auto found = hudVariables.find(std::string(variable));
      return found != hudVariables.end() && found->second;
    };
    // Ефекти появи бачить і сам будівник геометрії: alpha множить
    // прозорість, move зсуває прямокутник.
    spawnContext.showState = [&](const obf2::hud::Node& node) { return hudAnimator.state(node); };
    rebuildIngame = [&]() {
      for (OwnedPiece& piece : ingamePieces) renderer->release(piece.mesh);
      ingamePieces.clear();
      for (const char* root : {"Global", "BottomLeftAnimate", "BottomLeftStatic",
                               "BottomRightAnimate", "BottomRightStatic"}) {
        obf2::hud::updateAnimator(ingameHud, root, hudAnimator, hudContext);
      }
      for (auto& piece : buildIngamePieces()) {
        if (auto uploaded = renderer->upload(piece.geometry, resolveTexture)) {
          ingamePieces.push_back(OwnedPiece{*uploaded, piece.tint});
        }
      }
      if (ingameReported) {
        std::printf("  HUD: бойовий перебудовано, шматків %zu\n", ingamePieces.size());
      }
      ingameReported = true;
    };
    hudContext.showState = [&](const obf2::hud::Node& node) { return hudAnimator.state(node); };
    rebuildIngame();

    rebuildSpawn = [&]() {
      for (OwnedPiece& piece : ingamePieces) renderer->release(piece.mesh);
  for (OwnedPiece& piece : spawnPieces) renderer->release(piece.mesh);
      spawnPieces.clear();
      applySpawnState();
      for (const char* root : {"SpawnMenu", "MapSplit", "TopLayer"}) {
        obf2::hud::updateAnimator(ingameHud, root, hudAnimator, spawnContext);
      }
      ingameHud.setMapView(obf2::hud::MapView::Maxi);
      auto built = obf2::hud::buildTree(ingameHud, "SpawnMenu", hudFont.font, hudFont.atlasPath,
                                        hudScreen, spawnContext);
      auto mapPieces = obf2::hud::buildTree(ingameHud, "MapSplit", hudFont.font,
                                            hudFont.atlasPath, hudScreen, spawnContext);
      const std::size_t mapCount = mapPieces.size();
      for (auto& piece : mapPieces) built.push_back(std::move(piece));
      // TopLayer — це справжня ділянка з даних (`createSplitNode TopLayer
      // TopLayerHud` у GeneralHudSettings.con), і саме в ній лежать
      // кнопки DONE та SUICIDE: MapButtons -> DoneButton 666 539 124 17.
      for (auto& piece : obf2::hud::buildTree(ingameHud, "TopLayer", hudFont.font,
                                              hudFont.atlasPath, hudScreen, spawnContext)) {
        built.push_back(std::move(piece));
      }
      reportRects("SpawnMenu", built);
      ingameHud.setMapView(obf2::hud::MapView::Mini);
      for (auto& piece : built) {
        if (auto uploaded = renderer->upload(piece.geometry, resolveTexture)) {
          spawnPieces.push_back(OwnedPiece{*uploaded, piece.tint});
        }
      }
      std::printf("  HUD: екран появи перебудовано, шматків %zu (карта %zu), кружечків %zu\n",
                  spawnPieces.size(), mapCount, spawnContext.spawnMarkers.size());
    };
    rebuildSpawn();

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
      for (const auto& piece : buildIngamePieces()) {
        if (piece.node == nullptr) continue;
        std::printf("    %-10s %-28s %-22s %6.0f %6.0f %5.0f %5.0f  %s\n",
                    std::string(obf2::hud::nodeTypeName(piece.node->type)).c_str(),
                    piece.node->name.c_str(), piece.node->group.c_str(), piece.node->x,
                    piece.node->y, piece.node->width, piece.node->height,
                    piece.texture.c_str());
      }
    }

    std::printf("  HUD: %zu вузлів у дереві, шматків до малювання %zu, живих підписів %zu\n",
                ingameHud.nodes().size(), ingamePieces.size(), hudDynamic.size());

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

  // Заповнювач для чужих солдатів. Розмір — не на око: це колізійна форма
  // солдата з даних гри (`coll-soldier-radius` 0.25 і
  // `coll-soldier-stand-height` 1.7), тобто 0.5 x 1.7 x 0.5.
  obf2::gfx::GpuMesh enemyBox;
  bool enemyBoxReady = false;
  if (remote != nullptr) {
    const obf2::server::PhysicsConstants& shape = remote->physics;
    const obf2::mesh::RenderMesh box = obf2::mesh::buildBox(
        obf2::mesh::Vec3{shape.radius * 2.0f, shape.standHeight, shape.radius * 2.0f}, "#c03030");
    if (auto uploaded = renderer->upload(box, resolveTexture)) {
      enemyBox = *uploaded;
      enemyBoxReady = true;
    }
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

  // Рівень у GPU — тепер можна сказати серверові, що ми завантажилися,
  // і далі відповідати на пінги щокадру.
  if (remote != nullptr) {
    remote->join.setClientLoaded();
    // Передбаченню руху потрібен той самий терен і ті самі константи, що
    // й серверу: інакше воно розходилося б із ним щокроку.
    remote->terrain = level ? &*level : nullptr;
    remote->physics = loadPhysics(files);
    remote->maxSpeed = remote->physics.runSpeed;
    if (level) {
      remoteCollision = buildCollisionWorld(files, registry, level->objects);
      remote->collision = remoteCollision.get();
    }
    std::printf("  зв'язок: рівень завантажено, продовжуємо розмову\n");
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
  // Захоплення миші вмикається не тут, а щокадру за станом HUD: у бою
  // так, на екрані появи ні (див. wantRelativeMouse нижче).
  bool relativeMouse = false;
  // Детермінований знімок меню: ставимо курсор туди, куди попросили.
  if (args.mouseX >= 0.0f) {
    SDL_WarpMouseInWindow(device->window(), args.mouseX, args.mouseY);
  }

  int frame = 0;
  // Скільки часу минуло від попереднього кадру. Передбачення руху має
  // рахувати саме його: із твердою 1/60 солдат ішов би повільніше за
  // камеру на швидкій машині й швидше на повільній, і рух смикався б
  // рівно настільки, наскільки кадри нерівні.
  auto lastFrameStart = std::chrono::steady_clock::now();
  float frameStep = 1.0f / 60.0f;
  while (device->pumpEvents()) {
    {
      const auto nowFrame = std::chrono::steady_clock::now();
      frameStep = std::chrono::duration<float>(nowFrame - lastFrameStart).count();
      lastFrameStart = nowFrame;
      // Довгий кадр (завантаження, вікно перетягли) не має перетворитися
      // на стрибок через півкарти.
      frameStep = std::min(frameStep, 0.1f);
    }
    // Сервер шле пінги й чекає відповіді: якщо мовчати кадр за кадром,
    // він нас відключить. Тому зв'язок крутиться разом із картинкою, а
    // чекання тримаємо коротким — інакше це були б завмирання.
    if (remote != nullptr) {
      // Вибираємо **всю** чергу, а не один пакет за кадр. Інакше вона
      // росте: сервер шле привидів частіше, ніж ми малюємо кадри, і
      // кожне «наше місце» приходить із запізненням, яке накопичується.
      // Саме це виглядало як гігантська затримка й ривки: ми ставили
      // солдата туди, де він був кілька секунд тому.
      remote->pump(1);
      while (remote->pump(0)) {
      }
      // Ввід іде окремо від решти розмови й зі своєю частотою.
      remote->sendActions();
    }

    auto acquired = device->beginFrame();
    if (!acquired) continue;

    // Власна пара клієнт-сервер крутиться щокадру незалежно від того,
    // чи гравець уже з'явився: інакше сервер не встиг би виконати сам
    // запит на появу.
    if (hostedServer && hostedClient) {
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

      // Солдат з'являється не при під'єднанні, а після DONE, тож його
      // номер доводиться перепитувати щокадру.
      const std::uint32_t hadSoldier = localSoldierId;
      for (const auto& player : hostedServer->players()) localSoldierId = player.soldierId;
      if (hadSoldier == 0 && localSoldierId != 0) {
        for (const auto& object : hostedServer->objects()) {
          if (object.id != localSoldierId) continue;
          std::printf("  поява: солдат %u на %.1f %.1f %.1f\n", object.id, object.position.x,
                      object.position.y, object.position.z);
        }
      }
    }

    // --- камера ---
    obf2::Vec3f eye;
    obf2::Vec3f lookTarget = scene.center;

    // Доки гравець не з'явився, від першої особи дивитися нема з чого:
    // солдата ще немає. Тоді працює камера екрана появи з Init.con.
    const auto remoteSoldier =
        remote != nullptr ? remote->soldierPosition() : std::optional<obf2::Vec3f>{};
    if ((hostedServer && hostedClient && localSoldierId != 0) || remoteSoldier) {
      // Місце солдата дає той, хто ним володіє: у власній грі наш сервер,
      // на справжньому — той сервер.
      eye = remoteSoldier ? *remoteSoldier : hostedClient->interpolatedPosition(localSoldierId);
      eye.y += 1.7f;  // зріст солдата: камера на рівні очей

      // На справжньому сервері ввід іде туди ж, куди й у власній грі, —
      // тільки в іншому вигляді: потоком дій гравця.
      if (remoteSoldier && remote != nullptr) {
        const auto raw = device->readInput();
        constexpr float kMouseSensitivity = 0.15f;
        yaw += raw.mouseDeltaX * kMouseSensitivity;
        pitch -= raw.mouseDeltaY * kMouseSensitivity;
        pitch = std::max(-89.0f, std::min(89.0f, pitch));

        obf2::net::bf2::PlayerAction& out = remote->action;
        out = obf2::net::bf2::PlayerAction{};
        // Повний хід уперед у дампі — 99, тож наш ±1 множимо на нього.
        out.axes[obf2::net::bf2::kAxisThrottle] =
            static_cast<std::int16_t>(raw.moveForward * obf2::net::bf2::kAxisFull);
        // Крок вбік — вісь рискання: у Controls.con D/A висять саме на
        // `c_PIYaw`, окремої осі для кроку вбік рушій не має.
        out.axes[obf2::net::bf2::kAxisYaw] =
            static_cast<std::int16_t>(raw.moveRight * obf2::net::bf2::kAxisFull);
        out.axes[obf2::net::bf2::kAxisMouseX] = static_cast<std::int16_t>(raw.mouseDeltaX);
        out.axes[obf2::net::bf2::kAxisMouseY] = static_cast<std::int16_t>(raw.mouseDeltaY);
        if (raw.sprint) out.buttons |= obf2::net::bf2::kButtonSprint;
        if (raw.jump) out.buttons |= obf2::net::bf2::kButtonAction;
        if (raw.fire) out.buttons |= obf2::net::bf2::kButtonFire;

        // І одразу рахуємо власний рух: чекати на виправлення сервера
        // означало б ривки раз на кілька десятих секунди.
        remote->predict(frameStep, yaw);
        eye = *remote->soldierPosition();
        eye.y += 1.7f;
      }

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
    const bool firstPerson = hostedServer != nullptr || remoteSoldier.has_value();
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
      // Чужі об'єкти домальовуємо до готового переліку: сцена рівня
      // складається один раз, а ці з'являються й зникають у грі.
      const std::vector<obf2::gfx::MeshRenderer::DrawItem>* toDraw = &items;
      std::vector<obf2::gfx::MeshRenderer::DrawItem> withOthers;
      if (remote != nullptr && enemyBoxReady && !remote->dynamicObjects.empty()) {
        withOthers = items;
        for (const auto& [id, at] : remote->dynamicObjects) {
          if (id == remote->ourSoldier && !args.showOwnBox) continue;  // себе зсередини не малюємо
          withOthers.push_back(
              obf2::gfx::MeshRenderer::DrawItem{&enemyBox, obf2::translation(at)});
        }
        toDraw = &withOthers;
      }
      renderer->renderScene(*acquired, *toDraw, projection * view,
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
        // Стан HUD. У грі це не набір клавіш, а машина на 32 позиції
        // (docs/functions/hud-states.md): стан 1 — екран появи, і він
        // стоїть сам, доки гравець не з'явився; стан 9 — табло, воно
        // справді на клавіші. Ми поки розрізняємо саме ці два випадки.
        // DONE закриває екран появи. У грі кнопка не «ховає меню», а
        // просить сервер про появу, і стан перемикається вже за фактом
        // появи гравця; поки сервер цього не вміє, закриваємо самі —
        // інакше решту HUD не подивитися. Борг.
        // «Гравець є» — це не «клієнт під'єднаний», а «сервер поставив
        // йому солдата». Саме цим у рушії керується бойовий HUD
        // (0x78d0f0 бере поточного гравця, і без нього гасить набір).
        const bool spawned =
            hostedServer != nullptr ? localSoldierId != 0 : spawnRequested;
        const int hudState = spawned ? 0 : 1;
        const bool spawnVisible = hudState == 1 || args.hudScreenName == "SpawnMenu";

        // Миша: у бою її захоплює вікно (інакше курсор упирається в край
        // екрана й огляд просто зупиняється — саме це виглядало як
        // «керування не працює»), а на екрані появи вона вільна, бо там
        // нею тиснуть кнопки. Досі захоплення вмикалося лише у власній
        // грі, і на справжньому сервері огляд ламався завжди.
        const bool wantRelativeMouse = spawned && !spawnVisible;
        if (wantRelativeMouse != relativeMouse) {
          relativeMouse = wantRelativeMouse;
          device->setRelativeMouse(relativeMouse);
        }
        // Стан HUD і похідні від нього змінні — щокадру, як у грі
        // (0x786260 перемикає стан, 0x466930 і 0x78d0f0 рахують похідні).
        // Карта на весь екран — це саме екран появи: у стані 1 сам
        // обробник вмикає MapBorderAlternateShow, а той у 0x4669ae
        // дорівнює запереченню MapMinSize.
        // Команду призначає сервер, а не наш вибір: у знятому трафіку
        // оригінальний клієнт `NESelectTeam` навіть не шле — приймає ту,
        // яку дав сервер у CreatePlayerEvent. Тож щойно ми її дізналися,
        // екран появи має показувати кружечки саме на її прапорах.
        if (remote != nullptr && remote->ourTeam > 0 && selectedTeam != remote->ourTeam) {
          selectedTeam = remote->ourTeam;
          selectedSpawn = 0;
          spawnDirty = true;
          std::printf("  екран появи: сервер дав команду %d\n", selectedTeam);
        }
        if (applyHudState) applyHudState(hudState);
        if (updateHudVariables) updateHudVariables(spawned, spawnVisible);

        // Натискання на екрані появи. Кнопка не має власної логіки — вона
        // виконує консольну команду з setButtonNodeConCmd, тож усе, що
        // тут треба, це знайти її під курсором і виконати.
        if (spawnVisible && !spawnPieces.empty()) {
          const auto input = device->readInput();
          // --click --mouse дає одне синтетичне натискання: так екран
          // перевіряється знімком, без рук.
          const bool clicked = input.clicked || (args.click && frame == 1);
          const float clickX = args.click ? args.mouseX : input.mouseX;
          const float clickY = args.click ? args.mouseY : input.mouseY;
          if (clicked) {
            // Спершу кружечки місць появи: у даних для них вузлів немає,
            // карта ловить мишу сама. Текстуру вибраного гра бере з
            // окремого масиву (BF2.exe 0x77f7eb проти 0x77f7fa —
            // 0x960 для вибраного, 0x950 для ні).
            // Карта на екрані появи — у великому поданні, тож і ловити
            // мишу треба в ньому: у мініатюрі вузол стоїть в іншому місці.
            ingameHud.setMapView(obf2::hud::MapView::Maxi);
            const auto hitSpawn = obf2::hud::spawnMarkerAt(
                ingameHud, "MapSplit", hudScreen, spawnContext, clickX, clickY);
            ingameHud.setMapView(obf2::hud::MapView::Mini);
            if (hitSpawn) {
              selectedSpawn = static_cast<int>(*hitSpawn);
              spawnDirty = true;
              std::printf("  екран появи: місце %d\n", selectedSpawn);
            }
            // Кнопки екрана появи лежать у двох гілках: власне SpawnMenu
            // і TopLayer, де сидять DONE та SUICIDE.
            const obf2::hud::Node* hit = nullptr;
            for (const char* root : {"SpawnMenu", "TopLayer"}) {
              if (const obf2::hud::Node* found = obf2::hud::buttonAt(
                      ingameHud, root, hudScreen, clickX, clickY, &spawnContext)) {
                hit = found;
              }
            }
            if (hit != nullptr) {
              // На кнопці кілька команд, і кожна має свою подію. У даних
              // їх чотири: 0 (247 разів), 1 (73), 3 (50) і 2 (11).
              // Натисканню належать 0 і 3 — на трійці висять, зокрема,
              // spawnManager.setPlayerTeam і scoreboard.setToggleShow;
              // 1 і 2 — це наведення й відведення, там самі звуки.
              for (const auto& [event, line] : hit->commands) {
                if (event != 0 && event != 3) continue;
                std::printf("  екран появи: %s -> %s\n", hit->name.c_str(), line.c_str());
                if (!engine.console().executeLine(line)) {
                  std::printf("  екран появи: команда без обробника — %s\n", line.c_str());
                }
              }
            }
          }
        }
        // Поки хоч один вузол їде або згасає, екран доводиться перепікати
        // щокадру: геометрія в нас лежить у мешах на відеокарті.
        {
          const auto now = std::chrono::steady_clock::now();
          const float dt =
              std::chrono::duration<float>(now - lastAnimationTick).count();
          lastAnimationTick = now;
          hudAnimator.advance(dt > 0.25f ? 0.25f : dt);
          if (hudAnimator.animating()) spawnDirty = true;
          // Швидкість 600 — з самого файлу (SetVariableSineAction).
          // Клас зветься Sine, тобто хід, найпевніше, згладжений, але
          // самої кривої ми не реверсили, тож їдемо рівно на цій
          // швидкості.
          const float step = 600.0f * (dt > 0.25f ? 0.25f : dt);
          const auto approach = [&](float& value, float target) {
            if (value == target) return;
            const float left = target - value;
            value = std::abs(left) <= step ? target : value + (left > 0 ? step : -step);
            hudDirty = true;
          };
          approach(bottomLeftX, bottomLeftTarget);
          approach(bottomRightX, bottomRightTarget);
        }
        // Перебудовуємо екран появи лише поки він на екрані.
        if (spawnDirty && spawnVisible && rebuildSpawn) {
          spawnDirty = false;
          rebuildSpawn();
        }
        if (hudDirty && rebuildIngame) {
          hudDirty = false;
          rebuildIngame();
        }
        for (const KeyScreen& screen : keyScreens) {
          const bool forced = screen.group == args.hudScreenName;
          bool visible = forced;
          if (!visible && screen.heldByKey) {
            const std::string_view key = controls.key(screen.action);
            visible = !key.empty() && device->isKeyDown(key);
          } else if (!visible) {
            visible = screen.state == hudState;
          }
          if (!visible) continue;
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
        for (const OwnedPiece& piece : ingamePieces) {
          obf2::gfx::MeshRenderer::DrawItem item{&piece.mesh, obf2::Mat4::identity()};
          item.tint[0] = piece.tint.r;
          item.tint[1] = piece.tint.g;
          item.tint[2] = piece.tint.b;
          item.tint[3] = piece.tint.a;
          hudItems.push_back(item);
        }
        for (const int index : extra) pushHud(index);
        if (spawnVisible) {
          for (const OwnedPiece& piece : spawnPieces) {
            obf2::gfx::MeshRenderer::DrawItem item{&piece.mesh, obf2::Mat4::identity()};
            item.tint[0] = piece.tint.r;
            item.tint[1] = piece.tint.g;
            item.tint[2] = piece.tint.b;
            item.tint[3] = piece.tint.a;
            hudItems.push_back(item);
          }
        }

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
            // Беремо загальний контекст, а не порожній: інакше живі
            // підписи малюються типовим шрифтом замість свого
            // (setTextNodeStyle) і без локалізації.
            obf2::hud::Context single = hudDynamicContext;
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
  for (OwnedPiece& piece : spawnPieces) renderer->release(piece.mesh);
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
