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

// Компас доводить кут до напряму гравця. Двома згладжувачами поспіль,
// тож і повільніше за один.
void testCompassFollowsTheLook() {
  hud::MapAngle angle;
  angle.setTarget(1.5f);
  for (int i = 0; i < 90; ++i) angle.update(1.0f / 30.0f);

  CHECK(std::abs(angle.angle() - 1.5f) < 0.01f);
  CHECK(std::abs(angle.delayed() - 1.5f) < 0.01f);
}

// Затриманий кут відстає від згладженого — саме через це в оригіналі
// компас доїжджає після повороту, а не разом із ним.
void testDelayedLagsBehind() {
  hud::MapAngle angle;
  angle.setTarget(1.5f);
  angle.update(1.0f / 30.0f);

  CHECK(angle.angle() > 0.0f);
  CHECK(angle.delayed() < angle.angle());
}

// Через нуль кут іде найкоротшим шляхом, а не через півкола: 3.0 -> -3.0
// це 0.28 радіана вперед, а не 6.0 назад.
void testShortestWayAroundZero() {
  hud::MapAngle angle;
  angle.setTarget(3.0f);
  for (int i = 0; i < 90; ++i) angle.update(1.0f / 30.0f);

  angle.setTarget(-3.0f);
  angle.update(1.0f / 30.0f);
  // Пішли далі за пі, тобто перескочили межу, а не поповзли назад до нуля.
  CHECK(angle.angle() > 3.0f || angle.angle() < -3.0f);
}

// Мінікарта їде за гравцем: центр доводиться до його місця, а не
// стрибає. Саме через це в оригіналі карта повзе, а не смикається.
void testCentreFollowsThePlayer() {
  hud::MapNode map;
  map.applyState(hud::kMapStateIngame);
  map.update(1.0f / 30.0f);

  map.setCentre(0.75f, 0.25f);
  map.update(1.0f / 30.0f);
  // Зрушило, але ще не доїхало.
  CHECK(map.centre().x > 0.5f);
  CHECK(map.centre().x < 0.75f);

  for (int i = 0; i < 90; ++i) map.update(1.0f / 30.0f);
  CHECK(std::abs(map.centre().x - 0.75f) < 0.001f);
  CHECK(std::abs(map.centre().y - 0.25f) < 0.001f);
}

// Масштаб: кожен наступний номер наближає в 2.3 раза, і значення теж
// доводиться, а не перемикається.
void testZoomIsAPowerOfBase() {
  hud::MapNode map;
  CHECK(std::abs(map.zoomScale() - 1.0f) < 0.001f);

  map.setZoomIndex(2);
  for (int i = 0; i < 120; ++i) map.update(1.0f / 30.0f);
  CHECK(std::abs(map.zoom() - 2.0f) < 0.01f);
  CHECK(std::abs(map.zoomScale() - 2.3f * 2.3f) < 0.05f);
}

// Номерів рівно три — більше в таблицях вузла карти немає.
void testZoomIndexIsClamped() {
  hud::MapNode map;
  map.setZoomIndex(7);
  CHECK_EQ(map.zoomIndex(), hud::kMapZoomLevels - 1);
  map.setZoomIndex(-3);
  CHECK_EQ(map.zoomIndex(), 0);
}

TEST_MAIN({
  testIngameIsMiniMap();
  testBigMapGrowsToMaxi();
  testNeitherFlagWhileMoving();
  testMapMenuDoesNotMoveMap();
  testCommanderMap();
  testCompassFollowsTheLook();
  testDelayedLagsBehind();
  testShortestWayAroundZero();
  testCentreFollowsThePlayer();
  testZoomIsAPowerOfBase();
  testZoomIndexIsClamped();
});
