#include <string>

#include "check.h"
#include "obf2/anim/system.h"

using namespace obf2;

namespace {

// Feeds the system commands directly — that way the test does not depend on the game's files.
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

}  // namespace

static void testValueHolderRange() {
  anim::ValueHolder holder;
  holder.low = 0.1f;
  holder.high = 3.9f;
  CHECK(holder.contains(0.1f));
  CHECK(holder.contains(2.0f));
  CHECK(holder.contains(3.9f));
  CHECK(!holder.contains(0.0f));
  CHECK(!holder.contains(4.0f));
}

static void testNegativeRangeIsSwapped() {
  // In the data negative ranges are written the other way round (`3p_turn -1 -3 -10`),
  // and the engine tells them apart by the first bound's sign.
  anim::ValueHolder holder;
  holder.low = -1.0f;
  holder.high = -3.0f;
  CHECK(holder.contains(-2.0f));
  CHECK(holder.contains(-1.0f));
  CHECK(holder.contains(-3.0f));
  CHECK(!holder.contains(0.0f));
  CHECK(!holder.contains(-4.0f));
}

static void testPoseTriggerPicksChildByPose() {
  const anim::System system = build({
      "animationSystem.createBundle standBundle",
      "animationBundle.addAnimation stand.baf",
      "animationSystem.createBundle crouchBundle",
      "animationBundle.addAnimation crouch.baf",
      "animationSystem.createTrigger Trigger stand",
      "animationTrigger.addBundle standBundle",
      "animationSystem.createTrigger Trigger crouch",
      "animationTrigger.addBundle crouchBundle",
      "animationSystem.createTrigger PoseTrigger pose",
      "animationTrigger.addChild stand",
      "animationTrigger.addChild crouch",
  });

  CHECK_EQ(system.roots().size(), std::size_t(1));

  anim::State state;
  state.pose = anim::Pose::Stand;
  auto chosen = system.select(state);
  CHECK_EQ(chosen.size(), std::size_t(1));
  if (!chosen.empty()) CHECK_EQ(chosen[0]->name, std::string("standBundle"));

  state.pose = anim::Pose::Crouch;
  chosen = system.select(state);
  CHECK_EQ(chosen.size(), std::size_t(1));
  if (!chosen.empty()) CHECK_EQ(chosen[0]->name, std::string("crouchBundle"));

  // A pose outside the list falls back to the last child — that is what
  // `PoseTrigger::update` does in the engine.
  state.pose = anim::Pose::Swim;
  chosen = system.select(state);
  CHECK_EQ(chosen.size(), std::size_t(1));
  if (!chosen.empty()) CHECK_EQ(chosen[0]->name, std::string("crouchBundle"));
}

static void testMovementTriggerGatesOnSpeed() {
  const anim::System system = build({
      "animationSystem.createBundle runBundle",
      "animationBundle.addAnimation run.baf",
      "AnimationSystem.createValueHolder runRange",
      "AnimationValueHolder.values 0.1 3.9 5.8",
      "animationSystem.createTrigger MovementTrigger run",
      "animationTrigger.addBundle runBundle",
      "animationTrigger.valueHolder runRange",
  });

  anim::State state;
  state.speed = 0.0f;
  CHECK(system.select(state).empty());

  state.speed = 2.0f;
  CHECK_EQ(system.select(state).size(), std::size_t(1));

  state.speed = 5.0f;
  CHECK(system.select(state).empty());
}

// A direction trigger tests one component of `speed * direction`, with its sign:
// `ForwardTrigger` the third, `SideTrigger` the first (`BF2.exe` 0x7ff680 and the
// Linux server's 0x6cd5e0).
static void testDirectionTriggersTakeTheirComponent() {
  const anim::System system = build({
      "animationSystem.createBundle forwardBundle",
      "animationBundle.addAnimation forward.baf",
      "animationSystem.createBundle leftBundle",
      "animationBundle.addAnimation left.baf",
      "AnimationSystem.createValueHolder forwardRange",
      "AnimationValueHolder.values 0.1 3.9 0",
      "AnimationSystem.createValueHolder leftRange",
      "AnimationValueHolder.values -0.1 -3.9 0",
      "animationSystem.createTrigger ForwardTrigger forward",
      "animationTrigger.addBundle forwardBundle",
      "animationTrigger.valueHolder forwardRange",
      "animationSystem.createTrigger SideTrigger left",
      "animationTrigger.addBundle leftBundle",
      "animationTrigger.valueHolder leftRange",
      "animationSystem.createTrigger Trigger root",
      "animationTrigger.addChild forward",
      "animationTrigger.addChild left",
  });

  anim::State state;
  state.speed = 3.0f;
  state.direction[0] = 0.0f;
  state.direction[2] = 1.0f;  // straight ahead
  auto chosen = system.select(state);
  CHECK_EQ(chosen.size(), std::size_t(1));
  if (!chosen.empty()) CHECK_EQ(chosen[0]->name, std::string("forwardBundle"));

  state.direction[0] = -1.0f;  // strafing left
  state.direction[2] = 0.0f;
  chosen = system.select(state);
  CHECK_EQ(chosen.size(), std::size_t(1));
  if (!chosen.empty()) CHECK_EQ(chosen[0]->name, std::string("leftBundle"));

  // Running backwards is neither: the forward range is positive only.
  state.direction[0] = 0.0f;
  state.direction[2] = -1.0f;
  CHECK(system.select(state).empty());
}

