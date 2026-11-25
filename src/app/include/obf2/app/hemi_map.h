#pragma once

// The level's base hemi map as sprite particles read it: `hemiMapManager.
// setBaseHemiMap <path> <centre> <size> <height>` (RendDX9.dll 0x100c4bcd →
// HemiMapManager vtable 0x1021ce84 +0x14 = 0x1012c2a0 → 0x1012b930). Notes in
// docs/functions/sprite-particles.md, "Hemi map".

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "obf2/core/math.h"

namespace obf2::app {

class HemiMap {
 public:
  // `dds` is `<path>.dds`. A missing file is an 8×8 white texture in the
  // original (0x1012ba13: every texel 0xffffffff), so alpha 1 everywhere.
  static HemiMap load(std::optional<std::span<const std::byte>> dds, Vec3f centre, float size,
                      float shadowAltitude);
  static HemiMap white(Vec3f centre, float size, float shadowAltitude);
  // Rows top to bottom, one alpha byte per texel.
  static HemiMap fromAlpha(int width, int height, std::vector<std::uint8_t> alpha, Vec3f centre,
                           float size, float shadowAltitude);

  // `HemiShadowAltitude`, the data's +0x10 (0x101721c0).
  float shadowAltitude() const { return shadowAltitude_; }
  // tex2D(lutSampler, texCoords2).a for a particle at `position`
  // (Particles.fx:93-94, FXCommon.fx:43-51: LINEAR, CLAMP). All six vertices
  // of a sprite carry its record position (0x10170070), so the coordinate is
  // the same over the sprite and mip level 0 is the one sampled.
  float alpha(Vec3f position) const;

 private:
  Vec3f centre_;
  float size_ = 0, shadowAltitude_ = 0;
  int width_ = 0, height_ = 0;
  std::vector<std::uint8_t> alpha_;
};

}  // namespace obf2::app
