// The round's score (obf2/net/score_state.h): `ScoreManager::setNetUpdate`'s
// record — the mask, each field a sign and a magnitude, in the writer's order —
// and the client's copy that a record changes only where its mask says.
#include <cstddef>
#include <vector>

#include "check.h"
#include "obf2/net/score_state.h"

using namespace obf2::net::bf2;
using obf2::net::BitReader;
using obf2::net::BitWriter;

namespace {

void put(BitWriter& writer, int value, unsigned bits) {
  writer.writeBits(value < 0 ? 1u : 0u, 1);
  writer.writeBits(static_cast<std::uint32_t>(value < 0 ? -value : value), bits - 1);
}

// Team 1's tickets with the bleed, team 2's block[7]/[14] pair, team 2's rounds
// won, and the round's result with both block[13].
void testRecordIsReadInWritersOrder() {
  std::vector<std::byte> buffer(64);
  BitWriter writer(buffer);
  const std::uint32_t mask = 0x400u | 0x10000u | 0x800000u | 0x40000000u | 0x80000000u;
  writer.writeBits(mask, 32);
  put(writer, 287, 18);   // team 1 tickets
  writer.writeBits(1, 1);  // bleeding
  put(writer, -3, 18);    // team 1 ticket state
  put(writer, 12, 18);    // team 2 block[7]
  put(writer, 34, 18);    // team 2 block[14]
  put(writer, 2, 18);     // team 2 rounds won
  put(writer, 2, 3);      // winner
  put(writer, 3, 3);      // victory type
  put(writer, 5, 18);     // team 1 block[13]
  put(writer, 6, 18);     // team 2 block[13]
  const std::size_t bits = writer.bitPosition();

  BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto state = readScoreState(reader);
  CHECK(state && state->complete);
  CHECK_EQ(reader.bitPosition(), bits);
  CHECK(state->teams[0].tickets == 287);
  CHECK(state->teams[0].ticketBleed == true);
  CHECK(state->teams[0].ticketState == -3);
  CHECK(state->teams[1].block[7] == 12);
  CHECK(state->teams[1].block[14] == 34);
  CHECK(state->teams[1].roundsWon == 2);
  CHECK(!state->teams[1].tickets);
  CHECK(state->winner == 2);
  CHECK(state->victoryType == 3);
  CHECK(state->teams[0].block[13] == 5);
  CHECK(state->teams[1].block[13] == 6);

  Score score;
  score.apply(*state);
  CHECK(score.known);
  CHECK_EQ(score.teams[0].tickets, 287);
  // A later record naming only team 2's tickets leaves team 1's alone.
  ScoreState later;
  later.teams[1].tickets = 150;
  score.apply(later);
  CHECK_EQ(score.teams[0].tickets, 287);
  CHECK_EQ(score.teams[1].tickets, 150);
}

// A record cut short is not complete.
void testCutRecordIsIncomplete() {
  std::vector<std::byte> buffer(8);
  BitWriter writer(buffer);
  writer.writeBits(0x400u, 32);
  put(writer, 100, 18);
  BitReader reader(std::span<const std::byte>(buffer.data(), writer.byteSize()));
  const auto state = readScoreState(reader);
  CHECK(state && !state->complete);
}

}  // namespace

TEST_MAIN({
  testRecordIsReadInWritersOrder();
  testCutRecordIsIncomplete();
})
