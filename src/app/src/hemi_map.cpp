#include "obf2/app/hemi_map.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "obf2/texture/dds.h"

namespace obf2::app {

HemiMap HemiMap::white(Vec3f centre, float size, float shadowAltitude) {
  HemiMap map;
  map.centre_ = centre;
  map.size_ = size;
  map.shadowAltitude_ = shadowAltitude;
  map.width_ = map.height_ = 8;
  map.alpha_.assign(64, 255);
  return map;
}

HemiMap HemiMap::fromAlpha(int width, int height, std::vector<std::uint8_t> alpha, Vec3f centre,
                           float size, float shadowAltitude) {
  HemiMap map = white(centre, size, shadowAltitude);
  if (width <= 0 || height <= 0 || alpha.size() < static_cast<std::size_t>(width) * height) return map;
  map.width_ = width;
  map.height_ = height;
  map.alpha_ = std::move(alpha);
  return map;
}

HemiMap HemiMap::load(std::optional<std::span<const std::byte>> dds, Vec3f centre, float size,
                      float shadowAltitude) {
  HemiMap map = white(centre, size, shadowAltitude);
  if (!dds) return map;
  const auto texture = texture::loadDds(*dds);
  if (!texture) return map;
  const auto rgba = texture::decodeRgba8(*texture);
  const auto count = static_cast<std::size_t>(texture->width) * texture->height;
  if (rgba.size() < count * 4) return map;
  std::vector<std::uint8_t> alpha(count);
  for (std::size_t i = 0; i < count; ++i) alpha[i] = rgba[i * 4 + 3];
  return fromAlpha(static_cast<int>(texture->width), static_cast<int>(texture->height), std::move(alpha),
                   centre, size, shadowAltitude);
}

float HemiMap::alpha(Vec3f p) const {
  if (size_ <= 0 || alpha_.empty()) return 1.0f;
  // HemiMapInfo = (centre.x, centre.z, size, …) (0x1012b620); the shader's
  // ((pos + size/2).xz − info.xy) / size, then v = 1 − v.
  const float u = (p.x + size_ * 0.5f - centre_.x) / size_;
  const float v = 1.0f - (p.z + size_ * 0.5f - centre_.z) / size_;
  // Bilinear with clamped addressing, texel centres at (i + 0.5) / n.
  const float x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(width_) - 0.5f;
  const float y = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(height_) - 0.5f;
  const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
  const auto at = [&](int i, int j) {
    i = std::clamp(i, 0, width_ - 1);
    j = std::clamp(j, 0, height_ - 1);
    return static_cast<float>(alpha_[static_cast<std::size_t>(j) * width_ + i]) / 255.0f;
  };
  const float top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
  const float bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
  return top + (bottom - top) * fy;
}

}  // namespace obf2::app
