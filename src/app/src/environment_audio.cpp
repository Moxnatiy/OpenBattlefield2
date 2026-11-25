#include "obf2/app/environment_audio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <unordered_map>

#define AL_ALEXT_PROTOTYPES
#include <AL/alext.h>
#include <AL/efx.h>
#include <SDL3/SDL_log.h>

#include "obf2/con/interpreter.h"

namespace obf2::app {
namespace {

class CurrentContext {
 public:
  explicit CurrentContext(ALCcontext* context)
      : previous_(alcGetCurrentContext()), bound_(alcMakeContextCurrent(context)) {}
  ~CurrentContext() { if (bound_) alcMakeContextCurrent(previous_); }
  explicit operator bool() const { return bound_; }
 private:
  ALCcontext* previous_;
  bool bound_;
};

using Parameters = std::unordered_map<std::string, std::array<float, 3>>;

// The preset's text (`Sound/LevelAmbients/City.eax`, Common_client.zip): the
// header, then each field's name and value on lines of their own; the two pans
// take three values. A line with a value that is not a number ends the read.
Parameters readParameters(std::string_view text) {
  std::istringstream input{std::string(text)};
  std::string header;
  std::getline(input, header);
  if (header != "EAX ENVIRONMENT PROPERTIES\r" && header != "EAX ENVIRONMENT PROPERTIES") return {};
  Parameters fields;
  for (std::string name; input >> name;) {
    std::array<float, 3> value{};
    if (!(input >> value[0])) return {};
    if ((name == "vReflectionsPan" || name == "vReverbPan") && !(input >> value[1] >> value[2])) {
      return {};
    }
    if (std::ranges::all_of(value, [](float number) { return std::isfinite(number); })) {
      fields.emplace(std::move(name), value);
    }
  }
  return fields;
}

// EAX levels are millibels: gain = 10^(mB / 2000).
float gainFromMillibels(float level) { return std::pow(10.0f, level / 2000.0f); }

// `sound.setReverb <file>` in the level's `Sounds.con`
// (`Levels/Gulf_of_Oman/server.zip:Sounds.con`, line 3). The client's handler of
// the command is not opened.
std::string presetPath(FileSystem& files, std::string_view level) {
  std::string path;
  con::Interpreter reader(files, [&](const con::Command& command) {
    if (command.lowerPath == "sound.setreverb") path = command.argStr(0);
  });
  reader.runFile("Levels/" + std::string(level) + "/Sounds.con");
  return path;
}

}  // namespace

struct EnvironmentAudio::Context {
  ALCdevice* device = nullptr;
  ALCcontext* context = nullptr;
  ALuint effect = 0;
  int tailFrames = 0;

  ~Context() {
    if (context) {
      if (effect) {
        const CurrentContext bind(context);
        if (bind) alDeleteEffects(1, &effect);
      }
      alcDestroyContext(context);
    }
    if (device) alcCloseDevice(device);
  }
};

EnvironmentAudio::EnvironmentAudio() = default;
EnvironmentAudio::~EnvironmentAudio() = default;
bool EnvironmentAudio::ready() const { return bool(context_); }

bool EnvironmentAudio::load(FileSystem& files, std::string_view level) {
  context_.reset();
  const auto path = presetPath(files, level);
  const auto file = path.empty() ? std::nullopt : files.loadText(path);
  if (!file) return false;
  const auto fields = readParameters(*file);
  if (!fields.contains("flDecayTime")) return false;

  auto candidate = std::make_unique<Context>();
  candidate->device = alcLoopbackOpenDeviceSOFT(nullptr);
  if (!candidate->device || !alcIsExtensionPresent(candidate->device, "ALC_EXT_EFX")) return false;
  const ALCint attributes[]{ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT,
                            ALC_SHORT_SOFT, ALC_FREQUENCY, outputFormat.freq, 0};
  candidate->context = alcCreateContext(candidate->device, attributes);
  if (!candidate->context) return false;
  const CurrentContext bind(candidate->context);
  if (!bind) return false;
  alGenEffects(1, &candidate->effect);
  const auto effect = candidate->effect;
  alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);

