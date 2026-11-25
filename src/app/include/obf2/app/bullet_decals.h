#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/game/object_template.h"
#include "obf2/app/decal_lifetime.h"
#include "obf2/app/decal_placement.h"
#include "obf2/gfx/mesh_renderer.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::app {

class BulletDecals {
 public:
  struct Receiver {
    const mesh::RenderMesh* geometry = nullptr;
    Mat4 transform = Mat4::identity();
    Vec3f origin, ray;
  };
  void add(FileSystem& files, const game::Registry& registry, gfx::MeshRenderer& renderer,
           int projectileMaterial, std::uint16_t surfaceMaterial, Vec3f point, Vec3f normal,
           const Receiver* receiver = nullptr);
  std::string_view impactEffect(FileSystem& files, int projectileMaterial, std::uint16_t surfaceMaterial);
  // The cell's primary `setSoundTemplate` (materialManagerSettings.con).
  std::string_view impactSound(FileSystem& files, int projectileMaterial, std::uint16_t surfaceMaterial);
  void advance(float step, int round);
  void draw(std::vector<gfx::MeshRenderer::DrawItem>& items) const;
  void release(gfx::MeshRenderer& renderer);
  int count() const { return count_; }
  // The placement of the last mark `add` drew its random values for, for traces.
  const std::optional<DecalPlacement>& lastPlacement() const { return lastPlacement_; }
  std::size_t liveCount() const { return marks_.size(); }
  float oldestAge() const { return marks_.empty() ? 0.0f : marks_.front().lifetime.age; }
  float oldestOpacity() const { return marks_.empty() ? 0.0f : marks_.front().lifetime.opacity(); }

 private:
  struct Decal {
    std::vector<gfx::GpuMesh> frames;
    DecalTemplateValues values;
    float width = 1.0f, height = 1.0f;
  };

  struct MaterialPair {
    std::string decal;
    std::string effect;
    std::string sound;
  };

  const MaterialPair* materialPair(FileSystem& files, int projectileMaterial, std::uint16_t surfaceMaterial);
  std::map<std::pair<int, int>, MaterialPair> materials_;
  std::map<std::string, Decal> decals_;
  struct Mark {
    gfx::MeshRenderer::DrawItem item;
    DecalLifetime lifetime;
    std::unique_ptr<gfx::GpuMesh> projected;
  };
  void releaseMark(Mark& mark);
  gfx::MeshRenderer* renderer_ = nullptr;
  std::deque<Mark> marks_;
  DecalRandom random_;
  std::optional<DecalPlacement> lastPlacement_;
  bool loaded_ = false;
  int round_ = -1;
  int count_ = 0;
};

}  // namespace obf2::app
