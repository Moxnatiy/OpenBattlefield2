#include "obf2/net/ghost_track.h"

namespace obf2::net::bf2 {

GhostSample& GhostTrack::push(float timeMs) {
  const GhostSample previous = count_ > 0 ? ring_[head_] : GhostSample{};
  head_ = count_ > 0 ? (head_ + 1) % kSlots : 0;
  if (count_ < kSlots) ++count_;
  ring_[head_] = previous;
  ring_[head_].timeMs = timeMs;
  return ring_[head_];
}

const GhostSample* GhostTrack::newest() const { return count_ > 0 ? &ring_[head_] : nullptr; }

std::optional<GhostPose> GhostTrack::poseAt(float nowMs, float interpolationMs,
                                            float extrapolationMs) const {
  const GhostSample* last = newest();
  if (last == nullptr) return std::nullopt;

  GhostPose pose;
  pose.position = last->position;
  pose.bodyYaw = last->bodyYaw;
  pose.pitch = last->pitch;

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
      pose.bodyYaw = earlier.bodyYaw + (later.bodyYaw - earlier.bodyYaw) * f;
      pose.pitch = earlier.pitch + (later.pitch - earlier.pitch) * f;
      pose.mode = GhostPrediction::Interpolated;
      return pose;
    }
    return pose;
  }

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
