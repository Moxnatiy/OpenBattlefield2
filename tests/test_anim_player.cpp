// What plays and how far into it (`anim/player.h`).
//
// The numbers are the soldier's own data: the run bundle's four clips in the
// order `soldiers/Common/Animations/AnimationSystem3p.inc` adds them, and the
// range `3p_stand_run 0.1 3.9 5.8` whose third number is the speed they were
// animated at (docs/functions/animation-system.md, "Playing a bundle").
#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/anim/player.h"

using namespace obf2;

namespace {

anim::System build(const std::vector<std::string>& lines) {
  anim::System system;
  for (const std::string& line : lines) {
    const std::vector<std::string> tokens = con::tokenizeLine(line);
    if (tokens.empty()) continue;
    con::Command command;
    command.path = con::splitCommandPath(tokens[0]);
    command.lowerPath = tokens[0];
    for (char& c : command.lowerPath) {
      c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    command.args.assign(tokens.begin() + 1, tokens.end());
    system.feed(command);
  }
  return system;
}

anim::System runSystem() {
  return build({
      "animationSystem.createBundle stand_run",
      "animationBundle.addAnimation 3p_strafeLeft.baf",
      "animationBundle.addAnimation 3p_strafeRight.baf",
      "animationBundle.addAnimation 3p_runBackward.baf",
      "animationBundle.addAnimation 3p_runForward.baf",
      "AnimationSystem.createValueHolder 3p_stand_run",
      "AnimationValueHolder.values 0.1 3.9 5.8",
      "animationSystem.createTrigger MovementTrigger stand_run",
      "animationTrigger.addBundle stand_run",
      "animationTrigger.valueHolder 3p_stand_run",
      "animationSystem.createTrigger Trigger root",
      "animationTrigger.addChild stand_run",
  });
}

}  // namespace

// Running straight ahead: the forward clip carries the whole weight, and the clip
// runs at speed / 5.8 rather than on the spot.
static void testRunningForward() {
  const anim::System system = runSystem();
  anim::Player player;
  player.setLengthOf([](const std::string&) { return 1.0f; });

  anim::State state;
  state.speed = 3.9f;
  state.direction[0] = 0.0f;
  state.direction[2] = 1.0f;
  player.update(system, state, 0.1f);

  const auto& playing = player.playing();
  CHECK_EQ(playing.size(), std::size_t(2));
  if (playing.size() != 2) return;
  CHECK_EQ(playing[0].path, std::string("3p_runForward.baf"));
  CHECK(std::abs(playing[0].weight - 1.0f) < 0.001f);
  // With no side movement at all the side index falls on the dead zone's branch,
  // which is the data's first clip; its weight is zero either way.
  CHECK_EQ(playing[1].path, std::string("3p_strafeLeft.baf"));
  CHECK(std::abs(playing[1].weight) < 0.001f);
  // 0.1 s at 3.9 / 5.8 of the clip's own rate.
  CHECK(std::abs(playing[0].time - 0.1f * (3.9f / 5.8f)) < 0.0001f);
}

// Strafing left: the side clip takes over, and the pair is the left one.
static void testStrafingLeft() {
  const anim::System system = runSystem();
  anim::Player player;
  player.setLengthOf([](const std::string&) { return 1.0f; });

  anim::State state;
  state.speed = 3.9f;
  state.direction[0] = -1.0f;
  state.direction[2] = 0.0f;
  player.update(system, state, 0.1f);

  const auto& playing = player.playing();
  CHECK_EQ(playing.size(), std::size_t(2));
  if (playing.size() != 2) return;
  CHECK(std::abs(playing[0].weight) < 0.001f);            // no forward
  CHECK_EQ(playing[1].path, std::string("3p_strafeLeft.baf"));
  CHECK(std::abs(playing[1].weight - 1.0f) < 0.001f);

  // Running backwards takes the backward clip.
  state.direction[0] = 0.0f;
  state.direction[2] = -1.0f;
  player.update(system, state, 0.1f);
  CHECK_EQ(player.playing()[0].path, std::string("3p_runBackward.baf"));
}

// Diagonal: half the angle, both clips carry weight, and the two add up to one.
static void testDiagonalBlends() {
  const anim::System system = runSystem();
  anim::Player player;
  player.setLengthOf([](const std::string&) { return 1.0f; });

  anim::State state;
  state.speed = 3.9f;
  const float half = 0.70710678f;
  state.direction[0] = half;
  state.direction[2] = half;
  player.update(system, state, 0.1f);

  const auto& playing = player.playing();
  CHECK_EQ(playing.size(), std::size_t(2));
  if (playing.size() != 2) return;
  // asin(0.707) * 2/pi = 0.5 exactly — the engine's own formula.
  CHECK(std::abs(playing[0].weight - 0.5f) < 0.002f);
  CHECK(std::abs(playing[0].weight + playing[1].weight - 1.0f) < 0.001f);
  CHECK_EQ(playing[1].path, std::string("3p_strafeRight.baf"));
}

// The time keeps running while the bundle keeps being asked for, and wraps around
// the longest clip; a bundle nobody asks for loses its time.
static void testTimeIsKeptAndWrapped() {
  const anim::System system = runSystem();
  anim::Player player;
  player.setLengthOf([](const std::string&) { return 0.5f; });

  anim::State state;
  state.speed = 2.9f;  // inside the run range, so the bundle is asked for
  state.direction[2] = 1.0f;
  const float rate = 2.9f / 5.8f;  // half the clips' own speed
  player.update(system, state, 0.4f);
  CHECK(std::abs(player.timeOf("stand_run") - 0.4f * rate) < 0.001f);
  player.update(system, state, 0.4f);
  CHECK(std::abs(player.timeOf("stand_run") - 0.4f * rate * 2.0f) < 0.001f);
  player.update(system, state, 0.4f);  // past 0.5 now: wrapped
  CHECK(std::abs(player.timeOf("stand_run") - (0.4f * rate * 3.0f - 0.5f)) < 0.001f);

  state.speed = 0.0f;  // standing: the run trigger's range no longer holds
  player.update(system, state, 0.1f);
  CHECK(player.playing().empty());
  CHECK_EQ(player.timeOf("stand_run"), 0.0f);
}

TEST_MAIN({
  testRunningForward();
  testStrafingLeft();
  testDiagonalBlends();
  testTimeIsKeptAndWrapped();
})
