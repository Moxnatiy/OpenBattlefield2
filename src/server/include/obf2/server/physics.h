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
#include <vector>

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

  // --- сталі рушія (docs/functions/soldier-physics.md) ---
  //
  // Це типові значення з самого рушія, витягнуті з Linux-сервера. Дані гри
  // можуть їх перевизначити через ті самі Vars.Set — тоді виграють дані,
  // як в оригіналі.
  float walkSpeed = 1.5f;      // phy-soldier-walk-speed
  float runSpeed = 3.9f;       // phy-soldier-run-speed
  float sprintSpeed = 7.0f;    // phy-soldier-sprint-speed
  float crouchSpeed = 2.0f;    // phy-soldier-crouch-speed
  float crawlSpeed = 0.8f;     // phy-soldier-crawl-speed
  float swimSpeed = 2.1f;      // phy-soldier-swim-speed
  float inAirSpeed = 2.0f;     // phy-soldier-inair-speed

  // Форма солдата: стовпчик сфер. Кількість залежить від пози.
  float radius = 0.25f;        // coll-soldier-radius
  float standHeight = 1.7f;    // coll-soldier-stand-height
  float crouchHeight = 1.4f;   // coll-soldier-crouch-height
  float proneHeight = 0.8f;    // coll-soldier-prone-height

  // Наскільки полога поверхня ще тримає: 0.5 це нахил до 60 градусів.
  float feetContactNormal = 0.5f;  // phy-soldier-feet-contact-normal
  float feetLevel = -0.04f;        // phy-soldier-feet-level

  // Частка висоти солдата у воді, з якої він спливає і з якої знову стає
  // на дно. Гістерезис, щоб не смикався на межі.
  float startFloat = 0.99f;  // phy-soldier-start-float
  float stopFloat = 0.9f;    // phy-soldier-stop-float

  // Скільки сфер у стовпчику за позою — з `getSoldierHeight` рушія.
  int standSpheres = 5;
  int crouchSpheres = 3;
  int proneSpheres = 1;

  // Наскільки високу сходинку солдат переступає. Це не окрема змінна
  // рушія, а наслідок форми: нижня сфера має діаметр 2 * radius, і все,
  // що нижче за неї, вона проходить, виштовхуючись угору по ребру.
  float stepHeight() const { return radius * 2.0f; }

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

// Центри сфер солдата над рівнем ніг. Кількість і крок — за формулою
// рушія: spacing = (висота - 2 * радіус) / (кількість - 1).
std::vector<float> soldierSphereHeights(const PhysicsConstants& constants);

}  // namespace obf2::server
