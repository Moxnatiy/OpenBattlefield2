#include "obf2/app/sound_envelope.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>

namespace obf2::app {

namespace {

// 1/32767, BF2Audio.dll 0x1003b298 (0x38000100), the rand() scale in 0x10019260.
constexpr float kRandScale = 3.0518509e-05f;
// 2^-23, BF2Audio.dll 0x10039fd8 (0x34000000): the "same x" test in 0x10019130.
constexpr float kSameX = 1.1920929e-07f;
// A randomized envelope keeps 64 rows (0x10019260: `(randomize ? 0x3f : 0) + 1`).
constexpr unsigned kRandomRows = 64;

}  // namespace

bool SoundEnvelope::parse(std::string_view text, SoundEnvelope& out, Random random, void* state) {
  out = SoundEnvelope{};
  // Linux SoundObjectTemplate::setPitchEnvelope (0x766150) reads the header
  // with operator>> (0x7658c0), sizes the point vector to `count`, and reads
  // each point with operator>> (0x765970). Every value is followed by one
  // character, the '/'. What it does with a short text is not established;
  // here a short text gives no envelope.
  std::istringstream in{std::string(text)};
  char separator = 0;
  SoundEnvelope e;
  unsigned count = 0;
  in >> e.xMin_ >> separator >> e.xMax_ >> separator >> e.yMin_ >> separator >> e.yMax_ >> separator >>
      e.randomize_ >> separator >> count >> separator;
  if (!in) return false;
  e.points_.resize(count);
  for (auto& point : e.points_) {
    in >> point.x >> separator >> point.low >> separator >> point.high >> separator;
    if (!in) return false;
  }
  // 0x10019260.
  if (e.xMax_ < e.xMin_) std::swap(e.xMin_, e.xMax_);
  if (e.yMax_ < e.yMin_) std::swap(e.yMin_, e.yMax_);
  for (auto& point : e.points_) {
    point.x = std::clamp(point.x, e.xMin_, e.xMax_);
    point.low = std::clamp(point.low, e.yMin_, e.yMax_);
    point.high = std::clamp(point.high, e.yMin_, e.yMax_);
  }
  // 0x10019f00 orders the points; by x is how 0x10019130 walks them.
  std::stable_sort(e.points_.begin(), e.points_.end(),
                   [](const Point& a, const Point& b) { return a.x < b.x; });
  const unsigned rows = e.randomize_ ? kRandomRows : 1;
  const std::size_t n = e.points_.size();
  e.table_.assign(n * rows, 0.0f);
  for (std::size_t i = 0; i < n; ++i) e.table_[i] = e.points_[i].low;
  for (unsigned row = 1; row < rows; ++row) {
    for (std::size_t i = 0; i < n; ++i) {
      const auto& p = e.points_[i];
      const int r = random ? random(state) : 0;
      e.table_[row * n + i] = (p.high - p.low) * static_cast<float>(r) * kRandScale + p.low;
    }
  }
  out = std::move(e);
  return true;
}

float SoundEnvelope::evaluate(float x, unsigned row) const {
  const std::size_t n = points_.size();
  if (n == 0) return yMax_;
  const unsigned r = randomize_ ? (row & 0x3f) : 0;
  const float* values = table_.data() + static_cast<std::size_t>(r) * n;
  if (x <= points_.front().x) return values[0];
  if (x >= points_.back().x) return values[n - 1];
  for (std::size_t i = 1; i < n; ++i) {
    if (x < points_[i].x) {
      const float dx = points_[i].x - points_[i - 1].x;
      if (std::fabs(dx) <= kSameX) return values[i];
      return values[i - 1] + (x - points_[i - 1].x) / dx * (values[i] - values[i - 1]);
    }
  }
  return values[0];
}

SoundDistance SoundDistance::compute(float volume, float minDistance, float halfVolumeDistance, float cutoff) {
  // 0x10015fc0. Constants: 0.01 at 0x1003a80c, 0.511872 at 0x1003b118
  // (0x3f030a0b), 2^-23 at 0x10039fd8.
  constexpr float kNear = 0.01f;
  constexpr float kHalfGain = 0.51187199f;
  SoundDistance d;
  volume = std::clamp(volume, 0.0f, 1.0f);  // setVolume, 0x10016420
  d.minDistance = minDistance < kNear ? kNear : minDistance;
  d.halfVolumeDistance = halfVolumeDistance;
  d.rolloff = 1.0f;
  if (d.minDistance + kNear < d.halfVolumeDistance) {
    const float half = volume * kHalfGain;
    if (half > kSameX)
      d.rolloff = (1.0f / half - 1.0f) * (d.minDistance / (d.halfVolumeDistance - d.minDistance));
  } else {
    d.halfVolumeDistance = 0.0f;
  }
  d.maxDistance = (1.0f / (cutoff * d.rolloff) - 1.0f / d.rolloff + 1.0f) * d.minDistance;
  return d;
}

float SoundDistance::gain(float distance) const {
  // AL_INVERSE_DISTANCE_CLAMPED with no AL_MAX_DISTANCE set by 0x10013000.
  const float ref = minDistance;
  const float d = std::max(distance, ref);
  const float denominator = ref + rolloff * (d - ref);
  return denominator > 0.0f ? ref / denominator : 1.0f;
}

float soundPitch(float templatePitch, const SoundEnvelope& envelope, float x, unsigned row) {
  float pitch = templatePitch;
  if (!envelope.empty())
    pitch = envelope.evaluate(x, row + static_cast<unsigned>(static_cast<int>(std::floor(x)))) * templatePitch;
  return std::clamp(pitch, 0.3f, 2.0f);
}

int soundDelayTicks(float distance, bool is3d, int loopCount, float threshold, float factor) {
  if (!is3d || loopCount <= 0 || !(distance > threshold)) return 0;
  constexpr float kSecondsPerMetre = 0.002915452f;  // 0x1003a4ac
  constexpr float kTicksPerSecond = 30.0f;          // 0x100397d8
  return static_cast<int>(std::floor((distance - threshold) * factor * kSecondsPerMetre * kTicksPerSecond));
}

float soundPriority(bool is3d, float level, const SoundDistance& distance, float d) {
  if (!is3d) return 2.0f;
  float priority = level;
  if (distance.minDistance < d) {
    priority = level / ((d - distance.minDistance) * distance.rolloff / distance.minDistance + 1.0f);
    if (distance.maxDistance <= d) priority -= 1.0f;
  }
  return priority;
}

int soundSources(int quality, int forcedVoices, int driverSources, int minFreeSources) {
  constexpr int kVoices[4] = {16, 32, 32, 128};  // BF2.exe 0x8a3550
  int voices = kVoices[std::clamp(quality, 0, 3)];
  if (forcedVoices > 0) voices = forcedVoices;
  if (voices == 0) voices = 16;
  else if (voices < 2) voices = 2;
  return std::min(voices + minFreeSources, driverSources - 1);
}

int soundLengthTicks(std::size_t frames, int rate, int loops, float templatePitch) {
  if (loops == 0) return 0x7fffffff;
  if (rate <= 0) return 0;
  return static_cast<int>(std::lround(static_cast<float>(frames * static_cast<std::size_t>(loops)) *
                                      (30.0f / static_cast<float>(rate)) * templatePitch));
}

bool soundStillDue(int late, int lengthTicks, bool started, int loops) {
  // Never started: past 60 ticks a finite one is dropped, else its due
  // tick becomes now (late 0). Started (and stolen): late counts from the
  // first due tick.
  if (!started) return loops == 0 || late <= 60;
  return late < lengthTicks;
}

Tinnitus Tinnitus::trigger(float attack, float hold, float release, float depth) {
  attack = std::max(attack, 0.1f);    // 0x10039fb0
  hold = std::max(hold, 0.1f);
  release = std::max(release, 0.1f);
  Tinnitus t;
  t.depth = std::clamp(depth, 0.0f, 1.0f);
  const float total = attack + hold + release;
  const float a = attack / total, b = (attack + hold) / total;
  char text[160];
  std::snprintf(text, sizeof text, "0/1/0/1/0/4/0/0/0/%.9g/1/0/%.9g/1/0/1/0/0/", a, b);
  SoundEnvelope::parse(text, t.envelope, nullptr, nullptr);
  t.length = std::max(20, static_cast<int>(std::lround(total * 30.0f)));
  return t;
}

float Tinnitus::volume(int elapsed) const {
  if (length <= 0) return 0.0f;
  return envelope.evaluate(static_cast<float>(elapsed) / static_cast<float>(length), 0);
}

int soundFileIndex(int files, int previous, SoundEnvelope::Random random, void* state) {
  if (files <= 1 || !random) return 0;
  int index = previous;
  while (index == previous) index = random(state) % files;
  return index;
}

float soundObstruction(Vec3f listener, Vec3f source, float sourceRadius, SoundRay ray, void* context,
                       SoundObstructionMemory* memory, float listenerRadius, float obstructionRadius,
                       float obstructionMax) {
  if (!ray) return 0.0f;
  if (sourceRadius <= 0.0f) sourceRadius = 0.05f;  // 0x3d4ccccd at 0x7df090
  const Vec3f relative = source - listener;
  const float full = length(relative);
  const float d = full - sourceRadius - listenerRadius;
  if (d <= 0.0f) return 0.0f;
  if (kObstructionOpt1 && memory) {
    const std::uint32_t left = memory->cache >> 16;
    if (memory->cache != 0 && left != 0) {
      memory->cache = ((left - 1) << 16) | (memory->cache & 0xffffu);
      return static_cast<float>(memory->cache & 0xffffu) * 1.5259022e-05f;  // 0x920c3c
    }
  }
  if (memory) memory->cache = 0;
  const float value = [&] {
  const Vec3f dir = relative * (1.0f / full);
  const Vec3f up = std::fabs(dir.y) >= 0.99f ? Vec3f{1, 0, 0} : Vec3f{0, 1, 0};
  const Vec3f a = normalize(cross(up, dir));
  const Vec3f b = normalize(cross(dir, a));
  struct Segment {
    Vec3f start, motion;
  };
  const Segment rays[3] = {
      {listener + dir * listenerRadius, relative - dir * (sourceRadius + listenerRadius)},
      {listener + a * listenerRadius, (source + a * sourceRadius) - (listener + a * listenerRadius)},
      {listener + b * listenerRadius, (source + b * sourceRadius) - (listener + b * listenerRadius)},
  };
  const unsigned count = d > 40.0f ? 1 : 3;
  const bool near = count == 3;  // intersectLine's first bool (0x7df090)
  const unsigned first = kObstructionOpt0 && memory ? memory->ray : 0;
  const unsigned end = first + count;
  float hit = 0.0f;
  for (unsigned i = count > 1 ? first : 0; i < end; ++i) {
    const unsigned index = i % count;
    const auto blocked = ray(context, rays[index].start, rays[index].motion, near);
    if (!blocked) {
      if (memory) memory->ray = index;
      return 0.0f;
    }
    if (index == 0) hit = *blocked;
  }
  constexpr float kInvPi = 0.31830987f;  // 0x913304
  const float R = obstructionRadius;
  const float theta = std::atan(hit / R) + std::atan((d - hit) / R);
  return std::clamp((1.0f - theta * kInvPi) * obstructionMax, 0.0f, 1.0f);
  }();
  if (kObstructionOpt1 && memory) {
    const float e = std::exp(0.01f * std::min(d, 1000.0f) + 1.0f);
    const unsigned reuse = std::min(static_cast<unsigned>(std::nearbyint(e)) + kObstructionReuseFrames,
                                    kObstructionMaxReuseFrames);
    memory->cache = (static_cast<std::uint32_t>(std::nearbyint(value * 65535.0f)) & 0xffffu) | (reuse << 16);
  }
  return value;
}

float soundPan(Vec3f relative, Vec3f listenerRight) {
  const float distance = length(relative);
  if (distance == 0.0f) return 0.5f;
  return (dot(relative * (1.0f / distance), listenerRight) + 1.0f) * 0.5f;
}

float dopplerPitch(float pitch, Vec3f relative, Vec3f listenerVelocity, Vec3f sourceVelocity,
                   float factor, float speed) {
  if (factor == 0.0f) return pitch;
  const float distance = length(relative);
  const Vec3f u = distance != 0.0f ? relative * (-1.0f / distance) : Vec3f{};
  const float limit = speed / factor;
  const auto clampSpeed = [limit](float v) {
    if (v >= limit) return limit - 1.0f;
    if (v <= -limit) return 1.0f - limit;
    return v;
  };
  const float vl = clampSpeed(dot(listenerVelocity, u));
  const float vs = clampSpeed(dot(sourceVelocity, u));
  return (speed - vl * factor) * pitch / (speed - vs * factor);
}

Vec3f soundListenerVelocity(Vec3f velocity) {
  if (dot(velocity, velocity) > 90000.0f) return normalize(velocity) * 300.0f;
  return velocity;
}

}  // namespace obf2::app
