#include "obf2/texture/dds.h"

#include <algorithm>
#include <cstring>

namespace obf2::texture {
namespace {

constexpr std::uint32_t kMagic = 0x20534444;  // "DDS "
constexpr std::uint32_t kHeaderSize = 124;

// DDS_PIXELFORMAT.dwFlags
constexpr std::uint32_t kPfAlphaPixels = 0x1;
constexpr std::uint32_t kPfFourCC = 0x4;
constexpr std::uint32_t kPfRgb = 0x40;

// DDSCAPS2
constexpr std::uint32_t kCapsCubemap = 0x200;
constexpr std::uint32_t kCapsVolume = 0x200000;

std::uint32_t fourcc(const char* code) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(code[0])) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(code[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(code[2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(code[3])) << 24);
}

std::uint32_t readU32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

}  // namespace

std::string_view formatName(Format format) {
  switch (format) {
    case Format::Bc1: return "BC1 (DXT1)";
    case Format::Bc2: return "BC2 (DXT3)";
    case Format::Bc3: return "BC3 (DXT5)";
    case Format::Bgra8: return "B8G8R8A8";
    case Format::Bgra4: return "B4G4R4A4";
    case Format::Bgr565: return "B5G6R5";
    case Format::R8: return "R8";
  }
  return "?";
}

std::size_t levelSize(Format format, std::uint32_t width, std::uint32_t height) {
  switch (format) {
    case Format::Bc1:
    case Format::Bc2:
    case Format::Bc3: {
      // Compressed formats are always counted in 4x4 blocks, even when the
      // level is smaller: 2x2 and 1x1 maps still take a whole block.
      const std::size_t blocksX = std::max<std::uint32_t>(1, (width + 3) / 4);
      const std::size_t blocksY = std::max<std::uint32_t>(1, (height + 3) / 4);
      return blocksX * blocksY * (format == Format::Bc1 ? 8u : 16u);
    }
    case Format::Bgra8: return static_cast<std::size_t>(width) * height * 4;
    case Format::Bgra4:
    case Format::Bgr565: return static_cast<std::size_t>(width) * height * 2;
    case Format::R8: return static_cast<std::size_t>(width) * height;
  }
  return 0;
}

namespace {

// A4R4G4B4 / R5G6B5 -> B8G8R8A8. Nibbles and five-bit fields are expanded so
// that 0xF becomes 0xFF rather than 0xF0.
void expandToBgra8(Texture& texture) {
  std::vector<std::byte> out;
  std::vector<MipLevel> mips;
  out.reserve(texture.data.size() * 2);

  for (const MipLevel& mip : texture.mips) {
    const std::size_t pixels = static_cast<std::size_t>(mip.width) * mip.height;
    const std::size_t start = out.size();

    for (std::size_t i = 0; i < pixels; ++i) {
      const std::size_t at = mip.offset + i * 2;
      if (at + 1 >= texture.data.size()) break;
      const auto value = static_cast<std::uint32_t>(
          static_cast<std::uint8_t>(texture.data[at]) |
          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(texture.data[at + 1])) << 8));

      std::uint8_t b = 0, g = 0, r = 0, a = 255;
      if (texture.format == Format::Bgra4) {
        const auto expand4 = [](std::uint32_t v) { return static_cast<std::uint8_t>(v * 17u); };
        b = expand4(value & 0xF);
        g = expand4((value >> 4) & 0xF);
        r = expand4((value >> 8) & 0xF);
        a = expand4((value >> 12) & 0xF);
      } else {  // R5G6B5
        b = static_cast<std::uint8_t>(((value & 0x1F) * 255 + 15) / 31);
        g = static_cast<std::uint8_t>((((value >> 5) & 0x3F) * 255 + 31) / 63);
        r = static_cast<std::uint8_t>((((value >> 11) & 0x1F) * 255 + 15) / 31);
      }

      out.push_back(static_cast<std::byte>(b));
      out.push_back(static_cast<std::byte>(g));
      out.push_back(static_cast<std::byte>(r));
      out.push_back(static_cast<std::byte>(a));
    }
    mips.push_back(MipLevel{mip.width, mip.height, start, out.size() - start});
  }

