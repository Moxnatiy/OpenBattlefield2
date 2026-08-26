#include <string>

#include "check.h"
#include "obf2/anim/system.h"

using namespace obf2;

namespace {

// Годує систему командами напряму — так тест не залежить від файлів гри.
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
  // У даних від'ємні діапазони записані навпаки (`3p_turn -1 -3 -10`), і
  // рушій розрізняє це за знаком першої межі.
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

  // Поза поза межами списку зводиться до останньої дитини — так робить
  // `PoseTrigger::update` у рушії.
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

TEST_MAIN({
  testValueHolderRange();
  testNegativeRangeIsSwapped();
  testPoseTriggerPicksChildByPose();
  testMovementTriggerGatesOnSpeed();
})
