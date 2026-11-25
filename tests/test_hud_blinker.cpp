// The HUD's float blinker (obf2/hud/blinker.h): the ticket bleed's triangle.
#include <cmath>

#include "check.h"
#include "obf2/hud/blinker.h"

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void testTriangleWhileGated() {
  obf2::hud::FloatBlinker blinker(true, 1.0f, true);
  float out = 0.0f;
  // Direction starts at -1: the first stroke falls from 1.
  blinker.update(0.25f, true, out);
  CHECK(near(out, 0.75f));
  blinker.update(0.5f, true, out);
  CHECK(near(out, 0.25f));
  // The period is reached: the direction flips, the time starts again.
  blinker.update(0.25f, true, out);
  CHECK(near(out, 0.0f));
  blinker.update(0.5f, true, out);
  CHECK(near(out, 0.5f));
}

void testReleasedGateStopsAtOne() {
  obf2::hud::FloatBlinker blinker(true, 1.0f, true);
  float out = 0.0f;
  blinker.update(0.5f, true, out);   // falling: 0.5
  blinker.update(0.5f, false, out);  // flips, rises from 0
  CHECK(out < 1.0f);
  for (int i = 0; i < 8; ++i) blinker.update(0.25f, false, out);
  CHECK(near(out, 1.0f) || out >= 1.0f);
  const float rest = out;
  blinker.update(0.3f, false, out);
  CHECK_EQ(out, rest);  // at rest
}

}  // namespace

TEST_MAIN({
  testTriangleWhileGated();
  testReleasedGateStopsAtOne();
})
