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
  for (const auto& select : args.selects) {
    if (frame == select.frame) out.weaponKeys |= 1u << (select.key - 1);
  }
  for (const auto& zoom : args.zooms) {
    if (frame >= zoom.frame && frame < zoom.frame + zoom.frames) out.altFire = true;
  }
  for (const auto& hold : args.crouches) {
    if (frame >= hold.frame && frame < hold.frame + hold.frames) out.crouch = true;
  }
  for (const auto& hold : args.prones) {
    if (frame >= hold.frame && frame < hold.frame + hold.frames) out.prone = true;
  }
  for (const int reloadFrame : args.reloads) {
    if (frame == reloadFrame) out.reload = true;
  }
  for (const auto& fire : args.fires) {
    if (frame >= fire.frame && frame < fire.frame + fire.frames) out.fire = true;
  }
  for (const auto& use : args.uses) {
    if (frame >= use.frame && frame < use.frame + use.frames) out.use = true;
  }
  for (const auto& seat : args.seats) {
    if (frame == seat.frame) out.seatKeys |= 1u << (seat.key - 1);
  }
  for (const int cameraFrame : args.cameraKeys) {
    if (frame == cameraFrame) out.camera = true;
  }
  return out;
}

}  // namespace obf2::app
