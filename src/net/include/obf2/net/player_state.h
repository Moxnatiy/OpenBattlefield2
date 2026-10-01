#pragma once
// A player's network state — `Player::setNetUpdate` (`BF2.exe` 0x500570; the
// Linux server's 0x49c580 names what each field is handed to), read whole.
// docs/functions/player-state.md has the table.
//
// A player — score, ping, team, squad, kit, alive — is a networkable of its own:
// `CreatePlayerEvent` names its network id, and the ghost records with that id
// carry this.
#include <array>
#include <cstdint>
#include <optional>

#include "obf2/net/bitstream.h"

namespace obf2::net::bf2 {

inline constexpr unsigned kPlayerMaskBits = 28;

// One of the three firing states (0x40 fire, 0x80 alt fire, 0x20000 flare).
struct PlayerFiring {
  bool on = false;
  bool detailed = false;  // the fields below were read
  std::uint32_t value7 = 0;     // 7 bits, stored minus one — purpose not established
  std::uint32_t value2 = 0;     // 2 bits — purpose not established
  bool flag = false;            // 1 bit — purpose not established
  float fraction = 0.0f;        // 8 bits × 10/255 or 6 bits / 63 — purpose not established
  std::uint32_t value4 = 0;     // 4 bits — purpose not established
};

struct PlayerState {
  std::uint32_t mask = 0;
  // Read through to the last field. False: the buffer ran out.
  bool complete = false;

  std::optional<bool> alive;              // 0x1, `setIsAlive`
  std::optional<std::uint32_t> spawnGroup;  // 0x2, 0..255

  // 0x8: the score block, as `PlayerScoreData` keeps it.
  struct Score {
    int score = 0;      // [0]
    int deaths = 0;     // [4]
    int kills = 0;      // [5]
    int teamwork = 0;   // [1] rplScore
    int teamKills = 0;  // [6] TKs
  };
  std::optional<Score> score;

  std::optional<bool> flagHolder;       // 0x200000
  std::optional<bool> flag10;           // 0x10 — purpose not established
  std::optional<std::uint32_t> counter2000000;  // 0x2000000, 2 bits — purpose not established
  std::array<std::optional<PlayerFiring>, 3> firing;  // 0x40, 0x80, 0x20000
  std::optional<bool> sprint;           // 0x40000, `setSprintState`
  std::optional<bool> sprintLastTick;   // 0x40000, `setSprintStateLastTick`
  std::optional<std::uint32_t> ping;    // 0x100, 11 bits
  // 0x20: the tick the player may spawn at, and whether he is "man down" — Linux
  // applies them to +0x104 and +0x108, which `getTimeToSpawn` (0x4944e0: that tick
  // less the game tick, over 30, not below 0) and `getManDownTimeLeft` (0x4a2320)
  // read. The client keeps them at +0xe4 and +0xe8.
  std::optional<std::uint32_t> spawnAtTick;
  std::optional<bool> manDown;
  std::optional<int> team;               // 0x200, `setTeam`
  std::optional<std::uint32_t> camera400;  // 0x400, 4 bits — a camera setting, purpose not established
  std::optional<bool> flag400;             // 0x400 — purpose not established
  std::optional<std::uint32_t> value800;   // 0x800, 0..255 — purpose not established
  std::optional<int> kit;                  // 0x1000, the low four bits: `setKit`
  std::optional<int> unlockLevel;          // 0x1000, the high four bits
  std::optional<std::uint32_t> squad;      // 0x2000, `setSquadId`
  // 0x4000: the rank and the three place medals (`firstPlace`, `secondPlace`,
  // `thirdPlace` of `pmgr_getScore`).
  std::optional<int> rank;
  std::optional<std::array<std::uint32_t, 3>> places;
  std::optional<bool> freeCamera;       // 0x8000
  std::optional<bool> overheadCamera;   // 0x400000
  std::optional<std::uint32_t> value800000;   // 0x800000, 8 bits — purpose not established
  std::optional<int> value1000000;            // 0x1000000, 17 bits minus one — purpose not established
  bool alwaysA = false;                  // always, 1 bit — purpose not established
  std::optional<bool> commander;         // 0x4000000, `setIsCommander`
  std::array<bool, 5> commanderFlags{};  // when commander — purpose not established
  std::array<float, 3> commanderFractions{};  // when commander — purpose not established
  bool alwaysB = false;                  // always, 1 bit — purpose not established
  std::optional<std::uint32_t> maskSetting;  // 0x8000000, `setMaskSetting`
};

std::optional<PlayerState> readPlayerState(BitReader& reader);

}  // namespace obf2::net::bf2
