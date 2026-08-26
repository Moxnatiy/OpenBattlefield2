#include "obf2/server/physics.h"

#include <algorithm>

namespace obf2::server {

void PhysicsConstants::bind(engine::Console& console) {
  // `Vars.Set <ім'я> <значення>` — перший аргумент це ім'я змінної.
  console.bind("Vars.Set", [this](const con::Command& command) {
    const std::string_view name = command.argStr(0);
    const auto value = command.argFloat(1);
    if (!value) return;

    if (name == "phy-soldier-acceleration") acceleration = *value;
    else if (name == "phy-soldier-deceleration") deceleration = *value;
    else if (name == "phy-soldier-air-movement-factor") airMovementFactor = *value;
    else if (name == "phy-soldier-speed-factor") speedFactor = *value;
    else if (name == "phy-soldier-jump-factor") jumpFactor = *value;
  });
}

void stepSoldier(BodyState& body, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& constants, float groundHeight, float step) {
  const float targetSpeed = maxSpeed * constants.speedFactor;

  // У повітрі керування майже немає — саме тому в BF2 не можна змінити
  // напрямок стрибка в польоті.
  const float control = body.onGround ? 1.0f : constants.airMovementFactor;

  const Vec3f target{wish.x * targetSpeed, 0.0f, wish.z * targetSpeed};
  const Vec3f horizontal{body.velocity.x, 0.0f, body.velocity.z};

  // Розгін і гальмування мають різні коефіцієнти: зупиняється солдат
  // швидше, ніж розганяється (0.4 проти 0.2).
  const bool accelerating = length(target) > length(horizontal);
  const float rate = (accelerating ? constants.acceleration : constants.deceleration) * control;

  // Коефіцієнти задані на такт 30 Гц, тому масштабуємо під фактичний крок.
  const float blend = std::min(1.0f, rate * step * 30.0f);
  const Vec3f moved = horizontal + (target - horizontal) * blend;

  body.velocity.x = moved.x;
  body.velocity.z = moved.z;

  if (jump && body.onGround) {
    body.velocity.y = constants.jumpSpeed * constants.jumpFactor;
    body.onGround = false;
  } else if (!body.onGround) {
    body.velocity.y -= constants.gravity * step;
  }

  body.position = body.position + body.velocity * step;

  // Земля: нижче терену не провалюємось. Це поки що єдина колізія —
  // геометрія об'єктів ще не бере участі.
  if (body.position.y <= groundHeight) {
    body.position.y = groundHeight;
    if (body.velocity.y < 0.0f) body.velocity.y = 0.0f;
    body.onGround = true;
  } else {
    body.onGround = false;
  }
}

}  // namespace obf2::server
