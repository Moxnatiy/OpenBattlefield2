#include "obf2/net/score_state.h"

namespace obf2::net::bf2 {

namespace {

// `readSignedMagnitude` (OpenBF2 ScoreManager.cpp): a sign bit, then `bits - 1` of
// magnitude. 18 for the score fields, 3 for the round's result.
std::optional<int> signedMagnitude(BitReader& reader, unsigned bits) {
  const auto sign = reader.readBits(1);
  const auto magnitude = reader.readBits(bits - 1);
  if (!sign || !magnitude) return std::nullopt;
  const int value = static_cast<int>(*magnitude);
  return *sign != 0 ? -value : value;
}

// One team's fields in the writer's order; `bits` are that team's mask bits.
struct TeamBits {
  std::uint32_t b0, b1, b2and15, b3, b4, b5, b6, b7and14, b8, b9, b10, b11, b12, tickets, rounds;
};
// The order and the bits: `ScoreManager::setNetUpdate` (OpenBF2), the same order
// as `updateStateMask`'s tables.
constexpr TeamBits kTeamBits[2] = {
    {0x1u, 0x2u, 0x01000000u, 0x04000000u, 0x10000000u, 0x4u, 0x8u, 0x10u, 0x20u, 0x40u, 0x80u,
     0x100u, 0x200u, 0x400u, 0x800u},
    {0x1000u, 0x2000u, 0x02000000u, 0x08000000u, 0x20000000u, 0x4000u, 0x8000u, 0x10000u,
     0x20000u, 0x40000u, 0x80000u, 0x100000u, 0x200000u, 0x400000u, 0x800000u},
};
constexpr std::uint32_t kWinnerBit = 0x40000000u;
constexpr std::uint32_t kVictoryBit = 0x80000000u;

bool readTeam(BitReader& reader, std::uint32_t mask, const TeamBits& bits, TeamScoreState& team) {
  const auto field = [&](std::uint32_t bit, std::optional<int>& out) {
    if ((mask & bit) == 0) return true;
    out = signedMagnitude(reader, 18);
    return out.has_value();
  };
  if (!field(bits.b0, team.block[0])) return false;
  if (!field(bits.b1, team.block[1])) return false;
  if (!field(bits.b2and15, team.block[2]) ||
      ((mask & bits.b2and15) != 0 && !(team.block[15] = signedMagnitude(reader, 18)))) {
    return false;
  }
  if (!field(bits.b3, team.block[3])) return false;
  if (!field(bits.b4, team.block[4])) return false;
  if (!field(bits.b5, team.block[5])) return false;
  if (!field(bits.b6, team.block[6])) return false;
  if (!field(bits.b7and14, team.block[7]) ||
      ((mask & bits.b7and14) != 0 && !(team.block[14] = signedMagnitude(reader, 18)))) {
    return false;
  }
  if (!field(bits.b8, team.block[8])) return false;
  if (!field(bits.b9, team.block[9])) return false;
  if (!field(bits.b10, team.block[10])) return false;
  if (!field(bits.b11, team.block[11])) return false;
  if (!field(bits.b12, team.block[12])) return false;
  if ((mask & bits.tickets) != 0) {
    team.tickets = signedMagnitude(reader, 18);
    const auto bleed = reader.readBits(1);
    team.ticketState = signedMagnitude(reader, 18);
    if (!team.tickets || !bleed || !team.ticketState) return false;
    team.ticketBleed = *bleed != 0;
  }
  return field(bits.rounds, team.roundsWon);
}

}  // namespace

std::optional<ScoreState> readScoreState(BitReader& reader) {
  ScoreState state;
  const auto mask = reader.readBits(kScoreMaskBits);
  if (!mask) return std::nullopt;
  state.mask = *mask;
  if (!readTeam(reader, state.mask, kTeamBits[0], state.teams[0]) ||
      !readTeam(reader, state.mask, kTeamBits[1], state.teams[1])) {
    return state;
  }
  if ((state.mask & kWinnerBit) != 0) {
    state.winner = signedMagnitude(reader, 3);
    if (!state.winner) return state;
  }
  if ((state.mask & kVictoryBit) != 0) {
    state.victoryType = signedMagnitude(reader, 3);
    if (!state.victoryType) return state;
    state.teams[0].block[13] = signedMagnitude(reader, 18);
    state.teams[1].block[13] = signedMagnitude(reader, 18);
    if (!state.teams[0].block[13] || !state.teams[1].block[13]) return state;
  }
  state.complete = true;
  return state;
}

void Score::apply(const ScoreState& state) {
  known = true;
  for (std::size_t t = 0; t < 2; ++t) {
    const TeamScoreState& in = state.teams[t];
    TeamScore& out = teams[t];
    for (std::size_t i = 0; i < in.block.size(); ++i) {
      if (in.block[i]) out.block[i] = *in.block[i];
    }
    if (in.tickets) out.tickets = *in.tickets;
    if (in.ticketBleed) out.ticketBleed = *in.ticketBleed;
    if (in.ticketState) out.ticketState = *in.ticketState;
    if (in.roundsWon) out.roundsWon = *in.roundsWon;
  }
  if (state.winner) winner = *state.winner;
  if (state.victoryType) victoryType = *state.victoryType;
}

}  // namespace obf2::net::bf2
