#pragma once
// A soldier's network state — `SoldierNetworkable::setNetUpdate` (`BF2.exe`,
// 0x62d4e0), read whole.
//
// The same function reads two layouts, chosen by its last argument:
//
//   * **Ghost** — a record in the ghost stream: another player's soldier. The
//     angles travel in twelve bits, the velocity at 0.01;
//   * **Controlled** — our own soldier inside the controlled-object state. The
//     reader of that state, `GhostManager::readControlObjectState` (0x5b9860),
//     passes 3 through `FUN_005b81c0`/`FUN_005b8550` to every networkable of the
//     object, and 3 is the branch with the precise layout: the angles as raw
//     32-bit floats, three more vectors, sixteen-bit ranges.
//
// The fields in read order, both layouts:
//
//   mask                                  21 bits
//   0x40      8 bits, 1 bit               the first is scaled by 1/255 and a maximum (health, by the look of the apply code — not established)
//   0x20      two values 0..4, 3 bits each
//   0x8000    another branch altogether (`FUN_007e5440` / `FUN_007ea290`) — the rest of the layout is skipped
//   0x1       the position: a compressed vector from the stream's reference, 0.001
//   Controlled:  0x80 vector 0.001, 0x100 vector 0.0001, 0x200 vector 0.0001, 0x40000 vector 0.001 (all from zero)
//   Ghost:       0x80 vector 0.01 from zero — the velocity
//   0x2, 0x4, 0x8, 0x10
//             Controlled: 32 bits each, the float itself
//             Ghost: 12 bits each, expanded into ±360, ±90, ±180, ±90
//   always    a value 0..3 (3 bits), a value 0..1 (2 bits), three single bits
//   0x400, 0x800
//             Controlled: 16 bits each, Ghost: 10 bits each — expanded into ±50
//   always    one bit
//   0x4000    Controlled: 7 bits over 127, then one bit; Ghost: 5 bits over 31
//   0x1000    a value 0..(weapon slots + 1), stored minus one — the weapon's index
//   0x2000    2 bits
//   0x20000   Controlled only: 7 bits expanded into ±1
//   0x10000   a value 0..255 (9 bits); when not zero, 12 bits over 4095
//   0x100000  16 bits; when not zero, 12 bits over 4095
//   0x80000   a value 0..15 (5 bits)
//
// The ranged reads are `FUN_004f9a10(min, max)`, which takes the smallest n
// with 2^n - 1 >= max - min + 1 — one bit more than the range strictly needs, so
// 0..3 is three bits and 0..255 is nine. The expanded ones are
// `(v * 2 / (2^n - 1) - 1) * limit`, with anything within one step of zero set
// to zero.
//
// Only one field's width is not in the layout: 0x1000's range is the soldier's
// weapon count, asked of its inventory (`*(object+0x14)+0x22c` → `+0x10`). A
// reader that does not know it stops before that field and says so.
#include <cstdint>
#include <optional>

#include "obf2/core/math.h"
#include "obf2/net/bitstream.h"

namespace obf2::net::bf2 {

enum class SoldierLayout {
  Ghost,       // `setNetUpdate` with any type but 3
  Controlled,  // type 3, from the controlled-object state
};

inline constexpr unsigned kSoldierMaskBits = 21;

struct SoldierState {
  std::uint32_t mask = 0;
  // Read through to the last field. False means the reader stopped: the ragdoll
  // branch, a weapon index whose width is not known, or the buffer ran out.
  bool complete = false;
  bool ragdoll = false;  // 0x8000

  std::optional<std::uint32_t> value40;  // 0x40: 8 bits — purpose not established
  std::optional<bool> flag40;            // 0x40: 1 bit — purpose not established
  std::optional<std::uint32_t> pairA20;  // 0x20: 0..4 — purpose not established
  std::optional<std::uint32_t> pairB20;  // 0x20: 0..4

  std::optional<Vec3f> position;   // 0x1
  std::optional<Vec3f> velocity;   // 0x80
  std::optional<Vec3f> vector100;  // 0x100, Controlled only — purpose not established
  std::optional<Vec3f> vector200;  // 0x200, Controlled only — purpose not established
  std::optional<Vec3f> vector40000;  // 0x40000, Controlled only — purpose not established

  // The angles, degrees. Where the apply block of 0x62d4e0 writes them, and what
  // `FUN_005a8630` (the soldier's look) makes of those fields:
  //   0x2  -> soldier +0x224  the body's yaw
  //   0x4  -> soldier +0x240  the aim's yaw offset from the body; the look adds the
  //                           mouse's own offset (+0x244, `0x5a99a0`) and clamps
  //   0x8  -> soldier +0x248  purpose not established
  //   0x10 -> soldier +0x238  the pitch; the look adds the mouse's (+0x23c) and
  //                           clamps. Positive is down: mouse down turns it up.
  // So the direction the soldier looks is `bodyYaw + aimYaw` — measured on a live
  // server, the sum follows a turn while the body lags behind it and catches up.
  std::optional<float> bodyYaw;  // 0x2
  std::optional<float> aimYaw;   // 0x4
  std::optional<float> angle8;   // 0x8 — purpose not established (±180)
  std::optional<float> pitch;    // 0x10

  std::uint32_t value0to3 = 0;  // always — purpose not established
  std::uint32_t value0to1 = 0;  // always — purpose not established
  bool bitA = false, bitB = false, bitC = false;  // always — purpose not established

  std::optional<float> value400;  // 0x400, ±50 — purpose not established
  std::optional<float> value800;  // 0x800, ±50 — purpose not established
  bool bitD = false;              // always — purpose not established

  // 0x4000: the sprint's stamina and, Controlled only, whether it sprints
  // (`SprintState` +0x10 and +0x16, soldier_sprint.h). Measured on the live server:
  // the flag rises with shift and forward held and falls when either is let go; the
  // value drains by 1/300 a tick while it is up, as a light kit's
  // `SprintDissipationTime 10` gives.
  std::optional<float> value4000;
  std::optional<bool> flag4000;

  std::optional<int> weaponIndex;  // 0x1000
  std::optional<std::uint32_t> value2000;   // 0x2000: 2 bits
  std::optional<float> value20000;          // 0x20000, Controlled only, ±1
  std::optional<std::uint32_t> id10000;     // 0x10000: 0..255
  std::optional<float> fraction10000;       // when id10000 is not zero
  std::optional<std::uint32_t> id100000;    // 0x100000: 16 bits
  std::optional<float> fraction100000;      // when id100000 is not zero
  std::optional<std::uint32_t> value80000;  // 0x80000: 0..15
};

// The width of a `FUN_004f9a10(0, max)` read.
unsigned rangedBits(std::uint32_t max);

// `reference` is the stream's compression vector — the base of the position.
// `weaponSlots` is the soldier's weapon count; negative when not known.
std::optional<SoldierState> readSoldierState(BitReader& reader, const Vec3f& reference,
                                             SoldierLayout layout, int weaponSlots = -1);

}  // namespace obf2::net::bf2
