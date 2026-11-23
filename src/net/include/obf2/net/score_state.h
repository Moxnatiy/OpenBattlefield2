#pragma once
// The round's score — `ScoreManager::setNetUpdate` (OpenBF2 ScoreSystem/
// ScoreManager.cpp, Linux 0x81086a0 for the writer), read whole.
//
// The score manager is a networkable with no object: `Game::registerStatic
// Networkables` gives it network id 0x0201 on both sides (OpenBF2 Game.cpp), so
// its ghost records are told by that id alone, never by a create event.
//
// The record is a 32-bit mask, then the fields its bits name, in the writer's
// order (not the bits' numeric order). Every number is a sign bit and a
// magnitude: 17 bits for the score fields, 2 for the round's result. The mask
// bits per team (team 1 / team 2):
//
//   0x1 / 0x1000          block[0]  (TeamScore +0x04)
//   0x2 / 0x2000          block[1]  (+0x08)
//   0x1000000 / 0x2000000 block[2] and block[15] (+0x0C, +0x40), together
//   0x4000000 / 0x8000000 block[3]  (+0x10)
//   0x10000000 / 0x20000000 block[4] (+0x14)
//   0x4 / 0x4000          block[5]  (+0x18)
//   0x8 / 0x8000          block[6]  (+0x1C)
//   0x10 / 0x10000        block[7] and block[14] (+0x20, +0x3C), together
//   0x20 / 0x20000        block[8]  (+0x24)
//   0x40 / 0x40000        block[9]  (+0x28)
//   0x80 / 0x80000        block[10] (+0x2C)
//   0x100 / 0x100000      block[11] (+0x30)
//   0x200 / 0x200000      block[12] (+0x34)
//   0x400 / 0x400000      the tickets (+0x84), the bleed bit (+0x90, one bit),
//                         the ticket state (+0x8C)
//   0x800 / 0x800000      the rounds won (+0x88)
//
// and for the round: 0x40000000 the winner (3 bits), 0x80000000 the victory type
// (3 bits) followed by block[13] (+0x38) of team 1, then of team 2.
//
// What `block` holds is not established — OpenBF2 has it as `TeamScore::
// m_block[16]` "[UNRESOLVED]". The tickets, their state, the bleed and the rounds
// won are named (`TeamScore.h`; `python::gl_sendEndOfRoundData` reads +0x84).
#include <array>
#include <cstdint>
#include <optional>

#include "obf2/net/bitstream.h"

namespace obf2::net::bf2 {

// `Game::registerStaticNetworkables`: `addNetworkable(scoreManager, 0x0201)`.
inline constexpr std::uint16_t kScoreManagerNetworkId = 0x0201;
inline constexpr unsigned kScoreMaskBits = 32;

struct TeamScoreState {
  // `TeamScore` +0x04..+0x40, purpose not established; index = (offset - 4) / 4.
  std::array<std::optional<int>, 16> block{};
  std::optional<int> tickets;      // +0x84, 0..9999
  std::optional<bool> ticketBleed; // +0x90
  std::optional<int> ticketState;  // +0x8C, purpose not established beyond its name
  std::optional<int> roundsWon;    // +0x88
};

struct ScoreState {
  std::uint32_t mask = 0;
  // Read through to the last field the mask names. False: the buffer ran out.
  bool complete = false;
  std::array<TeamScoreState, 2> teams{};  // team 1, team 2
  std::optional<int> winner;       // ScoreManager +0x9C
  std::optional<int> victoryType;  // ScoreManager +0x9D
};

std::optional<ScoreState> readScoreState(BitReader& reader);

// The score as the client keeps it: each field holds its last value; a record
// changes only what its mask names (the engine's apply walks the same mask).
struct TeamScore {
  std::array<int, 16> block{};
  int tickets = 0;
  bool ticketBleed = false;
  int ticketState = 0;
  int roundsWon = 0;
};
struct Score {
  bool known = false;  // a record has come
  std::array<TeamScore, 2> teams{};
  int winner = 0;
  int victoryType = 0;

  void apply(const ScoreState& state);
};

}  // namespace obf2::net::bf2
