#include "obf2/server/soldier_node.h"

#include <algorithm>
#include <cmath>

namespace obf2::server {
namespace {

// Components under this are zeroed by every node setter (0xb84754).
constexpr float kTiny = 1e-6f;

float lengthSquared(const Vec3f& v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

// The drag's clamp (0x6f1c5d..0x6f1ccb): a component is kept no larger than the
// speed's own — `min(d, v)` for a speed at or above zero, `max(d, v)` below.
float clampToSpeed(float d, float v) { return v >= 0.0f ? std::min(d, v) : std::max(d, v); }

}  // namespace

void stepSoldierNode(BodyState& body, float matrixYaw, const PhysicsConstants& physics,
                     float step) {
  // `updatePhysics` (0x6f2130) runs the drag first when the node has any (`+0x5c`).
  if (physics.drag > 0.0f) {
    // 0x6f1890. The node's rows: 0 right, 1 up, 2 forward; a zero yaw looks along
    // +Z, as the rest of our soldier code has it.
    const float radians = matrixYaw * (3.14159265358979323846f / 180.0f);
    const Vec3f right{std::cos(radians), 0.0f, -std::sin(radians)};
    const Vec3f up{0.0f, 1.0f, 0.0f};
    const Vec3f forward{std::sin(radians), 0.0f, std::cos(radians)};
    // w = speed * getDragMod(dry = 0) - wind; the wind is not read.
    const Vec3f w = body.velocity;
    const float k = -physics.drag * std::sqrt(lengthSquared(w)) / physics.mass;
    Vec3f d = right * (physics.dragRight * k * dot(w, right)) +
              up * (physics.dragUp * k * dot(w, up)) +
              forward * (physics.dragForward * k * dot(w, forward));
    // 1/30 (0xb2fcc0), the clamp, then back to an acceleration (* 30, 0xb2efc4).
    d = d * (1.0f / 30.0f);
    d.x = clampToSpeed(d.x, body.velocity.x);
    d.y = clampToSpeed(d.y, body.velocity.y);
    d.z = clampToSpeed(d.z, body.velocity.z);
    body.acceleration = body.acceleration + d * 30.0f;
  }

  // `updatePositionalPhysics` (0x6f1de0).
  const float accelerationSquared = lengthSquared(body.acceleration);
  if (accelerationSquared > 1e6f) {  // 0xb94234
    body.acceleration = body.acceleration * (1000.0f / std::sqrt(accelerationSquared));
  }
  if (lengthSquared(body.friction) > 62500.0f) body.friction = Vec3f{};  // 0xb94238

  body.velocity = body.velocity + body.linearSpeed;
  const Vec3f old = body.velocity;
  body.velocity = body.velocity + (body.friction + body.acceleration) * step;
  body.velocity = body.velocity * physics.positionalDamping;
  body.position = body.position + (old + body.velocity) * (0.5f * step);
  body.acceleration = Vec3f{};
  body.friction = Vec3f{};
  body.linearSpeed = Vec3f{};

  // Back in `updatePhysics`: gravity for the next step (0x6f21d8, the gravity
  // modifier `+0x64` taken as 1), and the friction contacts cleared (0x6f21e3).
  body.acceleration.y -= physics.gravity;
  body.frictionContacts = 0;
}

namespace {

// `setAdjust` (0x6dfd20).
void setAdjust(float& adjust, float value) {
  if (adjust == 0.0f) {
    adjust = value;
  } else if (adjust > 0.0f) {
    if (value > 0.0f) adjust = std::max(adjust, value);
    else adjust += value;
  } else {
    if (value < 0.0f) adjust = std::min(adjust, value);
    else adjust += value;
  }
}

void setAdjust(Vec3f& adjust, const Vec3f& value) {
  setAdjust(adjust.x, value.x);
  setAdjust(adjust.y, value.y);
  setAdjust(adjust.z, value.z);
}

// `addAdjustVector` (0x6dfef0): a running mean, components under 1e-6 zeroed.
void addAdjust(Vec3f& mean, const Vec3f& value, int count) {
  const float n = static_cast<float>(count);
  mean = (mean * n + value) * (1.0f / (n + 1.0f));
  if (std::abs(mean.x) < kTiny) mean.x = 0.0f;
  if (std::abs(mean.y) < kTiny) mean.y = 0.0f;
  if (std::abs(mean.z) < kTiny) mean.z = 0.0f;
}

// `Soldier_CollisionMesh`'s first five vertices (`BF2.exe` 0x707070), from the
// feet — the pivot's (0, -1, 0) and (±0.2, -0.6, ±0.2) moved up by the pivot height.
constexpr Vec3f kTerrainPoints[5] = {
    {0.0f, 0.0f, 0.0f},   {-0.2f, 0.4f, -0.2f}, {0.2f, 0.4f, -0.2f},
    {0.2f, 0.4f, 0.2f},   {-0.2f, 0.4f, 0.2f},
};

}  // namespace

void resetSoldierContacts(BodyState& body, const PhysicsConstants& physics) {
  body.onGround = false;
  body.contacts = 0;
  body.contactSpeed = Vec3f{};
  body.groundNormal = Vec3f{0.0f, physics.feetContactNormal, 0.0f};
}

void soldierImpulse(BodyState& body, float depth, const Vec3f& normal, bool feet) {
  setAdjust(body.positionAdjust, normal * (-depth * normal.y));
  const float nn = lengthSquared(normal);
  Vec3f speed{};
  if (std::abs(nn) > 1.1920929e-7f) speed = normal * (dot(body.velocity * -1.0f, normal) / nn);
  setAdjust(body.speedAdjust, speed);
  addAdjust(body.contactSpeed, body.velocity, body.contacts);
  ++body.contacts;
  if (feet && normal.y > body.groundNormal.y) {
    body.groundNormal = normal;
    body.onGround = true;
  }
}

void solveSoldierImpulse(BodyState& body) {
  body.position = body.position + body.positionAdjust;
  body.positionAdjust = Vec3f{};
  if (lengthSquared(body.speedAdjust) > 0.0f) {
    // One plus the elasticity (the materials' mean, 0 for `Human_body` and every
    // ground), times `phy-imp-mod` 1.0.
    body.linearSpeed = body.linearSpeed + body.speedAdjust;
  }
  body.speedAdjust = Vec3f{};
}

void soldierVsTerrain(BodyState& body, float yaw,
                      const std::function<TerrainSample(const Vec3f&)>& terrainAt) {
  const float radians = yaw * (3.14159265358979323846f / 180.0f);
  const Vec3f right{std::cos(radians), 0.0f, -std::sin(radians)};
  const Vec3f forward{std::sin(radians), 0.0f, std::cos(radians)};
  for (int i = 0; i < 5; ++i) {
    const Vec3f& p = kTerrainPoints[i];
    const Vec3f point = body.position + right * p.x + Vec3f{0.0f, p.y, 0.0f} + forward * p.z;
    const TerrainSample ground = terrainAt(point);
    const float depth = point.y - ground.height;
    if (depth > 0.0f) continue;
    soldierImpulse(body, depth, ground.normal, i == 0);
  }
}

void soldierFriction(BodyState& body, const PhysicsConstants& physics) {
  // 0x6f33c0: in the air (and not in water) the flags become 1 — sticking off —
  // and nothing else happens.
  if (!body.onGround) {
    body.sticking = false;
    return;
  }

  const Vec3f& n = body.groundNormal;
  const float ny2 = n.y * n.y;
  const float ny5 = ny2 * ny2 * n.y;
  // 7.2 (0xb95a48), 4.8 (0xb95a4c), 9.82 (0xb94c0c), over 30 (0xb2efc4).
  const float staticLimit = physics.groundFriction * 7.2f * 9.82f * ny5 / 30.0f;
  const float dynamicLimit = physics.groundFriction * 4.8f * 9.82f * ny5 / 30.0f;

  // rel = contacts - surface, and one tick of gravity down (0x6f34d7).
  Vec3f rel = body.contactSpeed - body.surfaceSpeed;
  rel.y += -physics.gravity / 30.0f;
  const float nn = lengthSquared(n);
  Vec3f normalPart{};
  if (std::abs(nn) > 1.1920929e-7f) normalPart = n * (dot(n, rel) / nn);  // 0xb2bfb0
  Vec3f t = (rel - normalPart) * -1.0f;

  // The resistance term (0x6f35be): only when the materials resist at all.
  if (physics.groundResistance > 0.0f) {
    body.acceleration = body.acceleration + t * physics.groundResistance;
  }

  const float tSquared = lengthSquared(t);
  const auto scaleTo = [&](float limit) {
    t = t * (std::abs(limit) / std::sqrt(lengthSquared(t)));
  };
  if (body.sticking) {
    // 0x6f364f.
    if (tSquared > staticLimit * staticLimit && tSquared > kTiny) {
      scaleTo(staticLimit);
      body.sticking = false;
      if (lengthSquared(t) > dynamicLimit * dynamicLimit && lengthSquared(t) > kTiny) {
        scaleTo(dynamicLimit);
      }
    }
  } else {
    // 0x6f37b6.
    if (tSquared > dynamicLimit * dynamicLimit && tSquared > kTiny) {
      scaleTo(dynamicLimit);
    } else {
      body.sticking = true;
    }
  }

  // `addFrictionAtAbsolutePosition` (0x6f1630): the mean over this tick's contacts.
  const float count = static_cast<float>(body.frictionContacts);
  body.friction = (body.friction * count + t * 30.0f) * (1.0f / (count + 1.0f));
  ++body.frictionContacts;

  // 0x6f3710.
  body.surfaceSpeed = Vec3f{};
  body.contactSpeed = Vec3f{};
}

}  // namespace obf2::server
