#pragma once
// A control point's network state — `ControlPointNetworkable::setNetUpdate`
// (OpenBF2 Game/Objects/Networkables/ControlPointNetworkable.cpp), read whole.
//
// A 7-bit mask, then per bit:
//
//   0x01  the team               sign + 3 bits          `setTeam`
//   0x02  "neutral" (+0x158)     1 bit                  written raw
//   0x04  the take-over counter  16 bits, then the change per second, 16 bits;
//         each a unit over -1.25..1.25: q / 65535, doubled minus one, a dead
//         zone of 3.0518044e-05, times 1.25          `setTakeOverChangePerSecond`
//   0x08  capture allowed, team 1   sign (always 0) + 3 bits
//   0x10  capture allowed, team 2   the same
//   0x20  the flag's team         sign + 3 bits          `setFlag`
//   0x40  the capturing state     sign + 3 bits          `setCapturingState`
//
// A full record is 60 bits, the take-over alone 39 — both measured on the live
// server's control points.
#include <cstdint>
#include <optional>

#include "obf2/net/bitstream.h"

namespace obf2::net::bf2 {

inline constexpr unsigned kControlPointMaskBits = 7;

struct ControlPointState {
  std::uint32_t mask = 0;
  bool complete = false;  // read through to the last field the mask names
  std::optional<int> team;
  std::optional<bool> neutral;
  std::optional<float> takeOverCounter;
  std::optional<float> takeOverPerSecond;
  std::optional<bool> captureByTeam1;
  std::optional<bool> captureByTeam2;
  std::optional<int> flagTeam;
  std::optional<int> capturingState;  // purpose of the values not established
};

std::optional<ControlPointState> readControlPointState(BitReader& reader);

}  // namespace obf2::net::bf2
