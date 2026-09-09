#include "check.h"
#include "obf2/core/math.h"

using namespace obf2;

namespace {

// The camera at the origin looks along +Z: the coordinate system is left-handed,
// as in the game's engine (RendDX9.dll uses D3DXMatrixLookAtLH/PerspectiveFovLH).
Mat4 makeViewProjection(float nearZ = 1.0f, float farZ = 100.0f) {
  const Mat4 projection = perspective(1.0f, 16.0f / 9.0f, nearZ, farZ);
  const Mat4 view = lookAt(Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{0.0f, 0.0f, 1.0f}, Vec3f{0.0f, 1.0f, 0.0f});
  return projection * view;
}

// A world point in clip space: we look here to check which side of the screen it
// will end up on.
struct Clip { float x, y, z, w; };
Clip toClip(const Mat4& m, Vec3f p) {
  return Clip{m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
              m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14],
              m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15]};
}

}  // namespace

static void testPointInFrontIsVisible() {
  const Frustum frustum = extractFrustum(makeViewProjection());
  CHECK(frustum.intersectsSphere(Vec3f{0.0f, 0.0f, 10.0f}, 1.0f));
  CHECK(frustum.intersectsSphere(Vec3f{0.0f, 0.0f, 50.0f}, 1.0f));
}

static void testBehindCameraIsCulled() {
  const Frustum frustum = extractFrustum(makeViewProjection());
  CHECK(!frustum.intersectsSphere(Vec3f{0.0f, 0.0f, -10.0f}, 1.0f));
  CHECK(!frustum.intersectsSphere(Vec3f{0.0f, 0.0f, -50.0f}, 1.0f));
}

static void testBeyondFarPlaneIsCulled() {
  const Frustum frustum = extractFrustum(makeViewProjection(1.0f, 100.0f));
  CHECK(!frustum.intersectsSphere(Vec3f{0.0f, 0.0f, 500.0f}, 1.0f));
  // A large sphere, on the other hand, reaches the frustum even from afar.
  CHECK(frustum.intersectsSphere(Vec3f{0.0f, 0.0f, 500.0f}, 450.0f));
}

static void testFarToTheSideIsCulled() {
  const Frustum frustum = extractFrustum(makeViewProjection());
  CHECK(!frustum.intersectsSphere(Vec3f{500.0f, 0.0f, 10.0f}, 1.0f));
  CHECK(!frustum.intersectsSphere(Vec3f{0.0f, 500.0f, 10.0f}, 1.0f));
}

static void testSphereTouchingEdgeStaysVisible() {
  // An object that merely touches the boundary has to stay visible: otherwise things
  // at the screen's edge would disappear before leaving the frame.
  const Frustum frustum = extractFrustum(makeViewProjection());
  const Vec3f justOutside{20.0f, 0.0f, 10.0f};
  CHECK(!frustum.intersectsSphere(justOutside, 0.5f));
  CHECK(frustum.intersectsSphere(justOutside, 30.0f));
}

static void testPlanesAreNormalized() {
  // Comparing against the radius only makes sense while the normals are unit ones.
  const Frustum frustum = extractFrustum(makeViewProjection());
  for (const Plane& plane : frustum.planes) {
    const float len = length(plane.normal);
    CHECK(len > 0.99f && len < 1.01f);
  }
}

static void testWorldIsNotMirrored() {
  // The most important thing in a left-handed system: what ends up on the right of
  // the screen. The camera at zero looks at +Z, "up" is +Y. A point with a positive
  // X then has to be in the frame's right half. Were the pipeline still
  // right-handed, it would slide to the left — and the whole world would look mirrored.
  const Mat4 viewProjection = makeViewProjection();
  const Clip right = toClip(viewProjection, Vec3f{1.0f, 0.0f, 5.0f});
  CHECK(right.w > 0.0f);   // in front of the camera
  CHECK(right.x > 0.0f);   // to the right on screen

  const Clip left = toClip(viewProjection, Vec3f{-1.0f, 0.0f, 5.0f});
  CHECK(left.x < 0.0f);

  // And up has to stay up too.
  const Clip above = toClip(viewProjection, Vec3f{0.0f, 1.0f, 5.0f});
  CHECK(above.y > 0.0f);
}

static void testDepthGrowsForward() {
  // The depth is in the range [0,1]: closer to the camera means smaller.
  const Mat4 viewProjection = makeViewProjection(1.0f, 100.0f);
  const Clip near = toClip(viewProjection, Vec3f{0.0f, 0.0f, 2.0f});
  const Clip far = toClip(viewProjection, Vec3f{0.0f, 0.0f, 50.0f});
  CHECK(near.w > 0.0f && far.w > 0.0f);
  CHECK(near.z / near.w < far.z / far.w);
  CHECK(near.z / near.w >= 0.0f);
  CHECK(far.z / far.w <= 1.0f);
}

TEST_MAIN({
  testWorldIsNotMirrored();
  testDepthGrowsForward();
  testPointInFrontIsVisible();
  testBehindCameraIsCulled();
  testBeyondFarPlaneIsCulled();
  testFarToTheSideIsCulled();
  testSphereTouchingEdgeStaysVisible();
  testPlanesAreNormalized();
})
