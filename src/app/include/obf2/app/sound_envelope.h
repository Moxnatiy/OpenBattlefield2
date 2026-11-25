#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "obf2/core/math.h"

namespace obf2::app {

// `ObjectTemplate.pitchEnvelope` / `volumeEnvelope` of a Sound template
// (docs/functions/sound-template.md). The text is a header of four floats, a
// bool and a point count, then three floats per point, every value followed
// by one separator (Linux `operator>>(istream&, SoundEnvelopeHeader&)`
// 0x7658c0, `operator>>(istream&, SoundEnvelopePoint&)` 0x765970).
//
// BF2Audio.dll builds it at 0x100194e0 and 0x10019260: the x range and the y
// range are swapped into order, every point's x is clamped to the x range and
// both of its y values to the y range, the points are sorted by x, and a
// table of `count` values per row is filled — one row of each point's low y,
// or with `randomize` 64 rows, rows 1..63 drawn from [low, high].
class SoundEnvelope {
 public:
  struct Point {
    float x = 0, low = 0, high = 0;
  };

  // A source of the C library's rand() range [0, 0x7fff]; Retail calls rand().
  using Random = int (*)(void* state);

  SoundEnvelope() = default;
  // False (and an empty envelope) when the text does not hold a whole header
  // and `count` whole points.
  static bool parse(std::string_view text, SoundEnvelope& out, Random random, void* state);

  bool empty() const { return points_.empty(); }
  float xMin() const { return xMin_; }
  float xMax() const { return xMax_; }
  float yMin() const { return yMin_; }
  float yMax() const { return yMax_; }
  bool randomize() const { return randomize_; }
  const std::vector<Point>& points() const { return points_; }
  std::size_t rows() const { return points_.empty() ? 0 : table_.size() / points_.size(); }

  // BF2Audio.dll 0x10019130. `row` is masked to 0..63 when randomized and
  // ignored otherwise. No points: yMax. At or before the first point: its
  // value; at or past the last: its value; between: linear in x, or the
  // right point's value when the two x differ by at most 2^-23.
  float evaluate(float x, unsigned row) const;

 private:
  float xMin_ = 0, xMax_ = 0, yMin_ = 0, yMax_ = 0;
  bool randomize_ = false;
  std::vector<Point> points_;
  std::vector<float> table_;  // rows × points
};

// Distance attenuation of a 3-D Sound template (docs/functions/sound-template.md).
// BF2Audio.dll 0x10015fc0, run by setVolume, setMinDistance and
// setHalfVolumeDistance (template vtable 0x1003b1a0 slots +0x20, +0x58, +0x60).
// The source then gets AL_REFERENCE_DISTANCE = minDistance and
// AL_ROLLOFF_FACTOR = rolloff (0x10013000) under
// AL_INVERSE_DISTANCE_CLAMPED (alDistanceModel(0xd002), OpenALProvider init).
struct SoundDistance {
  float minDistance = 0;
  float halfVolumeDistance = 0;
  float rolloff = 1;
  float maxDistance = 0;  // where the gain reaches `max-distance-cutoff`

