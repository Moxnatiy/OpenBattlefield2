// A soldier's sprint (`soldier_sprint.h`, `SprintState`, Linux server 0x43ded0).
//
// The rates are the live server's: a light kit sprinting reported its stamina
// falling from 0.992 to 0.661 over 101 ticks, and rising from 0.661 to 0.732 over
// 37 (docs/functions/soldier-physics.md, "Sprint").
#include <cmath>

#include "check.h"
#include "obf2/server/soldier_sprint.h"

namespace {

using namespace obf2::server;

constexpr float kTick = 1.0f / 30.0f;

// A start message starts it; a go-on message alone does not.
void testMessagesStartAndKeep() {
  SprintState sprint;
  sprintMessage(sprint, true);
  updateSprint(sprint, false, 0.0f, kTick);
  CHECK(!sprint.sprinting);

  sprintMessage(sprint, false);
  updateSprint(sprint, false, 0.0f, kTick);
  CHECK(sprint.sprinting);

  sprintMessage(sprint, true);
  updateSprint(sprint, false, 0.0f, kTick);
  CHECK(sprint.sprinting);

  // A tick with no message ends it: the message is used up by every update.
  updateSprint(sprint, false, 0.0f, kTick);
  CHECK(!sprint.sprinting);
}

// Stamina drains by step / dissipation time and recovers by step / recover time.
void testStaminaRates() {
  SprintState sprint;
  sprint.stamina = 0.992f;
  sprint.sprinting = true;
  for (int i = 0; i < 101; ++i) {
    sprintMessage(sprint, true);
    updateSprint(sprint, false, 0.0f, kTick);
  }
  CHECK(sprint.sprinting);
  CHECK(std::abs(sprint.stamina - 0.655f) < 0.01f);

  sprint.sprinting = false;
  sprint.stamina = 0.661f;
  for (int i = 0; i < 37; ++i) updateSprint(sprint, false, 0.0f, kTick);
  CHECK(std::abs(sprint.stamina - 0.733f) < 0.01f);

  // A recharge delay holds the recovery.
  const float held = sprint.stamina;
  updateSprint(sprint, false, 0.5f, kTick);
  CHECK(sprint.stamina == held);
}

// Under the limit nothing starts; an empty sprint ends; blocked never runs.
void testLimits() {
  SprintState low;
  low.stamina = 0.01f;
  sprintMessage(low, false);
  updateSprint(low, false, 0.5f, kTick);
  CHECK(!low.sprinting);

  SprintState empty;
  empty.sprinting = true;
  empty.stamina = 0.001f;
  sprintMessage(empty, true);
  updateSprint(empty, false, 0.0f, kTick);
  CHECK(!empty.sprinting);
  CHECK(empty.stamina == 0.0f);

  SprintState blocked;
  sprintMessage(blocked, false);
  updateSprint(blocked, true, 0.0f, kTick);
  CHECK(!blocked.sprinting);
}

}  // namespace

TEST_MAIN({
  testMessagesStartAndKeep();
  testStaminaRates();
  testLimits();
})
