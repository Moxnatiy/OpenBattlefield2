#pragma once
// One step of a soldier's movement — whole: ground, water, gravity, walls.
//
// This is the same action the engine performs on both the server and the client:
// the server moves the body, the client predicts its own soldier's movement with
// the same step and the same constants (`PlayerControlObjectNetworkable` lives
// on the client, while the soldier's physics in `SoldierResponsePhysics` is shared).
//
// So here too it is **one** function, not two similar ones. When it was
// duplicated, the client's half knew only the ground's height — and the soldier
// walked through walls on a real server, even though he did not in our own game.
#include "obf2/core/math.h"
#include "obf2/level/level.h"
#include "obf2/server/collision_world.h"
#include "obf2/server/physics.h"

namespace obf2::server {

// The engine's simulation tick. Not a "customary" constant but a number from the
// binary: `dice::hfe::WorldPref::mTickTime` lies in the Linux server's `.data` at
// 0xf68c50 and equals exactly 0.0333333333333333 (double), that is 1/30 s.
// It is read by `WorldPref::getTickTime` (0x74aa60).
//
// The engine computes physics in **whole** ticks only. So our movement step
// cannot be a frame's length either: otherwise a jump at 120 frames comes out
// different from one at 60 — which is exactly what was visible.
inline constexpr float kTickTime = 1.0f / 30.0f;

// The state that outlives a step and does not belong to the body itself.
struct SwimState {
  bool swimming = false;
};

// The time accumulated between ticks. A frame rarely equals a tick, so the
// remainder carries over to the next frame rather than being lost or lengthening the step.
struct TickAccumulator {
  float pending = 0.0f;

  // How many whole ticks matured over `elapsed`. A long pause (loading, the
  // window being dragged) must not wind up hundreds of ticks in one frame.
  int take(float elapsed, int limit = 8) {
    pending += elapsed;
    int ticks = 0;
    while (pending >= kTickTime && ticks < limit) {
      pending -= kTickTime;
      ++ticks;
    }
    if (ticks == limit) pending = 0.0f;
    return ticks;
  }
};

// wish is the desired direction in the plane (already rotated by the look angle),
// of length 0..1. terrain and collision may be empty: with no terrain the ground
// counts as zero, with no collision there are no walls.
void moveSoldier(BodyState& body, SwimState& swim, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step);

}  // namespace obf2::server
