#pragma once
// The input a run gives itself: a turn of a known size, a run, a jump — each on a
// frame named on the command line.
//
// These are measuring instruments, not a way to play: what the server's own yaw
// says after a `--look-at` is the measure of the look chain, and where it puts us
// after a `--move-at` is the measure of the movement. They ride on top of the
// player's real input, exactly as if a hand had done it.
#include <vector>

#include "obf2/app/command_line.h"

namespace obf2::app {

// One frame's scripted input, added to whatever the player pressed.
struct ScriptedFrame {
  float mouseDeltaX = 0.0f;
  float mouseDeltaY = 0.0f;
  bool hasMove = false;  // whether `--move-at` speaks for this frame
  float moveForward = 0.0f;
  float moveRight = 0.0f;
  bool sprint = false;
  bool jump = false;
};

// A range is `<frame>:<frames>:...`, so it covers `frame` to `frame + frames - 1`.
// Several ranges may overlap: the mouse deltas add up, and the last movement
// range named for the frame wins, the way a second key press replaces the first.
ScriptedFrame scriptedInput(const Args& args, int frame);

}  // namespace obf2::app
