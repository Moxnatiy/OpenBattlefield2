#include "obf2/anim/ragdoll.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "obf2/core/path.h"
#include "obf2/mesh/skinning.h"

namespace obf2::anim {
namespace {

constexpr float kEpsilon = 1.1920929e-07f;  // FLT_EPSILON, the engine's every test

float dot3(const Vec3f& a, const Vec3f& b) { return dot(a, b); }

Vec3f cross3(const Vec3f& a, const Vec3f& b) { return cross(a, b); }

// `Vec3::normalize`: a vector of squared length within epsilon of 1 is left as it
// is, one within epsilon of 0 becomes zero (0x6c06a0 and every caller).
Vec3f engineNormalize(const Vec3f& v) {
  const float squared = dot3(v, v);
  if (std::fabs(squared - 1.0f) < kEpsilon) return v;
  if (std::fabs(squared) < kEpsilon) return Vec3f{};
  return v * (1.0f / std::sqrt(squared));
}

Vec3f translationOf(const mesh::Mat4& m) { return Vec3f{m.m[12], m.m[13], m.m[14]}; }

// Our matrices are column-major with column vectors; the engine's are row-major
// with row vectors. The bytes are the same, so the engine's `a × b` is our
// `b * a`.
mesh::Mat4 multiply(const mesh::Mat4& a, const mesh::Mat4& b) {
  mesh::Mat4 out;
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[column * 4 + k];
      out.m[column * 4 + row] = sum;
    }
  }
  return out;
}

// A bone's rest matrix relative to its parent, from the `.ske` (the engine's
// `Skeleton::Bone` +0, `Quat::toMat` and the translation at +0x30, 0x6c8da0).
mesh::Mat4 restLocal(const mesh::SkeletonBone& bone) {
  const float x = bone.rotation[0], y = bone.rotation[1], z = bone.rotation[2],
              w = bone.rotation[3];
  mesh::Mat4 out{};
  out.m[0] = 1.0f - 2.0f * (y * y + z * z);
  out.m[1] = 2.0f * (x * y - z * w);
  out.m[2] = 2.0f * (x * z + y * w);
  out.m[4] = 2.0f * (x * y + z * w);
  out.m[5] = 1.0f - 2.0f * (x * x + z * z);
  out.m[6] = 2.0f * (y * z - x * w);
  out.m[8] = 2.0f * (x * z - y * w);
  out.m[9] = 2.0f * (y * z + x * w);
  out.m[10] = 1.0f - 2.0f * (x * x + y * y);
  out.m[12] = bone.position.x;
  out.m[13] = bone.position.y;
  out.m[14] = bone.position.z;
  out.m[15] = 1.0f;
  return out;
}

// `Vec3::makeOrthonormalBasis(z, x, y)` (0x734110): x and y normalised, then
// z = x × y, y = z × x, x = y × z, each normalised; components under epsilon
// become zero.
void makeOrthonormalBasis(Vec3f& z, Vec3f& x, Vec3f& y) {
  x = engineNormalize(x);
  y = engineNormalize(y);
  z = engineNormalize(cross3(x, y));
  y = engineNormalize(cross3(z, x));
  x = engineNormalize(cross3(y, z));
  for (Vec3f* v : {&z, &x, &y}) {
    if (std::fabs(v->x) < kEpsilon) v->x = 0.0f;
    if (std::fabs(v->y) < kEpsilon) v->y = 0.0f;
    if (std::fabs(v->z) < kEpsilon) v->z = 0.0f;
  }
}

}  // namespace

float closestBetweenSegments(const Vec3f& p0, const Vec3f& p1, const Vec3f& q0, const Vec3f& q1,
                             float& s, float& t) {
  // 0x723cf0, with its names: a = |d1|², b = −d1·d2, c = |d2|², d = d1·r.
  const Vec3f d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
  const float a = dot(d1, d1), c = dot(d2, d2);
  const float d1d2 = dot(d1, d2);
  const float b = -d1d2;
  const float d = dot(d1, r);
  if (std::fabs(a * c - b * b) < 0.001f) {
    // Nearly parallel: the engine's own choice of endpoints (0x723d9x..0x7240dd).
    s = 0.0f;
    t = 0.0f;
    if (b <= 0.0f) {
      if (-d < a) {
        if (d <= 0.0f) {
          s = -d / a;
        } else if (d < d1d2) {
          t = -d / b;
        } else {
          t = 1.0f;
        }
      } else {
        s = 1.0f;
      }
    } else if (d < 0.0f) {
      if (a < -d) {
        s = 1.0f;
        t = b <= -(a + d) ? 1.0f : -(a + d) / b;
      } else {
        s = -d / a;
      }
    }
  } else {
    // Not parallel: the closest points are unique, and the engine's region-by-
    // region walk finds the same two as clamping does.
    const float e = dot(d2, r);
    const float denominator = a * c - d1d2 * d1d2;
    s = std::clamp((d1d2 * e - d * c) / denominator, 0.0f, 1.0f);
    t = (d1d2 * s + e) / c;
    if (t < 0.0f) {
      t = 0.0f;
      s = std::clamp(-d / a, 0.0f, 1.0f);
    } else if (t > 1.0f) {
      t = 1.0f;
      s = std::clamp((d1d2 - d) / a, 0.0f, 1.0f);
    }
  }
  return length((p0 + d1 * s) - (q0 + d2 * t));
}

