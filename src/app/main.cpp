// openbf2 — точка входу рушія.
//
//   openbf2 [--mod <шлях до mods/bf2>] [--mesh <шлях у VFS>] [--frames N]
//
// Зараз: монтує дані гри так само, як fileManager у Refractor 2, читає
// .staticmesh просто з архіву й малює його з орбітальною камерою.

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>

#include "obf2/core/math.h"
#include "obf2/core/path.h"
#include "obf2/core/platform.h"
#include "obf2/game/scene.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/texture/dds.h"
#include "obf2/vfs/filesystem.h"

namespace {

constexpr const char* kDefaultMesh =
    "objects/staticobjects/common/com_objects/barrel_green/meshes/barrel_green.staticmesh";

struct Args {
  std::filesystem::path modDir = "Game Files/mods/bf2";
  std::string meshPath = kDefaultMesh;
  std::string objectName;  // --object: зібрати за деревом ObjectTemplate
  int geometryIndex = -1;  // -1 = вибрати найбільший lod0
  int lodIndex = 0;
  int frames = 0;  // 0 = крутитися, доки не закриють вікно
  std::string screenshot;  // куди зберегти останній кадр
};

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--mod" && i + 1 < argc) args.modDir = argv[++i];
    else if (flag == "--mesh" && i + 1 < argc) args.meshPath = argv[++i];
    else if (flag == "--object" && i + 1 < argc) args.objectName = argv[++i];
    else if (flag == "--geom" && i + 1 < argc) args.geometryIndex = std::atoi(argv[++i]);
    else if (flag == "--lod" && i + 1 < argc) args.lodIndex = std::atoi(argv[++i]);
    else if (flag == "--frames" && i + 1 < argc) args.frames = std::atoi(argv[++i]);
    else if (flag == "--screenshot" && i + 1 < argc) args.screenshot = argv[++i];
  }
  return args;
}

// У .bundledmesh техніки кілька geom: вигляд із кабіни, зовнішній вигляд і
// уламки. Явного маркера у файлі немає, але є надійна ознака: **вигляд із
// кабіни має рівно один lod** — гравець завжди поруч, тож ланцюжок деталізації
// йому не потрібен, тоді як зовнішній вигляд має 3-4 рівні.
//
//   ahe_ah1z:  geom0 = 1 lod (кабіна), geom1 = 4 (зовні), geom2 = 3 (уламки)
//   apc_btr90: geom0 = 1 lod (кабіна), geom1 = 4 (зовні), geom2 = 4 (уламки)
//
// Тому обираємо geom із найдовшим ланцюжком lod, а за однакової довжини —
// найдетальніший: у BTR обидва «не-кабінні» geom мають по 4 рівні, і від
// уламків зовнішній вигляд відрізняє саме кількість трикутників.
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
                                               const Args& args) {
  const std::string normalized = obf2::normalizeAssetPath(path);
  const auto kind = obf2::mesh::kindFromExtension(obf2::assetExtension(normalized));
  if (!kind) {
    std::fprintf(stderr, "невідоме розширення меша: %s\n", normalized.c_str());
    return std::nullopt;
  }
  const auto bytes = files.read(normalized);
  if (!bytes) {
    std::fprintf(stderr, "меш не знайдено у VFS: %s\n", normalized.c_str());
    return std::nullopt;
  }

  std::string error;
  const auto parsed = obf2::mesh::load(*bytes, *kind, &error);
  if (!parsed) {
    std::fprintf(stderr, "не розібрано %s: %s\n", normalized.c_str(), error.c_str());
    return std::nullopt;
  }

  const std::size_t lodIndex = static_cast<std::size_t>(std::max(0, args.lodIndex));
  const std::size_t geometryIndex = args.geometryIndex >= 0
                                        ? static_cast<std::size_t>(args.geometryIndex)
                                        : pickGeometry(*parsed, lodIndex);

  auto render = obf2::mesh::extract(*parsed, geometryIndex, lodIndex, &error);
  if (!render) {
    std::fprintf(stderr, "не розпаковано %s: %s\n", normalized.c_str(), error.c_str());
    return std::nullopt;
  }

  std::printf("меш: %s\n  версія %u, geom %zu/%zu, lod %zu, вершин %zu, трикутників %zu, "
              "матеріалів %zu\n",
              normalized.c_str(), parsed->header.version, geometryIndex,
              parsed->geometries.size(), lodIndex, render->vertices.size(),
              render->indices.size() / 3, render->ranges.size());
  return render;
}

