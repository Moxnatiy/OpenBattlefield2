#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "obf2/core/math.h"

namespace obf2::app {

// A vehicle engine's sound state (docs/functions/engine-sound.md), the Linux
// server's `EngineSound` (constructor 0x7632c0, update 0x763040,
// updateAudioRpm 0x762d00, updateSwayEffect 0x762b80, resetAudioRpm
// 0x762780). It owns no audio: it gives each of the Idle, Rpm1 and Rpm2
// sounds the x its envelopes are read at and the scale of its volume
// evaluator, and says when the Load (gear shift) sound plays.

// `EngineSoundTemplate` (EngineTemplate +0x6f0), defaults from its reset
// 0x7626d0 (Vars::getFloat). No game data sets the vars.
struct EngineSoundSettings {
  bool newStyleAudio = false;               // +0x00, `audio.newStyleAudio`
  bool transformationRelativeRoot = false;  // +0x01; set: no EngineSound (initSound 0x630505)
  float swayFrequencyFirst = 80.0f;         // +0x04, 0xb4fa5c
  float swayFrequencyLast = 35.0f;          // +0x08, 0xb4718c
  float swayAmplitudeFirst = 0.05f;         // +0x0c, 0xb38948
  float swayAmplitudeLast = 0.01f;          // +0x10, 0xb33208
  float swayTimeFirst = 0.7f;               // +0x14, 0xb35e38
  float swayTimeLast = 0.7f;                // +0x18, 0xb35e38
  float materialTweak = 10.0f;              // +0x1c, 0xb2f3cc
};

// Static init 0x762900: `engine-sound-smooth-time` 0.1 (0xb317dc) and
// `rpm-wheel` 0.06 (0xba08e8).
inline constexpr float kEngineSoundSmoothTime = 0.1f;
inline constexpr float kRpmWheel = 0.06f;
// The step EngineSound smooths with, 1/30 (0xb2fcc0).
inline constexpr float kEngineSoundStep = 1.0f / 30.0f;
// `getWheelMaterials` when no wheel reports one (updateAudioRpm 0x762f63).
inline constexpr int kNoWheelMaterial = -2;

// smooth_in_out 0x733f40: critically damped approach of `current` to
// `target` over `smoothTime`, one step of `dt`; `velocity` carries over.
float smoothInOut(float current, float target, float& velocity, float smoothTime, float dt);

// EngineBase::updateEngineSound 0x630290: the rpm EngineSound reads
// (+0x2d4). Type bit 0x20: from 2·(+0x2ec): under −0.5 (0xb3320c) 0, over
// 0.5 (0xb2f234) 1, else + 0.5. Otherwise +0x2d8 clamped to [−1, 1].
float engineSoundRpm(unsigned engineType, float rpm2d8, float field2ec);

class EngineSound {
 public:
  enum Slot { Idle = 0, Rpm1 = 1, Rpm2 = 2 };
  // A source of 31-bit random values; updateAudioRpm turns each into
  // (((r >> 7) | 1) + 1) · 2^-24. The Linux generator is at 0x10a5128;
  // BF2.exe's is not established.
  using Random = std::uint32_t (*)(void* state);

  explicit EngineSound(const EngineSoundSettings& settings = {});

  // resetAudioRpm 0x762780.
  void resetAudioRpm();
  // setVelocity 0x762b60 (the root object's velocity, updateEngineSound
  // 0x6302bd).
  void setVelocity(Vec3f velocity) { velocity_ = velocity; }

  struct Engine {
    float rpm = 0;        // EngineBase +0x2d4, the normalised rpm
    bool running = false;  // EngineBase +0x260 != 0
    float load = 0;       // EngineBase +0x2f8
    int material = kNoWheelMaterial;  // getWheelMaterials
    int gear = 1;          // EngineBase +0x2fc
  };
  // update 0x763040, once per engine update. `systemTime` is g_system slot
  // +0x38 (updateSwayEffect). Returns true when the Load sound is to play
  // (playGearShift 0x7628b0).
  bool update(const Engine& engine, double systemTime, Random random, void* state);

  // x of Idle, Rpm1, Rpm2 (+0x3c, +0x40, +0x44).
  float x(Slot slot) const { return x_[slot]; }
  // Scale of each ScaleEvaluator (+0x60, +0x78, +0x90).
  float scale(Slot slot) const { return scale_[slot]; }

  // Not used yet outside tests; exposed for them.
  float fade() const { return fade_; }
  float smoothedRpm() const { return smoothed_; }
  int countdown() const { return countdown_; }
  float jitter() const { return jitter_; }
  float swayTime() const { return swayTime_; }
  int lastGear() const { return lastGear_; }

 private:
  float updateAudioRpm(const Engine& engine, double systemTime, Random random, void* state, bool& shift);
  float updateSwayEffect(const Engine& engine, double systemTime, bool& shift);

  EngineSoundSettings settings_;
  std::array<float, 3> x_{};                  // +0x3c
  std::array<float, 3> scale_{1, 1, 1};       // ScaleEvaluator +0x10
  [[maybe_unused]] float rpmCopy_ = 0;        // +0x38, old-style audio; not used yet
  Vec3f velocity_{};                          // +0xb0
  float fade_ = 0;                            // +0xd4
  int countdown_ = 0;                         // +0xd8
  float jitter_ = 0;                          // +0xdc
  float smoothed_ = 0;                        // +0xe0
  [[maybe_unused]] float unknownE4_ = 0;      // +0xe4, purpose not established
  float swayTime_ = 0;                        // +0xe8
  int lastGear_ = 0;                          // +0xec
  float smoothVelocity_ = 0;                  // +0xf0
};

}  // namespace obf2::app
