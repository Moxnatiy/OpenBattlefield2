#include "obf2/app/smoke_flare.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include <SDL3/SDL.h>

#include "obf2/game/scene.h"
#include "obf2/core/path.h"

namespace obf2::app {
namespace {

Vec3f cameraRight(const Mat4& view) { return {view.m[0], view.m[4], view.m[8]}; }

}  // namespace

void SmokeFlare::load(FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer) {
  loaded_ = true;
  if (const auto* root = registry.find(effect_)) windTemplate_ = game::WindAffectorTemplate::fromDefinition(*root);
  // BF2.exe seeds the C library's rand() from a clock (srand at 0x669407,
  // 0x66f0cf, 0x5b5d7d, 0x66ab57 with 0x67cc40's result; 0x5c00c3 with an
  // import XOR 0x33333333; 0x4cea9e with a rounded float); a clock here too.
  randSeed_ = static_cast<std::uint32_t>(SDL_GetPerformanceCounter());
  const auto tree = game::flattenObject(registry, effect_);
  if (!tree) return;

  for (const auto& part : tree->parts) {
    const auto* definition = registry.find(part.templateName);
    if (!definition) continue;
    if (part.className == "SpriteParticleSystem") {
      std::string path = normalizeAssetPath(definition->text("textureName")) + ".dds";
      const auto bytes = files.read(path);
      if (!bytes) continue;
      const auto texture = texture::loadDds(*bytes);
      if (!texture) continue;
      auto* gpuTexture = renderer.uploadSharedTexture(*texture);
      if (!gpuTexture) continue;
      System system;
      system.offset = transformPoint(part.transform, {});
      system.tmpl = particles::SpriteTemplate::fromDefinition(*definition);
      const auto& t = system.tmpl;
      const bool animated = t.animationEnable && t.frameCount > 0;
      const int count = animated ? t.frameCount : 1;
      const float width = animated ? t.frameWidth : 1.0f;
      const float height = animated ? t.frameHeight : 1.0f;
      for (int index = 0; index < count; ++index) {
        auto uploaded = renderer.upload(particles::spriteQuad(static_cast<float>(index % t.frameCountX) * width,
                                               static_cast<float>(index / t.frameCountX) * height,
                                               width, height, t.pivotX, t.pivotY), {});
        if (!uploaded) break;
        auto& range = uploaded->ranges.front();
        range.texture = gpuTexture;
        range.alphaBlend = true;
        range.additiveBlend = t.additive;
        system.frames.push_back(std::move(*uploaded));
      }
      if (!system.frames.empty()) systems_.push_back(std::move(system));
    } else if (part.className == "LightSource" && definition->text("lightType") == "Point") {
      Light light;
      light.offset = transformPoint(part.transform, {});
      const auto color = definition->text("color");
      float r = 1, g = 1, b = 1;
      if (std::sscanf(std::string(color).c_str(), "%f/%f/%f", &r, &g, &b) != 3) r = g = b = 1;
      light.color = {r, g, b};
      light.range2 = definition->number("attenuationRange2").value_or(0.0f);
      light.flicker = definition->number("flicker").value_or(0.0f);
      light.autoOff = definition->number("enabled").value_or(1.0f) == 0.0f;
      if (light.range2 > 0.0f) lights_.push_back(light);
    } else if (part.className == "Sound" && !definition->text("soundFilename").empty()) {
      // A property the .tweak leaves out keeps BF2Audio's template
      // constructor default (0x10017560): volume 1 (+0x28), minDistance 1
      // (+0x0c), halfVolumeDistance 0 (+0x10), pitch 1 (+0x2c; setPitch
      // 0x10015b80 raises anything under 0.01 to 0.01), is3dSound 1, loopCount
      // 1. Whether BF2.exe's SoundObjectTemplate pushes defaults of its own is
      // not established.
      sound_.pitch = std::max(0.01f, definition->number("pitch").value_or(1));
      sound_.volume = definition->number("volume").value_or(1);
      sound_.is3d = definition->number("is3dSound").value_or(1) != 0;
      sound_.loopCount = static_cast<int>(definition->number("loopCount").value_or(1));
      sound_.globalEffectLevel = std::clamp(definition->number("globalEffectLevel").value_or(1), 0.0f, 1.0f);
      sound_.radius = definition->number("soundRadius").value_or(0);
      SoundEnvelope::parse(definition->text("pitchEnvelope"), sound_.pitchEnvelope, &SmokeFlare::rand, &randSeed_);
      SoundEnvelope::parse(definition->text("volumeEnvelope"), sound_.volumeEnvelope, &SmokeFlare::rand, &randSeed_);
      sound_.distance = SoundDistance::compute(sound_.volume, definition->number("minDistance").value_or(1),
                                               definition->number("halfVolumeDistance").value_or(0),
                                               kMaxDistanceCutoff);
      std::istringstream names(std::string(definition->text("soundFilename")));
      for (std::string name; std::getline(names, name, ',');) {
        const auto bytes = files.read(normalizeAssetPath(name));
        if (!bytes) continue;
        SDL_AudioSpec format{};
        Uint8* pcm = nullptr;
        Uint32 size = 0;
        if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes->data(), bytes->size()), true, &format, &pcm, &size)) continue;
        const SDL_AudioSpec mono{SDL_AUDIO_F32, 1, format.freq};
        Uint8* converted = nullptr;
        int length = 0;
        if (SDL_ConvertAudioSamples(&format, pcm, static_cast<int>(size), &mono, &converted, &length)) {
          const auto* input = reinterpret_cast<const float*>(converted);
          samples_.push_back({{input, input + length / sizeof(float)}, format.freq});
        }
        SDL_free(converted);
        SDL_free(pcm);
      }
    }
  }
  std::printf("  %.*s assets: %zu sprite systems, %zu sound samples\n",
              static_cast<int>(effect_.size()), effect_.data(), systems_.size(), samples_.size());
}

