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
  // The format's key property: within a byte the low bits come first.
  // Get the order wrong and the protocol falls apart on the very first packet, so
  // this is the most important check in the whole set.
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
  // A value that does not fit into the current byte continues into the next.
  auto data = buffer(4);
  BitWriter writer(data);
  CHECK(writer.writeBits(0b111, 3));
  CHECK(writer.writeBits(0xFF, 8));  // 5 bits into the first byte, 3 into the second

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
    // The value is truncated to its width — the same as in the original.
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

  // 4+8 bits of the basic header plus 6+6+32 of the extended = 56 bits = exactly 7 bytes.
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
  CHECK(writer.writeString("ARNE", 8));  // padded with zeroes

  BitReader reader(data);
  const auto text = reader.readString(8);
  CHECK(text.has_value());
  if (text) CHECK_EQ(*text, std::string("ARNE"));

  // A too-long string is truncated to the given length.
  auto data2 = buffer(32);
  BitWriter writer2(data2);
  CHECK(writer2.writeString("ARNESON", 4));
  BitReader reader2(data2);
  const auto cut = reader2.readString(4);
  CHECK(cut.has_value());
  if (cut) CHECK_EQ(*cut, std::string("ARNE"));
}

static void testReaderRefusesToRunPastBuffer() {
  // Packets arrive from the network: running past the buffer has to give an error
  // rather than read somebody else's memory.
  const auto data = buffer(2);
  BitReader reader(data);
  CHECK(reader.readBits(16).has_value());
  CHECK(!reader.readBits(1).has_value());
  CHECK(!reader.ok());

  // After an error the stream stays broken — we do not return partial data.
  BitReader reader2(data);
  CHECK(!reader2.readBits(17).has_value());
  CHECK(!reader2.readBits(1).has_value());

  BitReader reader3(data);
  CHECK(!reader3.readBits(33).has_value());  // more than 32 bits never happens
}

static void testWriterRefusesToOverflow() {
  auto data = buffer(1);
  BitWriter writer(data);
  CHECK(writer.writeBits(0xFF, 8));
  CHECK(!writer.writeBits(1, 1));
  CHECK(!writer.ok());
  CHECK_EQ(writer.bitPosition(), std::size_t(8));  // the position did not move
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
  // A close point: the difference is small, so the highest compression level (12 bits).
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
    // Quantisation gives an error no larger than the step.
    CHECK(std::abs(decoded->x - value.x) <= precision);
    CHECK(std::abs(decoded->y - value.y) <= precision);
    CHECK(std::abs(decoded->z - value.z) <= precision);
  }

  // 2 bits of level + 3 * (1 sign bit + 11 bits of magnitude) = 38 bits.
  CHECK_EQ(writer.bitPosition(), std::size_t(38));
}

static void testCompressionLevelGrowsWithDistance() {
  const obf2::Vec3f reference{0.0f, 0.0f, 0.0f};
  const float precision = 1.0f;

  struct Case {
    float distance;
    std::size_t expectedBits;  // 2 + 3 * the table's bits
  };
  const Case cases[] = {
      {10.0f, 2 + 3 * 12},        // level 3
      {5000.0f, 2 + 3 * 16},      // level 2
      {100000.0f, 2 + 3 * 20},    // level 1
      {1000000.0f, 2 + 3 * 32},   // level 0: raw floats
  };

  for (const Case& c : cases) {
    auto data = buffer(64);
    BitWriter writer(data);
    CHECK(writer.writeCompressedVector(obf2::Vec3f{c.distance, 0.0f, 0.0f}, reference, precision));
    CHECK_EQ(writer.bitPosition(), c.expectedBits);
  }
}

static void testFarVectorKeepsFullPrecision() {
  // At level 0 an absolute position is written as raw floats, so the value has to
  // come back exactly.
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
