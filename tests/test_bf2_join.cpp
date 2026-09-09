// The sequence for joining an original BF2 server.
//
// Every check here is a rule won by measurement against a live server
// (docs/functions/network-events.md), not an invention of ours.
#include <chrono>

#include "check.h"
#include "obf2/net/bf2_join.h"

using namespace obf2::net::bf2;
using Clock = JoinSequence::Clock;

namespace {

// A convenience: advance the clock by exactly the pause between steps.
Clock::time_point after(Clock::time_point at) { return at + JoinSequence::kStepDelay; }

// Go through a step: make sure the sequence asks for exactly it, and report that we
// sent it.
bool take(JoinSequence& join, Clock::time_point& at, JoinStep expected) {
  const auto step = join.next(at);
  if (!step || *step != expected) return false;
  join.commit(at);
  at = after(at);
  return true;
}

}  // namespace

// Until the server has named the level and we have loaded it, nothing may be sent:
// during loading we stay silent, and the server breaks the connection for that
// silence.
static void testWaitsForLevelAndLoad() {
  JoinSequence join;
  Clock::time_point at{};

  CHECK(!join.next(at).has_value());

  join.setLevelReady();
  CHECK(!join.next(at).has_value());  // the level is named but not loaded yet

  join.setClientLoaded();
  const auto step = join.next(at);
  CHECK(step.has_value());
  if (step) CHECK(*step == JoinStep::Level);
}

// The content check goes together with "the level is loaded", while the pause
// stands before the message about the player base. That is how it is in the
// original's captured traffic, and that is how it should be by its meaning: the
// server asks about the content when the client has just read it.
static void testContentGoesWithLoadComplete() {
  JoinSequence join;
  join.setLevelReady();
  join.setClientLoaded();

  Clock::time_point at{};
  const auto first = join.next(at);
  CHECK(first.has_value());
  if (first) CHECK(*first == JoinStep::Level);
  join.commit(at);

  // At the same moment — the content check, without waiting.
  const auto second = join.next(at);
  CHECK(second.has_value());
  if (second) CHECK(*second == JoinStep::Content);
  join.commit(at);

  // The player base, on the other hand, waits its 1.1 s.
  CHECK(!join.next(at).has_value());
  CHECK(!join.next(at + std::chrono::milliseconds(900)).has_value());
  const auto third = join.next(after(at));
  CHECK(third.has_value());
  if (third) CHECK(*third == JoinStep::Database);
}

// Without DONE the sequence stops at the spawn screen and goes no further.
static void testStopsAtSpawnScreen() {
  JoinSequence join;
  join.setLevelReady();
  join.setClientLoaded();

  Clock::time_point at{};
  CHECK(take(join, at, JoinStep::Level));
  CHECK(take(join, at, JoinStep::Content));
  CHECK(take(join, at, JoinStep::Database));

  // Here we stand, however much time passes.
  CHECK(!join.next(at).has_value());
  CHECK(!join.next(at + std::chrono::minutes(5)).has_value());
  CHECK(join.step() == JoinStep::Ready);

  join.ask(JoinChoice{2, 3, 515});
  CHECK(take(join, at, JoinStep::Team));
  CHECK(take(join, at, JoinStep::Kit));
  CHECK(take(join, at, JoinStep::Group));
  CHECK(join.done());
  CHECK(!join.next(at + std::chrono::minutes(5)).has_value());

  CHECK_EQ(join.choice().team, 2);
  CHECK_EQ(join.choice().kit, 3);
  CHECK_EQ(join.choice().group, 515);
}

// A headless run: the choice is set before the handshake, so `Ready` passes without
// stopping.
static void testChoiceMadeEarlyDoesNotStop() {
  JoinSequence join;
  join.ask(JoinChoice{1, 0, 515});
  join.setLevelReady();
  join.setClientLoaded();

  Clock::time_point at{};
  CHECK(take(join, at, JoinStep::Level));
  CHECK(take(join, at, JoinStep::Content));
  CHECK(take(join, at, JoinStep::Database));
  CHECK(take(join, at, JoinStep::Team));
  CHECK(take(join, at, JoinStep::Kit));
  CHECK(take(join, at, JoinStep::Group));
  CHECK(join.done());
}

// The --no-content and --no-database experiments: skipped steps do not eat a pause
// of their own, because they send nothing.
static void testSkippedStepsCostNoDelay() {
  JoinSequence join;
  join.setSkipContent(true);
  join.setSkipDatabase(true);
  join.ask(JoinChoice{1, 0, 515});
  join.setLevelReady();
  join.setClientLoaded();

  Clock::time_point at{};
  CHECK(take(join, at, JoinStep::Level));
  // Right after the level the team has to come: the check and the base are skipped.
  CHECK(take(join, at, JoinStep::Team));
  CHECK(take(join, at, JoinStep::Kit));
  CHECK(take(join, at, JoinStep::Group));
  CHECK(join.done());
}

TEST_MAIN({
  testWaitsForLevelAndLoad();
  testContentGoesWithLoadComplete();
  testStopsAtSpawnScreen();
  testChoiceMadeEarlyDoesNotStop();
  testSkippedStepsCostNoDelay();
})
