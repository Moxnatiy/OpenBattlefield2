#include "obf2/app/engine_sound.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <utility>

namespace obf2::app {

namespace {

// 1e-4 (0xb355d0), the dead band of smooth_in_out and of update's x.
constexpr float kDeadBand = 9.999999747378752e-05f;

float unitRandom(EngineSound::Random random, void* state) {
  // updateAudioRpm 0x762e6c: ((r >> 7) | 1) + 1, times 2^-24 (0xb37900).
  const auto r = static_cast<std::int32_t>(random(state) & 0x7fffffffu);
  return static_cast<float>(((r >> 7) | 1) + 1) * 5.960464477539063e-08f;
}

float aboveDeadBand(float x) { return std::fabs(x) >= kDeadBand ? x : 0.0f; }

}  // namespace

float engineSoundRpm(unsigned engineType, float rpm2d8, float field2ec) {
  if (engineType & 0x20u) {
    const float twice = field2ec + field2ec;
    if (twice < -0.5f) return 0.0f;
    return twice > 0.5f ? 1.0f : twice + 0.5f;
  }
  // 0x630305: below −1 (0xb2f3c0) → −1, else min(1, v).
  return rpm2d8 < -1.0f ? -1.0f : std::min(1.0f, rpm2d8);
}

float smoothInOut(float current, float target, float& velocity, float smoothTime, float dt) {
  // 0x733f40; 2 (0xb43564), 0.48 (0xb97b50), 0.235 (0xb97b54).
  const float omega = 2.0f / smoothTime;
  const float k = omega * dt;
  const float decay = 1.0f / (1.0f + k + 0.48f * k * k + 0.235f * k * k * k);
  float change = current - target;
  if (std::fabs(change) < kDeadBand) change = 0.0f;
  const float temp = (velocity + omega * change) * dt;
  velocity = (velocity - omega * temp) * decay;
  if (std::fabs(velocity) < kDeadBand) velocity = 0.0f;
  return target + (change + temp) * decay;
}

EngineSound::EngineSound(const EngineSoundSettings& settings) : settings_(settings) { resetAudioRpm(); }

void EngineSound::resetAudioRpm() {
  fade_ = 0;
  countdown_ = 0;
  jitter_ = 0;
  smoothed_ = 0;
  unknownE4_ = 0;
  swayTime_ = 0;
  lastGear_ = 0;
  smoothVelocity_ = 0;
}

bool EngineSound::update(const Engine& engine, double systemTime, Random random, void* state) {
  bool shift = false;
  if (!settings_.newStyleAudio) {
    // 0x763143: |rpm / 1.2| (0xb47184).
    rpmCopy_ = engine.rpm;
    const float x = aboveDeadBand(std::fabs(engine.rpm / 1.2f));
    x_ = {x, x, x};
    return shift;
  }
  // Load fade, at most 0.2 a step either way (0xba08f4, 0xb34b58).
  const float target = (engine.running ? 1.0f : 0.0f) * engine.load;
  fade_ += std::clamp(target - fade_, -0.2f, 0.2f);
  scale_[Rpm1] = std::clamp(1.0f - fade_, 0.0f, 1.0f);
  scale_[Rpm2] = std::clamp(fade_, 0.0f, 1.0f);
  // x = rpm / 1.1 (0xb34a7c) in [0, 1].
  float x = updateAudioRpm(engine, systemTime, random, state, shift) / 1.1f;
  x = aboveDeadBand(std::clamp(x, 0.0f, 1.0f));
  x_ = {x, x, x};
  return shift;
}

float EngineSound::updateAudioRpm(const Engine& engine, double systemTime, Random random, void* state,
                                  bool& shift) {
  const float base = std::fabs(engine.rpm);
  if (!engine.running || engine.load == 0.0f) {
    jitter_ = 0.0f;
  } else if (countdown_ <= 0) {
    // 6.5 (0xba08ec), 5 (0xb34b64), 0.1 (0xb317dc).
    const float amp = 6.5f / (base * 5.0f + 1.0f) * (0.1f * settings_.materialTweak * kRpmWheel);
    // |v| / 30 (0xb2efc4) · 23 (0xba08f0) + 2 (0xb43564).
    const float speed = length(velocity_) / 30.0f * 23.0f + 2.0f;
    // -0.3 (0xb4fa54).
    jitter_ = amp * -0.3f + unitRandom(random, state) * amp;
    countdown_ = static_cast<int>(speed * unitRandom(random, state) + 0.0f);
    // Wheels off the ground: +0.3 (0xb2f3d4).
    if (engine.material < 0 && engine.material != kNoWheelMaterial) jitter_ += 0.3f;
  }
  countdown_ -= 1;
  const float sway = updateSwayEffect(engine, systemTime, shift);
  const float target = base + jitter_ + sway;
  // 0.001 (0xb2f3c4).
  if (std::fabs(target - smoothed_) < 0.001f) {
    smoothed_ = target;
    smoothVelocity_ = 0.0f;
  } else {
    smoothed_ = smoothInOut(smoothed_, target, smoothVelocity_, kEngineSoundSmoothTime, kEngineSoundStep);
  }
  return smoothed_;
}

float EngineSound::updateSwayEffect(const Engine& engine, double systemTime, bool& shift) {
  const int gear = std::max(engine.gear, 1);
  const int previous = lastGear_;
  if (gear != previous) {
    lastGear_ = gear;
    // (gear − 1) / 5 (0xb34b64).
    swayTime_ = settings_.swayTimeFirst +
                static_cast<float>(gear - 1) / 5.0f * (settings_.swayTimeLast - settings_.swayTimeFirst);
    shift = true;
  } else {
    swayTime_ -= kEngineSoundStep;
  }
  if (swayTime_ < 0.0f) return 0.0f;
  const float g = static_cast<float>(gear);
  float amp = (settings_.swayAmplitudeFirst + (settings_.swayAmplitudeLast - settings_.swayAmplitudeFirst) * g) *
              swayTime_;
  // Gear down: half (0xb2f234); else times running.
  amp *= gear < previous ? 0.5f : (engine.running ? 1.0f : 0.0f);
  // fmod(t, 2π) (0xb355b8) · 0.85 (0xb3adc8) · frequency.
  const float phase = std::fmod(static_cast<float>(systemTime), 6.2831854820251465f) * 0.85f;
  const float frequency =
      settings_.swayFrequencyFirst + (settings_.swayFrequencyLast - settings_.swayFrequencyFirst) * g;
  return std::sin(phase * frequency) * amp;
}

}  // namespace obf2::app
