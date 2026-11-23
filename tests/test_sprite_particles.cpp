#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

#include "check.h"
#include "obf2/app/sprite_particles.h"

using namespace obf2;
using namespace obf2::app::particles;

namespace {

// `em_exp_grenade_fireflash`
// (Objects_client.zip:Effects/impacts/explosions/e_exp_grenade/e_exp_grenade.tweak).
SpriteTemplate fireflash() {
  SpriteTemplate t;
  t.emitFrequency = 22;
  t.emitTime = 0.15f;
  t.emitDelay = 0.11f;
  t.preWarmTime = 0.2f;
  t.emitSpeed = 1;
  t.timeToLive = 0.2f;
  t.randomTimeToLive = 0.05f;
  t.gravity = 1;
  t.airResistance = 0.2f;
  t.randomSize = 0.2f;
  t.particleMaxSize = 3.3f;
  t.sizeGraph.c = {0, 0.75f, -1.59f, 1};
  t.transparencyGraph.c = {0.69f, -0.7f, -0.55f, 0.55f};
  t.randomDirectionAngle = {15, 0, 15};
  return t;
}

// Emission subtracts one interval from the emit time before each particle and
// stops at ≤ 0 (RendDX9.dll 0x10180790): 0.15 s at 22/s is three particles,
// and none before the 0.11 s delay.
void testEmissionCount() {
  const auto t = fireflash();
  SpriteEmitter emitter(t, 1);
  std::size_t most = 0;
  float clock = 0;
  bool beforeDelay = true;
  for (int frame = 0; frame < 120; ++frame) {
    emitter.update(1.0f / 60.0f, {});
    clock += 1.0f / 60.0f;
    if (clock < 0.11f && !emitter.particles().empty()) beforeDelay = false;
    most = std::max(most, emitter.particles().size());
  }
  CHECK(beforeDelay);
  CHECK_EQ(most, std::size_t{3});
  CHECK(emitter.finished());
}

// Pre-warm (0x10180f23) spends preWarmTime when the delay runs out, so all
// three particles appear together, already aged. Retail measured 0.16, 0.12
// and 0.07 s at 0.10 s (decal-ab-20261122/retail-corrugated-grenade-23).
void testPreWarmAges() {
  const auto t = fireflash();
  SpriteEmitter emitter(t, 1);
  float clock = 0;
  while (emitter.particles().empty() && clock < 1) {
    emitter.update(1.0f / 60.0f, {});
    clock += 1.0f / 60.0f;
  }
  CHECK(clock < 0.13f);
  CHECK_EQ(emitter.particles().size(), std::size_t{3});
  std::vector<float> ages;
  for (const auto& p : emitter.particles()) ages.push_back(p.age);
  std::sort(ages.begin(), ages.end());
  // One emission interval (1/22 s) apart; the oldest is within the 0.2 s
  // pre-warm plus the frame in which the delay ran out.
  CHECK(std::abs(ages[1] - ages[0] - 1.0f / 22.0f) < 0.005f);
  CHECK(std::abs(ages[2] - ages[1] - 1.0f / 22.0f) < 0.005f);
  CHECK(ages[2] > 0.15f && ages[2] < 0.2f);
}

// Positive gravity accelerates upwards (0x1017f520, dv.y += g·h·gravity).
void testGravityRises() {
  SpriteTemplate t;
  t.emitFrequency = 1;
  // One interval of 1 s fits in 1.5 s of emit time: one particle, at 1 s.
  t.emitTime = 1.5f;
  t.timeToLive = 5;
  t.gravity = 2;
  SpriteEmitter emitter(t, 7);
  for (int frame = 0; frame < 90; ++frame) emitter.update(1.0f / 60.0f, {});
  CHECK(!emitter.particles().empty());
  const auto& p = emitter.particles().front();
  CHECK(p.velocity.y > 0.5f);
  CHECK(p.position.y > 0);
}

// Quadratic air resistance: dv = −v·|v|·k·h, so v(t) = v0 / (1 + k·v0·t).
void testQuadraticDrag() {
  SpriteTemplate t;
  t.emitFrequency = 1;
  t.emitTime = 1.5f;
  t.timeToLive = 5;
  t.emitSpeed = 10;
  t.airResistance = 0.5f;
  SpriteEmitter emitter(t, 3);
  emitter.update(2.0f, {});  // the particle is born at 1 s and integrated 1 s ahead
  CHECK(!emitter.particles().empty());
  const float speed = length(emitter.particles().front().velocity);
  CHECK(std::abs(speed - 10.0f / (1.0f + 0.5f * 10.0f * 1.0f)) < 0.2f);
}

void testRandomRanges() {
  Random random(12345);
  float low = 1, high = -1;
  for (int i = 0; i < 20000; ++i) {
    const float s = random.symmetric();
    const float u = random.unit();
    CHECK(s >= -1.0f && s < 1.0f);
    CHECK(u > 0.0f && u <= 1.0f);
    low = std::min(low, s);
    high = std::max(high, s);
  }
  CHECK(low < -0.99f);
  CHECK(high > 0.99f);
}

// Graphs are a·t³ + b·t² + c·t + d; the shader caps at 1 only.
void testDrawGraphs() {
  auto t = fireflash();
  t.emitDelay = 0;
  SpriteEmitter emitter(t, 9);
  emitter.update(0.05f, {});
  std::vector<SpriteDraw> sprites;
  emitter.collect(0.05f, sprites);
  CHECK(!sprites.empty());
  for (const auto& s : sprites) {
    CHECK(s.alpha <= 1.0f);
    CHECK(s.halfSize <= 3.3f + 0.2f);
  }
}

// One particle born at 1 s, thrown with `speed` along `direction`.
SpriteTemplate single(float speed, Vec3f direction = {0, 1, 0}) {
  SpriteTemplate t;
  t.emitFrequency = 1;
  t.emitTime = 1.5f;
  t.timeToLive = 10;
  t.emitSpeed = speed;
  t.emitDirection = direction;
  return t;
}

// Drag acts on velocity − wind·windAffectionFactor (0x1017f520): a resting
// particle in a 4 m/s wind at factor 0.5 drifts towards 2 m/s.
void testWind() {
  auto t = single(0);
  t.airResistance = 5;
  t.windAffectionFactor = 0.5f;
  World world;
  world.wind = [](Vec3f) { return Vec3f{4, 0, 0}; };
  SpriteEmitter emitter(t, 2);
  emitter.setWorld(&world);
  for (int i = 0; i < 300; ++i) emitter.update(1.0f / 60.0f, {});
  CHECK(!emitter.particles().empty());
  CHECK(std::abs(emitter.particles().front().velocity.x - 2.0f) < 0.1f);
  // A factor of 0 ignores the wind.
  t.windAffectionFactor = 0;
  SpriteEmitter calm(t, 2);
  calm.setWorld(&world);
  for (int i = 0; i < 300; ++i) calm.update(1.0f / 60.0f, {});
  CHECK(calm.particles().front().velocity.x == 0.0f);
}

// A falling particle is put back on the plane, its normal velocity reversed
// times collisionSpeedAlongNormal and the rest scaled by ...AlongPlane.
void testCollision() {
  auto t = single(5, normalize(Vec3f{1, -1, 0}));
  t.collisionEnable = true;
  t.collisionSpeedAlongNormal = 0.5f;
  t.collisionSpeedAlongPlane = 0.8f;
  SpriteEmitter emitter(t, 4);
  emitter.setCollisionPlane({{0, 1, 0}, 0.0f});  // y = 0
  bool bounced = false;
  for (int i = 0; i < 120; ++i) {
    emitter.update(1.0f / 60.0f, {0, 0.05f, 0});
    for (const auto& p : emitter.particles()) {
      CHECK(p.position.y >= -1e-4f);
      if (p.velocity.y > 0) {
        bounced = true;
        // One impact: vy = 0.5·|vy|, vx = 0.8·vx, from (3.54, −3.54) at once.
        CHECK(p.velocity.x < 3.6f);
      }
    }
  }
  CHECK(bounced);
  // The setter normalizes the normal and keeps d (0x101800e0): (0, 2, 0), −0.2
  // becomes n = (0, 1, 0), d = −0.2 — the plane y = 0.2.
  SpriteEmitter raised(t, 4);
  raised.setCollisionPlane({{0, 2, 0}, -0.2f});
  for (int i = 0; i < 120; ++i) {
    raised.update(1.0f / 60.0f, {0, 0.25f, 0});
    for (const auto& p : raised.particles()) CHECK(p.position.y >= 0.2f - 1e-4f);
  }
  // With no plane set, the ctor's (0, 1, 0, 0) in the current slot and the
  // whole ring (0x1017fb00) holds every particle, young or old, above y = 0.
  SpriteEmitter fresh(t, 4);
  for (int i = 0; i < 120; ++i) {
    fresh.update(1.0f / 60.0f, {0, 0.05f, 0});
    for (const auto& p : fresh.particles()) CHECK(p.position.y >= -1e-4f);
  }
}

// The plane a particle collides with is chosen by its age against the ring
// clock: young particles see the current plane.
void testCollisionRingAge() {
  auto t = single(0);
  t.gravity = -9.8f;
  t.collisionEnable = true;
  SpriteEmitter emitter(t, 5);
  emitter.setCollisionPlane({{0, 1, 0}, 0.0f});
  for (int i = 0; i < 90; ++i) emitter.update(1.0f / 60.0f, {0, 0.5f, 0});
  // Born at 1 s, 0.5 s old: older than the ring clock, so a ring plane,
  // pushed at 1/3 s and 2/3 s — the same plane here.
  CHECK(!emitter.particles().empty());
  CHECK(emitter.particles().front().position.y >= -1e-4f);
}

// forceToWaterSurface moves only the drawn copy to the water height.
void testWaterSurface() {
  auto t = single(1);
  t.forceToWaterSurface = true;
  World world;
  world.waterHeight = [](Vec3f) { return 3.0f; };
  SpriteEmitter emitter(t, 6);
  emitter.setWorld(&world);
  for (int i = 0; i < 70; ++i) emitter.update(1.0f / 60.0f, {});
  const auto& p = emitter.particles().front();
  CHECK(p.drawPosition.y == 3.0f);
  CHECK(p.position.y != 3.0f);
  std::vector<SpriteDraw> sprites;
  t.transparencyGraph.c = {0, 0, 0, 1};
  emitter.collect(0, sprites);
  CHECK(!sprites.empty() && sprites.front().position.y == 3.0f);
}

// The rotation table: 4096 steps, sin and cos ·32767 truncated (0x10171a58).
void testQuantizedRotation() {
  const auto zero = quantizedRotation(0);
  CHECK(zero.sin == 0.0f && zero.cos == 1.0f);
  const auto quarter = quantizedRotation(std::numbers::pi_v<float> / 2);
  CHECK(std::abs(quarter.sin - 1.0f) < 1e-4f && std::abs(quarter.cos) < 1e-3f);
  // A full turn wraps to index 0 (& 0xfff), a negative angle to the top.
  CHECK(quantizedRotation(2 * std::numbers::pi_v<float>).sin == 0.0f);
  CHECK(quantizedRotation(-0.5f).sin < 0.0f);
}

// alignRotationToSpeed: a particle moving right on screen gets π/2, moving
// up 0 (acos of the screen cosine, 0x10172450).
void testAlignRotation() {
  View view{lookAt({0, 0, -10}, {0, 0, 0}, {0, 1, 0}), 0.1f, 0, {}};
  for (const auto& [direction, expected] :
       {std::pair{Vec3f{1, 0, 0}, std::numbers::pi_v<float> / 2}, std::pair{Vec3f{0, 1, 0}, 2 * std::numbers::pi_v<float>},
        std::pair{Vec3f{-1, 0, 0}, 1.5f * std::numbers::pi_v<float>}}) {
    auto t = single(5, direction);
    t.alignRotationToSpeed = true;
    SpriteEmitter emitter(t, 8);
    for (int i = 0; i < 65; ++i) emitter.update(1.0f / 60.0f, {});
    std::vector<SpriteDraw> sprites;
    emitter.collect(1.0f / 60.0f, sprites, &view);
    CHECK(!sprites.empty());
    CHECK(std::abs(sprites.front().angle - expected) < 1e-3f);
  }
}

// Distance fade (0x10171d20): full alpha to the near distance, then
// (1 − (d − near)·scale)²; the lodDistance High row is 191.25 m, 1/33.75.
void testDistanceFade() {
  auto t = single(0);
  t.transparencyGraph.c = {0, 0, 0, 1};
  SpriteEmitter emitter(t, 10);
  for (int i = 0; i < 65; ++i) emitter.update(1.0f / 60.0f, {});
  std::vector<SpriteDraw> sprites;
  View close{lookAt({0, 0, -100}, {0, 0, 0}, {0, 1, 0}), 0.1f, 0, {}};
  emitter.collect(0, sprites, &close);
  CHECK(!sprites.empty() && sprites.back().alpha == 1.0f);
  View far{lookAt({0, 0, -(191.25f + 16.875f)}, {0, 0, 0}, {0, 1, 0}), 0.1f, 0, {}};
  emitter.collect(0, sprites, &far);
  CHECK(std::abs(sprites.back().alpha - 0.25f) < 1e-3f);
  // Behind the camera nothing is collected.
  const auto before = sprites.size();
  View behind{lookAt({0, 0, 10}, {0, 0, 20}, {0, 1, 0}), 0.1f, 0, {}};
  emitter.collect(0, sprites, &behind);
  CHECK_EQ(sprites.size(), before);
}

// Near culling, sorted systems only: within 6 m and past age factor 0.15 the
// alpha ramps out; below 0.01 the sprite is not drawn (0x10171e48).
void testNearCulling() {
  auto t = single(0);
  t.timeToLive = 1;
  t.emitTime = 1.5f;
  t.transparencyGraph.c = {0, 0, 0, 1};
  t.nearCullingEnable = true;
  SpriteEmitter emitter(t, 11);
  for (int i = 0; i < 90; ++i) emitter.update(1.0f / 60.0f, {});  // age ≈ 0.5
  View view{lookAt({0, 0, -1}, {0, 0, 0}, {0, 1, 0}), 0.0f, 0, {}};
  std::vector<SpriteDraw> sprites;
  emitter.collect(0, sprites, &view);
  CHECK(sprites.empty());  // 1 m: ramp by distance > 1, age term 0
  View mid{lookAt({0, 0, -5}, {0, 0, 0}, {0, 1, 0}), 0.0f, 0, {}};
  emitter.collect(0, sprites, &mid);
  // byDistance = 0.5, aged = 1 − (0.5 − 0.15)·4 < 0 → 0: alpha 0.5.
  CHECK(!sprites.empty());
  CHECK(std::abs(sprites.back().alpha - 0.5f) < 0.02f);
  t.additive = true;  // the additive collector does not near-cull
  sprites.clear();
  emitter.collect(0, sprites, &view);
  CHECK(!sprites.empty());
}

// Self-shadow: the bottom corners use min(shadow, intensity).
void testSelfShadow() {
  auto t = single(0);
  t.transparencyGraph.c = {0, 0, 0, 1};
  t.color1 = t.color2 = {0.8f, 0.6f, 0.4f};
  t.selfShadowIntensity = 0.5f;
  SpriteEmitter emitter(t, 12);
  for (int i = 0; i < 65; ++i) emitter.update(1.0f / 60.0f, {});
  std::vector<SpriteDraw> sprites;
  emitter.collect(0, sprites);
  CHECK(!sprites.empty());
  CHECK(std::abs(sprites.front().color.x - 0.8f) < 1e-5f);
  CHECK(std::abs(sprites.front().shadowColor.x - 0.4f) < 1e-5f);
}

// calcParticleLighting: lerp(1, lerp(shadow, sun, saturate(lm + offset)), factor).
void testLighting() {
  const Vec3f sun{2, 2, 2}, shade{0.5f, 0.5f, 0.5f};
  CHECK(lighting(1, 0, 0, sun, shade).x == 1.0f);
  CHECK(std::abs(lighting(1, 0.5f, 1, sun, shade).x - 2.0f) < 1e-6f);
  CHECK(std::abs(lighting(0, 0, 1, sun, shade).x - 0.5f) < 1e-6f);
  CHECK(std::abs(lighting(0, 0.5f, 0.5f, sun, shade).x - 1.125f) < 1e-6f);
}

// The light factor is the template's when ≥ 0, else the level default
// (0x10177160); with lm = 1 the sprite takes lerp(1, sun, factor).
void testSpriteLighting() {
  auto t = single(0);
  t.transparencyGraph.c = {0, 0, 0, 1};
  t.lightMapIntensityOffset = 1;
  SpriteEmitter emitter(t, 13);
  for (int i = 0; i < 65; ++i) emitter.update(1.0f / 60.0f, {});
  View view{lookAt({0, 0, -10}, {0, 0, 0}, {0, 1, 0}), 0.1f, 0,
            {{2, 2, 2}, {0.5f, 0.5f, 0.5f}, 0.5f}};
  std::vector<SpriteDraw> sprites;
  emitter.collect(0, sprites, &view);
  CHECK(!sprites.empty() && std::abs(sprites.back().light.x - 1.5f) < 1e-5f);
  t.lightAffectionFactor = 0;  // the template's own 0 wins over the default
  emitter.collect(0, sprites, &view);
  CHECK(sprites.back().light.x == 1.0f);
  emitter.collect(0, sprites);  // no level: 1
  CHECK(sprites.back().light.x == 1.0f);
  // ParticleHigh: lm is the hemi map's alpha. Alpha 0, the particle 500 m
  // below HemiShadowAltitude and no offset: lut 0, the shadow colour,
  // lerp(1, 0.5, 0.5) = 0.75.
  t.lightAffectionFactor = -1;
  t.lightMapIntensityOffset = 0;
  const auto dark = obf2::app::HemiMap::fromAlpha(1, 1, {0}, {}, 100, 500);
  view.hemi = &dark;
  emitter.collect(0, sprites, &view);
  CHECK(std::abs(sprites.back().light.x - 0.75f) < 1e-5f);
}

// `CRD_UNIFORM/2/-2/0` (`emitRangeX` of the dam's spray) samples between the
// two bounds (0x1009f8f0); CRD_NONE gives its first value (0x1017a620).
void testCrd() {
  const auto uniform = Crd::parse("CRD_UNIFORM/2/-2/0");
  CHECK(uniform.type == 1 && uniform.a == 2 && uniform.b == -2 && !uniform.mirror);
  Random random(5);
  float low = 1e9f, high = -1e9f;
  for (int i = 0; i < 2000; ++i) {
    const float v = uniform.sample(random);
    low = std::min(low, v);
    high = std::max(high, v);
  }
  CHECK(low >= -2.0f && high <= 2.0f && low < -1.9f && high > 1.9f);
  CHECK(Crd::parse("CRD_NONE/0.5/0/0").sample(random) == 0.5f);
}

// Box (0x101816f0) places the particle inside the ranges, turned by the
// emitter's axes, and ignores emitRadius.
void testBoxShape() {
  SpriteTemplate t;
  t.emitFrequency = 50;
  t.timeToLive = 5;
  t.emitterType = 3;
  t.emitRadius = {9, 9, 9};
  t.emitRange = {Crd::parse("CRD_UNIFORM/2/-2/0"), Crd{}, Crd{}};
  t.emitterAxes = {Vec3f{0, 0, 1}, Vec3f{0, 1, 0}, Vec3f{-1, 0, 0}};
  SpriteEmitter emitter(t, 3);
  emitter.update(0.5f, {});
  CHECK(!emitter.particles().empty());
  for (const auto& p : emitter.particles()) {
    CHECK(std::abs(p.position.x) < 1e-4f);
    CHECK(std::abs(p.position.z) <= 2.0f);
  }
}

// Type 1 (0x10184450): no velocity component is zeroed, so a slow particle
// under no force keeps drifting; type 0 (0x1017f520) stops it.
void testMeshIntegratorKeepsSlowSpeed() {
  SpriteTemplate t;
  t.emitFrequency = 10;
  t.timeToLive = 5;
  t.emitSpeed = 0.0005f;
  t.emitDirection = {1, 0, 0};
  t.meshType = true;
  SpriteEmitter mesh(t, 2);
  mesh.update(0.5f, {});
  CHECK(!mesh.particles().empty());
  CHECK(mesh.particles().front().velocity.x > 0);
  t.meshType = false;
  SpriteEmitter sprite(t, 2);
  sprite.update(0.5f, {});
  CHECK(!sprite.particles().empty());
  CHECK(sprite.particles().front().velocity.x == 0);
}

// Instance cap +0x220 (RendDX9.dll 0x1017fb00): round(span · emitFrequency + 10).
void testParticleCap() {
  auto t = fireflash();
  // One-shot: span = min(emitTime 0.15, ttl 0.2 + random 0.05) → 0.15·22 + 10 = 13.3.
  CHECK_EQ(SpriteEmitter::particleCap(t), std::size_t{13});
  // Looping: span = 0.25 → 15.5, x87 rounds ties to even.
  t.isLooping = true;
  CHECK_EQ(SpriteEmitter::particleCap(t), std::size_t{16});
  // emitTime 0: the full lifetime span, looping or not.
  t.isLooping = false;
  t.emitTime = 0;
  CHECK_EQ(SpriteEmitter::particleCap(t), std::size_t{16});
  // emitTime longer than the lifetime: the lifetime wins.
  t.emitTime = 2;
  t.emitFrequency = 40;
  CHECK_EQ(SpriteEmitter::particleCap(t), std::size_t{20});
  // No emission: the 10 alone.
  t.emitFrequency = 0;
  CHECK_EQ(SpriteEmitter::particleCap(t), std::size_t{10});
}

}  // namespace

TEST_MAIN({
  testSpriteLighting();
  testWind();
  testCollision();
  testCollisionRingAge();
  testWaterSurface();
  testQuantizedRotation();
  testAlignRotation();
  testDistanceFade();
  testNearCulling();
  testSelfShadow();
  testLighting();
  testCrd();
  testBoxShape();
  testMeshIntegratorKeepsSlowSpeed();
  testParticleCap();
  testEmissionCount();
  testPreWarmAges();
  testGravityRises();
  testQuadraticDrag();
  testRandomRanges();
  testDrawGraphs();
})
