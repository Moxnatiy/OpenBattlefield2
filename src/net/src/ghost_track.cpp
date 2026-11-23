#include "obf2/net/ghost_track.h"

#include <algorithm>
#include <cmath>

namespace obf2::net::bf2 {

std::array<float, 4> interpolateGhostRotation(const std::array<float, 4>& a,
                                              const std::array<float, 4>& b, float fraction) {
  const float sign = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3] < 0 ? -1.0f : 1.0f;
  std::array<float, 4> blended;
  float norm = 0.0f;
  for (std::size_t i = 0; i < blended.size(); ++i) {
    blended[i] = std::lerp(a[i], sign * b[i], fraction);
    norm += blended[i] * blended[i];
  }
  if (norm <= 0.0f) return a;
  const float scale = 1.0f / std::sqrt(norm);
  for (float& component : blended) component *= scale;
  return blended;
}

GhostSample& GhostTrack::push(float timeMs, bool hasPosition, bool hasRotation) {
  if (count_ > 0 && ring_[head_].timeMs == timeMs) {
    ring_[head_].hasPosition |= hasPosition;
    ring_[head_].hasRotation |= hasRotation;
    return ring_[head_];
  }
  const GhostSample previous = count_ > 0 ? ring_[head_] : GhostSample{};
  head_ = count_ > 0 ? (head_ + 1) % kSlots : 0;
  if (count_ < kSlots) ++count_;
  ring_[head_] = previous;
  ring_[head_].timeMs = timeMs;
  ring_[head_].hasPosition = hasPosition || count_ == 1;
  ring_[head_].hasRotation = hasRotation;
  return ring_[head_];
}

const GhostSample* GhostTrack::newest() const { return count_ > 0 ? &ring_[head_] : nullptr; }

std::optional<GhostPose> GhostTrack::poseAt(float nowMs, float interpolationMs,
                                            float extrapolationMs) const {
  const GhostSample* last = newest();
  if (last == nullptr) return std::nullopt;

  const auto samplePose = [](const GhostSample& sample) {
    GhostPose pose;
    pose.position = sample.position;
    pose.bodyYaw = sample.bodyYaw;
    pose.pitch = sample.pitch;
    pose.aimYaw = sample.aimYaw;
    pose.rotation = sample.rotation;
    pose.stance = sample.stance;
    return pose;
  };
  GhostPose pose = samplePose(*last);

  const float at = nowMs - interpolationMs;
  if (at <= last->timeMs) {
    // 0x62bb10: needs two updates and a moment not before the oldest.
    if (count_ < 2) return pose;
    const std::size_t oldest = head_ + kSlots - (count_ - 1);
    if (slot(oldest).timeMs > at) return pose;
    for (std::size_t i = 1; i < count_; ++i) {
      const GhostSample& later = slot(oldest + i);
      if (later.timeMs < at) continue;
      const GhostSample& earlier = slot(oldest + i - 1);
      const float gap = later.timeMs - earlier.timeMs;
      if (!(gap > 0.0f && gap < kGhostMaxGapMs)) return pose;
      const float f = gap > 1.0f ? (at - earlier.timeMs) / gap : 1.0f;
      pose.position = earlier.position + (later.position - earlier.position) * f;
      pose.bodyYaw = earlier.bodyYaw + std::remainder(later.bodyYaw - earlier.bodyYaw, 360.0f) * f;
      pose.pitch = earlier.pitch + (later.pitch - earlier.pitch) * f;
      pose.aimYaw = earlier.aimYaw + (later.aimYaw - earlier.aimYaw) * f;
      pose.stance = (f < 1.0f ? earlier : later).stance;
      if (earlier.rotation && later.rotation) {
        pose.rotation = interpolateGhostRotation(*earlier.rotation, *later.rotation, f);
      }
      pose.mode = GhostPrediction::Interpolated;
      return pose;
    }
    return pose;
  }

  if (extrapolationMs <= 0.0f) return pose;
  const float over = at - last->timeMs;
  if (over >= extrapolationMs) return pose;
  // A soldier carries its velocity; a simple object's is the difference of its
  // last two positions over their gap (0x62f7d0).
  Vec3f velocity{};
  if (last->velocity) {
    velocity = *last->velocity;
  } else if (count_ >= 2) {
    const GhostSample& before = slot(head_ + kSlots - 1);
    const float gap = last->timeMs - before.timeMs;
    if (gap > 0.0f) velocity = (last->position - before.position) * (1000.0f / gap);
  }
  pose.position = last->position + velocity * (over * 0.001f);
  pose.mode = GhostPrediction::Extrapolated;
  return pose;
}

}  // namespace obf2::net::bf2
