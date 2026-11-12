#pragma once
// The startup movies: the queue `bf2Engine.playMovie` fills and the main loop plays.
//
// Read in `BF2.exe` (the addresses are the client's; B2BF/OpenBF2's notes on the
// same functions agreed, and every point was checked against the binary):
//
//   * `BF2Engine::playMovie` 0x407a60 appends (name, flag) to the list at
//     engine+0xF8, unless the setting `skipMovies` is on or the engine is a
//     dedicated server (0x440510). Nothing else gates it — `GeneralSettings`'
//     `viewIntroMovie` (its getter 0x4b7280, slot +0x78) is never called through
//     the settings global 0x987390 in the client; the menu shows it as a box to tick.
//   * The queue is filled by the mod's `Init.con`, five lines:
//     EA, dice, legal with flag 1, intro and welcome with flag 0.
//   * `checkMovie` 0x407cb0: nothing when `skipMovies` is on, a movie plays, or
//     the list is empty. Otherwise it takes the head, keeps its flag at engine+0x104,
//     and creates `dice.hfe.geom.FSMoviePlayer`; when that fails to load the file it
//     is released and the next one is tried, until the list runs out.
//   * The main loop (0x40ca80) calls `checkMovie` every frame while nothing plays
//     (0x40cd58), and while a movie plays (0x40cee9..0x40cf8b) it draws a frame and
//     ends the movie when it has finished (player +0x1C) **or** the key IDKey_Escape
//     (1, 0x6b40be) is pressed **and** the movie's flag is not 1. So the publisher's
//     and the legal movies cannot be skipped; the intro and welcome can, and
//     nothing but Escape skips them.
//   * `+restart` on the command line sets `skipMovies` (B2BF/OpenBF2's reading of
//     the argument table; not yet checked here, so it is not wired).
//
// We do not decode Bink, so a "loaded" movie is one whose file opens and whose
// header reads; it has "finished" when its own length (frames over rate, from the
// header) has gone by.
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace obf2::engine {

struct QueuedMovie {
  std::string name;  // as `Init.con` wrote it: `Movies\EA.bik`
  int flag = 0;      // 1: cannot be skipped
};

class MovieQueue {
 public:
  // Opens a movie by its name; its length in seconds, or nullopt when it cannot be
  // opened — what `FSMoviePlayer::load` failing stands for.
  using Loader = std::function<std::optional<double>(const std::string& name)>;

  // The setting `skipMovies`.
  void setSkipMovies(bool skip) { skipMovies_ = skip; }
  bool skipMovies() const { return skipMovies_; }

  // `bf2Engine.playMovie <name> <flag>` (0x407a60).
  void play(std::string name, int flag);

  // `checkMovie` (0x407cb0): start the next movie that opens, if none plays.
  void check(const Loader& load);

  // One frame of the main loop's movie arm (0x40cee9). Ends the playing movie when
  // its time is up or Escape is pressed on a skippable one, then checks the queue.
  void frame(double seconds, bool escapePressed, const Loader& load);

  // Drop the queue and the playing movie: `--screen menu` and other measuring runs
  // that want the menu at once. Not an engine path.
  void clear();

  bool playing() const { return playing_; }
  const std::string& current() const { return current_; }
  int currentFlag() const { return flag_; }
  double elapsed() const { return elapsed_; }
  double length() const { return length_; }
  const std::vector<QueuedMovie>& pending() const { return pending_; }
  // Every movie ever queued, in order — for the startup report.
  const std::vector<QueuedMovie>& queued() const { return queued_; }

 private:
  bool skipMovies_ = false;
  std::vector<QueuedMovie> pending_;
  std::vector<QueuedMovie> queued_;
  bool playing_ = false;
  std::string current_;
  int flag_ = 0;
  double elapsed_ = 0.0;
  double length_ = 0.0;
};

}  // namespace obf2::engine