  // The size-to-density and millibel-to-gain conversions are the EAX mappings
  // used by OpenAL Soft's EaxReverbCommitter in al/effects/reverb.cpp.
  const auto get = [&](const char* key) -> const std::array<float, 3>* {
    const auto field = fields.find(key);
    return field == fields.end() ? nullptr : &field->second;
  };
  const auto set = [&](const char* key, ALenum property, float min, float max, bool millibels = false) {
    if (const auto* value = get(key)) {
      alEffectf(effect, property, std::clamp(millibels ? gainFromMillibels((*value)[0]) : (*value)[0], min, max));
    }
  };
  if (const auto* size = get("flEnvironmentSize")) {
    const float meters = (*size)[0];
    alEffectf(effect, AL_EAXREVERB_DENSITY, std::clamp(meters * meters * meters / 16, 0.0f, 1.0f));
  }
  set("flEnvironmentDiffusion", AL_EAXREVERB_DIFFUSION, 0, 1);
  set("lRoom", AL_EAXREVERB_GAIN, 0, 1, true);
  set("lRoomHF", AL_EAXREVERB_GAINHF, 0, 1, true);
  set("lRoomLF", AL_EAXREVERB_GAINLF, 0, 1, true);
  set("flDecayTime", AL_EAXREVERB_DECAY_TIME, AL_EAXREVERB_MIN_DECAY_TIME, AL_EAXREVERB_MAX_DECAY_TIME);
  set("flDecayHFRatio", AL_EAXREVERB_DECAY_HFRATIO, AL_EAXREVERB_MIN_DECAY_HFRATIO, AL_EAXREVERB_MAX_DECAY_HFRATIO);
  set("flDecayLFRatio", AL_EAXREVERB_DECAY_LFRATIO, AL_EAXREVERB_MIN_DECAY_LFRATIO, AL_EAXREVERB_MAX_DECAY_LFRATIO);
  set("lReflections", AL_EAXREVERB_REFLECTIONS_GAIN, 0, AL_EAXREVERB_MAX_REFLECTIONS_GAIN, true);
  set("flReflectionsDelay", AL_EAXREVERB_REFLECTIONS_DELAY, 0, AL_EAXREVERB_MAX_REFLECTIONS_DELAY);
  if (const auto* pan = get("vReflectionsPan")) alEffectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, pan->data());
  set("lReverb", AL_EAXREVERB_LATE_REVERB_GAIN, 0, AL_EAXREVERB_MAX_LATE_REVERB_GAIN, true);
  set("flReverbDelay", AL_EAXREVERB_LATE_REVERB_DELAY, 0, AL_EAXREVERB_MAX_LATE_REVERB_DELAY);
  if (const auto* pan = get("vReverbPan")) alEffectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, pan->data());
  set("flEchoTime", AL_EAXREVERB_ECHO_TIME, AL_EAXREVERB_MIN_ECHO_TIME, AL_EAXREVERB_MAX_ECHO_TIME);
  set("flEchoDepth", AL_EAXREVERB_ECHO_DEPTH, 0, 1);
  set("flModulationTime", AL_EAXREVERB_MODULATION_TIME, AL_EAXREVERB_MIN_MODULATION_TIME, AL_EAXREVERB_MAX_MODULATION_TIME);
  set("flModulationDepth", AL_EAXREVERB_MODULATION_DEPTH, 0, 1);
  set("flAirAbsorptionHF", AL_EAXREVERB_AIR_ABSORPTION_GAINHF, AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF,
      AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF, true);
  set("flHFReference", AL_EAXREVERB_HFREFERENCE, AL_EAXREVERB_MIN_HFREFERENCE, AL_EAXREVERB_MAX_HFREFERENCE);
  set("flLFReference", AL_EAXREVERB_LFREFERENCE, AL_EAXREVERB_MIN_LFREFERENCE, AL_EAXREVERB_MAX_LFREFERENCE);
  set("flRoomRolloffFactor", AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, 0, AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR);
  // `dwEnvironment` (the EAX preset's number) is not used: the fields above are
  // the whole environment.
  if (const auto* flags = get("dwFlags")) {
    // EAXREVERBFLAGS_DECAYHFLIMIT is bit 5 of EAX's flags (eax.h).
    const int value = static_cast<int>(std::clamp((*flags)[0], 0.0f, 255.0f));
    alEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, (value & 0x20) != 0);
  }
  if (alGetError() != AL_NO_ERROR) return false;
  float decay = 0;
  alGetEffectf(effect, AL_EAXREVERB_DECAY_TIME, &decay);
  candidate->tailFrames = static_cast<int>(decay * outputFormat.freq);
  context_ = std::move(candidate);
  SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "EAX reverb preset: %s", path.c_str());
  return true;
}

std::vector<Uint8> EnvironmentAudio::render(const SDL_AudioSpec& input, std::span<const Uint8> pcm, float send) {
  if (!context_ || pcm.empty() || send <= 0) return {};
  const SDL_AudioSpec mono{SDL_AUDIO_S16, 1, outputFormat.freq};
  Uint8* converted = nullptr;
  int bytes = 0;
  if (!SDL_ConvertAudioSamples(&input, pcm.data(), static_cast<int>(pcm.size()), &mono, &converted, &bytes)) {
    return {};
  }
  const std::unique_ptr<Uint8, decltype(&SDL_free)> samples(converted, SDL_free);
  const CurrentContext bind(context_->context);
  if (!bind) return {};

  ALuint voice = 0, buffer = 0, slot = 0, filter = 0;
  alGenSources(1, &voice);
  alGenBuffers(1, &buffer);
  alGenAuxiliaryEffectSlots(1, &slot);
  alGenFilters(1, &filter);
  alBufferData(buffer, AL_FORMAT_MONO16, converted, bytes, mono.freq);
  alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
  alFilterf(filter, AL_LOWPASS_GAIN, std::clamp(send, 0.0f, 1.0f));
  alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>(context_->effect));
  alSourcei(voice, AL_BUFFER, static_cast<ALint>(buffer));
  alSource3i(voice, AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(slot), 0, static_cast<ALint>(filter));
  alSourcePlay(voice);

  std::vector<Uint8> result;
  if (alGetError() == AL_NO_ERROR) {
    const int frames = bytes / static_cast<int>(sizeof(Sint16)) + context_->tailFrames;
    result.resize(static_cast<std::size_t>(frames) * 2 * sizeof(Sint16));
    alcRenderSamplesSOFT(context_->device, result.data(), frames);
    if (alcGetError(context_->device) != ALC_NO_ERROR) result.clear();
  }
  if (voice) alDeleteSources(1, &voice);
  if (buffer) alDeleteBuffers(1, &buffer);
  if (filter) alDeleteFilters(1, &filter);
  if (slot) alDeleteAuxiliaryEffectSlots(1, &slot);
  return result;
}

}  // namespace obf2::app
