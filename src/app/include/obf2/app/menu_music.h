#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "obf2/vfs/filesystem.h"

namespace obf2::app {

// One music or voice-over stream outside the game's sound engine: the mod's and
// the levels' `<music>` (engine::readModMusic, engine::LevelEntry::music), or a
// RIFF `.wav` the HUD names. An `.ogg` is decoded whole by stb_vorbis and played
// through an SDL3 audio stream of its own; a `.wav` goes through SDL's loader.
// docs/functions/menu-music.md.
class MenuMusic {
 public:
  // Starts `path` at `gain` (0..1). An `.ogg` with `loop` is fed again whenever
  // the stream runs low; a `.wav` plays once. A file that does not decode, or no
  // audio device, leaves it silent.
  void open(FileSystem& files, std::string_view path, float gain, bool loop);
  // Once a frame: the gain, and the loop's refill.
  void update(float gain);
  void stop();
  bool active() const { return static_cast<bool>(stream_); }

 private:
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream_{nullptr, SDL_DestroyAudioStream};
  std::vector<Sint16> pcm_;  // the whole decoded loop, kept for the refill
};

}  // namespace obf2::app
