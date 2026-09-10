#pragma once
// Running the same job over a range of items on every core there is.
//
// Loading a level is where this matters: unpacking a mesh out of an archive,
// taking a `.con` apart, decoding a texture — each is a few milliseconds of one
// core, there are thousands of them, and none of them looks at another's work.
// The frame loop stays single-threaded; this is for the phases before it.
//
// There is no pool. A phase starts its threads and joins them, which costs
// about 30 microseconds per phase on this machine and saves whole seconds, and
// it keeps the rule that a thread cannot outlive the call that made it.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace obf2 {

// How many threads a phase should use. `std::thread::hardware_concurrency` on
// an Apple M-series counts the efficiency cores too (8 = 6 + 2), and using them
// is right here: the work is throughput, not latency.
inline unsigned parallelThreads() {
  const unsigned reported = std::thread::hardware_concurrency();
  return reported == 0 ? 1u : reported;
}

// Calls `body(i)` for every i in [0, count), on up to `parallelThreads()`
// threads, and returns when the last one is done. The order is not the loop's:
// whatever `body` writes has to be indexed by `i`, never appended.
//
// `body` must not throw — a phase that fails mid-flight would leave the other
// threads running, so the callers here report failure through their own output.
template <typename Body>
void parallelFor(std::size_t count, Body body) {
  if (count == 0) return;
  const unsigned threads =
      static_cast<unsigned>(std::min<std::size_t>(parallelThreads(), count));
  if (threads <= 1) {
    for (std::size_t i = 0; i < count; ++i) body(i);
    return;
  }

  // The items are handed out one at a time rather than in equal blocks: a
  // level's meshes differ by a hundredfold in size, and a block split would
  // leave one thread with the cathedral and seven with the fence posts.
  std::atomic<std::size_t> next{0};
  std::vector<std::thread> workers;
  workers.reserve(threads);
  for (unsigned t = 0; t < threads; ++t) {
    workers.emplace_back([&next, count, &body] {
      for (;;) {
        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= count) return;
        body(i);
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
}

}  // namespace obf2
