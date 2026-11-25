#include <algorithm>
#include <cmath>

#include "check.h"
#include "obf2/app/hit_direction.h"
#include "obf2/net/bf2_events.h"

using namespace obf2;

namespace {

bool near(float a, float b, float tolerance = 1e-3f) { return std::fabs(a - b) <= tolerance; }

// The server's packing (`getStateSync`, Linux 0x4a17ad), to round-trip against.
std::uint32_t pack(float angle) {
  const float unit = std::clamp((angle / 6.2831853f + 1.0f) * 0.5f, 0.0f, 1.0f);
  return static_cast<std::uint32_t>(unit * 255.0f);
}

}  // namespace

int main() {
  constexpr float kPi = 3.14159265f;

  // The byte: 0..255 over −2π..2π, a dead zone round the middle (0x5018a0).
  CHECK(near(net::bf2::decodeHitFromDirection(0), -2 * kPi));
  CHECK(near(net::bf2::decodeHitFromDirection(255), 2 * kPi));
  CHECK(net::bf2::decodeHitFromDirection(127) == 0.0f);  // |s| = 1/255 ≤ 1/127.5
  CHECK(net::bf2::decodeHitFromDirection(128) == 0.0f);
  CHECK(net::bf2::decodeHitFromDirection(129) != 0.0f);
  for (const float angle : {0.9f, 2.0f, 4.5f, 6.0f}) {
    CHECK(near(net::bf2::decodeHitFromDirection(pack(angle)), angle, 2 * 6.2832f / 255.0f));
  }

  // Facing +Z (yaw 0): a hit from yaw a puts the indicator at −a.
  app::HitDirection hit;
  const Vec3f north{0, 0, 1};
  CHECK(!hit.show());
  hit.cue(north, kPi / 2);
  CHECK(hit.show() && near(hit.angle(), -kPi / 2));

  // It follows the camera while shown (0x464aa0 recomputes it every frame).
  const Vec3f east{1, 0, 0};  // atan2(1, 0) = π/2
  hit.update(0.06f, east, kPi / 2);  // 0.04 left
  CHECK(hit.show() && near(hit.angle(), 0.0f));

  // Shown for 0.1 s (+0x1a4): the frame that takes the time below 0 still shows,
  // the next one hides it — after taking the angle once more.
  hit.update(0.06f, east, kPi / 2);  // −0.02 left
  CHECK(hit.show());
  hit.update(0.016f, north, kPi / 2);
  CHECK(!hit.show());
  CHECK(near(hit.angle(), -kPi / 2));
  hit.update(0.016f, east, kPi / 2);
  CHECK(!hit.show() && near(hit.angle(), -kPi / 2));  // frozen while it fades out

  // A fresh cue restarts the full 0.1 s.
  hit.cue(north, 0.0f);
  hit.update(0.09f, north, 0.0f);
  CHECK(hit.show());

  return obf2test::g_failures == 0 ? 0 : 1;
}
