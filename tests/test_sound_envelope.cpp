#include <cmath>
#include <vector>

#include "check.h"
#include "obf2/app/sound_envelope.h"
#include "obf2/app/sound_voices.h"

using namespace obf2::app;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// rand() as Visual C++ defines it.
int msvcRand(void* state) {
  auto& seed = *static_cast<std::uint32_t*>(state);
  seed = seed * 214013u + 2531011u;
  return static_cast<int>((seed >> 16) & 0x7fff);
}

int maxRand(void*) { return 0x7fff; }

// S_e_exp_grenade_Start.pitchEnvelope
// (Objects_server.zip:Effects/impacts/explosions/e_exp_grenade/e_exp_grenade.tweak:51).
constexpr const char* kGrenadePitch = "0/1/0.9/1.1/1/1/0/0.9/1.1/";

void testGrenadeHeader() {
  SoundEnvelope e;
  std::uint32_t seed = 1;
  CHECK(SoundEnvelope::parse(kGrenadePitch, e, msvcRand, &seed));
  CHECK(e.xMin() == 0.0f && e.xMax() == 1.0f);
  CHECK(near(e.yMin(), 0.9f) && near(e.yMax(), 1.1f));
  CHECK(e.randomize());
  CHECK(e.points().size() == 1);
  CHECK(e.points()[0].x == 0.0f && near(e.points()[0].low, 0.9f) && near(e.points()[0].high, 1.1f));
  CHECK(e.rows() == 64);
}

// Row 0 is the low value; the 63 others are low + (high − low)·rand/32767.
void testRows() {
  SoundEnvelope e;
  CHECK(SoundEnvelope::parse(kGrenadePitch, e, maxRand, nullptr));
  CHECK(near(e.evaluate(0.0f, 0), 0.9f));
  CHECK(near(e.evaluate(0.0f, 1), 1.1f));
  CHECK(near(e.evaluate(0.0f, 64), 0.9f));  // masked to 0..63
  std::uint32_t seed = 7;
  CHECK(SoundEnvelope::parse(kGrenadePitch, e, msvcRand, &seed));
  for (unsigned row = 1; row < 64; ++row) {
    const float v = e.evaluate(0.5f, row);
    CHECK(v >= 0.9f - 1e-6f && v <= 1.1f + 1e-6f);
  }
}

// Not randomized: one row, every row index reads it.
void testPlainEnvelope() {
  SoundEnvelope e;
  CHECK(SoundEnvelope::parse("0/1/0/2/0/2/0/0.5/0.5/1/1.5/1.5/", e, maxRand, nullptr));
  CHECK(e.rows() == 1);
  CHECK(near(e.evaluate(-1.0f, 5), 0.5f));
  CHECK(near(e.evaluate(0.5f, 5), 1.0f));  // linear between the points
  CHECK(near(e.evaluate(2.0f, 5), 1.5f));
}

// The header ranges are put in order and the points clamped and sorted.
void testClampAndSort() {
  SoundEnvelope e;
  CHECK(SoundEnvelope::parse("1/0/2/1/0/2/5/3/0/0.5/1.5/1.5/", e, maxRand, nullptr));
  CHECK(e.xMin() == 0.0f && e.xMax() == 1.0f && e.yMin() == 1.0f && e.yMax() == 2.0f);
  CHECK(e.points()[0].x == 0.5f && e.points()[1].x == 1.0f);
  CHECK(e.points()[1].low == 2.0f && e.points()[1].high == 1.0f);
  CHECK(!SoundEnvelope::parse("0/1/0/1/1/2/0/1/", e, maxRand, nullptr));
  CHECK(e.empty() && e.evaluate(0.0f, 0) == 0.0f);
}

