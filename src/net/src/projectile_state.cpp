#include "obf2/net/projectile_state.h"

#include "obf2/net/soldier_state.h"  // rangedBits — `0x4f9a10(0, max)`

namespace obf2::net::bf2 {

std::optional<ProjectileState> readProjectileState(BitReader& reader, const Vec3f& reference,
                                                   const std::optional<ProjectileArmor>& armor,
                                                   bool armorKnown) {
  ProjectileState out;
  const auto mask = reader.readBits(kProjectileMaskBits);
  if (!mask) return std::nullopt;
  out.mask = *mask;

  // 0x636ac0 reads position before rotation, whatever the bits' order.
  if (out.mask & kProjectilePosition) {
    out.position = reader.readCompressedVector(reference, kProjectilePrecision);
    if (!out.position) return out;
  }
  if (out.mask & kProjectileRotation) {
    out.rotation = reader.readUnitQuaternion(kProjectileQuaternionBits);
    if (!out.rotation) return out;
  }
  if (out.mask & kProjectileDetonation) {
    const auto flag = reader.readBits(1);
    if (!flag) return out;
    out.detonated = *flag == 1;
    if (*out.detonated) {
      // `0x4f9c10(0x10)`: a sign bit, then 15 bits.
      const auto sign = reader.readBits(1);
      const auto magnitude = reader.readBits(15);
      if (!sign || !magnitude) return out;
      out.detonationNumber = *sign == 1 ? -static_cast<int>(*magnitude)
                                        : static_cast<int>(*magnitude);
      out.detonationVector = reader.readHighCompressedVector(Vec3f{}, kProjectilePrecision);
      if (!out.detonationVector) return out;
      const auto hasTarget = reader.readBits(1);
      if (!hasTarget) return out;
      if (*hasTarget == 1) {
        const auto target = reader.readBits(16);
        if (!target) return out;
        out.target = static_cast<std::uint16_t>(*target);
      }
    }
  }
  if (out.mask & kProjectileHitPoints) {
    if (!armorKnown) return out;
    if (armor) {
      // The lower bound is maxWreck − maxWreck, 0; the value comes back less maxWreck.
      const int upper = armor->maxHitPoints + armor->maxWreckHitPoints;
      const auto raw = reader.readBits(rangedBits(static_cast<std::uint32_t>(upper)));
      if (!raw) return out;
      out.hitPoints = static_cast<int>(*raw) - armor->maxWreckHitPoints;
    }
  }
  out.complete = true;
  return out;
}

}  // namespace obf2::net::bf2
