#pragma once
// Where the frame is seen from.
//
// The engine has one camera and several things that may own it: the player's
// soldier, the level's before-spawn camera (`gameLogic.setBeforeSpawnCamera` in
// `Levels/<level>/Init.con`) and the tools. Ours has the same owners plus the
// ones the measurements need — a camera put exactly where the original's is, and
// a view from straight above for comparing the world against the level's own
// minimap.
#include <optional>

#include "obf2/core/math.h"

namespace obf2::app {

// What the camera is looking at this frame.
struct CameraView {
  Vec3f eye;
  Vec3f target;
};

// A direction from yaw and pitch in degrees. A zero angle looks along +Z — the
// same as the server computes (docs/functions/soldier-physics.md).
Vec3f lookDirection(float yawDegrees, float pitchDegrees);

// The eye and its target for one frame, in the order the owners take precedence:
//
//   1. `--camera x/y/z --angles yaw pitch` — stand exactly here and look exactly
//      there, whatever the rest of the run is doing. It outranks the soldier on
//      purpose: the point of it is to put our frame and the original's in the
//      same spot, and the original is put there with the same numbers;
//   2. the player's soldier, in first person;
//   3. `--topdown` — straight down over the scene's centre;
//   4. the level's before-spawn camera;
//   5. an orbit around the scene, for the model viewer.
struct CameraInputs {
  std::optional<Vec3f> fixed;  // --camera
  float fixedYaw = 0.0f;
  float fixedPitch = 0.0f;
  std::optional<Vec3f> soldierEye;  // the eye of the player's soldier, if he exists
  float soldierYaw = 0.0f;
  float soldierPitch = 0.0f;
  bool topDown = false;
  std::optional<Vec3f> beforeSpawnPosition;
  Vec3f beforeSpawnRotation;  // yaw/pitch/roll in degrees
  Vec3f sceneCentre;
  float distance = 1.0f;
  float eyeHeight = 0.0f;
  int frame = 0;
};

CameraView chooseCamera(const CameraInputs& inputs);

}  // namespace obf2::app
