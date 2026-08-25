#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "obf2/net/bitstream.h"

using namespace obf2::net;

namespace {

std::vector<std::byte> buffer(std::size_t bytes) { return std::vector<std::byte>(bytes, std::byte{0}); }

}  // namespace

static void testBitOrderIsLowBitsFirst() {
  // Ключова властивість формату: у байті молодші біти йдуть першими.
  // Якщо переплутати порядок, протокол розсиплеться на першому ж пакеті,
  // тому це найважливіша перевірка в усьому наборі.
  auto data = buffer(4);
  BitWriter writer(data);
  CHECK(writer.writeBits(0b101, 3));
  CHECK_EQ(static_cast<int>(data[0]), 0x05);

  auto data2 = buffer(4);
  BitWriter writer2(data2);
  CHECK(writer2.writeBits(1, 1));
  CHECK(writer2.writeBits(0, 1));
  CHECK(writer2.writeBits(1, 1));
  CHECK_EQ(static_cast<int>(data2[0]), 0b101);
}

static void testSpansByteBoundary() {
  // Значення, яке не влазить у поточний байт, продовжується в наступний.
  auto data = buffer(4);
  BitWriter writer(data);
  CHECK(writer.writeBits(0b111, 3));
  CHECK(writer.writeBits(0xFF, 8));  // 5 біт у перший байт, 3 у другий

  CHECK_EQ(static_cast<int>(data[0]), 0xFF);
  CHECK_EQ(static_cast<int>(data[1]), 0b111);
  CHECK_EQ(writer.bitPosition(), std::size_t(11));
  CHECK_EQ(writer.byteSize(), std::size_t(2));

  BitReader reader(data);
  CHECK_EQ(reader.readBits(3).value_or(0), 0b111u);
  CHECK_EQ(reader.readBits(8).value_or(0), 0xFFu);
}

static void testRoundTripOfManyWidths() {
  auto data = buffer(256);
  BitWriter writer(data);

  const std::uint32_t values[] = {0, 1, 2, 7, 255, 4096, 0x7FFFFFFF, 0xFFFFFFFF, 12345};
  const unsigned widths[] = {1, 1, 3, 3, 8, 13, 31, 32, 17};

  for (std::size_t i = 0; i < std::size(values); ++i) {
    CHECK(writer.writeBits(values[i], widths[i]));
  }

  BitReader reader(data);
  for (std::size_t i = 0; i < std::size(values); ++i) {
    const auto read = reader.readBits(widths[i]);
    CHECK(read.has_value());
    // Значення обрізається до своєї ширини — так само, як в оригіналі.
    const std::uint32_t expected =
        widths[i] >= 32 ? values[i] : (values[i] & ((1u << widths[i]) - 1u));
    if (read) CHECK_EQ(*read, expected);
  }
  CHECK_EQ(reader.bitPosition(), writer.bitPosition());
}

static void testHeaders() {
  auto data = buffer(16);
  BitWriter writer(data);
  CHECK(writer.writeBasicHeader(BasicHeader{static_cast<std::uint32_t>(PacketType::Data), 42}));
  CHECK(writer.writeExtendedHeader(ExtendedHeader{3, 17, 0xDEADBEEF}));

  // 4+8 біт основного плюс 6+6+32 розширеного = 56 біт = рівно 7 байтів.
  CHECK_EQ(writer.bitPosition(), std::size_t(56));

  BitReader reader(data);
  const auto basic = reader.readBasicHeader();
  CHECK(basic.has_value());
  if (basic) {
    CHECK_EQ(basic->type, static_cast<std::uint32_t>(PacketType::Data));
    CHECK_EQ(basic->subtype, 42u);
  }

  const auto extended = reader.readExtendedHeader();
  CHECK(extended.has_value());
  if (extended) {
    CHECK_EQ(extended->type, 3u);
    CHECK_EQ(extended->dataId, 17u);
    CHECK_EQ(extended->sequence, 0xDEADBEEFu);
  }
}

static void testStrings() {
  auto data = buffer(32);
  BitWriter writer(data);
  CHECK(writer.writeString("ARNE", 8));  // доповнюється нулями

  BitReader reader(data);
  const auto text = reader.readString(8);
  CHECK(text.has_value());
  if (text) CHECK_EQ(*text, std::string("ARNE"));

  // Задовгий рядок обрізається до заданої довжини.
  auto data2 = buffer(32);
  BitWriter writer2(data2);
  CHECK(writer2.writeString("ARNESON", 4));
  BitReader reader2(data2);
  const auto cut = reader2.readString(4);
  CHECK(cut.has_value());
  if (cut) CHECK_EQ(*cut, std::string("ARNE"));
}

