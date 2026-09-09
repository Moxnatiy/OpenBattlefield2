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

TEST_MAIN({
  testWorldViewCollectsPlayersAndObjects();
  testWorldViewTakesCompressionReference();
});
