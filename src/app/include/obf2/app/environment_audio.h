#pragma once
// The level's reverb (docs/functions/environment-audio.md).
//
// Each level names its environment in its own `Sounds.con`:
// `sound.setReverb "common/sound/levelambients/City.eax"`
// (`Levels/Gulf_of_Oman/server.zip:Sounds.con`, line 3). The file is the EAX
// reverb's property set as text: the header `EAX ENVIRONMENT PROPERTIES`, then a
// name and a value per line (`Common_client.zip:Sound/LevelAmbients/City.eax`).
// The original hands it to its OpenAL's EAX (BF2OpenAL.dll); ours renders it
// through OpenAL Soft's EFX EAX-reverb effect on a loopback device and mixes the
// wet result into the sample's stream. OpenAL Soft is LGPL-2.1 and linked
// statically (cmake/openal-soft.cmake).
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "obf2/vfs/filesystem.h"

namespace obf2::app {

class EnvironmentAudio {
 public:
  EnvironmentAudio();
  ~EnvironmentAudio();
  // Reads the level's preset; false (and no reverb) when the level names none
  // or the file cannot be read.
  bool load(FileSystem& files, std::string_view level);
  bool ready() const;

  // What `render` returns: 16-bit stereo. The rate is ours (**not measured**:
  // the original's mixer rate is the device's).
  static constexpr SDL_AudioSpec outputFormat{SDL_AUDIO_S16, 2, 48000};
  // The sample through the reverb, its tail included; `send` is the sample's
  // template `reverbLevel` (0 dry only). Empty when there is no reverb.
  std::vector<Uint8> render(const SDL_AudioSpec& input, std::span<const Uint8> pcm, float send);

 private:
  struct Context;
  std::unique_ptr<Context> context_;
};

}  // namespace obf2::app
