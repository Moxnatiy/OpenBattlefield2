#include "obf2/net/soldier_state.h"

#include <cmath>
#include <cstring>

namespace obf2::net::bf2 {
namespace {

// `(v * 2 / (2^n - 1) - 1) * limit`, with a value within one step of zero set
// to zero — the pattern every expanded field of 0x62d4e0 repeats.
float expand(std::uint32_t packed, unsigned bits, float limit) {
  const float steps = static_cast<float>((1u << bits) - 1u);
  float unit = static_cast<float>(packed) * (2.0f / steps) - 1.0f;
  if (std::abs(unit) < 2.0f / steps) unit = 0.0f;
  return unit * limit;
}

float rawFloat(std::uint32_t bits) {
  float out = 0.0f;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

}  // namespace

unsigned rangedBits(std::uint32_t max) {
  // `do { n++ } while ((1 << n) - 1 < range + 1)` — FUN_004f9a10.
  unsigned n = 0;
  do {
    ++n;
  } while (((1ull << n) - 1ull) < static_cast<std::uint64_t>(max) + 1ull);
  return n;
}

std::optional<SoldierState> readSoldierState(BitReader& reader, const Vec3f& reference,
                                             SoldierLayout layout, int weaponSlots) {
  const bool controlled = layout == SoldierLayout::Controlled;
  SoldierState out;
  const auto mask = reader.readBits(kSoldierMaskBits);
  if (!mask) return std::nullopt;
  out.mask = *mask;
  const auto has = [&](std::uint32_t bit) { return (out.mask & bit) != 0; };
  const auto bits = [&](unsigned n) { return reader.readBits(n); };

  if (has(0x40)) {
    const auto a = bits(8);
    const auto b = bits(1);
    if (!a || !b) return out;
    out.value40 = *a;
    out.flag40 = *b != 0;
  }
  if (has(0x20)) {
    const auto a = bits(rangedBits(4));
    const auto b = bits(rangedBits(4));
    if (!a || !b) return out;
    out.pairA20 = *a;
    out.pairB20 = *b;
  }
  if (has(0x8000)) {
    out.ragdoll = true;
    return out;
  }

  const auto vector = [&](std::uint32_t bit, const Vec3f& base, float precision,
                          std::optional<Vec3f>& into) {
    if (!has(bit)) return true;
    const auto v = reader.readCompressedVector(base, precision);
    if (!v) return false;
    into = *v;
    return true;
  };
  if (!vector(0x1, reference, 0.001f, out.position)) return out;
  if (controlled) {
    if (!vector(0x80, Vec3f{}, 0.001f, out.velocity)) return out;
    if (!vector(0x100, Vec3f{}, 0.0001f, out.vector100)) return out;
    if (!vector(0x200, Vec3f{}, 0.0001f, out.vector200)) return out;
    if (!vector(0x40000, Vec3f{}, 0.001f, out.vector40000)) return out;
  } else {
    if (!vector(0x80, Vec3f{}, 0.01f, out.velocity)) return out;
  }

  const auto angle = [&](std::uint32_t bit, float limit, std::optional<float>& into) {
    if (!has(bit)) return true;
    const auto v = bits(controlled ? 32 : 12);
    if (!v) return false;
    into = controlled ? rawFloat(*v) : expand(*v, 12, limit);
    return true;
  };
  if (!angle(0x2, 360.0f, out.bodyYaw) || !angle(0x4, 90.0f, out.aimYaw) ||
      !angle(0x8, 180.0f, out.angle8) || !angle(0x10, 90.0f, out.pitch)) {
    return out;
  }

  {
    const auto a = bits(rangedBits(3));
    const auto b = bits(rangedBits(1));
    const auto c = bits(1);
    const auto d = bits(1);
    const auto e = bits(1);
    if (!a || !b || !c || !d || !e) return out;
    out.value0to3 = *a;
    out.value0to1 = *b;
    out.bitA = *c != 0;
    out.bitB = *d != 0;
    out.bitC = *e != 0;
  }

  const auto spread = [&](std::uint32_t bit, std::optional<float>& into) {
    if (!has(bit)) return true;
    const unsigned width = controlled ? 16 : 10;
    const auto v = bits(width);
    if (!v) return false;
    into = expand(*v, width, 50.0f);
    return true;
  };
  if (!spread(0x400, out.value400) || !spread(0x800, out.value800)) return out;

  {
    const auto d = bits(1);
    if (!d) return out;
    out.bitD = *d != 0;
  }

  if (has(0x4000)) {
    const unsigned width = controlled ? 7 : 5;
    const auto v = bits(width);
    if (!v) return out;
    out.value4000 = static_cast<float>(*v) / static_cast<float>((1u << width) - 1u);
    if (controlled) {
      const auto flag = bits(1);
      if (!flag) return out;
      out.flag4000 = *flag != 0;
    }
  }

  if (has(0x1000)) {
    if (weaponSlots < 0) return out;  // the width is the soldier's own weapon count
    const auto v = bits(rangedBits(static_cast<std::uint32_t>(weaponSlots) + 1u));
    if (!v) return out;
    out.weaponIndex = static_cast<int>(*v) - 1;
  }
  if (has(0x2000)) {
    const auto v = bits(2);
    if (!v) return out;
    out.value2000 = *v;
  }
  if (controlled && has(0x20000)) {
    const auto v = bits(7);
    if (!v) return out;
    out.value20000 = expand(*v, 7, 1.0f);
  }
  if (has(0x10000)) {
    const auto id = bits(rangedBits(0xff));
    if (!id) return out;
    out.id10000 = *id;
    if (*id != 0) {
      const auto f = bits(12);
      if (!f) return out;
      out.fraction10000 = static_cast<float>(*f) / 4095.0f;
    }
  }
  if (has(0x100000)) {
    const auto id = bits(16);
    if (!id) return out;
    out.id100000 = *id;
    if (*id != 0) {
      const auto f = bits(12);
      if (!f) return out;
      out.fraction100000 = static_cast<float>(*f) / 4095.0f;
    }
  }
  if (has(0x80000)) {
    const auto v = bits(rangedBits(0xf));
    if (!v) return out;
    out.value80000 = *v;
  }
  out.complete = true;
  return out;
}

}  // namespace obf2::net::bf2
