// The spawn groups the server sends as CreateSpawnGroupEvent events.
//
// The numbers here are not invented: this is what a live server gives on Dalian
// Plant, and the flags' positions from the same level's `GamePlayObjects.con`
// (docs/functions/network-events.md).
#include <cmath>
#include <vector>

#include "check.h"
#include "obf2/net/bf2_events.h"

using namespace obf2::net::bf2;

namespace {

// Dalian Plant's world size: (1025 - 1) * 2.
constexpr float kWorld = 2048.0f;

// The four flag groups as the server sends them. The fields come from the event's
// parsing: the first number, the team, three flags, the packed position, the network id.
std::vector<CreateSpawnGroup> dalianGroups() {
  return {
      {1, 1, true, false, true, 117, 94, 515},   // powerplant, the Chinese
      {2, 2, true, false, true, 106, 119, 516},  // constructionsite, the Americans
      {3, 0, true, false, true, 137, 120, 517},  // reactors, neutral
      {4, 0, true, false, true, 98, 97, 518},    // mainentrance, neutral
  };
}

}  // namespace

// Unpacking the position: `pos = byte / 255 * worldSize - worldSize / 2`
// (SpawnGroup::getUnsignedWorldPosition, 0x4b94b0; the multiplier 255 as a constant at
// 0xb355bc).
static void testUnpackedPositions() {
  // The map's centre is 128 plus half a rounding error.
  CHECK(std::abs(spawnGroupWorldPos(128, kWorld) - 3.8f) < 1.0f);
  // The edges.
  CHECK(std::abs(spawnGroupWorldPos(0, kWorld) + 1024.0f) < 0.01f);
  CHECK(std::abs(spawnGroupWorldPos(255, kWorld) - 1024.0f) < 0.01f);

  // Powerplant: the server says 117 and 94, while the flag in the level's data stands
  // at (-92.3, -260.8). The discrepancy is expected — a group's position is the
  // average of its spawn points.
  const float x = spawnGroupWorldPos(117, kWorld);
  const float z = spawnGroupWorldPos(94, kWorld);
  CHECK(std::abs(x - (-92.3f)) < 40.0f);
  CHECK(std::abs(z - (-260.8f)) < 40.0f);
}

// Every Dalian flag has to find its group — and precisely the one next to it.
static void testEachFlagFindsItsGroup() {
  const auto groups = dalianGroups();
  struct Flag {
    float x, z;
    std::uint8_t expected;
  };
  // We expect the **small** group number: it is what the original client sends
  // (`NESelectSpawnGroup = 2` for the second flag in the captured traffic).
  const Flag flags[] = {
      {-92.3f, -260.8f, 1},   // powerplant
      {-151.8f, -58.9f, 2},   // constructionsite
      {88.0f, -40.0f, 3},     // reactors
      {-254.0f, -210.0f, 4},  // mainentrance
  };
  for (const Flag& flag : flags) {
    float away = 0.0f;
    const std::uint8_t id = nearestSpawnGroup(groups, flag.x, flag.z, kWorld, &away);
    CHECK_EQ(id, flag.expected);
    // The group stands next to its own flag rather than somewhere on the map.
    CHECK(away < 60.0f);
  }
}

// An empty list gives zero — and the server understands zero as "no point chosen",
// so the player simply will not spawn. There must be no silent substitution here.
static void testNoGroupsGivesZero() {
  float away = -1.0f;
  CHECK_EQ(nearestSpawnGroup({}, 0.0f, 0.0f, kWorld, &away), std::uint8_t(0));
  CHECK_EQ(away, 0.0f);
}

// The server sends squad groups at the map's centre (127, 127). A flag standing far
// from the centre must not fall onto them.
static void testSquadGroupsInTheCentreDoNotWin() {
  auto groups = dalianGroups();
  for (std::uint16_t i = 0; i < 20; ++i) {
    groups.push_back({static_cast<std::uint8_t>(192 + i), (i % 2) ? 2u : 1u, false, false, false,
                      127, 127, static_cast<std::uint16_t>(578 + i)});
  }
  float away = 0.0f;
  CHECK_EQ(nearestSpawnGroup(groups, -254.0f, -210.0f, kWorld, &away), std::uint8_t(4));
}

// A run-time group (192 and up, `SpawnManager::createDynamicSpawnGroup`) nearer
// the flag than the flag's own must not win: on the co-op Karkand server the LAV's
// group 197 did, and the player never appeared.
static void testDynamicGroupNearTheFlagDoesNotWin() {
  std::vector<CreateSpawnGroup> groups;
  groups.push_back({3, 2u, true, false, true, 0, 0, 517});
  groups.push_back({197, 2u, true, false, true, 0, 0, 583});
  // Pack both positions: the flag's group a little further than the vehicle's.
  const float flagX = -161.0f, flagZ = -263.0f;
  // The inverse of spawnGroupWorldPos.
  const auto pack = [](float world) {
    return static_cast<std::uint8_t>(std::lround((world + kWorld * 0.5f) / kWorld * 255.0f));
  };
  groups[0].worldX = pack(flagX - 30.0f);
  groups[0].worldZ = pack(flagZ);
  groups[1].worldX = pack(flagX - 10.0f);
  groups[1].worldZ = pack(flagZ);
  float away = 0.0f;
  CHECK_EQ(nearestSpawnGroup(groups, flagX, flagZ, kWorld, &away, 2), std::uint8_t(3));
}

TEST_MAIN({
  testDynamicGroupNearTheFlagDoesNotWin();
  testUnpackedPositions();
  testEachFlagFindsItsGroup();
  testNoGroupsGivesZero();
  testSquadGroupsInTheCentreDoNotWin();
})
