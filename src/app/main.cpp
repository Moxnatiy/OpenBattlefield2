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
#include "obf2/game/scene.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/level/level.h"
#include "obf2/mesh/bf2_mesh.h"
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
  std::optional<obf2::Vec3f> focus;  // куди дивиться камера
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

int main(int argc, char** argv) {
  const Args args = parseArgs(argc, argv);

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
    std::printf("  статичних об'єктів: %zu\n", level->objects.size());

    auto patches = obf2::level::buildTerrainPatches(*level, files);
    std::printf("  патчів терену: %zu з %d (решта під водою, колормап немає)\n", patches.size(),
                ((level->primary.size - 1) / level->terrain.patchSize) *
                    ((level->primary.size - 1) / level->terrain.patchSize));
    for (auto& patch : patches) scene.add(std::move(patch.geometry), obf2::Mat4::identity());
    scene.add(obf2::level::buildWaterPlane(*level), obf2::Mat4::identity());

    registry = buildRegistry(files);
    std::printf("  реєстр: %zu шаблонів (%.1f с)\n", registry.size(), secondsSince(started));

    std::unordered_map<std::string, int> meshIndexByTemplate;
    std::map<std::string, int> missing;
    int placed = 0;

    for (const auto& object : level->objects) {
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

    const float extent = level->halfExtent() * level->primary.scale.x;
    scene.center = obf2::Vec3f{0.0f, level->terrain.seaLevel, 0.0f};
    scene.radius = extent;
  }

  // --- решта режимів ----------------------------------------------------

  obf2::engine::Engine engine;
  const bool bootMode = args.levelName.empty() && args.objectName.empty() && args.meshPath.empty();
  int introQuad = -1;
  int menuQuad = -1;

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
      if (shown++ >= 8) break;
      std::printf("    без обробника: %s (x%d)\n", name.c_str(), count);
    }

    const std::string background = findMenuBackground(files);
    std::printf("  стан: %s, тло меню: %s\n",
                std::string(obf2::engine::stateName(engine.state())).c_str(),
                background.empty() ? "(немає)" : background.c_str());

    scene.meshes.push_back(buildScreenQuad("#000000"));
    introQuad = static_cast<int>(scene.meshes.size()) - 1;
    scene.meshes.push_back(buildScreenQuad(background));
    menuQuad = static_cast<int>(scene.meshes.size()) - 1;
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
    if (!bytes) {
      ++texturesMissing;
      textureCache.emplace(path, std::nullopt);
      return std::nullopt;
    }

    std::string textureError;
    auto decoded = obf2::texture::loadImage(*bytes, &textureError);
    if (!decoded) ++texturesMissing; else ++texturesLoaded;
    textureCache.emplace(path, decoded);
    return decoded;
  };

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

  int frame = 0;
  while (device->pumpEvents()) {
    auto acquired = device->beginFrame();
    if (!acquired) continue;

    const float angle = static_cast<float>(frame) / 60.0f * 0.6f;
    const obf2::Vec3f eye{scene.center.x + std::sin(angle) * distance, scene.center.y + eyeHeight,
                          scene.center.z + std::cos(angle) * distance};

    const float aspect =
        acquired->height == 0
            ? 1.0f
            : static_cast<float>(acquired->width) / static_cast<float>(acquired->height);
    const obf2::Mat4 projection =
        obf2::perspective(1.05f, aspect, scene.radius * 0.002f + 0.05f, scene.radius * 40.0f);
    const obf2::Mat4 view = obf2::lookAt(eye, scene.center, obf2::Vec3f{0.0f, 1.0f, 0.0f});

    if (bootMode) {
      engine.update(1.0f / 60.0f);
      if (device->consumeSkip()) engine.skipMovie();

      const int quad = engine.state() == obf2::engine::State::Intro ? introQuad : menuQuad;
      std::vector<obf2::gfx::MeshRenderer::DrawItem> screen;
      if (quad >= 0 && uploadedOk[static_cast<std::size_t>(quad)]) {
        screen.push_back(obf2::gfx::MeshRenderer::DrawItem{
            &gpuMeshes[static_cast<std::size_t>(quad)], obf2::Mat4::identity()});
      }
      renderer->renderScene(*acquired, screen, obf2::Mat4::identity(),
                            obf2::gfx::Color{0.0f, 0.0f, 0.0f, 1.0f});
    } else {
      renderer->renderScene(*acquired, items, projection * view,
                            obf2::gfx::Color{0.42f, 0.55f, 0.68f, 1.0f});
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

    ++frame;
    if (args.frames > 0 && frame >= args.frames) break;
  }

  for (auto& gpuMesh : gpuMeshes) renderer->release(gpuMesh);
  std::printf("кадрів намальовано: %d\n", frame);
  return 0;
}
