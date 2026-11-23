#pragma once

#include "obf2/core/math.h"
#include "obf2/game/object_template.h"

namespace obf2::game {

// The engine step of `Engine::handleUpdate` (Linux 0x62f020) that sets the
// rpm, the load and the gear (docs/functions/engine-sound.md, "The driven
// vehicle's rpm"). The engine is a RotationalBundle: its axis angles (+0x240,
// stepped by calculateAndClipAngle 0x585a40 before this) are its input.

// `setEngineType` values (Linux `operator<<(EngineType)` 0x630fa0):
// Plane 0x1, Car 0x2, Tank 0x6, Ship 0x9, Rocket 0x11, Torpedo 0x19,
// Helicopter 0x21, NewCar 0x42, NewTank 0x46, NewCar2 0xc2; 0 otherwise.
unsigned engineTypeFromName(std::string_view name);

// EngineTemplate fields (makeScript, Linux 0x631180…).
struct EngineUpdateSettings {
  unsigned type = 0;              // setEngineType
  Vec3f maxRotation{};            // RotationalBundleTemplate +0x200 (setMaxRotation)
  int numberOfGears = 1;          // +0x448; constructor 0x632538 stores 1
  float differential = 0;         // +0x44c (setDifferential), the tank branch's maxSpeed
  float gearUp = 0;               // +0x454 (setGearUp)
  float gearDown = 0;             // +0x458 (setGearDown)
  float gearChangeTime = 0;       // +0x45c (setGearChangeTime)
  // +0x614 / +0x618: the overheat rise and fall. Their property names are
  // not established, so no template sets them here (0: off).
  float heatRise = 0, heatFall = 0;
};
EngineUpdateSettings engineUpdateSettings(const ObjectTemplate& engine);

// Engine statics, Linux 0x62db40 (Vars::getFloat defaults).
inline constexpr float kPhyEngineInertia = 0.05f;      // 0xb38948
inline constexpr float kPhyTankEngineInertia = 0.07f;  // 0xb56710
inline constexpr float kFeedbackMod = 0.95f;           // 0xb56720

// EngineBase fields the step reads and writes.
struct EngineUpdateState {
  bool on = false;      // +0x2c8
  float rpm = 0;        // +0x2d8
  float feedback = 0;   // +0x2dc, zeroed after each step (into +0x2e0)
  float field2e8 = 0;   // +0x2e8: angle.x / maxRotation.x, purpose not established
  float field2ec = 0;   // +0x2ec, zeroed with the engine off
  float heat = 0;       // +0x2cc
  bool reverse = false; // +0x2f0: running against the rpm's sign
  float load = 0;       // +0x2f8
  int gear = 1;         // +0x2fc (EngineBase constructor 0x63098d)
};

// One handleUpdate step of `dt` seconds for an engine type without bit 0x20.
// `angle` is the bundle's axis angles after calculateAndClipAngle, `input`
// its roll input (+0x260, `running`), `speed` the root object's |velocity|
// (the tank branch). `ghost`: flag bit 2 of the call (no load decay, no
// updateEngineSound). Returns false for type bit 0x20, which is not ported.
bool engineHandleUpdate(EngineUpdateState& state, const EngineUpdateSettings& settings, Vec3f angle,
                        float input, float speed, float dt, bool ghost = false);

}  // namespace obf2::game
