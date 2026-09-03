// Розбір справжніх пакетів оригінального сервера.
//
// Зразок у tests/data/bf2-world.bin спіймано командою
//   tools/linuxded/capture.py --stage world --out tests/data/bf2-world.bin
// на сервері з картою dalian_plant. Тримати його в репозиторії дешево, а
// перевірка виходить така, якої не дає жоден синтетичний пакет: якщо
// хоч в одній події зіб'ється довжина, наступна прочитається як сміття.
#include <cstdio>
#include <map>
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

// Зразок tests/data/bf2-ghosts.bin спіймано після повного рукостискання
// (`capture.py --stage spawn`): у ньому вже є потік привидів.
void testGhostStreamIsWalkable() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-ghosts.bin");
  CHECK(!packets.empty());

  int withGhosts = 0, records = 0;
  std::map<std::uint16_t, int> perObject;
  for (const auto& packet : packets) {
    const auto header = obf2::net::bf2::readGhostHeader(packet);
    if (!header) continue;
    ++withGhosts;
    if (header->controlObjectState) continue;

    const auto found = obf2::net::bf2::readGhostRecords(packet);
    // Кожен запис має бути прочитаний: рушій покладається на те, що
    // довжина в записі дозволяє пропустити навіть незнайомий об'єкт.
    CHECK_EQ(found.size(), std::size_t(header->records));
    for (const auto& record : found) {
      CHECK(record.kind != 2);       // вид 2 — помилка потоку
      ++records;
      ++perObject[record.networkId];
    }
  }

  CHECK(withGhosts >= 100);
  CHECK(records >= 100);
  // Рухомі об'єкти оновлюються майже щопакета, статика — раз.
  CHECK(perObject.size() >= 10);
}

// Стан керованого об'єкта: сервер сам каже, яким об'єктом ми керуємо.
// Номер їде 16 бітами одразу за трійкою чисел, і рушій віддає його в
// `NetworkManager::getObject` (0x445dc3) — тобто це не здогад.
void testControlObjectStateNamesItsObject() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-ghosts.bin");
  CHECK(!packets.empty());

  std::map<std::uint16_t, int> perObject;
  std::int32_t previousCounter = -1;
  for (const auto& packet : packets) {
    const auto state = obf2::net::bf2::readControlObjectState(packet);
    if (!state) continue;
    ++perObject[state->networkId];
    // Керований об'єкт існує, тобто нульового номера тут бути не може.
    CHECK(state->networkId != 0);
    // Лічильник у знятому потоці лише росте.
    CHECK(state->counter >= previousCounter);
    previousCounter = state->counter;
  }

  CHECK(!perObject.empty());
  // За один зняток об'єкт не міняється: ми весь час керуємо тим самим.
  CHECK_EQ(perObject.size(), std::size_t(1));
}

TEST_MAIN({
  testWorldCaptureIsFullyDecoded();
  testHeightsAreOnTheTerrain();
  testGhostStreamIsWalkable();
  testControlObjectStateNamesItsObject();
});