float RagdollTemplate::boneDistance(int a, int b) const {
  const auto at = [this](int bone) {
    return bone >= 0 && static_cast<std::size_t>(bone) < restWorld.size()
               ? translationOf(restWorld[static_cast<std::size_t>(bone)])
               : Vec3f{};
  };
  return length(at(a) - at(b));
}

bool RagdollTemplate::apply(
    const con::Command& command,
    const std::function<std::optional<mesh::Skeleton>(std::string_view)>& loadSkeleton,
    const RagdollSettings& settings) {
  const std::string_view name = command.lowerPath;
  const auto i = [&](std::size_t n) { return command.argInt(n).value_or(-1); };
  const auto f = [&](std::size_t n) { return command.argFloat(n).value_or(0.0f); };

  if (name == "ragdoll.skeleton") {
    // Loaded once (0x6bdf50).
    if (restWorld.empty() && loadSkeleton) {
      if (auto loaded = loadSkeleton(command.argStr(0))) {
        skeleton = std::move(*loaded);
        restWorld = mesh::poseSkeleton(skeleton, nullptr, 0);
      }
    }
    return true;
  }
  if (name == "ragdoll.addparticle") {
    // 0x6bd8e0: the size by the bone.
    const int bone = i(0);
    if (bone < 0 || bone >= 128) return true;
    float size = settings.boneSize;
    if (bone == 2 || bone == 7) size = settings.kneeBoneSize;
    else if (bone == 6 || bone == 31 || bone == 1 || bone == 15) size = settings.bodyBoneSize;
    particleOfBone[static_cast<std::size_t>(bone)] = static_cast<int>(particles.size());
    particles.push_back(Particle{bone, f(1), size});
    return true;
  }
  if (name == "ragdoll.lockbone") {
    for (int bone = i(0); bone <= i(1); ++bone) {
      if (bone >= 0 && bone < 128) locked.set(static_cast<std::size_t>(bone));
    }
    return true;
  }
  if (name == "ragdoll.toskeleton") {
    mappings.push_back(BoneMapping{i(0), i(1), i(2)});
    return true;
  }
  if (name == "ragdoll.addconstraint") {
    // addDistanceConstraint (0x6bbf90): a constraint on the same pair is updated.
    const int a = particleOf(i(0)), b = particleOf(i(1));
    if (a < 0 || b < 0) return true;
    const float rest = boneDistance(i(0), i(1));
    for (Distance& existing : distances) {
      if (existing.a == a && existing.b == b) {
        existing.rest = rest;
        return true;
      }
    }
    distances.push_back(Distance{a, b, rest});
    return true;
  }
  if (name == "ragdoll.addcapsulecollision") {
    const int a = particleOf(i(0)), b = particleOf(i(1));
    if (a >= 0 && b >= 0) capsules.push_back(Capsule{a, b});
    return true;
  }
  if (name == "ragdoll.addangularconstraint") {
    // 0x6bc2b0: the distance a–c the angle allows, signed with the angle.
    const int a = particleOf(i(0)), b = particleOf(i(1)), c = particleOf(i(2));
    if (a < 0 || b < 0 || c < 0) return true;
    const float degrees = f(3);
    const float ab = boneDistance(i(0), i(1));
    const float bc = boneDistance(i(1), i(2));
    const float cosine = std::cos(std::fabs(degrees) * 0.017453292f);
    float target = std::sqrt(ab * bc * -2.0f * cosine + bc * bc + ab * ab);
    if (!(degrees >= 0.0f)) target = -target;
    for (Angular& existing : angulars) {
      if (existing.a == a && existing.b == b && existing.c == c) {
        existing.target = target;
        return true;
      }
    }
    angulars.push_back(Angular{a, b, c, ab, bc, target});
    return true;
  }
  if (name == "ragdoll.adddihedralangleconstraint") {
    // 0x6bc080.
    const int a = particleOf(i(0)), b = particleOf(i(1)), c = particleOf(i(2)),
              d = particleOf(i(3));
    if (a < 0 || b < 0 || c < 0 || d < 0) return true;
    const float ab = boneDistance(i(0), i(1));
    const float bc = boneDistance(i(1), i(2));
    const float cd = boneDistance(i(2), i(3));
    const float cosine = std::cos(f(4) * 0.017453292f);
    const float target = std::sqrt(cosine * ab * cd * -2.0f + cd * cd + ab * ab);
    for (Dihedral& existing : dihedrals) {
      if (existing.a == a && existing.b == b && existing.c == c && existing.d == d) {
        existing.target = target;
        return true;
      }
    }
    dihedrals.push_back(Dihedral{a, b, c, d, ab, bc, cd, target});
    return true;
  }
  if (name == "ragdoll.adddistancelessthanconstraint") {
    const int a = particleOf(i(0)), b = particleOf(i(1));
    if (a >= 0 && b >= 0) lessThans.push_back(DistanceLessThan{a, b, f(2)});
    return true;
  }
  if (name == "ragdoll.setparticlecollisioncriteria") {
    collisionMinSpeed = f(0);
    collisionDelay = f(1);
    return true;
  }
  return false;
}