static void testReaderRefusesToRunPastBuffer() {
  // Пакети приходять з мережі: вихід за буфер має давати помилку,
  // а не читання чужої пам'яті.
  const auto data = buffer(2);
  BitReader reader(data);
  CHECK(reader.readBits(16).has_value());
  CHECK(!reader.readBits(1).has_value());
  CHECK(!reader.ok());

  // Після помилки потік лишається зіпсованим — часткові дані не повертаємо.
  BitReader reader2(data);
  CHECK(!reader2.readBits(17).has_value());
  CHECK(!reader2.readBits(1).has_value());

  BitReader reader3(data);
  CHECK(!reader3.readBits(33).has_value());  // більше за 32 біти не буває
}

static void testWriterRefusesToOverflow() {
  auto data = buffer(1);
  BitWriter writer(data);
  CHECK(writer.writeBits(0xFF, 8));
  CHECK(!writer.writeBits(1, 1));
  CHECK(!writer.ok());
  CHECK_EQ(writer.bitPosition(), std::size_t(8));  // позиція не зрушила
}

static void testSkipBits() {
  auto data = buffer(4);
  BitWriter writer(data);
  CHECK(writer.writeBits(0b1111, 4));
  CHECK(writer.writeBits(0b1010, 4));

  BitReader reader(data);
  CHECK(reader.skipBits(4));
  CHECK_EQ(reader.readBits(4).value_or(0), 0b1010u);
  CHECK(!reader.skipBits(1000));
}

static void testCompressedVectorRoundTrip() {
  // Близька точка: різниця мала, отже найвищий рівень стиснення (12 біт).
  auto data = buffer(64);
  const obf2::Vec3f reference{100.0f, 50.0f, -20.0f};
  const obf2::Vec3f value{100.5f, 50.25f, -20.75f};
  const float precision = 0.01f;

  BitWriter writer(data);
  CHECK(writer.writeCompressedVector(value, reference, precision));

  BitReader reader(data);
  const auto decoded = reader.readCompressedVector(reference, precision);
  CHECK(decoded.has_value());
  if (decoded) {
    // Квантування дає похибку не більшу за крок.
    CHECK(std::abs(decoded->x - value.x) <= precision);
    CHECK(std::abs(decoded->y - value.y) <= precision);
    CHECK(std::abs(decoded->z - value.z) <= precision);
  }

  // 2 біти рівня + 3 * (1 біт знаку + 11 біт модуля) = 38 біт.
  CHECK_EQ(writer.bitPosition(), std::size_t(38));
}

static void testCompressionLevelGrowsWithDistance() {
  const obf2::Vec3f reference{0.0f, 0.0f, 0.0f};
  const float precision = 1.0f;

  struct Case {
    float distance;
    std::size_t expectedBits;  // 2 + 3 * біти таблиці
  };
  const Case cases[] = {
      {10.0f, 2 + 3 * 12},        // рівень 3
      {5000.0f, 2 + 3 * 16},      // рівень 2
      {100000.0f, 2 + 3 * 20},    // рівень 1
      {1000000.0f, 2 + 3 * 32},   // рівень 0: сирі float-и
  };

  for (const Case& c : cases) {
    auto data = buffer(64);
    BitWriter writer(data);
    CHECK(writer.writeCompressedVector(obf2::Vec3f{c.distance, 0.0f, 0.0f}, reference, precision));
    CHECK_EQ(writer.bitPosition(), c.expectedBits);
  }
}

static void testFarVectorKeepsFullPrecision() {
  // На рівні 0 пишеться абсолютна позиція сирими float-ами, тому
  // значення має відновитися точно.
  auto data = buffer(64);
  const obf2::Vec3f reference{0.0f, 0.0f, 0.0f};
  const obf2::Vec3f value{1234567.5f, -98765.25f, 555555.0f};

  BitWriter writer(data);
  CHECK(writer.writeCompressedVector(value, reference, 1.0f));

  BitReader reader(data);
  const auto decoded = reader.readCompressedVector(reference, 1.0f);
  CHECK(decoded.has_value());
  if (decoded) {
    CHECK_EQ(decoded->x, value.x);
    CHECK_EQ(decoded->y, value.y);
    CHECK_EQ(decoded->z, value.z);
  }
}

static void testCompressedVectorRefusesTruncatedBuffer() {
  const auto data = buffer(2);
  BitReader reader(data);
  CHECK(!reader.readCompressedVector(obf2::Vec3f{}, 1.0f).has_value());
}

TEST_MAIN({
  testBitOrderIsLowBitsFirst();
  testSpansByteBoundary();
  testRoundTripOfManyWidths();
  testHeaders();
  testStrings();
  testReaderRefusesToRunPastBuffer();
  testWriterRefusesToOverflow();
  testSkipBits();
  testCompressedVectorRoundTrip();
  testCompressionLevelGrowsWithDistance();
  testFarVectorKeepsFullPrecision();
  testCompressedVectorRefusesTruncatedBuffer();
})
