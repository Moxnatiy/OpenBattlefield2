#pragma once
// Фізика руху — на константах самої гри.
//
// BF2 тримає їх у даних, а не в коді: `objects/soldiers/common/common.con`
// задає
//
//   Vars.Set phy-soldier-acceleration        0.2
//   Vars.Set phy-soldier-deceleration        0.4
//   Vars.Set phy-soldier-air-movement-factor 0.05
//   Vars.Set phy-soldier-speed-factor        1.0
//   Vars.Set phy-soldier-jump-factor         1.0
//
// Ці значення читаються нашим же інтерпретатором і потрапляють сюди —
// тобто поведінка збігається з оригіналом настільки, наскільки її взагалі
// визначають дані.
//
// **Чого тут свідомо немає.** Базова швидкість ходьби в BF2 задана не
// числом, а **анімацією**: система `AnimationSystem3p.inc` рухає солдата
// разом із програванням кліпу. Доки скелетної анімації немає, швидкість
// береться з налаштувань сервера, і це єдине місце, де ми відходимо від
// оригіналу. Занотовано в docs/TODO.md.
#include "obf2/core/math.h"
#include "obf2/engine/console.h"

namespace obf2::server {

struct PhysicsConstants {
  // Частка від максимальної швидкості, яка додається за такт. 0.2 означає,
  // що солдат розганяється приблизно за п'ять тактів.
  float acceleration = 0.2f;
  float deceleration = 0.4f;
  // Наскільки керованим лишається рух у повітрі: 0.05 — майже ніяк.
  float airMovementFactor = 0.05f;
  float speedFactor = 1.0f;
  float jumpFactor = 1.0f;

  // Прискорення вільного падіння. У даних його немає — це фізична стала.
  float gravity = 9.81f;
  // Початкова швидкість стрибка, множиться на jumpFactor.
  float jumpSpeed = 5.0f;

  // Реєструє обробники Vars.Set у консолі: значення приходять із .con гри.
  void bind(engine::Console& console);
};

// Стан руху одного тіла.
struct BodyState {
  Vec3f position;
  Vec3f velocity;
  bool onGround = false;
};

// Один крок руху солдата.
//
// wish — бажаний напрямок у площині (вже повернутий на кут огляду),
// довжина 0..1. groundHeight — висота терену під тілом.
void stepSoldier(BodyState& body, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& constants, float groundHeight, float step);

}  // namespace obf2::server
