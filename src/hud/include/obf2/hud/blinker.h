#pragma once
// The HUD's float blinker — `HudFloatBlinker` (OpenBF2 Menu/HudBlinker.cpp;
// `BF2.exe` table 0x92FDCC family, built by `HudInformationLayer`'s
// registration, PE 0x46B500).
//
// It drives a float HUD variable from a bool "gate". While the gate holds its
// value, a smooth blinker runs a triangle: the output climbs from 0 to 1 over
// one period, falls back over the next, and so on (the direction flips each time
// the elapsed time reaches the period, and the excess is dropped). When the gate
// lets go, the output finishes its stroke and stops once it reaches 1. A discrete
// blinker toggles between 0 and 1 each period instead.
//
// The ticket bleed is its one use we have: `FriendlyTicketBleed` and
// `EnemyTicketBleed` are the outputs, the gates the teams' `m_ticketBleed`
// (`HudInformationLayer::updateTickets`, PE 0x4646b0, writes them into +0x88 and
// +0x90), the period 1 second, smooth.
namespace obf2::hud {

class FloatBlinker {
 public:
  FloatBlinker(bool gateValue, float period, bool smooth)
      : gateValue_(gateValue), period_(period), smooth_(smooth) {}

  // One frame: `gate` is the gate's current value, `output` the variable.
  void update(float dt, bool gate, float& output);

 private:
  bool gateValue_;
  float period_;
  bool smooth_;
  float elapsed_ = 0.0f;
  float direction_ = -1.0f;  // +0x2C, -1 at construction
};

}  // namespace obf2::hud