  // `volume` is the template volume after setVolume's clamp to [0, 1];
  // `cutoff` is the engine property `max-distance-cutoff`.
  static SoundDistance compute(float volume, float minDistance, float halfVolumeDistance, float cutoff);
  // The inverse-distance-clamped gain: ref / (ref + rolloff·(max(d, ref) − ref)).
  float gain(float distance) const;
};

// `max-distance-cutoff`, `Settings/Sound.con` line 69 (`sound.setProperty
// max-distance-cutoff 0.015`); BF2Audio.dll's own default is 0.02
// (settings constructor 0x100072b0, property 6).
inline constexpr float kMaxDistanceCutoff = 0.015f;

// The final pitch of a played sample (BF2Audio.dll 0x100161a0): the
// template pitch, times the pitch envelope at (x, row + floor(x)) when there
// is one, clamped to [0.3, 2.0].
float soundPitch(float templatePitch, const SoundEnvelope& envelope, float x, unsigned row);

// `distance-delay-threshold 5` and `distance-delay-factor 1`,
// Settings/Sound.con lines 62 and 64 (BF2Audio defaults 5 and 1, properties
// 4 and 5 of 0x100072b0).
inline constexpr float kDistanceDelayThreshold = 5.0f;
inline constexpr float kDistanceDelayFactor = 1.0f;

// Start delay in sound ticks of 1/30 s (BF2Audio.dll 0x10006880): a 3-D play
// with a finite loop count (sound object +0x28, stored by every play entry
// from its argument or the template's getLoopCount, 0x10017080…0x10017290)
// further than the threshold waits
// floor((d − threshold) · factor · 0.002915452 · 30) ticks — 1/343 s per metre
// (0x1003a4ac), 30 ticks a second (0x100397d8).
int soundDelayTicks(float distance, bool is3d, int loopCount,
                    float threshold = kDistanceDelayThreshold, float factor = kDistanceDelayFactor);
inline constexpr float kSoundTick = 1.0f / 30.0f;

// Sample priority (BF2Audio.dll 0x10005510): 2 for a 2-D sound; for a 3-D
// one the volume at x = 0 (0x10016100) divided by 1 + (d − min)·rolloff/min
// past minDistance, less 1 at or past maxDistance — the "negative bias" of
// `max-distance-cutoff` (Settings/Sound.con line 67).
float soundPriority(bool is3d, float level, const SoundDistance& distance, float d);

// A pending sample takes the lowest-priority playing one's source when no
// source is free (0x10007140, 0x10006ba0): if its priority is higher, or
// equal and it was requested later. The newcomer is always the later one.
inline bool soundSteals(float pendingPriority, float lowestPlayingPriority) {
  return pendingPriority >= lowestPlayingPriority;
}

// Sources the engine allocates (BF2Audio.dll 0x10004c60): the voice count
// BF2.exe passes (0x4b1420: table 0x8a3550 {16, 32, 32, 128} by
// `AudioSettings.setSoundQuality` Low/Medium/High/index 3, unless
// `setForceNumberOfVoices` is above 0), 0 → 16, at least 2, plus
// `min-free-sources` (property 32, default 4, not set by Sound.con), capped
// at the driver's count − 1 (0x10011eb0 probes up to 128).
int soundSources(int quality, int forcedVoices, int driverSources = 128, int minFreeSources = 4);

// A sample's length in sound ticks (BF2Audio.dll 0x10016d40):
// round(frames · loops · 30 / rate · pitch); a looping sample (0 loops) is
// endless (0x7fffffff).
int soundLengthTicks(std::size_t frames, int rate, int loops, float templatePitch);

// Whether a request may still start (0x10005d80), `late` ticks after its
// due tick: a finite request never started is dropped past 60 ticks; a
// started (stolen) one is dropped once late reaches its length. Every start
// of a file without loop points is at frame 0 (0x1001ad00 leaves the loop
// start at 0).
bool soundStillDue(int late, int lengthTicks, bool started, int loops);

// A playing sample that lost its source (0x10006ba0) is queued again 5
// ticks on when more than 15 ticks of it remain; otherwise it is released.
inline bool soundRequeues(int late, int lengthTicks) { return late + 15 < lengthTicks; }
inline constexpr int kRequeueTicks = 5;

// Tinnitus (docs/functions/sound-template.md). `sound.tinnitusSetup`
// (Settings/Sound.con line 35: attack 1, hold 2, release 4, depth 0.88;
// BF2.exe stores them at 0x7df93e, defaults 0.5/2/2/0.8 at 0x7ddfb7) and
// BF2Audio.dll 0x10004830 at the trigger: each time under 0.1 becomes 0.1,
// depth is clamped to [0, 1], the volume envelope is 0 → 1 → 1 → 0 at
// 0, attack/T, (attack+hold)/T, 1 with T the sum, and the length is
// round(T·30) ticks, at least 20.
struct Tinnitus {
  SoundEnvelope envelope;
  int length = 0;    // ticks (+0x164 − +0x160)
  float depth = 0;   // +0x168

