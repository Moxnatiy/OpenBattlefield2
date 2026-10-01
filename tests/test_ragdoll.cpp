// The ragdoll (obf2/anim/ragdoll.h, docs/functions/ragdoll.md).
//
// The template is read from the user's installation; without one the test says
// so and passes. The network states are the server's own, from a live run kept
// in tests/data/bf2-ragdoll.bin.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

#include "check.h"
#include "obf2/anim/ragdoll.h"
#include "obf2/net/bf2_world.h"
#include "obf2/server/collision_world.h"

using namespace obf2;

namespace {

std::vector<std::vector<std::byte>> loadCapture(const std::string& path) {
  std::vector<std::vector<std::byte>> packets;
  std::ifstream in(path, std::ios::binary);
  std::uint32_t size = 0;
  while (in.read(reinterpret_cast<char*>(&size), sizeof(size))) {
    std::vector<std::byte> packet(size);
    if (!in.read(reinterpret_cast<char*>(packet.data()), size)) break;
    packets.push_back(std::move(packet));
  }
  return packets;
}

std::optional<anim::RagdollTemplate> loadTemplate() {
  const auto modDir = std::filesystem::path(OBF2_GAME_FILES) / "mods" / "bf2";
  if (!std::filesystem::exists(modDir)) return std::nullopt;
  // Mounted the way the game mounts them (`main.cpp`): the archive lists, then
  // the directory.
  static FileSystem files;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    mountArchivesFromCon(files, modDir, modDir / list, nullptr);
  }
  files.mountDirectory(modDir);
  std::string error;
  auto out = anim::RagdollTemplate::load(files, {}, &error);
  if (!out) std::printf("  %s\n", error.c_str());
  return out;
}

// What the data builds (objects/soldiers/common/animations/ragDollInit.con and
// ragDollConstraints.inc): thirteen particles; four angular constraints are
// given twice with the same three particles, and the engine keeps the last of
// each (`addAngularConstraint` updates, 0x6bc2b0) — so twelve.
void testTemplate(const anim::RagdollTemplate& t) {
  std::printf("  particles %zu, distances %zu, angular %zu, dihedral %zu, less-than %zu, "
              "capsules %zu, mappings %zu, locked %zu\n",
              t.particles.size(), t.distances.size(), t.angulars.size(), t.dihedrals.size(),
              t.lessThans.size(), t.capsules.size(), t.mappings.size(), t.locked.count());
  CHECK_EQ(t.particles.size(), std::size_t{13});
  CHECK_EQ(t.angulars.size(), std::size_t{12});
  CHECK_EQ(t.dihedrals.size(), std::size_t{4});
  CHECK_EQ(t.lessThans.size(), std::size_t{2});
  CHECK_EQ(t.capsules.size(), std::size_t{12});
  CHECK_EQ(t.mappings.size(), std::size_t{30});
  // `addParticle` sizes the body's particles 0.12, the knees 0.05 (0x6bd8e0).
  CHECK_EQ(t.particles[static_cast<std::size_t>(t.particleOf(1))].size, 0.12f);
  CHECK_EQ(t.particles[static_cast<std::size_t>(t.particleOf(2))].size, 0.05f);
  CHECK_EQ(t.particles[static_cast<std::size_t>(t.particleOf(4))].size, 0.1f);
  // A thigh's rest length, out of 3p_setup.ske — about the length of a thigh.
  const float thigh = t.boneDistance(1, 2);
  std::printf("  thigh %.3f m, shin %.3f m\n", thigh, t.boneDistance(2, 4));
  CHECK(thigh > 0.3f && thigh < 0.6f);
}