std::optional<RagdollTemplate> RagdollTemplate::load(FileSystem& files,
                                                     const RagdollSettings& settings,
                                                     std::string* error) {
  RagdollTemplate out;
  const auto loadSkeleton = [&files](std::string_view path) -> std::optional<mesh::Skeleton> {
    const auto bytes = files.read(normalizeAssetPath(path));
    if (!bytes) return std::nullopt;
    return mesh::loadSkeleton(*bytes);
  };
  con::Interpreter interpreter(files, [&](const con::Command& command) {
    out.apply(command, loadSkeleton, settings);
  });
  constexpr std::string_view kInit = "objects/soldiers/common/animations/ragdollinit.con";
  const char* failed = nullptr;
  if (!interpreter.runFile(kInit)) failed = "the file was not run";
  else if (out.restWorld.empty()) failed = "its skeleton was not read";
  else if (out.particles.empty()) failed = "it added no particle";
  if (failed != nullptr) {
    if (error != nullptr) *error = std::string(kInit) + ": " + failed;
    return std::nullopt;
  }
  return out;
}

Ragdoll::Ragdoll(const RagdollTemplate& tmpl, const std::vector<mesh::Mat4>& boneWorld,
                 Vec3f velocity, const RagdollSettings& settings)
    : tmpl_(&tmpl), settings_(settings) {
  // `RagDoll::RagDoll` (0x6bbd70): the gravity ramp, awake, a fresh body.
  time_ = tmpl.collisionDelay + tmpl.collisionDelay;
  gravityRamp_ = settings.gravityOverTime;
  wakeUp();

  // makeInstance (0x6bf380).
  const float speed = length(velocity);
  if (settings.maxInheritSpeed < speed) velocity = velocity * (settings.maxInheritSpeed / speed);

  // reset (0x6bef00).
  particles_.reserve(tmpl.particles.size());
  for (const RagdollTemplate::Particle& source : tmpl.particles) {
    Particle p;
    p.bone = source.bone;
    p.mass = source.mass;
    p.size = source.size;
    if (source.bone >= 0 && static_cast<std::size_t>(source.bone) < boneWorld.size()) {
      p.position = translationOf(boneWorld[static_cast<std::size_t>(source.bone)]);
    }
    p.previous = p.position - velocity * 0.015f;
    switch (source.bone) {
      case 1: p.networked = (settings.netBoneEnable & 1) != 0; break;
      case 6: p.networked = (settings.netBoneEnable & 2) != 0; break;
      case 15: p.networked = (settings.netBoneEnable & 4) != 0; break;
      case 31: p.networked = (settings.netBoneEnable & 8) != 0; break;
      default: break;
    }
    particles_.push_back(p);
  }
  lastCenter_ = center();
}

void Ragdoll::wakeUp() {
  // 0x6ba5e0.
  sleepTimer_ = settings_.sleep;
  awake_ = true;
}

Vec3f Ragdoll::center() const {
  // getCenter (0x6bf800): bone 6's particle.
  const int index = tmpl_->particleOf(6);
  return index >= 0 ? particles_[static_cast<std::size_t>(index)].position : Vec3f{};
}

Vec3f Ragdoll::right() const {
  // getRight (0x6c0aa0).
  const int p1 = tmpl_->particleOf(1), p6 = tmpl_->particleOf(6);
  if (p1 < 0 || p6 < 0) return Vec3f{};
  return engineNormalize(particles_[static_cast<std::size_t>(p1)].position -
                         particles_[static_cast<std::size_t>(p6)].position);
}

