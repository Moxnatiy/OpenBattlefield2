#pragma once
// The head of a Bink video file: how many frames it has and at what rate.
//
// We do not decode Bink. What the engine's startup needs from a movie is how long
// it lasts, and that is written in the file's own header, so the intro holds each
// movie for exactly its length instead of a number we would have to make up.
//
// The layout is the public one (MultimediaWiki, "Bink Container"), every field a
// little-endian 32-bit word:
//
//   +0x00  signature, "BIK" and a version letter (`BIKi` in every BF2 movie)
//   +0x04  file size less 8
//   +0x08  number of frames
//   +0x0C  largest frame size in bytes
//   +0x10  number of frames again
//   +0x14  width
//   +0x18  height
//   +0x1C  frame rate dividend
//   +0x20  frame rate divider
//
// Checked against the game's own files (`mods/bf2/Movies/*.bik`): in all seven the
// size word plus 8 is the file's length on disk.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace obf2::engine {

struct BinkHeader {
  char version = 0;               // the letter after "BIK"
  std::uint32_t fileSize = 0;     // +0x04 plus 8: the whole file
  std::uint32_t frames = 0;       // +0x08
  std::uint32_t largestFrame = 0; // +0x0C
  std::uint32_t width = 0;        // +0x14
  std::uint32_t height = 0;       // +0x18
  std::uint32_t rateDividend = 0; // +0x1C
  std::uint32_t rateDivider = 0;  // +0x20

  // frames × divider / dividend. Zero when the rate is zero.
  double seconds() const;
};

// The bytes from the start of the file; 36 are enough. nullopt when the
// signature is not "BIK" or the head is cut short.
std::optional<BinkHeader> readBinkHeader(std::span<const std::byte> head);

// How many bytes of the file `readBinkHeader` needs.
inline constexpr std::size_t kBinkHeaderBytes = 0x24;

}  // namespace obf2::engine