void SmokeFlare::advance(float seconds, int round) {
  if (round_ != round) {
    for (auto& [id, flare] : instances_) {
      dropWind(flare);
      stop(flare);
    }
    instances_.clear();
  }
  round_ = round;
  for (auto& [id, flare] : instances_) {
    flare.lastStep = seconds;
    // WindManager::update (0x7296b0) runs each affector's update; one whose
    // blow time ran out leaves the manager (0x67e630).
    if (flare.wind && !flare.wind->update(seconds)) dropWind(flare);
    for (std::size_t i = 0; i < flare.emitters.size(); ++i) {
      const auto& system = systems_[i];
      // A looping system runs while the bundle's object is still reported;
      // a one-shot system runs its own emitTime out.
      const bool emitting = flare.present || !system.tmpl.isLooping;
      flare.emitters[i].update(seconds, flare.position + system.offset, emitting);
    }
    // LightSource::handleFrameUpdate (Linux 0x63e5d0): a flickering light's
    // ranges are scaled by 1 + flicker * (rand/2^31 - 1); an auto-off light
    // counts its updates and goes off when the count passes 1. The ranges are
    // then times +0x284, which the constructor sets to 1 (Linux 0x63f312,
    // 0x63f3c1) and only vehicle engines change (setAttenuationIntensity
    // 0x63dca0, called from CarEngine/Engine::handleUpdate 0x62d47d, 0x62fa78),
    // so it is 1 here. The update runs once per main-loop pass with the
    // wall-clock step (BF2Engine::mainLoop 0x4db05c, step +0x48 from 0x4dabd5),
    // not per tick: one call per rendered frame.
    for (std::size_t i = 0; i < flare.lightOn.size(); ++i) {
      if (!flare.lightOn[i]) continue;
      const auto& light = lights_[i];
      float scale = 1.0f;
      if (light.flicker > 0.0f) {
        flickerSeed_ = flickerSeed_ * 1103515245u + 12345u;  // ours; Retail uses the C library's rand()
        scale = 1.0f + light.flicker * (static_cast<float>(flickerSeed_ >> 1) * 4.656613e-10f - 1.0f);
      }
      flare.lightRange[i] = light.range2 * scale;
      if (light.autoOff && ++flare.lightUpdates[i] > 1) flare.lightOn[i] = false;
    }
    flare.present = false;
    // Its source taken by another sound (SoundVoices::claim paused it then):
    // queued again 5 ticks on while more than 15 ticks of it remain
    // (0x10006ba0), else gone.
    if (flare.sound && voices_ && !voices_->held(flare.voice)) {
      flare.sound.reset();
      flare.voice = 0;
      if (soundRequeues(static_cast<int>(flare.soundLate / kSoundTick), lengthTicks(flare))) {
        flare.soundPending = true;
        flare.soundWait = kRequeueTicks * kSoundTick;
      }
    }
    // `soundLate`: ticks since the due tick; once started it keeps counting
    // through a re-queue (0x10005d80 compares against the first due tick).
    if (flare.soundStarted) flare.soundLate += seconds;
    if (flare.soundPending) {
      if (flare.soundWait > 0.0f) {
        flare.soundWait -= seconds;
      } else if (!flare.soundStarted) {
        flare.soundLate += seconds;
      }
      if (flare.soundWait <= 0.0f) {
        const int late = static_cast<int>(flare.soundLate / kSoundTick);
        if (soundStillDue(late, lengthTicks(flare), flare.soundStarted, sound_.loopCount)) {
          if (!flare.soundStarted) flare.soundLate = 0.0f;  // the due tick becomes now
          start(flare);
        } else {
          flare.soundPending = false;
        }
      }
    }
    if (flare.sound && sound_.is3d) {
      // The sound engine updates its sources once per tick (0x100078d0).
      flare.obstructionClock += seconds;
      const int updates = static_cast<int>(flare.obstructionClock / kSoundTick);
      if (updates > 0) {
        flare.obstructionClock -= static_cast<float>(updates) * kSoundTick;
        // One evaluator call per update: the cache counts calls (0x7df090).
        for (int i = 0; i < updates; ++i)
          flare.obstruction = obstructionStep(flare.obstruction, obstructionTarget(flare), 1);
      }
    }
    if (flare.sound) {
      feed(flare);
      if (flare.cursor >= flare.frames && SDL_GetAudioStreamAvailable(flare.sound.get()) <= 0) stop(flare);
    }
    follow(flare);
  }
  std::erase_if(instances_, [this](auto& entry) {
    auto& flare = entry.second;
    const bool done = !flare.present && !flare.sound && !flare.soundPending &&
                      std::ranges::all_of(flare.emitters, [](const auto& e) { return e.finished(); });
    if (done) dropWind(flare);
    return done;
  });
}