// The server's ragdoll states for one dead soldier, replayed into a body in the
// client's mode the way `readCurrentState` and `update` take them. Measured: the
// networked particles end where the server put them, and the body between them
// stays a body — every distance constraint near its rest length, no particle
// under the ground.
void testReplay(const anim::RagdollTemplate& t) {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-ragdoll.bin");
  CHECK(!packets.empty());
  net::bf2::WorldView world;
  world.setInventorySize([](std::uint32_t) { return 10; });
  std::map<std::uint16_t, std::vector<std::vector<Vec3f>>> states;
  std::map<std::uint16_t, int> seen;
  for (const auto& packet : packets) {
    world.feed(packet);
    for (const auto& [id, object] : world.objects()) {
      if (object.ragdollExact == seen[id]) continue;
      seen[id] = object.ragdollExact;
      states[id].push_back(object.ragdollParticles);
    }
  }
  std::uint16_t chosen = 0;
  for (const auto& [id, list] : states) {
    if (chosen == 0 || list.size() > states[chosen].size()) chosen = id;
  }
  CHECK(chosen != 0);
  const auto& list = states[chosen];
  std::printf("  soldier %u: %zu ragdoll states\n", chosen, list.size());
  CHECK(list.size() > 10);

  // The ground: flat, under the lowest particle the server sent.
  float lowest = 1e9f;
  for (const auto& state : list) {
    for (const Vec3f& p : state) lowest = std::min(lowest, p.y);
  }
  const float groundY = lowest - 0.12f;
  anim::RagdollGround ground;
  ground.cast = [groundY](const Vec3f& start,
                          const Vec3f& end) -> std::optional<anim::RagdollGround::Hit> {
    if (end.y > groundY) return std::nullopt;
    (void)start;
    return anim::RagdollGround::Hit{end.y - groundY, Vec3f{0.0f, 1.0f, 0.0f}};
  };
  ground.height = [groundY](const Vec3f&) { return groundY; };

  // He dies standing: the rest pose, put with his hips where the first state has
  // them (bone 6 is the second networked particle).
  std::vector<mesh::Mat4> pose = t.restWorld;
  const Vec3f offset = list.front()[1] - Vec3f{pose[6].m[12], pose[6].m[13], pose[6].m[14]};
  for (mesh::Mat4& m : pose) {
    m.m[12] += offset.x;
    m.m[13] += offset.y;
    m.m[14] += offset.z;
  }
  anim::Ragdoll body(t, pose, Vec3f{});
  body.setClient(true);
  for (const auto& state : list) {
    body.readNetwork(state);
    // Four frames of a 60-frame second between two states of the server's 15.
    for (int frame = 0; frame < 4; ++frame) body.update(1.0f / 60.0f, ground);
  }

  // The networked particles are where the server's last state put them, within
  // the 10% a step closes (`rd_netInterpolate`).
  std::size_t next = 0;
  float worstNet = 0.0f;
  for (const auto& p : body.particles()) {
    if (!p.networked) continue;
    worstNet = std::max(worstNet, length(p.position - list.back()[next++]));
  }
  float worstStretch = 0.0f, lowestParticle = 1e9f;
  for (const auto& c : t.distances) {
    const float d = length(body.particles()[static_cast<std::size_t>(c.a)].position -
                           body.particles()[static_cast<std::size_t>(c.b)].position);
    worstStretch = std::max(worstStretch, std::fabs(d - c.rest) / c.rest);
  }
  for (const auto& p : body.particles()) lowestParticle = std::min(lowestParticle, p.position.y);
  std::printf("  networked off by %.3f m at most, constraints off by %.0f%% at most, lowest "
              "particle %.3f m over the ground\n",
              worstNet, worstStretch * 100.0f, lowestParticle - groundY);
  CHECK(worstNet < 0.05f);
  CHECK(worstStretch < 0.25f);
  CHECK(lowestParticle > groundY - 0.05f);

  // Onto the skeleton: relative to the centre, which is bone 6's particle.
  std::vector<mesh::Mat4> skeleton = pose;
  body.applyOnSkeleton(skeleton);
  const Vec3f hips{skeleton[6].m[12], skeleton[6].m[13], skeleton[6].m[14]};
  CHECK(length(hips) < 1e-4f);
  for (const mesh::Mat4& m : skeleton) {
    for (const float value : m.m) CHECK(std::isfinite(value));
  }
  // The head lies within a body's length of the hips.
  const Vec3f head{skeleton[47].m[12], skeleton[47].m[13], skeleton[47].m[14]};
  std::printf("  head %.2f m from the hips\n", length(head));
  CHECK(length(head) > 0.3f && length(head) < 1.2f);
}