  texture.format = Format::Bgra8;
  texture.data = std::move(out);
  texture.mips = std::move(mips);
}

}  // namespace

Texture solidColor(float red, float green, float blue, float alpha) {
  auto toByte = [](float value) {
    const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    return static_cast<std::byte>(static_cast<std::uint8_t>(clamped * 255.0f + 0.5f));
  };

  Texture texture;
  texture.format = Format::Bgra8;  // channel order B, G, R, A
  texture.width = 1;
  texture.height = 1;
  texture.data = {toByte(blue), toByte(green), toByte(red), toByte(alpha)};
  texture.mips.push_back(MipLevel{1, 1, 0, 4});
  return texture;
}

std::optional<Texture> loadDds(std::span<const std::byte> bytes, std::string* error) {
  auto fail = [error](std::string why) -> std::optional<Texture> {
    if (error) *error = std::move(why);
    return std::nullopt;
  };

  if (bytes.size() < 128) return fail("file too small for a DDS");
  if (readU32(bytes, 0) != kMagic) return fail("no DDS signature");
  if (readU32(bytes, 4) != kHeaderSize) return fail("unexpected header size");

  const std::uint32_t height = readU32(bytes, 12);
  const std::uint32_t width = readU32(bytes, 16);
  const std::uint32_t declaredMips = readU32(bytes, 28);

  const std::uint32_t pfFlags = readU32(bytes, 80);
  const std::uint32_t pfFourCC = readU32(bytes, 84);
  const std::uint32_t bitCount = readU32(bytes, 88);
  const std::uint32_t maskR = readU32(bytes, 92);
  const std::uint32_t maskA = readU32(bytes, 104);
  const std::uint32_t caps2 = readU32(bytes, 112);

  if (width == 0 || height == 0) return fail("texture has zero size");
  if ((caps2 & kCapsCubemap) != 0) return fail("cube maps are not supported");
  if ((caps2 & kCapsVolume) != 0) return fail("volume textures are not supported");

  Format format{};
  if ((pfFlags & kPfFourCC) != 0) {
    if (pfFourCC == fourcc("DXT1")) format = Format::Bc1;
    else if (pfFourCC == fourcc("DXT3")) format = Format::Bc2;
    else if (pfFourCC == fourcc("DXT5")) format = Format::Bc3;
    else if (pfFourCC == fourcc("DX10")) return fail("the DX10 header is not supported");
    else {
      char code[5] = {0};
      std::memcpy(code, &pfFourCC, 4);
      return fail(std::string("unknown FourCC: ") + code);
    }
  } else if ((pfFlags & kPfRgb) != 0) {
    if (bitCount == 32) format = Format::Bgra8;
    else if (bitCount == 16 && (pfFlags & kPfAlphaPixels) != 0 && maskA == 0xF000) format = Format::Bgra4;
    else if (bitCount == 16 && maskR == 0xF800) format = Format::Bgr565;
    else return fail("unsupported uncompressed layout, " + std::to_string(bitCount) + " bits");
  } else if (bitCount == 8) {
    format = Format::R8;  // the only such file in the game is an interface icon
  } else {
    return fail("unsupported pixel format");
  }

  Texture texture;
  texture.format = format;
  texture.width = width;
  texture.height = height;

  const std::size_t payloadOffset = 128;
  const std::size_t available = bytes.size() - payloadOffset;

  // The header's level count is not trusted: we take as many as actually fit
  // into the file. The game contains DDS files with mipCount=0 that in fact
  // have one level.
  const std::uint32_t wantedMips = std::max<std::uint32_t>(1, declaredMips);
  std::size_t offset = 0;
  std::uint32_t levelWidth = width;
  std::uint32_t levelHeight = height;

  for (std::uint32_t level = 0; level < wantedMips; ++level) {
    const std::size_t size = levelSize(format, levelWidth, levelHeight);
    if (size == 0 || offset + size > available) break;

    texture.mips.push_back(MipLevel{levelWidth, levelHeight, offset, size});
    offset += size;

    if (levelWidth == 1 && levelHeight == 1) break;
    levelWidth = std::max<std::uint32_t>(1, levelWidth / 2);
    levelHeight = std::max<std::uint32_t>(1, levelHeight / 2);
  }

  if (texture.mips.empty()) return fail("the texture's data is truncated");

  texture.data.resize(offset);
  std::memcpy(texture.data.data(), bytes.data() + payloadOffset, offset);

  // 16-bit formats are expanded into B8G8R8A8 at load time.
  //
  // The reason: in a DDS the channel order is given by masks (A4R4G4B4 keeps
  // alpha in the high nibble), while the names of packed 16-bit formats in
  // graphics APIs mean the order their own way. Risking swapped channels for
  // the sake of 345 small interface textures is not worth it — expanding is cheaper.
  if (texture.format == Format::Bgra4 || texture.format == Format::Bgr565) {
    expandToBgra8(texture);
  }
  return texture;
}