Vec3f Ragdoll::forward() const {
  // getForward (0x6c06a0).
  const int p31 = tmpl_->particleOf(31), p1 = tmpl_->particleOf(1), p6 = tmpl_->particleOf(6);
  if (p31 < 0 || p1 < 0 || p6 < 0) return Vec3f{};
  const Vec3f a = particles_[static_cast<std::size_t>(p31)].position -
                  particles_[static_cast<std::size_t>(p1)].position;
  const Vec3f b = particles_[static_cast<std::size_t>(p6)].position -
                  particles_[static_cast<std::size_t>(p31)].position;
  return engineNormalize(cross3(b, a));
}

void Ragdoll::setClient(bool client) {
  // setIsClient (0x6c1540) without the constraints' pass; `update` leaves the
  // client mode through its own copy, which has the ground.
  if (client_ == client) return;
  if (!client) clientTimer_ = 0.0f;
  client_ = client;
}

void Ragdoll::readNetwork(const std::vector<Vec3f>& positions) {
  // readCurrentState (`BF2.exe` 0x7ea290): +0x5c, +0x61, +0x1d and +0x38 there
  // are the server's +0x70, +0x75, +0x31 and +0x4c (the pointer fields move them
  // by 0x14): the client mode for 0.3 s more, awake, and a full sleep timer.
  clientTimer_ = 0.3f;  // 0x3e99999a
  client_ = true;
  sleepTimer_ = settings_.sleep;
  awake_ = true;
  std::size_t next = 0;
  for (Particle& p : particles_) {
    if (!p.networked || next >= positions.size()) continue;
    p.network = positions[next++];
    // While +0x62 (the server's +0x76) is set — the first read — the position
    // goes there at once too.
    if (fresh_) p.position = p.network;
  }
  fresh_ = false;
}

void Ragdoll::accumulateForces() {
  // 0x6bd540.
  const float factor = gravityRamp_ <= 0.0f ? 1.0f : gravityTime_ / gravityRamp_;
  for (Particle& p : particles_) {
    const float inverse = 1.0f / p.mass;
    p.acceleration = p.acceleration + p.force * inverse;
    p.acceleration.y += factor * tmpl_->gravity;
  }
}

void Ragdoll::verlet(float dt) {
  // 0x6bdff0.
  for (Particle& p : particles_) {
    if (!client_ || !p.networked) {
      const Vec3f step = (p.position - p.previous) + p.acceleration * (dt * dt);
      p.previous = p.position;
      p.position = p.position + (step + p.lastStep) * 0.5f;
      p.lastStep = step;
    } else {
      p.previous = p.position;
      p.position = p.position + (p.network - p.position) * settings_.netInterpolate;
    }
    p.acceleration = Vec3f{};
    p.force = Vec3f{};
  }
}

void Ragdoll::checkDistance() {
  // 0x6bc4d0.
  for (const RagdollTemplate::Distance& c : tmpl_->distances) {
    Particle& a = particles_[static_cast<std::size_t>(c.a)];
    Particle& b = particles_[static_cast<std::size_t>(c.b)];
    if (client_ && a.networked && b.networked) continue;
    const Vec3f diff = b.position - a.position;
    const float len = std::sqrt(dot3(diff, diff));
    if (!(0.0001f < len)) continue;
    const float wa = 1.0f / a.mass, wb = 1.0f / b.mass;
    const float k = (len - c.rest) / (len * (wa + wb));
    if (!pinned(a)) a.position = a.position + diff * (k * wa);
    if (!pinned(b)) b.position = b.position - diff * (k * wb);
  }
}

void Ragdoll::checkDistanceLessThan() {
  // 0x6bc6a0: only while the pair is closer than its length.
  for (const RagdollTemplate::DistanceLessThan& c : tmpl_->lessThans) {
    Particle& a = particles_[static_cast<std::size_t>(c.a)];
    Particle& b = particles_[static_cast<std::size_t>(c.b)];
    if (client_ && a.networked && b.networked) continue;
    const Vec3f diff = b.position - a.position;
    const float len = std::sqrt(dot3(diff, diff));
    if (!(0.0001f < len) || !(len < c.length)) continue;
    const float wa = 1.0f / a.mass, wb = 1.0f / b.mass;
    const float k = (len - c.length) / (len * (wa + wb));
    if (!pinned(a)) a.position = a.position + diff * (k * wa);
    if (!pinned(b)) b.position = b.position - diff * (k * wb);
  }
}