// `MessageTrigger` plays only while its message is in the state's mask, and a
// `MovementTrigger`'s value holder can demand or forbid one of its own.
static void testMessagesGate() {
  const anim::System system = build({
      "animationSystem.createBundle fireBundle",
      "animationBundle.addAnimation fire.baf",
      "animationSystem.createBundle moveBundle",
      "animationBundle.addAnimation move.baf",
      "AnimationSystem.createValueHolder moveRange",
      "AnimationValueHolder.values 0.1 3.9 0",
      "AnimationValueHolder.stopOnMessage 4",
      "animationSystem.createTrigger MessageTrigger fire",
      "animationTrigger.message 2",
      "animationTrigger.addBundle fireBundle",
      "animationSystem.createTrigger MovementTrigger move",
      "animationTrigger.addBundle moveBundle",
      "animationTrigger.valueHolder moveRange",
      "animationSystem.createTrigger Trigger root",
      "animationTrigger.addChild fire",
      "animationTrigger.addChild move",
  });

  anim::State state;
  state.speed = 2.0f;
  auto chosen = system.select(state);
  CHECK_EQ(chosen.size(), std::size_t(1));  // moving, not firing
  if (!chosen.empty()) CHECK_EQ(chosen[0]->name, std::string("moveBundle"));

  state.messages = 2;
  CHECK_EQ(system.select(state).size(), std::size_t(2));

  // The holder's `stopOnMessage` takes the movement out.
  state.messages = 4;
  CHECK(system.select(state).empty());
}

// A RandomTrigger plays one of its bundles, not all of them.
static void testRandomTriggerPlaysOne() {
  const anim::System system = build({
      "animationSystem.createBundle hit1",
      "animationBundle.addAnimation hit1.baf",
      "animationSystem.createBundle hit2",
      "animationBundle.addAnimation hit2.baf",
      "animationSystem.createTrigger RandomTrigger hit",
      "animationTrigger.addBundle hit1",
      "animationTrigger.addBundle hit2",
  });

  const anim::State state;
  const auto first = system.select(state, [](std::size_t) { return std::size_t(0); });
  CHECK_EQ(first.size(), std::size_t(1));
  if (!first.empty()) CHECK_EQ(first[0]->name, std::string("hit1"));

  const auto second = system.select(state, [](std::size_t) { return std::size_t(1); });
  CHECK_EQ(second.size(), std::size_t(1));
  if (!second.empty()) CHECK_EQ(second[0]->name, std::string("hit2"));
}

// `SwitchMessageTrigger` picks child [0] while its message is set and [1] while it
// is not, and plays its own bundle only on the tick the flag changes.
static void testSwitchMessageTrigger() {
  const anim::System system = build({
      "animationSystem.createBundle zoomIn",
      "animationBundle.addAnimation zoomin.baf",
      "animationSystem.createBundle zoomed",
      "animationBundle.addAnimation zoomed.baf",
      "animationSystem.createBundle hip",
      "animationBundle.addAnimation hip.baf",
      "animationSystem.createTrigger Trigger zoomedPose",
      "animationTrigger.addBundle zoomed",
      "animationSystem.createTrigger Trigger hipPose",
      "animationTrigger.addBundle hip",
      "animationSystem.createTrigger SwitchMessageTrigger zoom",
      "animationTrigger.message 1",
      "animationTrigger.addBundle zoomIn",
      "animationTrigger.addChild zoomedPose",
      "animationTrigger.addChild hipPose",
  });

  anim::State state;
  auto chosen = system.select(state);  // no message, no change
  CHECK_EQ(chosen.size(), std::size_t(1));
  if (!chosen.empty()) CHECK_EQ(chosen[0]->name, std::string("hip"));

  state.messages = 1;  // the tick it is switched on
  chosen = system.select(state);
  CHECK_EQ(chosen.size(), std::size_t(2));
  if (chosen.size() == 2) {
    CHECK_EQ(chosen[0]->name, std::string("zoomed"));
    CHECK_EQ(chosen[1]->name, std::string("zoomIn"));
  }

  state.previousMessages = 1;  // held on: the switch clip is done
  chosen = system.select(state);
  CHECK_EQ(chosen.size(), std::size_t(1));
  if (!chosen.empty()) CHECK_EQ(chosen[0]->name, std::string("zoomed"));
}

// An IdleTrigger says nothing until the idle time is inside its own range — and
// then it walks its children, which in the game's data are a RandomTrigger with
// the idle animations (`face_idle` in the soldier's system).
static void testIdleTrigger() {
  const anim::System system = build({
      "animationSystem.createBundle idle1",
      "animationBundle.addAnimation idle1.baf",
      "animationSystem.createBundle idle2",
      "animationBundle.addAnimation idle2.baf",
      "animationSystem.createTrigger RandomTrigger rnd_idle",
      "animationTrigger.addBundle idle1",
      "animationTrigger.addBundle idle2",
      "animationSystem.createTrigger IdleTrigger idle",
      "animationTrigger.idleTime 5/10",
      "animationTrigger.addChild rnd_idle",
  });

  anim::State state;
  CHECK(system.select(state).empty());  // no idle time at all
  state.idleTime = 2.0f;
  CHECK(system.select(state).empty());
  state.idleTime = 7.0f;
  CHECK_EQ(system.select(state).size(), std::size_t(1));
  state.idleTime = 20.0f;
  CHECK(system.select(state).empty());
}

TEST_MAIN({
  testValueHolderRange();
  testNegativeRangeIsSwapped();
  testPoseTriggerPicksChildByPose();
  testMovementTriggerGatesOnSpeed();
  testDirectionTriggersTakeTheirComponent();
  testMessagesGate();
  testRandomTriggerPlaysOne();
  testSwitchMessageTrigger();
  testIdleTrigger();
})
