#include "obf2/app/menu_music.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

#include <cstring>

#include <SDL3/SDL_init.h>

namespace obf2::app {
namespace {

// Our own bounds on what is decoded whole, not the engine's: the game's
// music files are a few megabytes and a few minutes long. Not measured
// against the original, which streams through its own sound engine.
constexpr std::size_t kMaxFileBytes = 16 * 1024 * 1024;
constexpr unsigned kMaxSeconds = 300;
// The loop is fed again when less than this many bytes are queued. Our own
// choice, not measured: the original's music player is not opened.
constexpr int kRefillBelow = 4096;

bool startAudio() { return SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO); }

}  // namespace

void MenuMusic::open(FileSystem& files, std::string_view path, float gain, bool loop) {
  if (path.empty()) return;
  const auto data = files.read(path);
  if (!data || data->empty() || data->size() > kMaxFileBytes) return;
  // A RIFF file goes through SDL's loader. It plays once: nothing loops one yet.
  if (data->size() >= 4 && std::memcmp(data->data(), "RIFF", 4) == 0) {
    SDL_AudioSpec spec{};
    Uint8* pcm = nullptr;
    Uint32 size = 0;
    if (!startAudio()) return;
    if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(data->data(), data->size()), true, &spec, &pcm, &size))
      return;
    std::unique_ptr<Uint8, decltype(&SDL_free)> owned(pcm, SDL_free);
    stream_.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
    pcm_.clear();
    if (!stream_ || !SDL_SetAudioStreamGain(stream_.get(), gain) ||
        !SDL_PutAudioStreamData(stream_.get(), pcm, static_cast<int>(size)) ||
        !SDL_ResumeAudioStreamDevice(stream_.get())) {
      stop();
      return;
    }
    SDL_FlushAudioStream(stream_.get());
    return;
  }
  std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)> decoder{
      stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(data->data()),
                            static_cast<int>(data->size()), nullptr, nullptr), stb_vorbis_close};
  if (!decoder) return;
  const auto info = stb_vorbis_get_info(decoder.get());
  const auto frames = stb_vorbis_stream_length_in_samples(decoder.get());
  // Mono or stereo at a rate SDL takes; anything else is not the game's music.
  if (info.channels < 1 || info.channels > 2 || info.sample_rate < 8000 ||
      info.sample_rate > 96000 || !frames || frames > info.sample_rate * kMaxSeconds) return;
  std::vector<Sint16> samples(static_cast<std::size_t>(frames) * info.channels);
  const int decoded = stb_vorbis_get_samples_short_interleaved(
      decoder.get(), info.channels, samples.data(), static_cast<int>(samples.size()));
  if (decoded <= 0) return;
  samples.resize(static_cast<std::size_t>(decoded) * info.channels);
  if (!startAudio()) return;
  const SDL_AudioSpec spec{SDL_AUDIO_S16, info.channels, static_cast<int>(info.sample_rate)};
  stream_.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
  if (!stream_ ||
      !SDL_SetAudioStreamGain(stream_.get(), gain) ||
      !SDL_PutAudioStreamData(stream_.get(), samples.data(), static_cast<int>(samples.size() * sizeof(Sint16))) ||
      !SDL_ResumeAudioStreamDevice(stream_.get())) {
    stop();
    return;
  }
  if (loop) pcm_ = std::move(samples);
  else SDL_FlushAudioStream(stream_.get());
}

void MenuMusic::update(float gain) {
  if (!stream_) return;
  SDL_SetAudioStreamGain(stream_.get(), gain);
  if (!pcm_.empty() && SDL_GetAudioStreamQueued(stream_.get()) < kRefillBelow) {
    SDL_PutAudioStreamData(stream_.get(), pcm_.data(), static_cast<int>(pcm_.size() * sizeof(Sint16)));
  }
}

void MenuMusic::stop() {
  stream_.reset();
  pcm_.clear();
}

}  // namespace obf2::app
