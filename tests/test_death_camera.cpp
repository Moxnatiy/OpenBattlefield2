// The death camera (obf2/app/death_camera.h): `FreeCamera::getTransformationDeath`
// backing away from the body to its goal distance, behind the body's facing,
// raised, above the ground, and turning away from the killer.
#include <cmath>

#include "check.h"
#include "obf2/app/death_camera.h"

using obf2::Vec3f;
using obf2::app::DeathCamera;

namespace {

using obf2::length;

// The first frame: half a metre behind, then out to max(3 × radius, 3) behind the
// facing and above the body.
void testBacksAway() {
  DeathCamera camera;
  const Vec3f body{10.0f, 0.0f, 20.0f};
  camera.start(body, Vec3f{0.0f, 0.0f, 1.0f}, 0.5f);  // 0.5 × 3 < 3: the floor
  const auto flat = [](float, float) { return -100.0f; };
  auto view = camera.update(0.0f, body, std::nullopt, flat);
  CHECK(std::fabs(camera.distance() - 0.5f) < 1e-5f);
  CHECK(std::fabs(view.eye.z - (20.0f - 0.05f)) < 1e-4f);  // offset −forward × 0.1, × 0.5
  for (int i = 0; i < 600; ++i) view = camera.update(1.0f / 60.0f, body, std::nullopt, flat);
  CHECK(std::fabs(camera.distance() - 3.0f) < 1e-3f);
  CHECK(std::fabs(length(view.eye - body) - 3.0f) < 1e-2f);
  CHECK(view.eye.z < body.z);  // behind its facing
  CHECK(view.eye.y > body.y);  // raised by the 0.3
  CHECK(std::fabs(view.target.x - body.x) < 1e-5f);
}

// Never under the ground plus 0.8.
void testAboveGround() {
  DeathCamera camera;
  const Vec3f body{0.0f, 0.0f, 0.0f};
  camera.start(body, Vec3f{0.0f, 0.0f, 1.0f}, 1.0f);
  const auto high = [](float, float) { return 5.0f; };
  const auto view = camera.update(0.1f, body, std::nullopt, high);
  CHECK(view.eye.y >= 5.8f - 1e-5f);
}

// Out past 0.8 of its distance, the camera goes to the side away from the killer.
void testTurnsFromKiller() {
  DeathCamera camera;
  const Vec3f body{0.0f, 0.0f, 0.0f};
  camera.start(body, Vec3f{0.0f, 0.0f, 1.0f}, 1.0f);
  const auto flat = [](float, float) { return -100.0f; };
  const Vec3f killer{50.0f, 0.0f, 0.0f};
  DeathCamera::View view{};
  for (int i = 0; i < 1200; ++i) view = camera.update(1.0f / 60.0f, body, killer, flat);
  CHECK(view.eye.x < -2.0f);  // the killer is at +x; the camera stands at −x
}

}  // namespace

TEST_MAIN({
  testBacksAway();
  testAboveGround();
  testTurnsFromKiller();
})