// 0x100161a0: pitch × envelope, clamped to [0.3, 2.0].
void testPitchClamp() {
  SoundEnvelope e;
  CHECK(near(soundPitch(1.0f, e, 0.0f, 0), 1.0f));
  CHECK(near(soundPitch(5.0f, e, 0.0f, 0), 2.0f));
  CHECK(near(soundPitch(0.1f, e, 0.0f, 0), 0.3f));
  CHECK(SoundEnvelope::parse(kGrenadePitch, e, maxRand, nullptr));
  CHECK(near(soundPitch(1.0f, e, 0.0f, 0), 0.9f));
  CHECK(near(soundPitch(1.0f, e, 0.0f, 1), 1.1f));
}

// S_e_exp_grenade_Start: minDistance 9, halfVolumeDistance 20, volume 1
// (e_exp_grenade.tweak:47-53). At half distance the gain is 0.511872·volume.
void testGrenadeDistance() {
  const auto d = SoundDistance::compute(1.0f, 9.0f, 20.0f, kMaxDistanceCutoff);
  CHECK(near(d.rolloff, (1.0f / 0.511872f - 1.0f) * 9.0f / 11.0f));
  CHECK(near(d.gain(0.0f), 1.0f));
  CHECK(near(d.gain(9.0f), 1.0f));
  CHECK(near(d.gain(20.0f), 0.511872f));
  CHECK(near(d.gain(d.maxDistance), kMaxDistanceCutoff, 1e-5f));
  CHECK(d.maxDistance > 700.0f && d.maxDistance < 800.0f);
  // Half the volume halves the half-distance gain.
  const auto quiet = SoundDistance::compute(0.5f, 9.0f, 20.0f, kMaxDistanceCutoff);
  CHECK(near(quiet.gain(20.0f), 0.255936f));
}

// halfVolumeDistance not past minDistance + 0.01: rolloff 1. The template
// constructor's minDistance 1 and halfVolumeDistance 0 (0x10017560) give
// a cutoff at 1/0.015 m.
void testDefaults() {
  const auto d = SoundDistance::compute(1.0f, 1.0f, 0.0f, kMaxDistanceCutoff);
  CHECK(d.rolloff == 1.0f);
  CHECK(near(d.maxDistance, 1.0f / kMaxDistanceCutoff, 1e-3f));
  CHECK(near(d.gain(2.0f), 0.5f));
  const auto tiny = SoundDistance::compute(1.0f, 0.0f, 0.0f, kMaxDistanceCutoff);
  CHECK(near(tiny.minDistance, 0.01f));
}

// 0x10006880: 1/343 s per metre past 5 m, in whole 1/30 s ticks, only for a
// finite 3-D play.
void testDelay() {
  CHECK(soundDelayTicks(5.0f, true, 1) == 0);
  CHECK(soundDelayTicks(16.0f, true, 1) == 0);    // 11 m · 30/343 = 0.96
  CHECK(soundDelayTicks(17.0f, true, 1) == 1);    // 1.05
  CHECK(soundDelayTicks(120.0f, true, 1) == 10);  // 115 m · 30/343 = 10.06
  CHECK(soundDelayTicks(120.0f, true, 0) == 0);   // looping
  CHECK(soundDelayTicks(120.0f, false, 1) == 0);  // 2-D
}

// 0x10005510: expected gain, less 1 at or past maxDistance; 2-D is 2.
void testPriority() {
  const auto d = SoundDistance::compute(1.0f, 9.0f, 20.0f, kMaxDistanceCutoff);
  CHECK(soundPriority(false, 1.0f, d, 100.0f) == 2.0f);
  CHECK(near(soundPriority(true, 1.0f, d, 5.0f), 1.0f));
  CHECK(near(soundPriority(true, 1.0f, d, 20.0f), 0.511872f));
  const float past = soundPriority(true, 1.0f, d, d.maxDistance + 1.0f);
  CHECK(past < 0.0f && past > -1.0f);
  CHECK(soundSteals(0.5f, 0.5f));
  CHECK(!soundSteals(0.4f, 0.5f));
}

