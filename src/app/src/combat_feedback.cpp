#include "obf2/app/combat_feedback.h"

#include <cstring>
#include <utility>

#include <SDL3/SDL.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

#include "obf2/app/environment_audio.h"
#include "obf2/app/sound_envelope.h"
#include "obf2/app/sound_voices.h"
#include "obf2/core/path.h"

namespace obf2::app {
namespace {

// The largest sample we decode: ours, **not measured** (a guard, not a rule of
// the original's).
constexpr std::size_t kMaxSampleBytes = 1024 * 1024;
// Streams at once when no source pool is given: ours, **not measured**
// (the pool, `SoundVoices`, is the original's rule).
constexpr std::size_t kStreamsWithoutPool = 12;
// Played without a position, so 2-D: priority 2 (BF2Audio.dll 0x10005510,
// docs/functions/sound-template.md).
constexpr float kTwoDimensionalPriority = 2.0f;

}  // namespace

bool CombatFeedback::shot(FileSystem& files, const game::Registry& registry,
                          const game::ObjectTemplate* weapon, bool firstPerson) {
  return weaponSound(files, registry, weapon, firstPerson ? "fire1p" : "fire3p");
}

bool CombatFeedback::reload(FileSystem& files, const game::Registry& registry,
                            const game::ObjectTemplate* weapon, unsigned stage) {
  return weaponSound(files, registry, weapon, "reload1p", stage);
}

bool CombatFeedback::switchFireRate(FileSystem& files, const game::Registry& registry,
                                    const game::ObjectTemplate* weapon) {
  return weaponSound(files, registry, weapon, "switchfirerate");
}

bool CombatFeedback::weaponSound(FileSystem& files, const game::Registry& registry,
                                 const game::ObjectTemplate* weapon, std::string_view event,
                                 unsigned variant) {
  if (!weapon) return false;
  for (const auto& child : weapon->children) {
    if (normalizeAssetPath(child.name).find(event) == std::string::npos) continue;
    const auto* sound = registry.find(child.name);
    if (!sound || sound->className != "Sound") continue;
    // `soundFilename` lists the files, comma-separated; `variant` picks one.
    auto paths = sound->text("soundFilename");
    for (unsigned index = 0; index < variant && !paths.empty(); ++index) {
      const auto comma = paths.find(',');
      paths = comma == std::string_view::npos ? std::string_view{} : paths.substr(comma + 1);
    }
    // The template's own `volume` and `reverbLevel` (sound-template.md).
    if (play(files, paths.substr(0, paths.find(',')), sound->number("volume").value_or(1.0f),
             sound->number("reverbLevel").value_or(1.0f)))
      return true;
  }
  return false;
}

void CombatFeedback::setVolume(float gain) {
  if (gain == volume_) return;
  volume_ = gain;
  for (auto& sound : streams_) SDL_SetAudioStreamGain(sound.stream.get(), sound.gain * volume_);
}

CombatFeedback::~CombatFeedback() { closeAudio(); }

void CombatFeedback::prepareAudio() {
  // The output is opened while the level loads, not at the first sound: closing
  // and reopening a device stream between sounds stalls the render thread on
  // CoreAudio.
  if (playback_ != 0) return;
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
  playback_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
}

void CombatFeedback::releaseFinished() {
  // A finished sample gives its source back. One whose source was taken
  // (SoundVoices::claim) is handled as 0x10006ba0 does: with more than 15
  // ticks of it left (soundRequeues) it waits kRequeueTicks and asks again,
  // resuming where the play has got to by then; else it is dropped. While it
  // waits it asks again each frame until it is no longer due (soundStillDue).
  const Uint64 now = SDL_GetTicks();
  const auto lateTicks = [now](const Playing& sound) {
    return static_cast<int>(static_cast<double>(now - sound.startMs) * 30.0 / 1000.0);
  };
  std::erase_if(streams_, [&](Playing& sound) {
    if (sound.retryMs != 0) {
      const int late = lateTicks(sound);
      if (!soundStillDue(late, sound.lengthTicks, true, 1)) return true;
      if (now < sound.retryMs) return false;
      const std::uint32_t voice = voices_->claim(kTwoDimensionalPriority);
      if (voice == 0) return false;
      const std::size_t skip = static_cast<std::size_t>(
          static_cast<double>(now - sound.startMs) * sound.rate / 1000.0) *
          static_cast<std::size_t>(sound.frameBytes);
      if (skip >= sound.pcm->size() || !SDL_ClearAudioStream(sound.stream.get()) ||
          !SDL_BindAudioStream(playback_, sound.stream.get()) ||
          !SDL_PutAudioStreamData(sound.stream.get(), sound.pcm->data() + skip,
                                  static_cast<int>(sound.pcm->size() - skip)) ||
          !SDL_FlushAudioStream(sound.stream.get())) {
        voices_->release(voice);
        return true;
      }
      voices_->attach(voice, sound.stream.get(), true);
      sound.voice = voice;
      sound.retryMs = 0;
      return false;
    }
    const bool stolen = voices_ && sound.voice && !voices_->held(sound.voice);
    if (!stolen && SDL_GetAudioStreamQueued(sound.stream.get()) > 0) return false;
    if (voices_ && sound.voice) voices_->release(sound.voice);
    sound.voice = 0;
    if (stolen && soundRequeues(lateTicks(sound), sound.lengthTicks)) {
      sound.retryMs = now + static_cast<Uint64>(kRequeueTicks * 1000 / 30);
      return false;
    }
    return true;
  });
}

void CombatFeedback::closeAudio() {
  if (voices_)
    for (const Playing& sound : streams_) voices_->release(sound.voice);
  streams_.clear();
  if (playback_ != 0) SDL_CloseAudioDevice(std::exchange(playback_, 0));
}

bool CombatFeedback::play(FileSystem& files, std::string_view path, float gain, float reverbSend) {
  if (path.empty() || playback_ == 0) return false;
  auto [entry, inserted] = samples_.try_emplace(normalizeAssetPath(path));
  auto& sample = entry->second;
  if (inserted) {
    if (const auto data = files.read(entry->first)) {
      if (entry->first.ends_with(".ogg") && !data->empty() && data->size() <= kMaxSampleBytes) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(data->data());
        std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)> decoder{
            stb_vorbis_open_memory(bytes, static_cast<int>(data->size()), nullptr, nullptr),
            stb_vorbis_close};
        if (decoder) {
          const auto info = stb_vorbis_get_info(decoder.get());
          const auto frames = stb_vorbis_stream_length_in_samples(decoder.get());
          if (info.channels >= 1 && info.channels <= 2 && info.sample_rate > 0 &&
              frames <= kMaxSampleBytes / (static_cast<unsigned>(info.channels) * sizeof(Sint16))) {
            std::vector<Sint16> pcm(static_cast<std::size_t>(frames) * static_cast<std::size_t>(info.channels));
            const int decoded = stb_vorbis_get_samples_short_interleaved(
                decoder.get(), info.channels, pcm.data(), static_cast<int>(pcm.size()));
            if (decoded > 0) {
              sample.format = {SDL_AUDIO_S16, info.channels, static_cast<int>(info.sample_rate)};
              sample.pcm.resize(static_cast<std::size_t>(decoded) * static_cast<std::size_t>(info.channels) *
                                sizeof(Sint16));
              std::memcpy(sample.pcm.data(), pcm.data(), sample.pcm.size());
            }
          }
        }
      } else {
        Uint8* samples = nullptr;
        Uint32 size = 0;
        if (SDL_LoadWAV_IO(SDL_IOFromConstMem(data->data(), data->size()), true, &sample.format,
                           &samples, &size)) {
          std::unique_ptr<Uint8, decltype(&SDL_free)> buffer(samples, SDL_free);
          if (size <= kMaxSampleBytes) sample.pcm.assign(samples, samples + size);
        }
      }
    }
  }
  if (sample.pcm.empty()) return false;
  releaseFinished();
  if (!voices_ && streams_.size() >= kStreamsWithoutPool) return false;
  const std::uint32_t voice = voices_ ? voices_->claim(kTwoDimensionalPriority) : 0;
  if (voices_ && voice == 0) return false;
  const auto giveBack = [&] {
    if (voices_) voices_->release(voice);
    return false;
  };
  const SDL_AudioSpec* format = &sample.format;
  const std::vector<Uint8>* pcm = &sample.pcm;
  if (environment_ && environment_->ready() && reverbSend > 0.0f) {
    if (sample.wetSend != reverbSend) {
      sample.wet = environment_->render(sample.format, sample.pcm, reverbSend);
      sample.wetSend = reverbSend;
    }
    if (!sample.wet.empty()) {
      format = &EnvironmentAudio::outputFormat;
      pcm = &sample.wet;
    }
  }
  Stream stream(SDL_CreateAudioStream(format, nullptr), SDL_DestroyAudioStream);
  // Binding supplies the device's output format before SDL accepts PCM.
  if (!stream || !SDL_BindAudioStream(playback_, stream.get()) ||
      !SDL_SetAudioStreamGain(stream.get(), gain * volume_) ||
      !SDL_PutAudioStreamData(stream.get(), pcm->data(), static_cast<int>(pcm->size())) ||
      !SDL_FlushAudioStream(stream.get()))
    return giveBack();
  if (voices_) voices_->attach(voice, stream.get(), true);
  const int frameBytes = SDL_AUDIO_FRAMESIZE(*format);
  const int frames = frameBytes > 0 ? static_cast<int>(pcm->size()) / frameBytes : 0;
  streams_.push_back({std::move(stream), gain, voice, pcm, frameBytes, format->freq,
                      soundLengthTicks(static_cast<std::size_t>(frames), format->freq, 1, 1.0f),
                      SDL_GetTicks(), 0});
  ++played_;
  return true;
}

}  // namespace obf2::app
