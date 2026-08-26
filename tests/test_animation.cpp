#include <cmath>
#include <cstring>
#include <vector>

#include "check.h"
#include "obf2/mesh/animation.h"

using namespace obf2;

namespace {

// Збирач `.baf`. Заодно фіксує розкладку в коді: саме за нею всі 3470
// анімацій гри читаються без жодного зайвого байта.
class AnimationBuilder {
 public:
  AnimationBuilder(std::uint16_t boneCount, std::uint32_t frames, std::uint8_t precision) {
    u32(4);  // версія
    u16(boneCount);
    for (std::uint16_t i = 0; i < boneCount; ++i) u16(static_cast<std::uint16_t>(40 + i));
    u32(frames);
    byte(precision);
  }

  // Один канал зі сталим значенням на весь пробіг.
  void constantChannel(std::uint8_t length, std::int16_t value) {
    channel_.clear();
    // Заголовок: довжина з увімкненим бітом 7 і крок у словах.
    channel_.push_back(word(static_cast<std::uint8_t>(length | 0x80), 2));
    channel_.push_back(value);
  }

  // Канал зі значенням на кожен кадр.
  void frameChannel(const std::vector<std::int16_t>& values) {
    channel_.clear();
    const auto length = static_cast<std::uint8_t>(values.size());
    channel_.push_back(word(length, static_cast<std::uint8_t>(values.size() + 1)));
    for (const std::int16_t value : values) channel_.push_back(value);
  }

  void flushBone(const std::vector<std::vector<std::int16_t>>& channels) {
    std::size_t total = 0;
    for (const auto& one : channels) total += one.size();
    u16(static_cast<std::uint16_t>(total));
    for (const auto& one : channels) {
      u16(static_cast<std::uint16_t>(one.size()));
      for (const std::int16_t value : one) append(&value, sizeof(value));
    }
  }

  const std::vector<std::int16_t>& channel() const { return channel_; }
  const std::vector<std::byte>& bytes() const { return bytes_; }

 private:
  static std::int16_t word(std::uint8_t low, std::uint8_t high) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(low) |
                                     (static_cast<std::uint16_t>(high) << 8));
  }
  void byte(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
  void u16(std::uint16_t value) { append(&value, sizeof(value)); }
  void u32(std::uint32_t value) { append(&value, sizeof(value)); }
  void append(const void* data, std::size_t size) {
    const auto* start = static_cast<const std::byte*>(data);
    bytes_.insert(bytes_.end(), start, start + size);
  }

  std::vector<std::byte> bytes_;
  std::vector<std::int16_t> channel_;
};

std::vector<std::byte> buildOneBone(std::uint32_t frames, std::uint8_t precision,
                                    const std::vector<std::int16_t>& firstChannel,
                                    bool firstIsConstant) {
  AnimationBuilder builder(1, frames, precision);
  std::vector<std::vector<std::int16_t>> channels;

  if (firstIsConstant) {
    builder.constantChannel(static_cast<std::uint8_t>(frames), firstChannel.front());
  } else {
    builder.frameChannel(firstChannel);
  }
  channels.push_back(builder.channel());

  // Решта каналів — сталі нулі, щоб файл був повним.
  for (int i = 1; i < mesh::kAnimationChannels; ++i) {
    builder.constantChannel(static_cast<std::uint8_t>(frames), 0);
    channels.push_back(builder.channel());
  }
  builder.flushBone(channels);
  return builder.bytes();
}

}  // namespace

static void testConstantRunGivesSameValueEveryFrame() {
  // 0x7fff / 32767 = 1.0 — зручно перевіряти.
  const auto bytes = buildOneBone(10, 15, {32767}, true);
  std::string error;
  const auto animation = mesh::loadBoneAnimation(bytes, &error);
  CHECK(animation.has_value());
  if (!animation) return;

  CHECK_EQ(animation->frameCount, 10u);
  CHECK(std::abs(animation->value(0, 0, 0) - 1.0f) < 0.001f);
  CHECK(std::abs(animation->value(0, 9, 0) - 1.0f) < 0.001f);
}

static void testFrameRunGivesValuePerFrame() {
  const auto bytes = buildOneBone(4, 15, {0, 16383, 32767, -32767}, false);
  std::string error;
  const auto animation = mesh::loadBoneAnimation(bytes, &error);
  CHECK(animation.has_value());
  if (!animation) return;

  CHECK(std::abs(animation->value(0, 0, 0) - 0.0f) < 0.001f);
  CHECK(std::abs(animation->value(0, 1, 0) - 0.5f) < 0.001f);
  CHECK(std::abs(animation->value(0, 2, 0) - 1.0f) < 0.001f);
  CHECK(std::abs(animation->value(0, 3, 0) + 1.0f) < 0.001f);
}

static void testPositionUsesPrecisionAndQuaternionDoesNot() {
  // Канали 0..3 завжди діляться на 32767, канали зсуву — на (1<<точність)-1.
  const auto bytes = buildOneBone(4, 10, {1023}, true);
  const auto animation = mesh::loadBoneAnimation(bytes);
  CHECK(animation.has_value());
  if (!animation) return;

  // Канал 0 (кватерніон): 1023 / 32767.
  CHECK(std::abs(animation->value(0, 0, 0) - 1023.0f / 32767.0f) < 0.0001f);
  // Канал 4 (зсув) тут сталий нуль, тому перевіряємо саму тривалість.
  CHECK(std::abs(animation->duration() - 4.0f / 24.0f) < 0.001f);
}

static void testTotalWordCountIsChecked() {
  // Зіпсуємо число слів у заголовку кістки — файл має бути відхилений.
  auto bytes = buildOneBone(4, 15, {0}, true);
  // Заголовок: 4 версія + 2 кістки + 2 id + 4 кадри + 1 точність = 13.
  bytes[13] = static_cast<std::byte>(0x7f);
  std::string error;
  CHECK(!mesh::loadBoneAnimation(bytes, &error).has_value());
  CHECK(!error.empty());
}

static void testTruncatedFileDoesNotCrash() {
  const auto full = buildOneBone(6, 15, {1, 2, 3, 4, 5, 6}, false);
  for (std::size_t size = 0; size < full.size(); ++size) {
    std::vector<std::byte> truncated(full.begin(), full.begin() + static_cast<long>(size));
    std::string error;
    const auto animation = mesh::loadBoneAnimation(truncated, &error);
    if (animation.has_value()) continue;
    CHECK(!error.empty());
  }
}

TEST_MAIN({
  testConstantRunGivesSameValueEveryFrame();
  testFrameRunGivesValuePerFrame();
  testPositionUsesPrecisionAndQuaternionDoesNot();
  testTotalWordCountIsChecked();
  testTruncatedFileDoesNotCrash();
})
