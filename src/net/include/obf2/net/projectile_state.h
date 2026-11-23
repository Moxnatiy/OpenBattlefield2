#pragma once
// A projectile's ghost record — `GenericProjectileNetworkable::setNetUpdate`,
// `BF2.exe` 0x636ac0 (its "error READING data" check names
// `Game\Objects\Networkables\GenericProjectileNetworkable.cpp`, line 0x109, at
// 0x636dda). docs/functions/projectiles.md.
//
// A 5-bit mask (`0x6b6470(&mask, 5)`), then, in this order:
//
//   0x2   position: a compressed vector against the stream's compression
//         vector (`0x6b69f0(stream + 0x54, 0.01)`) → record +0x08
//   0x1   rotation: a unit quaternion of four 12-bit values (`0x6b7270(12)`)
//         → record +0x14
//   0x4   detonated: 1 bit (→ +0x24); when it is 1:
//           a signed number, sign plus 15 bits (`0x4f9c10(16)`) → +0x28,
//             written into the projectile's +0x1e4 on detonation;
//           a high-compressed vector against zero at 0.01 (`0x6b6c50`, the
//             zero is 0x6d8f10's static) → +0x2c, into the projectile's
//             +0x1ec..+0x1f4;
//           has a target: 1 bit (→ +0x48); when 1, the target's network id,
//             16 bits (→ +0x4c), looked up with 0x6a6c10;
//   0x8   nothing — no branch reads it;
//   0x10  hit points, only when the object has an armor component (+0x3c):
//         `0x4f9a10(0, maxWreck + max)` minus maxWreck → +0x44, then the
//         component's +0x38 setter. The range is the template's — see
//         ProjectileArmor.
//
// The detonation's two fields are named by OpenBF2's sender
// (`GenericProjectileNetworkable::getNetUpdate`): the number is the projectile's
// +0x1c4, the vector its +0x1cc. Their purpose in the engine is **not
// established** here; the projectile's `detonate(true)` (its +0x154 interface,
// slot 0x14) is what the client calls after them.
#include <cstdint>
#include <optional>

#include "obf2/net/bitstream.h"

namespace obf2::net::bf2 {

inline constexpr unsigned kProjectileMaskBits = 5;  // 0x636ac0: `0x6b6470(&mask, 5)`
inline constexpr std::uint32_t kProjectileRotation = 0x1;
inline constexpr std::uint32_t kProjectilePosition = 0x2;
inline constexpr std::uint32_t kProjectileDetonation = 0x4;
inline constexpr std::uint32_t kProjectileHitPoints = 0x10;
// 0x3c23d70a, both vectors' precision (0x636ac0).
inline constexpr float kProjectilePrecision = 0.01f;
inline constexpr unsigned kProjectileQuaternionBits = 12;  // `0x6b7270(0xc)`

// The range of the 0x10 field: the armor component's +0x84 and +0x88 — the
// template's maximum and maximum wreck hit points (OpenBF2 ArmorGLComp:
// getMaxHitPoints, getMaxWreckHitPoints). Both default to 10
// (`ArmorGLCompTemplate::ArmorGLCompTemplate`, Linux 0x5b5f4c..0x5b5f5e);
// `armor.wreckHitPoints` sets the wreck and its maximum together (Linux
// `setWreckHitPoints` 0x5b8690 writes +0x164 and +0x170).
struct ProjectileArmor {
  int maxHitPoints = 10;
  int maxWreckHitPoints = 10;
};

struct ProjectileState {
  std::uint32_t mask = 0;
  std::optional<Vec3f> position;
  std::optional<Quatf> rotation;
  std::optional<bool> detonated;
  // Only with `detonated`: the projectile's +0x1c4 and +0x1cc (purpose not
  // established), and the object it hit.
  std::optional<int> detonationNumber;
  std::optional<Vec3f> detonationVector;
  std::optional<std::uint16_t> target;
  std::optional<int> hitPoints;
  // The whole mask was read. False when 0x10 is set and the caller did not know
  // whether the object has armor (`armor` empty): what came before it is good.
  bool complete = false;
};

// `armor`: the template's armor, or nothing when the template has none — then a
// 0x10 bit is skipped, as the client does (it reads the field only with the
// component). `armorKnown` false means the caller could not tell.
std::optional<ProjectileState> readProjectileState(BitReader& reader, const Vec3f& reference,
                                                   const std::optional<ProjectileArmor>& armor,
                                                   bool armorKnown = true);

}  // namespace obf2::net::bf2
