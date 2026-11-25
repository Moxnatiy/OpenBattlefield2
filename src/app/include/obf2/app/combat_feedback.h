#pragma once
// Our own soldier's weapon sounds: the weapon's `Sound` children
// (`ObjectTemplate.addTemplate` under the weapon in its `.tweak`), played without
// a position, through the level's reverb when there is one
// (environment_audio.h). docs/functions/environment-audio.md.
//
// A weapon's sounds are told apart by their template names — the fire sound's
// holds `fire1p` (first person) or `fire3p`, the reload's `reload1p`, the fire
// rate switch's `switchfirerate`. That is the data's naming, not a rule read in
// the binary.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "obf2/game/object_template.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

class EnvironmentAudio;
class SoundVoices;

class CombatFeedback {
 public:
  CombatFeedback() = default;
  ~CombatFeedback();
  CombatFeedback(const CombatFeedback&) = delete;
  CombatFeedback& operator=(const CombatFeedback&) = delete;

  // Opens the output; done while the level loads.
  void prepareAudio();
  // A round fired: the weapon's `fire1p` (first person) or `fire3p` sound.
  bool shot(FileSystem& files, const game::Registry& registry, const game::ObjectTemplate* weapon,
            bool firstPerson);
  // A reload: the weapon's `reload1p` sound, file `stage` of its list.
  bool reload(FileSystem& files, const game::Registry& registry, const game::ObjectTemplate* weapon,
              unsigned stage = 0);
  bool switchFireRate(FileSystem& files, const game::Registry& registry,
                      const game::ObjectTemplate* weapon);
  // One file at `gain` (the template's `volume`) with `reverbSend` (its
  // `reverbLevel`).
  bool play(FileSystem& files, std::string_view path, float gain, float reverbSend = 1.0f);
  // The effects volume (the profile's `AudioSettings`).
  void setVolume(float gain);
  void setEnvironment(EnvironmentAudio* environment) { environment_ = environment; }
  // The sound engine's one pool of sources (sound_voices.h); null: our own cap.
  void setVoices(SoundVoices* voices) { voices_ = voices; }
  // Gives finished samples' sources back; once a frame.
  void releaseFinished();
  int played() const { return played_; }

 private:
  void closeAudio();
  bool weaponSound(FileSystem& files, const game::Registry& registry,
                   const game::ObjectTemplate* weapon, std::string_view event, unsigned variant = 0);

  struct Sample {
    SDL_AudioSpec format{};
    std::vector<Uint8> pcm;
    std::vector<Uint8> wet;
    float wetSend = -1.0f;
  };
  using Stream = std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)>;
  std::unordered_map<std::string, Sample> samples_;
  struct Playing {
    Stream stream;
    float gain;
    std::uint32_t voice = 0;
    // For a re-queue after a steal: the PCM it plays, its frame size and
    // rate, its length in ticks, when it started, and when it asks again (0:
    // not waiting).
    const std::vector<Uint8>* pcm = nullptr;
    int frameBytes = 0;
    int rate = 0;
    int lengthTicks = 0;
    Uint64 startMs = 0;
    Uint64 retryMs = 0;
  };
  std::vector<Playing> streams_;
  SDL_AudioDeviceID playback_ = 0;
  float volume_ = 1.0f;
  EnvironmentAudio* environment_ = nullptr;
  SoundVoices* voices_ = nullptr;
  int played_ = 0;
};

}  // namespace obf2::app
