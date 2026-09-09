#pragma once
// DDS is DirectDraw Surface's texture container. BF2 1.5 contains exactly
// six variants (checked across all 2230 files in the archives):
//
//   DXT5     999   DXT1     502   A4R4G4B4  337
//   A8R8G8B8 251   DXT3     131   R5G6B5      9   + one 8-bit icon
//
// So there are no DX10 headers, no cube maps and no volume textures here —
// which is exactly why the parser can stay small.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obf2::texture {

enum class Format {
  Bc1,      // DXT1 — 8 bytes per 4x4 block
  Bc2,      // DXT3 — 16 bytes per block
  Bc3,      // DXT5 — 16 bytes per block
  Bgra8,    // A8R8G8B8
  Bgra4,    // A4R4G4B4
  Bgr565,   // R5G6B5
  R8,       // 8 bits, one channel
};

struct MipLevel {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t offset = 0;  // offset into Texture::data
  std::size_t size = 0;
};

struct Texture {
  Format format = Format::Bc1;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<MipLevel> mips;   // mips[0] is the full size
  std::vector<std::byte> data;  // every level, back to back

  bool isCompressed() const {
    return format == Format::Bc1 || format == Format::Bc2 || format == Format::Bc3;
  }
};

std::string_view formatName(Format format);

// How many bytes one level of the given size takes.
std::size_t levelSize(Format format, std::uint32_t width, std::uint32_t height);

// nullopt plus an explanation instead of an exception: a corrupt texture is an
// expected situation, not an exceptional one. The data is not copied twice, but
// neither does it reference the input buffer — Texture::data stands alone.
std::optional<Texture> loadDds(std::span<const std::byte> bytes, std::string* error = nullptr);

// PNG. The game's textures are DDS, but the menu's assets (BF2 draws it in
// Flash) are PNG, so the menu screen needs both.
std::optional<Texture> loadPng(std::span<const std::byte> bytes, std::string* error = nullptr);

// Dispatches on the contents: DDS has the signature "DDS ", PNG has its own.
std::optional<Texture> loadImage(std::span<const std::byte> bytes, std::string* error = nullptr);

// A 1x1 texture of the given colour. Needed where the game gives a colour as a
// number rather than a file — renderer.waterColor in a level's Water.con, say.
Texture solidColor(float red, float green, float blue, float alpha = 1.0f);

// One mip level as plain RGBA8, four bytes per pixel, rows top to bottom.
//
// The GPU eats BC1/BC2/BC3 as they are, so the renderer never needs this.
// The Flash menu does: the movie loads images through the player, and the
// player wants pixels. Almost every `.png` in the game is in fact a DDS
// (map previews, flags, award icons), which is why the original player
// decodes them at all.
//
// Empty vector when the level is missing or the format is not handled.
std::vector<std::uint8_t> decodeRgba8(const Texture& texture, std::size_t level = 0);

}  // namespace obf2::texture
