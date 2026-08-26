// Розбір справжніх пакетів оригінального сервера.
//
// Зразок у tests/data/bf2-world.bin спіймано командою
//   tools/linuxded/capture.py --stage world --out tests/data/bf2-world.bin
// на сервері з картою dalian_plant. Тримати його в репозиторії дешево, а
// перевірка виходить така, якої не дає жоден синтетичний пакет: якщо
// хоч в одній події зіб'ється довжина, наступна прочитається як сміття.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "obf2/net/bf2_events.h"
#include "check.h"

namespace {

using namespace obf2;

std::vector<std::vector<std::byte>> loadCapture(const std::string& path) {
  std::vector<std::vector<std::byte>> packets;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return packets;

  // Формат простий: для кожного пакета u32 довжина, далі байти.
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

void testWorldCaptureIsFullyDecoded() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-world.bin");
  CHECK(!packets.empty());

  int objects = 0, positioned = 0, players = 0;
  std::string playerName;

  for (const auto& packet : packets) {
    const auto events = obf2::net::bf2::readEvents(packet);
    for (const auto& event : events) {
      // Номер типу не буває більшим за 69: усе понад це означає, що
      // попередню подію прочитано неправильної довжини.
      CHECK(event.type <= 69);
      CHECK(!obf2::net::bf2::eventName(event.type).empty());
      if (event.object) {
        ++objects;
        if (event.object->position) ++positioned;
      }
      if (event.player) {
        ++players;
        playerName = event.player->name;
      }
    }
  }

  // Сервер шле світ картою dalian_plant: об'єктів там багато.
  CHECK(objects >= 40);
  CHECK(positioned >= 40);

  // Гравець — це ми. Пробіл попереду не помилка: на сервері без рейтингу
  // рушій складає ім'я як «тег клану + пробіл + ім'я», а тег порожній.
  CHECK_EQ(players, 1);
  CHECK_EQ(playerName, std::string(" OpenBF2"));
}

void testHeightsAreOnTheTerrain() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-world.bin");
  int checked = 0;
  for (const auto& packet : packets) {
    for (const auto& event : obf2::net::bf2::readEvents(packet)) {
      if (!event.object || !event.object->position) continue;
      const auto& p = *event.object->position;
      // Якщо розкладку зсунуто хоч на біт, з float вилазять числа на
      // кшталт 1e38. Рельєф dalian_plant тримається в цих межах.
      CHECK(p.y > 100.0f && p.y < 300.0f);
      CHECK(p.x > -2048.0f && p.x < 2048.0f);
      CHECK(p.z > -2048.0f && p.z < 2048.0f);
      ++checked;
    }
  }
  CHECK(checked >= 40);
}

}  // namespace

TEST_MAIN({
  testWorldCaptureIsFullyDecoded();
  testHeightsAreOnTheTerrain();
});