// Ім'я геометрії з ObjectTemplate — це не шлях. Файл лежить поруч із .con,
// у підтеці meshes: objects/vehicles/land/apc_btr90/meshes/apc_btr90.bundledmesh.
std::string resolveGeometryPath(obf2::FileSystem& files, const std::string& templateFile,
                                const std::string& geometryName) {
  const std::string_view dir = obf2::assetParentDir(templateFile);
  for (const char* extension : {".bundledmesh", ".staticmesh", ".skinnedmesh"}) {
    for (const char* subdirectory : {"meshes/", ""}) {
      const std::string candidate =
          obf2::joinAssetPath(dir, std::string(subdirectory) + geometryName + extension);
      if (files.exists(candidate)) return candidate;
    }
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

  // Режим --object: збираємо техніку за деревом ObjectTemplate.
  std::string meshPath = args.meshPath;
  std::optional<obf2::game::ObjectInstance> instance;
  obf2::game::Registry registry;

  if (!args.objectName.empty()) {
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
    std::printf("реєстр: %zu шаблонів\n", registry.size());

    instance = obf2::game::flattenObject(registry, args.objectName);
    if (!instance) {
      std::fprintf(stderr, "шаблон не знайдено: %s\n", args.objectName.c_str());
      return 1;
    }
    const auto* root = registry.find(args.objectName);
    meshPath = resolveGeometryPath(files, root->file, instance->geometryName);
    if (meshPath.empty()) {
      std::fprintf(stderr, "не знайдено геометрію '%s' поруч із %s\n",
                   instance->geometryName.c_str(), root->file.c_str());
      return 1;
    }
    std::printf("об'єкт: %s (%s)\n  вузлів у дереві: %zu, глибина: %d, нерозв'язаних: %d, "
                "циклів: %d\n",
                instance->rootName.c_str(), root->className.c_str(), instance->parts.size(),
                instance->maxDepth, instance->unresolved, instance->cycles);
  }

  auto renderMesh = loadMesh(files, meshPath, args);
  if (!renderMesh) return 1;

  if (instance) {
    const auto transforms = obf2::game::partTransformMap(*instance);
    const std::size_t moved = obf2::game::applyPartTransforms(*renderMesh, transforms);
    std::printf("  частин із трансформом: %zu, переставлено вершин: %zu з %zu\n",
                transforms.size(), moved, renderMesh->vertices.size());
  }

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
  // Резолвер текстур: шлях із матеріалу -> байти з архіву -> розібраний DDS.
  int texturesLoaded = 0, texturesMissing = 0;
  auto resolveTexture =
      [&](const std::string& mapName) -> std::optional<obf2::texture::Texture> {
    const std::string path = obf2::normalizeAssetPath(mapName);
    auto bytes = files.read(path);
    if (!bytes) {
      // Частина матеріалів посилається на текстуру без префікса точки
      // монтування — пробуємо ще раз у "objects".
      bytes = files.read(obf2::joinAssetPath("objects", path));
    }
    if (!bytes) {
      ++texturesMissing;
      std::fprintf(stderr, "  текстуру не знайдено: %s\n", path.c_str());
      return std::nullopt;
    }

    std::string textureError;
    auto decoded = obf2::texture::loadDds(*bytes, &textureError);
    if (!decoded) {
      ++texturesMissing;
      std::fprintf(stderr, "  %s: %s\n", path.c_str(), textureError.c_str());
      return std::nullopt;
    }
    ++texturesLoaded;
    std::printf("  текстура: %s (%ux%u, %s, рівнів %zu)\n", path.c_str(), decoded->width,
                decoded->height, std::string(obf2::texture::formatName(decoded->format)).c_str(),
                decoded->mips.size());
    return decoded;
  };

  auto gpuMesh = renderer->upload(*renderMesh, resolveTexture, &error);
  if (!gpuMesh) {
    std::fprintf(stderr, "завантаження в GPU: %s\n", error.c_str());
    return 1;
  }

  // Камера облітає меш, підібравшись під його bbox.
  const obf2::Vec3f boundsMin{renderMesh->bounds.min.x, renderMesh->bounds.min.y,
                              renderMesh->bounds.min.z};
  const obf2::Vec3f boundsMax{renderMesh->bounds.max.x, renderMesh->bounds.max.y,
                              renderMesh->bounds.max.z};
  const obf2::Vec3f center = (boundsMin + boundsMax) * 0.5f;
  const float radius = std::max(0.001f, obf2::length(boundsMax - boundsMin) * 0.5f);
  const float distance = radius * 2.6f;

  int frame = 0;
  while (device->pumpEvents()) {
    auto acquired = device->beginFrame();
    if (!acquired) continue;

    const float time = static_cast<float>(frame) / 60.0f;
    const float angle = time * 0.6f;
    const obf2::Vec3f eye{center.x + std::sin(angle) * distance, center.y + radius * 0.8f,
                          center.z + std::cos(angle) * distance};

    const float aspect =
        acquired->height == 0 ? 1.0f
                              : static_cast<float>(acquired->width) / static_cast<float>(acquired->height);
    const obf2::Mat4 projection = obf2::perspective(1.05f, aspect, 0.05f, radius * 40.0f);
    const obf2::Mat4 view = obf2::lookAt(eye, center, obf2::Vec3f{0.0f, 1.0f, 0.0f});

    renderer->render(*acquired, *gpuMesh, projection * view,
                     obf2::gfx::Color{0.09f, 0.11f, 0.13f, 1.0f});

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

  renderer->release(*gpuMesh);
  std::printf("текстур завантажено: %d, не знайдено: %d\n", texturesLoaded, texturesMissing);
  std::printf("кадрів намальовано: %d\n", frame);
  return 0;
}
