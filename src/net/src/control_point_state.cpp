#include "obf2/net/control_point_state.h"

#include <cmath>

namespace obf2::net::bf2 {

namespace {

// `readSigned3` (OpenBF2): a sign bit, then 3 bits of magnitude.
std::optional<int> signed3(BitReader& reader) {
  const auto sign = reader.readBits(1);
  const auto magnitude = reader.readBits(3);
  if (!sign || !magnitude) return std::nullopt;
  const int value = static_cast<int>(*magnitude);
  return *sign == 1 ? -value : value;
}

// `readUnit16` (OpenBF2): q / 65535 as a float, doubled minus one, a dead zone at
// 3.0518044e-05, times 1.25.
std::optional<float> unit16(BitReader& reader) {
  const auto q = reader.readBits(16);
  if (!q) return std::nullopt;
  const float t = static_cast<float>(*q / 65535.0);
  double f = static_cast<double>(t) + static_cast<double>(t) - 1.0;
  if (!(std::fabs(f) > 3.0518044e-05)) f = 0.0;
  return static_cast<float>(f * 1.25);
}

}  // namespace

std::optional<ControlPointState> readControlPointState(BitReader& reader) {
  ControlPointState state;
  const auto mask = reader.readBits(kControlPointMaskBits);
  if (!mask) return std::nullopt;
  state.mask = *mask;
  if (state.mask & 0x01) {
    if (!(state.team = signed3(reader))) return state;
  }
  if (state.mask & 0x02) {
    const auto bit = reader.readBits(1);
    if (!bit) return state;
    state.neutral = *bit == 1;
  }
  if (state.mask & 0x04) {
    state.takeOverCounter = unit16(reader);
    state.takeOverPerSecond = unit16(reader);
    if (!state.takeOverCounter || !state.takeOverPerSecond) return state;
  }
  if (state.mask & 0x08) {
    const auto value = signed3(reader);
    if (!value) return state;
    state.captureByTeam1 = *value != 0;
  }
  if (state.mask & 0x10) {
    const auto value = signed3(reader);
    if (!value) return state;
    state.captureByTeam2 = *value != 0;
  }
  if (state.mask & 0x20) {
    if (!(state.flagTeam = signed3(reader))) return state;
  }
  if (state.mask & 0x40) {
    if (!(state.capturingState = signed3(reader))) return state;
  }
  state.complete = true;
  return state;
}

}  // namespace obf2::net::bf2
