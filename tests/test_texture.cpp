#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/texture/dds.h"

using namespace obf2::texture;

namespace {

// Збирач DDS-заголовка (128 байт) плюс дані. Тест не залежить від наявності
// гри на диску й заодно фіксує розкладку заголовка в коді.
class DdsBuilder {
 public:
  DdsBuilder() : header_(128, std::byte{0}) {
    put32(0, 0x20534444);  // "DDS "
    put32(4, 124);         // розмір заголовка
    put32(76, 32);         // розмір DDS_PIXELFORMAT
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
  // Стиснені формати рахуються блоками 4x4 навіть на крихітних рівнях —
  // якщо цього не врахувати, поїде розбір усього ланцюжка mip-ів.
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
  // 256x256 DXT5 з повним ланцюжком до 1x1 — типова текстура гри.
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
    std::fprintf(stderr, "  причина: %s\n", error.c_str());
    return;
  }

  CHECK(texture->format == Format::Bc3);
  CHECK(texture->isCompressed());
  CHECK_EQ(texture->mips.size(), std::size_t(9));
  CHECK_EQ(texture->mips[0].width, 256u);
  CHECK_EQ(texture->mips[8].width, 1u);
  CHECK_EQ(texture->mips[8].size, std::size_t(16));  // цілий блок навіть на 1x1
  CHECK_EQ(texture->data.size(), total);
  // Рівні мають лежати впритул один за одним.
  CHECK_EQ(texture->mips[1].offset, texture->mips[0].size);
}

static void testUncompressedVariants() {
  struct Case {
    std::uint32_t bits;
    std::uint32_t maskR;
    std::uint32_t maskA;
    Format expected;
  };
  const Case cases[] = {
      {32, 0x00FF0000, 0xFF000000, Format::Bgra8},
      {16, 0x00000F00, 0x0000F000, Format::Bgra4},
      {16, 0x0000F800, 0x00000000, Format::Bgr565},
  };

  for (const Case& c : cases) {
    const auto bytes = DdsBuilder()
                           .size(4, 4)
                           .mipCount(1)
                           .rgb(c.bits, c.maskR, c.maskA)
                           .payload(levelSize(c.expected, 4, 4))
                           .build();
    std::string error;
    const auto texture = loadDds(bytes, &error);
    CHECK(texture.has_value());
    if (texture) {
      CHECK(texture->format == c.expected);
      CHECK(!texture->isCompressed());
    } else {
      std::fprintf(stderr, "  %u біт: %s\n", c.bits, error.c_str());
    }
  }
}

static void testMipCountZeroMeansOneLevel() {
  // У грі трапляються DDS із mipCount = 0; це один рівень, а не нуль.
  const auto bytes = DdsBuilder().size(16, 16).mipCount(0).fourcc("DXT1").payload(128).build();
  const auto texture = loadDds(bytes);
  CHECK(texture.has_value());
  if (texture) CHECK_EQ(texture->mips.size(), std::size_t(1));
}

static void testTruncatedMipChainIsClamped() {
  // Заголовок обіцяє 9 рівнів, а даних вистачає лише на перший: беремо
  // стільки, скільки реально є, замість читання за межами буфера.
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

  CHECK(!loadDds(std::vector<std::byte>(16), &error).has_value());  // замалий

  auto noMagic = DdsBuilder().size(4, 4).fourcc("DXT1").payload(8).build();
  noMagic[0] = std::byte{0};
  CHECK(!loadDds(noMagic, &error).has_value());

  const auto zeroSize = DdsBuilder().size(0, 0).fourcc("DXT1").payload(8).build();
  CHECK(!loadDds(zeroSize, &error).has_value());

  const auto unknown = DdsBuilder().size(4, 4).fourcc("ZZZZ").payload(8).build();
  CHECK(!loadDds(unknown, &error).has_value());

  const auto dx10 = DdsBuilder().size(4, 4).fourcc("DX10").payload(8).build();
  CHECK(!loadDds(dx10, &error).has_value());

  // Кубічні й об'ємні текстури поки не підтримуються — у грі є рівно один
  // такий файл (common/textures/watervolume.dds).
  const auto volume = DdsBuilder().size(4, 4).fourcc("DXT1").caps2(0x200000).payload(8).build();
  CHECK(!loadDds(volume, &error).has_value());

  const auto cubemap = DdsBuilder().size(4, 4).fourcc("DXT1").caps2(0x200).payload(8).build();
  CHECK(!loadDds(cubemap, &error).has_value());

  // Заголовок є, даних немає.
  const auto empty = DdsBuilder().size(256, 256).mipCount(1).fourcc("DXT5").build();
  CHECK(!loadDds(empty, &error).has_value());
}

TEST_MAIN({
  testLevelSize();
  testDxt5WithMips();
  testUncompressedVariants();
  testMipCountZeroMeansOneLevel();
  testTruncatedMipChainIsClamped();
  testRejects();
})