void SmokeFlare::observe(std::uint16_t id, Vec3f position, Vec3f eye, const Mat4& view,
                         FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer,
                         bool restart, std::optional<particles::Plane> surface) {
  if (!loaded_) load(files, registry, renderer);
  if (restart) {
    if (const auto old = instances_.find(id); old != instances_.end()) {
      dropWind(old->second);
      stop(old->second);
      instances_.erase(old);
    }
  }
  auto [entry, created] = instances_.try_emplace(id);
  auto& flare = entry->second;
  flare.present = true;
  if (!created) return;
  flare.position = position;
  for (std::size_t i = 0; i < systems_.size(); ++i)
    flare.emitters.emplace_back(systems_[i].tmpl, static_cast<std::uint32_t>(id) * 7919u + 1u + static_cast<std::uint32_t>(i));
  for (auto& emitter : flare.emitters) {
    emitter.setWorld(&world_);
    // The surface the bundle was spawned on (BF2.exe 0x5ca910 → 0x641ba0).
    if (surface) emitter.setCollisionPlane(*surface);
  }
  if (windTemplate_) {
    // The bundle carries no orientation in the port: identity.
    flare.wind = std::make_unique<game::WindAffector>(*windTemplate_, position);
    if (windManager_) windManager_->add(flare.wind.get());
  }
  // The bundle's start message switches each light on (LightSource::handleMessage,
  // Linux 0x63e4d0, message 0 for affectingType 1) and clears its counter.
  flare.lightOn.assign(lights_.size(), true);
  flare.lightUpdates.assign(lights_.size(), 0);
  flare.lightRange.assign(lights_.size(), 0.0f);
  for (std::size_t i = 0; i < lights_.size(); ++i) flare.lightRange[i] = lights_[i].range2;
  play(id, flare, eye, view);
  std::printf("  %.*s %u started at %.1f %.1f %.1f (%s audio, %zu systems)\n",
              static_cast<int>(effect_.size()), effect_.data(), id, position.x, position.y,
              position.z, flare.sound ? "playing" : "no", systems_.size());
}

