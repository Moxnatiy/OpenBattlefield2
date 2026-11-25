#include "obf2/app/third_person_look.h"

#include <algorithm>
#include <cmath>

#include "obf2/core/math.h"

namespace obf2::app {
namespace {

// `setRotateXDeg` (0x731a10) and `setRotateYDeg` (0x731ae0) write the same
// sixteen floats as core's `rotationX` / `rotationY`.
mesh::Mat4 toMesh(const Mat4& m) {
  mesh::Mat4 out;
  for (int i = 0; i < 16; ++i) out.m[i] = m.m[i];
  return out;
}

}  // namespace

std::vector<mesh::BonePost> soldierLookPosts(float pitch, float aimYaw, std::uint32_t pose) {
  constexpr float kRadians = 3.14159265358979323846f / 180.0f;
  // Swimming looks straight ahead (pose 3 zeroes the pitch, 0x55515d).
  if (pose == 3) pitch = 0.0f;
  // Prone has its own limits (pose 2, 0x556189), the rest the standing ones
  // (0x5552e2).
  if (pose == 2) pitch = std::clamp(pitch, kSoldierProneLookUp, kSoldierProneLookDown);
  else pitch = std::clamp(pitch, kSoldierLookUp, kSoldierLookDown);
  // A quarter of the pitch to each of four bones, a third of the yaw to each of
  // three (0x55531d, 0x555325). The engine's yaw is `asin` of the camera's
  // forward on the body's side axis, the aim's own yaw while it is inside ±90.
  const float quarter = pitch * 0.25f;
  const float third = std::asin(std::sin(aimYaw * kRadians)) / kRadians / 3.0f;
  // `rotMult(post, rotX, rotY)` in the engine's order is rotY * rotX in ours.
  const Mat4 spine = rotationY(third * kRadians) * rotationX(quarter * kRadians);
  const Mat4 head = rotationX(quarter * kRadians);  // yaw: the idle look, not ported
  // The back gear part is turned back by the quarter (0x5558fa..0x555901, `rotX(-pitch)`).
  const Mat4 gear = rotationX(-quarter * kRadians);
  return {
      {kLookSpine2, toMesh(spine)}, {kLookSpine3, toMesh(spine)}, {kLookTorso, toMesh(spine)},
      {kLookHead, toMesh(head)},    {kLookMesh15, toMesh(gear)},
  };
}

}  // namespace obf2::app
