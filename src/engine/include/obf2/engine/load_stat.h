#pragma once
// How far a level's load has come, the way the engine counts it.
//
// The engine's LoadStat (BF2.exe: the singleton at 0x9a60a8; B2BF's
// reconstruction names it and its setNextLoadPart, to be read at its address
// before more is relied on) splits a load into parts. Each part names itself,
// says how many steps it has and how much it weighs; the window it fills runs
// from where the last one ended to that plus 0.15 × weight, never past 1. The
// percent the loading screen shows is the window's start plus the share of its
// steps done (the engine's load callback):
//
//     complete            → 1
//     steps of the part   → start + done/steps × (end − start)   (done clamped to steps)
//     no steps            → start
//
// The parts and their weights are the engine's own business; ours are the
// phases of our loader (main.cpp), named here and weighted 1 each — **not the
// engine's parts, so our bar does not move as the original's does**.
#include <string>

namespace obf2::engine {

class LoadStat {
 public:
  // A new part: the window moves on by 0.15 × weight; the step count restarts.
  // A part with no steps changes nothing (the engine returns early).
  void nextPart(const std::string& name, int steps, float weight);
  // `count` more steps of the current part done.
  void step(int count = 1);
  // The file being read now, for the screen's caption (LoadStat::setCurrentFile).
  void setCurrentFile(const std::string& file) { currentFile_ = file; }
  // The load has finished: the last callback sees 100 %.
  void complete() { complete_ = true; }
  void reset();

  float progress() const;  // 0..1
  const std::string& currentFile() const { return currentFile_; }
  const std::string& partName() const { return partName_; }
  bool isComplete() const { return complete_; }

 private:
  int done_ = 0;
  int steps_ = 0;
  float start_ = 0.0f;
  float end_ = 0.0f;
  bool complete_ = false;
  std::string partName_;
  std::string currentFile_;
};

}  // namespace obf2::engine
