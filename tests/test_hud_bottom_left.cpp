// The left corner region: the state machine from BF2.exe, 0x78b600.
#include <cmath>

#include "obf2/hud/bottom_left.h"
#include "obf2/meme/graph.h"
#include "check.h"

using namespace obf2;

namespace {

// One tick: the state machine picks the targets while the graph moves the values.
// Here the graph is stood in for by the very action it performs
// (`SetVariableSine`/`Soft` with the speeds from the file).
void tick(hud::BottomLeftPanel& panel, hud::BottomLeftMode mode, float background) {
  const float step = 1.0f / 30.0f;
  panel.update(mode, background);
  meme::approachVariable(panel.x, panel.targetX, hud::kCornerMoveSpeed, 0.0f, step);
  meme::approachVariable(panel.healthAlpha, panel.targetHealthAlpha, hud::kCornerAlphaSpeed, 0.0f,
                         step);
  meme::approachVariable(panel.vehicleAlpha, panel.targetVehicleAlpha, hud::kCornerAlphaSpeed,
                         0.0f, step);
}

// Run seconds forward in tick-sized steps.
void run(hud::BottomLeftPanel& panel, hud::BottomLeftMode mode, float seconds) {
  const float step = 1.0f / 30.0f;
  for (float t = 0.0f; t < seconds; t += step) tick(panel, mode, 0.8f);
}

}  // namespace

// On foot the region drives out to -137 and shows the health bars, while the
// vehicle bars stay faded. That is exactly what caught us out: with the wrong
// position the plate stretched over the full width, as if the player were in a vehicle.
void testOnFootStopsAtFootPosition() {
  hud::BottomLeftPanel panel;
  run(panel, hud::BottomLeftMode::Health, 2.0f);

  CHECK(std::abs(panel.x - hud::kBottomLeftFootX) < 0.5f);
  CHECK(std::abs(panel.healthAlpha - 1.0f) < 0.01f);
  CHECK(std::abs(panel.vehicleAlpha) < 0.01f);
}

// In a vehicle the region travels further — to 54, and only there do the vehicle
// bars fade in. But only after the health bars have faded in: in 0x78b600 that is
// visible from the condition `HealthAlpha == 1.0`.
void testVehicleGoesFurther() {
  hud::BottomLeftPanel panel;
  run(panel, hud::BottomLeftMode::Vehicle, 3.0f);

  CHECK(std::abs(panel.x - hud::kBottomLeftVehicleX) < 0.5f);
  CHECK(std::abs(panel.healthAlpha - 1.0f) < 0.01f);
  CHECK(std::abs(panel.vehicleAlpha - 1.0f) < 0.01f);
}

// The region does not hide at once: first the bars fade, and only then does it
// drive off the edge.
void testHiddenWaitsForAlpha() {
  hud::BottomLeftPanel panel;
  run(panel, hud::BottomLeftMode::Health, 2.0f);

  tick(panel, hud::BottomLeftMode::Hidden, 0.8f);
  CHECK(panel.x > hud::kBottomLeftHiddenX);  // it has not driven off yet

  run(panel, hud::BottomLeftMode::Hidden, 3.0f);
  CHECK(std::abs(panel.x - hud::kBottomLeftHiddenX) < 0.5f);
  CHECK(std::abs(panel.healthAlpha) < 0.01f);
}

// The dimmed alpha is the alpha minus what the profile's plate alpha eats:
// clamp(Alpha - (1 - base), 0, 1).
void testFadedFollowsBackgroundAlpha() {
  hud::BottomLeftPanel panel;
  run(panel, hud::BottomLeftMode::Health, 2.0f);
  CHECK(std::abs(panel.healthFadedAlpha - 0.8f) < 0.01f);

  tick(panel, hud::BottomLeftMode::Health, 1.0f);
  CHECK(std::abs(panel.healthFadedAlpha - 1.0f) < 0.01f);
}

// The graph's action: without a braking stretch it is linear, with one the step
// decays as a sine and does not overshoot the target.
void testGraphActionCurve() {
  float value = 0.0f;
  meme::approachVariable(value, 100.0f, 600.0f, 0.0f, 1.0f / 30.0f);
  CHECK(std::abs(value - 20.0f) < 0.01f);

  // The target is closer than the step — we land exactly on it, no further.
  value = 99.0f;
  meme::approachVariable(value, 100.0f, 600.0f, 0.0f, 1.0f / 30.0f);
  CHECK(std::abs(value - 100.0f) < 0.001f);

  // With braking: halfway along the braking stretch the step is smaller than the
  // full one, because sin(pi/4) < 1.
  value = 95.0f;
  meme::approachVariable(value, 100.0f, 600.0f, 10.0f, 1.0f / 30.0f);
  CHECK(value > 95.0f);
  CHECK(value < 95.0f + 20.0f);

  // Backwards it works the same way.
  value = 100.0f;
  meme::approachVariable(value, 0.0f, 600.0f, 0.0f, 1.0f / 30.0f);
  CHECK(std::abs(value - 80.0f) < 0.01f);
}

TEST_MAIN({
  testGraphActionCurve();
  testOnFootStopsAtFootPosition();
  testVehicleGoesFurther();
  testHiddenWaitsForAlpha();
  testFadedFollowsBackgroundAlpha();
});
