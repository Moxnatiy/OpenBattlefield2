#pragma once
// A hand weapon's zoom: its template (`ObjectTemplate.zoom.*`) and the toggle the
// weapon's `DefaultZoomComp` runs on it (docs/functions/weapon-zoom.md).
//
// The server's code (Linux `DefaultZoomComp`, 0x629990..0x62a7d0) — the client
// runs the same component for its own weapon.
#include <cstdint>
#include <optional>
#include <vector>

#include "obf2/game/object_template.h"

namespace obf2::game {

// `DefaultZoomCompTemplate` (Linux 0x629d40, `makeScript` 0x62a030 names each
// command against its field). Times are seconds in the data, ticks (×30) in the
// component.
struct ZoomTemplate {
  // `zoom.addZoomFactor`, one call per level; index 0 is the level the weapon
  // starts at, the constructor's 0. A factor already in the list is not added
  // again (0x629ea0), so the sniper's own `addZoomFactor 0` is a no-op. The
  // factor is the camera's field of view, vertical, in radians; 0 is "the
  // camera's own" (`Player::setFov`, Linux 0x495e00: 0 → `getActiveFOV`).
  std::vector<float> factors{0.0f};
  float zoomDelay = 0.5f;           // `zoom.zoomDelay`, +0x38 (0xb2f234); ticks +0x3c
  float changeFovDelay = -1.0f;     // `zoom.changeFovDelay`, +0x48 (0xb2f3c0); ticks +0x4c
  int zoomLod = 0;                  // `zoom.zoomLod`, +0x40 — a render detail, not used
  bool zoomOutAfterFire = false;    // `zoom.zoomOutAfterFire`, +0x44
  float mouseSpeedMod = 1.0f;       // `zoom.mouseSpeedMod`, +0x50
  bool disableMuzzleWhenZoomed = false;  // `zoom.disableMuzzleWhenZoomed`, +0x54
  int input = 31;                   // `ZoomCompTemplate` +0x10: c_PIAltFire (0x1f)

  int zoomDelayTicks() const { return static_cast<int>(zoomDelay * 30.0f); }
  int changeFovDelayTicks() const { return static_cast<int>(changeFovDelay * 30.0f); }
};

// Null when the weapon has no zoom component.
std::optional<ZoomTemplate> zoomOf(const ObjectTemplate& weapon);

class WeaponZoom {
 public:
  void setWeapon(const std::optional<ZoomTemplate>& weapon);
  bool present() const { return weapon_.has_value(); }

  // The zoom key went down (`c_PIAltFire`, `TemplateMessage` 7 to the weapon:
  // `GenericFireArm::internalHandleMessage` 0x5e7506..0x5e753a asks the zoom
  // component whether the input is its own and calls `toggle`, 0x629c70). Ignored
  // while the component is busy (0x629500): a toggle waiting to happen, or inside
  // the zoom delay after the last change.
  void press(std::uint32_t tick);

  // A round left the barrel: with `zoomOutAfterFire` and a zoom on, the next update
  // asks for a toggle (`fireComplete`, 0x6295a0).
  void onRound();
  // A reload began: the zoom goes back to level 0 (`reload`, 0x629b70).
  void onReload(std::uint32_t tick);

  // One tick (`updateComponent`, 0x6297e0): a toggle asked for by a round is made;
  // a change of level takes the field of view `changeFovDelay` ticks later. True
  // when `fov()` changed in it.
  bool update(std::uint32_t tick);

  std::size_t levels() const { return weapon_ ? weapon_->factors.size() : 0; }
  int index() const { return index_; }
  bool zoomed() const { return index_ > 0; }
  bool hidesMuzzle() const { return zoomed() && weapon_ && weapon_->disableMuzzleWhenZoomed; }
  // The field of view the camera is set to: 0 means its own.
  float fov() const { return fov_; }
  // The detail level the weapon's mesh is drawn at: 0, and the template's `zoomLod`
  // once zoomed (`setZoomLod`, 0x6299xx): the M24's lod 1 is the inside of the
  // scope, with the scope texture. It comes `zoomDelay` ticks after the zoom in and
  // goes at once with the zoom out.
  int lod() const { return lod_; }
  // `getMouseSpeedMod` (0x6295c0): the template's while zoomed, 1 otherwise.
  float mouseSpeedMod() const;

 private:
  bool busy(std::uint32_t tick) const;
  void toggle(std::uint32_t tick);
  void changeZoom(int index, std::uint32_t tick);
  float factor(int index) const;

  std::optional<ZoomTemplate> weapon_;
  int index_ = 0;                  // +0x34
  std::uint32_t changedAt_ = 0;    // +0x38
  bool pendingFov_ = false;        // +0x3e
  bool pendingToggle_ = false;     // +0x44
  bool pendingLod_ = false;        // +0x3c
  int lod_ = 0;
  float fov_ = 0.0f;               // the camera's end field of view (Player +0xdc)
  bool changedThisTick_ = false;
};

}  // namespace obf2::game
