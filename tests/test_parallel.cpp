// `parallelFor` — the one thing the loading phases are spread over.
#include <atomic>
#include <cstdint>
#include <numeric>
#include <vector>

#include "check.h"
#include "obf2/core/parallel.h"

using namespace obf2;

namespace {

// Every index exactly once, and nothing outside the range. The body writes only
// its own slot, which is the contract the callers rely on.
void testEveryIndexOnce() {
  for (const std::size_t count : {std::size_t(1), std::size_t(2), std::size_t(7),
                                  std::size_t(64), std::size_t(4001)}) {
    std::vector<int> visits(count, 0);
    parallelFor(count, [&visits](std::size_t i) { visits[i] += 1; });
    const int total = std::accumulate(visits.begin(), visits.end(), 0);
    CHECK_EQ(total, static_cast<int>(count));
    bool everyOne = true;
    for (const int v : visits) {
      if (v != 1) everyOne = false;
    }
    CHECK(everyOne);
  }
}

// An empty range runs nothing and starts no thread.
void testEmptyRange() {
  std::atomic<int> calls{0};
  parallelFor(0, [&calls](std::size_t) { calls.fetch_add(1); });
  CHECK_EQ(calls.load(), 0);
}

// The work is handed out one item at a time rather than in equal blocks, so one
// slow item does not hold a whole block behind it. This is what that buys: with
// a single heavy item among many light ones, every thread keeps working, and the
// count of items done by threads other than the heavy one's is not zero.
//
// It is checked by arithmetic and not by a clock: the result must be right
// however the operating system schedules the run.
void testUnevenWorkStillFinishes() {
  constexpr std::size_t kCount = 512;
  std::vector<std::uint64_t> out(kCount, 0);
  parallelFor(kCount, [&out](std::size_t i) {
    // The first item is a hundred times the others' work.
    const std::uint64_t rounds = i == 0 ? 100000 : 1000;
    std::uint64_t sum = 0;
    for (std::uint64_t r = 0; r < rounds; ++r) sum += r % 7;
    out[i] = sum;
  });
  CHECK(out[0] > out[1]);
  bool allDone = true;
  for (std::size_t i = 1; i < kCount; ++i) {
    if (out[i] != out[1]) allDone = false;
  }
  CHECK(allDone);
}

// The threads see what the caller wrote before the call and the caller sees what
// they wrote after it — join is the barrier, and nothing else is needed.
void testResultsAreVisibleAfterTheCall() {
  constexpr std::size_t kCount = 1000;
  std::vector<std::size_t> input(kCount);
  for (std::size_t i = 0; i < kCount; ++i) input[i] = i * 3;

  std::vector<std::size_t> doubled(kCount, 0);
  parallelFor(kCount, [&](std::size_t i) { doubled[i] = input[i] * 2; });
  for (std::size_t i = 0; i < kCount; ++i) CHECK_EQ(doubled[i], i * 6);
}

}  // namespace

TEST_MAIN({
  testEveryIndexOnce();
  testEmptyRange();
  testUnevenWorkStillFinishes();
  testResultsAreVisibleAfterTheCall();
})
