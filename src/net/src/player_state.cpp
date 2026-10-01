#include "obf2/net/player_state.h"

#include <cmath>

#include "obf2/net/soldier_state.h"  // rangedBits

namespace obf2::net::bf2 {
namespace {

// `do n++ while ((1 << n) - 1 < limit)` — the inline form of the ranged read.
unsigned widthFor(std::uint32_t limit) {
  unsigned n = 0;
  do {
    ++n;
  } while (((1ull << n) - 1ull) < limit);
  return n;
}

}  // namespace

std::optional<PlayerState> readPlayerState(BitReader& reader) {
  PlayerState out;
  const auto mask = reader.readBits(kPlayerMaskBits);
  if (!mask) return std::nullopt;
  out.mask = *mask;
  const auto has = [&](std::uint32_t bit) { return (out.mask & bit) != 0; };
  // Every read below either succeeds or leaves `complete` false.
  bool ok = true;
  const auto bits = [&](unsigned n) -> std::uint32_t {
    const auto v = reader.readBits(n);
    if (!v) ok = false;
    return v.value_or(0);
  };
  const auto flag = [&]() { return bits(1) == 1; };
  // `0x4f9a10(0, max)`.
  const auto ranged = [&](std::uint32_t max) { return bits(rangedBits(max)); };
  // `0x4f9c10(n)`: a sign bit, then n - 1 bits.
  const auto signedBits = [&](unsigned n) {
    const bool negative = flag();
    const int magnitude = static_cast<int>(bits(n - 1));
    return negative ? -magnitude : magnitude;
  };

  if (has(0x1)) out.alive = flag();
  if (has(0x2)) out.spawnGroup = ranged(0xff);
  if (has(0x8)) {
    PlayerState::Score score;
    score.score = signedBits(12);
    score.deaths = static_cast<int>(ranged(0x7ff));
    score.kills = static_cast<int>(ranged(0x7ff));
    score.teamwork = signedBits(12);
    score.teamKills = static_cast<int>(ranged(0x7ff));
    out.score = score;
  }
  if (has(0x200000)) out.flagHolder = flag();
  if (has(0x10)) out.flag10 = flag();
  // 0x80000 and 0x100000 read nothing: the client stamps the tick.
  if (has(0x2000000)) out.counter2000000 = bits(2);

  // The three firing states (the table { 0x40, 0x80, 0x20000 } at 0x500790).
  constexpr std::uint32_t kFiring[3] = {0x40, 0x80, 0x20000};
  for (std::size_t i = 0; i < 3 && ok; ++i) {
    const bool all = has(0x10000);
    if (!has(kFiring[i]) && !all) continue;
    PlayerFiring firing;
    firing.on = flag();
    if (firing.on || all) {
      firing.detailed = true;
      firing.value7 = bits(widthFor(0x40)) - 1u;
      firing.value2 = bits(2);
      firing.flag = flag();
      if (flag()) {
        firing.fraction = static_cast<float>(bits(8)) * (1.0f / 255.0f) * 10.0f;
      } else {
        firing.fraction = static_cast<float>(bits(6)) * (1.0f / 63.0f);
      }
      firing.value4 = bits(4);
    }
    out.firing[i] = firing;
  }
  if (has(0x40000)) {
    out.sprint = flag();
    out.sprintLastTick = flag();
  }
  if (has(0x100)) out.ping = bits(widthFor(0x400));
  if (has(0x20)) {
    out.spawnAtTick = bits(32);
    out.manDown = flag();
  }
  if (has(0x200)) out.team = static_cast<int>(bits(widthFor(4))) - 1;
  if (has(0x400)) {
    out.camera400 = bits(widthFor(9));
    out.flag400 = flag();
  }
  if (has(0x800)) out.value800 = bits(widthFor(0x100));
  if (has(0x1000)) {
    const std::uint32_t packed = bits(widthFor(0x100));
    out.kit = static_cast<int>(packed & 0xf);
    out.unlockLevel = static_cast<int>(packed >> 4);
  }
  if (has(0x2000)) out.squad = bits(widthFor(0x10));
  if (has(0x4000)) {
    out.rank = static_cast<int>(bits(widthFor(0x20))) - 1;
    std::array<std::uint32_t, 3> places{};
    for (std::uint32_t& place : places) place = bits(widthFor(0x100));
    out.places = places;
  }
  if (has(0x8000)) out.freeCamera = flag();
  if (has(0x400000)) out.overheadCamera = flag();
  if (has(0x800000)) out.value800000 = bits(8);
  if (has(0x1000000)) out.value1000000 = static_cast<int>(bits(widthFor(0x10001))) - 1;
  out.alwaysA = flag();
  if (has(0x4000000)) {
    out.commander = flag();
    if (*out.commander) {
      for (bool& b : out.commanderFlags) b = flag();
      for (float& f : out.commanderFractions) {
        f = static_cast<float>(bits(8)) * (1.0f / 255.0f);
        if (std::abs(f) < 0.001953125f) f = 0.0f;  // 0x501075..: under 1/512 is zero
      }
    }
  }
  out.alwaysB = flag();
  if (has(0x8000000)) out.maskSetting = bits(widthFor(4));
  out.complete = ok;
  return out;
}

}  // namespace obf2::net::bf2