void SmokeFlare::setVolume(float gain) {
  if (gain == volume_) return;
  volume_ = gain;
  for (auto& [id, effect] : instances_) follow(effect);
}

int SmokeFlare::rand(void* state) {
  // The C library's rand() as the Visual C++ runtime defines it.
  auto& seed = *static_cast<std::uint32_t*>(state);
  seed = seed * 214013u + 2531011u;
  return static_cast<int>((seed >> 16) & 0x7fff);
}

float SmokeFlare::level(unsigned row, float x) const {
  // 0x10016100: template volume times the volume envelope at
  // (x, row + floor(x)). The volume group and master levels are volume_.
  float value = std::clamp(sound_.volume, 0.0f, 1.0f);
  if (!sound_.volumeEnvelope.empty())
    value *= sound_.volumeEnvelope.evaluate(x, row + static_cast<unsigned>(static_cast<int>(std::floor(x))));
  return value;
}

void SmokeFlare::play(std::uint16_t, Instance& flare, Vec3f eye, const Mat4& view) {
  if (samples_.empty()) return;
  // The request (BF2Audio.dll 0x10006880): one rand() gives the pitch row
  // (low byte, +0x14) and the volume row (next byte, +0x15); a finite 3-D
  // play past the threshold waits out the distance delay.
  const auto relative = flare.position - eye;
  const float distance = length(relative);
  const int bytes = rand(&randSeed_);
  flare.pitchRow = static_cast<unsigned>(bytes) & 0xff;
  flare.volumeRow = (static_cast<unsigned>(bytes) >> 8) & 0xff;
  flare.soundWait = static_cast<float>(soundDelayTicks(distance, sound_.is3d, sound_.loopCount)) * kSoundTick;
  // A new bundle instance is a new sound object: no previous file (-1).
  // maxSamples per quality (template +0x90) is left at its constructor
  // default 0x7fffffff, so every file counts.
  flare.soundFile = soundFileIndex(static_cast<int>(samples_.size()), -1, &SmokeFlare::rand, &randSeed_);
  flare.priority = soundPriority(sound_.is3d, level(flare.volumeRow, 0.0f), sound_.distance, distance);
  flare.soundPending = true;
  flare.soundStarted = false;
  flare.soundLate = 0.0f;
  listener_ = eye;
  listenerRight_ = cameraRight(view);
  if (flare.soundWait <= 0.0f) start(flare);
}

void SmokeFlare::listen(Vec3f eye, const Mat4& view, Vec3f velocity) {
  // OpenAL takes the listener every update (0x100134d0 reads it back for
  // each source); the distance gain, the pan and the Doppler follow it here.
  listener_ = eye;
  listenerRight_ = cameraRight(view);
  listenerVelocity_ = soundListenerVelocity(velocity);
}

int SmokeFlare::lengthTicks(const Instance& flare) const {
  if (samples_.empty()) return 0;
  const auto& sample = samples_[static_cast<std::size_t>(std::max(0, flare.soundFile)) % samples_.size()];
  return soundLengthTicks(sample.mono.size(), sample.rate, sound_.loopCount, sound_.pitch);
}

void SmokeFlare::start(Instance& flare) {
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
  // One pool of sources for every sound (SoundVoices; soundSources: Low
  // quality, no forced count, 16 + 4). With every source taken the request
  // takes the playing sample of lowest priority if soundSteals says so
  // (0x10007140, 0x10006ba0); the loser finds out in its own advance().
  if (voices_) {
    flare.voice = voices_->claim(flare.priority);
    if (flare.voice == 0) return;  // stays pending
  }
  flare.soundPending = false;
  const auto& sample = samples_[static_cast<std::size_t>(std::max(0, flare.soundFile)) % samples_.size()];
  flare.frames = sample.mono.size();
  flare.cursor = 0;  // a file without loop points starts at frame 0 (0x10005d80)
  const SDL_AudioSpec format{SDL_AUDIO_F32, 2, sample.rate};
  flare.sound.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, nullptr, nullptr));
  if (!flare.sound) {
    stop(flare);
    return;
  }
  if (voices_) voices_->attach(flare.voice, flare.sound.get());
  flare.soundStarted = true;
  // Starting, the obstruction takes the evaluator's value at once (0x10013130).
  flare.obstruction = sound_.is3d ? obstructionTarget(flare) : 0.0f;
  flare.obstructionClock = 0.0f;
  feed(flare);
  follow(flare);
  if (!SDL_ResumeAudioStreamDevice(flare.sound.get())) stop(flare);
}

