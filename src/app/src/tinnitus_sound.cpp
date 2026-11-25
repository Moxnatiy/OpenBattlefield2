#include "obf2/app/tinnitus_sound.h"

#include <algorithm>

#include <SDL3/SDL.h>

#include "obf2/core/path.h"

namespace obf2::app {

void TinnitusSound::trigger(FileSystem& files) {
  if (!loaded_) {
    loaded_ = true;
    if (const auto bytes = files.read(normalizeAssetPath("Common/Sound/tinnitus.wav"))) {
      SDL_AudioSpec format{};
      Uint8* pcm = nullptr;
      Uint32 size = 0;
      if (SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes->data(), bytes->size()), true, &format, &pcm, &size)) {
        const SDL_AudioSpec mono{SDL_AUDIO_F32, 1, format.freq};
        Uint8* converted = nullptr;
        int length = 0;
        if (SDL_ConvertAudioSamples(&format, pcm, static_cast<int>(size), &mono, &converted, &length)) {
          const auto* input = reinterpret_cast<const float*>(converted);
          mono_.assign(input, input + length / sizeof(float));
          rate_ = format.freq;
        }
        SDL_free(converted);
        SDL_free(pcm);
      }
    }
  }
  // A trigger while it rings starts it over (0x10004830 stops the sample
  // first through engine slot +0x64).
  tinnitus_ = Tinnitus::trigger(kTinnitusAttack, kTinnitusHold, kTinnitusRelease, kTinnitusDepth);
  elapsed_ = 0.0f;
  active_ = true;
  requeue_ = -1.0f;
  stop();
  open();
}

void TinnitusSound::open() {
  cursor_ = 0;
  if (mono_.empty() || rate_ <= 0) return;
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
  // A 2-D sample: priority 2 (0x10005510), from the sources every sound shares.
  if (voices_) {
    voice_ = voices_->claim(2.0f);
    if (voice_ == 0) {
      requeue_ = 0.0f;  // pending: asked again each update while it rings
      return;
    }
  }
  const SDL_AudioSpec format{SDL_AUDIO_F32, 2, rate_};
  stream_.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, nullptr, nullptr));
  if (!stream_) {
    stop();
    return;
  }
  if (voices_) voices_->attach(voice_, stream_.get());
  feed();
  SDL_SetAudioStreamGain(stream_.get(), 0.0f);
  if (!SDL_ResumeAudioStreamDevice(stream_.get())) stop();
}

void TinnitusSound::stop() {
  stream_.reset();
  if (voices_ && voice_) voices_->release(voice_);
  voice_ = 0;
}

TinnitusSound::~TinnitusSound() { stop(); }

void TinnitusSound::feed() {
  // Looping (loopCount 0, 0x10004760): the file wraps; 0.25 s ahead (ours).
  if (!stream_) return;
  const std::size_t ahead = static_cast<std::size_t>(rate_ / 4);
  const int queuedBytes = SDL_GetAudioStreamQueued(stream_.get());
  const std::size_t queued = static_cast<std::size_t>(std::max(0, queuedBytes)) / (2 * sizeof(float));
  if (queued >= ahead) return;
  scratch_.clear();
  for (std::size_t i = queued; i < ahead; ++i) {
    const float value = mono_[cursor_];
    cursor_ = (cursor_ + 1) % mono_.size();
    scratch_.push_back(value);
    scratch_.push_back(value);
  }
  SDL_PutAudioStreamData(stream_.get(), scratch_.data(), static_cast<int>(scratch_.size() * sizeof(float)));
}

void TinnitusSound::advance(float seconds) {
  if (!active_) return;
  elapsed_ += seconds;
  const int ticks = static_cast<int>(elapsed_ / kSoundTick);
  if (tinnitus_.over(ticks)) {
    active_ = false;
    stop();
    return;
  }
  // Its source taken (SoundVoices): queued again 5 ticks on while more than
  // 15 ticks remain (0x10006ba0); a file without loop points restarts at 0.
  if (stream_ && voices_ && !voices_->held(voice_)) {
    stream_.reset();
    voice_ = 0;
    requeue_ = soundRequeues(ticks, tinnitus_.length) ? kRequeueTicks * kSoundTick : -1.0f;
  }
  if (!stream_ && requeue_ >= 0.0f) {
    requeue_ -= seconds;
    if (requeue_ <= 0.0f) {
      requeue_ = -1.0f;
      open();
    }
  }
  feed();
  if (stream_) SDL_SetAudioStreamGain(stream_.get(), volume_ * tinnitus_.volume(ticks));
}

float TinnitusSound::duck() const {
  return active_ ? tinnitus_.duck(static_cast<int>(elapsed_ / kSoundTick)) : 0.0f;
}

}  // namespace obf2::app
