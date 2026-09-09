#include "obf2/server/soldier_move.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace obf2::server {

void moveSoldier(BodyState& body, SwimState& swim, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& physics, const level::Level* terrain,
                 const CollisionWorld* collision, float step) {
  // The ground is not only the terrain. The engine looks for support on objects
  // too, otherwise a roof or a staircase cannot be climbed. We take the higher of the two.
  float ground = terrain != nullptr ? terrain->groundHeightAt(body.position) : 0.0f;
  if (collision != nullptr) {
    // We start slightly above the feet so as to find a step in front as well.
    Vec3f from = body.position;
    from.y += physics.stepHeight();
    float surface = 0.0f;
    if (collision->groundHeight(from, physics.stepHeight() + 2.0f, physics.feetContactNormal,
                                &surface)) {
      if (surface > ground) ground = surface;
    }
  }

  // Water. The engine measures how deep the soldier is submerged, and from a
  // certain share of his height he floats up (`phy-soldier-start-float`), while he
  // stands on the bottom again from a different one (`stop-float`) — no jitter at the boundary.
  const float waterLevel = terrain != nullptr ? terrain->terrain.seaLevel : 0.0f;
  const float submersion =
      physics.standHeight > 0.0f ? (waterLevel - body.position.y) / physics.standHeight : 0.0f;
  if (swim.swimming) {
    if (submersion <= physics.stopFloat) swim.swimming = false;
  } else if (submersion >= physics.startFloat) {
    swim.swimming = true;
  }

  if (swim.swimming) {
    // Floating: gravity does not apply, the soldier stays near the surface, and
    // the speed is his own (`phy-soldier-swim-speed`).
    const float surface = waterLevel - physics.standHeight * physics.startFloat;
    body.position = body.position + wish * (physics.swimSpeed * step);
    body.position.y += (surface - body.position.y) * std::min(1.0f, step * 4.0f);
    body.velocity = Vec3f{};
    body.onGround = false;
  } else {
    stepSoldier(body, wish, maxSpeed, jump, physics, ground, step);
  }

  // Collision with walls: a soldier in BF2 is a column of spheres, not one sphere
  // at chest level (SoldierResponsePhysics::getSoldierHeight). That is exactly
  // why he can step onto a stair: below stepHeight we do not push at all, and
  // above it we test every sphere.
  if (collision == nullptr) return;
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
  if (length(offset) > 1e-4f) {
    body.position.x += offset.x;
    body.position.z += offset.z;
    const Vec3f direction = normalize(offset);
    const float into = dot(body.velocity, direction);
    if (into < 0.0f) body.velocity = body.velocity - direction * into;
  }
}

}  // namespace obf2::server
