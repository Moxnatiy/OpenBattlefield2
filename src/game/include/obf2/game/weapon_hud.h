#pragma once
// A handheld weapon's crosshair: its HUD settings, its deviation as the engine
// grows and shrinks it every input, and where the four ticks stand
// (docs/functions/hud-crosshair.md).
#include <string_view>

#include "obf2/game/object_template.h"

namespace obf2::game {

// `ObjectTemplate.weaponHud.*`. The defaults are `WeaponHudCompTemplate`'s
// constructor (Linux 0x5c4f00); the client reads the same fields at its
// `getHudInfo` +0x8c..+0xa0.
struct WeaponHudTemplate {
  int guiIndex = 5;               // which HUD group the weapon shows (`GuiIndex`)
  int altGuiIndex = -1;           // not used yet: the zoomed group
  int deviationFactor = 10;
  int maxVisualDeviation = 1000;
  int minVisualDeviation = 0;
  int crosshairIconSize = 32;     // not used yet: only `CrosshairIconPathString` weapons
};
WeaponHudTemplate weaponHudOf(const ObjectTemplate& weapon);

// `ObjectTemplate.deviation.*` — a `SoldierDeviationComp`'s template. The
// defaults are its constructor's (Linux 0x5f12a0 over 0x5ef2e0): every term 0,
// every mod 1. The client's field each one lives in is in the notes.
struct DeviationTemplate {
  float fireMax = 0.0f;     // setFireDev a, +0x10: in getMaxDeviation only
  float fireAdd = 0.0f;     // setFireDev b, +0x08: per shot (postFire, not reversed)
  float fireFall = 0.0f;    // setFireDev c, +0x0c
  float minDev = 0.0f;      // +0x14
  float turnMax = 0.0f;     // setTurnDev a, +0x2c
  float turnPitch = 0.0f;   // setTurnDev b, +0x20: × |mouse Y|
  float turnYaw = 0.0f;     // setTurnDev c, +0x24: × |mouse X|
  float turnFall = 0.0f;    // setTurnDev d, +0x28
  float speedMax = 0.0f;    // setSpeedDev a, +0x3c
  float speedForward = 0.0f;  // setSpeedDev b, +0x30: while throttling
  float speedStrafe = 0.0f;   // setSpeedDev c, +0x34: while strafing
  float speedFall = 0.0f;   // setSpeedDev d, +0x38
  float miscMax = 0.0f;     // setMiscDev a, +0x48
  float miscAdd = 0.0f;     // setMiscDev b, +0x40
  float miscFall = 0.0f;    // setMiscDev c, +0x44
  float modStand = 1.0f;    // devModStand, +0x4c
  float modCrouch = 1.0f;   // devModCrouch, +0x50
  float modLie = 1.0f;      // devModLie, +0x54
  float modZoom = 1.0f;     // devModZoom, +0x58
};
DeviationTemplate deviationOf(const ObjectTemplate& weapon);

// What one `Soldier::handlePlayerInput` hands the weapon (`BF2.exe` 0x5af4ea):
// the input's axes — zero where the input's mask does not carry them — and the
// two flags. An axis smaller than 0.01 counts as still.
struct DeviationInput {
  float strafe = 0.0f;    // axis 0, c_PIYaw (A/D)
  float throttle = 0.0f;  // axis 3, c_PIThrottle (W/S)
  float mouseX = 0.0f;    // axis 4, c_PIMouseLookX
  float mouseY = 0.0f;    // axis 5, c_PIMouseLookY
  // Jump (c_PIAction, axis 9, after the soldier's own vetoes) or lie down
  // (c_PILie, axis 37) this input; both cleared in a vehicle (0x5ae2c0..0x5ae308).
  bool jumpOrLie = false;
  bool firing = false;    // GenericFireArm::getFireState() != 0
  int pose = 0;           // 0 stand, 1 crouch, 2 lie, 3 swim (counts as stand)
  bool zoomed = false;    // setZoom
};

// `SoldierDeviationComp::updateDeviation`, `BF2.exe` 0x60ad60, once per input.
class SoldierDeviation {
 public:
  void update(const DeviationTemplate& tmpl, const DeviationInput& input);
  float deviation() const { return deviation_; }

  float speed() const { return speed_; }
  float turn() const { return turn_; }
  float misc() const { return misc_; }
  float shot() const { return shot_; }

 private:
  float speed_ = 0.0f;  // +0x24
  float turn_ = 0.0f;   // +0x20
  float misc_ = 0.0f;   // +0x28
  float shot_ = 0.0f;   // +0x08
  float deviation_ = 0.0f;  // +0x04
};

// The four ticks' coordinates in the HUD's 800×600 space, `BF2.exe` 0x7a5690:
// `CrosshairUpPos`, `CrosshairDownPos` (Y), `CrosshairLeftPos`,
// `CrosshairRightPos` (X).
struct CrosshairTicks {
  int up = 0, down = 0, left = 0, right = 0;
};
CrosshairTicks crosshairTicks(const WeaponHudTemplate& hud, float deviation);

}  // namespace obf2::game
