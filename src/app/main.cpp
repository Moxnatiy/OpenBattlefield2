// openbf2 — точка входу рушія.
//
//   openbf2 [--mod <шлях до mods/bf2>] [--mesh <шлях у VFS>] [--frames N]
//
// Зараз: монтує дані гри так само, як fileManager у Refractor 2, читає
// .staticmesh просто з архіву й малює його з орбітальною камерою.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "obf2/core/math.h"
#include "obf2/core/path.h"
#include "obf2/core/platform.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/vfs/filesystem.h"

namespace {

constexpr const char* kDefaultMesh =
    "objects/staticobjects/common/com_objects/barrel_green/meshes/barrel_green.staticmesh";

struct Args {
  std::filesystem::path modDir = "Game Files/mods/bf2";
  std::string meshPath = kDefaultMesh;
  int frames = 0;  // 0 = крутитися, доки не закриють вікно
  std::string screenshot;  // куди зберегти останній кадр
};

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--mod" && i + 1 < argc) args.modDir = argv[++i];
    else if (flag == "--mesh" && i + 1 < argc) args.meshPath = argv[++i];
    else if (flag == "--frames" && i + 1 < argc) args.frames = std::atoi(argv[++i]);
    else if (flag == "--screenshot" && i + 1 < argc) args.screenshot = argv[++i];
  }
  return args;
}

std::optional<obf2::mesh::RenderMesh> loadMesh(obf2::FileSystem& files, const std::string& path) {
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
  auto render = obf2::mesh::extract(*parsed, 0, 0, &error);
  if (!render) {
    std::fprintf(stderr, "не розпаковано %s: %s\n", normalized.c_str(), error.c_str());
    return std::nullopt;
  }

  std::printf("меш: %s\n  версія %u, вершин %zu, трикутників %zu, матеріалів %zu\n",
              normalized.c_str(), parsed->header.version, render->vertices.size(),
              render->indices.size() / 3, render->ranges.size());
  return render;
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

  const auto renderMesh = loadMesh(files, args.meshPath);
  if (!renderMesh) return 1;

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
  auto gpuMesh = renderer->upload(*renderMesh, &error);
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
  std::printf("кадрів намальовано: %d\n", frame);
  return 0;
}
