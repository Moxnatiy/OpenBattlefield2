#include "obf2/app/camera.h"

#include <cmath>

namespace obf2::app {
namespace {

constexpr float kToRadians = 3.14159265358979323846f / 180.0f;

}  // namespace

Vec3f lookDirection(float yawDegrees, float pitchDegrees) {
  const float yaw = yawDegrees * kToRadians;
  const float pitch = pitchDegrees * kToRadians;
  return Vec3f{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

CameraView chooseCamera(const CameraInputs& in) {
  CameraView view;
  view.target = in.sceneCentre;

  if (in.fixed) {
    view.eye = *in.fixed;
    view.target = view.eye + lookDirection(in.fixedYaw, in.fixedPitch);
    return view;
  }
  if (in.soldierEye) {
    view.eye = *in.soldierEye;
    view.target = view.eye + lookDirection(in.soldierYaw, in.soldierPitch);
    return view;
  }
  if (in.topDown) {
    view.eye = Vec3f{in.sceneCentre.x, in.sceneCentre.y + in.distance, in.sceneCentre.z};
    return view;
  }
  if (in.beforeSpawnPosition) {
    // Until the player has spawned the camera stands where the level said:
    //
    //   gameLogic.setBeforeSpawnCamera -50/185/-285 -16/-3/0
    //
    // The first triple is the position, the second the rotation in degrees.
    view.eye = *in.beforeSpawnPosition;
    view.target =
        view.eye + lookDirection(in.beforeSpawnRotation.x, in.beforeSpawnRotation.y);
    return view;
  }
  // The model viewer: a slow orbit around the scene, a turn every ten seconds at
  // sixty frames a second.
  const float angle = static_cast<float>(in.frame) / 60.0f * 0.6f;
  view.eye = Vec3f{in.sceneCentre.x + std::sin(angle) * in.distance,
                   in.sceneCentre.y + in.eyeHeight,
                   in.sceneCentre.z + std::cos(angle) * in.distance};
  return view;
}

}  // namespace obf2::app
