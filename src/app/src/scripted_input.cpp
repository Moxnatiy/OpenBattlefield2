#include "obf2/app/scripted_input.h"

namespace obf2::app {

ScriptedFrame scriptedInput(const Args& args, int frame) {
  ScriptedFrame out;
  for (const auto& look : args.looks) {
    if (frame < look.frame || frame >= look.frame + look.frames) continue;
    out.mouseDeltaX += look.dx;
    out.mouseDeltaY += look.dy;
  }
  for (const auto& move : args.moves) {
    if (frame < move.frame || frame >= move.frame + move.frames) continue;
    out.hasMove = true;
    out.moveForward = move.dx;
    out.moveRight = move.dy;
    out.sprint = move.sprint != 0;
  }
  for (const int jumpFrame : args.jumps) {
    if (frame == jumpFrame) out.jump = true;
  }
  return out;
}

}  // namespace obf2::app
