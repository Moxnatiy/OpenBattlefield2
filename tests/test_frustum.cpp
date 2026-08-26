#include "check.h"
#include "obf2/core/math.h"

using namespace obf2;

namespace {

// Камера в початку координат дивиться вздовж +Z: система координат ліва,
// як у рушія гри (RendDX9.dll бере D3DXMatrixLookAtLH/PerspectiveFovLH).
Mat4 makeViewProjection(float nearZ = 1.0f, float farZ = 100.0f) {
  const Mat4 projection = perspective(1.0f, 16.0f / 9.0f, nearZ, farZ);
  const Mat4 view = lookAt(Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{0.0f, 0.0f, 1.0f}, Vec3f{0.0f, 1.0f, 0.0f});
  return projection * view;
}

// Точка світу в кліп-просторі: сюди дивимось, щоб перевірити, з якого боку
// екрана вона опиниться.
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
  // А ось велика сфера дістає до піраміди навіть здалеку.
  CHECK(frustum.intersectsSphere(Vec3f{0.0f, 0.0f, 500.0f}, 450.0f));
}

static void testFarToTheSideIsCulled() {
  const Frustum frustum = extractFrustum(makeViewProjection());
  CHECK(!frustum.intersectsSphere(Vec3f{500.0f, 0.0f, 10.0f}, 1.0f));
  CHECK(!frustum.intersectsSphere(Vec3f{0.0f, 500.0f, 10.0f}, 1.0f));
}

static void testSphereTouchingEdgeStaysVisible() {
  // Об'єкт, що лише зачіпає межу, має лишатися видимим: інакше на краю
  // екрана речі зникали б раніше, ніж виходять із кадру.
  const Frustum frustum = extractFrustum(makeViewProjection());
  const Vec3f justOutside{20.0f, 0.0f, 10.0f};
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

static void testWorldIsNotMirrored() {
  // Найважливіше в лівій системі: що опиняється праворуч на екрані.
  // Камера в нулі дивиться на +Z, «вгору» — +Y. Тоді точка з додатним X
  // має бути в правій половині кадру. Якби конвеєр лишався правостороннім,
  // вона з'їхала б ліворуч — і весь світ виглядав би дзеркальним.
  const Mat4 viewProjection = makeViewProjection();
  const Clip right = toClip(viewProjection, Vec3f{1.0f, 0.0f, 5.0f});
  CHECK(right.w > 0.0f);   // попереду камери
  CHECK(right.x > 0.0f);   // праворуч на екрані

  const Clip left = toClip(viewProjection, Vec3f{-1.0f, 0.0f, 5.0f});
  CHECK(left.x < 0.0f);

  // І вгору теж має лишатися вгорою.
  const Clip above = toClip(viewProjection, Vec3f{0.0f, 1.0f, 5.0f});
  CHECK(above.y > 0.0f);
}

static void testDepthGrowsForward() {
  // Глибина в діапазоні [0,1]: ближче до камери — менше.
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