void Ragdoll::checkAngular() {
  // 0x6bc870.
  if (settings_.useAngularConstraints == 0) return;
  for (const RagdollTemplate::Angular& c : tmpl_->angulars) {
    Particle& a = particles_[static_cast<std::size_t>(c.a)];
    Particle& b = particles_[static_cast<std::size_t>(c.b)];
    Particle& cc = particles_[static_cast<std::size_t>(c.c)];
    const Vec3f diff = cc.position - a.position;
    const float len = std::sqrt(dot3(diff, diff));
    if (!(0.0001f < len)) continue;
    float target = c.target;
    float low = target;
    float high = 9999.0f;
    if (target < 0.0f) {
      low = 0.0f;
      target = -target;
      high = target;
    }
    if (!(len < low || high < len)) continue;
    const float wa = 1.0f / a.mass, wc = 1.0f / cc.mass;
    const float k = (len - target) / (len * (wa + wc));
    if (!pinned(a)) a.position = a.position + diff * (k * wa);
    if (!pinned(cc)) cc.position = cc.position - diff * (k * wc);

    // Back to the bones' lengths. Both corrections are weighted by c's mass —
    // b's own is not read (0x6bca28, 0x6bcb2d).
    const Vec3f ab = a.position - b.position;
    const float lab = std::sqrt(dot3(ab, ab));
    if (!(0.0001f < lab)) continue;
    const float k2 = (lab - c.ab) / ((wa + wc) * lab);
    if (!pinned(b)) b.position = b.position + ab * (k2 * wc);
    if (!pinned(a)) a.position = a.position - ab * (k2 * wa);
    const Vec3f bc = b.position - cc.position;
    const float lbc = std::sqrt(dot3(bc, bc));
    if (!(0.0001f < lbc)) continue;
    const float k3 = (lbc - c.bc) / (lbc * (wc + wc));
    if (!pinned(cc)) cc.position = cc.position + bc * (k3 * wc);
    if (!pinned(b)) b.position = b.position - bc * (k3 * wc);
  }
}

void Ragdoll::checkDihedral() {
  // 0x6bccf0.
  if (settings_.useAngularConstraints == 0) return;
  for (const RagdollTemplate::Dihedral& c : tmpl_->dihedrals) {
    Particle& a = particles_[static_cast<std::size_t>(c.a)];
    Particle& b = particles_[static_cast<std::size_t>(c.b)];
    Particle& cc = particles_[static_cast<std::size_t>(c.c)];
    Particle& d = particles_[static_cast<std::size_t>(c.d)];
    const Vec3f v = (d.position - cc.position) + b.position - a.position;
    const float len = std::sqrt(dot3(v, v));
    if (!(0.0001f < len) || !(len < c.target)) continue;
    const float wa = 1.0f / a.mass, wd = 1.0f / d.mass, wb = 1.0f / b.mass, wc = 1.0f / cc.mass;
    const float k = (len - c.target) / (len * (wa + wd));
    if (!pinned(a)) a.position = a.position + v * (k * wa);
    // d is moved when **c** is not pinned (0x6bce2f).
    if (!pinned(cc)) d.position = d.position - v * (k * wd);

    const Vec3f ab = a.position - b.position;
    const float lab = std::sqrt(dot3(ab, ab));
    if (!(0.0001f < lab)) continue;
    const float kab = (lab - c.ab) / (lab * (wa + wb));
    if (!pinned(b)) b.position = b.position + ab * (kab * wb);
    if (!pinned(a)) a.position = a.position - ab * (kab * wa);

    const Vec3f cd = cc.position - d.position;
    const float lcd = std::sqrt(dot3(cd, cd));
    if (!(0.0001f < lcd)) continue;
    const float kcd = (lcd - c.cd) / (lcd * (wc + wd));
    if (!pinned(d)) d.position = d.position + cd * (kcd * wd);
    if (!pinned(cc)) cc.position = cc.position - cd * (kcd * wc);

    const Vec3f bc = b.position - cc.position;
    const float lbc = std::sqrt(dot3(bc, bc));
    if (!(0.0001f < lbc)) continue;
    const float kbc = (lbc - c.bc) / (lbc * (wb + wc));
    if (!pinned(cc)) cc.position = cc.position + bc * (kbc * wc);
    if (!pinned(b)) b.position = b.position - bc * (kbc * wb);
  }
}

