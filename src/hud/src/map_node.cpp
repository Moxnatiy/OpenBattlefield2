#include "obf2/hud/map_node.h"

#include <cmath>

namespace obf2::hud {
namespace {

// The engine's smoothing: `FUN_00402ee0(dt * -6.0, -10, 10)` clamps the exponent,
// then comes e^x (ROUND/f2xm1/fscale), and the result is
//   value += (1 - e^(-6*dt)) * (target - value)
// (`BF2.exe`, 0x77d1a4..0x77d1c1 — the same block repeated for every animated
// field).
void approach(float& value, float target, float dt, float snapDistance, bool snapNow) {
  if (snapNow || std::fabs(value - target) < snapDistance) {
    value = target;
    return;
  }
  float exponent = dt * -kMapApproachRate;
  if (exponent < -10.0f) exponent = -10.0f;
  if (exponent > 10.0f) exponent = 10.0f;
  value += (1.0f - std::exp(exponent)) * (target - value);
}

// Wrap an angle into (-pi, pi] — `FUN_007722d0`: `(a/2pi - floor(a/2pi +
// 0.5)) * 2pi`.
float wrapAngle(float radians) {
  const float turns = radians * 0.15915494f;
  return (turns - std::floor(turns + 0.5f)) * 6.2831855f;
}

// Which way to turn from `from` to `to`: -1, 0 or +1. Verbatim
// `FUN_00772310` (`BF2.exe`, 0x772310) — zero is also returned when the angles
// coincide up to a full turn.
int angleDirection(float to, float from, float epsilon) {
  if (std::fabs(from - to) < epsilon || std::fabs(from - (to + 6.2831855f)) < epsilon ||
      std::fabs(from - (to - 6.2831855f)) < epsilon) {
    return 0;
  }
  const float span = std::fabs(to - from);
  if (span > 3.1415927f) {
    if (from < 0.0f && to > 0.0f) return -1;
    if (from > 0.0f && to < 0.0f) return 1;
  }
  return static_cast<int>(std::ceil((to - from) / span));
}

// The shortest distance between angles — `FUN_00772410` (0x772410).
float angleDistance(float a, float b) {
  float difference = (a >= 0.0f || b <= 0.0f) ? a - b : b - a;
  if (std::fabs(difference) > 3.1415927f) difference = 6.2831855f - std::fabs(difference);
  return difference;
}

// One step of an angle's approach: 0x77c4f4..0x77c569 and 0x77c632..0x77c6b2 —
// both identical.
void approachAngle(float& value, float target, float dt) {
  value = wrapAngle(value);
  const int direction = angleDirection(target, value, kMapAngleEpsilon);
  if (direction == 0) {
    value = target;
    return;
  }
  float exponent = dt * -kMapAngleRate;
  if (exponent < -10.0f) exponent = -10.0f;
  if (exponent > 10.0f) exponent = 10.0f;
  value += std::fabs(angleDistance(target, value)) * (1.0f - std::exp(exponent)) *
           static_cast<float>(direction);
}

}  // namespace

void MapAngle::update(float dt) {
  approachAngle(angle_, wrapAngle(target_), dt);
  approachAngle(delayed_, wrapAngle(angle_), dt);
}

void MapNode::takeRects(const Node& node) {
  const auto take = [](const MapRect& rect, MapPoint& position, MapPoint& size) {
    if (!rect.set) return;
    position = MapPoint{rect.x, rect.y};
    size = MapPoint{rect.width, rect.height};
  };
  take(node.mapMini, mini_, miniSize_);
  take(node.mapMaxi, maxi_, maxiSize_);
  take(node.mapCommander, commander_, commanderSize_);
  applyState(state_);
  snap();
}

void MapNode::applyState(int state) {
  state_ = state;
  switch (state) {
    case kMapStateIngame:
      // 0x777e3d: the target is the thumbnail in the corner.
      targetPosition_ = mini_;
      targetSize_ = miniSize_;
      commanderMode_ = false;
      break;

    case kMapStateSpawn:
    case kMapStateBigMap:
      // 0x777f8a and 0x778089: both states take one and the same big rectangle.
      // They differ only in alpha: on the spawn screen it is fixed (1.0 and 0.5
      // for the icons), while on the big map it comes from the settings
      // `MenuMapAlpha`/`MenuMapIconAlpha`.
      // We do not drive the alpha yet — see "not measured" below.
      targetPosition_ = maxi_;
      targetSize_ = maxiSize_;
      commanderMode_ = false;
      break;

    case kMapStateSquadLeaderMenu:
    case kMapStateCommanderMenu:
      // 0x778299 and 0x778281. In the original these two states also look at the
      // previous state (0x1e4): if we already came from the corresponding menu,
      // the target is left alone.
      targetPosition_ = maxi_;
      targetSize_ = maxiSize_;
      commanderMode_ = false;
      break;

    case kMapStateCommander:
      // 0x7781b0: only if commander mode is not on yet.
      if (!commanderMode_) {
        commanderMode_ = true;
        targetPosition_ = commander_;
        targetSize_ = commanderSize_;
      }
      break;

    default:
      // The other states do not change the map's target (0x777e11, the branch
      // 9/0xc/0xd/0xe/0x10/0x13/0x14/0x15/0x1a/0x1b/0x1c) — state 19
      // `MapMenuShow` included: the quick zoom menu lands **on top of** the map
      // and does not move the map itself.
      break;
  }
}

void MapNode::snap() { snapNext_ = true; }

void MapNode::setCentre(float u, float v) {
  targetCentre_ = MapPoint{u, v};
}

void MapNode::setZoomIndex(int index) {
  if (index < 0) index = 0;
  if (index >= kMapZoomLevels) index = kMapZoomLevels - 1;
  zoomIndex_ = index;
}

float MapNode::zoomScale() const { return std::pow(kMapZoomBase, zoom_); }

void MapNode::update(float dt) {
  const bool snapNow = snapNext_;
  snapNext_ = false;

  // The order and the half-screen offset come from 0x77d13c..0x77d2b0: (400, 300)
  // is added to the target the state set, because the map's coordinates in the
  // data are counted from the screen's centre.
  approach(position_.y, targetPosition_.y + kReferenceHeight * 0.5f, dt, kMapSnapDistance,
           snapNow);
  approach(position_.x, targetPosition_.x + kReferenceWidth * 0.5f, dt, kMapSnapDistance, snapNow);
  approach(size_.y, targetSize_.y, dt, kMapSnapDistance, snapNow);
  approach(size_.x, targetSize_.x, dt, kMapSnapDistance, snapNow);

  // The centre and the zoom use the same speed of 6.0 (0x77cd10 for +0x748/+0x74c
  // and 0x77ce90 for +0x698). The threshold here differs: the centre is measured
  // in fractions of the world, so 0.1 would be half the map.
  approach(centre_.x, targetCentre_.x, dt, 0.0001f, snapNow);
  approach(centre_.y, targetCentre_.y, dt, 0.0001f, snapNow);
  approach(zoom_, static_cast<float>(zoomIndex_), dt, 0.001f, snapNow);

  // 0x77d3a0: both show variables are **derived from the current size**, not from
  // the state number. While the size is in transit neither is on — and that is
  // exactly why in the original the minimap's frame manages to fade before the
  // big one's frame appears.
  if (state_ == kMapStateCommander) {
    if (size_.x <= commanderSize_.x - kMapSizeTolerance ||
        size_.y <= commanderSize_.y - kMapSizeTolerance) {
      settled_ = false;
      fullSize_ = false;
      minSize_ = false;
    } else {
      settled_ = true;
      fullSize_ = true;
      minSize_ = false;
    }
    return;
  }

  if (size_.x <= maxiSize_.x - kMapSizeTolerance ||
      size_.y <= maxiSize_.y - kMapSizeTolerance) {
    if (miniSize_.x + kMapSizeTolerance <= size_.x ||
        miniSize_.y + kMapSizeTolerance <= size_.y) {
      settled_ = false;
      fullSize_ = false;
      minSize_ = false;
    } else {
      settled_ = true;
      fullSize_ = false;
      minSize_ = true;
    }
  } else {
    settled_ = true;
    fullSize_ = true;
    minSize_ = false;
  }
}

}  // namespace obf2::hud
