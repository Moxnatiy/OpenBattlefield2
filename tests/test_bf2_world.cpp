// Стан світу зі знятого трафіку.
//
// Зразок `tests/data/bf2-spawned.bin` знято вже після появи гравця й із
// надісланим вводом (`openbf2 --connect ... --record`), тож у ньому є все
// одразу: події про гравців, стан керованого об'єкта і записи привидів.
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

  // Гравці зі знятку: у ньому були і ми, і ще хтось.
  CHECK(!world.players().empty());
  // Себе впізнали за іменем — сервер шле його з пробілом попереду.
  CHECK(world.ownPlayer() >= 0);
  CHECK(world.ownTeam() > 0);

  // Об'єкти світу теж зібралися, і всі — в межах карти.
  CHECK(!world.objects().empty());
  for (const auto& [id, object] : world.objects()) {
    CHECK(id != 0);
    CHECK(std::abs(object.position.x) < 1024.0f);
    CHECK(std::abs(object.position.z) < 1024.0f);
  }
}

// Опорна точка стиснення береться зі стану керованого об'єкта і не
// лишається нулем: без неї місця з потоку розлетілися б.
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
