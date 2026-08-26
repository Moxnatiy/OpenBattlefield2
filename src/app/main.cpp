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
#include "obf2/game/scene.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/level/gameplay.h"
#include "obf2/level/level.h"
#include "obf2/server/game_client.h"
#include "obf2/server/game_server.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/mesh/collision.h"
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
  bool click = false;  // --click: одне натискання в позиції --mouse
  float distance = 0.0f;             // 0 = підібрати за габаритами
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
std::optional<obf2::mesh::RenderMesh> buildObjectMesh(obf2::FileSystem& files,
                                                      const obf2::game::Registry& registry,
                                                      const std::string& templateName,
                                                      const Args& args, bool verbose) {
  const auto* root = registry.find(templateName);
  if (root == nullptr) return std::nullopt;

  const auto instance = obf2::game::flattenObject(registry, templateName);
  if (!instance) return std::nullopt;

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
int runSession(const Args& args, obf2::FileSystem& files, std::string* nextLevel) {
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
        obf2::con::Interpreter settingsInterpreter(
            files, [&](const obf2::con::Command& c) { settingsConsole.execute(c); });
        settingsInterpreter.runFile("GameLogicInit.con");
        settingsInterpreter.runFile("Settings/ServerSettings.con");
        std::printf("  квитки: %d проти %d (ticketRatio %.0f%%)\n",
                    serverSettings.defaultTickets[1], serverSettings.defaultTickets[2],
                    serverSettings.ticketRatio);
      }

      hostedServer = std::make_unique<obf2::server::GameServer>(serverSettings);
      obf2::server::GameServer& gameServer = *hostedServer;
      // Ігрова логіка режиму: контрольні точки й спавнери техніки.
      std::string gameplayError;
      if (auto gameplay = obf2::level::loadGameplayObjects(files, level->name, "gpm_cq", 16,
                                                           &gameplayError)) {
        std::printf("  ігрова логіка: %zu контрольних точок, %zu спавнерів техніки\n",
                    gameplay->controlPoints.size(), gameplay->spawners.size());
        for (const auto& point : gameplay->controlPoints) {
          std::printf("    точка %d \"%s\" радіус %.0f @ %.0f/%.0f/%.0f\n", point.id,
                      point.nameKey.c_str(), point.radius, point.position.x, point.position.y,
                      point.position.z);
        }
        gameServer.setGameplay(std::move(*gameplay));
      } else {
        std::printf("  ігрова логіка: %s\n", gameplayError.c_str());
      }

      gameServer.loadWorld(*level);
      // Рельєф для зіткнення з землею: без нього солдат падає без кінця.
      gameServer.setTerrain(&*level);

      // Геометрія зіткнень: для кожного статичного об'єкта беремо шар
      // солдата з .collisionmesh і переводимо у світові координати.
      auto collisionWorld = std::make_unique<obf2::server::CollisionWorld>();
      int withCollision = 0, withoutCollision = 0;
      std::unordered_map<std::string, std::shared_ptr<obf2::mesh::CollisionMesh>> collisionCache;

      for (const auto& object : level->objects) {
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

        obf2::Mat4 transform = obf2::translation(object.position);
        if (object.hasRotation) {
          transform = transform * obf2::rotationYawPitchRoll(object.rotation.x, object.rotation.y,
                                                             object.rotation.z);
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

      obf2::Mat4 transform = obf2::translation(object.position);
      if (object.hasRotation) {
        transform = transform * obf2::rotationYawPitchRoll(object.rotation.x, object.rotation.y,
                                                           object.rotation.z);
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

      menuScreen.width = 1280;
      menuScreen.height = 720;
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
  std::string error;
  auto device = obf2::gfx::Device::create(desc, &error);
  if (!device) {
    std::fprintf(stderr, "не вдалося створити пристрій: %s\n", error.c_str());
    return 1;
  }
  std::printf("GPU-бекенд: %s\n", std::string(device->driver()).c_str());

  auto renderer = obf2::gfx::MeshRenderer::create(*device, &error);
  if (!renderer) {
    std::fprintf(stderr, "рендерер: %s\n", error.c_str());
    return 1;
  }

  if (level) {
    renderer->setFog(obf2::gfx::MeshRenderer::Fog{
        obf2::gfx::Color{level->terrain.fogColor.x, level->terrain.fogColor.y,
                         level->terrain.fogColor.z, 1.0f},
        level->terrain.fogStart, level->terrain.fogEnd});
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
    if (!bytes) bytes = files.read(obf2::joinAssetPath("objects", path));
    // Шляхи в HUD відлічуються від теки текстур інтерфейсу — так само, як
    // це видно в `nametags.setTexture Menu/HUD/Texture/...`.
    if (!bytes) bytes = files.read(obf2::joinAssetPath("menu/hud/texture", path));
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
  std::map<std::string, bool> hudVariables;
  std::map<std::string, std::string> hudStrings;
  // Вузли, підпис яких змінюється в грі: геометрію для них перебудовуємо,
  // але лише коли справді змінився рядок.
  struct DynamicText {
    const obf2::hud::Node* node = nullptr;
    std::string variable;
    std::string shown;
    obf2::gfx::GpuMesh mesh;
    bool valid = false;
  };
  std::vector<DynamicText> hudDynamic;
  const LoadedFont hudFont = bootMode ? LoadedFont{} : loadFont(files, "Fonts/800/dynamicText_13");
  obf2::hud::Screen hudScreen;
  if (!bootMode && hudFont.valid) {
    obf2::con::Interpreter hudInterpreter(
        files, [&](const obf2::con::Command& command) { ingameHud.feed(command); });
    hudInterpreter.runFile("Menu/HUD/HudSetup/HudSetupMain.con");

    // Змінні показу: у даних це або стала 1/0, або назва стану інтерфейсу.
    // Невідому назву вважаємо вимкненою — інакше на екран одразу виїхали б
    // інтерфейс командира, табло й кабіни всієї техніки.
    // ReferenceCross — це вирівнювальний хрест розробників, у грі він
    // вимкнений; решта — базовий набір, який видно в бою.
    for (const char* on : {"ShowIngameHud", "PlayerHealthShow", "PlayerStaminaShow",
                           "PrimaryAmmoShow", "PrimaryAmmoBarShow", "MapShow", "MapMinSize",
                           "CPInterfaceEnabled"}) {
      hudVariables[on] = true;
    }

    obf2::hud::Context hudContext;
    hudContext.localize = [&](std::string_view key) { return engine.lexicon().text(key); };
    hudContext.isVisible = [&](std::string_view variable) {
      if (variable == "1") return true;
      const auto found = hudVariables.find(std::string(variable));
      return found != hudVariables.end() && found->second;
    };
    hudContext.variableText = [&](std::string_view variable) -> std::string_view {
      const auto found = hudStrings.find(std::string(variable));
      return found == hudStrings.end() ? std::string_view{} : std::string_view(found->second);
    };

    hudScreen.width = 1280;
    hudScreen.height = 720;
    auto pieces = obf2::hud::buildTree(ingameHud, "IngameHud", hudFont.font, hudFont.atlasPath,
                                       hudScreen, hudContext);
    for (auto& piece : pieces) {
      scene.meshes.push_back(std::move(piece.geometry));
      hudQuads.push_back(static_cast<int>(scene.meshes.size()) - 1);
    }
    // Живі значення: квитки обох команд. Далі сюди підуть здоров'я й набій.
    for (const auto& node : ingameHud.nodes()) {
      if (node.textVariable != "FriendlyTicketsString" &&
          node.textVariable != "EnemyTicketsString") {
        continue;
      }
      if (node.group != "TicketInfo") continue;  // той самий підпис є і в командира
      hudDynamic.push_back(DynamicText{&node, node.textVariable, {}, {}, false});
    }

    std::printf("  HUD: %zu вузлів у дереві, шматків до малювання %zu, живих підписів %zu\n",
                ingameHud.nodes().size(), pieces.size(), hudDynamic.size());
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
      yaw -= raw.mouseDeltaX * kMouseSensitivity;
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
      lookTarget = eye + obf2::Vec3f{-std::sin(yawRadians) * std::cos(pitchRadians),
                                     std::sin(pitchRadians),
                                     -std::cos(yawRadians) * std::cos(pitchRadians)};
    } else {
      const float angle = static_cast<float>(frame) / 60.0f * 0.6f;
      eye = obf2::Vec3f{scene.center.x + std::sin(angle) * distance, scene.center.y + eyeHeight,
                        scene.center.z + std::cos(angle) * distance};
    }

    const float aspect =
        acquired->height == 0
            ? 1.0f
            : static_cast<float>(acquired->width) / static_cast<float>(acquired->height);
    const obf2::Mat4 projection =
        obf2::perspective(1.05f, aspect, scene.radius * 0.002f + 0.05f, scene.radius * 40.0f);
    const obf2::Mat4 view = obf2::lookAt(eye, lookTarget, obf2::Vec3f{0.0f, 1.0f, 0.0f});

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
        }

        std::vector<obf2::gfx::MeshRenderer::DrawItem> hudItems;
        hudItems.reserve(hudQuads.size() + hudDynamic.size());
        for (const int index : hudQuads) {
          if (index < 0 || !uploadedOk[static_cast<std::size_t>(index)]) continue;
          hudItems.push_back(obf2::gfx::MeshRenderer::DrawItem{
              &gpuMeshes[static_cast<std::size_t>(index)], obf2::Mat4::identity()});
        }

        for (DynamicText& dynamic : hudDynamic) {
          const auto found = hudStrings.find(dynamic.variable);
          const std::string value = found == hudStrings.end() ? std::string() : found->second;
          if (value != dynamic.shown) {
            // Рядок змінився — перебудовуємо тільки цей підпис.
            if (dynamic.valid) renderer->release(dynamic.mesh);
            dynamic.valid = false;
            dynamic.shown = value;
            if (!value.empty()) {
              obf2::hud::Node copy = *dynamic.node;
              copy.text = value;
              copy.textVariable.clear();
              auto built = obf2::hud::buildNode(copy, hudFont.font, hudFont.atlasPath, hudScreen,
                                                obf2::hud::Context{});
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
