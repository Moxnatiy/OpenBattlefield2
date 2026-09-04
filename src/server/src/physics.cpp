#include "obf2/server/physics.h"

#include <algorithm>

namespace obf2::server {

void PhysicsConstants::bind(engine::Console& console) {
  // `Vars.Set <ім'я> <значення>` — перший аргумент це ім'я змінної.
  console.bind("Vars.Set", [this](const con::Command& command) {
    const std::string_view name = command.argStr(0);
    const auto value = command.argFloat(1);
    if (!value) return;

    if (name == "coll-soldier-radius") radius = *value;
    else if (name == "coll-soldier-stand-height") standHeight = *value;
    else if (name == "coll-soldier-crouch-height") crouchHeight = *value;
    else if (name == "coll-soldier-prone-height") proneHeight = *value;
    else if (name == "phy-soldier-walk-speed") walkSpeed = *value;
    else if (name == "phy-soldier-run-speed") runSpeed = *value;
    else if (name == "phy-soldier-sprint-speed") sprintSpeed = *value;
    else if (name == "phy-soldier-crouch-speed") crouchSpeed = *value;
    else if (name == "phy-soldier-crawl-speed") crawlSpeed = *value;
    else if (name == "phy-soldier-swim-speed") swimSpeed = *value;
    else if (name == "phy-soldier-inair-speed") inAirSpeed = *value;
    else if (name == "phy-soldier-feet-contact-normal") feetContactNormal = *value;
    else if (name == "phy-soldier-feet-level") feetLevel = *value;
    else if (name == "phy-soldier-start-float") startFloat = *value;
    else if (name == "phy-soldier-stop-float") stopFloat = *value;
    else if (name == "phy-soldier-acceleration") acceleration = *value;
    else if (name == "phy-soldier-deceleration") deceleration = *value;
    else if (name == "phy-soldier-air-movement-factor") airMovementFactor = *value;
    else if (name == "phy-soldier-speed-factor") speedFactor = *value;
    else if (name == "phy-soldier-look-factor-x") lookFactorX = *value;
    else if (name == "phy-soldier-look-factor-y") lookFactorY = *value;
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

std::vector<float> soldierSphereHeights(const PhysicsConstants& constants) {
  // Поки що солдат завжди стоїть: присідання й лежання ще немає.
  const int count = constants.standSpheres;
  const float height = constants.standHeight;
  std::vector<float> centers;
  if (count <= 0) return centers;

  const float spacing =
      count > 1 ? (height - 2.0f * constants.radius) / static_cast<float>(count - 1) : 0.0f;
  centers.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    centers.push_back(constants.radius + spacing * static_cast<float>(i));
  }
  return centers;
}

}  // namespace obf2::server
