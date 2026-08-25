// mesh_info — регресія парсера мешів на справжніх даних.
//
//   mesh_info <modDir> --all [staticmesh|bundledmesh|skinnedmesh]
//   mesh_info <modDir> <шлях/усередині/vfs.staticmesh>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "obf2/core/path.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/vfs/filesystem.h"

namespace {

obf2::FileSystem mountGame(const std::filesystem::path& modDir) {
  obf2::FileSystem files;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    obf2::mountArchivesFromCon(files, modDir, modDir / list);
  }
  files.mountDirectory(modDir);
  return files;
}

void printOne(obf2::FileSystem& files, const std::string& path) {
  const auto kind = obf2::mesh::kindFromExtension(obf2::assetExtension(path));
  if (!kind) { std::fprintf(stderr, "невідоме розширення: %s\n", path.c_str()); return; }

  const auto bytes = files.read(path);
  if (!bytes) { std::fprintf(stderr, "не знайдено: %s\n", path.c_str()); return; }

  std::string error;
  const auto mesh = obf2::mesh::load(*bytes, *kind, &error);
  if (!mesh) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str()); return; }

  std::printf("%s\n  тип: %s, версія: %u, %zu байт\n", path.c_str(),
              std::string(obf2::mesh::kindName(mesh->kind)).c_str(), mesh->header.version,
              bytes->size());
  std::printf("  вершин: %u (stride %u), індексів: %zu, geom: %zu\n", mesh->vertexCount,
              mesh->vertexStride, mesh->indices.size(), mesh->geometries.size());

  std::printf("  атрибути:");
  for (const auto& a : mesh->attributes) {
    if (a.flag != 0) continue;
    std::printf(" usage=%u@%u", a.usage, a.offset);
  }
  std::putchar('\n');

  for (std::size_t g = 0; g < mesh->geometries.size(); ++g) {
    std::printf("  geom %zu: lod-ів %zu\n", g, mesh->geometries[g].lods.size());
  }

  const auto render = obf2::mesh::extract(*mesh, 0, 0, &error);
  if (!render) { std::fprintf(stderr, "  extract: %s\n", error.c_str()); return; }
  std::printf("  lod0: вершин %zu, індексів %zu (%zu трикутників), діапазонів %zu\n",
              render->vertices.size(), render->indices.size(), render->indices.size() / 3,
              render->ranges.size());
  std::printf("  bbox: %.2f/%.2f/%.2f .. %.2f/%.2f/%.2f\n", render->bounds.min.x,
              render->bounds.min.y, render->bounds.min.z, render->bounds.max.x,
              render->bounds.max.y, render->bounds.max.z);
  for (std::size_t i = 0; i < render->ranges.size() && i < 4; ++i) {
    const auto& range = render->ranges[i];
    std::printf("    [%zu] %s / %s, індексів %u, текстур %zu%s\n", i, range.fxFile.c_str(),
                range.technique.c_str(), range.indexCount, range.maps.size(),
                range.maps.empty() ? "" : (" -> " + range.maps.front()).c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: mesh_info <modDir> <--all [kind] | path.staticmesh>\n", stderr);
    return 2;
  }
  obf2::FileSystem files = mountGame(argv[1]);
  const std::string what = argv[2];

  // Розвідка: які technique зустрічаються і що лежить у кожному слоті текстур.
  // Потрібно, щоб зрозуміти, який слот вважати базовим кольором.
  if (what == "--materials") {
    std::map<std::string, int> techniques;
    std::map<std::string, int> slotSuffix;  // "слот N: суфікс" -> скільки разів

    auto scan = files.list();
    std::sort(scan.begin(), scan.end());
    scan.erase(std::unique(scan.begin(), scan.end()), scan.end());

    for (const auto& path : scan) {
      if (obf2::assetExtension(path) != "staticmesh") continue;
      const auto bytes = files.read(path);
      if (!bytes) continue;
      const auto mesh = obf2::mesh::load(*bytes, obf2::mesh::Kind::Static);
      if (!mesh) continue;

      for (const auto& geometry : mesh->geometries) {
        for (const auto& lod : geometry.lods) {
          for (const auto& material : lod.materials) {
            ++techniques[material.technique];
            for (std::size_t slot = 0; slot < material.maps.size() && slot < 4; ++slot) {
              const std::string& map = material.maps[slot];
              const std::size_t dot = map.find_last_of('.');
              const std::size_t underscore = map.find_last_of('_', dot);
              std::string suffix = (underscore == std::string::npos || dot == std::string::npos)
                                       ? "<без суфікса>"
                                       : map.substr(underscore, dot - underscore);
              ++slotSuffix["слот " + std::to_string(slot) + ": " + suffix];
            }
          }
        }
      }
    }

    std::puts("technique (топ-10):");
    std::vector<std::pair<std::string, int>> sortedTechniques(techniques.begin(), techniques.end());
    std::sort(sortedTechniques.begin(), sortedTechniques.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < sortedTechniques.size() && i < 10; ++i) {
      std::printf("  %-28s %d\n", sortedTechniques[i].first.c_str(), sortedTechniques[i].second);
    }

    std::puts("\nсуфікси текстур по слотах (топ-14):");
    std::vector<std::pair<std::string, int>> sortedSlots(slotSuffix.begin(), slotSuffix.end());
    std::sort(sortedSlots.begin(), sortedSlots.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < sortedSlots.size() && i < 14; ++i) {
      std::printf("  %-28s %d\n", sortedSlots[i].first.c_str(), sortedSlots[i].second);
    }
    return 0;
  }

  if (what != "--all") { printOne(files, obf2::normalizeAssetPath(what)); return 0; }

  const std::string wanted = argc > 3 ? argv[3] : "";
  std::map<std::string, int> parsed, failed;
  std::map<std::string, int> failureKinds;
  long long triangles = 0, vertices = 0;
  std::vector<std::string> firstFailures;

  auto paths = files.list();
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

  for (const auto& path : paths) {
    const std::string_view extension = obf2::assetExtension(path);
    const auto kind = obf2::mesh::kindFromExtension(extension);
    if (!kind) continue;
    if (!wanted.empty() && extension != wanted) continue;

    const auto bytes = files.read(path);
    if (!bytes) continue;

    std::string error;
    const auto mesh = obf2::mesh::load(*bytes, *kind, &error);
    if (!mesh) {
      ++failed[std::string(extension)];
      ++failureKinds[error.substr(0, error.find(" (зсув"))];
      if (firstFailures.size() < 5) firstFailures.push_back(path + ": " + error);
      continue;
    }
    ++parsed[std::string(extension)];
    vertices += mesh->vertexCount;

    if (auto render = obf2::mesh::extract(*mesh, 0, 0, &error)) {
      triangles += static_cast<long long>(render->indices.size() / 3);
    }
  }

  std::puts("розібрано:");
  for (const auto& [extension, count] : parsed) std::printf("  %-14s %d\n", extension.c_str(), count);
  if (!failed.empty()) {
    std::puts("не розібрано:");
    for (const auto& [extension, count] : failed) std::printf("  %-14s %d\n", extension.c_str(), count);
    std::puts("причини:");
    for (const auto& [reason, count] : failureKinds) std::printf("  %-44s %d\n", reason.c_str(), count);
    std::puts("приклади:");
    for (const auto& example : firstFailures) std::printf("  %s\n", example.c_str());
  }
  std::printf("\nвершин усього: %lld, трикутників у lod0: %lld\n", vertices, triangles);
  return failed.empty() ? 0 : 1;
}
