#pragma once
// The spawn screen's circles on `--connect`: the server's spawn groups as the
// client's `SpawnManager` keeps them (`createClientSpawnGroup`, Linux 0x4ba600),
// handed to the HUD in the world's own coordinates.
#include <vector>

#include "obf2/hud/manager.h"
#include "obf2/net/bf2_events.h"

namespace obf2::app {

// `worldSize` is the level's GLSWorldSizeX, which the server packs a group's
// position with: `byte / 255 * size - size / 2` (0x4ba600).
std::vector<hud::Manager::ServerSpawnGroup> spawnCircles(
    const std::vector<net::bf2::CreateSpawnGroup>& groups, float worldSize);

}  // namespace obf2::app
