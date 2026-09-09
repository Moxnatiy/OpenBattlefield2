#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/texture/dds.h"

using namespace obf2::texture;

namespace {

// A DDS header builder (128 bytes) plus the data. The test does not depend on the
// game being on disk and also fixes the header's layout in code.
class DdsBuilder {
 public:
  DdsBuilder() : header_(128, std::byte{0}) {
    put32(0, 0x20534444);  // "DDS "
    put32(4, 124);         // the header's size
    put32(76, 32);         // the size of DDS_PIXELFORMAT
  }

  DdsBuilder& size(std::uint32_t width, std::uint32_t height) {
    put32(16, width);
    put32(12, height);
    return *this;
  }
  DdsBuilder& mipCount(std::uint32_t count) { put32(28, count); return *this; }

  DdsBuilder& fourcc(const char* code) {
    put32(80, 0x4);  // DDPF_FOURCC
    std::memcpy(header_.data() + 84, code, 4);
    return *this;
  }
  DdsBuilder& rgb(std::uint32_t bits, std::uint32_t maskR, std::uint32_t maskA) {
    put32(80, maskA != 0 ? 0x41u : 0x40u);  // DDPF_RGB [| DDPF_ALPHAPIXELS]
    put32(88, bits);
    put32(92, maskR);
    put32(104, maskA);
    return *this;
  }
  DdsBuilder& caps2(std::uint32_t value) { put32(112, value); return *this; }

  DdsBuilder& payload(std::size_t bytes) {
    payload_.assign(bytes, std::byte{0x7F});
    return *this;
  }

  std::vector<std::byte> build() const {
    std::vector<std::byte> out = header_;
    out.insert(out.end(), payload_.begin(), payload_.end());
    return out;
  }

 private:
  void put32(std::size_t offset, std::uint32_t value) {
    std::memcpy(header_.data() + offset, &value, sizeof(value));
  }
  std::vector<std::byte> header_;
  std::vector<std::byte> payload_;
};

}  // namespace

static void testLevelSize() {
  // Compressed formats are counted in 4x4 blocks even at tiny levels — failing to
  // account for that makes the parsing of the whole mip chain slide.
  CHECK_EQ(levelSize(Format::Bc1, 4, 4), std::size_t(8));
  CHECK_EQ(levelSize(Format::Bc1, 1, 1), std::size_t(8));
  CHECK_EQ(levelSize(Format::Bc1, 2, 2), std::size_t(8));
  CHECK_EQ(levelSize(Format::Bc3, 4, 4), std::size_t(16));
  CHECK_EQ(levelSize(Format::Bc3, 256, 256), std::size_t(65536));
  CHECK_EQ(levelSize(Format::Bgra8, 8, 4), std::size_t(128));
  CHECK_EQ(levelSize(Format::Bgr565, 8, 4), std::size_t(64));
  CHECK_EQ(levelSize(Format::R8, 8, 4), std::size_t(32));
}

static void testDxt5WithMips() {
  // 256x256 DXT5 with a full chain down to 1x1 — a typical texture of the game.
  std::size_t total = 0;
  std::uint32_t w = 256, h = 256;
  for (int level = 0; level < 9; ++level) {
    total += levelSize(Format::Bc3, w, h);
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
  }

  const auto bytes = DdsBuilder().size(256, 256).mipCount(9).fourcc("DXT5").payload(total).build();
  std::string error;
  const auto texture = loadDds(bytes, &error);
  CHECK(texture.has_value());
  if (!texture) {
    std::fprintf(stderr, "  reason: %s\n", error.c_str());
    return;
  }

  CHECK(texture->format == Format::Bc3);
  CHECK(texture->isCompressed());
  CHECK_EQ(texture->mips.size(), std::size_t(9));
  CHECK_EQ(texture->mips[0].width, 256u);
  CHECK_EQ(texture->mips[8].width, 1u);
  CHECK_EQ(texture->mips[8].size, std::size_t(16));  // a whole block even at 1x1
  CHECK_EQ(texture->data.size(), total);
  // The levels have to lie immediately one after another.
  CHECK_EQ(texture->mips[1].offset, texture->mips[0].size);
}

static void testUncompressedVariants() {
  struct Case {
    std::uint32_t bits;
    std::uint32_t maskR;
    std::uint32_t maskA;
    Format expected;
  };
  // 16-bit formats are expanded into B8G8R8A8 at load time: the names of packed
  // 16-bit formats in graphics APIs mean the channel order their own way, and
  // relying on a match with the DDS masks is unreliable.
  const Case cases[] = {
      {32, 0x00FF0000, 0xFF000000, Format::Bgra8},
      {16, 0x00000F00, 0x0000F000, Format::Bgra8},
      {16, 0x0000F800, 0x00000000, Format::Bgra8},
  };

  for (const Case& c : cases) {
    const auto bytes = DdsBuilder()
                           .size(4, 4)
                           .mipCount(1)
                           .rgb(c.bits, c.maskR, c.maskA)
                           .payload(levelSize(c.bits == 32 ? Format::Bgra8 : Format::Bgra4, 4, 4))
                           .build();
    std::string error;
    const auto texture = loadDds(bytes, &error);
    CHECK(texture.has_value());
    if (texture) {
      CHECK(texture->format == c.expected);
      CHECK(!texture->isCompressed());
    } else {
      std::fprintf(stderr, "  %u bits: %s\n", c.bits, error.c_str());
    }
  }
}

