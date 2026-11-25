#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "obf2/app/sound_envelope.h"
#include "obf2/app/sound_voices.h"
#include "obf2/app/sprite_particles.h"
#include "obf2/game/object_template.h"
#include "obf2/game/wind.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/server/collision_world.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

// One effect bundle (`Smokeflare`, `e_exp_grenade`, …) played wherever the
// world asks for it: its sprite particle systems through
// `particles::SpriteEmitter` (docs/functions/sprite-particles.md) and its
// start sound. An instance lives until every emitter has finished and its
// last particle has died.
class SmokeFlare {
 public:
  explicit SmokeFlare(std::string_view effect = "Smokeflare") : effect_(effect) {}
  SmokeFlare(const SmokeFlare&) = delete;
  SmokeFlare& operator=(const SmokeFlare&) = delete;
  SmokeFlare(SmokeFlare&&) = default;
  // Only onto a bundle that plays nothing (world_view.cpp's fresh entries).
  SmokeFlare& operator=(SmokeFlare&&) = default;
  ~SmokeFlare();  // gives back the sources its sounds hold
  void advance(float seconds, int round);
  void setVolume(float gain);
  // The level's effect lighting (Sky.con), for calcParticleLighting.
  void setLighting(const particles::EffectLighting& lighting) { lighting_ = lighting; }
  // The level's base hemi map, for ParticleHigh; null draws ParticleMedium.
  void setHemiMap(const HemiMap* hemi) { hemi_ = hemi; }
  // The world the bundle's particles read (docs/functions/wind.md,
  // docs/functions/sprite-particles.md "Where the inputs come from"): the
  // wind manager its WindAffector joins and which particles sample, and the
  // water height forceToWaterSurface puts drawn particles on.
  void setWorld(game::WindManager* wind, float waterHeight);
  // The listener for this frame; call before advance(). `velocity` is the
  // listener's (BF2.exe 0x7de9a0: the positional speed of the root of the
  // player's vehicle, or soldier, while the view is first person; else 0).
  void listen(Vec3f eye, const Mat4& view, Vec3f velocity = {});
  // The sources every sound shares (null: this bundle plays without a limit).
  void setVoices(SoundVoices* voices) { voices_ = voices; }
  // TinnitusSound::duck for this frame.
  void setDuck(float duck) { duck_ = duck; }
  // The world the obstruction rays go through (null: none).
  void setCollision(const server::CollisionWorld* collision) { collision_ = collision; }
  void observe(std::uint16_t id, Vec3f position, Vec3f eye, const Mat4& view,
               FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer,
               bool restart = false, std::optional<particles::Plane> surface = std::nullopt);
  void draw(std::vector<gfx::MeshRenderer::DrawItem>& items, const Mat4& view);
  // The bundle's point lights that are on this frame (docs/functions/effect-lights.md).
  void lights(std::vector<gfx::MeshRenderer::EffectLight>& out) const;
  void release(gfx::MeshRenderer& renderer);

 private:
  struct System {
    Vec3f offset;
    particles::SpriteTemplate tmpl;
    std::vector<gfx::GpuMesh> frames;  // one quad per atlas cell
  };
  // A `LightSource` child with `lightType Point` (Linux LightSource, 0x63ea90
  // init, 0x63e4d0 handleMessage, 0x63e5d0 handleFrameUpdate).
  struct Light {
    Vec3f offset;
    Vec3f color;           // `color`
    float range2 = 0.0f;   // `attenuationRange2`
    float flicker = 0.0f;  // `flicker`
    bool autoOff = false;  // +0x26c = !`enabled`: off again after two frame updates
  };
  struct Sample {
    std::vector<float> mono;
    int rate = 0;  // the file's own rate; pitch is applied when played
  };
  // The bundle's `Sound` part (docs/functions/sound-template.md).
  struct SoundPart {
    float pitch = 1;    // `pitch`
    float volume = 1;   // `volume`
    bool is3d = true;   // `is3dSound`: setIs3dSound 0x10015c40 → +0x1c, default 1 (0x10017560)
    int loopCount = 1;  // `loopCount`: setLoopCount 0x10015b50 → +0x08, default 1 (0x10017560)
    float globalEffectLevel = 1;  // `globalEffectLevel`: 0x10015e60 → +0x64, default 1 (0x10017560)
    float radius = 0;   // `soundRadius` (getRadius); ≤ 0 counts as 0.05 (BF2.exe 0x7df090)
    SoundEnvelope pitchEnvelope;
    SoundEnvelope volumeEnvelope;
    SoundDistance distance;
  };
  struct Instance {
    Vec3f position;
    bool present = true;
    float lastStep = 0;
    std::vector<particles::SpriteEmitter> emitters;
    // Per light: on, frame updates since it came on (+0x270), current range2.
    std::vector<bool> lightOn;
    std::vector<int> lightUpdates;
    std::vector<float> lightRange;
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> sound{nullptr, SDL_DestroyAudioStream};
    // The bundle's WindAffector component, registered with the manager while
    // the instance lives (BF2.exe init 0x67e600, destroy 0x67e680).
    std::unique_ptr<game::WindAffector> wind;
    // The scheduled play (docs/functions/sound-template.md).
    bool soundPending = false;
    float soundWait = 0;      // distance delay left, seconds
    int soundFile = -1;       // sound object +0x60
    unsigned pitchRow = 0;    // request +0x14
    unsigned volumeRow = 0;   // request +0x15
    float priority = 0;
    bool soundStarted = false;  // sample +0x18: has had a source
    float soundLate = 0;        // seconds since the due tick
    std::size_t frames = 0;   // the file's frames, for the play position x
    std::size_t cursor = 0;   // frames queued so far
    float obstruction = 0;    // sample +0x38 (0x10013130)
    float obstructionClock = 0;  // seconds toward the next sound update
    SoundObstructionMemory obstructionMemory;  // the sound object's user data 1 and 2 (0x7de130)
    std::uint32_t voice = 0;     // SoundVoices claim while it plays
  };

  void load(FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer);
  void play(std::uint16_t id, Instance& flare, Vec3f eye, const Mat4& view);
  void start(Instance& flare);
  void stop(Instance& flare);
  void feed(Instance& flare);
  void follow(Instance& flare);
  int lengthTicks(const Instance& flare) const;
  float obstructionTarget(Instance& flare) const;
  float level(unsigned row, float x) const;
  static int rand(void* state);

  std::vector<System> systems_;
  std::vector<Light> lights_;
  std::uint32_t flickerSeed_ = 1;
  std::vector<Sample> samples_;
  std::unordered_map<std::uint16_t, Instance> instances_;
  std::vector<particles::SpriteDraw> scratch_;
  int round_ = -1;
  bool loaded_ = false;
  std::string_view effect_;
  float volume_ = 1.0f;
  particles::EffectLighting lighting_;
  const HemiMap* hemi_ = nullptr;
  std::optional<game::WindAffectorTemplate> windTemplate_;
  game::WindManager* windManager_ = nullptr;
  particles::World world_;
  void dropWind(Instance& flare);
  SoundPart sound_;
  float duck_ = 0.0f;
  const server::CollisionWorld* collision_ = nullptr;
  Vec3f listener_{};
  Vec3f listenerRight_{1, 0, 0};
  Vec3f listenerVelocity_{};
  SoundVoices* voices_ = nullptr;
  std::vector<float> scratchAudio_;
  // The C library's rand() for the envelope rows and the per-play bytes
  // (BF2Audio.dll 0x10019260, 0x10006880), seeded from a clock in load().
  std::uint32_t randSeed_ = 1;
};

}  // namespace obf2::app
