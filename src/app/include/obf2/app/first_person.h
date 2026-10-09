#pragma once
// The player's own arms and weapon, seen from his eyes.
//
// The engine draws them as a model of their own, not as a picture: the soldier's
// first-person sub-geometry (the bare arms, `1p_*`) and the weapon's, posed on a
// separate skeleton (`1p_setup.ske`) by the weapon's `animationSystem1P`, and
// placed so that the skeleton's first bone, `Camerabone`, stands where the camera
// does. The renderer then draws them through a camera of their own, with a
// narrower field of view and a near plane of a centimetre.
//
// The engine's: `Soldier::updateFirstPersonAnimations` (Linux server 0x552b30)
// and, in the client, the soldier's animation update (`BF2.exe` 0x61f630). Notes:
// docs/functions/soldier-model.md, "Stage 4 — first person".
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "obf2/anim/camera_spring.h"
#include "obf2/anim/player.h"
#include "obf2/anim/system.h"
#include "obf2/app/render_context.h"
#include "obf2/core/math.h"
#include "obf2/game/object_template.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/mesh/animation.h"
#include "obf2/mesh/skeleton.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

// The first-person camera's field of view, **vertical**, in radians. The
// renderer's constructor gives its first-person camera 0.82 (`RendDX9.dll`
// 0x1002d277, `0x3f51eb85`, through the camera's `setFov`, vtable +0x1c), and the
// game hands the soldier's and the weapon's geometry the same number in first
// person, from the variable `soldier-fov1p` (`BF2.exe` 0x85b030, default
// 0x3f51eb85; read at 0x61f68e and 0x61f884). No file of the game's data sets it.
// Vertical because the camera builds its projection with
// `D3DXMatrixPerspectiveFovLH(fov, 1 / its +0x30, near, far)` (0x10079e10), and
// that function's angle is the vertical one.
inline constexpr float kFirstPersonFov = 0.82f;

// Its near plane, 0.01 (`RendDX9.dll` 0x1002dd20, `renderer.tweakNearPlane1p`),
// and the far end of its projection: the camera's far plane is +0x598 − +0x594 =
// 15 − 0.01 (0x1002c333), and the projection is built with far + near
// (0x101c2f30: `+0x2c + +0x28`), so 15.0.
inline constexpr float kFirstPersonNear = 0.01f;
inline constexpr float kFirstPersonFar = 15.0f;

// The camera's own frame in the world: at the eye, turned by the look. `+Z` goes
// to `lookDirection(yaw, pitch)` (camera.h), which is the direction the view is
// built with.
Mat4 firstPersonCamera(const Vec3f& eye, float yawDegrees, float pitchDegrees);

// Where the first-person skeleton stands: the camera times the inverse of the
// skeleton's first bone at rest. `SoldierTemplate::skeleton1P` (Linux 0x5527a0)
// keeps the skeleton's first matrix inverted in the template's +0x2e0
// (`Mat4::setInverse`, 0x55293f), and the update multiplies it by the camera's
// absolute transformation (`Mat4::mult`, 0x55313d) before `Skeleton::transform`
// (0x5532d8). The first bone is `Camerabone`, the root, so its matrix at rest is
// its own, with no parent's to add.
//
// `sway` is the camera spring's offset along the camera's own axes (right, up,
// forward): the update adds it to the matrix's translation turned by the
// camera's rotation (0x553207..0x5532c5), which is a move in the camera's frame.
Mat4 firstPersonRoot(const mesh::Skeleton& skeleton, const Mat4& camera,
                     const Vec3f& sway = Vec3f{});

// The projection the arms and weapon are drawn with. `aspect` is width over
// height, as `perspective` takes it.
Mat4 firstPersonProjection(float aspect);

class FirstPersonView {
 public:
  void init(FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer,
            TextureResolver resolve);

  // The soldier, his kit and the item he has out. A change builds the model
  // again; the same three keep it. False when nothing can be drawn — no weapon
  // out, or a template the registry does not know.
  bool setWeapon(const std::string& soldier, const std::string& kit, int item);

  // One frame of the two animation systems and of the camera spring. The spring
  // is the soldier's system's, with the weapon's merged over it (0x552c1e..
  // 0x553564); `spring.step` is the frame's.
  void update(const anim::State& state, float step, const anim::CameraSpring::Input& spring);

  // The arms and weapon at `camera` (`firstPersonCamera`), for the first-person
  // pass. Nothing when `setWeapon` has nothing to draw.
  void collect(std::vector<gfx::MeshRenderer::DrawItem>& out, const Mat4& camera);

  void release(gfx::MeshRenderer& renderer);

  // What the last `collect` put on screen, for the log.
  const std::string& key() const { return key_; }
  bool ready() const { return model_ != nullptr && model_->ready; }

 private:
  struct Model {
    gfx::GpuMesh gpu;
    mesh::Skeleton skeleton;
    std::optional<anim::System> body;
    std::optional<anim::System> weapon;
    bool ready = false;
  };

  const mesh::BoneAnimation* clipAt(const std::string& path);

  FileSystem* files_ = nullptr;
  const game::Registry* registry_ = nullptr;
  gfx::MeshRenderer* renderer_ = nullptr;
  TextureResolver resolve_;

  std::map<std::string, Model> models_;
  std::map<std::string, std::optional<mesh::BoneAnimation>> clips_;
  Model* model_ = nullptr;
  std::string key_;
  anim::Player body_;
  anim::Player weapon_;
  std::vector<mesh::Mat4> pose_;
  anim::CameraSpringTemplate springTemplate_;
  anim::CameraSpring spring_;
};

}  // namespace obf2::app
