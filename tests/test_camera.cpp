#include <cmath>
#include <vector>

#include "check.h"
#include "obf2/app/camera.h"
#include "obf2/app/scripted_input.h"

using namespace obf2;

namespace {

bool near(float a, float b) { return std::abs(a - b) < 1e-4f; }
bool nearVec(const Vec3f& a, const Vec3f& b) {
  return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

// A zero angle looks along +Z, and the angle grows clockwise — the same as the
// server computes (docs/functions/soldier-physics.md).
static void testLookDirection() {
  CHECK(nearVec(app::lookDirection(0.0f, 0.0f), Vec3f{0.0f, 0.0f, 1.0f}));
  CHECK(nearVec(app::lookDirection(90.0f, 0.0f), Vec3f{1.0f, 0.0f, 0.0f}));
  CHECK(nearVec(app::lookDirection(180.0f, 0.0f), Vec3f{0.0f, 0.0f, -1.0f}));
  CHECK(nearVec(app::lookDirection(0.0f, 90.0f), Vec3f{0.0f, 1.0f, 0.0f}));
  // Pitch shortens the horizontal part rather than adding to it.
  const Vec3f down = app::lookDirection(0.0f, -45.0f);
  CHECK(near(down.y, -std::sqrt(0.5f)));
  CHECK(near(down.z, std::sqrt(0.5f)));
}

// `--camera` outranks everything, including the player's own soldier: the point
// of it is to put our frame and the original's in the same spot.
static void testFixedCameraWins() {
  app::CameraInputs in;
  in.fixed = Vec3f{-135.4f, 165.0f, -133.0f};
  in.fixedYaw = 90.0f;
  in.soldierEye = Vec3f{10.0f, 2.0f, 10.0f};
  in.topDown = true;
  in.beforeSpawnPosition = Vec3f{1.0f, 1.0f, 1.0f};
  const app::CameraView view = app::chooseCamera(in);
  CHECK(nearVec(view.eye, *in.fixed));
  CHECK(nearVec(view.target, *in.fixed + Vec3f{1.0f, 0.0f, 0.0f}));
}

// With a soldier the camera is his, and it outranks the level's before-spawn one.
static void testSoldierBeatsTheLevelCamera() {
  app::CameraInputs in;
  in.soldierEye = Vec3f{5.0f, 3.0f, -2.0f};
  in.soldierYaw = 180.0f;
  in.beforeSpawnPosition = Vec3f{1.0f, 1.0f, 1.0f};
  in.topDown = true;
  const app::CameraView view = app::chooseCamera(in);
  CHECK(nearVec(view.eye, *in.soldierEye));
  CHECK(nearVec(view.target, *in.soldierEye + Vec3f{0.0f, 0.0f, -1.0f}));
}

// From above the eye stands over the scene's centre, the whole distance up.
static void testTopDown() {
  app::CameraInputs in;
  in.topDown = true;
  in.sceneCentre = Vec3f{3.0f, 146.0f, -7.0f};
  in.distance = 500.0f;
  const app::CameraView view = app::chooseCamera(in);
  CHECK(nearVec(view.eye, Vec3f{3.0f, 646.0f, -7.0f}));
  // The target stays the centre: the view looks straight down.
  CHECK(nearVec(view.target, in.sceneCentre));
}

// The level's own camera: `gameLogic.setBeforeSpawnCamera -50/185/-285 -16/-3/0`.
static void testBeforeSpawnCamera() {
  app::CameraInputs in;
  in.beforeSpawnPosition = Vec3f{-50.0f, 185.0f, -285.0f};
  in.beforeSpawnRotation = Vec3f{-16.0f, -3.0f, 0.0f};
  const app::CameraView view = app::chooseCamera(in);
  CHECK(nearVec(view.eye, *in.beforeSpawnPosition));
  CHECK(nearVec(view.target, *in.beforeSpawnPosition + app::lookDirection(-16.0f, -3.0f)));
}

// The model viewer orbits: frame 0 stands south of the centre, and the eye rises
// by the height it was given.
static void testOrbit() {
  app::CameraInputs in;
  in.sceneCentre = Vec3f{0.0f, 0.0f, 0.0f};
  in.distance = 10.0f;
  in.eyeHeight = 3.0f;
  in.frame = 0;
  const app::CameraView view = app::chooseCamera(in);
  CHECK(nearVec(view.eye, Vec3f{0.0f, 3.0f, 10.0f}));
  CHECK(nearVec(view.target, in.sceneCentre));

  in.frame = 60;  // a second at sixty frames: 0.6 radians round
  const app::CameraView later = app::chooseCamera(in);
  CHECK(near(later.eye.x, std::sin(0.6f) * 10.0f));
  CHECK(near(later.eye.z, std::cos(0.6f) * 10.0f));
}

// The scripted input: a range covers `frame` to `frame + frames - 1`, the mouse
// deltas of overlapping ranges add up, and a jump is one frame only.
static void testScriptedInput() {
  app::Args args;
  args.looks.push_back(app::Args::ScheduledLook{10, 5, 3.0f, -1.0f, 0});
  args.looks.push_back(app::Args::ScheduledLook{12, 5, 1.0f, 0.5f, 0});
  args.moves.push_back(app::Args::ScheduledLook{10, 10, 1.0f, 0.0f, 1});
  args.jumps.push_back(14);

  const app::ScriptedFrame before = app::scriptedInput(args, 9);
  CHECK(near(before.mouseDeltaX, 0.0f));
  CHECK(!before.hasMove);
  CHECK(!before.jump);

  const app::ScriptedFrame first = app::scriptedInput(args, 10);
  CHECK(near(first.mouseDeltaX, 3.0f));
  CHECK(near(first.mouseDeltaY, -1.0f));
  CHECK(first.hasMove);
  CHECK(near(first.moveForward, 1.0f));
  CHECK(first.sprint);

  // Both look ranges speak for frame 12, so the deltas add.
  const app::ScriptedFrame both = app::scriptedInput(args, 12);
  CHECK(near(both.mouseDeltaX, 4.0f));
  CHECK(near(both.mouseDeltaY, -0.5f));

  const app::ScriptedFrame jumping = app::scriptedInput(args, 14);
  CHECK(jumping.jump);
  CHECK(!app::scriptedInput(args, 15).jump);

  // The look ranges are over by frame 17, the movement is not.
  const app::ScriptedFrame tail = app::scriptedInput(args, 17);
  CHECK(near(tail.mouseDeltaX, 0.0f));
  CHECK(tail.hasMove);
  // And past the movement's own range nothing is left.
  CHECK(!app::scriptedInput(args, 20).hasMove);
}

}  // namespace

TEST_MAIN({
  testLookDirection();
  testFixedCameraWins();
  testSoldierBeatsTheLevelCamera();
  testTopDown();
  testBeforeSpawnCamera();
  testOrbit();
  testScriptedInput();
})
