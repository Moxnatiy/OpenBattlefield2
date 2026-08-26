// mesh_info — регресія парсера мешів на справжніх даних.
//
//   mesh_info <modDir> --all [staticmesh|bundledmesh|skinnedmesh]
//   mesh_info <modDir> <шлях/усередині/vfs.staticmesh>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "obf2/core/path.h"
#include "obf2/mesh/bf2_mesh.h"
#include "obf2/mesh/collision.h"
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

void printOne(obf2::FileSystem& files, const std::string& path, std::size_t geometryIndex = 0,
              std::size_t lodIndex = 0) {
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
    std::printf(" usage=%u@%u(type %u)", a.usage, a.offset, a.vartype);
  }
  std::putchar('\n');

  for (std::size_t g = 0; g < mesh->geometries.size(); ++g) {
    std::printf("  geom %zu: lod-ів %zu\n", g, mesh->geometries[g].lods.size());
  }

  const auto render = obf2::mesh::extract(*mesh, geometryIndex, lodIndex, &error);
  if (!render) { std::fprintf(stderr, "  extract: %s\n", error.c_str()); return; }

  if (!render->vertexPart.empty()) {
    std::map<int, int> partHistogram;
    for (const std::uint8_t part : render->vertexPart) ++partHistogram[part];
    std::printf("  частини (geometryPart -> вершин):");
    for (const auto& [part, count] : partHistogram) std::printf(" %d:%d", part, count);
    std::putchar('\n');
  }
  std::printf("  geom %zu lod %zu: вершин %zu, індексів %zu (%zu трикутників), діапазонів %zu\n",
              geometryIndex, lodIndex, render->vertices.size(), render->indices.size(),
              render->indices.size() / 3, render->ranges.size());
  // Сирі дані прив'язки вершин: вага й пара номерів кісток. Потрібні, щоб
  // зрозуміти, як саме BLENDINDICES пакує пару в один D3DCOLOR.
  if (mesh->kind == obf2::mesh::Kind::Skinned) {
    std::size_t weightOffset = 0, indexOffset = 0;
    bool hasWeight = false, hasIndex = false;
    for (const auto& attribute : mesh->attributes) {
      if (attribute.flag != 0) continue;
      if (attribute.usage == 1) { weightOffset = attribute.offset / 4; hasWeight = true; }
      if (attribute.usage == 2) { indexOffset = attribute.offset / 4; hasIndex = true; }
    }
    if (hasWeight && hasIndex) {
      const std::size_t stride = mesh->floatsPerVertex();
      std::printf("  прив'язка вершин (вага, байти BLENDINDICES):\n");
      for (std::size_t v = 0; v < mesh->vertexCount && v < 8; ++v) {
        const float weight = mesh->vertexData[v * stride + weightOffset];
        float raw = mesh->vertexData[v * stride + indexOffset];
        std::uint32_t bits = 0;
        std::memcpy(&bits, &raw, 4);
        std::printf("    %zu: вага %.3f  байти %u %u %u %u\n", v, weight, bits & 0xff,
                    (bits >> 8) & 0xff, (bits >> 16) & 0xff, (bits >> 24) & 0xff);
      }
    }
  }

  // Риґи скінінгу: які кістки бере кожен матеріал.
  if (mesh->kind == obf2::mesh::Kind::Skinned) {
    for (std::size_t g = 0; g < mesh->geometries.size(); ++g) {
      for (std::size_t l = 0; l < mesh->geometries[g].lods.size(); ++l) {
        const auto& lod = mesh->geometries[g].lods[l];
        for (std::size_t r = 0; r < lod.rigs.size(); ++r) {
          std::printf("  geom %zu lod %zu риґ %zu (матеріалів %zu): кісток %zu, номери:", g, l, r,
                      lod.materials.size(), lod.rigs[r].bones.size());
          for (std::size_t b = 0; b < lod.rigs[r].bones.size() && b < 16; ++b) {
            std::printf(" %u", lod.rigs[r].bones[b].id);
          }
          std::printf("\n");
        }
      }
    }
  }

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

  // Розвідка: у якому порядку обходяться вершини трикутника. Порівнюємо
  // геометричну нормаль (векторний добуток ребер) із нормалями вершин, які
  // художник задав явно. Якщо вони дивляться в один бік — обхід проти
  // годинникової стрілки, і саме такі грані лицьові.
  // Регресія парсера зіткнень на всіх .collisionmesh гри.
  if (what == "--collision") {
    int parsed = 0, failed = 0;
    long long triangles = 0;
    std::map<std::string, int> reasons;
    std::map<int, int> layerTypes;

    auto paths = files.list();
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    for (const auto& path : paths) {
      if (obf2::assetExtension(path) != "collisionmesh") continue;
      const auto bytes = files.read(path);
      if (!bytes) continue;

      std::string error;
      const auto mesh = obf2::mesh::loadCollisionMesh(*bytes, &error);
      if (!mesh) {
        ++failed;
        ++reasons[error.substr(0, error.find(" (зсув"))];
        if (failed <= 3) {
          std::uint32_t major = 0, minor = 0;
          if (bytes->size() >= 8) {
            std::memcpy(&major, bytes->data(), 4);
            std::memcpy(&minor, bytes->data() + 4, 4);
          }
          std::fprintf(stderr, "  %s: версія %u.%u — %s\n", path.c_str(), major, minor,
                       error.c_str());
        }
        continue;
      }
      ++parsed;
      for (const auto& layer : mesh->layers) {
        triangles += static_cast<long long>(layer.faces.size());
        ++layerTypes[static_cast<int>(layer.type)];
      }
    }

    std::printf("розібрано: %d, не розібрано: %d\n", parsed, failed);
    std::printf("трикутників зіткнень: %lld\n", triangles);
    std::puts("шари за призначенням:");
    for (const auto& [type, count] : layerTypes) {
      const char* name = type == 0   ? "снаряди"
                         : type == 1 ? "техніка"
                         : type == 2 ? "солдат"
                         : type == 3 ? "боти"
                                     : "?";
      std::printf("  %-10s %d\n", name, count);
    }
    for (const auto& [reason, count] : reasons) std::printf("  %-44s %d\n", reason.c_str(), count);
    return failed == 0 ? 0 : 1;
  }

  if (what == "--winding") {
    long long agree = 0, disagree = 0, degenerate = 0;
    int meshesScanned = 0;

    auto paths = files.list();
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    for (const auto& path : paths) {
      const auto kind = obf2::mesh::kindFromExtension(obf2::assetExtension(path));
      if (!kind) continue;
      const auto bytes = files.read(path);
      if (!bytes) continue;
      const auto mesh = obf2::mesh::load(*bytes, *kind);
      if (!mesh) continue;
      const auto render = obf2::mesh::extract(*mesh, 0, 0);
      if (!render) continue;
      ++meshesScanned;

      for (std::size_t i = 0; i + 2 < render->indices.size(); i += 3) {
        const auto& a = render->vertices[render->indices[i]];
        const auto& b = render->vertices[render->indices[i + 1]];
        const auto& c = render->vertices[render->indices[i + 2]];

        const float e1[3] = {b.position.x - a.position.x, b.position.y - a.position.y,
                             b.position.z - a.position.z};
        const float e2[3] = {c.position.x - a.position.x, c.position.y - a.position.y,
                             c.position.z - a.position.z};
        const float face[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                               e1[0] * e2[1] - e1[1] * e2[0]};
        const float shading[3] = {a.normal.x + b.normal.x + c.normal.x,
                                  a.normal.y + b.normal.y + c.normal.y,
                                  a.normal.z + b.normal.z + c.normal.z};
        const float dot = face[0] * shading[0] + face[1] * shading[1] + face[2] * shading[2];

        if (face[0] == 0.0f && face[1] == 0.0f && face[2] == 0.0f) ++degenerate;
        else if (dot > 0.0f) ++agree;
        else if (dot < 0.0f) ++disagree;
      }
    }

    const long long total = agree + disagree;
    std::printf("мешів: %d, трикутників: %lld (вироджених %lld)\n", meshesScanned, total,
                degenerate);
    if (total > 0) {
      std::printf("  обхід збігається з нормалями вершин: %lld (%.2f%%)\n", agree,
                  100.0 * static_cast<double>(agree) / static_cast<double>(total));
      std::printf("  протилежний:                        %lld (%.2f%%)\n", disagree,
                  100.0 * static_cast<double>(disagree) / static_cast<double>(total));
    }
    return 0;
  }

  if (what != "--all") {
    const std::size_t geometryIndex = argc > 3 ? static_cast<std::size_t>(std::atoi(argv[3])) : 0;
    const std::size_t lodIndex = argc > 4 ? static_cast<std::size_t>(std::atoi(argv[4])) : 0;
    printOne(files, obf2::normalizeAssetPath(what), geometryIndex, lodIndex);
    return 0;
  }

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