void SmokeFlare::stop(Instance& flare) {
  flare.sound.reset();
  if (voices_ && flare.voice) voices_->release(flare.voice);
  flare.voice = 0;
}

SmokeFlare::~SmokeFlare() {
  for (auto& [id, flare] : instances_)
    if (flare.sound) stop(flare);
}

float SmokeFlare::obstructionTarget(Instance& flare) const {
  if (!collision_) return 0.0f;
  // The engine's ray (SoundRay): the Projectile lod of the static objects,
  // and of the vehicles (our movables) on the near rays; one-sided faces; no
  // terrain. The world holds no soldiers, which the predicator skips.
  const auto ray = [](void* context, Vec3f start, Vec3f motion, bool dynamic) -> std::optional<float> {
    const auto* world = static_cast<const server::CollisionWorld*>(context);
    const float span = length(motion);
    if (span <= 0.0f) return std::nullopt;
    // b2 true: inside a mesh, the first face its tree's walk accepts
    // (docs/functions/collision-bsp.md).
    const auto contact = world->segmentContact(start, motion, start + motion * 0.5f, span * 0.5f,
                                               dynamic, /*firstFace=*/true);
    if (!contact) return std::nullopt;
    return length(contact->point - start);
  };
  return soundObstruction(listener_, flare.position, sound_.radius, ray,
                          const_cast<server::CollisionWorld*>(collision_), &flare.obstructionMemory) *
         sound_.globalEffectLevel;
}

void SmokeFlare::feed(Instance& flare) {
  // The file goes out in pieces, each panned and attenuated for the listener
  // at the time it is queued; 0.25 s stays queued ahead (ours, so a slow
  // frame does not starve the device).
  if (!flare.sound || flare.cursor >= flare.frames) return;
  const auto& sample = samples_[static_cast<std::size_t>(std::max(0, flare.soundFile)) % samples_.size()];
  const std::size_t ahead = static_cast<std::size_t>(sample.rate / 4);
  const int queuedBytes = SDL_GetAudioStreamQueued(flare.sound.get());
  const std::size_t queued = static_cast<std::size_t>(std::max(0, queuedBytes)) / (2 * sizeof(float));
  if (queued >= ahead) return;
  const std::size_t count = std::min(ahead - queued, flare.frames - flare.cursor);
  // BF2OpenAL.dll 0x100464d0, stereo: left √(1 − p), right √p (soundPan).
  // A 2-D source stays at (0, 0, 0) (BF2Audio.dll 0x10014260 sets it at the
  // start), where the listener is (its position is never set; positions go
  // into the listener's space, 0x100050f0): p = 0.5.
  float gain = 1.0f, pan = 0.5f;
  if (sound_.is3d) {
    const auto relative = flare.position - listener_;
    gain = sound_.distance.gain(length(relative));
    pan = soundPan(relative, listenerRight_);
  }
  const float left = gain * std::sqrt(1.0f - pan);
  const float right = gain * std::sqrt(pan);
  scratchAudio_.clear();
  scratchAudio_.reserve(count * 2);
  for (std::size_t i = 0; i < count; ++i) {
    const float value = sample.mono[flare.cursor + i];
    scratchAudio_.push_back(value * left);
    scratchAudio_.push_back(value * right);
  }
  flare.cursor += count;
  if (!SDL_PutAudioStreamData(flare.sound.get(), scratchAudio_.data(),
                              static_cast<int>(scratchAudio_.size() * sizeof(float)))) {
    stop(flare);
    return;
  }
  if (flare.cursor >= flare.frames) SDL_FlushAudioStream(flare.sound.get());
}

