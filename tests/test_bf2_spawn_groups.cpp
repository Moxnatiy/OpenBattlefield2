// Групи появи, які сервер шле подіями CreateSpawnGroupEvent.
//
// Числа тут не вигадані: це те, що віддає живий сервер на Dalian Plant,
// і положення прапорців із `GamePlayObjects.con` того ж рівня
// (docs/functions/network-events.md).
#include <vector>

#include "check.h"
#include "obf2/net/bf2_events.h"

using namespace obf2::net::bf2;

namespace {

// Розмір світу Dalian Plant: (1025 - 1) * 2.
constexpr float kWorld = 2048.0f;

// Чотири групи прапорів, як їх шле сервер. Поля — з розбору події:
// перше число, команда, три прапорці, спаковане місце, мережевий номер.
std::vector<CreateSpawnGroup> dalianGroups() {
  return {
      {1, 1, true, false, true, 117, 94, 515},   // powerplant, китайці
      {2, 2, true, false, true, 106, 119, 516},  // constructionsite, американці
      {3, 0, true, false, true, 137, 120, 517},  // reactors, нічия
      {4, 0, true, false, true, 98, 97, 518},    // mainentrance, нічия
  };
}

}  // namespace

// Розпакування місця: `pos = байт / 255 * worldSize - worldSize / 2`
// (SpawnGroup::getUnsignedWorldPosition, 0x4b94b0; множник 255 сталою за
// 0xb355bc).
static void testUnpackedPositions() {
  // Центр карти — це 128 з половиною похибки округлення.
  CHECK(std::abs(spawnGroupWorldPos(128, kWorld) - 3.8f) < 1.0f);
  // Краї.
  CHECK(std::abs(spawnGroupWorldPos(0, kWorld) + 1024.0f) < 0.01f);
  CHECK(std::abs(spawnGroupWorldPos(255, kWorld) - 1024.0f) < 0.01f);

  // Powerplant: сервер каже 117 і 94, прапор у даних рівня стоїть на
  // (-92.3, -260.8). Розбіжність очікувана — місце групи це середнє її
  // точок появи.
  const float x = spawnGroupWorldPos(117, kWorld);
  const float z = spawnGroupWorldPos(94, kWorld);
  CHECK(std::abs(x - (-92.3f)) < 40.0f);
  CHECK(std::abs(z - (-260.8f)) < 40.0f);
}

// Кожен прапор Dalian має знайти свою групу — і саме ту, що поруч.
static void testEachFlagFindsItsGroup() {
  const auto groups = dalianGroups();
  struct Flag {
    float x, z;
    std::uint8_t expected;
  };
  // Чекаємо **малий** номер групи: саме його шле оригінальний клієнт
  // (у знятому трафіку `NESelectSpawnGroup = 2` на другий прапор).
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
    // Група стоїть біля свого прапора, а не десь на карті.
    CHECK(away < 60.0f);
  }
}

// Порожній перелік дає нуль — а нуль сервер розуміє як «місце не
// обране», і гравець просто не з'явиться. Мовчазної підміни тут бути не
// повинно.
static void testNoGroupsGivesZero() {
  float away = -1.0f;
  CHECK_EQ(nearestSpawnGroup({}, 0.0f, 0.0f, kWorld, &away), std::uint8_t(0));
  CHECK_EQ(away, 0.0f);
}

// Групи загонів сервер шле в центрі карти (127, 127). Прапор, що стоїть
// далеко від центра, не має на них попастися.
static void testSquadGroupsInTheCentreDoNotWin() {
  auto groups = dalianGroups();
  for (std::uint16_t i = 0; i < 20; ++i) {
    groups.push_back({static_cast<std::uint8_t>(192 + i), (i % 2) ? 2u : 1u, false, false, false,
                      127, 127, static_cast<std::uint16_t>(578 + i)});
  }
  float away = 0.0f;
  CHECK_EQ(nearestSpawnGroup(groups, -254.0f, -210.0f, kWorld, &away), std::uint8_t(4));
}

TEST_MAIN({
  testUnpackedPositions();
  testEachFlagFindsItsGroup();
  testNoGroupsGivesZero();
  testSquadGroupsInTheCentreDoNotWin();
})
