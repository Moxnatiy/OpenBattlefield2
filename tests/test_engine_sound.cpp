#include <cmath>
#include <cstdint>

#include "check.h"
#include "obf2/app/engine_sound.h"

using namespace obf2::app;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

std::uint32_t zero(void*) { return 0; }

EngineSoundSettings newStyle() {
  EngineSoundSettings s;
  s.newStyleAudio = true;  // JEEP_FAAV.tweak line 462, `audio.newStyleAudio 1`
  return s;
}

// smooth_in_out 0x733f40, one 1/30 step towards 1 over 0.1 s:
// ω = 20, k = 2/3, e = 1/(1 + k + 0.48k² + 0.235k³).
void testSmooth() {
  float velocity = 0;
  const float k = 20.0f / 30.0f;
  const float e = 1.0f / (1.0f + k + 0.48f * k * k + 0.235f * k * k * k);
  const float out = smoothInOut(0, 1, velocity, kEngineSoundSmoothTime, kEngineSoundStep);
  CHECK(near(out, 1.0f - (1.0f + k) * e));
  CHECK(near(velocity, 20.0f * k * e));
  // Inside the 1e-4 band nothing moves.
  float still = 0;
  CHECK(near(smoothInOut(0.5f, 0.50005f, still, 0.1f, kEngineSoundStep), 0.50005f));
  CHECK_EQ(still, 0.0f);
}

// update 0x763143: old-style audio reads x = |rpm / 1.2| for all three.
void testOldStyle() {
  EngineSound sound;
  CHECK(!sound.update({.rpm = -0.6f}, 0, zero, nullptr));
  CHECK(near(sound.x(EngineSound::Idle), 0.5f));
  CHECK(near(sound.x(EngineSound::Rpm2), 0.5f));
  CHECK_EQ(sound.scale(EngineSound::Rpm1), 1.0f);
  sound.update({.rpm = 0.00005f}, 0, zero, nullptr);
  CHECK_EQ(sound.x(EngineSound::Rpm1), 0.0f);
}

// New style: the load fade moves 0.2 a step and splits Rpm1 / Rpm2.
void testFade() {
  EngineSound sound(newStyle());
  const EngineSound::Engine engine{.rpm = 0, .running = true, .load = 1};
  CHECK(sound.update(engine, 0, zero, nullptr));  // first gear: playGearShift
  CHECK(near(sound.fade(), 0.2f));
  CHECK(near(sound.scale(EngineSound::Rpm1), 0.8f));
  CHECK(near(sound.scale(EngineSound::Rpm2), 0.2f));
  for (int i = 0; i < 6; ++i) CHECK(!sound.update(engine, 0, zero, nullptr));
  CHECK(near(sound.scale(EngineSound::Rpm1), 0.0f));
  CHECK(near(sound.scale(EngineSound::Rpm2), 1.0f));
  // Not running: the fade target is 0.
  sound.update({.running = false, .load = 1}, 0, zero, nullptr);
  CHECK(near(sound.fade(), 0.8f));
  CHECK_EQ(sound.jitter(), 0.0f);
}

// updateAudioRpm 0x762d00 with r = 0 → (((0 >> 7) | 1) + 1)·2⁻²⁴.
void testJitter() {
  EngineSound sound(newStyle());
  const float r = 2.0f * 5.960464477539063e-08f;
  const float amp = 6.5f * (0.1f * 10.0f * kRpmWheel);
  sound.update({.running = true, .load = 1}, 0, zero, nullptr);
  CHECK(near(sound.jitter(), amp * (r - 0.3f), 1e-6f));
  CHECK_EQ(sound.countdown(), -1);
  CHECK_EQ(sound.x(EngineSound::Rpm1), 0.0f);  // a negative rpm clamps to 0
  // Wheels off the ground (material −1, not −2): +0.3.
  EngineSound air(newStyle());
  air.update({.running = true, .load = 1, .material = -1}, 0, zero, nullptr);
  CHECK(near(air.jitter(), amp * (r - 0.3f) + 0.3f, 1e-6f));
  // At rpm 0.5 the amplitude is 6.5 / 3.5 of the factors.
  EngineSound high(newStyle());
  high.update({.rpm = 0.5f, .running = true, .load = 1}, 0, zero, nullptr);
  CHECK(near(high.jitter(), 6.5f / 3.5f * 0.1f * 10.0f * kRpmWheel * (r - 0.3f), 1e-6f));
  CHECK(high.x(EngineSound::Rpm2) > 0.0f);
}

// updateSwayEffect 0x762b80: a new gear starts the sway time, then 1/30
// comes off each update; a gear change plays the Load sound.
void testSway() {
  EngineSound sound(newStyle());
  sound.update({.running = true}, 0, zero, nullptr);
  CHECK_EQ(sound.lastGear(), 1);
  CHECK(near(sound.swayTime(), 0.7f));
  for (int i = 0; i < 21; ++i) sound.update({.running = true}, 0, zero, nullptr);
  CHECK(sound.swayTime() < 0.0f);
  CHECK(sound.update({.running = true, .gear = 2}, 0, zero, nullptr));
  CHECK_EQ(sound.lastGear(), 2);
  // time-first = time-last = 0.7: every gear starts at 0.7.
  CHECK(near(sound.swayTime(), 0.7f));
  // Gear 0 counts as 1 (cmovg at 0x762bb9).
  EngineSound zeroGear(newStyle());
  zeroGear.update({.gear = 0}, 0, zero, nullptr);
  CHECK_EQ(zeroGear.lastGear(), 1);
  // resetAudioRpm clears the last gear: the next update shifts again.
  sound.resetAudioRpm();
  CHECK(sound.update({.running = true, .gear = 2}, 0, zero, nullptr));
}

// updateEngineSound 0x630290 and the engine types (0x630fa0).
void testRpmFeed() {
  // NewCar2 (no 0x20 bit): +0x2d8 clamped to [−1, 1].
  CHECK_EQ(engineSoundRpm(0xc2, 1.25f, 0), 1.0f);
  CHECK_EQ(engineSoundRpm(0xc2, -3.0f, 0), -1.0f);
  CHECK_EQ(engineSoundRpm(0xc2, 0.4f, 9), 0.4f);
  // Type bit 0x20: from 2·(+0x2ec).
  CHECK_EQ(engineSoundRpm(0x21, 9, -0.3f), 0.0f);
  CHECK_EQ(engineSoundRpm(0x21, 9, 0.3f), 1.0f);
  CHECK(near(engineSoundRpm(0x21, 9, 0.1f), 0.7f));
  // A network rpm of 1 at full throttle: x = 1 / 1.1 plus sway and jitter.
  EngineSound sound(newStyle());
  for (int i = 0; i < 60; ++i) sound.update({.rpm = 1.0f, .running = true}, 0, zero, nullptr);
  CHECK(near(sound.x(EngineSound::Rpm1), 1.0f / 1.1f, 1e-3f));
}

}  // namespace

TEST_MAIN({
  testRpmFeed();
  testSmooth();
  testOldStyle();
  testFade();
  testJitter();
  testSway();
})
