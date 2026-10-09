// The first-person camera spring (`obf2/anim/camera_spring.h`): the numbers as
// the M4's and the soldier's `AnimationSystem1p.inc` write them, the merge, the
// fall back to rest and one frame of the update.
#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/anim/camera_spring.h"
#include "obf2/anim/system.h"

using namespace obf2;

namespace {

bool near(float a, float b, float tolerance = 1e-5f) { return std::abs(a - b) < tolerance; }

anim::System build(const std::vector<std::string>& lines) {
  anim::System system;
  for (const std::string& line : lines) {
    const std::vector<std::string> tokens = con::tokenizeLine(line);
    if (tokens.empty()) continue;
    con::Command command;
    command.path = con::splitCommandPath(tokens[0]);
    command.lowerPath = tokens[0];
    for (char& c : command.lowerPath) {
      c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    command.args.assign(tokens.begin() + 1, tokens.end());
    system.feed(command);
  }
  return system;
}

// `objects/weapons/handheld/usrif_m4/animationsystem1p.inc`, its first lines.
anim::System m4() {
  return build({
      "animationSystem.cameraSpring.use 1",
      "animationSystem.cameraSpring.amplitude 0.02/0.02/0.02",
      "animationSystem.cameraSpring.zoomDamping 2.9/2.9/0",
      "animationSystem.cameraSpring.stiffness 10/10/10",
      "animationSystem.cameraSpring.move 2/2/2",
      "animationSystem.cameraSpring.look 1/1",
      "animationSystem.cameraSpring.fire 0/0/40",
  });
}

// `objects/soldiers/common/animations/animationsystem1p.inc`.
anim::System soldier() {
  return build({
      "animationSystem.cameraSpring.use 1",
      "animationSystem.cameraSpring.amplitude 0.045/0.045/0.045",
      "animationSystem.cameraSpring.zoomDamping 2.5/2.5/0",
      "animationSystem.cameraSpring.stiffness 10/10/10",
      "animationSystem.cameraSpring.move 3/3/3",
      "animationSystem.cameraSpring.look 1/1",
      "animationSystem.cameraSpring.fire 0/0/150",
  });
}

}  // namespace

static void testScriptFillsTheFields() {
  const anim::CameraSpringTemplate& spring = m4().cameraSpring();
  CHECK(spring.use);
  CHECK(near(spring.amplitude.x, 0.02f) && near(spring.amplitude.z, 0.02f));
  CHECK(near(spring.zoomDamping.x, 2.9f) && near(spring.zoomDamping.z, 0.0f));
  CHECK(near(spring.stiffness.y, 10.0f));
  CHECK(near(spring.move.z, 2.0f));
  CHECK(near(spring.look[0], 1.0f) && near(spring.look[1], 1.0f));
  CHECK(near(spring.fire.z, 40.0f) && near(spring.fire.x, 0.0f));
  CHECK(!build({}).cameraSpring().use);
}

// The weapon's numbers over the soldier's, field by field; a zero field keeps
// the soldier's.
static void testMergeTakesWhatIsSet() {
  anim::CameraSpringTemplate merged = soldier().cameraSpring();
  anim::CameraSpringTemplate weapon = m4().cameraSpring();
  weapon.stiffness = Vec3f{};
  weapon.look[0] = weapon.look[1] = 0.0f;
  merged.merge(weapon);
  CHECK(near(merged.amplitude.x, 0.02f));
  CHECK(near(merged.fire.z, 40.0f));
  CHECK(near(merged.move.x, 2.0f));
  CHECK(near(merged.stiffness.x, 10.0f));  // the soldier's, the weapon's is zero
  CHECK(near(merged.look[0], 1.0f));
}

// The fall: by value² × stiffness × step, at least the clamp 0.001, never past
// zero, and nothing left within 1e-4 of it.
static void testSpringFallsToRest() {
  float value = 0.5f;
  anim::applySpringFunction(10.0f, value, 0.1f);
  CHECK(near(value, 0.25f));
  value = -0.5f;
  anim::applySpringFunction(10.0f, value, 0.1f);
  CHECK(near(value, -0.25f));
  value = 0.0005f;
  anim::applySpringFunction(10.0f, value, 0.1f);
  CHECK(value == 0.0f);
  value = 0.00105f;  // 0.00105 - 0.001 is 5e-5: within 1e-4, so rest
  anim::applySpringFunction(10.0f, value, 0.1f);
  CHECK(value == 0.0f);
  value = 0.0012f;  // 2e-4 is left
  anim::applySpringFunction(10.0f, value, 0.1f);
  CHECK(near(value, 0.0002f, 1e-6f));
}

// A turn to the right and a strafe to the right, a slow walk that does nothing,
// and the limit.
static void testUpdate() {
  const anim::CameraSpringTemplate spring = m4().cameraSpring();
  anim::CameraSpring state;
  anim::CameraSpring::Input input;
  input.step = 0.01f;
  input.turn[0] = 3.0f;  // three units to the right in the last action
  state.update(spring, input);
  CHECK(near(state.state().x, -0.03f));
  CHECK(near(state.state().y, 0.0f));

  anim::CameraSpring moving;
  input = anim::CameraSpring::Input{};
  input.step = 0.01f;
  input.velocity = Vec3f{4.0f, 0.0f, 0.0f};  // right, along the movement matrix's x
  moving.update(spring, input);
  CHECK(near(moving.state().x, 4.0f * 0.01f * 2.0f));
  CHECK(near(moving.state().y, -0.3f * 4.0f * 0.01f * 2.0f));
  // The offset is the state times the amplitude, along the camera's axes.
  CHECK(near(moving.offset(spring).x, 0.08f * 0.02f));

  anim::CameraSpring slow;
  input.velocity = Vec3f{0.6f, 0.0f, 0.0f};  // 0.36 < 0.49
  slow.update(spring, input);
  CHECK(slow.state().x == 0.0f);

  anim::CameraSpring hard;
  input = anim::CameraSpring::Input{};
  input.step = 1.0f;
  input.turn[0] = -50.0f;
  hard.update(spring, input);
  CHECK(near(hard.state().x, 1.0f));

  // An amplitude under 0.001 moves nothing.
  anim::CameraSpringTemplate faint = spring;
  faint.amplitude = Vec3f{0.0005f, 0.0005f, 0.0005f};
  CHECK(hard.offset(faint).x == 0.0f);
}

TEST_MAIN({
  testScriptFillsTheFields();
  testMergeTakesWhatIsSet();
  testSpringFallsToRest();
  testUpdate();
})
