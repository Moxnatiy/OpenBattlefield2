// The world's state from captured traffic.
//
// The sample `tests/data/bf2-spawned.bin` was taken after the player spawned and
// with input sent (`openbf2 --connect ... --record`), so it holds everything at
// once: the player events, the controlled-object state and the ghost records.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "obf2/net/bf2_world.h"
#include "check.h"

namespace {

std::vector<std::vector<std::byte>> loadCapture(const std::string& path) {
  std::vector<std::vector<std::byte>> packets;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return packets;
  while (true) {
    std::uint32_t length = 0;
    if (std::fread(&length, sizeof(length), 1, file) != 1) break;
    if (length == 0 || length > 4096) break;
    std::vector<std::byte> packet(length);
    if (std::fread(packet.data(), 1, length, file) != length) break;
    packets.push_back(std::move(packet));
  }
  std::fclose(file);
  return packets;
}

}  // namespace

void testWorldViewCollectsPlayersAndObjects() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-spawned.bin");
  CHECK(!packets.empty());

  obf2::net::bf2::WorldView world;
  world.setOwnName("OpenBF2");
  for (const auto& packet : packets) world.feed(packet);

  // The players from the capture: it held both us and somebody else.
  CHECK(!world.players().empty());
  // We recognised ourselves by name — the server sends it with a leading space.
  CHECK(world.ownPlayer() >= 0);
  CHECK(world.ownTeam() > 0);

  // The world's objects assembled too, and all of them are within the map.
  CHECK(!world.objects().empty());
  for (const auto& [id, object] : world.objects()) {
    CHECK(id != 0);
    CHECK(std::abs(object.position.x) < 1024.0f);
    CHECK(std::abs(object.position.z) < 1024.0f);
  }
}

// The compression reference point comes from the controlled-object state and does
// not stay zero: without it the positions from the stream would scatter.
void testWorldViewTakesCompressionReference() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-spawned.bin");
  obf2::net::bf2::WorldView world;
  for (const auto& packet : packets) world.feed(packet);

  const obf2::Vec3f& reference = world.compressionReference();
  const bool zero = reference.x == 0.0f && reference.y == 0.0f && reference.z == 0.0f;
  CHECK(!zero);
  CHECK(std::abs(reference.x) < 1024.0f);
  CHECK(std::abs(reference.z) < 1024.0f);
}

// Another player's objects from their ghost records.
// `tests/data/bf2-karkand-other-player.bin`: the original client (`defaultPlayer`)
// sat in the gas station's jeep on Strike at Karkand while ours spawned. The jeep
// is 1841 (template 5184), his soldier 1731 (template 3283). The jeep has a player
// in it and is still a jeep: every one of its positions has to lie on the spot its
// `CreateObjectEvent` named (-149.53, 162.14, -269.96) — before our spawn, when
// the server writes raw floats, and after it, when it writes a difference from our
// own soldier's position. Read with the soldier's layout, as it was while "a
// player entered it" meant "a soldier", they scattered.
void testOtherPlayersObjectsFromTheirRecords() {
  const auto packets =
      loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-karkand-other-player.bin");
  CHECK(!packets.empty());
  obf2::net::bf2::WorldView world;
  world.setOwnName("OpenBF2");
  int updates = 0, onSpot = 0, before = 0;
  for (const auto& packet : packets) {
    before = world.objects().count(1841) ? world.objects().at(1841).updates : 0;
    world.feed(packet);
    const auto found = world.objects().find(1841);
    if (found == world.objects().end() || found->second.updates == before) continue;
    ++updates;
    const obf2::Vec3f& at = found->second.position;
    if (std::abs(at.x + 149.53f) < 1.0f && std::abs(at.y - 162.14f) < 1.0f &&
        std::abs(at.z + 269.96f) < 1.0f) {
      ++onSpot;
    }
  }
  std::printf("  jeep 1841: updates %d, on its spot %d\n", updates, onSpot);
  CHECK(updates >= 10);
  CHECK_EQ(onSpot, updates);

  // The classes come from the full records' masks.
  CHECK(world.objects().at(1841).netClass == obf2::net::bf2::GhostClass::SimpleObject);
  CHECK(world.objects().count(1731) == 1);
  const auto& soldier = world.objects().at(1731);
  CHECK(soldier.netClass == obf2::net::bf2::GhostClass::Soldier);
  // His soldier's full record puts it by the spot its `CreateObjectEvent` named
  // in this capture (-151.03, 162.30, -271.28): -151.01, 162.30, -271.18.
  CHECK(soldier.fromGhostStream);
  CHECK(std::abs(soldier.position.x + 151.03f) < 0.2f);
  CHECK(std::abs(soldier.position.y - 162.30f) < 0.2f);
  CHECK(std::abs(soldier.position.z + 271.28f) < 0.2f);
}

TEST_MAIN({
  testWorldViewCollectsPlayersAndObjects();
  testWorldViewTakesCompressionReference();
  testOtherPlayersObjectsFromTheirRecords();
});
