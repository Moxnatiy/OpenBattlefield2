#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "obf2/app/sound_envelope.h"

namespace obf2::app {

// The sound engine's sources: one pool for every sample that plays
// (docs/functions/sound-template.md, "Source count"). BF2Audio.dll 0x10004c60
// sizes it (soundSources); 0x10007140 searches the engine's whole playing
// list for the sample of lowest priority when a request finds no source
// free, and 0x10006ba0 takes that sample's source. Every SmokeFlare, the
// tinnitus sample, CombatFeedback (weapons, footsteps, soldier sounds) and
// the vehicle engines claim from the same pool.
class SoundVoices {
 public:
  explicit SoundVoices(int sources = soundSources(0, 0)) : sources_(sources) {}

  // A source for a sample of `priority`; 0 when none is free and no playing
  // sample may be taken (the request stays pending). When it takes one, that
  // sample's stream (attach) is paused at once and held() is false for it
  // from then on; its owner releases the stream and decides on the re-queue.
  std::uint32_t claim(float priority);
  // `bound`: a stream bound to a device other sounds share (CombatFeedback);
  // a steal unbinds it instead of pausing that device.
  void attach(std::uint32_t voice, SDL_AudioStream* stream, bool bound = false);
  bool held(std::uint32_t voice) const;
  void release(std::uint32_t voice);
  int playing() const { return static_cast<int>(voices_.size()); }
  int sources() const { return sources_; }

 private:
  struct Voice {
    std::uint32_t id = 0;
    float priority = 0;
    SDL_AudioStream* stream = nullptr;
    bool bound = false;
  };
  std::vector<Voice> voices_;  // in claim order
  std::uint32_t next_ = 1;
  int sources_;
};

// One claimed source, given back when it goes (for owners that are dropped
// without a stop of their own, such as a vehicle's engine sound).
class SoundVoice {
 public:
  SoundVoice() = default;
  SoundVoice(SoundVoices* pool, std::uint32_t id) : pool_(pool), id_(id) {}
  SoundVoice(SoundVoice&& other) noexcept : pool_(other.pool_), id_(std::exchange(other.id_, 0)) {}
  SoundVoice& operator=(SoundVoice&& other) noexcept {
    if (this != &other) {
      reset();
      pool_ = other.pool_;
      id_ = std::exchange(other.id_, 0);
    }
    return *this;
  }
  SoundVoice(const SoundVoice&) = delete;
  SoundVoice& operator=(const SoundVoice&) = delete;
  ~SoundVoice() { reset(); }
  void reset() {
    if (pool_ && id_) pool_->release(id_);
    id_ = 0;
  }
  std::uint32_t id() const { return id_; }
  bool held() const { return pool_ && pool_->held(id_); }

 private:
  SoundVoices* pool_ = nullptr;
  std::uint32_t id_ = 0;
};

}  // namespace obf2::app
