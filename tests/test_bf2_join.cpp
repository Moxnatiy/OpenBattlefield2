// Послідовність приєднання до оригінального сервера BF2.
//
// Кожна перевірка тут — це правило, здобуте вимірюванням на живому
// сервері (docs/functions/network-events.md), а не наша вигадка.
#include <chrono>

#include "check.h"
#include "obf2/net/bf2_join.h"

using namespace obf2::net::bf2;
using Clock = JoinSequence::Clock;

namespace {

// Зручність: просунути годинник рівно на паузу між кроками.
Clock::time_point after(Clock::time_point at) { return at + JoinSequence::kStepDelay; }

// Пройти крок: переконатися, що послідовність просить саме його, і
// відзвітувати, що ми його відіслали.
bool take(JoinSequence& join, Clock::time_point& at, JoinStep expected) {
  const auto step = join.next(at);
  if (!step || *step != expected) return false;
  join.commit(at);
  at = after(at);
  return true;
}

}  // namespace

// Доки сервер не назвав рівень і доки ми його не завантажили, слати
// нічого не можна: під час завантаження ми мовчимо, і сервер розриває
// з'єднання за мовчанку.
static void testWaitsForLevelAndLoad() {
  JoinSequence join;
  Clock::time_point at{};

  CHECK(!join.next(at).has_value());

  join.setLevelReady();
  CHECK(!join.next(at).has_value());  // рівень названо, але ще не завантажено

  join.setClientLoaded();
  const auto step = join.next(at);
  CHECK(step.has_value());
  if (step) CHECK(*step == JoinStep::Level);
}

// Пауза між кроками: два кроки поспіль сервер застає в старому стані.
static void testStepsAreSpacedApart() {
  JoinSequence join;
  join.setLevelReady();
  join.setClientLoaded();

  Clock::time_point at{};
  const auto first = join.next(at);
  CHECK(first.has_value());
  join.commit(at);

  // Одразу після кроку — ще рано.
  CHECK(!join.next(at).has_value());
  CHECK(!join.next(at + std::chrono::seconds(2)).has_value());
  CHECK(join.next(after(at)).has_value());
}

// Без DONE послідовність спиняється на екрані появи й далі не йде.
static void testStopsAtSpawnScreen() {
  JoinSequence join;
  join.setLevelReady();
  join.setClientLoaded();

  Clock::time_point at{};
  CHECK(take(join, at, JoinStep::Level));
  CHECK(take(join, at, JoinStep::Content));
  CHECK(take(join, at, JoinStep::Database));

  // Тут стоїмо, скільки б часу не минуло.
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

// Безголовий запуск: вибір задано ще до рукостискання, тож `Ready`
// проходиться без зупинки.
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

// Досліди --no-content і --no-database: пропущені кроки не з'їдають
// власної паузи, бо нічого не шлють.
static void testSkippedStepsCostNoDelay() {
  JoinSequence join;
  join.setSkipContent(true);
  join.setSkipDatabase(true);
  join.ask(JoinChoice{1, 0, 515});
  join.setLevelReady();
  join.setClientLoaded();

  Clock::time_point at{};
  CHECK(take(join, at, JoinStep::Level));
  // Одразу після рівня має йти команда: перевірку й базу пропущено.
  CHECK(take(join, at, JoinStep::Team));
  CHECK(take(join, at, JoinStep::Kit));
  CHECK(take(join, at, JoinStep::Group));
  CHECK(join.done());
}

TEST_MAIN({
  testWaitsForLevelAndLoad();
  testStepsAreSpacedApart();
  testStopsAtSpawnScreen();
  testChoiceMadeEarlyDoesNotStop();
  testSkippedStepsCostNoDelay();
})
