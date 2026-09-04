#include "obf2/server/soldier_move.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace obf2::server {

void moveSoldier(BodyState& body, SwimState& swim, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step) {
  // Земля — це не тільки рельєф. Рушій шукає опору й на об'єктах, інакше
  // на дах чи сходи не зійти. Беремо вищу з двох.
  float ground = terrain != nullptr ? terrain->groundHeightAt(body.position) : 0.0f;
  if (collision != nullptr) {
    // Починаємо трохи вище ніг, щоб знайти й сходинку перед собою.
    Vec3f from = body.position;
    from.y += physics.stepHeight();
    float surface = 0.0f;
    if (collision->groundHeight(from, physics.stepHeight() + 2.0f, physics.feetContactNormal,
                                &surface)) {
      if (surface > ground) ground = surface;
    }
  }

  // Вода. Рушій міряє, наскільки солдат занурений, і з певної частки
  // висоти той спливає (`phy-soldier-start-float`), а назад стає на дно
  // вже з іншої (`stop-float`) — щоб не смикався на межі.
  const float waterLevel = terrain != nullptr ? terrain->terrain.seaLevel : 0.0f;
  const float submersion =
      physics.standHeight > 0.0f ? (waterLevel - body.position.y) / physics.standHeight : 0.0f;
  if (swim.swimming) {
    if (submersion <= physics.stopFloat) swim.swimming = false;
  } else if (submersion >= physics.startFloat) {
    swim.swimming = true;
  }

  if (swim.swimming) {
    // Пливемо: тяжіння не діє, солдат тримається біля поверхні, а
    // швидкість своя (`phy-soldier-swim-speed`).
    const float surface = waterLevel - physics.standHeight * physics.startFloat;
    body.position = body.position + wish * (physics.swimSpeed * step);
    body.position.y += (surface - body.position.y) * std::min(1.0f, step * 4.0f);
    body.velocity = Vec3f{};
    body.onGround = false;
  } else {
    stepSoldier(body, wish, maxSpeed, jump, physics, ground, step);
  }

  // Зіткнення зі стінами: солдат у BF2 це стовпчик сфер, а не одна сфера
  // на рівні грудей (SoldierResponsePhysics::getSoldierHeight). Саме
  // тому він може зійти на сходинку: нижче за stepHeight ми не
  // штовхаємо взагалі, а вище перевіряємо кожну сферу.
  if (collision == nullptr) return;
  const std::vector<float> centers = soldierSphereHeights(physics);
  Vec3f offset{};
  for (const float center : centers) {
    if (center < physics.stepHeight()) continue;
    Vec3f probe = body.position + offset;
    probe.y += center;
    const Vec3f before = probe;
    if (collision->resolveSphere(probe, physics.radius) > 0) {
      offset.x += probe.x - before.x;
      offset.z += probe.z - before.z;
    }
  }
  if (length(offset) > 1e-4f) {
    body.position.x += offset.x;
    body.position.z += offset.z;
    const Vec3f direction = normalize(offset);
    const float into = dot(body.velocity, direction);
    if (into < 0.0f) body.velocity = body.velocity - direction * into;
  }
}

}  // namespace obf2::server