void Ragdoll::checkLegAngular() {
  // 0x6c0c00: no pin test at all.
  const Vec3f r = right();
  const Vec3f f = forward();
  for (const int leg : {1, 6}) {  // the static `leg` table, 0xf68b94
    const int hipIndex = tmpl_->particleOf(leg);
    const int kneeIndex = tmpl_->particleOf(leg + 1);
    const int footIndex = tmpl_->particleOf(leg + 3);
    if (hipIndex < 0 || kneeIndex < 0 || footIndex < 0) continue;
    Particle& hip = particles_[static_cast<std::size_t>(hipIndex)];
    Particle& knee = particles_[static_cast<std::size_t>(kneeIndex)];
    Particle& foot = particles_[static_cast<std::size_t>(footIndex)];
    const Vec3f thigh = knee.position - hip.position;
    const Vec3f n = engineNormalize(cross3(r, thigh));
    const float c = dot3(engineNormalize(thigh), f);
    if (settings_.legForwardFactor != 0.0f) {
      float bound = 0.0f;
      bool out = false;
      if (c < settings_.legBackAngle) {
        bound = settings_.legBackAngle;
        out = true;
      } else if (settings_.legForwardAngle < c) {
        bound = settings_.legForwardAngle;
        out = true;
      }
      const float s = (bound - c) * settings_.legForwardFactor;
      if (out && s != 0.0f) {
        hip.position = hip.position - n * s;
        knee.position = knee.position + n * s;
      }
    }
    if (settings_.legPointForwardFactor != 0.0f) {
      const float c2 = -dot3(n, engineNormalize(foot.position - knee.position));
      if (c2 < settings_.legPointForwardAngle) {
        const float s = (settings_.legPointForwardAngle - c2) * settings_.legPointForwardFactor;
        knee.position = knee.position + n * s;
        foot.position = foot.position - n * s;
      }
    }
  }
}

void Ragdoll::checkCapsules() {
  // `checkCapsuleConstraints` (0x6baa50): the shins, 2–4 against 7–9, as capsules
  // of `rd_boneSize` (`capsuleVsCapsuleCollision` 0x724400). No pin test.
  const int i2 = tmpl_->particleOf(2), i4 = tmpl_->particleOf(4);
  const int i7 = tmpl_->particleOf(7), i9 = tmpl_->particleOf(9);
  if (i2 < 0 || i4 < 0 || i7 < 0 || i9 < 0) return;
  Particle& a0 = particles_[static_cast<std::size_t>(i2)];
  Particle& a1 = particles_[static_cast<std::size_t>(i4)];
  Particle& b0 = particles_[static_cast<std::size_t>(i7)];
  Particle& b1 = particles_[static_cast<std::size_t>(i9)];
  float s = 0.0f, t = 0.0f;
  const float distance = closestBetweenSegments(a0.position, a1.position, b0.position,
                                                b1.position, s, t);
  if (!(kEpsilon <= std::fabs(distance))) return;
  const float radius = settings_.boneSize;
  const float depth = -((radius + radius) - distance);
  if (!(depth <= 0.0f)) return;
  const Vec3f onA = a0.position + (a1.position - a0.position) * s;
  const Vec3f onB = b0.position + (b1.position - b0.position) * t;
  const Vec3f normal = (onB - onA) * (-1.0f / distance);
  const Vec3f shift = normal * (0.5f * -depth * 0.5f);
  a0.position = a0.position + shift;
  a1.position = a1.position + shift;
  b0.position = b0.position - shift;
  b1.position = b1.position - shift;
}

void Ragdoll::checkCollision(const RagdollGround& ground) {
  // 0x6bf920.
  if (lod_ < 2) checkCapsules();

  // Against the objects near the body: a particle's motion, stretched by its
  // size, against their faces. A hit puts it back on the near side and is
  // remembered (+0x55) so the ground leaves it alone this pass.
  if (settings_.useObjectCollision != 0 && ground.objects) {
    for (Particle& p : particles_) {
      p.hit = false;
      const bool limbEnd = p.bone == 2 || p.bone == 7 || p.bone == 16 || p.bone == 32 ||
                           p.bone == 47;
      if ((2 <= lod_ && limbEnd) || pinned(p)) continue;
      Vec3f previous = p.previous;
      const Vec3f moved = p.position - previous;
      if (dot3(moved, moved) <= kEpsilon) {
        previous = Vec3f{p.position.x, p.size * 0.5f + p.position.y, p.position.z};
      }
      const Vec3f dir = engineNormalize(p.position - previous);
      const Vec3f motion = (p.position - previous) + dir * p.size;
      const auto hit = ground.objects(previous, motion, collectedAround_,
                                      settings_.completeCollRadius);
      if (hit && dot3(dir, hit->normal) < 0.0f) {
        p.hit = true;
        p.position = ((p.position + dir * p.size) - hit->normal * hit->along) - dir * p.size;
        p.previous = p.position;
        landed_ = true;
      }
    }
  }

  if (settings_.useLandCollision == 0 || !ground.cast || !ground.height) return;
  for (Particle& p : particles_) {
    if (p.hit) {
      p.hit = false;
      continue;
    }
    if (pinned(p)) continue;
    Vec3f previous = p.previous;
    const Vec3f moved = p.position - previous;
    if (dot3(moved, moved) <= kEpsilon) {
      previous = Vec3f{p.position.x, p.size * 0.5f + p.position.y, p.position.z};
    }
    const Vec3f dir = engineNormalize(p.position - previous);
    const Vec3f end = p.position + dir * p.size;
    const auto hit = ground.cast(previous, end);
    if (hit && dot3(dir, hit->normal) < 0.0f) {
      p.position = (end - hit->normal * hit->t) - dir * p.size;
      p.previous = p.position;
      landed_ = true;
      continue;
    }
    const float h = ground.height(p.position);
    if (p.position.y < h) {
      p.position.y = (h - p.position.y) * 0.01f + p.position.y;
      p.previous = p.position;
    }
  }
}

