#include <cmath>
#include <cstdint>

#include "check.h"
#include "obf2/con/interpreter.h"
#include "obf2/game/engine_update.h"
#include "obf2/game/rotational_bundle.h"

using namespace obf2;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
constexpr float kTick = 1.0f / 30.0f;

// Objects_server.zip: Vehicles/Land/JEEP_FAAV/JEEP_FAAV.tweak, lines 411–462
// (JEEP_FAAV_Engine) and 491–525 (S_JEEP_FAAV_Engine_Rpm1, _Rpm2).
class Files : public con::FileProvider {
  std::optional<std::string> loadText(std::string_view) override {
    return "ObjectTemplate.create Engine JEEP_FAAV_Engine\n"
           "ObjectTemplate.setMinRotation 0/0/-1\n"
           "ObjectTemplate.setMaxRotation 0/0/1\n"
           "ObjectTemplate.setMaxSpeed 0/0/10000\n"
           "ObjectTemplate.setAcceleration 0/0/10\n"
           "ObjectTemplate.setInputToRoll PIThrottle\n"
           "ObjectTemplate.setAutomaticReset 1\n"
           "ObjectTemplate.setEngineType c_ETNewCar2\n"
           "ObjectTemplate.setTorque 300\n"
           "ObjectTemplate.setDifferential 2\n"
           "ObjectTemplate.setNumberOfGears 5\n"
           "ObjectTemplate.setGearUp 0.85\n"
           "ObjectTemplate.setGearDown 0.5\n"
           "ObjectTemplate.setGearChangeTime 0.7\n";
  }
};

struct Faav {
  game::Registry registry;
  game::RotationalSettings bundle;
  game::EngineUpdateSettings engine;
  Faav() {
    Files files;
    con::Interpreter script(files, [&](const con::Command& c) { registry.feed(c); });
    script.runFile("faav.con");
    bundle = game::rotationalSettings(*registry.find("JEEP_FAAV_Engine"));
    engine = game::engineUpdateSettings(*registry.find("JEEP_FAAV_Engine"));
  }
};

void testSettings() {
  const Faav faav;
  CHECK(faav.bundle.supported);
  CHECK(faav.bundle.automaticReset);
  CHECK(faav.bundle.inputs[2] == game::RotationInput::Throttle);
  CHECK_EQ(faav.engine.type, 0xc2u);
  CHECK_EQ(faav.engine.maxRotation.z, 1.0f);
  CHECK_EQ(faav.engine.numberOfGears, 5);
  CHECK(near(faav.engine.gearUp, 0.85f));
  CHECK(near(faav.engine.gearDown, 0.5f));
  CHECK(near(faav.engine.gearChangeTime, 0.7f));
  CHECK_EQ(game::engineTypeFromName("C_ETHELICOPTER"), 0x21u);
  CHECK_EQ(game::engineTypeFromName("c_ETBoat"), 0u);
}

// One tick at full throttle: the angle moves 10 · 1/30 toward 1, and the
// rpm 0.05 · (1/3 − 0) from 0 (Engine::handleUpdate 0x62f020).
void testFirstTick() {
  const Faav faav;
  game::RotationalState bundle;
  game::EngineUpdateState engine;
  engine.on = true;
  game::stepRotationalBundle(bundle, faav.bundle, {0, 0, 1}, kTick);
  CHECK(near(bundle.angle.z, 1.0f / 3.0f));
  CHECK(game::engineHandleUpdate(engine, faav.engine, bundle.angle, 1, 0, kTick));
  CHECK(near(engine.rpm, 0.05f / 3.0f, 1e-6f));
  CHECK_EQ(engine.gear, 1);
  // Engine off: the rpm is 0 whatever the angle.
  game::EngineUpdateState off;
  game::engineHandleUpdate(off, faav.engine, bundle.angle, 1, 0, kTick);
  CHECK_EQ(off.rpm, 0.0f);
}

// Held throttle: rpm → 2 · w/maxW, clamped at 1.2; past gearUp with no load
// one gear a tick up to 5. Released: the angle returns, the rpm falls by
// 0.975 a tick, and below gearDown the gear steps back down to 1.
void testRunUpAndDown() {
  const Faav faav;
  game::RotationalState bundle;
  game::EngineUpdateState engine;
  engine.on = true;
  int firstShift = -1;
  for (int tick = 0; tick < 90; ++tick) {
    game::stepRotationalBundle(bundle, faav.bundle, {0, 0, 1}, kTick);
    game::engineHandleUpdate(engine, faav.engine, bundle.angle, 1, 0, kTick);
    if (engine.gear > 1 && firstShift < 0) firstShift = tick;
  }
  CHECK(near(engine.rpm, 1.2f));
  CHECK_EQ(engine.gear, 5);
  // rpm_n = 2 − 2·0.975^n after the angle is at 1 passes 0.85 around tick 23.
  CHECK(firstShift > 15 && firstShift < 30);
  for (int tick = 0; tick < 120; ++tick) {
    game::stepRotationalBundle(bundle, faav.bundle, {}, kTick);
    game::engineHandleUpdate(engine, faav.engine, bundle.angle, 0, 0, kTick);
  }
  CHECK_EQ(bundle.angle.z, 0.0f);
  CHECK(engine.rpm < 0.5f);
  CHECK_EQ(engine.gear, 1);
  // Under 0.01 the rpm is 0.
  for (int tick = 0; tick < 200; ++tick) game::engineHandleUpdate(engine, faav.engine, {}, 0, 0, kTick);
  CHECK_EQ(engine.rpm, 0.0f);
}

}  // namespace

TEST_MAIN({
  testSettings();
  testFirstTick();
  testRunUpAndDown();
})
