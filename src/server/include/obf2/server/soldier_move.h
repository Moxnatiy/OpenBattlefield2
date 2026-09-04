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

// Такт симуляції рушія. Не «звична» стала, а число з бінаря:
// `dice::hfe::WorldPref::mTickTime` лежить у `.data` лінукс-сервера за
// 0xf68c50 і дорівнює рівно 0.0333333333333333 (double), тобто 1/30 с.
// Читає його `WorldPref::getTickTime` (0x74aa60).
//
// Фізику рушій рахує **тільки** цілими тактами. Тому й у нас крок руху
// не може бути тривалістю кадру: інакше на 120 кадрах стрибок виходить
// інакший, ніж на 60, — саме це й було видно.
inline constexpr float kTickTime = 1.0f / 30.0f;

// Стан, який переживає крок і не належить самому тілу.
struct SwimState {
  bool swimming = false;
};

// Час, накопичений між тактами. Кадр рідко дорівнює такту, тож залишок
// переноситься на наступний кадр, а не губиться і не подовжує крок.
struct TickAccumulator {
  float pending = 0.0f;

  // Скільки цілих тактів визріло за `elapsed`. Довга пауза (завантаження,
  // вікно перетягли) не має намотати сотні тактів за один кадр.
  int take(float elapsed, int limit = 8) {
    pending += elapsed;
    int ticks = 0;
    while (pending >= kTickTime && ticks < limit) {
      pending -= kTickTime;
      ++ticks;
    }
    if (ticks == limit) pending = 0.0f;
    return ticks;
  }
};

// wish — бажаний напрямок у площині (вже повернутий на кут огляду),
// довжина 0..1. terrain і collision можуть бути порожні: без терену
// землею вважається нуль, без колізій стін немає.
void moveSoldier(BodyState& body, SwimState& swim, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step);

}  // namespace obf2::server
