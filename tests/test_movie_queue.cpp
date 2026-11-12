#include <map>
#include <string>

#include "check.h"
#include "obf2/engine/movie_queue.h"

using namespace obf2;

namespace {

// The lengths the game's own movies have (their Bink headers), and one that does
// not open.
std::optional<double> load(const std::string& name) {
  static const std::map<std::string, double> kLengths = {
      {"Movies\\EA.bik", 3.2},     {"Movies\\dice.bik", 6.2}, {"Movies\\legal.bik", 5.0},
      {"Movies\\intro.bik", 152.9}, {"Movies\\welcome.bik", 1.6},
  };
  const auto it = kLengths.find(name);
  if (it == kLengths.end()) return std::nullopt;
  return it->second;
}

// mods/bf2/Init.con, line for line.
void queueInitCon(engine::MovieQueue& queue) {
  queue.play("Movies\\EA.bik", 1);
  queue.play("Movies\\dice.bik", 1);
  queue.play("Movies\\legal.bik", 1);
  queue.play("Movies\\intro.bik", 0);
  queue.play("Movies\\welcome.bik", 0);
}

}  // namespace

// 0x40cf4d: Escape does nothing to a flag-1 movie; it plays to its end.
static void testFlagOneCannotBeSkipped() {
  engine::MovieQueue queue;
  queueInitCon(queue);
  queue.check(load);
  CHECK(queue.playing());
  CHECK_EQ(queue.current(), std::string("Movies\\EA.bik"));
  CHECK_EQ(queue.currentFlag(), 1);

  queue.frame(0.1, true, load);
  CHECK_EQ(queue.current(), std::string("Movies\\EA.bik"));

  // Its time up, the next one starts in the same frame (0x40cf8b).
  queue.frame(3.2, false, load);
  CHECK_EQ(queue.current(), std::string("Movies\\dice.bik"));
}

// Escape ends a flag-0 movie at once.
static void testFlagZeroIsSkippedByEscape() {
  engine::MovieQueue queue;
  queueInitCon(queue);
  queue.check(load);
  queue.frame(3.2, false, load);
  queue.frame(6.2, false, load);
  queue.frame(5.0, false, load);
  CHECK_EQ(queue.current(), std::string("Movies\\intro.bik"));
  CHECK_EQ(queue.currentFlag(), 0);

  queue.frame(0.016, true, load);
  CHECK_EQ(queue.current(), std::string("Movies\\welcome.bik"));
  queue.frame(0.016, true, load);
  CHECK(!queue.playing());
  CHECK(queue.pending().empty());
}

// 0x407cb0: a movie that does not open is passed over for the next.
static void testUnopenedMovieIsPassedOver() {
  engine::MovieQueue queue;
  queue.play("Movies\\missing.bik", 1);
  queue.play("Movies\\welcome.bik", 0);
  queue.check(load);
  CHECK_EQ(queue.current(), std::string("Movies\\welcome.bik"));
  CHECK_EQ(queue.currentFlag(), 0);
}

// 0x407a9a and 0x407ced: `skipMovies` drops the requests and starts nothing.
static void testSkipMovies() {
  engine::MovieQueue queue;
  queue.setSkipMovies(true);
  queueInitCon(queue);
  queue.check(load);
  CHECK(!queue.playing());
  CHECK(queue.queued().empty());
}

TEST_MAIN({
  testFlagOneCannotBeSkipped();
  testFlagZeroIsSkippedByEscape();
  testUnopenedMovieIsPassedOver();
  testSkipMovies();
})
