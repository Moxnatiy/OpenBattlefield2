// Strategic objects gathered into holders (strategic_objects.h,
// `GameLogic::addStrategicObject`, BF2.exe 0x4f1750 / Linux 0x482e00), and the
// collision mesh radius the merge measures with (0x719a70).
#include "check.h"
#include "obf2/hud/strategic_objects.h"
#include "obf2/mesh/collision.h"

using namespace obf2;
using hud::StrategicCandidate;

int main() {
  // Karkand's north bridge: two `lrg_stonebridge` 43.9 m apart join when their
  // radii reach; the holder sits at the midpoint, where the original draws it.
  {
    const std::vector<StrategicCandidate> objects{
        {"lrgstnbrdg", false, {-95.0f, 0.0f, 10.0f}, 12.0f},
        {"lrgstnbrdg", false, {-51.1f, 0.0f, 10.0f}, 12.0f},
    };
    const auto holders = hud::groupStrategicObjects(objects);
    CHECK(holders.size() == 1);
    CHECK(holders[0].members.size() == 2);
    CHECK(std::abs(holders[0].position.x - -73.05f) < 1e-3f);
  }
  // Too far apart: 2 x (10 + 10) = 40 <= 43.9, so a second holder, index 1.
  {
    const std::vector<StrategicCandidate> objects{
        {"lrgstnbrdg", false, {-95.0f, 0.0f, 0.0f}, 10.0f},
        {"lrgstnbrdg", false, {-51.1f, 0.0f, 0.0f}, 10.0f},
    };
    const auto holders = hud::groupStrategicObjects(objects);
    CHECK(holders.size() == 2);
    CHECK(holders[1].index == 1);
  }
  // Exactly at the reach does not join (the engine's test is `reach <= distance`).
  {
    const std::vector<StrategicCandidate> objects{
        {"a", false, {0.0f, 0.0f, 0.0f}, 1.0f},
        {"a", false, {4.0f, 0.0f, 0.0f}, 1.0f},
    };
    CHECK(hud::groupStrategicObjects(objects).size() == 2);
  }
  // The distance is to the holder's first object, not its mean: the third
  // segment is near the second but far from the first.
  {
    const std::vector<StrategicCandidate> objects{
        {"b", false, {0.0f, 0.0f, 0.0f}, 1.0f},
        {"b", false, {3.0f, 0.0f, 0.0f}, 1.0f},
        {"b", false, {6.0f, 0.0f, 0.0f}, 1.0f},
    };
    const auto holders = hud::groupStrategicObjects(objects);
    CHECK(holders.size() == 2);
    CHECK(holders[0].members.size() == 2);
    CHECK(holders[1].members.front() == 2);
  }
  // A one-piece object never joins; other names never mix.
  {
    const std::vector<StrategicCandidate> objects{
        {"c", false, {0.0f, 0.0f, 0.0f}, 5.0f},
        {"c", true, {1.0f, 0.0f, 0.0f}, 5.0f},
        {"d", false, {1.0f, 0.0f, 0.0f}, 5.0f},
    };
    const auto holders = hud::groupStrategicObjects(objects);
    CHECK(holders.size() == 3);
    CHECK(holders[1].name == "c" && holders[1].index == 1);
    CHECK(holders[2].name == "d" && holders[2].index == 0);
  }

  // The mesh radius: the first geom with lods, the first type its table holds,
  // the farthest corner of that lod's box.
  {
    mesh::CollisionMesh collision;
    mesh::CollisionLayer empty;
    mesh::CollisionLayer box;
    box.type = mesh::ColType::Vehicle;
    box.bounds = {{-3.0f, -1.0f, -2.0f}, {1.0f, 4.0f, 2.0f}};
    mesh::CollisionLayer later;
    later.type = mesh::ColType::Soldier;
    later.bounds = {{-100.0f, -100.0f, -100.0f}, {100.0f, 100.0f, 100.0f}};
    collision.layers = {empty, box, later};
    mesh::CollisionGeometry nothing;  // no lods: skipped
    mesh::CollisionGeometry geometry;
    geometry.lods = {-1, 1, 2};       // type 0 is a null, 1 the box, 2 the later one
    geometry.table[1] = 1;
    geometry.table[2] = 2;
    collision.parts.push_back({{nothing, geometry}});
    // Farthest corner: (-3, 4, +-2) → sqrt(9 + 16 + 4).
    CHECK(std::abs(collision.maxBoundingRadius(0) - std::sqrt(29.0f)) < 1e-5f);
    CHECK(collision.maxBoundingRadius(1) == 0.0f);
  }
  return 0;
}
