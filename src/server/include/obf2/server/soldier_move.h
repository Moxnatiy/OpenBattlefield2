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
// counts as zero, with no collision there are no walls. directVelocity: wish is
// already the smoothed direction of `soldierMoveDirection` and becomes the
// velocity on the ground as it is (`BF2.exe` 0x5a7c50).
void moveSoldier(BodyState& body, SwimState& swim, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step, bool directVelocity = false);

// One tick of a soldier whose motion comes from the network rather than from
// input — another player seen from this client.
//
// The original does not draw the predicted point. `SoldierNetworkable::predict`
// (Linux server 0x5dc070) asks the soldier's physics node `getIsMobile()`
// (0x5dc15c) and seeds a mobile one with the newest update: its velocity through
// `setPositionalSpeed` (vtable 0xd0, which stores it at +0x2c capped at
// `g_maxSpeed` 1500, 0x6ddd80) and the predicted matrix through
// `setPrevTransformation` (vtable 0xa8, 0x6f2310). The node's own tick,
// `SoldierPhysicsNode::updatePositionalPhysics` (0x6f1de0), then damps that
// velocity by `p-pos-damp` and moves by the average of the old and the new one,
// and the soldier's ground and wall passes run after it — so a soldier whose
// update points down stops on the floor instead of running into it.
//
// That node step is the same one our own soldier takes (`stepSoldier` with
// `directVelocity`), with the network's velocity standing where the input's
// request stands: on the ground the node takes the velocity it was given, damped,
// either way. `feet` is the predicted position less `coll-soldier-pivot-height`.
//
// Not read: the forces the physics manager accumulates into the node (+0x38,
// +0xac); gravity in the air is the one `stepSoldier` already applies.
void carryRemoteSoldier(BodyState& body, SwimState& swim, const Vec3f& feet,
                        const Vec3f& velocity, const PhysicsConstants& physics,
                        const level::Level* terrain, const CollisionWorld* collision,
                        float step);

}  // namespace obf2::server
