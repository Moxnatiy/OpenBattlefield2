// The player's own arms and weapon (`obf2/app/first_person.h`): where the camera
// looks, where the first-person skeleton stands against it, the projection, and
// the weapon's parts on the first-person skeleton's bones.
#include <cmath>
#include <vector>

#include "check.h"
#include "obf2/app/camera.h"
#include "obf2/app/first_person.h"
#include "obf2/mesh/skinning.h"

using namespace obf2;

namespace {

bool near(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) < tolerance; }
bool nearVec(const Vec3f& a, const Vec3f& b) {
  return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

}  // namespace

// The camera's +Z is the direction the view is built with, and its origin the eye.
static void testCameraLooksWhereTheViewDoes() {
  const Vec3f eye{10.0f, 150.0f, -20.0f};
  for (const float yaw : {0.0f, 37.0f, -120.0f, 180.0f}) {
    for (const float pitch : {0.0f, 25.0f, -60.0f}) {
      const Mat4 camera = app::firstPersonCamera(eye, yaw, pitch);
      CHECK(nearVec(transformDirection(camera, Vec3f{0.0f, 0.0f, 1.0f}),
                    app::lookDirection(yaw, pitch)));
      CHECK(nearVec(transformPoint(camera, Vec3f{}), eye));
    }
  }
}

// `Camerabone` of `1p_setup.ske` (`ske_info`): the root, unrotated, at
// -0.024 1.504 -0.111. Through the root the engine builds, it lands on the eye
// and the rest of the skeleton keeps its offset from it, turned with the look.
static void testCameraboneStandsAtTheEye() {
  mesh::Skeleton skeleton;
  mesh::SkeletonBone camerabone;
  camerabone.name = "Camerabone";
  camerabone.position = mesh::Vec3{-0.024f, 1.504f, -0.111f};
  skeleton.bones.push_back(camerabone);
  mesh::SkeletonBone torus;
  torus.name = "torus";
  torus.position = mesh::Vec3{0.0f, 1.356f, -0.073f};
  skeleton.bones.push_back(torus);

  const Vec3f eye{5.0f, 160.0f, 7.0f};
  const Mat4 camera = app::firstPersonCamera(eye, 90.0f, 0.0f);
  const Mat4 root = app::firstPersonRoot(skeleton, camera);
  CHECK(nearVec(transformPoint(root, Vec3f{-0.024f, 1.504f, -0.111f}), eye));
  // `torus` at rest is 0.148 below and 0.038 forward of `Camerabone`, 0.024
  // to its right; at a yaw of 90° forward is +X and right is -Z.
  const Vec3f torusAt = transformPoint(root, Vec3f{0.0f, 1.356f, -0.073f});
  CHECK(nearVec(torusAt, Vec3f{eye.x + 0.038f, eye.y - 0.148f, eye.z - 0.024f}));
}

// 0.82 radians of vertical field of view: a point on the edge of that angle
// lands on the top of the screen, and the near and far planes at 0 and 1.
static void testProjection() {
  const Mat4 projection = app::firstPersonProjection(4.0f / 3.0f);
  const auto clip = [&](const Vec3f& p) {
    const float x = projection.m[0] * p.x + projection.m[4] * p.y + projection.m[8] * p.z + projection.m[12];
    const float y = projection.m[1] * p.x + projection.m[5] * p.y + projection.m[9] * p.z + projection.m[13];
    const float z = projection.m[2] * p.x + projection.m[6] * p.y + projection.m[10] * p.z + projection.m[14];
    const float w = projection.m[3] * p.x + projection.m[7] * p.y + projection.m[11] * p.z + projection.m[15];
    return Vec3f{x / w, y / w, z / w};
  };
  const float edge = std::tan(app::kFirstPersonFov * 0.5f);
  CHECK(near(clip(Vec3f{0.0f, edge, 1.0f}).y, 1.0f));
  CHECK(near(clip(Vec3f{0.0f, 0.0f, app::kFirstPersonNear}).z, 0.0f));
  CHECK(near(clip(Vec3f{0.0f, 0.0f, app::kFirstPersonFar}).z, 1.0f));
}

// The M4's five parts on `mesh1`..`mesh5` of the first-person skeleton, 54..58,
// and a seventeenth part on the sixteenth bone, the last the cap allows.
static void testWeaponPartsOnTheFirstPersonBones() {
  mesh::RenderMesh weapon;
  weapon.vertices.resize(5);
  weapon.indices = {0, 1, 2, 3, 4};
  weapon.vertexPart = {0, 1, 2, 3, 4};
  mesh::DrawRange range;
  range.indexCount = 5;
  weapon.ranges.push_back(range);
  mesh::bindPartsToBones(weapon, mesh::kFirstWeaponBone1p, mesh::kMaxWeaponParts1p);
  CHECK_EQ(weapon.rigs.size(), std::size_t(1));
  if (weapon.rigs.empty()) return;
  CHECK_EQ(weapon.rigs[0].bones.size(), std::size_t(5));
  CHECK_EQ(weapon.rigs[0].bones[0].id, std::uint32_t(54));
  CHECK_EQ(weapon.rigs[0].bones[4].id, std::uint32_t(58));

  mesh::RenderMesh many;
  many.vertices.resize(17);
  many.indices.resize(17);
  many.vertexPart.resize(17);
  for (std::size_t i = 0; i < 17; ++i) {
    many.indices[i] = static_cast<std::uint32_t>(i);
    many.vertexPart[i] = static_cast<std::uint8_t>(i);
  }
  mesh::DrawRange whole;
  whole.indexCount = 17;
  many.ranges.push_back(whole);
  mesh::bindPartsToBones(many, mesh::kFirstWeaponBone1p, mesh::kMaxWeaponParts1p);
  CHECK_EQ(many.rigs[0].bones.size(), mesh::kMaxWeaponParts1p);
  CHECK_EQ(many.rigs[0].bones.back().id, std::uint32_t(54 + 15));
  CHECK_EQ(many.skin[16].boneA, std::uint8_t(15));
}

// Standing: the pivot a metre over the feet, then 0.47 up and 0.1 ahead along the
// body — at a body yaw of 90°, ahead is +X.
static void testEyeStandsOnThePoseCameraPoint() {
  const Vec3f feet{10.0f, 150.0f, -5.0f};
  const Vec3f stand{0.0f, 0.47f, 0.1f};
  CHECK(nearVec(app::soldierEye(feet, 1.0f, 0.0f, stand), Vec3f{10.0f, 151.47f, -4.9f}));
  CHECK(nearVec(app::soldierEye(feet, 1.0f, 90.0f, stand), Vec3f{10.1f, 151.47f, -5.0f}));
}

TEST_MAIN({
  testEyeStandsOnThePoseCameraPoint();
  testCameraLooksWhereTheViewDoes();
  testCameraboneStandsAtTheEye();
  testProjection();
  testWeaponPartsOnTheFirstPersonBones();
})