static void testMipCountZeroMeansOneLevel() {
  // The game contains DDS files with mipCount = 0; that is one level, not zero.
  const auto bytes = DdsBuilder().size(16, 16).mipCount(0).fourcc("DXT1").payload(128).build();
  const auto texture = loadDds(bytes);
  CHECK(texture.has_value());
  if (texture) CHECK_EQ(texture->mips.size(), std::size_t(1));
}

static void testTruncatedMipChainIsClamped() {
  // The header promises 9 levels while the data is only enough for the first: we
  // take as many as really exist instead of reading past the buffer.
  const auto bytes = DdsBuilder()
                         .size(256, 256)
                         .mipCount(9)
                         .fourcc("DXT5")
                         .payload(levelSize(Format::Bc3, 256, 256))
                         .build();
  const auto texture = loadDds(bytes);
  CHECK(texture.has_value());
  if (texture) CHECK_EQ(texture->mips.size(), std::size_t(1));
}

static void testRejects() {
  std::string error;

  CHECK(!loadDds(std::vector<std::byte>(16), &error).has_value());  // too small

  auto noMagic = DdsBuilder().size(4, 4).fourcc("DXT1").payload(8).build();
  noMagic[0] = std::byte{0};
  CHECK(!loadDds(noMagic, &error).has_value());

  const auto zeroSize = DdsBuilder().size(0, 0).fourcc("DXT1").payload(8).build();
  CHECK(!loadDds(zeroSize, &error).has_value());

  const auto unknown = DdsBuilder().size(4, 4).fourcc("ZZZZ").payload(8).build();
  CHECK(!loadDds(unknown, &error).has_value());

  const auto dx10 = DdsBuilder().size(4, 4).fourcc("DX10").payload(8).build();
  CHECK(!loadDds(dx10, &error).has_value());

  // Cube and volume textures are not supported yet — the game holds exactly one
  // such file (common/textures/watervolume.dds).
  const auto volume = DdsBuilder().size(4, 4).fourcc("DXT1").caps2(0x200000).payload(8).build();
  CHECK(!loadDds(volume, &error).has_value());

  const auto cubemap = DdsBuilder().size(4, 4).fourcc("DXT1").caps2(0x200).payload(8).build();
  CHECK(!loadDds(cubemap, &error).has_value());

  // There is a header but no data.
  const auto empty = DdsBuilder().size(256, 256).mipCount(1).fourcc("DXT5").build();
  CHECK(!loadDds(empty, &error).has_value());
}

// A single BC1 block: two endpoints and four indices.
static std::vector<std::byte> bc1Block(std::uint16_t first, std::uint16_t second,
                                       std::uint32_t indices) {
  std::vector<std::byte> out(8, std::byte{0});
  std::memcpy(out.data(), &first, 2);
  std::memcpy(out.data() + 2, &second, 2);
  std::memcpy(out.data() + 4, &indices, 4);
  return out;
}

static void testDecodeRgba8() {
  // Red and blue endpoints; index 0 everywhere, so every pixel is the
  // first endpoint. 0xF800 is 565 red, 0x001F is 565 blue.
  auto payload = bc1Block(0xF800, 0x001F, 0);
  std::vector<std::byte> file = DdsBuilder().size(4, 4).mipCount(1).fourcc("DXT1").build();
  file.insert(file.end(), payload.begin(), payload.end());

  const auto texture = loadDds(file);
  CHECK(texture.has_value());
  const auto pixels = decodeRgba8(*texture);
  CHECK_EQ(pixels.size(), std::size_t(4 * 4 * 4));
  CHECK_EQ(int(pixels[0]), 255);
  CHECK_EQ(int(pixels[1]), 0);
  CHECK_EQ(int(pixels[2]), 0);
  CHECK_EQ(int(pixels[3]), 255);

  // Index 1 in the top-left pixel gives the second endpoint instead.
  payload = bc1Block(0xF800, 0x001F, 1);
  file = DdsBuilder().size(4, 4).mipCount(1).fourcc("DXT1").build();
  file.insert(file.end(), payload.begin(), payload.end());
  const auto blue = loadDds(file);
  CHECK(blue.has_value());
  const auto bluePixels = decodeRgba8(*blue);
  CHECK_EQ(int(bluePixels[0]), 0);
  CHECK_EQ(int(bluePixels[2]), 255);

  // When the first endpoint is not greater than the second, BC1 spends
  // the fourth palette entry on transparency.
  payload = bc1Block(0x001F, 0xF800, 0xFF);
  file = DdsBuilder().size(4, 4).mipCount(1).fourcc("DXT1").build();
  file.insert(file.end(), payload.begin(), payload.end());
  const auto punch = loadDds(file);
  CHECK(punch.has_value());
  const auto punched = decodeRgba8(*punch);
  CHECK_EQ(int(punched[3]), 0);

  // A level that does not exist yields nothing rather than garbage.
  CHECK(decodeRgba8(*texture, 5).empty());
}

TEST_MAIN({
  testLevelSize();
  testDecodeRgba8();
  testDxt5WithMips();
  testUncompressedVariants();
  testMipCountZeroMeansOneLevel();
  testTruncatedMipChainIsClamped();
  testRejects();
})
