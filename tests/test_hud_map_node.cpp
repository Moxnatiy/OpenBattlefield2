// Карта: ціль за станом HUD (BF2.exe 0x777dc0) і виведення
// MapFullSize/MapMinSize з поточного розміру (0x77c330).
#include <cmath>

#include "obf2/hud/map_node.h"
#include "check.h"

using namespace obf2;

namespace {

// Прокрутити секунди кроком такту рушія (1/30).
void run(hud::MapNode& map, float seconds) {
  const float step = 1.0f / 30.0f;
  for (float t = 0.0f; t < seconds; t += step) map.update(step);
}

}  // namespace

// У бою карта стоїть мініатюрою в кутку: 197x197 на (597, 0) —
// setMiniPos 197/-300 плюс пів екрана. І це той стан, у якому ввімкнена
// `MapMinSize`, а не `MapFullSize`.
void testIngameIsMiniMap() {
  hud::MapNode map;
  map.applyState(hud::kMapStateIngame);
  run(map, 1.0f);

  CHECK(std::abs(map.size().x - 197.0f) < 0.5f);
  CHECK(std::abs(map.position().x - 597.0f) < 0.5f);
  CHECK(std::abs(map.position().y - 0.0f) < 0.5f);
  CHECK(map.minSize());
  CHECK(!map.fullSize());
  CHECK(map.settled());
}

// Клавіша M — це стан 2: карта їде на setMaxiPos -122/-273 і виростає до
// 512x512, після чого вмикається `MapFullSize`.
void testBigMapGrowsToMaxi() {
  hud::MapNode map;
  map.applyState(hud::kMapStateIngame);
  run(map, 1.0f);

  map.applyState(hud::kMapStateBigMap);
  run(map, 3.0f);

  CHECK(std::abs(map.size().x - 512.0f) < 0.5f);
  CHECK(std::abs(map.size().y - 512.0f) < 0.5f);
  CHECK(std::abs(map.position().x - 278.0f) < 0.5f);
  CHECK(std::abs(map.position().y - 27.0f) < 0.5f);
  CHECK(map.fullSize());
  CHECK(!map.minSize());
}

// Головне, заради чого це й розбиралося: **поки розмір у дорозі, не
// ввімкнена жодна** зі змінних показу. Доти ми перемикали їх разом зі
// станом, і рамки мінікарти й великої карти встигали накластися.
void testNeitherFlagWhileMoving() {
  hud::MapNode map;
  map.applyState(hud::kMapStateIngame);
  run(map, 1.0f);

  map.applyState(hud::kMapStateBigMap);
  map.update(1.0f / 30.0f);

  CHECK(!map.fullSize());
  CHECK(!map.minSize());
  CHECK(!map.settled());
  // Розмір уже пішов угору, але до кінця не доїхав.
  CHECK(map.size().x > 197.0f);
  CHECK(map.size().x < 512.0f);
}

// Швидке меню масштабу (стан 19, `MapMenuShow`) лягає поверх карти й
// саму карту не рухає — у 0x777e11 цей номер потрапляє в гілку, що
// цілі не чіпає.
void testMapMenuDoesNotMoveMap() {
  hud::MapNode map;
  map.applyState(hud::kMapStateBigMap);
  run(map, 3.0f);

  map.applyState(19);
  run(map, 1.0f);

  CHECK(std::abs(map.size().x - 512.0f) < 0.5f);
  CHECK(map.fullSize());
}

// Командирська карта більша за велику: 561x561 на (239, 19).
void testCommanderMap() {
  hud::MapNode map;
  map.applyState(hud::kMapStateCommander);
  run(map, 4.0f);

  CHECK(std::abs(map.size().x - 561.0f) < 0.5f);
  CHECK(std::abs(map.position().x - 239.0f) < 0.5f);
  CHECK(map.fullSize());
}

TEST_MAIN({
  testIngameIsMiniMap();
  testBigMapGrowsToMaxi();
  testNeitherFlagWhileMoving();
  testMapMenuDoesNotMoveMap();
  testCommanderMap();
});