void Ragdoll::satisfyConstraints(const RagdollGround& ground) {
  // 0x6c12e0.
  // `getCollidingObjects` (0x6be1f0) looks around the centre once per pass.
  collectedAround_ = center();
  std::vector<Vec3f> saved;
  if (client_) {
    for (const Particle& p : particles_) {
      if (p.networked) saved.push_back(p.position);
    }
  }
  for (int iteration = 0; iteration < settings_.iterations; ++iteration) {
    if (settings_.useConstraints != 0) {
      checkAngular();
      checkDihedral();
      checkLegAngular();
      checkDistanceLessThan();
      checkDistance();
    }
    checkCollision(ground);
    if (client_) {
      std::size_t next = 0;
      for (Particle& p : particles_) {
        if (!p.networked) continue;
        p.position = saved[next++];
        p.previous = p.position;
      }
    }
  }
}

Vec3f Ragdoll::update(float frameTime, const RagdollGround& ground, int lod) {
  // 0x6c1850.
  lod_ = lod;
  float step = settings_.dT;
  if (0 < lod) step *= 1.5f;
  const float dt = frameTime * settings_.slowMotion;
  if (tmpl_->distances.empty()) return lastCenter_;
  if (!(0.0f <= dt) || frozen_ || (client_ && fresh_)) {
    lastCenter_ = center();
    return lastCenter_;
  }
  if (client_ && awake_ && 0.0f < clientTimer_) {
    clientTimer_ -= dt;
    if (clientTimer_ <= 0.0f) {
      // setIsClient(false) (0x6c1540): the networked particles onto the server's
      // last positions, then the constraints once.
      clientTimer_ = 0.0f;
      for (Particle& p : particles_) {
        if (!p.networked) continue;
        p.previous = p.position;
        p.position = p.network;
        p.acceleration = Vec3f{};
        p.force = Vec3f{};
      }
      client_ = false;
      satisfyConstraints(ground);
    }
  }
  if (!awake_) {
    // Asleep: the engine looks for something to wake it every 1/30 s among the
    // colliding objects, which we do not collect.
    accumulated_ += dt;
    if (0.033333335f <= accumulated_) accumulated_ = 0.0f;
    return lastCenter_;
  }
  accumulated_ += dt;
  time_ += dt;
  if (step <= accumulated_) {
    const int steps = static_cast<int>(accumulated_ / step);
    accumulated_ -= static_cast<float>(steps) * step;
    for (int i = 0; i < steps; ++i) {
      if (0.0f < gravityRamp_) gravityTime_ = std::min(gravityTime_ + step, gravityRamp_);
      accumulateForces();
      verlet(step);
      satisfyConstraints(ground);
    }
  }
  const Vec3f now = center();
  if (settings_.sleep <= 0.0f) {
    awake_ = true;
  } else {
    const Vec3f moved = now - lastCenter_;
    if (settings_.sleepDelta * settings_.sleepDelta < dot3(moved, moved)) {
      wakeUp();
    } else {
      sleepTimer_ -= dt;
      if (sleepTimer_ <= 0.0f) {
        sleepTimer_ = 0.0f;
        awake_ = false;
      }
    }
  }
  lastCenter_ = now;
  return lastCenter_;
}

