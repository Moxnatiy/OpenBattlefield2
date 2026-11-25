#pragma once

// Effects placed by the level itself (`StaticObjects.con`), such as FuShe
// Pass's dam waterfall: a `Bundle` (`ambStat_Damn_waterFall`) whose tree holds
// an `EffectBundle` of particle systems. They have no geometry of their own, so
// `placeObjects` skips them; this plays them for as long as the level is loaded.
//
// Both sprite and mesh particle systems are simulated by
// `particles::SpriteEmitter` (mesh systems through its type-1 spawn and
// integrator). A sprite is drawn as `SmokeFlare` draws one; a mesh particle
// draws one instance slot of its mesh with the matrix RendDX9.dll 0x10175ac0
// builds, lit as MeshParticleMesh.fx lights it
// (docs/functions/ambient-effects.md). The bundle's looping 3D sound plays at
// the placement.

#include <cstddef>
#include <memory>

#include <SDL3/SDL_audio.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "obf2/app/sprite_particles.h"
#include "obf2/game/object_template.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/level/level.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

class AmbientEffects {
 public:
  void load(FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer,
            const gfx::MeshRenderer::TextureResolver& resolve,
            const std::vector<level::StaticObject>& placement, const level::Lighting& lighting);
  void advance(float seconds);
  // Distance gain of the looping sounds, from where the camera is.
  void listen(Vec3f eye);
  void setVolume(float gain) { volume_ = gain; }
  void draw(std::vector<gfx::MeshRenderer::DrawItem>& items, const Mat4& view);
  void release(gfx::MeshRenderer& renderer);

 private:
  // One particle system template's GPU side, shared by every placement.
  struct Look {
    bool mesh = false;
    std::vector<gfx::GpuMesh> frames;  // sprite: one quad per atlas cell; mesh: slot 0
    float directionalScale = 0;        // template +0x1ec
    bool alignRotationToSpeed = false; // template +0x1f0
    int technique = 0;                 // 0x10174740: 0 Diffuse, 1 Additive, 2 DiffuseWithZWrite
    Vec3f lighting{1, 1, 1};           // calcParticleLighting, Diffuse techniques only
  };
  struct Sound {
    std::vector<float> mono;
    int rate = 0;
    float volume = 1, minDistance = 10, halfDistance = 20;
    bool loop = false;
    Vec3f position;
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{nullptr, SDL_DestroyAudioStream};
  };
  struct Emitter {
    Emitter(const particles::SpriteTemplate& source, std::uint32_t seed, Vec3f at, std::size_t which)
        : tmpl(source), emitter(tmpl, seed), position(at), look(which) {}
    particles::SpriteTemplate tmpl;  // emitDirection turned into the world by the placement
    particles::SpriteEmitter emitter;
    Vec3f position;
    std::size_t look;
    float lastStep = 0;
  };

  int spriteLook(const std::string& name, const game::ObjectTemplate& definition, FileSystem& files,
                 gfx::MeshRenderer& renderer);
  int meshLook(const std::string& name, const game::ObjectTemplate& definition, FileSystem& files,
               const game::Registry& registry, gfx::MeshRenderer& renderer,
               const gfx::MeshRenderer::TextureResolver& resolve);
  void drawMesh(Emitter& emitter, const Look& look, std::vector<gfx::MeshRenderer::DrawItem>& items);
  void loadSound(const game::ObjectTemplate& definition, FileSystem& files, Vec3f position);

  std::vector<Look> looks_;
  std::unordered_map<std::string, int> lookByTemplate_;
  std::vector<std::unique_ptr<Emitter>> emitters_;
  std::vector<particles::SpriteDraw> scratch_;
  std::vector<Sound> sounds_;
  float volume_ = 1;
  level::Lighting lighting_;
};

}  // namespace obf2::app
