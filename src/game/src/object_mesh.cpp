#include "obf2/game/object_mesh.h"

#include <algorithm>
#include <cstdio>

#include "obf2/core/path.h"
#include "obf2/game/scene.h"
#include "obf2/mesh/material.h"

namespace obf2::game {
namespace {

// Adds a mesh transformed by a matrix to the target. The indices are shifted by
// the vertices already present, the materials' ranges by the indices already there.
void appendMesh(mesh::RenderMesh& target, const mesh::RenderMesh& source, const Mat4& transform) {
  const auto vertexBase = static_cast<std::uint32_t>(target.vertices.size());
  const auto indexBase = static_cast<std::uint32_t>(target.indices.size());

  for (const auto& vertex : source.vertices) {
    mesh::Vertex moved = vertex;
    const Vec3f at =
        transformPoint(transform, Vec3f{vertex.position.x, vertex.position.y, vertex.position.z});
    moved.position = {at.x, at.y, at.z};
    // The normals are rotated without translation: these transforms have no
    // scale, so an ordinary multiplication by the upper 3x3 block is enough.
    const Vec3f n =
        transformDirection(transform, Vec3f{vertex.normal.x, vertex.normal.y, vertex.normal.z});
    moved.normal = {n.x, n.y, n.z};
    target.vertices.push_back(moved);
  }
  for (const auto index : source.indices) target.indices.push_back(index + vertexBase);
  for (auto range : source.ranges) {
    range.indexStart += indexBase;
    target.ranges.push_back(std::move(range));
  }
}

}  // namespace

std::size_t pickGeometry(const mesh::Mesh& mesh, std::size_t lodIndex) {
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

std::string resolveGeometryPath(FileSystem& files, const std::string& templateFile,
                                const std::string& geometryName) {
  if (geometryName.empty()) return {};
  const std::string_view dir = assetParentDir(templateFile);
  for (const char* extension : {".staticmesh", ".bundledmesh", ".skinnedmesh"}) {
    for (const char* subdirectory : {"meshes/", ""}) {
      const std::string candidate =
          joinAssetPath(dir, std::string(subdirectory) + geometryName + extension);
      if (files.exists(candidate)) return candidate;
    }
  }
  return {};
}

std::optional<mesh::RenderMesh> loadMesh(FileSystem& files, const std::string& path,
                                         int geometryOverride, int lodIndex, bool verbose) {
  const std::string normalized = normalizeAssetPath(path);
  const auto kind = mesh::kindFromExtension(assetExtension(normalized));
  if (!kind) return std::nullopt;

  const auto bytes = files.read(normalized);
  if (!bytes) {
    if (verbose) std::fprintf(stderr, "mesh not found in the VFS: %s\n", normalized.c_str());
    return std::nullopt;
  }

  std::string error;
  const auto parsed = mesh::load(*bytes, *kind, &error);
  if (!parsed) {
    if (verbose) std::fprintf(stderr, "could not parse %s: %s\n", normalized.c_str(), error.c_str());
    return std::nullopt;
  }

  const std::size_t lod = static_cast<std::size_t>(std::max(0, lodIndex));
  const std::size_t geometry =
      geometryOverride >= 0 ? static_cast<std::size_t>(geometryOverride) : pickGeometry(*parsed, lod);

  auto render = mesh::extract(*parsed, geometry, lod, &error);
  if (!render) {
    if (verbose) std::fprintf(stderr, "could not unpack %s: %s\n", normalized.c_str(), error.c_str());
    return std::nullopt;
  }

  // Whether this mesh is vegetation is decided by its path, and by nothing
  // else — that is the engine's own test (`obf2::mesh::isVegetationPath`).
  mesh::markVegetationLeaves(*render, normalized);

  if (verbose) {
    std::printf("mesh: %s\n  version %u, geom %zu/%zu, lod %zu, vertices %zu, triangles %zu, "
                "materials %zu\n",
                normalized.c_str(), parsed->header.version, geometry, parsed->geometries.size(),
                lod, render->vertices.size(), render->indices.size() / 3, render->ranges.size());
  }
  return render;
}

std::optional<mesh::RenderMesh> buildObjectMesh(FileSystem& files, const Registry& registry,
                                                const std::string& templateName,
                                                int geometryOverride, int lodIndex, bool verbose) {
  const auto* root = registry.find(templateName);
  if (root == nullptr) return std::nullopt;

  const auto instance = flattenObject(registry, templateName);
  if (!instance) return std::nullopt;

  if (instance->geometryName.empty()) {
    mesh::RenderMesh merged;
    int added = 0;
    for (const auto& part : instance->parts) {
      if (part.geometryName.empty()) continue;
      const std::string path = resolveGeometryPath(files, part.file, part.geometryName);
      if (path.empty()) continue;
      const auto piece = loadMesh(files, path, geometryOverride, lodIndex, false);
      if (!piece) continue;
      appendMesh(merged, *piece, part.transform);
      ++added;
    }
    if (added == 0) return std::nullopt;
    // The bounds are computed by us: the meshes came from different files, and
    // each brought its own, in its own coordinates.
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
      std::printf("  root without geometry: assembled from %d child meshes, vertices %zu\n", added,
                  merged.vertices.size());
    }
    return merged;
  }

  const std::string path = resolveGeometryPath(files, root->file, instance->geometryName);
  if (path.empty()) return std::nullopt;

  auto render = loadMesh(files, path, geometryOverride, lodIndex, verbose);
  if (!render) return std::nullopt;

  const auto transforms = partTransformMap(*instance);
  const std::size_t moved = applyPartTransforms(*render, transforms);
  if (verbose) {
    std::printf("  nodes in the tree: %zu, depth: %d, cycles: %d\n"
                "  parts with a transform: %zu, vertices moved: %zu of %zu\n",
                instance->parts.size(), instance->maxDepth, instance->cycles, transforms.size(),
                moved, render->vertices.size());
  }
  return render;
}

std::string_view drawStageName(DrawStage stage) {
  switch (stage) {
    case DrawStage::Drawn: return "drawn";
    case DrawStage::NoTemplate: return "no template";
    case DrawStage::NoTree: return "the tree did not assemble";
    case DrawStage::NoGeometryName: return "no geometry anywhere";
    case DrawStage::GeometryInChild: return "assembled from children";
    case DrawStage::NoGeometryFile: return "file not found";
    case DrawStage::NoMesh: return "the mesh did not parse";
  }
  return "?";
}

DrawStage checkDrawable(FileSystem& files, const Registry& registry,
                        const std::string& templateName, int geometryOverride, int lodIndex) {
  const auto* root = registry.find(templateName);
  if (root == nullptr) return DrawStage::NoTemplate;

  const auto instance = flattenObject(registry, templateName);
  if (!instance) return DrawStage::NoTree;
  if (instance->geometryName.empty()) {
    // A tree may carry its geometry somewhere other than the root: a control point
    // has no mesh of its own, and the flag arrives from `addTemplate flagpole`.
    // We do not merely look for the file but really assemble the mesh: otherwise
    // "assembled" would only mean "it looks as though it should assemble".
    const auto merged =
        buildObjectMesh(files, registry, templateName, geometryOverride, lodIndex, false);
    if (merged && !merged->vertices.empty()) return DrawStage::GeometryInChild;
    return DrawStage::NoGeometryName;
  }

  const std::string path = resolveGeometryPath(files, root->file, instance->geometryName);
  if (path.empty()) return DrawStage::NoGeometryFile;

  if (!loadMesh(files, path, geometryOverride, lodIndex, false)) return DrawStage::NoMesh;
  return DrawStage::Drawn;
}

}  // namespace obf2::game
