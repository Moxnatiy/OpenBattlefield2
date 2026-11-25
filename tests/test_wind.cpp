#include <cmath>
#include <vector>

#include "check.h"
#include "obf2/game/wind.h"

using namespace obf2;
using namespace obf2::game;

namespace {

bool near(float a, float b) { return std::abs(a - b) < 1e-4f; }

// `e_exp_grenade`'s affector (Objects_client.zip:Effects/impacts/explosions/
// e_exp_grenade/e_exp_grenade.tweak): Point, speed 10, falloff 10.
WindAffectorTemplate grenade() {
  WindAffectorTemplate t;
  t.type = WindType::Point;
  t.speed = 10;
  t.falloff = 10;
  t.radialFalloff = 5;
  t.blowTime = -1;
  return t;
}

// Point (0x67e740): away from the affector, (1 − min(d²/falloff², 1))·speed.
void testPoint() {
  const WindAffector affector(grenade(), {0, 0, 0});
  const Vec3f w = affector.wind({5, 0, 0});
  CHECK(near(w.x, 7.5f) && near(w.y, 0) && near(w.z, 0));
  CHECK(near(length(affector.wind({0, 0, 10})), 0));
}

// The manager adds the global wind and only affectors the point is inside
// (0x729720, 0x729cd0).
void testManager() {
  WindManager manager;
  CHECK(near(length(manager.wind({1, 2, 3})), 0));  // ctor speed 0
  const WindAffector affector(grenade(), {0, 0, 0});
  manager.add(&affector);
  CHECK(near(manager.wind({0, 5, 0}).y, 7.5f));
  CHECK(near(length(manager.wind({0, 11, 0})), 0));
  manager.setGlobalSpeed(2);
  CHECK(near(manager.wind({0, 11, 0}).z, 2.0f));  // ctor direction 0/0/1
  manager.remove(&affector);
  CHECK(near(manager.wind({0, 5, 0}).y, 0));
}

// Directional turns the template direction by the object; same falloff.
void testDirectional() {
  auto t = grenade();
  t.type = WindType::Directional;
  t.direction = {0, -1, 0};
  const WindAffector affector(t, {});
  const Vec3f w = affector.wind({0, 0, 0});
  CHECK(near(w.y, -10.0f));
}

// RadialDir: along the axis the speed falls by along/falloff; off the axis
// beyond radialFalloff it is 0; the direction blends towards the radial.
void testRadial() {
  auto t = grenade();
  t.type = WindType::RadialDir;
  t.direction = {0, 1, 0};
  const WindAffector affector(t, {});
  float speed = 0;
  Vec3f direction{};
  affector.directionAndSpeed({0, 5, 0}, direction, speed);
  CHECK(near(speed, 5.0f));
  CHECK(near(direction.y, 0.8f));  // radial 0: out = 0.2, axis · 0.8
  affector.directionAndSpeed({6, 0, 0}, direction, speed);
  CHECK(speed == 0.0f);
}

// The AH-1Z rotor's downwash (Objects_client.zip:Vehicles/Air/ahe_ah1z/
// ahe_ah1z.tweak, Engine ahe_ah1z_Rotor): RadialDir, direction 0/0/−1, speed
// 20, falloff 30, radial 20. The axis is that direction as a row vector times
// the owner's matrix (0x67e740 → 0x5a1770): with the part's z row pointing up,
// the axis points down and a point below the rotor feels the wind downwards.
void testRotorOrientation() {
  WindAffectorTemplate t;
  t.type = WindType::RadialDir;
  t.direction = {0, 0, -1};
  t.speed = 20;
  t.falloff = 30;
  t.radialFalloff = 20;
  Mat4 m = Mat4::identity();
  m.m[5] = 0;   // the y row (m[4..6]) becomes 0/0/−1
  m.m[6] = -1;
  m.m[9] = 1;   // the z row (m[8..10]) becomes 0/1/0
  m.m[10] = 0;
  const WindAffector rotor(t, {0, 10, 0}, m);
  float speed = 0;
  Vec3f direction{};
  rotor.directionAndSpeed({0, 4, 0}, direction, speed);  // 6 below, on the axis
  CHECK(near(speed, 16.0f));                             // (1 − 6/30)·20
  CHECK(near(direction.y, -0.8f));
}

// The rotor Engine spins only about its own z (`setMaxRotation 0/0/150`,
// `setInputToRoll PIThrottle`; the engine update 0x680200 turns an axis only
// when its template rate is set). The downwash axis is 0/0/−1 · M, which lies
// on that z, so any spin angle leaves the wind as it was.
void testRotorSpinInvariant() {
  WindAffectorTemplate t;
  t.type = WindType::RadialDir;
  t.direction = {0, 0, -1};
  t.speed = 20;
  t.falloff = 30;
  t.radialFalloff = 20;
  const Mat4 rest = rotationYawPitchRoll(0, -86, 0);  // ahe_ah1z_Rotor's setRotation
  const WindAffector still(t, {}, rest);
  for (const float spin : {37.0f, 150.0f, 290.0f}) {
    const WindAffector spun(t, {}, rest * rotationYawPitchRoll(0, 0, spin));
    for (const Vec3f at : {Vec3f{3, -5, 1}, Vec3f{-8, -12, 4}}) {
      const Vec3f a = still.wind(at), b = spun.wind(at);
      CHECK(near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z));
    }
  }
}

// A scoped affector is in the manager while it lives, also across a move.
void testScoped() {
  WindManager manager;
  {
    std::vector<ScopedWindAffector> held;
    held.emplace_back(manager, grenade());
    held.emplace_back(manager, grenade());  // the vector moves the first
    CHECK(manager.affectors().size() == 2);
    held[1]->setPosition({100, 0, 0});  // out of reach; only the first blows
    CHECK(near(manager.wind({5, 0, 0}).x, 7.5f));
    // setActive (slot +0x40), as the engine's messages call it: off blows 0.
    held[0]->setActive(false);
    CHECK(near(length(manager.wind({5, 0, 0})), 0));
    held.clear();
  }
  CHECK(manager.affectors().empty());
}

// Blow time counts down above 0 and turns the affector off (0x67e630); −1 never.
void testBlowTime() {
  auto t = grenade();
  t.blowTime = 0.5f;
  WindAffector timed(t, {});
  CHECK(timed.update(0.3f));
  CHECK(!timed.update(0.3f));
  CHECK(near(length(timed.wind({1, 0, 0})), 0));
  WindAffector forever(grenade(), {});
  CHECK(forever.update(100.0f));
}

}  // namespace

TEST_MAIN({
  testPoint();
  testManager();
  testDirectional();
  testRadial();
  testRotorOrientation();
  testScoped();
  testRotorSpinInvariant();
  testBlowTime();
})
