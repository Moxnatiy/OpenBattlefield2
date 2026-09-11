#include "obf2/hud/combat_area.h"

namespace obf2::hud {

bool insidePolygon(const std::vector<Vec3f>& polygon, float x, float z) {
  if (polygon.size() < 3) return false;
  bool inside = false;
  std::size_t previous = polygon.size() - 1;
  for (std::size_t i = 0; i < polygon.size(); previous = i++) {
    const float zi = polygon[i].z;
    const float zj = polygon[previous].z;
    if ((zi > z) == (zj > z)) continue;
    const float crossing =
        polygon[i].x + (z - zi) / (zj - zi) * (polygon[previous].x - polygon[i].x);
    if (x < crossing) inside = !inside;
  }
  return inside;
}

texture::Texture buildCombatAreaOverlay(const texture::Texture& hatch,
                                        const std::vector<Vec3f>& polygon,
                                        const WorldSquare& square) {
  if (hatch.format != texture::Format::Bgra8 || hatch.mips.empty()) return hatch;
  if (polygon.size() < 3) return hatch;
  const float width = square.maxX - square.minX;
  const float height = square.maxZ - square.minZ;
  if (width <= 0.0f || height <= 0.0f) return hatch;

  // Only the full-size level is touched. The smaller mips are left as they
  // shipped: the overlay is drawn at one to one over a 512-pixel square, so
  // nothing else is ever sampled, and a hole cut into a blurred copy would
  // only be another picture to keep in step.
  texture::Texture out = hatch;
  const texture::MipLevel& level = out.mips.front();
  if (level.width == 0 || level.height == 0) return hatch;
  auto* pixels = reinterpret_cast<std::uint8_t*>(out.data.data()) + level.offset;

  for (std::uint32_t row = 0; row < level.height; ++row) {
    // v runs against z: the top of the picture is the far edge of the square.
    const float z =
        square.maxZ - (static_cast<float>(row) + 0.5f) / static_cast<float>(level.height) * height;
    for (std::uint32_t column = 0; column < level.width; ++column) {
      const float x =
          square.minX + (static_cast<float>(column) + 0.5f) / static_cast<float>(level.width) * width;
      if (!insidePolygon(polygon, x, z)) continue;
      // BGRA: the alpha is the fourth byte.
      pixels[(static_cast<std::size_t>(row) * level.width + column) * 4 + 3] = 0;
    }
  }
  return out;
}

}  // namespace obf2::hud