  static Tinnitus trigger(float attack, float hold, float release, float depth);
  // 0x10004ce0 per update: x = elapsed/length; the tinnitus sample's
  // volume is the envelope at x (evaluator +0x16c) and every other sound's
  // duck term is envelope·depth (+0x170).
  float volume(int elapsed) const;
  float duck(int elapsed) const { return volume(elapsed) * depth; }
  // Over once the envelope is back to 0 after the start (0x10004ce0 stops
  // the sample).
  bool over(int elapsed) const { return elapsed > 0 && volume(elapsed) <= 1.1920929e-07f; }
};

inline constexpr float kTinnitusAttack = 1.0f, kTinnitusHold = 2.0f, kTinnitusRelease = 4.0f,
                       kTinnitusDepth = 0.88f;
// `sound-tinnitus-range 1.05` (Settings/Sound.con line 51; BF2.exe default
// 0.6 at 0x8564b0): BF2.exe 0x5cd940 starts tinnitus when the local player
// is a soldier on foot within explosion radius × this.
inline constexpr float kTinnitusRange = 1.05f;

// The gain factor a non-EAX sound gets from tinnitus (BF2Audio.dll
// 0x100134d0): 1 − globalEffectLevel · duck · 0.9 (0x1003af68).
// globalEffectLevel is template +0x64, default 1 (0x10017560; setter
// 0x10015e60 clamps to [0, 1]); the tinnitus sound itself has 0.
inline float tinnitusDuck(float globalEffectLevel, float duck) { return 1.0f - globalEffectLevel * duck * 0.9f; }

// Obstruction (BF2.exe 0x7df090, the sound object's obstruction evaluator).
// Settings/Sound.con lines 16–20: `sound-obstruction-max 1.0`,
// `sound-listener-radius 0.15`, `sound-obstruction-radius 5` (BF2.exe
// defaults 0.8 at 0x86d330, 0.2 at 0x86d310).
inline constexpr float kObstructionMax = 1.0f;
inline constexpr float kListenerRadius = 0.15f;
inline constexpr float kObstructionRadius = 5.0f;

// A ray from `start` along `motion`: the distance along it to what blocks
// it, or nothing when it is clear. The engine's query is
// `ObjectManager::intersectLine` (BF2.exe 0xa08f58 slot +0x68; Linux
// 0x696030) with the bools (near, true, false, true) and mask 0:
// * near (`dynamic` here) — the dynamic culler (ObjectManager +0x10,
//   "dynamic culler" in the constructor, Linux 0x690e60) is searched too;
//   the static culler (+0x20) always. near is d ≤ 40, the three-ray case.
// * false — no heightmap test (the terrain block of 0x696030 is skipped):
//   the terrain never obstructs a sound.
// * the mesh test (`intersectLineWithObject` 0x6958f0 →
//   `CollisionMesh::getDistance` 0x7187a0 → `Bsp::getClosestFace` 0x709b20)
//   takes lod `getOverrideLineTestLod` (+0x2b0, −1 from the constructor,
//   which counts as 0): the Projectile collision; faces one-sided (flip
//   false, `checkFaceAndEdgeCollision` 0x724ae0 with its bool false: the
//   ray must run against the normal); the first face a leaf accepts ends
//   that mesh's walk (true); of the objects, the nearest hit.
// * the predicator (vtable 0x93c12c: 0x7df780 object, 0x7df7c0 mask 0x100,
//   0x5a1860 material, always true) skips soldiers (CID_SoldierTemplate
//   0x9493 at 0x91c240), objects without flag 0x100 (+5 bit 0), and objects
//   whose root is the sound's own object (sound object +0xcc).
using SoundRay = std::optional<float> (*)(void* context, Vec3f start, Vec3f motion, bool dynamic);

// d = |source − listener| − source radius (≤ 0: `soundRadius` 0.05,
// template getRadius) − listener radius; d ≤ 0 → 0. Rays: one when d > 40
// (0x8ebba4), else three — along the direction, and along two
// perpendiculars (cross with (0,1,0), or (1,0,0) when |dir.y| ≥ 0.99,
// 0x8f7b70) — each from listener + offset·listenerRadius to source +
// offset·sourceRadius (the first ends short of the source by its radius).
// Any clear ray → 0. All blocked → clamp((1 − (atan(h/R) + atan((d − h)/R))
// / π) · max, 0, 1) with h the first ray's hit distance and R the
// obstruction radius.
//
// What the sound object keeps between calls, its user data (ISound slots
// +0x64 set, +0x68 get, Linux `NullSound` vtable). The wrapper's create
// (BF2.exe 0x7de130) stores the sound's object in slot 0 and 0 in slots 1
// and 2.
// * `ray` (slot 1): with `sound-obstruction-opt0` (var 0xa25b08, name
//   0x93c590, default 1 at 0x86d37f; Sound.con does not set it) the rays
//   start at the index of the last clear one: i runs from ray (0 when there
//   is one ray) to ray + count, casting ray i % count; a clear one stores
//   its index. With one ray and a stored index of 2 that ray is cast up to
//   three times, as 0x7df090 does.
// * `cache` (slot 2): with `sound-obstruction-opt1` (0xa25bc0, 0x93c5a8,
//   default 1) a call while its high 16 bits are above 0 counts them down
//   and returns the low 16 bits × 1/65535 (0x920c3c, 0x37800080) without a
//   ray; otherwise it is cleared, and after the rays set to round(o · 65535)
//   (0x8fcbf8) | n << 16, n = min(round(e^(0.01 · min(d, 1000) + 1)) +
//   `sound-obstruction-reuse-frames` (0xa25a44, default 0),
//   `sound-obstruction-max-reuse-frames` (0xa249f8, default 15)); 0.01 at
//   0x892adc, 1000 at 0x8aa3c8.
// `sound-obstruction-disable` (0xa2449c, default 0) returns 0 before
// anything. Slot 0 — a hit whose root is the root of the sound's own object
// gives 0 — needs the hit object, which `SoundRay` does not return: the
// sounds ported (an effect bundle's: Linux `EffectBundle::EffectBundle`
// 0x594e96 passes the bundle itself) belong to unattached bundles with no
// collision, so for them the test never fires.
struct SoundObstructionMemory {
  unsigned ray = 0;
  std::uint32_t cache = 0;
};
inline constexpr bool kObstructionOpt0 = true;
inline constexpr bool kObstructionOpt1 = true;
inline constexpr unsigned kObstructionReuseFrames = 0;
inline constexpr unsigned kObstructionMaxReuseFrames = 15;
float soundObstruction(Vec3f listener, Vec3f source, float sourceRadius, SoundRay ray, void* context,
                       SoundObstructionMemory* memory = nullptr,
                       float listenerRadius = kListenerRadius, float obstructionRadius = kObstructionRadius,
                       float obstructionMax = kObstructionMax);

// BF2Audio.dll 0x10013130 without EAX: the obstruction moves at most 0.1
// per update toward the evaluator's value (straight to it while starting),
// is multiplied by globalEffectLevel, and a 3-D sound's gain gets
// clamp(1 − (listener-3d-obstruction + o) · listener-3d-obstruction-factor)
// — properties 28 (0, not set by Sound.con) and 29 (0.85).
inline float obstructionStep(float previous, float target, int updates) {
  const float step = 0.1f * static_cast<float>(std::max(1, updates));
  return previous + std::clamp(target - previous, -step, step);
}
inline float obstructionGain(float obstruction) {
  return std::clamp(1.0f - (0.0f + obstruction) * 0.85f, 0.0f, 1.0f);
}

// The file of a play (BF2Audio.dll 0x10016a60): with n = min(files,
// maxSamples[quality]) above 1, rand() % n, drawn again while it equals the
// sound object's previous index (+0x60, −1 from the constructor 0x100165b0).
int soundFileIndex(int files, int previous, SoundEnvelope::Random random, void* state);

// Retail's mixer is BF2OpenAL.dll's software path; 0x100464d0 works out a
// mono source's per-channel gain and pitch. Stereo output: the source
// position relative to the listener, in the listener's frame (right =
// normalize(at × up), up, −at; BF2Audio.dll 0x10005300 and 0x100050f0 put
// positions and velocities into the listener's space first), normalized;
// p = (x + 1) · 0.5 (0x1004b27c), 0.5 when the distance is 0; left gain
// √(1 − p), right √p, times the distance gain and the source gain.
// Returns p.
float soundPan(Vec3f relative, Vec3f listenerRight);

// Doppler, same function: with u = normalize(listener − source), vl =
// u·listener velocity and vs = u·source velocity, each clamped to
// SS/DF − 1 when at or above SS/DF and to 1 − SS/DF when at or below
// −SS/DF, pitch × (SS − vl·DF) / (SS − vs·DF), SS = speed of sound ×
// doppler velocity. DF 0 leaves the pitch. Context defaults (alcOpenDevice
// 0x10036056..): DF 1, doppler velocity 1, speed of sound 343.3
// (0x1004cd70); BF2Audio.dll never calls alSpeedOfSound or
// alDopplerVelocity. DF is `sound.dopplerEffect` (Settings/Sound.con line
// 73: 1; BF2.exe 0x7dacf0 registers it), below 0 → 0 (BF2Audio.dll
// 0x10004710) into alDopplerFactor (provider slot +0x24, 0x10011570).
inline constexpr float kSpeedOfSound = 343.3f;
inline constexpr float kDopplerFactor = 1.0f;
float dopplerPitch(float pitch, Vec3f relative, Vec3f listenerVelocity, Vec3f sourceVelocity,
                   float factor = kDopplerFactor, float speed = kSpeedOfSound);

// The listener velocity BF2Audio.dll hands OpenAL (0x10011650): longer
// than 300 (0x1003aa18; 300² at 0x1003aa1c) → scaled to 300.
Vec3f soundListenerVelocity(Vec3f velocity);

}  // namespace obf2::app
