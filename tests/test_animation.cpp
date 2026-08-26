#include <cmath>
#include <cstring>
#include <vector>

#include "check.h"
#include "obf2/mesh/animation.h"
#include "obf2/mesh/skeleton.h"
#include "obf2/mesh/skinning.h"

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

namespace {

// Скелет із двох кісток: корінь і дитина на метр вище.
mesh::Skeleton makeTwoBoneSkeleton() {
  mesh::Skeleton skeleton;
  skeleton.version = 2;

  mesh::SkeletonBone root;
  root.name = "root";
  root.parent = -1;
  root.rotation[3] = 1.0f;
  skeleton.bones.push_back(root);

  mesh::SkeletonBone child;
  child.name = "child";
  child.parent = 0;
  child.rotation[3] = 1.0f;
  child.position = mesh::Vec3{0.0f, 1.0f, 0.0f};
  skeleton.bones.push_back(child);
  return skeleton;
}

}  // namespace

static void testPoseWithoutClipIsRestPose() {
  const mesh::Skeleton skeleton = makeTwoBoneSkeleton();
  const auto pose = mesh::poseSkeleton(skeleton, nullptr, 0);
  CHECK_EQ(pose.size(), std::size_t(2));
  if (pose.size() < 2) return;

  // Дитина стоїть рівно на метр вище кореня.
  CHECK(std::abs(pose[1].m[13] - 1.0f) < 0.001f);
  CHECK(std::abs(pose[1].m[12]) < 0.001f);
}

static void testClipMovesOnlyItsOwnBones() {
  // Кліп рухає лише кістку 1, кістка 0 лишається в позі спокою.
  AnimationBuilder builder(1, 1, 15);
  builder.constantChannel(1, 0);      // кватерніон x
  auto qx = builder.channel();
  builder.constantChannel(1, 0);      // y
  auto qy = builder.channel();
  builder.constantChannel(1, 0);      // z
  auto qz = builder.channel();
  builder.constantChannel(1, 32767);  // w = 1, тобто без повороту
  auto qw = builder.channel();
  builder.constantChannel(1, 0);      // зсув x
  auto px = builder.channel();
  builder.constantChannel(1, static_cast<std::int16_t>((1 << 15) - 1));  // зсув y = 1
  auto py = builder.channel();
  builder.constantChannel(1, 0);      // зсув z
  auto pz = builder.channel();
  builder.flushBone({qx, qy, qz, qw, px, py, pz});

  auto bytes = builder.bytes();
  // Номер кістки в кліпі — 1 (у збирачі перший id це 40, підміняємо).
  bytes[6] = static_cast<std::byte>(1);
  bytes[7] = static_cast<std::byte>(0);

  const auto animation = mesh::loadBoneAnimation(bytes);
  CHECK(animation.has_value());
  if (!animation) return;

  const mesh::Skeleton skeleton = makeTwoBoneSkeleton();
  const auto pose = mesh::poseSkeleton(skeleton, &*animation, 0);
  CHECK_EQ(pose.size(), std::size_t(2));
  if (pose.size() < 2) return;
  // Кліп задав дитині зсув 1 по Y — той самий, що й у позі спокою.
  CHECK(std::abs(pose[1].m[13] - 1.0f) < 0.01f);
}

static void testSkinMovesVertexWithItsBone() {
  mesh::RenderMesh bind;
  bind.vertices.resize(1);
  bind.vertices[0].position = mesh::Vec3{0.0f, 0.0f, 0.0f};
  bind.vertices[0].normal = mesh::Vec3{0.0f, 1.0f, 0.0f};
  bind.indices = {0};
  bind.skin.resize(1);
  bind.skin[0].boneA = 0;
  bind.skin[0].boneB = 0;
  bind.skin[0].weight = 1.0f;

  // Один риґ з однією кісткою, обернена прив'язка — одинична.
  mesh::Rig rig;
  mesh::Bone entry;
  entry.id = 0;
  entry.transform.m[0] = entry.transform.m[5] = entry.transform.m[10] = entry.transform.m[15] = 1.0f;
  rig.bones.push_back(entry);
  bind.rigs.push_back(rig);

  mesh::DrawRange range;
  range.indexStart = 0;
  range.indexCount = 1;
  range.rig = 0;
  bind.ranges.push_back(range);

  // Кістка зсунута на 2 по X.
  std::vector<mesh::Mat4> pose(1);
  pose[0].m[0] = pose[0].m[5] = pose[0].m[10] = pose[0].m[15] = 1.0f;
  pose[0].m[12] = 2.0f;

  mesh::RenderMesh posed = bind;
  mesh::skinMesh(bind, pose, posed);
  CHECK(std::abs(posed.vertices[0].position.x - 2.0f) < 0.001f);
  // Нормаль зсув не чіпає.
  CHECK(std::abs(posed.vertices[0].normal.y - 1.0f) < 0.001f);
}

