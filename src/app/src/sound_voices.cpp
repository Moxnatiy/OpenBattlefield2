#include "obf2/app/sound_voices.h"

#include <algorithm>

namespace obf2::app {

std::uint32_t SoundVoices::claim(float priority) {
  if (static_cast<int>(voices_.size()) >= sources_) {
    // The lowest priority playing; of equals, the one that claimed first.
    auto lowest = voices_.end();
    for (auto it = voices_.begin(); it != voices_.end(); ++it)
      if (lowest == voices_.end() || it->priority < lowest->priority) lowest = it;
    if (lowest == voices_.end() || !soundSteals(priority, lowest->priority)) return 0;
    if (lowest->stream && lowest->bound) SDL_UnbindAudioStream(lowest->stream);
    else if (lowest->stream) SDL_PauseAudioStreamDevice(lowest->stream);
    voices_.erase(lowest);
  }
  const std::uint32_t id = next_++;
  if (next_ == 0) next_ = 1;
  voices_.push_back({id, priority, nullptr, false});
  return id;
}

void SoundVoices::attach(std::uint32_t voice, SDL_AudioStream* stream, bool bound) {
  for (Voice& v : voices_)
    if (v.id == voice) {
      v.stream = stream;
      v.bound = bound;
    }
}

bool SoundVoices::held(std::uint32_t voice) const {
  return voice != 0 && std::ranges::any_of(voices_, [voice](const Voice& v) { return v.id == voice; });
}

void SoundVoices::release(std::uint32_t voice) {
  std::erase_if(voices_, [voice](const Voice& v) { return v.id == voice; });
}

}  // namespace obf2::app