void Ragdoll::applyOnSkeleton(std::vector<mesh::Mat4>& world) const {
  // 0x6c1d90.
  const RagdollTemplate& t = *tmpl_;
  const Vec3f centre = center();
  std::vector<Vec3f> local(particles_.size());
  for (std::size_t i = 0; i < particles_.size(); ++i) local[i] = particles_[i].position - centre;
  const Vec3f f = forward();
  const Vec3f r = right();
  const auto boneAt = [&world](int bone) {
    return bone >= 0 && static_cast<std::size_t>(bone) < world.size()
               ? translationOf(world[static_cast<std::size_t>(bone)])
               : Vec3f{};
  };
  const auto particleAt = [&](int bone) {
    const int index = t.particleOf(bone);
    return index >= 0 ? local[static_cast<std::size_t>(index)] : Vec3f{};
  };

  // Carried from one mapping to the next, as the engine's locals are.
  Vec3f legSide{1.0f, 0.0f, 0.0f};
  Vec3f hint{1.0f, 0.0f, 0.0f};
  for (const RagdollTemplate::BoneMapping& m : t.mappings) {
    if (m.bone < 0 || static_cast<std::size_t>(m.bone) >= world.size()) continue;
    mesh::Mat4 matrix = world[static_cast<std::size_t>(m.bone)];
    const int bone = std::abs(m.bone), a = std::abs(m.a), b = std::abs(m.b);
    const int ib = t.particleOf(bone), ia = t.particleOf(a), ic = t.particleOf(b);
    Vec3f translation = translationOf(matrix);
    Vec3f y;
    if (ib < 0) {
      const Vec3f from = ia >= 0 ? local[static_cast<std::size_t>(ia)] : boneAt(a);
      const Vec3f to = ic >= 0 ? local[static_cast<std::size_t>(ic)] : boneAt(b);
      y = engineNormalize(to - from);
      if (m.a >= 0 && m.b >= 0) translation = from + y * t.boneDistance(m.a, bone);
    } else {
      if (m.a >= 0 && m.b >= 0) translation = local[static_cast<std::size_t>(ib)];
      y = engineNormalize((ic >= 0 ? local[static_cast<std::size_t>(ic)] : Vec3f{}) -
                          (ia >= 0 ? local[static_cast<std::size_t>(ia)] : Vec3f{}));
      if (m.bone == 1 || m.bone == 6) {
        const Vec3f knee = particleAt(m.bone + 1);
        const Vec3f foot = particleAt(m.bone + 3);
        legSide = engineNormalize(cross3(foot - translation, knee - translation));
      }
    }

    // The x axis by the bone (0x6c229x..0x6c2700).
    const int which = m.bone;
    if ((0 < which && which < 5) || (5 < which && which < 10)) hint = legSide;
    if (which == 0 || which == 11 || which == 12 || which == 13 || which == 45 || which == 46 ||
        which == 47) {
      hint = r * -1.0f;
    } else if (which == 30) {
      hint = f;
    } else if (which == 14) {
      hint = f * -1.0f;
    } else {
      const auto armHint = [&](int first, int second, int third) {
        const Vec3f n = engineNormalize(
            cross3(particleAt(third) - particleAt(first), particleAt(second) - particleAt(first)));
        return cross3(n, y);
      };
      if (31 <= which && which <= 34) hint = armHint(31, 32, 34);
      if (15 <= which && which <= 19) hint = armHint(15, 16, 19);
    }

    Vec3f x = hint;
    Vec3f z;
    makeOrthonormalBasis(z, x, y);
    mesh::Mat4 out{};
    out.m[0] = x.x;
    out.m[1] = x.y;
    out.m[2] = x.z;
    out.m[4] = y.x;
    out.m[5] = y.y;
    out.m[6] = y.z;
    out.m[8] = z.x;
    out.m[9] = z.y;
    out.m[10] = z.z;
    if (m.a == m.b && static_cast<std::size_t>(m.a) < t.skeleton.bones.size()) {
      // A bone mapped to itself: its local matrix under its parent's as it now
      // stands. The engine takes the soldier's skeleton's local matrix, which is
      // the last one an animation left; ours is the rest pose's.
      const int parent = t.skeleton.bones[static_cast<std::size_t>(m.a)].parent;
      const mesh::Mat4 parentWorld = parent >= 0 && static_cast<std::size_t>(parent) < world.size()
                                         ? world[static_cast<std::size_t>(parent)]
                                         : mesh::Mat4{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
      out = multiply(parentWorld, restLocal(t.skeleton.bones[static_cast<std::size_t>(m.a)]));
    }
    out.m[12] = translation.x;
    out.m[13] = translation.y;
    out.m[14] = translation.z;
    out.m[15] = 1.0f;
    world[static_cast<std::size_t>(m.bone)] = out;
  }

  // The locked bones follow their parents at rest (0x6c26xx).
  for (std::size_t i = 0; i < world.size() && i < t.skeleton.bones.size() && i < 128; ++i) {
    if (!t.locked.test(i)) continue;
    const int parent = t.skeleton.bones[i].parent;
    if (parent < 0) {
      if (i + 1 < world.size()) world[i] = world[i + 1];
      continue;
    }
    world[i] = multiply(world[static_cast<std::size_t>(parent)], restLocal(t.skeleton.bones[i]));
  }
}

}  // namespace obf2::anim