// `getClosestDistanceBetweenLines` (0x723cf0): crossing segments meet at their
// closest points; parallel ones take the engine's endpoints.
void testClosestBetweenSegments() {
  float s = 0.0f, t = 0.0f;
  const float crossing = anim::closestBetweenSegments(Vec3f{-1, 0, 0}, Vec3f{1, 0, 0},
                                                      Vec3f{0, 1, -1}, Vec3f{0, 1, 1}, s, t);
  CHECK(std::fabs(crossing - 1.0f) < 1e-5f);
  CHECK(std::fabs(s - 0.5f) < 1e-5f && std::fabs(t - 0.5f) < 1e-5f);
  // Past the end: the first segment's end is the closest point.
  const float past = anim::closestBetweenSegments(Vec3f{0, 0, 0}, Vec3f{1, 0, 0},
                                                  Vec3f{3, 0, -1}, Vec3f{3, 0, 1}, s, t);
  CHECK(std::fabs(past - 2.0f) < 1e-5f && s == 1.0f);
  // Parallel and side by side, 0.1 apart: the engine's branch, s from the start.
  const float parallel = anim::closestBetweenSegments(Vec3f{0, 0, 0}, Vec3f{0, 1, 0},
                                                      Vec3f{0.1f, 0, 0}, Vec3f{0.1f, 1, 0}, s, t);
  CHECK(std::fabs(parallel - 0.1f) < 1e-5f);
}

// A body thrown at a wall stays on its side of it: every particle's motion is
// cast at the faces near the body (`checkCollisionConstraints`' object pass,
// `CollisionMesh::getDistance`), and a hit puts it back on the near side. Without
// the objects the same body goes through — the check bites.
void testWall(const anim::RagdollTemplate& t) {
  // A wall at x = 0.5 facing −x, 3 m high and 4 m wide: two faces, wound the
  // engine's way (`cross(c − a, b − a)` is the normal).
  mesh::CollisionLayer wall;
  wall.vertices = {{0.5f, 0.0f, -2.0f}, {0.5f, 3.0f, -2.0f}, {0.5f, 0.0f, 2.0f}, {0.5f, 3.0f, 2.0f}};
  wall.faces = {{0, 1, 2, 0}, {1, 3, 2, 0}};
  server::CollisionWorld world;
  world.addLayer(wall, Mat4::identity());

  // Standing at x = −0.3, hips on the origin's vertical, thrown at 4 m/s.
  std::vector<mesh::Mat4> pose = t.restWorld;
  const Vec3f hips{pose[6].m[12], pose[6].m[13], pose[6].m[14]};
  float lowest = 1e9f;
  for (const mesh::Mat4& m : pose) lowest = std::min(lowest, m.m[13]);
  for (mesh::Mat4& m : pose) {
    m.m[12] += -0.3f - hips.x;
    m.m[13] -= lowest;
    m.m[14] -= hips.z;
  }
  anim::RagdollGround ground;
  ground.cast = [](const Vec3f&, const Vec3f& end) -> std::optional<anim::RagdollGround::Hit> {
    if (end.y > 0.0f) return std::nullopt;
    return anim::RagdollGround::Hit{end.y, Vec3f{0.0f, 1.0f, 0.0f}};
  };
  ground.height = [](const Vec3f&) { return 0.0f; };

  const auto furthest = [&](bool withObjects) {
    anim::RagdollGround g = ground;
    if (withObjects) {
      g.objects = [&world](const Vec3f& start, const Vec3f& motion, const Vec3f& around,
                           float radius) -> std::optional<anim::RagdollGround::ObjectHit> {
        const auto hit = world.segmentContact(start, motion, around, radius);
        if (!hit) return std::nullopt;
        return anim::RagdollGround::ObjectHit{hit->normal, hit->along};
      };
    }
    anim::Ragdoll body(t, pose, Vec3f{4.0f, 0.0f, 0.0f});
    float most = -1e9f;
    for (int frame = 0; frame < 120; ++frame) {
      body.update(1.0f / 60.0f, g);
      for (const auto& p : body.particles()) most = std::max(most, p.position.x);
    }
    return most;
  };
  const float with = furthest(true);
  const float without = furthest(false);
  std::printf("  thrown at a wall at x = 0.5: furthest particle %.3f with the objects, %.3f "
              "without\n",
              with, without);
  CHECK(with <= 0.5f + 0.01f);
  CHECK(without > 0.6f);
}

}  // namespace

TEST_MAIN({
  testClosestBetweenSegments();
  const auto t = loadTemplate();
  if (!t) {
    std::printf("  no installation: the ragdoll is not checked\n");
  } else {
    testTemplate(*t);
    testReplay(*t);
    testWall(*t);
  }
})
