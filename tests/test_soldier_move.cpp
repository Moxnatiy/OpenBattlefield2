// Рух солдата не має залежати від частоти кадрів.
//
// Рушій рахує фізику тактами по `WorldPref::mTickTime` = 1/30 с
// (лінукс-сервер, `.data` за 0xf68c50). Ми довго рахували крок завдовжки
// з кадр — і той самий стрибок на 120 кадрах виходив інакшим, ніж на 60.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "obf2/server/soldier_move.h"
#include "check.h"

namespace {

using namespace obf2;
using namespace obf2::server;

// Солдат, підкинутий угору й посланий уперед, за рівно секунду часу —
// нарізану кадрами різної тривалості. Швидкість задаємо прямо, а не
// кнопкою стрибка: тоді перевірка міряє саме інтегрування, не залежачи
// від того, в який кадр припало натискання.
Vec3f flyForOneSecond(float frameSeconds) {
  PhysicsConstants physics;
  BodyState body;
  body.position = Vec3f{0.0f, 0.0f, 0.0f};
  body.velocity = Vec3f{0.0f, 5.0f, 0.0f};
  body.onGround = false;
  SwimState swim;
  TickAccumulator accumulator;

  const int frames = static_cast<int>(std::lround(1.0f / frameSeconds));
  for (int frame = 0; frame < frames; ++frame) {
    const int ticks = accumulator.take(frameSeconds);
    for (int i = 0; i < ticks; ++i) {
      moveSoldier(body, swim, Vec3f{0.0f, 0.0f, 1.0f}, physics.runSpeed, false, physics, nullptr,
                  nullptr, kTickTime);
    }
  }
  return body.position;
}

void testMovementDoesNotDependOnFrameRate() {
  const Vec3f at60 = flyForOneSecond(1.0f / 60.0f);
  const Vec3f at120 = flyForOneSecond(1.0f / 120.0f);
  const Vec3f at30 = flyForOneSecond(1.0f / 30.0f);

  // Такт один і той самий, тож за секунду набігає та сама кількість
  // тактів, і кінець має збігтися до похибки float.
  CHECK(std::abs(at60.z - at120.z) < 1e-3f);
  CHECK(std::abs(at60.z - at30.z) < 1e-3f);
  CHECK(std::abs(at60.y - at120.y) < 1e-3f);
  CHECK(std::abs(at60.y - at30.y) < 1e-3f);

  // І рух справді був: інакше збіг нічого не значив би. У повітрі розгін
  // притлумлений (`phy-soldier-air-movement-factor`), тож уперед за
  // секунду набігає небагато — але не нуль.
  CHECK(at60.z > 0.1f);
}

// Накопичувач не має ні губити час, ні видавати зайвих тактів.
void testAccumulatorKeepsTheRemainder() {
  TickAccumulator accumulator;
  int total = 0;
  // Тридцять кадрів по 1/60 с — це рівно 15 тактів по 1/30 с.
  for (int i = 0; i < 30; ++i) total += accumulator.take(1.0f / 60.0f);
  CHECK_EQ(total, 15);

  // Кадр, коротший за такт, сам по собі такту не дає, але час не зникає.
  TickAccumulator slow;
  CHECK_EQ(slow.take(0.01f), 0);
  CHECK_EQ(slow.take(0.01f), 0);
  CHECK_EQ(slow.take(0.02f), 1);
}

}  // namespace

// Стрибок має ті висоту й тривалість, що дають сталі рушія: початкова
// швидкість 6.0 (0xb355c8) і тяжіння 14.73 (0x6d6ae4).
void testJumpMatchesEngineConstants() {
  PhysicsConstants physics;
  BodyState body;
  body.onGround = true;
  SwimState swim;

  float highest = 0.0f;
  float airborne = 0.0f;
  for (int tick = 0; tick < 200; ++tick) {
    const bool jump = tick == 0;
    moveSoldier(body, swim, Vec3f{}, physics.runSpeed, jump, physics, nullptr, nullptr, kTickTime);
    highest = std::max(highest, body.position.y);
    if (!body.onGround) airborne += kTickTime;
    else if (tick > 0 && airborne > 0.0f) break;
  }

  // Неперервна формула дає 6^2/(2*14.73) = 1.222 м і 2*6/14.73 = 0.815 с.
  // Рушій, як і ми, інтегрує тактами по 1/30, і від дискретності вершина
  // виходить трохи вище — близько 1.31 м. Порівнюємо саме з тим, що дає
  // такт, а не з ідеальною формулою.
  CHECK(highest > 1.25f && highest < 1.40f);
  CHECK(airborne > 0.75f && airborne < 0.92f);

  // І окремо — те, заради чого це міряється: зі старими вигаданими
  // числами (тяжіння 9.81, поштовх 5.0) солдат висів би в повітрі понад
  // секунду. Саме це й було видно як «задовгий стрибок».
  CHECK(airborne < 1.0f);
}

TEST_MAIN({
  testMovementDoesNotDependOnFrameRate();
  testJumpMatchesEngineConstants();
  testAccumulatorKeepsTheRemainder();
});
