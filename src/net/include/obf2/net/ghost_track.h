#pragma once
// Where another object is drawn between its ghost updates — the client side of
// `SoldierNetworkable::predict` (`BF2.exe` 0x62d130) and
// `SimpleObjectNetworkable::predict` (0x62f7d0).
//
// Both keep a ring of the last four updates (0xa0 bytes each for a soldier at
// `+0x54`, 0xbc for a simple object), stamped with the **server's** time: the
// ghost stream's header carries the server tick, `GhostManager::readData`
// (0x5b9ee0) stores `tick * (1/30)` seconds and `readData` (0x5b8840) passes it
// on in milliseconds. Every record pushes a new slot, copied from the previous
// one first (the `memcpy` of 0x28 floats at the top of 0x62d4e0), so an update
// that changes nothing still moves time forward.
//
// Drawing asks for the pose at `now - GSInterpolationTime`:
//
//   * inside the ring — the two updates around that moment (0x62bb10: the first
//     one not older than it, and the one before), and when they are more than 0
//     and less than 500 ms apart the position is interpolated linearly, the yaw
//     and pitch too (0x6da3d0 with the lerped angles); a gap of 1 ms or less
//     takes the later one;
//   * past the newest — extrapolated along the velocity for at most
//     `GSExtrapolationTime`;
//   * otherwise — the newest as it is.
//
// The two times are server settings registered with defaults at 0x4077cd:
// `GSInterpolationTime` 100 ms and `GSExtrapolationTime` 1200 ms (0x4b0), read
// into `+0x50` / `+0x54` of the game object by 0x6a5f60.
#include <array>
#include <cstddef>
#include <optional>

#include "obf2/core/math.h"

namespace obf2::net::bf2 {

inline constexpr float kGhostInterpolationMs = 100.0f;   // GSInterpolationTime, 0x4077d5
inline constexpr float kGhostExtrapolationMs = 1200.0f;  // GSExtrapolationTime, 0x407809
inline constexpr float kGhostMaxGapMs = 500.0f;          // 0x62d130 and 0x62f7d0
inline constexpr float kGhostTickMs = 1000.0f / 30.0f;   // 0x5b9ee0: tick * 0.0333 s

struct GhostSample {
  float timeMs = 0.0f;
  Vec3f position;
  std::optional<Vec3f> velocity;  // a soldier's own (0x80); a simple object has none
  float bodyYaw = 0.0f;           // a soldier's 0x2 + 0x4 (soldier_state.h)
  float pitch = 0.0f;
};

enum class GhostPrediction {
  Newest = 0,       // the state machine's 0 at `+0x10`
  Extrapolated = 1,
  Interpolated = 2,
};

struct GhostPose {
  Vec3f position;
  float bodyYaw = 0.0f;
  float pitch = 0.0f;
  GhostPrediction mode = GhostPrediction::Newest;
};

class GhostTrack {
 public:
  static constexpr std::size_t kSlots = 4;

  // A new slot at `timeMs`, starting as a copy of the newest; the caller then
  // writes what the record carried.
  GhostSample& push(float timeMs);

  std::size_t count() const { return count_; }
  const GhostSample* newest() const;

  // The pose at `nowMs`. nullopt only when nothing was pushed.
  std::optional<GhostPose> poseAt(float nowMs, float interpolationMs = kGhostInterpolationMs,
                                  float extrapolationMs = kGhostExtrapolationMs) const;

 private:
  const GhostSample& slot(std::size_t index) const { return ring_[index % kSlots]; }

  std::array<GhostSample, kSlots> ring_{};
  std::size_t head_ = 0;  // the newest
  std::size_t count_ = 0;
};

}  // namespace obf2::net::bf2
