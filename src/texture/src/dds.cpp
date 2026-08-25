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
      // Стиснені формати завжди рахуються блоками 4x4, навіть коли рівень
      // менший за блок: мапи 2x2 і 1x1 усе одно займають цілий блок.
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

std::optional<Texture> loadDds(std::span<const std::byte> bytes, std::string* error) {
  auto fail = [error](std::string why) -> std::optional<Texture> {
    if (error) *error = std::move(why);
    return std::nullopt;
  };

  if (bytes.size() < 128) return fail("файл замалий для DDS");
  if (readU32(bytes, 0) != kMagic) return fail("немає підпису DDS");
  if (readU32(bytes, 4) != kHeaderSize) return fail("несподіваний розмір заголовка");

  const std::uint32_t height = readU32(bytes, 12);
  const std::uint32_t width = readU32(bytes, 16);
  const std::uint32_t declaredMips = readU32(bytes, 28);

  const std::uint32_t pfFlags = readU32(bytes, 80);
  const std::uint32_t pfFourCC = readU32(bytes, 84);
  const std::uint32_t bitCount = readU32(bytes, 88);
  const std::uint32_t maskR = readU32(bytes, 92);
  const std::uint32_t maskA = readU32(bytes, 104);
  const std::uint32_t caps2 = readU32(bytes, 112);

  if (width == 0 || height == 0) return fail("нульовий розмір текстури");
  if ((caps2 & kCapsCubemap) != 0) return fail("кубічні мапи не підтримуються");
  if ((caps2 & kCapsVolume) != 0) return fail("об'ємні текстури не підтримуються");

  Format format{};
  if ((pfFlags & kPfFourCC) != 0) {
    if (pfFourCC == fourcc("DXT1")) format = Format::Bc1;
    else if (pfFourCC == fourcc("DXT3")) format = Format::Bc2;
    else if (pfFourCC == fourcc("DXT5")) format = Format::Bc3;
    else if (pfFourCC == fourcc("DX10")) return fail("DX10-заголовок не підтримується");
    else {
      char code[5] = {0};
      std::memcpy(code, &pfFourCC, 4);
      return fail(std::string("невідомий FourCC: ") + code);
    }
  } else if ((pfFlags & kPfRgb) != 0) {
    if (bitCount == 32) format = Format::Bgra8;
    else if (bitCount == 16 && (pfFlags & kPfAlphaPixels) != 0 && maskA == 0xF000) format = Format::Bgra4;
    else if (bitCount == 16 && maskR == 0xF800) format = Format::Bgr565;
    else return fail("непідтримувана нестиснена розкладка, " + std::to_string(bitCount) + " біт");
  } else if (bitCount == 8) {
    format = Format::R8;  // єдиний такий файл у грі — іконка інтерфейсу
  } else {
    return fail("непідтримуваний формат пікселів");
  }

  Texture texture;
  texture.format = format;
  texture.width = width;
  texture.height = height;

  const std::size_t payloadOffset = 128;
  const std::size_t available = bytes.size() - payloadOffset;

  // Кількості рівнів у заголовку не довіряємо: беремо стільки, скільки
  // реально влізло у файл. У грі трапляються DDS з mipCount=0, які насправді
  // мають один рівень.
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

  if (texture.mips.empty()) return fail("дані текстури обірвано");

  texture.data.resize(offset);
  std::memcpy(texture.data.data(), bytes.data() + payloadOffset, offset);
  return texture;
}

}  // namespace obf2::texture
