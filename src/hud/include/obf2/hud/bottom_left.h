#pragma once
// The HUD's left corner region: where it travels and how its two halves — the
// health bars and the vehicle bars — fade in.
//
// This is not our invention and not one show variable but a small state machine
// in the client: `BF2.exe`, 0x78b600 (choosing the target, every frame) and
// 0x78b870 (choosing the mode). The HUD object's fields it drives are registered
// as graph variables at 0x789480 — and it is through them that the region moves.
//
//   +0x170  the mode: 0 hide, 1 health (on foot), 2 vehicle
//   +0x178  -295  the hidden position       \ all three set by
//   +0x17c  -137  the on-foot position      | the HUD's constructor
//   +0x180    54  the in-vehicle position   / 0x78c560
//   +0x184  `BottomLeft_XPos`      the current one
//   +0x188  `BottomLeft_nextXPos`  the target
//   +0x18c  `BottomLeft_alpha1` = `BottomLeftHealthAlpha`
//   +0x190  `BottomLeft_alpha2` = `BottomLeftVehicleAlpha`
//   +0x194  `BottomLeftHealthFadedAlpha`
//   +0x198  `BottomLeftVehicleFadedAlpha`
//   +0x19c  `BottomLeft_nextAlpha1`  the target for +0x18c
//   +0x1a0  `BottomLeft_nextAlpha2`  the target for +0x190
//
// Who moves what: this machine writes the **targets**, while the values are
// driven by the `Menu/Ingame` graph — `SetVariableSineAction {Speed 600}` for the
// position and `SetVariableSoftAction {Speed 10}` for the alphas. Both actions
// are a linear approach to the target (docs/functions/hud-animation.md).
#include "obf2/hud/animation.h"

namespace obf2::hud {

// The region's mode. It is set by 0x78b870: "on foot" by default, and "vehicle"
// when the player's controlled object is not their soldier.
enum class BottomLeftMode { Hidden, Health, Vehicle };

struct BottomLeftPanel {
  // What the graph drives.
  float x = kBottomLeftHiddenX;
  float healthAlpha = 0.0f;
  float vehicleAlpha = 0.0f;

  // The targets the state machine writes.
  float targetX = kBottomLeftHiddenX;
  float targetHealthAlpha = 0.0f;
  float targetVehicleAlpha = 0.0f;

  // The dimmed variants — computed by the same function at its end.
  float healthFadedAlpha = 0.0f;
  float vehicleFadedAlpha = 0.0f;

  // One step of the state machine. **It is the graph that moves the values, not
  // this** (`Menu/Ingame`, obf2/meme/graph.h): this function only reads the
  // current `x`/`healthAlpha`/`vehicleAlpha` and writes the targets, exactly as
  // 0x78b600 reads and writes the HUD object's fields. So no `dt` is needed here.
  //
  // `menuBackgroundAlpha` is the plates' alpha from the player's profile.
  void update(BottomLeftMode mode, float menuBackgroundAlpha);

  // The dimmed alphas from the current ones. Called again after the graph has
  // moved `healthAlpha`/`vehicleAlpha`.
  void recomputeFaded(float menuBackgroundAlpha);
};

}  // namespace obf2::hud
