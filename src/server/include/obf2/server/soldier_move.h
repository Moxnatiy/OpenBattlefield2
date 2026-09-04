#pragma once
// Один крок руху солдата — цілком: земля, вода, тяжіння, стіни.
//
// Це та сама дія, яку рушій виконує і на сервері, і на клієнті: сервер
// рухає тіло, клієнт передбачає рух свого солдата тим самим кроком і тими
// самими сталими (`PlayerControlObjectNetworkable` живе на клієнті, а
// фізика солдата в `SoldierResponsePhysics` — одна на обох).
//
// Тому й у нас це **одна** функція, а не дві схожі. Коли вона була
// подвоєна, клієнтська половина знала лише висоту землі — і солдат
// проходив крізь стіни на справжньому сервері, хоч у власній грі не
// проходив.
#include "obf2/core/math.h"
#include "obf2/level/level.h"
#include "obf2/server/collision_world.h"
#include "obf2/server/physics.h"

namespace obf2::server {

// Стан, який переживає крок і не належить самому тілу.
struct SwimState {
  bool swimming = false;
};

// wish — бажаний напрямок у площині (вже повернутий на кут огляду),
// довжина 0..1. terrain і collision можуть бути порожні: без терену
// землею вважається нуль, без колізій стін немає.
void moveSoldier(BodyState& body, SwimState& swim, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step);

}  // namespace obf2::server
