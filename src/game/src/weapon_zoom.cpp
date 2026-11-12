#include "obf2/game/weapon_zoom.h"

#include <algorithm>
#include <cmath>

namespace obf2::game {
namespace {

std::optional<float> zoomFloat(const Component& zoom, const char* property) {
  const auto found = zoom.properties.find(property);
  if (found == zoom.properties.end() || found->second.empty()) return std::nullopt;
  return found->second.back().asFloat(0);
}

}  // namespace

std::optional<ZoomTemplate> zoomOf(const ObjectTemplate& weapon) {
  const Component* zoom = weapon.component("zoom");
  if (zoom == nullptr) return std::nullopt;
  ZoomTemplate out;
  if (const auto value = zoomFloat(*zoom, "zoomdelay")) out.zoomDelay = *value;
  if (const auto value = zoomFloat(*zoom, "changefovdelay")) out.changeFovDelay = *value;
  if (const auto value = zoomFloat(*zoom, "zoomlod")) out.zoomLod = static_cast<int>(*value);
  if (const auto value = zoomFloat(*zoom, "zoomoutafterfire")) out.zoomOutAfterFire = *value != 0.0f;
  if (const auto value = zoomFloat(*zoom, "mousespeedmod")) out.mouseSpeedMod = *value;
  if (const auto value = zoomFloat(*zoom, "disablemuzzlewhenzoomed")) {
    out.disableMuzzleWhenZoomed = *value != 0.0f;
  }
  if (const auto levels = zoom->properties.find("addzoomfactor"); levels != zoom->properties.end()) {
    for (const Property& call : levels->second) {
      const auto value = call.asFloat(0);
      if (!value) continue;
      // addZoomFactor (0x629ea0): a factor already there is skipped.
      if (std::find(out.factors.begin(), out.factors.end(), *value) == out.factors.end()) {
        out.factors.push_back(*value);
      }
    }
  }
  return out;
}

void WeaponZoom::setWeapon(const std::optional<ZoomTemplate>& weapon) {
  *this = WeaponZoom{};
  weapon_ = weapon;
}

float WeaponZoom::factor(int index) const {
  if (!weapon_) return 0.0f;
  // `getZoomFactor` (0x629d00): past the list is the constant at 0xb2f3c0, -1.
  return index >= 0 && static_cast<std::size_t>(index) < weapon_->factors.size()
             ? weapon_->factors[static_cast<std::size_t>(index)]
             : -1.0f;
}

float WeaponZoom::mouseSpeedMod() const {
  return weapon_ && index_ > 0 ? weapon_->mouseSpeedMod : 1.0f;
}

bool WeaponZoom::busy(std::uint32_t tick) const {
  if (!weapon_) return false;
  if (pendingToggle_ || pendingLod_) return true;
  return changedAt_ + static_cast<std::uint32_t>(weapon_->zoomDelayTicks()) > tick;
}

void WeaponZoom::changeZoom(int index, std::uint32_t tick) {
  // 0x629990: nothing when the level is already the one asked for.
  if (index == index_) return;
  changedAt_ = tick;
  const float target = factor(index);
  // With no `changeFovDelay` (-1) the camera is sent to the level over the zoom
  // delay (0x629b13), and the next `updateComponent` sets it outright anyway
  // (its delay in ticks is negative); with one, a zoom out is at once (0x629b40)
  // and a zoom in waits for that many ticks.
  if (weapon_->changeFovDelay == -1.0f || index == 0) {
    if (fov_ != target) {
      fov_ = target;
      changedThisTick_ = true;
    }
  }
  pendingFov_ = true;
  index_ = index;
  // The detail level (0x629a0d..0x629a39): none for a weapon without `zoomLod`;
  // level 0 at once when zooming out; a zoom in sets it at once when the zoom
  // delay is 0 and otherwise waits for it (`updateComponent`, 0x629910).
  if (weapon_->zoomLod > 0) {
    if (index == 0) {
      lod_ = 0;
      pendingLod_ = false;
    } else if (weapon_->zoomDelayTicks() == 0) {
      lod_ = weapon_->zoomLod;
    } else {
      pendingLod_ = true;
    }
  }
}

void WeaponZoom::toggle(std::uint32_t tick) {
  // DefaultZoomComp::toggle (0x629c70): the next level, round to 0 past the last.
  if (!weapon_ || weapon_->factors.size() < 2) return;
  const int next = index_ + 1;
  changeZoom(static_cast<std::size_t>(next) < weapon_->factors.size() ? next : 0, tick);
}

void WeaponZoom::press(std::uint32_t tick) {
  if (!weapon_ || busy(tick)) return;
  toggle(tick);
}

void WeaponZoom::onRound() {
  if (weapon_ && weapon_->zoomOutAfterFire && index_ != 0) pendingToggle_ = true;
}

void WeaponZoom::onReload(std::uint32_t tick) {
  if (weapon_ && index_ != 0) changeZoom(0, tick);
}

bool WeaponZoom::update(std::uint32_t tick) {
  changedThisTick_ = false;
  if (!weapon_) return false;
  if (pendingLod_ &&
      changedAt_ + static_cast<std::uint32_t>(weapon_->zoomDelayTicks()) <= tick) {
    lod_ = weapon_->zoomLod;
    pendingLod_ = false;
  }
  if (pendingFov_) {
    // 0x6298c4: `changeFovDelay` ticks after the change, the level's field of view
    // is set outright.
    if (static_cast<std::int64_t>(changedAt_) + weapon_->changeFovDelayTicks() <=
        static_cast<std::int64_t>(tick)) {
      const float target = factor(index_);
      if (fov_ != target) {
        fov_ = target;
        changedThisTick_ = true;
      }
      pendingFov_ = false;
    }
  }
  // 0x629896: a toggle a round asked for is made once nothing else of the weapon
  // is busy (assumed: nothing is).
  if (pendingToggle_) {
    pendingToggle_ = false;
    toggle(tick);
  }
  return changedThisTick_;
}

}  // namespace obf2::game
