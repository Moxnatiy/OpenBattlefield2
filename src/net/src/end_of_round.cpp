#include "obf2/net/end_of_round.h"

#include <string_view>
#include <utility>
#include "miniz.h"

namespace obf2::net::bf2 {

std::optional<EndOfRoundInfo> parseEndOfRoundInfo(std::span<const std::byte> bytes) {
  const auto read32 = [&](std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
      value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    }
    return value;
  };
  // Captured retail packets have two little-endian sizes, a zlib stream,
  // then the delay, the score rows and the round history.
  constexpr std::size_t headerSize = 8;
  constexpr std::size_t maxStatsSize = 64 * 1024;
  if (bytes.size() < headerSize + 4) return std::nullopt;
  const auto plainSize = read32(0);
  const auto compressedSize = read32(4);
  if (plainSize > maxStatsSize || compressedSize > bytes.size() - headerSize - 4) {
    return std::nullopt;
  }

  const auto signedMagnitude = [](std::uint32_t bits) {
    const auto magnitude = static_cast<std::int32_t>(bits >> 1);
    return (bits & 1) ? -magnitude : magnitude;
  };
  EndOfRoundInfo info;
  std::size_t position = headerSize + compressedSize;
  info.delaySeconds = signedMagnitude(read32(position));
  position += 4;
  const auto read8 = [&]() { return std::to_integer<std::uint8_t>(bytes[position++]); };
  const auto readString = [&](std::string& text) {
    if (position == bytes.size()) return false;
    const auto length = read8();
    if (length > bytes.size() - position) return false;
    text.assign(reinterpret_cast<const char*>(bytes.data() + position), length);
    position += length;
    return true;
  };

  if (position == bytes.size()) return std::nullopt;
  const auto playerCount = read8();
  constexpr std::size_t playerBytes = 13;
  if (playerCount > (bytes.size() - position) / playerBytes) return std::nullopt;
  for (unsigned i = 0; i < playerCount; ++i) {
    auto& player = info.players.emplace_back();
    player.player = read8();
    for (auto& score : player.scores) {
      const std::uint16_t low = read8();
      const std::uint16_t high = read8();
      score = static_cast<std::int16_t>(signedMagnitude(low | (high << 8)));
    }
  }
  if (position == bytes.size()) return std::nullopt;
  const auto roundCount = read8();
  for (unsigned i = 0; i < roundCount; ++i) {
    if (bytes.size() - position < 3) return std::nullopt;
    auto& round = info.history.emplace_back();
    round.mapSize = read8();
    round.resultCode = static_cast<std::int8_t>(signedMagnitude(read8()));
    round.winner = read8();
    if (!readString(round.map) || !readString(round.modeLabel)) return std::nullopt;
    for (auto& team : round.teams) {
      if (!readString(team.faction) || bytes.size() - position < 4) return std::nullopt;
      team.tickets = signedMagnitude(read32(position));
      position += 4;
    }
  }
  if (position != bytes.size()) return std::nullopt;
  if (!compressedSize) return plainSize == 0 ? std::optional(std::move(info)) : std::nullopt;

  std::string plain(plainSize + 1, '\0');
  mz_ulong written = plain.size();
  mz_ulong consumed = compressedSize;
  if (mz_uncompress2(reinterpret_cast<unsigned char*>(plain.data()), &written,
                     reinterpret_cast<const unsigned char*>(bytes.data() + headerSize),
                     &consumed) != MZ_OK || written != plainSize || consumed != compressedSize) {
    return std::nullopt;
  }

  std::string_view fields(plain.data(), plainSize);
  if (fields.empty() || fields == "\\") return info;
  if (fields.front() != '\\' || fields.find('\0') != std::string_view::npos) return std::nullopt;
  fields.remove_prefix(1);
  while (!fields.empty()) {
    const auto valueStart = fields.find('\\');
    if (valueStart == 0 || valueStart == std::string_view::npos) return std::nullopt;
    const auto key = fields.substr(0, valueStart);
    fields.remove_prefix(valueStart + 1);
    const auto valueEnd = fields.find('\\');
    info.stats.insert_or_assign(std::string(key), std::string(fields.substr(0, valueEnd)));
    if (valueEnd == std::string_view::npos) break;
    fields.remove_prefix(valueEnd + 1);
  }
  return info;
}

}  // namespace obf2::net::bf2
