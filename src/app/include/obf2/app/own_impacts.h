#pragma once
// Where our own rounds land: the mark on the surface (bullet_decals.h) and the
// impact's effect, both from the material cell of the round's material and the
// surface's (`common/material/materialManagerSettings.con`,
// `MaterialManager.createCell` with `setDecalTemplate` / `setEffectTemplate`,
// slot 0). docs/functions/own-impacts.md.
//
// The original flies a projectile and its contact gives the point. Ours is a
// straight line from the eye along the view: the projectile's flight is **not
// ported**, so gravity, drag and its speed are not in it.
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/app/bullet_decals.h"
#include "obf2/app/smoke_flare.h"
#include "obf2/core/math.h"
#include "obf2/game/object_template.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::session {
struct RemoteWorld;
}

namespace obf2::app {

class SoundVoices;

class OwnImpacts {
 public:
  void init(FileSystem& files, gfx::MeshRenderer& renderer, SoundVoices* voices) {
    files_ = &files;
    renderer_ = &renderer;
    voices_ = voices;
  }
  // A round left `weapon`: find where the view's line meets the world.
  void shot(const session::RemoteWorld& remote, const game::ObjectTemplate* weapon, Vec3f eye,
            Vec3f direction);
  // Once a frame: the impacts found since the last one become effects and
  // marks; the marks age (`decalStep`, decal_lifetime.h).
  void draw(const session::RemoteWorld& remote, Vec3f eye, const Mat4& view, float step,
            std::vector<gfx::MeshRenderer::DrawItem>& items);
  void release(gfx::MeshRenderer& renderer);
  int impacts() const { return impacts_; }
  int marks() const { return decals_.count(); }

 private:
  struct Pending {
    Vec3f point, normal;
    int projectileMaterial = -1;
    std::uint16_t surfaceMaterial = 0;
    int round = 0;
  };
  FileSystem* files_ = nullptr;
  gfx::MeshRenderer* renderer_ = nullptr;
  SoundVoices* voices_ = nullptr;
  BulletDecals decals_;
  std::map<std::string, SmokeFlare, std::less<>> effects_;
  std::deque<Pending> pending_;
  int impacts_ = 0;
};

}  // namespace obf2::app
