#include "obf2/engine/movie_queue.h"

#include <utility>

namespace obf2::engine {

void MovieQueue::play(std::string name, int flag) {
  // 0x407a9a: `skipMovies` drops the request; a server (0x440510) has no movies at
  // all, and we are only ever the client.
  if (skipMovies_) return;
  pending_.push_back(QueuedMovie{name, flag});
  queued_.push_back(QueuedMovie{std::move(name), flag});
}

void MovieQueue::check(const Loader& load) {
  // 0x407ced, 0x407cf5, 0x407d09: the three ways out before anything is taken.
  if (skipMovies_ || playing_ || pending_.empty()) return;
  while (!pending_.empty()) {
    // 0x407d10..0x407d6b: the head's name and flag (engine+0x104) are taken and the
    // entry is unlinked before the player is even created.
    QueuedMovie head = std::move(pending_.front());
    pending_.erase(pending_.begin());
    flag_ = head.flag;
    if (const auto seconds = load(head.name)) {
      playing_ = true;
      current_ = std::move(head.name);
      elapsed_ = 0.0;
      length_ = *seconds;
      return;
    }
    // A player that failed to load is released and the next entry is tried.
  }
}

void MovieQueue::frame(double seconds, bool escapePressed, const Loader& load) {
  if (playing_) {
    elapsed_ += seconds;
    // 0x40cf4d..0x40cf6a: Escape on a movie whose flag is not 1, or the player
    // says it has finished.
    const bool skipped = escapePressed && flag_ != 1;
    if (skipped || elapsed_ >= length_) {
      playing_ = false;
      current_.clear();
    }
  }
  // 0x40cf8b after a movie ends, 0x40cd63 on a frame with none: checkMovie.
  check(load);
}

void MovieQueue::clear() {
  pending_.clear();
  playing_ = false;
  current_.clear();
}

}  // namespace obf2::engine