// 0x10016a60: rand() % n, drawn again while equal to the previous index.
void testFileIndex() {
  std::uint32_t seed = 1;
  CHECK(soundFileIndex(1, -1, msvcRand, &seed) == 0);
  std::uint32_t a = 1, b = 1;
  CHECK(soundFileIndex(8, -1, msvcRand, &a) == msvcRand(&b) % 8);
  for (int previous = 0; previous < 5; ++previous) {
    CHECK(soundFileIndex(5, previous, msvcRand, &seed) != previous);
  }
}

// BF2Audio 0x10004c60 over BF2.exe 0x4b1420's voice table.
void testSources() {
  CHECK(soundSources(0, 0) == 20);      // Low: 16 + 4
  CHECK(soundSources(1, 0) == 36);
  CHECK(soundSources(3, 0) == 127);     // 128 + 4, capped at 128 − 1
  CHECK(soundSources(0, 1) == 6);       // forced 1 → at least 2
  CHECK(soundSources(2, 8, 10) == 9);   // driver cap
}

// 0x10016d40 length, 0x10005d80 drop rules, 0x10006ba0 re-queue.
void testScheduling() {
  CHECK(soundLengthTicks(44100, 44100, 1, 1.0f) == 30);
  CHECK(soundLengthTicks(44100, 44100, 2, 0.5f) == 30);
  CHECK(soundLengthTicks(44100, 44100, 0, 1.0f) == 0x7fffffff);
  CHECK(soundStillDue(60, 30, false, 1));
  CHECK(!soundStillDue(61, 30, false, 1));
  CHECK(soundStillDue(500, 30, false, 0));
  CHECK(soundStillDue(29, 30, true, 1));
  CHECK(!soundStillDue(30, 30, true, 1));
  CHECK(soundRequeues(10, 30));
  CHECK(!soundRequeues(15, 30));
}

// 0x10004830 with Sound.con's 1 2 4 0.88: 7 s, 210 ticks, full from 30 to
// 90 ticks, then down to 0 at 210.
void testTinnitus() {
  const auto t = Tinnitus::trigger(kTinnitusAttack, kTinnitusHold, kTinnitusRelease, kTinnitusDepth);
  CHECK(t.length == 210);
  CHECK(near(t.volume(0), 0.0f));
  CHECK(near(t.volume(15), 0.5f));
  CHECK(near(t.volume(30), 1.0f));
  CHECK(near(t.volume(90), 1.0f));
  CHECK(near(t.volume(150), 0.5f));
  CHECK(near(t.duck(60), 0.88f));
  CHECK(!t.over(0) && !t.over(100) && t.over(210));
  CHECK(near(tinnitusDuck(1.0f, 0.88f), 1.0f - 0.792f));
  CHECK(tinnitusDuck(0.0f, 0.88f) == 1.0f);
  // Short times become 0.1 s; the length is at least 20 ticks.
  CHECK(Tinnitus::trigger(0, 0, 0, 2).length == 20);
  CHECK(Tinnitus::trigger(0, 0, 0, 2).depth == 1.0f);
}

struct Wall {
  float x = 0;  // a wall at this x blocks every ray that crosses it
  int calls = 0;
  int dynamicCalls = 0;
};
std::optional<float> wallRay(void* context, obf2::Vec3f start, obf2::Vec3f motion, bool dynamic) {
  auto& wall = *static_cast<Wall*>(context);
  ++wall.calls;
  if (dynamic) ++wall.dynamicCalls;
  const float endX = start.x + motion.x;
  if ((start.x - wall.x) * (endX - wall.x) > 0.0f || motion.x == 0.0f) return std::nullopt;
  const float t = (wall.x - start.x) / motion.x;
  return t * length(motion);
}
std::optional<float> clearRay(void*, obf2::Vec3f, obf2::Vec3f, bool) { return std::nullopt; }

