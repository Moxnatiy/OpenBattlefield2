#include "check.h"
#include "obf2/game/rotational_bundle.h"
#include "obf2/game/scene.h"
#include <cmath>
#include <map>
#include "obf2/con/interpreter.h"

using namespace obf2;
int main() {
  class Files : public con::FileProvider {
    std::optional<std::string> loadText(std::string_view) override {
      // Objects_server.zip: USJEP_HMMWV.tweak, Cupola yaw and Lav pitch.
      return "ObjectTemplate.create RotationalBundle elevator\n"
             // Objects_server.zip: air_f35b.tweak, AIR_F35B_ElevatorR (a Wing).
             "ObjectTemplate.setMinRotation 0/-8/0\n"
             "ObjectTemplate.setMaxRotation 0/8/0\n"
             "ObjectTemplate.setMaxSpeed 0/300/0\n"
             "ObjectTemplate.setAcceleration 0/-30/0\n"
             "ObjectTemplate.setInputToPitch PIPitch\n"
             "ObjectTemplate.setAutomaticReset 1\n"
             "ObjectTemplate.rememberExcessInput 1\n"
             "ObjectTemplate.create RotationalBundle nav\n"
             // Objects_server.zip: JEEP_FAAV.tweak, JEEP_FAAV_nav_RF.
             "ObjectTemplate.setMinRotation -30/0/0\n"
             "ObjectTemplate.setMaxRotation 30/0/0\n"
             "ObjectTemplate.setMaxSpeed 1/0/0\n"
             "ObjectTemplate.setAcceleration 100/0/0\n"
             "ObjectTemplate.setInputToYaw PIYaw\n"
             "ObjectTemplate.setAutomaticReset 1\n"
             "ObjectTemplate.snapToZeroOnNoInput 1\n"
             "ObjectTemplate.create RotationalBundle cupola\n"
             "ObjectTemplate.setMaxSpeed 90/0/0\n"
             "ObjectTemplate.setAcceleration 5000/0/0\n"
             "ObjectTemplate.setInputToYaw PIMouseLookX\n"
             "ObjectTemplate.automaticYawStabilization 1\n"
             "ObjectTemplate.capMaxSpeed 1\n";
    }
  } files;
  game::Registry registry;
  con::Interpreter script(files,[&](const con::Command& c) { registry.feed(c); });
  script.runFile("test.con");
  auto settings = game::rotationalSettings(*registry.find("cupola"));
  CHECK(settings.supported);
  CHECK(settings.inputs[0] == game::RotationInput::MouseX);
  constexpr float tick = 1.0f/30;
  game::RotationalState state;
  game::stepRotationalBundle(state,settings,{1,0,0},tick);
  CHECK(std::abs(state.angle.x-3) < 1e-5f);
  game::stepRotationalBundle(state,settings,{},tick);
  CHECK(std::abs(state.angle.x-3) < 1e-5f); // release stops, no stale input drift
  state = {};
  game::stepRotationalBundle(state,settings,{327.67f,0,0},1);
  CHECK(std::abs(state.velocity.x-2700) < 1e-4f); // authored cap * registered 30
  settings.minimum.x = -45; settings.maximum.x = 18;
  state = {{17,0,0},{90,0,0}};
  game::stepRotationalBundle(state,settings,{1,0,0},tick);
  CHECK(state.angle.x == 18);
  settings.acceleration.x = -5000;
  state = {};
  game::stepRotationalBundle(state,settings,{1,0,0},tick);
  CHECK(std::abs(state.angle.x+3) < 1e-5f);
  settings.supported = false;
  game::stepRotationalBundle(state,settings,{1,0,0},tick);
  CHECK(std::abs(state.angle.x+3) < 1e-5f); // unsupported branches hold authority
  // Linux 0x585a40: setAutomaticReset steers the angle to input * maxRotation
  // at |acceleration| deg/s; snapToZeroOnNoInput zeroes it on release.
  auto nav = game::rotationalSettings(*registry.find("nav"));
  CHECK(nav.supported && nav.automaticReset && nav.snapToZero);
  game::RotationalState wheel;
  game::stepRotationalBundle(wheel,nav,{1,0,0},tick);
  CHECK(std::abs(wheel.angle.x-100.0f/30) < 1e-4f);
  for (int i = 0; i < 30; ++i) game::stepRotationalBundle(wheel,nav,{1,0,0},tick);
  CHECK(wheel.angle.x == 30);  // held at maxRotation
  game::stepRotationalBundle(wheel,nav,{0.5f,0,0},tick);
  CHECK(std::abs(wheel.angle.x-(30-100.0f/30)) < 1e-4f);  // half input: back toward 15
  game::stepRotationalBundle(wheel,nav,{},tick);
  CHECK(wheel.angle.x == 0);  // released: snapped, not eased
  for (int i = 0; i < 30; ++i) game::stepRotationalBundle(wheel,nav,{-1,0,0},tick);
  CHECK(wheel.angle.x == -30);
  game::stepRotationalBundle(wheel,nav,{0.04f,0,0},tick);
  // An input above -0.05 also zeroes a negative angle; the same tick then
  // moves it toward the small target, 0.04 * 30.
  CHECK(std::abs(wheel.angle.x-1.2f) < 1e-4f);
  // Linux 0x585a40, rememberExcessInput: input past ±1 is stored (to ±40)
  // and spent on later ticks; an input of the other sign empties the store.
  auto elevator = game::rotationalSettings(*registry.find("elevator"));
  CHECK(elevator.supported && elevator.rememberExcess && elevator.automaticReset);
  game::RotationalState flap;
  game::stepRotationalBundle(flap,elevator,{0,3,0},tick);
  CHECK(flap.excess.y == 2);       // 1 used, 2 kept
  CHECK(std::abs(flap.angle.y+1) < 1e-4f);  // toward -8 (negative acceleration) at 30/s
  game::stepRotationalBundle(flap,elevator,{},tick);
  CHECK(flap.excess.y == 1);       // released, the store still drives
  CHECK(std::abs(flap.angle.y+2) < 1e-4f);
  game::stepRotationalBundle(flap,elevator,{0,100,0},tick);
  CHECK(flap.excess.y == 39);      // 1 + 100 held to 40, 1 used
  game::stepRotationalBundle(flap,elevator,{0,-0.5f,0},tick);
  CHECK(flap.excess.y == 0);       // other sign: emptied, raw -0.5 used
  CHECK(std::abs(flap.angle.y+2) < 1e-4f);  // target 4, step +1 from -3
  // Retail 0x66dcc0: a root-relative cupola keeps its heading as the parent
  // main turret turns. Cover both asin quadrants and the unstabilized control.
  constexpr float pi=3.14159265358979323846f;
  const auto rest=translation({1,2,3});
  for (float parentYaw : {-150.0f,-90.0f,0.0f,70.0f,150.0f}) {
    const auto parent=rotationYawPitchRoll(parentYaw,0,0);
    const auto stable=game::composeTurretRotation(parent,rest,Mat4::identity(),{27,0,0},true);
    const auto forward=transformDirection(stable*rest,{0,0,1});
    CHECK(std::abs(std::remainder(std::atan2(forward.x,forward.z)*180/pi-27,360.0f))<.001f);
    const auto plain=game::composeTurretRotation(parent,rest,Mat4::identity(),{27,0,0},false);
    const auto ordinary=transformDirection(plain*rest,{0,0,1});
    CHECK(std::abs(std::remainder(std::atan2(ordinary.x,ordinary.z)*180/pi-parentYaw-27,360.0f))<.001f);
  }
  return obf2test::g_failures ? 1 : 0;
}