static void testFullWeightStageClearsWhatWasBefore() {
  // Правило з рушія: кліп із вагою 1 очищає стек кістки, тобто повністю
  // володіє нею, а не змішується з попередніми.
  const mesh::Skeleton skeleton = makeTwoBoneSkeleton();

  // Два кліпи на ту саму кістку 1: перший ставить зсув y = 1, другий 0.5.
  const auto makeClip = [](std::int16_t offsetY) {
    AnimationBuilder builder(1, 1, 15);
    std::vector<std::vector<std::int16_t>> channels;
    builder.constantChannel(1, 0); channels.push_back(builder.channel());
    builder.constantChannel(1, 0); channels.push_back(builder.channel());
    builder.constantChannel(1, 0); channels.push_back(builder.channel());
    builder.constantChannel(1, 32767); channels.push_back(builder.channel());
    builder.constantChannel(1, 0); channels.push_back(builder.channel());
    builder.constantChannel(1, offsetY); channels.push_back(builder.channel());
    builder.constantChannel(1, 0); channels.push_back(builder.channel());
    builder.flushBone(channels);
    auto bytes = builder.bytes();
    bytes[6] = static_cast<std::byte>(1);  // кістка 1
    bytes[7] = static_cast<std::byte>(0);
    return bytes;
  };

  const auto full = mesh::loadBoneAnimation(makeClip(static_cast<std::int16_t>((1 << 15) - 1)));
  const auto half = mesh::loadBoneAnimation(makeClip(static_cast<std::int16_t>(((1 << 15) - 1) / 2)));
  CHECK(full.has_value() && half.has_value());
  if (!full || !half) return;

  // Спершу половинний кліп, потім повний — має лишитися саме повний.
  std::vector<mesh::PoseStage> stages;
  stages.push_back(mesh::PoseStage{&*half, 0, 0.5f});
  stages.push_back(mesh::PoseStage{&*full, 0, 1.0f});
  const auto pose = mesh::poseSkeleton(skeleton, stages);
  CHECK_EQ(pose.size(), std::size_t(2));
  if (pose.size() < 2) return;
  CHECK(std::abs(pose[1].m[13] - 1.0f) < 0.01f);
}

static void testStageTouchesOnlyItsOwnBones() {
  // Кліп рухає лише перелічені в ньому кістки — саме тому в BF2 ноги й
  // верх тіла можуть іти з різних кліпів одночасно.
  const mesh::Skeleton skeleton = makeTwoBoneSkeleton();

  AnimationBuilder builder(1, 1, 15);
  std::vector<std::vector<std::int16_t>> channels;
  builder.constantChannel(1, 0); channels.push_back(builder.channel());
  builder.constantChannel(1, 0); channels.push_back(builder.channel());
  builder.constantChannel(1, 0); channels.push_back(builder.channel());
  builder.constantChannel(1, 32767); channels.push_back(builder.channel());
  builder.constantChannel(1, static_cast<std::int16_t>((1 << 15) - 1)); channels.push_back(builder.channel());
  builder.constantChannel(1, 0); channels.push_back(builder.channel());
  builder.constantChannel(1, 0); channels.push_back(builder.channel());
  builder.flushBone(channels);
  auto bytes = builder.bytes();
  bytes[6] = static_cast<std::byte>(1);  // тільки кістка 1
  bytes[7] = static_cast<std::byte>(0);

  const auto clip = mesh::loadBoneAnimation(bytes);
  CHECK(clip.has_value());
  if (!clip) return;

  const auto pose = mesh::poseSkeleton(skeleton, std::vector<mesh::PoseStage>{
                                                     mesh::PoseStage{&*clip, 0, 1.0f}});
  CHECK_EQ(pose.size(), std::size_t(2));
  if (pose.size() < 2) return;
  // Корінь лишився в позі спокою.
  CHECK(std::abs(pose[0].m[12]) < 0.001f);
  CHECK(std::abs(pose[0].m[13]) < 0.001f);
  // А кістка 1 зсунулася по X, як задав кліп.
  CHECK(std::abs(pose[1].m[12] - 1.0f) < 0.01f);
}

TEST_MAIN({
  testFullWeightStageClearsWhatWasBefore();
  testStageTouchesOnlyItsOwnBones();
  testPoseWithoutClipIsRestPose();
  testClipMovesOnlyItsOwnBones();
  testSkinMovesVertexWithItsBone();
  testConstantRunGivesSameValueEveryFrame();
  testFrameRunGivesValuePerFrame();
  testPositionUsesPrecisionAndQuaternionDoesNot();
  testTotalWordCountIsChecked();
  testTruncatedFileDoesNotCrash();
})
