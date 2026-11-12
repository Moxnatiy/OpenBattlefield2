// LoadStat's percent: a window per part (0.15 × weight), the share of its
// steps inside it, clamped, and 100 % once complete.
#include <cmath>

#include "check.h"
#include "obf2/engine/load_stat.h"

using obf2::engine::LoadStat;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

static void testWindows() {
  LoadStat stat;
  CHECK(near(stat.progress(), 0.0f));
  stat.nextPart("Objects", 10, 1.0f);  // window 0 .. 0.15
  CHECK(near(stat.progress(), 0.0f));
  stat.step(5);
  CHECK(near(stat.progress(), 0.075f));
  stat.step(50);  // clamped to the part's steps
  CHECK(near(stat.progress(), 0.15f));
  stat.nextPart("Animations", 4, 0.5f);  // window 0.15 .. 0.225
  stat.step(2);
  CHECK(near(stat.progress(), 0.1875f));
  stat.complete();
  CHECK(near(stat.progress(), 1.0f));
}

static void testEmptyPartAndCap() {
  LoadStat stat;
  stat.nextPart("Nothing", 0, 5.0f);  // no steps: ignored
  CHECK(near(stat.progress(), 0.0f));
  CHECK(stat.partName().empty());
  for (int i = 0; i < 10; ++i) stat.nextPart("Big", 1, 1.0f);  // the window stops at 1
  stat.step();
  CHECK(near(stat.progress(), 1.0f));
}

TEST_MAIN({
  testWindows();
  testEmptyPartAndCap();
})
