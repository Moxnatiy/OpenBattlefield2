#include <array>
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

TEST_MAIN({
  testBitOrderIsLowBitsFirst();
  testSpansByteBoundary();
  testRoundTripOfManyWidths();
  testHeaders();
  testStrings();
  testReaderRefusesToRunPastBuffer();
  testWriterRefusesToOverflow();
  testSkipBits();
})
