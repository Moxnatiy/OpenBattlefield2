#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace obf2::net::bf2 {

inline constexpr unsigned kEndOfRoundInfoBlock = 8;

struct EndOfRoundInfo {
  struct PlayerResult {
    std::uint8_t player;
    // Keep all six signed score fields in wire order.
    std::array<std::int16_t, 6> scores;
  };
  struct TeamResult {
    std::string faction;
    std::int32_t tickets;
  };
  struct RoundResult {
    std::uint8_t mapSize;
    std::int8_t resultCode;
    std::uint8_t winner;
    std::string map;
    std::string modeLabel;
    std::array<TeamResult, 2> teams;
  };
  std::int32_t delaySeconds = 0;
  std::vector<PlayerResult> players;
  std::vector<RoundResult> history;
  // Retail's stats/endofround.py emits named fields, including np0..np2
  // (top players) and their tsp, ssp and csp scores.
  std::map<std::string, std::string, std::less<>> stats;
};

std::optional<EndOfRoundInfo> parseEndOfRoundInfo(std::span<const std::byte> bytes);

}  // namespace obf2::net::bf2
