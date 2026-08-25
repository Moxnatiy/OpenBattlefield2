#include "check.h"
#include "obf2/core/math.h"

using namespace obf2;

namespace {

// Камера в початку координат дивиться вздовж -Z, як і в рушії.
Mat4 makeViewProjection(float nearZ = 1.0f, float farZ = 100.0f) {
  const Mat4 projection = perspective(1.0f, 16.0f / 9.0f, nearZ, farZ);
  const Mat4 view = lookAt(Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{0.0f, 0.0f, -1.0f}, Vec3f{0.0f, 1.0f, 0.0f});
  return projection * view;
}

}  // namespace

static void testPointInFrontIsVisible() {
  const Frustum frustum = extractFrustum(makeViewProjection());
  CHECK(frustum.intersectsSphere(Vec3f{0.0f, 0.0f, -10.0f}, 1.0f));
  CHECK(frustum.intersectsSphere(Vec3f{0.0f, 0.0f, -50.0f}, 1.0f));
}

static void testBehindCameraIsCulled() {
  const Frustum frustum = extractFrustum(makeViewProjection());
  CHECK(!frustum.intersectsSphere(Vec3f{0.0f, 0.0f, 10.0f}, 1.0f));
  CHECK(!frustum.intersectsSphere(Vec3f{0.0f, 0.0f, 50.0f}, 1.0f));
}

static void testBeyondFarPlaneIsCulled() {
  const Frustum frustum = extractFrustum(makeViewProjection(1.0f, 100.0f));
  CHECK(!frustum.intersectsSphere(Vec3f{0.0f, 0.0f, -500.0f}, 1.0f));
  // А ось велика сфера дістає до піраміди навіть здалеку.
  CHECK(frustum.intersectsSphere(Vec3f{0.0f, 0.0f, -500.0f}, 450.0f));
}

static void testFarToTheSideIsCulled() {
  const Frustum frustum = extractFrustum(makeViewProjection());
  CHECK(!frustum.intersectsSphere(Vec3f{500.0f, 0.0f, -10.0f}, 1.0f));
  CHECK(!frustum.intersectsSphere(Vec3f{0.0f, 500.0f, -10.0f}, 1.0f));
}

static void testSphereTouchingEdgeStaysVisible() {
  // Об'єкт, що лише зачіпає межу, має лишатися видимим: інакше на краю
  // екрана речі зникали б раніше, ніж виходять із кадру.
  const Frustum frustum = extractFrustum(makeViewProjection());
  const Vec3f justOutside{20.0f, 0.0f, -10.0f};
  CHECK(!frustum.intersectsSphere(justOutside, 0.5f));
  CHECK(frustum.intersectsSphere(justOutside, 30.0f));
}

static void testPlanesAreNormalized() {
  // Порівняння з радіусом має сенс лише коли нормалі одиничні.
  const Frustum frustum = extractFrustum(makeViewProjection());
  for (const Plane& plane : frustum.planes) {
    const float len = length(plane.normal);
    CHECK(len > 0.99f && len < 1.01f);
  }
}

TEST_MAIN({
  testPointInFrontIsVisible();
  testBehindCameraIsCulled();
  testBeyondFarPlaneIsCulled();
  testFarToTheSideIsCulled();
  testSphereTouchingEdgeStaysVisible();
  testPlanesAreNormalized();
})
