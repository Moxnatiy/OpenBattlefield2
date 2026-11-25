// A `.collisionmesh` lod's tree and its line walk (obf2/mesh/collision.h,
// docs/functions/collision-bsp.md), on a real mesh from the user's
// installation. Without one the test says so and passes. `OBF2_GAME_FILES` in
// the environment overrides the build's path (a worktree has no `Game Files`).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

#include "check.h"
#include "obf2/core/path.h"
#include "obf2/mesh/collision.h"
#include "obf2/server/collision_world.h"
#include "obf2/vfs/filesystem.h"

using namespace obf2;

namespace {

// 63 742 bytes in Objects_server.zip; its Projectile lod has a tree.
constexpr const char* kMesh =
    "staticobjects/industry/ind_buildings/gatehouse/meshes/gatehouse.collisionmesh";

std::filesystem::path gameFiles() {
  if (const char* root = std::getenv("OBF2_GAME_FILES")) return root;
  return OBF2_GAME_FILES;
}

std::optional<mesh::CollisionMesh> loadMesh() {
  const auto zip = gameFiles() / "mods" / "bf2" / "Objects_server.zip";
  if (!std::filesystem::exists(zip)) return std::nullopt;
  auto archive = ZipArchive::open(zip);
  if (!archive) return std::nullopt;
  const auto data = archive->read(normalizeAssetPath(kMesh));
  if (!data) return std::nullopt;
  std::string error;
  auto out = mesh::loadCollisionMesh(*data, &error);
  if (!out) std::printf("  %s\n", error.c_str());
  return out;
}

mesh::Vec3 sub(const mesh::Vec3& a, const mesh::Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(const mesh::Vec3& a, const mesh::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
mesh::Vec3 crossV(const mesh::Vec3& a, const mesh::Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// A one-sided crossing with the same outputs as checkFaceAndEdgeCollision:
// the end on or under the plane, the start on or above, against the normal,
// inside the triangle; distance (t − 1)·|motion|.
bool crossing(const mesh::CollisionLayer& layer, std::uint16_t face, const mesh::Vec3& n,
              const mesh::Vec3& from, const mesh::Vec3& to, mesh::BspFaceHit& hit) {
  const auto& f = layer.faces[face];
  const mesh::Vec3& a = layer.vertices[f.a];
  const mesh::Vec3& b = layer.vertices[f.b];
  const mesh::Vec3& c = layer.vertices[f.c];
  const mesh::Vec3 d = sub(to, from);
  const float endDepth = dot(sub(to, a), n);
  const float fromDepth = dot(sub(from, a), n);
  const float dn = dot(n, d);
  if (endDepth > 0.0f || fromDepth < 0.0f || dn >= 0.0f) return false;
  const float t = fromDepth / -dn;
  const mesh::Vec3 p{from.x + d.x * t, from.y + d.y * t, from.z + d.z * t};
  const float s0 = dot(crossV(sub(b, a), sub(p, a)), n);
  const float s1 = dot(crossV(sub(c, b), sub(p, b)), n);
  const float s2 = dot(crossV(sub(a, c), sub(p, c)), n);
  if (!((s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0))) return false;
  hit.point = p;
  hit.depth = endDepth;
  hit.distance = (t - 1.0f) * std::sqrt(dot(d, d));
  return true;
}

void testTree(const mesh::CollisionLayer& layer) {
  const mesh::CollisionBsp& bsp = *layer.bsp;
  std::printf("  faces %zu, nodes %zu, references %zu\n", layer.faces.size(), bsp.nodes.size(),
              bsp.faceRefs.size());
  CHECK(!bsp.nodes.empty());
  CHECK_EQ(bsp.normals.size(), layer.faces.size());
  // The layout read right: every child in range, every leaf's references in
  // range, and every face referenced by some leaf.
  std::set<std::uint16_t> referenced;
  std::size_t bad = 0;
  for (const mesh::CollisionBspNode& node : bsp.nodes) {
    if (node.axis() == 3) ++bad;
    for (int side = 0; side < 2; ++side) {
      if (!node.leaf(side)) {
        if (node.child[side] >= bsp.nodes.size()) ++bad;
        continue;
      }
      if (node.child[side] + node.count(side) > bsp.faceRefs.size()) {
        ++bad;
        continue;
      }
      for (std::uint32_t i = 0; i < node.count(side); ++i) {
        const std::uint16_t face = bsp.faceRefs[node.child[side] + i];
        if (face >= layer.faces.size()) ++bad;
        referenced.insert(face);
      }
    }
  }
  CHECK_EQ(bad, std::size_t{0});
  CHECK_EQ(referenced.size(), layer.faces.size());
}

// Segments across the tree's box: without stopFirst the walk finds what a
// test of every face finds; with it, a face is found exactly when one is
// crossed, and it is not always the nearest.
void testWalk(const mesh::CollisionLayer& layer) {
  const mesh::Aabb box = layer.bsp->bounds;
  std::uint32_t seed = 12345;
  const auto random = [&](float lo, float hi) {
    seed = seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * static_cast<float>(seed >> 8) / 16777216.0f;
  };
  const auto test = [&](std::uint16_t face, const mesh::Vec3& n, const mesh::Vec3& from,
                        const mesh::Vec3& to, mesh::BspFaceHit& hit) {
    return crossing(layer, face, n, from, to, hit);
  };
  int hits = 0, nearestMismatch = 0, foundMismatch = 0, firstNotNearest = 0;
  for (int i = 0; i < 4000; ++i) {
    const mesh::Vec3 from{random(box.min.x - 2, box.max.x + 2), random(box.min.y - 2, box.max.y + 2),
                          random(box.min.z - 2, box.max.z + 2)};
    const mesh::Vec3 to{random(box.min.x - 2, box.max.x + 2), random(box.min.y - 2, box.max.y + 2),
                        random(box.min.z - 2, box.max.z + 2)};
    const mesh::Vec3 motion = sub(to, from);
    std::optional<float> brute;
    for (std::size_t f = 0; f < layer.faces.size(); ++f) {
      const mesh::Vec3& n = layer.bsp->normals[f];
      if (dot(n, motion) >= 0.0f) continue;
      mesh::BspFaceHit hit;
      if (crossing(layer, static_cast<std::uint16_t>(f), n, from, to, hit) &&
          (!brute || hit.distance < *brute)) {
        brute = hit.distance;
      }
    }
    const auto all = mesh::closestFace(layer, from, motion, false, false, false, test);
    const auto first = mesh::closestFace(layer, from, motion, false, false, true, test);
    if (brute) ++hits;
    if (brute.has_value() != all.has_value() || (brute && all->hit.distance != *brute)) ++nearestMismatch;
    if (brute.has_value() != first.has_value()) ++foundMismatch;
    if (brute && first && first->hit.distance != *brute) ++firstNotNearest;
  }
  std::printf("  4000 segments: %d cross a face; nearest differs %d; found differs %d; "
              "first face not the nearest %d\n",
              hits, nearestMismatch, foundMismatch, firstNotNearest);
  CHECK(hits > 100);
  CHECK_EQ(nearestMismatch, 0);
  CHECK_EQ(foundMismatch, 0);
}

// The world asks the tree: with firstFace its answer is the walk's first face.
void testWorld(const mesh::CollisionLayer& layer) {
  server::CollisionWorld world;
  world.addLayer(layer, Mat4::identity());
  const mesh::Aabb box = layer.bsp->bounds;
  const Vec3f centre{(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
                     (box.min.z + box.max.z) * 0.5f};
  const float reach = std::sqrt((box.max.x - box.min.x) * (box.max.x - box.min.x) +
                                (box.max.y - box.min.y) * (box.max.y - box.min.y) +
                                (box.max.z - box.min.z) * (box.max.z - box.min.z));
  int asked = 0, differ = 0;
  for (int i = 0; i < 64; ++i) {
    const float angle = static_cast<float>(i) * 0.098174770f;  // 2π / 64
    const Vec3f start{centre.x + std::cos(angle) * reach, centre.y + 1.0f, centre.z + std::sin(angle) * reach};
    const Vec3f motion = (centre - start) * 2.0f;
    const auto nearest = world.segmentContact(start, motion, start + motion * 0.5f, reach, true, false);
    const auto first = world.segmentContact(start, motion, start + motion * 0.5f, reach, true, true);
    CHECK_EQ(nearest.has_value(), first.has_value());
    if (!nearest || !first) continue;
    ++asked;
    CHECK(nearest->along <= first->along);
    if (first->along != nearest->along) ++differ;
  }
  std::printf("  world: %d rays hit, the first face differs from the nearest on %d\n", asked, differ);
  CHECK(asked > 0);
}

}  // namespace

void run() {
  const auto loaded = loadMesh();
  if (!loaded) {
    std::printf("  no %s under %s — the check was skipped\n", kMesh, gameFiles().string().c_str());
    return;
  }
  const mesh::CollisionLayer* layer = loaded->layer(mesh::ColType::Projectile);
  CHECK(layer != nullptr);
  if (layer == nullptr) return;
  CHECK(layer->bsp.has_value());
  if (!layer->bsp) return;
  testTree(*layer);
  testWalk(*layer);
  testWorld(*layer);
}

TEST_MAIN({ run(); })