void SmokeFlare::follow(Instance& flare) {
  // 0x100134d0 every update: AL_PITCH from 0x100161a0 and the gain from
  // 0x10016100 at x, the play position 0..1 (0x10015660). One loop only:
  // the row advance at a wrap does not arise.
  if (!flare.sound || flare.frames == 0) return;
  const int queued = SDL_GetAudioStreamQueued(flare.sound.get());
  const float waiting = static_cast<float>(std::max(0, queued)) / (2.0f * sizeof(float));
  const float played = static_cast<float>(flare.cursor) - waiting;
  const float x = std::clamp(played / static_cast<float>(flare.frames), 0.0f, 0.999999f);
  float pitch = soundPitch(sound_.pitch, sound_.pitchEnvelope, x, flare.pitchRow);
  // Doppler (BF2OpenAL.dll 0x100464d0). The source's velocity: zero at the
  // start (BF2Audio.dll 0x10014260); what BF2.exe gives a bundle's sound
  // object later is not established, so it stays zero here.
  if (sound_.is3d) pitch = dopplerPitch(pitch, flare.position - listener_, listenerVelocity_, Vec3f{});
  SDL_SetAudioStreamFrequencyRatio(flare.sound.get(), pitch);
  // Not EAX: the tinnitus term multiplies (0x100134d0, clamped like its other factors).
  const float duck = std::clamp(tinnitusDuck(sound_.globalEffectLevel, duck_), 0.0f, 1.0f);
  const float obstructed = sound_.is3d ? obstructionGain(flare.obstruction) : 1.0f;
  SDL_SetAudioStreamGain(flare.sound.get(), volume_ * level(flare.volumeRow, x) * duck * obstructed);
}

void SmokeFlare::draw(std::vector<gfx::MeshRenderer::DrawItem>& items, const Mat4& view) {
  particles::View camera{view, particles::kWorldNearPlane, hemi_, lighting_};
  for (auto& [id, flare] : instances_) {
    for (std::size_t i = 0; i < flare.emitters.size(); ++i) {
      const auto& system = systems_[i];
      scratch_.clear();
      flare.emitters[i].collect(flare.lastStep, scratch_, &camera);
      particles::emitSprites(scratch_, system.frames, system.tmpl, view, items);
    }
    // Spin and frames advance once per simulated frame, not once per view.
    flare.lastStep = 0.0f;
  }
}

void SmokeFlare::lights(std::vector<gfx::MeshRenderer::EffectLight>& out) const {
  for (const auto& [id, flare] : instances_) {
    for (std::size_t i = 0; i < flare.lightOn.size(); ++i) {
      if (!flare.lightOn[i] || flare.lightRange[i] <= 0.0f) continue;
      // attSqrInv = 1 / range2² (RendDX9.dll 0x100dfff0 via light +0x190).
      out.push_back({flare.position + lights_[i].offset,
                     1.0f / (flare.lightRange[i] * flare.lightRange[i]), lights_[i].color});
    }
  }
}

void SmokeFlare::dropWind(Instance& flare) {
  if (flare.wind && windManager_) windManager_->remove(flare.wind.get());
  flare.wind.reset();
}

void SmokeFlare::setWorld(game::WindManager* wind, float waterHeight) {
  if (wind != windManager_) {
    for (auto& [id, flare] : instances_) {
      if (!flare.wind) continue;
      if (windManager_) windManager_->remove(flare.wind.get());
      if (wind) wind->add(flare.wind.get());
    }
    windManager_ = wind;
  }
  // RendDX9.dll reads the wind only while the manager's flag +0x8c10 is set,
  // which starts at 1 (0x100d609e) and only a console command changes.
  world_.wind = wind ? std::function<Vec3f(Vec3f)>([wind](Vec3f at) { return wind->wind(at); })
                     : std::function<Vec3f(Vec3f)>();
  world_.waterHeight = [waterHeight](Vec3f) { return waterHeight; };
}

void SmokeFlare::release(gfx::MeshRenderer& renderer) {
  for (auto& [id, flare] : instances_) dropWind(flare);
  lights_.clear();
  for (auto& [id, flare] : instances_) stop(flare);
  instances_.clear();
  for (auto& system : systems_) for (auto& mesh : system.frames) renderer.release(mesh);
  systems_.clear();
  samples_.clear();
  sound_ = {};
  loaded_ = false;
}

}  // namespace obf2::app
