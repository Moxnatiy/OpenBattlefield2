#include "obf2/hud/blinker.h"

namespace obf2::hud {

void FloatBlinker::update(float dt, bool gate, float& output) {
  // `HudFloatBlinker::update` (OpenBF2 HudBlinker.cpp). The elapsed time is
  // rounded to float before it is compared.
  const float elapsed = dt + elapsed_;
  elapsed_ = elapsed;
  if (gate != gateValue_) {
    // A gate let go finishes the stroke, discrete mode included, and stops at 1.
    if (!(output < 1.0f)) return;
  } else {
    if (elapsed > period_ && !smooth_) {
      // The original's gated discrete arm also tests its source pointer, which
      // this constructor leaves null — a crash nobody reaches, since every
      // caller is smooth. Here it is the plain toggle.
      output = output > 0.0f ? 0.0f : 1.0f;
      elapsed_ = 0.0f;
      return;
    }
    if (!smooth_ || !(period_ > 0.0f)) return;
  }
  if (elapsed >= period_) {
    elapsed_ = 0.0f;
    direction_ = -direction_;
  }
  float value = elapsed_;
  if (!(direction_ > 0.0f)) value = value * direction_ + period_;
  output = value / period_;
}

}  // namespace obf2::hud
