// mesh_info — a regression of the mesh parser on the real data.
//
//   mesh_info <modDir> --all [staticmesh|bundledmesh|skinnedmesh]
//   mesh_info <modDir> <path/inside/vfs.staticmesh>

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
  if (!kind) { std::fprintf(stderr, "unknown extension: %s\n", path.c_str()); return; }

  const auto bytes = files.read(path);
  if (!bytes) { std::fprintf(stderr, "not found: %s\n", path.c_str()); return; }

  std::string error;
  const auto mesh = obf2::mesh::load(*bytes, *kind, &error);
  if (!mesh) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str()); return; }

  std::printf("%s\n  kind: %s, version: %u, %zu bytes\n", path.c_str(),
              std::string(obf2::mesh::kindName(mesh->kind)).c_str(), mesh->header.version,
              bytes->size());
  std::printf("  vertices: %u (stride %u), indices: %zu, geom: %zu\n", mesh->vertexCount,
              mesh->vertexStride, mesh->indices.size(), mesh->geometries.size());

  std::printf("  attributes:");
  for (const auto& a : mesh->attributes) {
    if (a.flag != 0) continue;
    std::printf(" usage=%u@%u(type %u)", a.usage, a.offset, a.vartype);
  }
  std::putchar('\n');

  for (std::size_t g = 0; g < mesh->geometries.size(); ++g) {
    std::printf("  geom %zu: %zu lods\n", g, mesh->geometries[g].lods.size());
  }

  const auto render = obf2::mesh::extract(*mesh, geometryIndex, lodIndex, &error);
  if (!render) { std::fprintf(stderr, "  extract: %s\n", error.c_str()); return; }

  if (!render->vertexPart.empty()) {
    std::map<int, int> partHistogram;
    for (const std::uint8_t part : render->vertexPart) ++partHistogram[part];
    std::printf("  parts (geometryPart -> vertices):");
    for (const auto& [part, count] : partHistogram) std::printf(" %d:%d", part, count);
    std::putchar('\n');
  }
  std::printf("  geom %zu lod %zu: %zu vertices, %zu indices (%zu triangles), %zu ranges\n",
              geometryIndex, lodIndex, render->vertices.size(), render->indices.size(),
              render->indices.size() / 3, render->ranges.size());
  // The raw vertex binding data: the weight and a pair of bone numbers. Needed
  // to understand how exactly BLENDINDICES packs the pair into one D3DCOLOR.
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
      std::printf("  vertex binding (weight, BLENDINDICES bytes):\n");
      for (std::size_t v = 0; v < mesh->vertexCount && v < 8; ++v) {
        const float weight = mesh->vertexData[v * stride + weightOffset];
        float raw = mesh->vertexData[v * stride + indexOffset];
        std::uint32_t bits = 0;
        std::memcpy(&bits, &raw, 4);
        std::printf("    %zu: weight %.3f  bytes %u %u %u %u\n", v, weight, bits & 0xff,
                    (bits >> 8) & 0xff, (bits >> 16) & 0xff, (bits >> 24) & 0xff);
      }
    }
  }

  // Skinning rigs: which bones each material takes.
  if (mesh->kind == obf2::mesh::Kind::Skinned) {
    for (std::size_t g = 0; g < mesh->geometries.size(); ++g) {
      for (std::size_t l = 0; l < mesh->geometries[g].lods.size(); ++l) {
        const auto& lod = mesh->geometries[g].lods[l];
        for (std::size_t r = 0; r < lod.rigs.size(); ++r) {
          std::printf("  geom %zu lod %zu rig %zu (%zu materials): %zu bones, numbers:", g, l, r,
                      lod.materials.size(), lod.rigs[r].bones.size());
          for (std::size_t b = 0; b < lod.rigs[r].bones.size() && b < 16; ++b) {
            std::printf(" %u", lod.rigs[r].bones[b].id);
          }
          std::printf("\n");
        }
      }
    }
  }

  // The range of every TEXCOORD set. A light map's unwrap is unique and lives
  // in [0,1]; a detail set tiles and runs far past it. This is how to tell them
  // apart without guessing which index means what.
  {
    const std::size_t stride = mesh->floatsPerVertex();
    for (const auto& attribute : mesh->attributes) {
      if (attribute.flag != 0) continue;
      if ((attribute.usage & 0xFF) != 5) continue;  // TEXCOORD
      const std::size_t at = attribute.offset / sizeof(float);
      float minU = 1e30f, maxU = -1e30f, minV = 1e30f, maxV = -1e30f;
      for (std::uint32_t v = 0; v < mesh->vertexCount; ++v) {
        const std::size_t base = static_cast<std::size_t>(v) * stride + at;
        if (base + 1 >= mesh->vertexData.size()) break;
        const float u = mesh->vertexData[base], w = mesh->vertexData[base + 1];
        minU = std::min(minU, u); maxU = std::max(maxU, u);
        minV = std::min(minV, w); maxV = std::max(maxV, w);
      }
      std::printf("  TEXCOORD%u (usage %#x): u %.3f..%.3f  v %.3f..%.3f\n",
                  attribute.usage >> 8, attribute.usage, minU, maxU, minV, maxV);
    }
  }

  std::printf("  bbox: %.2f/%.2f/%.2f .. %.2f/%.2f/%.2f\n", render->bounds.min.x,
              render->bounds.min.y, render->bounds.min.z, render->bounds.max.x,
              render->bounds.max.y, render->bounds.max.z);
  for (std::size_t i = 0; i < render->ranges.size() && i < 4; ++i) {
    const auto& range = render->ranges[i];
    std::printf("    [%zu] %s / %s, alphaMode %u, %u indices, %zu textures\n", i,
                range.fxFile.c_str(), range.technique.c_str(), range.alphaMode, range.indexCount,
                range.maps.size());
    for (std::size_t slot = 0; slot < range.maps.size(); ++slot) {
      std::printf("         slot %zu: %s\n", slot, range.maps[slot].c_str());
    }
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

  // Which TEXCOORD set holds the light map's unwrap. The engine names it per
  // material (`TexLightMapInd`) and we cannot read that, so the question is
  // whether a rule holds over the whole corpus: a light map's unwrap is unique
  // and lives inside [0,1], while the detail sets tile and run far past it.
  if (what == "--lightmapuv") {
    int meshes = 0, withSets = 0, lastInUnitSquare = 0, earlierInUnitSquare = 0;
    std::map<int, int> setCounts;
    auto scan = files.list();
    std::sort(scan.begin(), scan.end());
    scan.erase(std::unique(scan.begin(), scan.end()), scan.end());
    for (const auto& path : scan) {
      if (obf2::assetExtension(path) != "staticmesh") continue;
      const auto bytes = files.read(path);
      if (!bytes) continue;
      const auto mesh = obf2::mesh::load(*bytes, obf2::mesh::Kind::Static);
      if (!mesh || mesh->vertexCount == 0) continue;
      ++meshes;

      const std::size_t stride = mesh->floatsPerVertex();
      std::vector<std::pair<int, std::size_t>> sets;  // set index -> float offset
      for (const auto& attribute : mesh->attributes) {
        if (attribute.flag != 0) continue;
        if ((attribute.usage & 0xFF) != 5) continue;
        sets.emplace_back(attribute.usage >> 8, attribute.offset / sizeof(float));
      }
      ++setCounts[static_cast<int>(sets.size())];
      if (sets.size() < 2) continue;
      ++withSets;
      std::sort(sets.begin(), sets.end());

      auto insideUnitSquare = [&](std::size_t at) {
        for (std::uint32_t v = 0; v < mesh->vertexCount; ++v) {
          const std::size_t base = static_cast<std::size_t>(v) * stride + at;
          if (base + 1 >= mesh->vertexData.size()) return false;
          const float u = mesh->vertexData[base], w = mesh->vertexData[base + 1];
          if (u < -0.001f || u > 1.001f || w < -0.001f || w > 1.001f) return false;
        }
        return true;
      };
      if (insideUnitSquare(sets.back().second)) ++lastInUnitSquare;
      for (std::size_t i = 0; i + 1 < sets.size(); ++i) {
        if (insideUnitSquare(sets[i].second)) { ++earlierInUnitSquare; break; }
      }
    }
    std::printf("static meshes: %d, with two or more TEXCOORD sets: %d\n", meshes, withSets);
    std::printf("  last set inside [0,1]:     %d\n", lastInUnitSquare);
    std::printf("  some earlier set inside:   %d\n", earlierInUnitSquare);
    std::puts("TEXCOORD sets per mesh:");
    for (const auto& [count, howMany] : setCounts) {
      std::printf("  %d sets: %d meshes\n", count, howMany);
    }
    return 0;
  }

  // Does anything in a mesh's material tell a leaf from a wall?
  //
  // The renderer picks between `RaShaderLeaf` and the static-mesh shaders on a
  // flag it holds per material (docs/research/12-renddx9.md), and where that
  // flag comes from is not established. This walks every material in the game
  // and splits them by what the artists called the base texture — `leaf_*` and
  // the like — then prints what each side's parsed fields look like. If some
  // field is the flag, the two columns differ on it.
  if (what == "--leafflag") {
    struct Side {
      int count = 0;
      std::map<std::uint32_t, int> alphaModes;
      std::map<std::uint32_t, int> u5, u6, nodeIndex;
      std::map<std::string, int> techniques;
    };
    Side leaf, other;
    // And the same question asked only of the meshes the engine calls
    // vegetation: it decides that by the path (`RendDX9.dll`, 0x1011acd0,
    // `StaticMeshTemplate::load` searches the file name for "vegitation").
    // Inside those, is the alpha-tested material the leaves?
    int vegLeafAlpha = 0, vegLeafPlain = 0, vegOtherAlpha = 0, vegOtherPlain = 0;

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
            const std::string& base = material.maps.empty() ? std::string() : material.maps.front();
            std::string name = base;
            const std::size_t slash = name.find_last_of('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
            for (char& c : name) c = static_cast<char>(std::tolower(c));
            const bool isLeafName = name.find("leaf") != std::string::npos;
            if (path.find("vegitation") != std::string::npos) {
              const bool tested = material.alphaMode == 2;
              if (isLeafName) (tested ? vegLeafAlpha : vegLeafPlain)++;
              else (tested ? vegOtherAlpha : vegOtherPlain)++;
            }
            Side& side = isLeafName ? leaf : other;
            ++side.count;
            ++side.alphaModes[material.alphaMode];
            ++side.u5[material.u5];
            ++side.u6[material.u6];
            ++side.nodeIndex[material.nodeIndex];
            ++side.techniques[material.technique];
          }
        }
      }
    }

    auto show = [](const char* title, const Side& side) {
      std::printf("%s: %d materials\n", title, side.count);
      auto top = [](const char* what, const std::map<std::uint32_t, int>& counts) {
        std::printf("    %-10s", what);
        int shown = 0;
        for (const auto& [value, howMany] : counts) {
          if (shown++ >= 6) { std::printf(" ..."); break; }
          std::printf(" %u:%d", value, howMany);
        }
        std::putchar('\n');
      };
      top("alphaMode", side.alphaModes);
      top("u5", side.u5);
      top("u6", side.u6);
      top("nodeIndex", side.nodeIndex);
      std::printf("    techniques");
      int shown = 0;
      for (const auto& [name, howMany] : side.techniques) {
        if (shown++ >= 4) { std::printf(" ..."); break; }
        std::printf(" %s:%d", name.c_str(), howMany);
      }
      std::putchar('\n');
    };
    show("base texture named leaf*", leaf);
    show("every other material", other);
    std::printf(
        "\ninside a mesh whose path says vegetation (the engine's own test):\n"
        "  named leaf*, alpha-tested:      %d\n"
        "  named leaf*, not tested:        %d\n"
        "  named otherwise, alpha-tested:  %d\n"
        "  named otherwise, not tested:    %d\n",
        vegLeafAlpha, vegLeafPlain, vegOtherAlpha, vegOtherPlain);
    return 0;
  }

  // Which shader every material asks for. The mesh names its own `.fx` file, so
  // the engine does not have to guess how a surface is lit: leaves say
  // `RaShaderLeaf.fx`, walls say `RaShaderSTM.fx`, and the two are lit by
  // different formulas. This mode prints the whole corpus's distribution and,
  // for each file, one mesh that uses it so the claim can be checked by hand.
  if (what == "--fx") {
    std::map<std::string, int> byFx;
    std::map<std::string, std::string> firstMesh;
    std::map<std::string, std::map<std::string, int>> techniquesOfFx;

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
            ++byFx[material.fxFile];
            ++techniquesOfFx[material.fxFile][material.technique];
            firstMesh.emplace(material.fxFile, path);
          }
        }
      }
    }

    std::vector<std::pair<std::string, int>> sorted(byFx.begin(), byFx.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::puts("fxFile over every .staticmesh in the mod:");
    for (const auto& [fx, count] : sorted) {
      std::printf("  %-28s %6d   %s\n", fx.c_str(), count, firstMesh[fx].c_str());
      const auto& techniques = techniquesOfFx[fx];
      std::vector<std::pair<std::string, int>> byCount(techniques.begin(), techniques.end());
      std::sort(byCount.begin(), byCount.end(),
                [](auto& a, auto& b) { return a.second > b.second; });
      for (std::size_t i = 0; i < byCount.size() && i < 4; ++i) {
        std::printf("      %-24s %6d\n", byCount[i].first.c_str(), byCount[i].second);
      }
      if (byCount.size() > 4) std::printf("      ... %zu more techniques\n", byCount.size() - 4);
    }
    return 0;
  }

  // How a mesh's height maps to its first unwrap: `mesh_info <mod> --heightuv
  // <path>`. Written for the sky dome, whose texture is a band from the zenith
  // down to a horizon the artists painted the fog's own colour — so where the
  // dome's own y stops being sky and starts being the skirt below the horizon
  // is a question about the mesh and not about the shader.
  if (what == "--heightuv") {
    if (argc < 4) {
      std::fputs("usage: mesh_info <modDir> --heightuv <path.staticmesh>\n", stderr);
      return 2;
    }
    const std::string path = obf2::normalizeAssetPath(argv[3]);
    const auto bytes = files.read(path);
    if (!bytes) {
      std::fprintf(stderr, "not found: %s\n", path.c_str());
      return 1;
    }
    const auto mesh = obf2::mesh::load(*bytes, obf2::mesh::Kind::Static);
    if (!mesh) {
      std::fputs("could not read the mesh\n", stderr);
      return 1;
    }
    const auto render = obf2::mesh::extract(*mesh);
    if (!render) {
      std::fputs("could not unpack the mesh\n", stderr);
      return 1;
    }
    std::vector<std::pair<float, float>> pairs;
    for (const auto& vertex : render->vertices) {
      pairs.emplace_back(vertex.position.y, vertex.uv[1]);
    }
    std::sort(pairs.begin(), pairs.end(), [](auto& a, auto& b) { return a.first > b.first; });
    if (pairs.empty()) return 0;
    std::printf("%zu vertices, y from %.1f to %.1f\n", pairs.size(), pairs.front().first,
                pairs.back().first);
    float lastY = 1e30f;
    for (const auto& [y, v] : pairs) {
      if (std::abs(y - lastY) < 0.5f) continue;
      lastY = y;
      std::printf("   y %9.2f   v %.4f\n", static_cast<double>(y), static_cast<double>(v));
    }
    return 0;
  }

  // Where a surface's transparency is written. A static mesh says it in
  // `alphaMode` — 2 is the alpha-tested one, the leaves and the grates. The
  // other two kinds do not use that field at all: they name a technique of
  // `SkinnedMesh.fx` or `BundledMesh.fx` and the technique's own name says it,
  // `Alpha_Test` or `Alpha`. The washing on Karkand's lines is one of those, and
  // it hung there as an opaque sheet until this mode was written.
  //
  // Prints, for every mesh kind: the `.fx` file, the technique and the
  // alphaMode, with a mesh that uses each so the claim can be checked by hand.
  if (what == "--alpha") {
    struct Row {
      int count = 0;
      std::string example;
    };
    std::map<std::string, Row> rows;

    auto scan = files.list();
    std::sort(scan.begin(), scan.end());
    scan.erase(std::unique(scan.begin(), scan.end()), scan.end());

    for (const auto& path : scan) {
      const std::string_view extension = obf2::assetExtension(path);
      obf2::mesh::Kind kind = obf2::mesh::Kind::Static;
      if (extension == "staticmesh") kind = obf2::mesh::Kind::Static;
      else if (extension == "bundledmesh") kind = obf2::mesh::Kind::Bundled;
      else if (extension == "skinnedmesh") kind = obf2::mesh::Kind::Skinned;
      else continue;

      const auto bytes = files.read(path);
      if (!bytes) continue;
      const auto mesh = obf2::mesh::load(*bytes, kind);
      if (!mesh) continue;
      for (const auto& geometry : mesh->geometries) {
        for (const auto& lod : geometry.lods) {
          for (const auto& material : lod.materials) {
            const std::string key = std::string(extension) + "  " + material.fxFile + "  " +
                                    material.technique + "  alphaMode " +
                                    std::to_string(material.alphaMode);
            Row& row = rows[key];
            ++row.count;
            if (row.example.empty()) row.example = path;
          }
        }
      }
    }

    std::vector<std::pair<std::string, Row>> sorted(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end(),
              [](auto& a, auto& b) { return a.second.count > b.second.count; });
    std::puts("kind, fx, technique, alphaMode over every mesh in the mod:");
    for (const auto& [key, row] : sorted) {
      std::printf("  %6d  %-66s %s\n", row.count, key.c_str(), row.example.c_str());
    }
    return 0;
  }

  // Reconnaissance: which techniques occur and what lies in each texture slot.
  // Needed to work out which slot to take for the base colour.
  if (what == "--materials") {
    std::map<std::string, int> techniques;
    std::map<std::string, int> slotSuffix;  // "slot N: suffix" -> how many times
    // alphaMode is what turns the shader's alpha test on, so it is worth seeing
    // which techniques carry which value.
    std::map<std::string, int> alphaModes;

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
            ++alphaModes[std::to_string(material.alphaMode) + "  " + material.technique];
            for (std::size_t slot = 0; slot < material.maps.size() && slot < 4; ++slot) {
              const std::string& map = material.maps[slot];
              const std::size_t dot = map.find_last_of('.');
              const std::size_t underscore = map.find_last_of('_', dot);
              std::string suffix = (underscore == std::string::npos || dot == std::string::npos)
                                       ? "<no suffix>"
                                       : map.substr(underscore, dot - underscore);
              ++slotSuffix["slot " + std::to_string(slot) + ": " + suffix];
            }
          }
        }
      }
    }

    std::puts("alphaMode + technique (top 12):");
    std::vector<std::pair<std::string, int>> sortedAlpha(alphaModes.begin(), alphaModes.end());
    std::sort(sortedAlpha.begin(), sortedAlpha.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < sortedAlpha.size() && i < 12; ++i) {
      std::printf("  %-44s %d\n", sortedAlpha[i].first.c_str(), sortedAlpha[i].second);
    }

    std::puts("\ntechnique (top 10):");
    std::vector<std::pair<std::string, int>> sortedTechniques(techniques.begin(), techniques.end());
    std::sort(sortedTechniques.begin(), sortedTechniques.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < sortedTechniques.size() && i < 10; ++i) {
      std::printf("  %-28s %d\n", sortedTechniques[i].first.c_str(), sortedTechniques[i].second);
    }

    std::puts("\ntexture suffixes by slot (top 14):");
    std::vector<std::pair<std::string, int>> sortedSlots(slotSuffix.begin(), slotSuffix.end());
    std::sort(sortedSlots.begin(), sortedSlots.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < sortedSlots.size() && i < 14; ++i) {
      std::printf("  %-28s %d\n", sortedSlots[i].first.c_str(), sortedSlots[i].second);
    }
    return 0;
  }

  // Reconnaissance: in which order the vertices of a triangle are walked. We
  // compare the geometric normal (the cross product of the edges) with the
  // vertex normals the artist set explicitly. When they look the same way the
  // walk is counter-clockwise, and those are the faces that face us.
  // A regression of the collision parser on every .collisionmesh in the game.
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
        ++reasons[error.substr(0, error.find(" (offset"))];
        if (failed <= 3) {
          std::uint32_t major = 0, minor = 0;
          if (bytes->size() >= 8) {
            std::memcpy(&major, bytes->data(), 4);
            std::memcpy(&minor, bytes->data() + 4, 4);
          }
          std::fprintf(stderr, "  %s: version %u.%u — %s\n", path.c_str(), major, minor,
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

    std::printf("parsed: %d, not parsed: %d\n", parsed, failed);
    std::printf("collision triangles: %lld\n", triangles);
    std::puts("layers by purpose:");
    for (const auto& [type, count] : layerTypes) {
      const char* name = type == 0   ? "projectiles"
                         : type == 1 ? "vehicles"
                         : type == 2 ? "soldier"
                         : type == 3 ? "bots"
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
    std::printf("meshes: %d, triangles: %lld (degenerate %lld)\n", meshesScanned, total,
                degenerate);
    if (total > 0) {
      std::printf("  the walk agrees with the vertex normals: %lld (%.2f%%)\n", agree,
                  100.0 * static_cast<double>(agree) / static_cast<double>(total));
      std::printf("  the opposite:                            %lld (%.2f%%)\n", disagree,
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
      ++failureKinds[error.substr(0, error.find(" (offset"))];
      if (firstFailures.size() < 5) firstFailures.push_back(path + ": " + error);
      continue;
    }
    ++parsed[std::string(extension)];
    vertices += mesh->vertexCount;

    if (auto render = obf2::mesh::extract(*mesh, 0, 0, &error)) {
      triangles += static_cast<long long>(render->indices.size() / 3);
    }
  }

  std::puts("parsed:");
  for (const auto& [extension, count] : parsed) std::printf("  %-14s %d\n", extension.c_str(), count);
  if (!failed.empty()) {
    std::puts("not parsed:");
    for (const auto& [extension, count] : failed) std::printf("  %-14s %d\n", extension.c_str(), count);
    std::puts("reasons:");
    for (const auto& [reason, count] : failureKinds) std::printf("  %-44s %d\n", reason.c_str(), count);
    std::puts("examples:");
    for (const auto& example : firstFailures) std::printf("  %s\n", example.c_str());
  }
  std::printf("\nvertices in total: %lld, triangles in lod0: %lld\n", vertices, triangles);
  return failed.empty() ? 0 : 1;
}
