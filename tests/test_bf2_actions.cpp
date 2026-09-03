// Потік дій гравця — те, чим клієнт рухає свого солдата.
//
// Розкладка з `PlayerActionManager::processReceivedPacket` (0x44d670).
// Перевіряємо її двома способами, і другий важливіший за перший:
//
//   1. що ми пишемо, те й читаємо назад;
//   2. що наш розбирач розуміє **справжні** пакети оригінального
//      клієнта, зняті з мережі (`tests/data/actions-*.bin`).
//
// Другий тест і є доказом, що розкладка правильна: байти там не наші.
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/net/bf2_events.h"
#include "obf2/net/bf2_protocol.h"

// Лише простір BF2: у `obf2::net` є свій ExtendedHeader, і два
// однакові імені зробили б звертання неоднозначним.
using namespace obf2::net::bf2;

namespace {

// Один пакет зі спійманого файлу: u32 довжина, далі байти.
std::vector<std::byte> capturedPacket(const char* name) {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/" + name);
  return packets.empty() ? std::vector<std::byte>{} : packets.front();
}

}  // namespace

// Кругообіг: складене нами читається назад тим самим розбирачем.
static void testRoundTrip() {
  ExtendedHeader header;
  header.sequence = 5;
  header.ack = 7;
  header.ackBits = 0xFFFFFFFFu;

  PlayerAction action;
  action.axes[kAxisForward] = kAxisFull;
  action.axes[kAxisMouseX] = -13;
  action.axes[kAxisMouseY] = 40;
  action.buttons = kButtonSprint;

  const PlayerAction three[3] = {action, action, action};
  const auto packet = writePlayerActions(9, header, 172, three, 3, 200);

  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read) return;

  CHECK_EQ(read->number, std::uint32_t(200));
  CHECK_EQ(read->tick, 172);
  CHECK_EQ(read->actions.size(), std::size_t(3));
  for (const PlayerAction& got : read->actions) {
    CHECK_EQ(got.axes[kAxisForward], kAxisFull);
    CHECK_EQ(got.axes[kAxisMouseX], std::int16_t(-13));
    CHECK_EQ(got.axes[kAxisMouseY], std::int16_t(40));
    CHECK_EQ(got.buttons, kButtonSprint);
    CHECK(got.flag);
    // Три осі, яких ми не називаємо, лишаються нулем.
    CHECK_EQ(got.axes[0], std::int16_t(0));
    CHECK_EQ(got.axes[1], std::int16_t(0));
    CHECK_EQ(got.axes[2], std::int16_t(0));
  }
}

// Від'ємні осі: хід назад і рух миші вліво.
static void testNegativeAxes() {
  ExtendedHeader header;
  PlayerAction action;
  action.axes[kAxisForward] = -kAxisFull;
  action.axes[kAxisMouseX] = -127;

  const auto packet = writePlayerActions(1, header, 1, &action, 1);
  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read || read->actions.empty()) return;
  CHECK_EQ(read->actions.front().axes[kAxisForward], std::int16_t(-kAxisFull));
  CHECK_EQ(read->actions.front().axes[kAxisMouseX], std::int16_t(-127));
}

// Пакет без дій розбирачу не належить.
static void testPacketWithoutActions() {
  ExtendedHeader header;
  const auto packet =
      writePostRemoteEvent(1, header, 0, kNetworkCategory, kNetSelectSpawnGroup, 2);
  CHECK(!readPlayerActions(packet).has_value());
}

// Справжній пакет оригінального клієнта: гравець біжить уперед.
static void testCapturedRun() {
  const auto packet = capturedPacket("actions-run.bin");
  CHECK(!packet.empty());
  if (packet.empty()) return;

  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read) return;

  // Оригінал кладе в пакет три останні набори — запас на втрату.
  CHECK_EQ(read->actions.size(), std::size_t(3));
  if (read->actions.empty()) return;
  const PlayerAction& first = read->actions.front();
  CHECK_EQ(first.axes[kAxisForward], kAxisFull);  // повний хід уперед
  CHECK_EQ(first.buttons, std::uint32_t(0));      // спринт не тримає
  CHECK(first.flag);
}

// Той самий гравець зі спринтом.
static void testCapturedSprint() {
  const auto packet = capturedPacket("actions-sprint.bin");
  CHECK(!packet.empty());
  if (packet.empty()) return;

  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read || read->actions.empty()) return;
  const PlayerAction& first = read->actions.front();
  CHECK_EQ(first.axes[kAxisForward], kAxisFull);
  CHECK_EQ(first.buttons, kButtonSprint);
}

// І він же, поки стоїть на місці.
static void testCapturedStill() {
  const auto packet = capturedPacket("actions-still.bin");
  CHECK(!packet.empty());
  if (packet.empty()) return;

  const auto read = readPlayerActions(packet);
  CHECK(read.has_value());
  if (!read || read->actions.empty()) return;
  for (const std::int16_t axis : read->actions.front().axes) CHECK_EQ(axis, std::int16_t(0));
  CHECK_EQ(read->actions.front().buttons, std::uint32_t(0));
}

// Лічильник вводу росте на одиницю за пакет — за ним сервер шикує
// набори в правильному порядку.
static void testCapturedTickIsSane() {
  const auto run = readPlayerActions(capturedPacket("actions-run.bin"));
  const auto sprint = readPlayerActions(capturedPacket("actions-sprint.bin"));
  CHECK(run.has_value() && sprint.has_value());
  if (!run || !sprint) return;
  // Спринт у дампі був пізніше за початок бігу.
  CHECK(sprint->tick > run->tick);
}

TEST_MAIN({
  testRoundTrip();
  testNegativeAxes();
  testPacketWithoutActions();
  testCapturedRun();
  testCapturedSprint();
  testCapturedStill();
  testCapturedTickIsSane();
})
