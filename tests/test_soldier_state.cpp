// A soldier's network state, `SoldierNetworkable::setNetUpdate` (`BF2.exe`,
// 0x62d4e0), both layouts.
//
// Two kinds of check. A written stream locks the field order: every field the
// layout has, written in the order the function reads it, has to come back. And
// a live capture checks the controlled layout against the game itself —
// `tests/data/bf2-karkand-lives.bin`, taken on the original server on Strike at
// Karkand: spawn, suicide, respawn (docs/functions/network-events.md).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "check.h"
#include "obf2/net/bf2_events.h"
#include "obf2/net/soldier_state.h"

using namespace obf2;
using namespace obf2::net;
using namespace obf2::net::bf2;

namespace {

std::uint32_t floatBits(float value) {
  std::uint32_t out = 0;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

}  // namespace

// `FUN_004f9a10(0, max)` takes one bit more than the range strictly needs.
static void testRangedBits() {
  CHECK_EQ(rangedBits(1), 2u);
  CHECK_EQ(rangedBits(3), 3u);
  CHECK_EQ(rangedBits(4), 3u);
  CHECK_EQ(rangedBits(0xf), 5u);
  CHECK_EQ(rangedBits(0xff), 9u);
}

// Every field of the controlled layout, written in the function's own order.
static void testControlledOrder() {
  std::vector<std::byte> buffer(256);
  BitWriter w(buffer);
  const std::uint32_t mask = 0x1 | 0x2 | 0x20 | 0x40 | 0x80 | 0x400 | 0x1000 | 0x2000 |
                             0x4000 | 0x10000 | 0x20000 | 0x80000 | 0x100000;
  const Vec3f reference{100.0f, 150.0f, -200.0f};
  w.writeBits(mask, 21);
  w.writeBits(200, 8);  // 0x40
  w.writeBits(1, 1);
  w.writeBits(2, 3);  // 0x20
  w.writeBits(4, 3);
  w.writeCompressedVector(Vec3f{101.5f, 150.25f, -199.0f}, reference, 0.001f);  // 0x1
  w.writeCompressedVector(Vec3f{1.0f, 0.0f, -2.0f}, Vec3f{}, 0.001f);           // 0x80
  w.writeBits(floatBits(123.5f), 32);                                            // 0x2
  w.writeBits(3, 3);  // always 0..3
  w.writeBits(1, 2);  // always 0..1
  w.writeBits(1, 1);
  w.writeBits(0, 1);
  w.writeBits(1, 1);
  w.writeBits(65535, 16);  // 0x400 -> +50
  w.writeBits(1, 1);       // always
  w.writeBits(127, 7);     // 0x4000 -> 1.0
  w.writeBits(1, 1);       //   and its bit
  w.writeBits(3, rangedBits(4 + 1));  // 0x1000, four weapon slots -> index 2
  w.writeBits(2, 2);                  // 0x2000
  w.writeBits(127, 7);                // 0x20000 -> +1
  w.writeBits(7, 9);                  // 0x10000
  w.writeBits(4095, 12);              //   fraction 1.0
  w.writeBits(0, 16);                 // 0x100000, zero: no fraction
  w.writeBits(9, 5);                  // 0x80000
  CHECK(w.ok());

  BitReader r(buffer);
  const auto state = readSoldierState(r, reference, SoldierLayout::Controlled, 4);
  CHECK(state.has_value());
  if (!state) return;
  CHECK(state->complete);
  CHECK_EQ(state->mask, mask);
  CHECK_EQ(*state->value40, 200u);
  CHECK_EQ(*state->pairB20, 4u);
  CHECK(state->position && std::abs(state->position->y - 150.25f) < 0.002f);
  CHECK(state->velocity && std::abs(state->velocity->z + 2.0f) < 0.002f);
  CHECK(state->bodyYaw && std::abs(*state->bodyYaw - 123.5f) < 0.0001f);
  CHECK_EQ(state->value0to3, 3u);
  CHECK(state->bitA && !state->bitB && state->bitC);
  CHECK(state->value400 && std::abs(*state->value400 - 50.0f) < 0.01f);
  CHECK(state->bitD);
  CHECK(state->value4000 && std::abs(*state->value4000 - 1.0f) < 0.001f);
  CHECK(state->flag4000 && *state->flag4000);
  CHECK(state->weaponIndex && *state->weaponIndex == 2);
  CHECK_EQ(*state->value2000, 2u);
  CHECK(state->value20000 && std::abs(*state->value20000 - 1.0f) < 0.001f);
  CHECK_EQ(*state->id10000, 7u);
  CHECK(state->fraction10000 && std::abs(*state->fraction10000 - 1.0f) < 0.001f);
  CHECK_EQ(*state->id100000, 0u);
  CHECK(!state->fraction100000.has_value());
  CHECK_EQ(*state->value80000, 9u);
  CHECK_EQ(r.bitPosition(), w.bitPosition());
}

// The ghost layout: twelve-bit angles and a coarser velocity.
static void testGhostAngles() {
  std::vector<std::byte> buffer(64);
  BitWriter w(buffer);
  w.writeBits(0x1 | 0x2, 21);
  w.writeCompressedVector(Vec3f{10.0f, 20.0f, 30.0f}, Vec3f{}, 0.001f);
  w.writeBits(4095, 12);  // +360
  for (unsigned i = 0; i < 3 + 2 + 3 + 1; ++i) w.writeBits(0, 1);
  BitReader r(buffer);
  const auto state = readSoldierState(r, Vec3f{}, SoldierLayout::Ghost);
  CHECK(state && state->complete);
  if (!state) return;
  CHECK(state->bodyYaw && std::abs(*state->bodyYaw - 360.0f) < 0.01f);
  CHECK(state->position && std::abs(state->position->z - 30.0f) < 0.002f);
}

// Our own soldier on the original server. The player never touched the keys, so
// in every packet of a life its state has to put the soldier on the spot the
// `CreateObjectEvent` of that life named — on x and z, which gravity does not
// move. Height is left out on purpose: whether the server's height is the feet
// or something above them is not established.
static void testLiveControlledSoldier() {
  const auto packets = loadCapture(std::string(OBF2_TEST_DATA) + "/bf2-karkand-lives.bin");
  CHECK(!packets.empty());
  std::unordered_map<std::uint16_t, Vec3f> createdAt;
  std::uint16_t soldier = 0;
  std::optional<Vec3f> created;
  int soldierStates = 0, onSpot = 0;
  for (const auto& packet : packets) {
    for (const auto& event : readEvents(packet)) {
      if (event.object && event.object->position) {
        createdAt[event.object->networkId] = *event.object->position;
      }
      if (event.enter) {
        soldier = event.enter->object;
        const auto it = createdAt.find(soldier);
        created = it == createdAt.end() ? std::nullopt : std::optional<Vec3f>(it->second);
      }
    }
    const auto state = readControlObjectState(packet);
    if (!state || soldier == 0 || state->networkId != soldier || !state->soldier || !created) {
      continue;
    }
    ++soldierStates;
    const auto& at = state->soldier->position;
    if (at && std::abs(at->x - created->x) < 0.05f && std::abs(at->z - created->z) < 0.05f) {
      ++onSpot;
    }
  }
  std::printf("  our soldier's states: %d, on its creation spot: %d\n", soldierStates, onSpot);
  CHECK(soldierStates > 30);
  CHECK_EQ(onSpot, soldierStates);
}

TEST_MAIN({
  testRangedBits();
  testControlledOrder();
  testGhostAngles();
  testLiveControlledSoldier();
})
