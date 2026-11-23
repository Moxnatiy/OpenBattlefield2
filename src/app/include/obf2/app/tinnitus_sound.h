#pragma once

#include <memory>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "obf2/app/sound_envelope.h"
#include "obf2/app/sound_voices.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

// The tinnitus sample after a near explosion (docs/functions/sound-template.md).
// `sound.tinnitusSetup`'s first file (Settings/Sound.con line 35,
// `Common/Sound/tinnitus.wav`) as BF2Audio.dll 0x10004760 makes it: 2-D,
// looping, volume following Tinnitus::volume.
class TinnitusSound {
 public:
  TinnitusSound() = default;
  TinnitusSound(const TinnitusSound&) = delete;
  TinnitusSound& operator=(const TinnitusSound&) = delete;
  ~TinnitusSound();
  void trigger(FileSystem& files);
  void advance(float seconds);
  // The sources it shares with every other sound (null: no limit).
  void setVoices(SoundVoices* voices) { voices_ = voices; }
  void setVolume(float gain) { volume_ = gain; }
  // The duck term for every other sound (Tinnitus::duck); 0 when silent.
  float duck() const;
  bool active() const { return active_; }

 private:
  void feed();
  void open();
  void stop();

  Tinnitus tinnitus_;
  std::vector<float> mono_;
  int rate_ = 0;
  std::size_t cursor_ = 0;
  float elapsed_ = 0;  // seconds since the trigger
  bool active_ = false;
  bool loaded_ = false;
  float volume_ = 1.0f;
  SoundVoices* voices_ = nullptr;
  std::uint32_t voice_ = 0;
  float requeue_ = -1.0f;  // seconds to the re-queue after its source was taken; < 0: none
  std::vector<float> scratch_;
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream_{nullptr, SDL_DestroyAudioStream};
};

}  // namespace obf2::app
