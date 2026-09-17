#pragma once
// A soldier's physics node, and what its contacts leave in it.
//
//   `SoldierPhysicsNode::updatePhysics`            Linux server 0x6f2130
//   `SoldierPhysicsNode::updatePositionalDragAdvanced`          0x6f1890
//   `SoldierPhysicsNode::updatePositionalPhysics`               0x6f1de0
//   `SoldierResponsePhysics::addFriction`                        0x6f3390
//   `SoldierPhysicsNode::addFrictionAtAbsolutePosition`          0x6f1630
//
// Notes, whole: docs/functions/soldier-physics.md, "The physics node, whole" and
// "The ground contact and the friction".
#include <functional>

#include "obf2/core/math.h"
#include "obf2/server/physics.h"

namespace obf2::server {

// One `updatePhysics`: the drag into the acceleration, then the positional step
//
//   speed += linearSpeed; old = speed
//   speed += (friction + acceleration) * step; speed *= p-pos-damp
//   position += (old + speed) * 0.5 * step
//   acceleration = friction = linearSpeed = 0; acceleration.y = gravity
//
// `matrixYaw` is the yaw of the node's matrix, whose rows the drag is taken along.
// Not modelled: the share under water (`+0x68`) and the wind (`basicPhysicsSystem`
// vtable 0x50) — both zero on dry ground with no wind set.
void stepSoldierNode(BodyState& body, float matrixYaw, const PhysicsConstants& physics,
                     float step);

// `SoldierResponsePhysics::reset` (0x6f2580), at the start of the collision pass:
// not on the ground, no contacts, the ground normal `(0, feet-contact-normal, 0)`.
// The surface speed is kept.
void resetSoldierContacts(BodyState& body, const PhysicsConstants& physics);

// One contact, `internalImpulseOn` (0x6f26f0): `depth` is how far the contact
// point is under the surface along the vertical (not above zero), `normal` the
// surface's. It collects
//
//   positionAdjust  <- normal * (-depth * normal.y)          (0x6f27dd)
//   speedAdjust     <- -(dot(velocity, normal) / |normal|²) normal  (0x6f27ee)
//   contactSpeed     = the mean of the contacts' velocities   (0x6f2803)
//
// each adjustment component through `setAdjust` (0x6dfd20): an empty one takes
// the value, same signs keep the larger, opposite signs add. `feet` is the
// collision mesh's first point: that one, on a surface whose normal is steeper
// up than the ground's so far, becomes the ground (0x6f63ca..0x6f6433).
void soldierImpulse(BodyState& body, float depth, const Vec3f& normal, bool feet);

// `solveImpulse` (0x6f7690), its first half: the position takes the position
// adjustment (0x6f77b7), and a speed adjustment goes into the local linear speed
// through `addRelativeImpulse` (0x6f790b, 0x6de3d0) times one plus the materials'
// elasticity (0 for ours) and `phy-imp-mod` (1.0, 0x6f2d52). The collision
// test loop that follows for other objects (`coll-soldier-collision-test-count`)
// is not reversed.
void solveSoldierImpulse(BodyState& body);

// The terrain pass, `internal_checkVsTerrain(step, 1.0)` (0x6f5e30 via
// `checkVsTerrain` 0x6f67a0): the collision mesh's first five points against the
// terrain's height and normal (level.h, `groundContactAt`), each at or under it a
// contact. The points are the procedural `Soldier_CollisionMesh` (`BF2.exe`
// 0x707070): relative to the pivot (0, -1, 0) and (±0.2, -0.6, ±0.2) — to the feet
// (0, 0, 0) and (±0.2, 0.4, ±0.2) — turned by the body's yaw. `feet` is the body's
// position. Not read: the water half of the same function (0x6f65ba).
struct TerrainSample {
  float height = 0.0f;
  Vec3f normal{0.0f, 1.0f, 0.0f};
};
void soldierVsTerrain(BodyState& body, float yaw,
                      const std::function<TerrainSample(const Vec3f&)>& terrainAt);

// `addFriction`: on the ground, the surface speed the input asked for, less the
// contacts' velocity, along the ground, becomes the node's friction for the next
// step — limited by the materials' friction (`dynamic`: 4.8, `static` while
// sticking: 7.2, times 9.82 and the normal's y to the fifth, over 30), with the
// resistance's share of it added to the acceleration. The surface speed and the
// contacts are cleared after. In the air nothing is done and the surface speed
// stays: a landing meets the speed asked for before the jump.
void soldierFriction(BodyState& body, const PhysicsConstants& physics);

}  // namespace obf2::server
