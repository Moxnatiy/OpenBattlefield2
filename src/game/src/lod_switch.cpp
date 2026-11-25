#include "obf2/game/lod_switch.h"

#include <cmath>

namespace obf2::game {

std::vector<float> lodSwitchDistances(const std::vector<float>* given, std::size_t skip,
                                      std::size_t count, float step) {
  std::vector<float> out;
  if (count < 2) return out;
  out.resize(count - 1);
  float running = static_cast<float>(skip + 1) * step;
  for (std::size_t i = 0; i + 1 < count; ++i) {
    if (given != nullptr && given->size() > i) {
      out[i] = skip + i < given->size() ? (*given)[skip + i] : running;
      running += (*given)[i];
    } else {
      out[i] = running;
      running += step;
    }
  }
  return out;
}

float lodBaseRadius(const mesh::Aabb& box) {
  const float dx = (box.max.x - box.min.x) * 0.5f;
  const float dy = (box.max.y - box.min.y) * 0.5f;
  const float dz = (box.max.z - box.min.z) * 0.5f;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float lodDistanceScale(int geometryQuality) {
  if (geometryQuality <= 1) return 2.0f * 0.5f;
  if (geometryQuality == 2) return 1.0f * 0.8f;
  return 1.0f;
}

float lodDistanceScaleBundled(int geometryQuality) {
  if (geometryQuality <= 1) return 0.5f;
  if (geometryQuality == 2) return 0.8f;
  return 1.0f;
}

float lodFovFactor(float fov, float firstFov) {
  if (fov == firstFov) return 1.0f;
  const float ratio = std::tan(firstFov * 0.5f);
  // setFov stores tan(first/2)/tan(fov/2) at +0x1c and, when that is not 0,
  // its inverse at +0x20; the case of 0 leaves +0x20 at 0.
  if (ratio == 0.0f) return 0.0f;
  return std::tan(fov * 0.5f) / ratio;
}

std::size_t pickLod(const std::vector<float>& distances, float scale, float fovFactor,
                    float distance, float baseRadius) {
  const float reach = fovFactor * distance - baseRadius;
  for (std::size_t i = distances.size(); i-- > 0;) {
    if (scale * distances[i] < reach) return i + 1;
  }
  return 0;
}

}  // namespace obf2::game