// BF2.exe 0x7df090.
void testObstruction() {
  Wall wall{10.0f};
  CHECK(soundObstruction({0, 0, 0}, {20, 0, 0}, 1.0f, clearRay, nullptr) == 0.0f);
  const float o = soundObstruction({0, 0, 0}, {20, 0, 0}, 1.0f, wallRay, &wall);
  CHECK(wall.calls == 3);  // within 40 m: three rays
  CHECK(wall.dynamicCalls == 3);  // and the dynamic culler with them
  // d = 20 − 1 − 0.15 = 18.85, hit h = 10 − 0.15 = 9.85, R = 5.
  const float d = 18.85f, h = 9.85f;
  CHECK(near(o, 1.0f - (std::atan(h / 5.0f) + std::atan((d - h) / 5.0f)) / 3.14159265f));
  Wall far{30.0f};
  soundObstruction({0, 0, 0}, {60, 0, 0}, 1.0f, wallRay, &far);
  CHECK(far.calls == 1);   // past 40 m: one ray
  CHECK(far.dynamicCalls == 0);  // static objects only
  CHECK(soundObstruction({0, 0, 0}, {1, 0, 0}, 1.0f, wallRay, &wall) == 0.0f);  // d ≤ 0
  CHECK(near(obstructionStep(0.0f, 1.0f, 1), 0.1f));
  CHECK(near(obstructionStep(0.0f, 1.0f, 3), 0.3f));
  CHECK(near(obstructionStep(0.5f, 0.45f, 1), 0.45f));
  CHECK(near(obstructionGain(1.0f), 0.15f));
  CHECK(obstructionGain(0.0f) == 1.0f);
}

// The rays a call cast, by index (0 along the line, 1 offset in z, 2 in y),
// and the cast number that comes back clear (0: none).
struct Casts {
  std::vector<int> order;
  int clearAt = 0;
};
std::optional<float> castRay(void* context, obf2::Vec3f start, obf2::Vec3f, bool) {
  auto& casts = *static_cast<Casts*>(context);
  casts.order.push_back(start.z != 0.0f ? 1 : start.y != 0.0f ? 2 : 0);
  if (static_cast<int>(casts.order.size()) == casts.clearAt) return std::nullopt;
  return 5.0f;
}

// BF2.exe 0x7df090 with the sound object's user data 1 and 2.
void testObstructionMemory() {
  // The cache: d = 18.85, n = min(round(e^(0.1885 + 1)), 15) = 3 calls answered
  // from it, the value to 1/65535.
  Wall wall{10.0f};
  SoundObstructionMemory memory;
  const float first = soundObstruction({0, 0, 0}, {20, 0, 0}, 1.0f, wallRay, &wall, &memory);
  CHECK(wall.calls == 3);
  CHECK((memory.cache >> 16) == 3);
  CHECK((memory.cache & 0xffff) == static_cast<std::uint32_t>(std::nearbyint(first * 65535.0f)));
  for (int i = 0; i < 3; ++i) {
    const float cached = soundObstruction({0, 0, 0}, {20, 0, 0}, 1.0f, wallRay, &wall, &memory);
    CHECK(std::fabs(cached - first) < 2e-5f);
  }
  CHECK(wall.calls == 3);  // no rays while it lasts
  soundObstruction({0, 0, 0}, {20, 0, 0}, 1.0f, wallRay, &wall, &memory);
  CHECK(wall.calls == 6);  // then the rays again
  // Far: n = min(round(e^(0.01 · 1000 + 1)), 15) = 15.
  SoundObstructionMemory far;
  Wall farWall{500.0f};
  soundObstruction({0, 0, 0}, {2000, 0, 0}, 1.0f, wallRay, &farWall, &far);
  CHECK((far.cache >> 16) == 15);

  // The first clear ray is remembered, and the next call starts from it.
  Casts casts{{}, 2};
  SoundObstructionMemory order;
  CHECK(soundObstruction({0, 0, 0}, {20, 0, 0}, 1.0f, castRay, &casts, &order) == 0.0f);
  CHECK((casts.order == std::vector<int>{0, 1}));
  CHECK(order.ray == 1);
  order.cache = 0;  // past the cached value
  casts = {{}, 0};
  soundObstruction({0, 0, 0}, {20, 0, 0}, 1.0f, castRay, &casts, &order);
  CHECK((casts.order == std::vector<int>{1, 2, 0}));
  // One ray (d > 40) with a stored index of 2: that ray is cast three times.
  order = {2, 0};
  casts = {{}, 0};
  soundObstruction({0, 0, 0}, {60, 0, 0}, 1.0f, castRay, &casts, &order);
  CHECK((casts.order == std::vector<int>{0, 0, 0}));
}

