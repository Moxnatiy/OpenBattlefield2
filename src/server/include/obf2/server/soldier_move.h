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

// Our own local server's step (the hosted game): wish is the desired direction in
// the plane, of length 0..1. terrain and collision may be empty: with no terrain
// the ground counts as zero, with no collision there are no walls. Not the engine's
// order — that is `tickSoldier`.
void moveSoldier(BodyState& body, SwimState& swim, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step);

// What one action asks of the soldier's movement.
struct SoldierIntent {
  // The smoothed axes along the movement matrix — `soldierMoveDirection`.
  Vec3f wish;
  // The movement matrix's rows 2 and 0, flattened and normalised (0x54fa44): the
  // jump keeps the speed along them.
  Vec3f forward{0.0f, 0.0f, 1.0f};
  Vec3f right{1.0f, 0.0f, 0.0f};
  // `getSoldierSpeed(7)`, the speed state's speed.
  float speed = 0.0f;
  // The action's jump axis (mask 0x200).
  bool jump = false;
};

// The movement half of `Soldier::handlePlayerInput` (Linux 0x54f160): the air
// timers, the jump, and `updateSoldierSpeed` — the surface speed on the ground,
// steering in the air. Returns whether the soldier jumped; the stamina the jump
// costs (`SprintLossAtJump`, 0x54fe1d) is the caller's, which holds the sprint.
// docs/functions/soldier-physics.md, "The jump" and "In the air".
bool soldierInput(BodyState& body, const SoldierIntent& intent, const PhysicsConstants& physics,
                  float step);

// One engine tick of our own soldier, in `GameServer::simulateFrame`'s order
// (Linux 0x45af70): the input, the physics node (`stepSoldierNode`), the collision
// and its friction; then the delays count down. `matrixYaw` is the yaw the node's
// matrix holds, for the drag. Returns whether the soldier jumped.
bool tickSoldier(BodyState& body, SwimState& swim, const SoldierIntent& intent, float matrixYaw,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step);

// One tick of a soldier whose motion comes from the network rather than from
// input — another player seen from this client.
//
// The original does not draw the predicted point. `SoldierNetworkable::predict`
// (Linux server 0x5dc070) asks the soldier's physics node `getIsMobile()`
// (0x5dc15c) and seeds a mobile one with the newest update: its velocity through
// `setPositionalSpeed` (vtable 0xd0, which stores it at +0x2c capped at
// `g_maxSpeed` 1500, 0x6ddd80) and the predicted matrix through
// `setPrevTransformation` (vtable 0xa8, 0x6f2310). The node's own tick then runs,
// and the soldier's ground and wall passes after it — so a soldier whose update
// points down stops on the floor instead of running into it.
//
// `feet` is the predicted position less `coll-soldier-pivot-height`; `yaw` his
// body's yaw, for the drag.
void carryRemoteSoldier(BodyState& body, SwimState& swim, const Vec3f& feet,
                        const Vec3f& velocity, float yaw, const PhysicsConstants& physics,
                        const level::Level* terrain, const CollisionWorld* collision,
                        float step);

}  // namespace obf2::server