namespace {

// One 565 colour to RGB8.
void unpack565(std::uint16_t value, std::uint8_t* out) {
  const std::uint32_t r = (value >> 11) & 0x1F;
  const std::uint32_t g = (value >> 5) & 0x3F;
  const std::uint32_t b = value & 0x1F;
  out[0] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
  out[1] = static_cast<std::uint8_t>((g << 2) | (g >> 4));
  out[2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
}

// Colour half of a BC block: two endpoints and 2 bits per pixel.
//
// BC1 spends the fourth entry on transparency when the first endpoint is
// not greater than the second; BC2 and BC3 always use the four-colour
// form, because their alpha lives in the first eight bytes.
void decodeColourBlock(const std::byte* block, bool punchThrough, std::uint8_t out[16][4]) {
  const auto word = [&](std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(block[at]) |
                                      (static_cast<std::uint8_t>(block[at + 1]) << 8));
  };
  const std::uint16_t first = word(0);
  const std::uint16_t second = word(2);

  std::uint8_t palette[4][4] = {};
  unpack565(first, palette[0]);
  unpack565(second, palette[1]);
  palette[0][3] = palette[1][3] = 255;

  const bool opaque = !punchThrough || first > second;
  for (int channel = 0; channel < 3; ++channel) {
    const int a = palette[0][channel];
    const int b = palette[1][channel];
    if (opaque) {
      palette[2][channel] = static_cast<std::uint8_t>((2 * a + b) / 3);
      palette[3][channel] = static_cast<std::uint8_t>((a + 2 * b) / 3);
    } else {
      palette[2][channel] = static_cast<std::uint8_t>((a + b) / 2);
      palette[3][channel] = 0;
    }
  }
  palette[2][3] = 255;
  palette[3][3] = opaque ? 255 : 0;

  for (int pixel = 0; pixel < 16; ++pixel) {
    const auto bits = static_cast<std::uint8_t>(block[4 + pixel / 4]);
    const int index = (bits >> (2 * (pixel % 4))) & 0x3;
    std::memcpy(out[pixel], palette[index], 4);
  }
}

// BC3 alpha: two endpoints and 3 bits per pixel.
void decodeAlphaBlock(const std::byte* block, std::uint8_t out[16]) {
  const auto first = static_cast<std::uint8_t>(block[0]);
  const auto second = static_cast<std::uint8_t>(block[1]);
  std::uint8_t palette[8] = {first, second};
  if (first > second) {
    for (int i = 0; i < 6; ++i) {
      palette[2 + i] = static_cast<std::uint8_t>(((6 - i) * first + (1 + i) * second) / 7);
    }
  } else {
    for (int i = 0; i < 4; ++i) {
      palette[2 + i] = static_cast<std::uint8_t>(((4 - i) * first + (1 + i) * second) / 5);
    }
    palette[6] = 0;
    palette[7] = 255;
  }

  std::uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) {
    bits |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(block[2 + i])) << (8 * i);
  }
  for (int pixel = 0; pixel < 16; ++pixel) {
    out[pixel] = palette[(bits >> (3 * pixel)) & 0x7];
  }
}

}  // namespace