// One pool for every sound (BF2Audio.dll 0x10007140, 0x10006ba0).
void testVoices() {
  SoundVoices voices(3);
  const auto a = voices.claim(0.5f);
  const auto b = voices.claim(0.2f);
  const auto c = voices.claim(0.2f);
  CHECK(a && b && c && voices.playing() == 3);
  CHECK(voices.claim(0.1f) == 0);  // lower than every playing one: pending
  const auto d = voices.claim(0.2f);  // equal and later: takes the first of the lowest
  CHECK(d != 0 && !voices.held(b) && voices.held(c) && voices.held(a));
  voices.release(a);
  CHECK(voices.playing() == 2 && voices.claim(0.0f) != 0);
  CHECK(SoundVoices().sources() == 20);
  // A SoundVoice gives its source back when it goes, and a move keeps it once.
  SoundVoices pool(2);
  {
    SoundVoice one(&pool, pool.claim(1.0f));
    SoundVoice moved = std::move(one);
    CHECK(pool.playing() == 1 && moved.held() && one.id() == 0);
  }
  CHECK(pool.playing() == 0);
}

// BF2OpenAL.dll 0x100464d0.
void testPanAndDoppler() {
  const obf2::Vec3f right{1, 0, 0};
  CHECK(soundPan({0, 0, 0}, right) == 0.5f);
  CHECK(near(soundPan({5, 0, 0}, right), 1.0f));
  CHECK(near(soundPan({-5, 0, 0}, right), 0.0f));
  CHECK(near(soundPan({0, 0, 5}, right), 0.5f));
  // Listener running at 10 m/s straight at a still source 100 m ahead.
  CHECK(near(dopplerPitch(1.0f, {100, 0, 0}, {10, 0, 0}, {}), (343.3f + 10.0f) / 343.3f));
  CHECK(near(dopplerPitch(1.0f, {100, 0, 0}, {-10, 0, 0}, {}), (343.3f - 10.0f) / 343.3f));
  CHECK(dopplerPitch(1.2f, {100, 0, 0}, {10, 0, 0}, {}, 0.0f) == 1.2f);  // factor 0
  CHECK(near(dopplerPitch(1.0f, {100, 0, 0}, {0, 10, 0}, {}), 1.0f));   // across the line
  // At or past the speed of sound: limit − 1.
  CHECK(near(dopplerPitch(1.0f, {100, 0, 0}, {-400, 0, 0}, {}), (343.3f - 342.3f) / 343.3f, 1e-3f));
  CHECK(near(length(soundListenerVelocity({400, 0, 0})), 300.0f, 1e-2f));
  CHECK(soundListenerVelocity({3, 4, 0}).x == 3.0f);
}

}  // namespace

TEST_MAIN({
  testVoices();
  testPanAndDoppler();
  testObstruction();
  testObstructionMemory();
  testTinnitus();
  testSources();
  testScheduling();
  testDelay();
  testPriority();
  testFileIndex();
  testGrenadeHeader();
  testRows();
  testPlainEnvelope();
  testClampAndSort();
  testPitchClamp();
  testGrenadeDistance();
  testDefaults();
})
