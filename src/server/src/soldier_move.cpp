#include "obf2/server/soldier_move.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "obf2/server/soldier_node.h"

namespace obf2::server {
namespace {

// The ground is not only the terrain. The engine looks for support on objects
// too, otherwise a roof or a staircase cannot be climbed. We take the higher of the two.
float groundUnder(const Vec3f& position, const PhysicsConstants& physics,
                  const level::Level* terrain, const CollisionWorld* collision) {
  float ground = terrain != nullptr ? terrain->groundHeightAt(position) : 0.0f;
  if (collision != nullptr) {
    // We start slightly above the feet so as to find a step in front as well.
    Vec3f from = position;
    from.y += physics.stepHeight();
    float surface = 0.0f;
    if (collision->groundHeight(from, physics.stepHeight() + 2.0f, physics.feetContactNormal,
                                &surface)) {
      if (surface > ground) ground = surface;
    }
  }
  return ground;
}

// Water. The engine measures how deep the soldier is submerged, and from a
// certain share of his height he floats up (`phy-soldier-start-float`), while he
// stands on the bottom again from a different one (`stop-float`) — no jitter at the boundary.
bool updateSwimming(const BodyState& body, SwimState& swim, const PhysicsConstants& physics,
                    const level::Level* terrain) {
  const float waterLevel = terrain != nullptr ? terrain->terrain.seaLevel : 0.0f;
  const float submersion =
      physics.standHeight > 0.0f ? (waterLevel - body.position.y) / physics.standHeight : 0.0f;
  if (swim.swimming) {
    if (submersion <= physics.stopFloat) swim.swimming = false;
  } else if (submersion >= physics.startFloat) {
    swim.swimming = true;
  }
  return swim.swimming;
}

// Floating: gravity does not apply, the soldier stays near the surface, and the
// speed is his own (`phy-soldier-swim-speed`). Not reversed.
void floatSoldier(BodyState& body, const Vec3f& wish, const PhysicsConstants& physics,
                  const level::Level* terrain, float step) {
  const float waterLevel = terrain != nullptr ? terrain->terrain.seaLevel : 0.0f;
  const float surface = waterLevel - physics.standHeight * physics.startFloat;
  body.position = body.position + wish * (physics.swimSpeed * step);
  body.position.y += (surface - body.position.y) * std::min(1.0f, step * 4.0f);
  body.velocity = Vec3f{};
  body.onGround = false;
}

// Collision with walls: a soldier in BF2 is a column of spheres, not one sphere
// at chest level (SoldierResponsePhysics::getSoldierHeight). That is exactly
// why he can step onto a stair: below stepHeight we do not push at all, and
// above it we test every sphere. Returns the push, zero when there was none.
Vec3f pushOutOfWalls(BodyState& body, const PhysicsConstants& physics,
                     const CollisionWorld* collision) {
  if (collision == nullptr) return Vec3f{};
  const std::vector<float> centers = soldierSphereHeights(physics);
  Vec3f offset{};
  for (const float center : centers) {
    if (center < physics.stepHeight()) continue;
    Vec3f probe = body.position + offset;
    probe.y += center;
    const Vec3f before = probe;
    if (collision->resolveSphere(probe, physics.radius) > 0) {
      offset.x += probe.x - before.x;
      offset.z += probe.z - before.z;
    }
  }
  if (length(offset) <= 1e-4f) return Vec3f{};
  body.position.x += offset.x;
  body.position.z += offset.z;
  return offset;
}

// The collision pass of the engine's tick (`GameServer::simulatePlayersCollisions`,
// Linux 0x4543e0) as `ResponsePhysicsManager::updateAllObjects` runs it for a
// soldier: the objects (`checkSoldierObjectVsObjects`, 0x6edc92), then the terrain
// (`checkVsTerrain`, response vtable 0x58, 0x6edca3), then the impulse and the
// friction (vtable 0x68, 0x70). `previous` is where the tick began (the node's
// previous transformation).
void collideSoldier(BodyState& body, const Vec3f& previous, float yaw,
                    const PhysicsConstants& physics, const level::Level* terrain,
                    const CollisionWorld* collision) {
  resetSoldierContacts(body, physics);
  if (collision != nullptr) soldierVsMeshes(body, previous, physics, *collision);
  // With no terrain the ground is the level plane at zero.
  soldierVsTerrain(body, yaw, [terrain](const Vec3f& point) {
    TerrainSample sample;
    if (terrain != nullptr) {
      const auto contact = terrain->groundContactAt(point);
      sample.height = contact.height;
      sample.normal = contact.normal;
    }
    return sample;
  });
  solveSoldierImpulse(body);
  soldierFriction(body, physics);
}

// `Soldier::handleFrameUpdate` (Linux 0x548f1d..0x548fd8): each delay counts down
// only while above zero.
void countDown(float& delay, float step) {
  if (delay > 0.0f) delay -= step;
}

}  // namespace

void moveSoldier(BodyState& body, SwimState& swim, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step) {
  const float ground = groundUnder(body.position, physics, terrain, collision);
  if (updateSwimming(body, swim, physics, terrain)) {
    floatSoldier(body, wish, physics, terrain, step);
  } else {
    stepSoldier(body, wish, maxSpeed, jump, physics, ground, step);
  }
  const Vec3f offset = pushOutOfWalls(body, physics, collision);
  if (length(offset) > 1e-4f) {
    const Vec3f direction = normalize(offset);
    const float into = dot(body.velocity, direction);
    if (into < 0.0f) body.velocity = body.velocity - direction * into;
  }
}

bool soldierInput(BodyState& body, const SoldierIntent& intent, const PhysicsConstants& physics,
                  float step) {
  // 0x54f1b1: the air timers count down to -1 (0xb2f3c0).
  const auto countToMinusOne = [step](float& timer) {
    if (timer > 0.0f) {
      timer -= step;
      if (timer <= 0.0f) timer = -1.0f;
    }
  };
  countToMinusOne(body.noAirControl);
  countToMinusOne(body.airControl);

  // 0x54f457..0x54f747: the jump's axis, not while the prone delay runs.
  bool jumped = false;
  if (body.onGround && intent.jump && body.jumpDelay <= 0.0f) {
    // 0x54fbaa..0x54fe1d.
    body.fireDelay = physics.fireDelayAfterJump;
    body.proneDelay = physics.proneDelayAfterJump;
    body.sprintRechargeDelay = physics.sprintRechargeDelayAfterJump;
    body.airControl = 2.0f;
    if (body.groundNormal.y < 0.8f) body.noAirControl = 0.5f;  // 0xb34b6c

    const Vec3f& v = body.velocity;
    Vec3f jumpSpeed = intent.forward * (dot(v, intent.forward) * physics.jumpLengthFactor) +
                      intent.right * (dot(v, intent.right) * physics.jumpLengthFactor);
    jumpSpeed.y = 0.0f;
    const float limit = intent.speed * physics.jumpLengthFactor;
    const float along = length(jumpSpeed);
    if (along > limit && along > 0.0f) jumpSpeed = jumpSpeed * (limit / along);
    // 6.0 (0xb355c8) times `phy-soldier-jump-factor`.
    jumpSpeed.y = physics.jumpSpeed * physics.jumpFactor;

    // `setLocalLinearSpeed(0)`, `setPositionalAcceleration(J)`, `setPositionalSpeed(J)`.
    body.linearSpeed = Vec3f{};
    body.acceleration = jumpSpeed;
    body.velocity = jumpSpeed;
    jumped = true;
  }

  if (body.onGround) {
    // `updateSoldierSpeed(false, ...)`, 0x54f043: the surface speed.
    const float speed = intent.speed * physics.speedFactor;
    body.surfaceSpeed = Vec3f{intent.wish.x * speed, intent.wish.y * speed, intent.wish.z * speed};
    return jumped;
  }

  // `updateSoldierSpeed(true, ...)`, 0x54f0a5 and 0x54eec0: steering in the air.
  if (body.noAirControl < 0.0f && body.airControl > 0.0f) {
    const float strength = body.airControl * 0.5f * physics.inAirSpeed;
    const float before = std::sqrt(body.velocity.x * body.velocity.x +
                                   body.velocity.z * body.velocity.z);
    body.velocity.x += intent.wish.x * strength * physics.speedFactor;
    body.velocity.z += intent.wish.z * strength * physics.speedFactor;
    const float after = std::sqrt(body.velocity.x * body.velocity.x +
                                  body.velocity.z * body.velocity.z);
    if (before > strength && after > before) {
      body.velocity.x *= before / after;
      body.velocity.z *= before / after;
    }
  }
  return jumped;
}

bool tickSoldier(BodyState& body, SwimState& swim, const SoldierIntent& intent, float matrixYaw,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step) {
  if (updateSwimming(body, swim, physics, terrain)) {
    floatSoldier(body, intent.wish, physics, terrain, step);
    return false;
  }
  // `GameServer::simulateFrame` (0x45af70): the input, the node, the collision.
  const bool jumped = soldierInput(body, intent, physics, step);
  const Vec3f previous = body.position;
  stepSoldierNode(body, matrixYaw, physics, step);
  collideSoldier(body, previous, matrixYaw, physics, terrain, collision);
  countDown(body.fireDelay, step);
  countDown(body.proneDelay, step);
  countDown(body.jumpDelay, step);
  countDown(body.sprintRechargeDelay, step);
  return jumped;
}

void carryRemoteSoldier(BodyState& body, SwimState& swim, const Vec3f& feet,
                        const Vec3f& velocity, float yaw, const PhysicsConstants& physics,
                        const level::Level* terrain, const CollisionWorld* collision,
                        float step) {
  // `setPrevTransformation` and `setPositionalSpeed`: where and how fast the
  // network says (0x5dc314, 0x6f2310, 0x6ddd80).
  body.position = feet;
  body.velocity = velocity;
  if (updateSwimming(body, swim, physics, terrain)) {
    floatSoldier(body, Vec3f{}, physics, terrain, step);
    return;
  }
  stepSoldierNode(body, yaw, physics, step);
  // No input runs for another player's soldier, so nothing sets a surface speed,
  // and the friction would stop him against the velocity `predict` just handed
  // the node. What surface speed the original's ghost has is not established;
  // ours is the network's velocity along the ground.
  body.surfaceSpeed = Vec3f{velocity.x, 0.0f, velocity.z};
  collideSoldier(body, feet, yaw, physics, terrain, collision);
}

}  // namespace obf2::server