std::vector<std::uint8_t> decodeRgba8(const Texture& texture, std::size_t level) {
  if (level >= texture.mips.size()) return {};
  const MipLevel& mip = texture.mips[level];
  if (mip.offset + mip.size > texture.data.size()) return {};

  const std::byte* source = texture.data.data() + mip.offset;
  std::vector<std::uint8_t> out(static_cast<std::size_t>(mip.width) * mip.height * 4, 0);
  const auto put = [&](std::uint32_t x, std::uint32_t y, const std::uint8_t rgba[4]) {
    if (x >= mip.width || y >= mip.height) return;
    std::memcpy(out.data() + (static_cast<std::size_t>(y) * mip.width + x) * 4, rgba, 4);
  };

  switch (texture.format) {
    case Format::Bc1:
    case Format::Bc2:
    case Format::Bc3: {
      const std::size_t blockBytes = texture.format == Format::Bc1 ? 8u : 16u;
      const std::uint32_t blocksX = (mip.width + 3) / 4;
      const std::uint32_t blocksY = (mip.height + 3) / 4;
      for (std::uint32_t by = 0; by < blocksY; ++by) {
        for (std::uint32_t bx = 0; bx < blocksX; ++bx) {
          const std::size_t at = (static_cast<std::size_t>(by) * blocksX + bx) * blockBytes;
          if (at + blockBytes > mip.size) return out;
          const std::byte* block = source + at;

          std::uint8_t alpha[16];
          const std::byte* colour = block;
          if (texture.format == Format::Bc3) {
            decodeAlphaBlock(block, alpha);
            colour = block + 8;
          } else if (texture.format == Format::Bc2) {
            // Four bits per pixel, low nibble first.
            for (int pixel = 0; pixel < 16; ++pixel) {
              const auto byte = static_cast<std::uint8_t>(block[pixel / 2]);
              const std::uint8_t nibble = (pixel % 2) == 0 ? (byte & 0xF) : (byte >> 4);
              alpha[pixel] = static_cast<std::uint8_t>(nibble * 17u);
            }
            colour = block + 8;
          } else {
            std::memset(alpha, 255, sizeof(alpha));
          }

          std::uint8_t pixels[16][4];
          decodeColourBlock(colour, texture.format == Format::Bc1, pixels);
          for (int pixel = 0; pixel < 16; ++pixel) {
            std::uint8_t rgba[4] = {pixels[pixel][0], pixels[pixel][1], pixels[pixel][2],
                                    pixels[pixel][3]};
            // BC1 carries transparency in the colour block, the others in
            // their own alpha block.
            if (texture.format != Format::Bc1) rgba[3] = alpha[pixel];
            put(bx * 4 + pixel % 4, by * 4 + pixel / 4, rgba);
          }
        }
      }
      return out;
    }
    case Format::Bgra8:
    case Format::Bgra4:
    case Format::Bgr565: {
      // The 16-bit ones are expanded to B8G8R8A8 on load already.
      const std::size_t pixels = static_cast<std::size_t>(mip.width) * mip.height;
      if (mip.size < pixels * 4) return out;
      for (std::size_t i = 0; i < pixels; ++i) {
        const auto* bgra = reinterpret_cast<const std::uint8_t*>(source) + i * 4;
        out[i * 4 + 0] = bgra[2];
        out[i * 4 + 1] = bgra[1];
        out[i * 4 + 2] = bgra[0];
        out[i * 4 + 3] = bgra[3];
      }
      return out;
    }
    case Format::R8: {
      const std::size_t pixels = static_cast<std::size_t>(mip.width) * mip.height;
      if (mip.size < pixels) return out;
      for (std::size_t i = 0; i < pixels; ++i) {
        const auto value = static_cast<std::uint8_t>(source[i]);
        out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = value;
        out[i * 4 + 3] = 255;
      }
      return out;
    }
  }
  return out;
}

}  // namespace obf2::texture
