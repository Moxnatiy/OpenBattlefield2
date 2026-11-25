#include "obf2/app/sprite_particles.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <string>

namespace obf2::app::particles {
namespace {

// Fixed integration step and its inverse (RendDX9.dll 0x1023a478, 0x1021fe40).
constexpr float kStep = 0.01f;
constexpr float kStepsPerSecond = 100.0f;
// Velocity components below this are zeroed (0x101dde54).
constexpr float kRestSpeed = 0.001f;
// Squared relative speed under which there is no drag (0x1021fe20).
constexpr float kDragEpsilon = 1.1920929e-7f;
constexpr float kDegrees = 0.017453292f;  // 0x10238738

float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

std::array<float, 4> fields(std::string_view text, std::array<float, 4> result) {
  std::string copy(text);
  std::ranges::replace(copy, '/', ' ');
  std::istringstream values(copy);
  for (float& value : result) {
    float read;
    if (!(values >> read)) break;
    value = read;
  }
  return result;
}

Graph graph(const game::ObjectTemplate& d, std::string_view key) {
  Graph result;
  if (const auto text = d.text(key); !text.empty()) result.c = fields(text, result.c);
  return result;
}

Vec3f vector(const game::ObjectTemplate& d, std::string_view key, Vec3f fallback) {
  const auto text = d.text(key);
  if (text.empty()) return fallback;
  const auto v = fields(text, {fallback.x, fallback.y, fallback.z, 0});
  return {v[0], v[1], v[2]};
}

// Row-vector rotations, as the D3D helpers 0x10078490 (X), 0x1017c470 (Y),
// 0x10079bf0 (Z) build them.
Vec3f rotateX(Vec3f v, float a) {
  const float c = std::cos(a), s = std::sin(a);
  return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}
Vec3f rotateY(Vec3f v, float a) {
  const float c = std::cos(a), s = std::sin(a);
  return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}
Vec3f rotateZ(Vec3f v, float a) {
  const float c = std::cos(a), s = std::sin(a);
  return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

}  // namespace

std::uint32_t Random::next() {
  // Two rounds of a Schrage-style product modulo 2³¹−1 (0x1009f860).
  const auto round = [](std::uint32_t s, std::uint32_t multiplier) {
    const std::uint32_t low = (s & 0xffffu) * multiplier;
    std::uint32_t high = static_cast<std::uint32_t>(static_cast<std::int32_t>(s) >> 16) * multiplier +
                         static_cast<std::uint32_t>(static_cast<std::int32_t>(low) >> 16);
    std::uint32_t r = (high & 0x7fffu) * 0x10000u +
                      static_cast<std::uint32_t>(static_cast<std::int32_t>(high) >> 15) -
                      0x7fffffffu + (low & 0xffffu);
    if (static_cast<std::int32_t>(r) < 0) r += 0x7fffffffu;
    return r;
  };
  state_ = round(round(state_, 0x5e30u), 0x661fu);
  return state_;
}

namespace {
float quantum(std::uint32_t u) {
  return static_cast<float>((static_cast<std::int32_t>(u | 0x80u) >> 7) + 1);
}
}  // namespace

float Random::symmetric() { return quantum(next()) * 1.1920929e-7f - 1.0f; }
float Random::unit() { return quantum(next()) * 5.9604645e-8f; }
float Random::fullTurn() { return quantum(next()) * 3.7450704e-7f; }
float Random::halfTurn() { return quantum(next()) * 1.8725352e-7f; }

std::size_t SpriteEmitter::particleCap(const SpriteTemplate& t) {
  float span = t.timeToLive + t.randomTimeToLive;
  if (t.emitTime != 0.0f && !t.isLooping) span = std::min(span, t.emitTime);
  // x87 ROUND under the default control word: nearest, ties to even.
  const float cap = std::nearbyint(span * t.emitFrequency + 10.0f);
  return cap > 0.0f ? static_cast<std::size_t>(cap) : 0;
}

float Random::normal() {
  if (spare_) {
    spare_ = false;
    return spareValue_;
  }
  float x, y, r;
  do {
    x = unit() * 2.0f - 1.0f;
    y = unit() * 2.0f - 1.0f;
    r = x * x + y * y;
  } while (r > 1.0f);
  const float f = std::sqrt(std::log(r) * -2.0f / r);  // −2 at 0x1020aa70
  spare_ = true;
  spareValue_ = f * y;
  return f * x;
}

float Crd::sample(Random& random) const {
  float sign = 1.0f;
  if (mirror && random.unit() <= 0.5f) sign = -1.0f;  // 0.5 at 0x1020d7d0
  float value = 0.0f;
  switch (type) {
    case 0: value = a; break;
    case 1: value = (b - a) * random.unit() + a; break;
    case 2: value = -std::log(random.unit()) * a; break;
    case 3: value = random.normal() * b + a; break;
    default: break;
  }
  return value * sign;
}

Crd Crd::parse(std::string_view text) {
  Crd crd;
  if (text.empty()) return crd;
  const auto slash = text.find('/');
  const auto name = text.substr(0, slash);
  if (name == "CRD_UNIFORM") crd.type = 1;
  else if (name == "CRD_EXPONENTIAL") crd.type = 2;
  else if (name == "CRD_NORMAL") crd.type = 3;
  if (slash == std::string_view::npos) return crd;
  const auto v = fields(text.substr(slash + 1), {0, 0, 0, 0});
  crd.a = v[0];
  crd.b = v[1];
  crd.mirror = v[2] != 0;
  return crd;
}

SpriteTemplate SpriteTemplate::fromDefinition(const game::ObjectTemplate& d) {
  SpriteTemplate t;
  const auto number = [&](std::string_view key, float fallback) { return d.number(key).value_or(fallback); };
  t.preWarmTime = number("preWarmTime", 0);
  t.emitFrequency = number("emitFrequency", 0);
  t.emitRadius = vector(d, "emitRadius", {});
  t.emitPrio = static_cast<int>(number("emitPrio", 0));
  if (const auto lod = d.text("lodDistance"); lod == "Low") t.lodDistance = 0;
  else if (lod == "Medium") t.lodDistance = 1;
  else if (lod == "High") t.lodDistance = 2;
  t.isLooping = number("isLooping", 0) != 0;
  t.additive = d.text("particleType") == "Additive";
  t.randomTimeToLive = number("randomTimeToLive", 0);
  t.timeToLive = number("timeToLive", 1);
  t.gravity = number("gravity", 0);
  t.airResistance = number("airResistance", 0);
  t.randomRotationSpeed = number("randomRotationSpeed", 0);
  t.rotationSpeed = number("rotationSpeed", 0);
  t.alphaCull = number("alphaCull", 0);
  t.selfShadowIntensity = number("selfShadowIntensity", 1);
  t.windAffectionFactor = number("windAffectionFactor", 0);
  t.forceToWaterSurface = number("forceToWaterSurface", 0) != 0;
  t.waterSurfaceOffset = number("waterSurfaceOffset", 0.03f);
  t.animationEnable = number("animationEnable", 0) != 0;
  t.animationSpeed = number("animationSpeed", 0);
  t.animationPlayOnce = number("animationPlayOnce", 0) != 0;
  t.animationRandomizedStartFrame = number("animationRandomizedStartFrame", 0) != 0;
  t.frameWidth = number("setAnimationFrameWidthRelative", 1);
  t.frameHeight = number("setAnimationFrameHeightRelative", 1);
  t.frameCount = static_cast<int>(number("animationFrameCount", 0));
  t.frameCountX = std::max(1, static_cast<int>(number("animationFrameCountX", 1)));
  // 0x10175ff0: play-once runs the whole strip over one lifetime.
  if (t.animationPlayOnce && t.timeToLive > 0)
    t.animationSpeed = static_cast<float>(t.frameCount) / t.timeToLive;
  t.collisionEnable = number("collisionEnable", 0) != 0;
  t.collisionSpeedAlongNormal = number("collisionSpeedAlongNormal", 0);
  t.collisionSpeedAlongPlane = number("collisionSpeedAlongPlane", 0);
  t.rotationGraph = graph(d, "rotationGraph");
  t.gravityGraph = graph(d, "gravityGraph");
  t.airResistanceGraph = graph(d, "airResistanceGraph");
  t.emitSpeedGraph = graph(d, "emitSpeedGraph");
  t.lightMapIntensityOffset = number("lightMapIntensityOffset", 0);
  t.colorBlendGraph = graph(d, "colorBlendGraph");
  t.transparencyGraph = graph(d, "transparencyGraph");
  t.sizeGraph = graph(d, "sizeGraph");
  const auto pivot = fields(d.text("pivotOffset"), {0, 0, 0, 0});
  t.pivotX = pivot[0];
  t.pivotY = pivot[1];
  t.randomSize = number("randomSize", 0);
  t.randomIntensity = number("randomIntensity", 0);
  t.randomRotation = number("randomRotation", 0);
  t.randomSpeed = number("randomSpeed", 0);
  t.randomDirectionAngle = vector(d, "randomDirectionAngle", {});
  t.emitSpeed = number("emitSpeed", 0);
  t.emitTime = number("emitTime", 0);
  t.emitDelay = number("emitDelay", 0);
  t.color1 = vector(d, "color1", {1, 1, 1});
  t.color2 = vector(d, "color2", {1, 1, 1});
  t.emitDirection = normalize(vector(d, "emitDirection", {0, 1, 0}));
  t.alignRotationToSpeed = number("alignRotationToSpeed", 0) != 0;
  t.particleMaxSize = number("particleMaxSize", 1);
  t.nearCullingEnable = number("nearCullingEnable", 0) != 0;
  t.lightAffectionFactor = number("LightAffectionFactor", -1);
  const auto shape = d.text("emitterType");
  t.emitterType = shape == "RadialDirection" ? 1 : shape == "Radial" ? 2 : shape == "Box" ? 3 : 0;
  t.emitRange = {Crd::parse(d.text("emitRangeX")), Crd::parse(d.text("emitRangeY")),
                 Crd::parse(d.text("emitRangeZ"))};
  t.meshType = d.className == "MeshParticleSystem";
  if (t.meshType) {
    t.randomRotation3 = vector(d, "randomRotation", {});
    t.randomRotationSpeed3 = vector(d, "randomRotationSpeed", {});
    t.rotationSpeed3 = vector(d, "rotationSpeed", {});
  }
  return t;
}

bool SpriteEmitter::integrate(Particle& p, float step, int steps) const {
  if (tmpl_->meshType) return integrateMesh(p, step, steps);
  const auto& t = *tmpl_;
  p.age += static_cast<float>(steps) * step;
  if (p.age >= p.ttl) return false;
  const float before = p.ageFactor;
  const float drag = clamp01(t.airResistanceGraph(before)) * t.airResistance * step;
  const float lift = clamp01(t.gravityGraph(before));
  // Wind, once per call at the particle's position, times
  // windAffectionFactor; only with the manager's wind (+0x8c10, +0x8c0c
  // virtual +0x3c) and a factor above 0 (0x1017f520). The +0x21d template
  // byte (name not established) also turns it off; the port never sets it.
  Vec3f wind{};
  if (world_ && world_->wind && t.windAffectionFactor > 0)
    wind = world_->wind(p.position) * t.windAffectionFactor;
  p.ageFactor = p.age / p.ttl;
  for (int i = 0; i < steps; ++i) {
    const Vec3f relative = p.velocity - wind;
    const float squared = dot(relative, relative);
    Vec3f change{};
    if (std::abs(squared) >= kDragEpsilon) change = relative * (-std::sqrt(squared) * drag);
    change.y += lift * step * t.gravity;
    p.position = p.position + (p.velocity + change * 0.5f) * step;
    p.velocity = p.velocity + change;
  }
  for (float* c : {&p.velocity.x, &p.velocity.y, &p.velocity.z})
    if (std::abs(*c) < kRestSpeed) *c = 0;
  // Collision, after the steps (0x1017f520): moving into the plane and behind
  // it, the particle is put back on it; the normal part of the velocity is
  // reversed times collisionSpeedAlongNormal, the rest scaled by
  // collisionSpeedAlongPlane.
  if (t.collisionEnable) {
    const Plane& plane = collisionPlane(p.age);
    const Vec3f n = plane.normal;
    const float distance = dot(p.position, n) + plane.d;
    if (dot(p.velocity, n) < 0 && distance < 0) {
      p.position = p.position - n * distance;
      const float along = dot(p.velocity, n);
      if (along < 0) {
        const Vec3f normal = n * along;
        p.velocity = (p.velocity - normal) * t.collisionSpeedAlongPlane - normal * t.collisionSpeedAlongNormal;
      }
    }
  }
  // The drawn copy (+0x18). forceToWaterSurface replaces its y with the water
  // height (manager +0x8c14 virtual +0x94); the simulated position keeps its own.
  p.drawPosition = p.position;
  if (t.forceToWaterSurface && world_ && world_->waterHeight)
    p.drawPosition.y = world_->waterHeight(p.drawPosition);
  return true;
}

// The plane a particle of this age collides with (0x1017f520): the current
// one while it is younger than the ring clock, otherwise the ring entry
// round(3·(age − clock)) + 1 pushes back (−3.0 at 0x1021fe14), wrapped into ten.
void SpriteEmitter::setCollisionPlane(Plane plane) {
  const float len = length(plane.normal);
  if (len > 0) plane.normal = plane.normal * (1.0f / len);
  plane_ = plane;
}

const Plane& SpriteEmitter::collisionPlane(float age) const {
  const float behind = ringClock_ - age;
  int index = ringCurrent_;
  if (behind < 0) {
    index = ringCurrent_ - static_cast<int>(std::lround(-3.0f * behind)) - 1;
    index = ((index % 10) + 10) % 10;
  }
  return index == ringCurrent_ ? plane_ : ring_[static_cast<std::size_t>(index)];
}

// 0x10184450. The type-0 port differs in two places: gravity's graph is read
// at each step's own age (0x10184450 updates age and ageFactor inside the
// loop), and no velocity component is zeroed afterwards. No wind manager,
// collision or water surface, as for type 0.
bool SpriteEmitter::integrateMesh(Particle& p, float step, int steps) const {
  const auto& t = *tmpl_;
  if (p.ttl <= static_cast<float>(steps) * step + p.age) return false;
  const float drag = clamp01(t.airResistanceGraph(p.ageFactor)) * t.airResistance * step;
  for (int i = 0; i < steps; ++i) {
    p.age += step;
    p.ageFactor = p.age / p.ttl;
    const float lift = clamp01(t.gravityGraph(p.ageFactor)) * step * t.gravity;
    // 0x10077e90 normalizes in place, zeroing below 0x10208958.
    const Vec3f relative = p.velocity;
    const float squared = dot(relative, relative);
    Vec3f change{};
    if (std::abs(squared) >= kDragEpsilon) change = relative * (-std::sqrt(squared) * drag);
    change.y += lift;
    p.position = p.position + (p.velocity + change * 0.5f) * step;
    p.velocity = p.velocity + change;
  }
  return true;
}

// The spawn offset for `emitterType`, in the emitter's own axes.
Vec3f SpriteEmitter::shapeOffset() {
  const auto& t = *tmpl_;
  Vec3f local;
  if (t.emitterType == 3) {
    // Box (0x101816f0, type 1 0x10186690): Z, Y, then X are drawn.
    const float z = t.emitRange[2].sample(random_);
    const float y = t.emitRange[1].sample(random_);
    const float x = t.emitRange[0].sample(random_);
    local = {x, y, z};
  } else {
    // Shape 0 (0x101813d0, type 1 0x10186370).
    const float a = random_.fullTurn();
    const float b = random_.halfTurn();
    const float u = random_.unit();
    const float v = random_.unit();
    const float w = random_.unit();
    local = {std::cos(a) * v * t.emitRadius.x * std::sin(b), std::sin(a) * u * t.emitRadius.y * std::sin(b),
             std::cos(b) * w * t.emitRadius.z};
  }
  return t.emitterAxes[0] * local.x + t.emitterAxes[1] * local.y + t.emitterAxes[2] * local.z;
}

// 0x101850e0, one particle. Draws: lifetime, size (and speed), intensity, one
// shared draw for the three start angles, one for the three spins, the shape,
// then the three direction angles.
void SpriteEmitter::spawnMesh(Vec3f at) {
  const auto& t = *tmpl_;
  Particle p;
  p.ttl = random_.unit() * t.randomTimeToLive + t.timeToLive;
  p.ageFactor = p.age / p.ttl;
  const float r = random_.symmetric();  // 2⁻²³ at 0x10220030
  p.sizeTerm = r * t.randomSize;
  p.intensityTerm = random_.symmetric() * t.randomIntensity;
  p.angles = t.randomRotation3 * random_.symmetric();
  const float s = random_.symmetric();
  const Vec3f spin = t.randomRotationSpeed3 * s;
  p.spins = s < 0 ? spin - t.rotationSpeed3 : spin + t.rotationSpeed3;
  const Vec3f offset = shapeOffset();
  const float ax = random_.symmetric() * t.randomDirectionAngle.x * kDegrees;
  const float ay = random_.symmetric() * t.randomDirectionAngle.y * kDegrees;
  const float az = random_.symmetric() * t.randomDirectionAngle.z * kDegrees;
  const Vec3f direction = rotateZ(rotateY(rotateX(t.emitDirection, ax), ay), az);
  const float emitted = t.emitTime > 0 ? 1.0f - remaining_ / t.emitTime : 0.0f;
  const float speed = clamp01(t.emitSpeedGraph(emitted)) * (t.emitSpeed - r * t.randomSpeed);
  p.position = at + offset;
  p.velocity = direction * speed;
  particles_.push_back(p);
}

void SpriteEmitter::spawn(Vec3f at) {
  if (tmpl_->meshType) return spawnMesh(at);
  const auto& t = *tmpl_;
  Particle p;
  if (t.animationRandomizedStartFrame)
    p.frame = std::floor(static_cast<float>(t.frameCount - 1) * random_.unit());
  p.ttl = random_.unit() * t.randomTimeToLive + t.timeToLive;
  p.ageFactor = p.age / p.ttl;
  p.shadow = t.selfShadowIntensity;  // record +0x08 (0x101802ef)
  p.alpha = p.alphaBase;
  const float r = random_.symmetric();
  p.sizeTerm = r * t.randomSize;
  p.intensityTerm = random_.symmetric() * t.randomIntensity;
  p.angle = random_.symmetric() * t.randomRotation;
  const float spin = random_.symmetric() * t.randomRotationSpeed;
  p.spin = spin < 0 ? spin - t.rotationSpeed : spin + t.rotationSpeed;
  const Vec3f offset = shapeOffset();
  const float ax = random_.symmetric() * t.randomDirectionAngle.x * kDegrees;
  const float ay = random_.symmetric() * t.randomDirectionAngle.y * kDegrees;
  const float az = random_.symmetric() * t.randomDirectionAngle.z * kDegrees;
  const Vec3f direction = rotateZ(rotateY(rotateX(t.emitDirection, ax), ay), az);
  const float emitted = t.emitTime > 0 ? 1.0f - remaining_ / t.emitTime : 0.0f;
  const float speed = clamp01(t.emitSpeedGraph(emitted)) * (t.emitSpeed - r * t.randomSpeed);
  p.position = at + offset;
  p.drawPosition = p.position;
  p.velocity = direction * speed;
  particles_.push_back(p);
}

void SpriteEmitter::update(float dt, Vec3f emitterPosition, bool emitting) {
  const auto& t = *tmpl_;
  if (!started_) {
    started_ = true;
    delay_ = t.emitDelay;
    remaining_ = t.emitTime;
    // Activate 0x1017ff20 copies preWarmTime (template +0x0c) to instance +0xf8.
    preWarm_ = t.preWarmTime;
    previousPosition_ = position_ = emitterPosition;
  }
  previousPosition_ = position_;
  position_ = emitterPosition;
  if (!emitting) stopped_ = true;
  // Wind time scale, every update, whatever the global wind flag: the wind at
  // the emitter, squared length (0x10027bd0). +0x21d (local) would skip it;
  // the port never sets that byte.
  if (t.windTimeScaleFactor >= 0 && t.windAffectionFactor >= 0) {
    const Vec3f wind = world_ && world_->wind ? world_->wind(emitterPosition) : Vec3f{};
    const float speed = t.windTimeScaleSpeed * t.windTimeScaleSpeed;
    const float share = clamp01(dot(wind, wind) / (speed >= 1.0f ? speed : 1.0f)) * t.windTimeScaleFactor;
    windTimeScale_ = std::max(share, 1.0f);  // the instance time scale +0x12c is 1 here
  } else {
    windTimeScale_ = 1;
  }
  // The plane ring (0x10180ba0 → 0x10180120), with the unclamped frame step:
  // every 0.33333 s (0x1021fe18) the current plane, normalized (0x10077e90),
  // goes into the next of ten slots, which becomes the current index.
  if (t.collisionEnable) {
    ringClock_ += dt;
    if (ringClock_ >= 0.33333f) {
      ringClock_ -= 0.33333f;
      ringCurrent_ = ringNext_;
      const float len = length(plane_.normal);
      ring_[static_cast<std::size_t>(ringNext_)] = {len > 0 ? plane_.normal * (1.0f / len) : plane_.normal, plane_.d};
      ringNext_ = (ringNext_ + 1) % 10;
    }
  }
  const float frame = std::min(dt, t.timeToLive + t.randomTimeToLive);
  const int steps = static_cast<int>(std::lround(kStepsPerSecond * (frame + carry_)));
  const float advance = static_cast<float>(steps) * kStep;
  carry_ = frame + carry_ - advance;
  previousNow_ = now_;
  now_ += advance;
  std::erase_if(particles_, [&](Particle& p) { return !integrate(p, kStep, steps); });

  delay_ -= frame;
  if (delay_ > 0) {
    lastEmit_ = now_;
    return;
  }
  // Pre-warm, 0x10180f23: the first update after the delay has run out takes
  // +0xf8, clears it, and runs this update on itself (virtual +0xa4) in steps
  // of min(remaining, 1/30 s) (0x1021fe1c; immediate 0x3d088889) while the
  // remaining time is above 0, before this frame's emission. Measured: the
  // grenade's fireflash (emitDelay 0.11, preWarmTime 0.2) shows three
  // particles already 0.07-0.16 s old at 0.10 s
  // (decal-ab-20261122/retail-corrugated-grenade-23).
  if (preWarm_ > 0) {
    constexpr float kPreWarmStep = 1.0f / 30.0f;
    float left = preWarm_;
    preWarm_ = 0;
    while (left > 0) {
      update(std::min(left, kPreWarmStep), emitterPosition, emitting);
      left -= kPreWarmStep;
    }
  }
  if (stopped_) return;
  // 0x10180790 multiplies the rate by manager+0x8bfc[emitPrio] (0.40, 0.55,
  // 0.70 at effects quality 2) only when the manager byte +0x1d3c is set or the
  // live particle count (+0x5f28) exceeds round(budget +0x5f24 · 0.75 at
  // +0x8c08). The port keeps no manager budget and +0x1d3c is not measured,
  // so the rate is left unscaled: the light-load case.
  const float interval = 1.0f / std::max(t.emitFrequency, t.minimumRate());
  while (lastEmit_ + interval <= now_) {
    if (remaining_ != 0) {
      remaining_ -= interval;
      if (remaining_ <= 0) {
        if (!t.isLooping) {
          stopped_ = true;
          return;
        }
        // Restart (virtual +0xc); what it resets beyond the emit time is not established.
        remaining_ = t.emitTime;
        return;
      }
    }
    lastEmit_ += interval;
    if (particles_.size() >= particleCap(t)) continue;
    const float span = now_ - previousNow_;
    const float blend = span > 0 ? (lastEmit_ - previousNow_) / span : 0.0f;
    spawn(previousPosition_ + (position_ - previousPosition_) * blend);
    const float ahead = now_ - lastEmit_;
    const int whole = static_cast<int>(std::lround(ahead * kStepsPerSecond));
    if (integrate(particles_.back(), kStep, whole)) {
      integrate(particles_.back(), ahead - static_cast<float>(whole) * kStep, 1);
    } else {
      particles_.pop_back();
    }
  }
}

Rotation quantizedRotation(float angle) {
  constexpr float kTableStep = 0.00153398f;     // 2π/4096, 0x1021ef18
  constexpr float kStepsPerTurn = 651.899f;     // 4096/2π, 0x1021ef10
  constexpr float kShort = 32767.0f;            // 0x10219558
  // 0x1016f8a0 rounds to nearest (FPU default), then & 0xfff.
  const auto index = static_cast<std::int64_t>(std::nearbyint(angle * kStepsPerTurn)) & 0xfff;
  const float a = static_cast<float>(index) * kTableStep;
  return {std::trunc(std::sin(a) * kShort) / kShort, std::trunc(std::cos(a) * kShort) / kShort};
}

Vec3f lighting(float lm, float lmOffset, float lightFactor, Vec3f sun, Vec3f shadow) {
  const float lut = clamp01(lm + lmOffset);
  const Vec3f diffuse = shadow + (sun - shadow) * lut;
  return Vec3f{1, 1, 1} + (diffuse - Vec3f{1, 1, 1}) * lightFactor;
}

namespace {

// A view-space point divided by its depth, as 0x10172450 does with the
// matrix at renderer +0x1c98: (0, 0) when |z| ≤ 2⁻²² (0x1021ef14).
std::array<float, 2> projected(const Mat4& m, Vec3f p) {
  const float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
  const float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
  const float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
  if (std::abs(z) <= 2.3841858e-7f) return {0, 0};
  return {x / z, y / z};
}

}  // namespace

void SpriteEmitter::collect(float dt, std::vector<SpriteDraw>& out, const View* view) {
  const auto& t = *tmpl_;
  const auto lod = static_cast<std::size_t>(std::clamp(t.lodDistance, 0, 2));
  const EffectLighting lit = view ? view->lighting : EffectLighting{};
  const HemiMap* hemi = view ? view->hemi : nullptr;
  // +0x168 (0x10177160): the template's own factor, or the level default below 0.
  const float lightFactor = t.lightAffectionFactor >= 0 ? t.lightAffectionFactor : lit.defaultLightAffectionFactor;
  for (auto& p : particles_) {
    if (view) {
      const Mat4& m = view->view;
      const Vec3f q = p.drawPosition;
      const float depth = m.m[2] * q.x + m.m[6] * q.y + m.m[10] * q.z + m.m[14];
      if (depth < view->nearClip) continue;
      // The additive collector 0x10171000 tests alphaCull against last
      // frame's alpha, before the fade; the sorted one 0x10171d20 after it.
      if (t.additive && p.alpha < t.alphaCull) continue;
      // Distance fade: (1 − min((d − near)·scale, 1))² · alpha base.
      if (depth <= kFadeNear[lod]) {
        p.alpha = p.alphaBase;
      } else {
        const float f = std::min((depth - kFadeNear[lod]) * kFadeScale[lod], 1.0f);
        p.alpha = (1.0f - f) * (1.0f - f) * p.alphaBase;
      }
      if (!t.additive) {
        if (p.alpha < t.alphaCull) continue;
        // Near culling, sorted only (0x10171e48): close to the camera and past
        // nearCullAge, the alpha is replaced by a ramp over both.
        const float near = depth - view->nearClip;
        if (t.nearCullingEnable && near < t.nearCullDistance && p.ageFactor > t.nearCullAge) {
          const float byAge = (p.ageFactor - t.nearCullAge) * t.nearCullAgeScale;
          const float aged = byAge <= 1.0f ? 1.0f - byAge : 0.0f;
          const float byDistance = (t.nearCullDistance - near) * t.nearCullDistanceScale;
          p.alpha = byDistance <= 1.0f ? (1.0f - byDistance) + byDistance * aged : aged;
          if (p.alpha < t.nearCullAlpha) continue;
        }
      }
    } else {
      p.alpha = p.alphaBase;
      if (p.alpha < t.alphaCull) continue;
    }
    // 0x10172450, only for collected particles.
    const float pace = clamp01(t.rotationGraph(p.ageFactor));
    if (!t.alignRotationToSpeed) {
      p.angle = std::fmod(p.angle + pace * dt * p.spin, 2.0f * std::numbers::pi_v<float>);
    } else if (view) {
      // The screen direction of the velocity: the drawn position and one
      // 0.001 s back (0x101dde54), each divided by its view depth.
      const auto a = projected(view->view, p.drawPosition);
      const auto b = projected(view->view, p.drawPosition - p.velocity * 0.001f);
      const float dx = a[0] - b[0], dy = a[1] - b[1];
      const float len = std::sqrt(dx * dx + dy * dy);
      if (std::abs(len) <= 2.3841858e-7f) {
        p.angle = 0;
      } else {
        // acos of the clamped cosine (0x100a6890); 2π − it for x ≤ 0 (0x101e2b9c).
        const float c = std::acos(std::clamp(dy / len, -1.0f, 1.0f));
        p.angle = dx <= 0 ? 2.0f * std::numbers::pi_v<float> - c : c;
      }
    }
    if (t.animationEnable && t.frameCount > 0) p.frame += pace * dt * t.animationSpeed;
    const float age = p.ageFactor;
    const float blend = std::min(t.colorBlendGraph(age), 1.0f);
    SpriteDraw draw;
    draw.position = p.drawPosition;
    const Vec3f base = t.color2 * blend + t.color1 * (1.0f - blend);
    const Vec3f term{p.intensityTerm, p.intensityTerm, p.intensityTerm};
    draw.color = base * p.intensity + term;
    // The vertex writer's bottom corners (texcoord y −1) carry
    // min(shadow, intensity) in place of the intensity (0x10170070).
    draw.shadowColor = base * std::min(p.shadow, p.intensity) + term;
    draw.alpha = std::min(t.transparencyGraph(age), 1.0f) * p.alpha;
    draw.halfSize = std::min(t.sizeGraph(age), 1.0f) * t.particleMaxSize + p.sizeTerm;
    draw.angle = p.angle;
    draw.rotation = quantizedRotation(p.angle);
    draw.lightMapOffset =
        clamp01(std::clamp((p.drawPosition.y - (hemi ? hemi->shadowAltitude() : 0.0f)) / 10.0f, 0.0f, 1.0f) +
                t.lightMapIntensityOffset);
    // ParticleHigh samples the hemi map at the record position (Particles.fx:93,
    // :165); without one, ParticleMedium's lm = 1 (:153).
    const float lm = hemi ? hemi->alpha(p.drawPosition) : 1.0f;
    draw.light = lighting(lm, draw.lightMapOffset, lightFactor, lit.sun, lit.shadow);
    draw.frame = t.frameCount > 0
        ? static_cast<int>(static_cast<std::int64_t>(std::floor(p.frame)) % t.frameCount) : 0;
    // 0x10172450 keeps frac(frame) at record+0x30; Particles.fx lerps the two
    // cells by it in ParticleHigh/AdditiveHigh (`:163-167`, `:194-197`).
    if (t.animationEnable && t.frameCount > 1) {
      draw.nextFrame = (draw.frame + 1) % t.frameCount;
      draw.frameBlend = p.frame - std::floor(p.frame);
    }
    // A negative size is still drawn by the shader, as a mirrored quad.
    if (draw.alpha > 0 && draw.halfSize != 0) out.push_back(draw);
  }
}

// A quad of half extent 1, the displacement Particles.fx gives each corner
// (±1, RendDX9.dll 0x10170070, −1 at 0x1021edc4), less the pivot. The texture
// window is the central 1/√2 of the atlas cell: the shader scales its ±1
// texcoords by `uvScale` = 1/√2 (`Shaders_client.zip:FXCommon.fx:14`) before
// halving them into the cell.
mesh::RenderMesh spriteQuad(float x, float y, float width, float height, float pivotX, float pivotY) {
  constexpr float window = 0.35355339f;  // 0.5 / √2
  const float u0 = x + width * (0.5f - window), u1 = x + width * (0.5f + window);
  const float v0 = y + height * (0.5f - window), v1 = y + height * (0.5f + window);
  const float l = -1.0f - pivotX, r = 1.0f - pivotX, b = -1.0f - pivotY, t = 1.0f - pivotY;
  // uv2: the corner's offset from the cell centre, which the particle branch
  // of the vertex shader rotates (Particles.fx:78).
  const float hu = width * window, hv = height * window;
  mesh::RenderMesh result;
  result.vertices = {
      {{l, b, 0}, {0, 0, -1}, {u0, v1}, {-hu, hv}},
      {{r, b, 0}, {0, 0, -1}, {u1, v1}, {hu, hv}},
      {{r, t, 0}, {0, 0, -1}, {u1, v0}, {hu, -hv}},
      {{l, t, 0}, {0, 0, -1}, {u0, v0}, {-hu, -hv}},
  };
  // Particles.fx draws with CullMode NONE; both windings reuse the scene's
  // back-face-culling blend pipelines.
  result.indices = {0, 1, 2, 0, 2, 3, 2, 1, 0, 3, 2, 0};
  result.ranges.emplace_back().indexCount = 12;
  return result;
}

void emitSprites(const std::vector<SpriteDraw>& sprites, const std::vector<gfx::GpuMesh>& frames,
                 const SpriteTemplate& t, const Mat4& view, std::vector<gfx::MeshRenderer::DrawItem>& items) {
  if (frames.empty()) return;
  const Vec3f right{view.m[0], view.m[4], view.m[8]};
  const Vec3f up{view.m[1], view.m[5], view.m[9]};
  for (const auto& sprite : sprites) {
    // The quad stays screen-aligned; the angle turns its texcoords
    // (Particles.fx:78), in the shader's particle branch.
    const auto x = right * sprite.halfSize;
    const auto y = up * sprite.halfSize;
    auto transform = translation(sprite.position);
    transform.m[0] = x.x; transform.m[1] = x.y; transform.m[2] = x.z;
    transform.m[4] = y.x; transform.m[5] = y.y; transform.m[6] = y.z;
    const auto frame = static_cast<std::size_t>(std::max(0, sprite.frame)) % frames.size();
    auto& item = items.emplace_back(&frames[frame], transform);
    item.particleOpacity = sprite.alpha;
    // calcParticleLighting multiplies the texel like the colour does
    // (Particles.fx:153-155), so it folds into both colours here.
    item.particleColor[0] = sprite.color.x * sprite.light.x;
    item.particleColor[1] = sprite.color.y * sprite.light.y;
    item.particleColor[2] = sprite.color.z * sprite.light.z;
    item.particleColor[3] = sprite.rotation.cos;
    item.particleFrame[3] = sprite.rotation.sin;
    // The bottom corners' min(selfShadow, intensity) colour (0x10170070).
    item.particleShadow[0] = sprite.shadowColor.x * sprite.light.x;
    item.particleShadow[1] = sprite.shadowColor.y * sprite.light.y;
    item.particleShadow[2] = sprite.shadowColor.z * sprite.light.z;
    item.particleShadow[3] = 1.0f;
    // Retail draws ParticleHigh (particle context +0x8cac = 3, measured,
    // docs/functions/sprite-particles.md): the next cell blended in.
    if (sprite.frameBlend > 0.0f && frames.size() > 1) {
      const int columns = std::max(1, t.frameCountX);
      const int next = sprite.nextFrame;
      const int current = static_cast<int>(frame);
      item.particleFrame[0] = static_cast<float>(next % columns - current % columns) * t.frameWidth;
      item.particleFrame[1] = static_cast<float>(next / columns - current / columns) * t.frameHeight;
      item.particleFrame[2] = sprite.frameBlend;
    }
  }
}

}  // namespace obf2::app::particles
