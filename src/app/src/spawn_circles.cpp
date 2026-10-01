#include "obf2/app/spawn_circles.h"

namespace obf2::app {

std::vector<hud::Manager::ServerSpawnGroup> spawnCircles(
    const std::vector<net::bf2::CreateSpawnGroup>& groups, float worldSize) {
  std::vector<hud::Manager::ServerSpawnGroup> out;
  out.reserve(groups.size());
  for (const net::bf2::CreateSpawnGroup& group : groups) {
    hud::Manager::ServerSpawnGroup circle;
    circle.id = group.id;
    circle.team = static_cast<int>(group.team);
    circle.aiOnly = group.aiOnly;
    circle.selectable = group.selectable;
    circle.squad = group.squad;
    circle.worldX = net::bf2::spawnGroupWorldPos(group.worldX, worldSize);
    circle.worldZ = net::bf2::spawnGroupWorldPos(group.worldZ, worldSize);
    out.push_back(circle);
  }
  return out;
}

}  // namespace obf2::app
