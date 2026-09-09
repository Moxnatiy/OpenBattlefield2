#include <cmath>
#include <cstring>
#include <vector>

#include "check.h"
#include "obf2/mesh/animation.h"
#include "obf2/mesh/skeleton.h"
#include "obf2/mesh/skinning.h"

using namespace obf2;

namespace {

// A `.baf` builder. It also fixes the layout in code: it is by this layout that all
// 3470 of the game's animations read with not a single byte left over.
class AnimationBuilder {
 public:
  AnimationBuilder(std::uint16_t boneCount, std::uint32_t frames, std::uint8_t precision) {
    u32(4);  // version
    u16(boneCount);
    for (std::uint16_t i = 0; i < boneCount; ++i) u16(static_cast<std::uint16_t>(40 + i));
    u32(frames);
    byte(precision);
  }

  // One channel with a constant value over the whole run.
  void constantChannel(std::uint8_t length, std::int16_t value) {
    channel_.clear();
    // The header: the length with bit 7 set and the step in words.
    channel_.push_back(word(static_cast<std::uint8_t>(length | 0x80), 2));
    channel_.push_back(value);
  }

  // A channel with a value per frame.
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

  // The other channels are constant zeroes, so the file is complete.
  for (int i = 1; i < mesh::kAnimationChannels; ++i) {
    builder.constantChannel(static_cast<std::uint8_t>(frames), 0);
    channels.push_back(builder.channel());
  }
  builder.flushBone(channels);
  return builder.bytes();
}

}  // namespace

static void testConstantRunGivesSameValueEveryFrame() {
  // 0x7fff / 32767 = 1.0 — convenient to check.
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
  // Channels 0..3 are always divided by 32767, the translation channels by (1<<precision)-1.
  const auto bytes = buildOneBone(4, 10, {1023}, true);
  const auto animation = mesh::loadBoneAnimation(bytes);
  CHECK(animation.has_value());
  if (!animation) return;

  // Channel 0 (the quaternion): 1023 / 32767.
  CHECK(std::abs(animation->value(0, 0, 0) - 1023.0f / 32767.0f) < 0.0001f);
  // Channel 4 (translation) is a constant zero here, so we check the length itself.
  CHECK(std::abs(animation->duration() - 4.0f / 24.0f) < 0.001f);
}

static void testTotalWordCountIsChecked() {
  // Corrupt the bone header's word count — the file has to be rejected.
  auto bytes = buildOneBone(4, 15, {0}, true);
  // The header: 4 version + 2 bones + 2 ids + 4 frames + 1 precision = 13.
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

// A skeleton of two bones: the root and a child a metre above.
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

  // The child stands exactly a metre above the root.
  CHECK(std::abs(pose[1].m[13] - 1.0f) < 0.001f);
  CHECK(std::abs(pose[1].m[12]) < 0.001f);
}

static void testClipMovesOnlyItsOwnBones() {
  // The clip moves only bone 1; bone 0 stays in the rest pose.
  AnimationBuilder builder(1, 1, 15);
  builder.constantChannel(1, 0);      // quaternion x
  auto qx = builder.channel();
  builder.constantChannel(1, 0);      // y
  auto qy = builder.channel();
  builder.constantChannel(1, 0);      // z
  auto qz = builder.channel();
  builder.constantChannel(1, 32767);  // w = 1, that is no rotation
  auto qw = builder.channel();
  builder.constantChannel(1, 0);      // translation x
  auto px = builder.channel();
  builder.constantChannel(1, static_cast<std::int16_t>((1 << 15) - 1));  // translation y = 1
  auto py = builder.channel();
  builder.constantChannel(1, 0);      // translation z
  auto pz = builder.channel();
  builder.flushBone({qx, qy, qz, qw, px, py, pz});

  auto bytes = builder.bytes();
  // The bone's id in the clip is 1 (in the builder the first id is 40, we substitute).
  bytes[6] = static_cast<std::byte>(1);
  bytes[7] = static_cast<std::byte>(0);

  const auto animation = mesh::loadBoneAnimation(bytes);
  CHECK(animation.has_value());
  if (!animation) return;

  const mesh::Skeleton skeleton = makeTwoBoneSkeleton();
  const auto pose = mesh::poseSkeleton(skeleton, &*animation, 0);
  CHECK_EQ(pose.size(), std::size_t(2));
  if (pose.size() < 2) return;
  // The clip gave the child a translation of 1 along Y — the same as in the rest pose.
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

  // One rig with one bone, the inverse bind being the identity.
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

  // The bone is shifted by 2 along X.
  std::vector<mesh::Mat4> pose(1);
  pose[0].m[0] = pose[0].m[5] = pose[0].m[10] = pose[0].m[15] = 1.0f;
  pose[0].m[12] = 2.0f;

  mesh::RenderMesh posed = bind;
  mesh::skinMesh(bind, pose, posed);
  CHECK(std::abs(posed.vertices[0].position.x - 2.0f) < 0.001f);
  // A translation does not touch the normal.
  CHECK(std::abs(posed.vertices[0].normal.y - 1.0f) < 0.001f);
}

static void testFullWeightStageClearsWhatWasBefore() {
  // The engine's rule: a clip with weight 1 clears the bone's stack, that is owns it
  // entirely rather than blending with the previous ones.
  const mesh::Skeleton skeleton = makeTwoBoneSkeleton();

  // Two clips on the same bone 1: the first sets translation y = 1, the second 0.5.
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
    bytes[6] = static_cast<std::byte>(1);  // bone 1
    bytes[7] = static_cast<std::byte>(0);
    return bytes;
  };

  const auto full = mesh::loadBoneAnimation(makeClip(static_cast<std::int16_t>((1 << 15) - 1)));
  const auto half = mesh::loadBoneAnimation(makeClip(static_cast<std::int16_t>(((1 << 15) - 1) / 2)));
  CHECK(full.has_value() && half.has_value());
  if (!full || !half) return;

  // The half clip first, then the full one — the full one has to remain.
  std::vector<mesh::PoseStage> stages;
  stages.push_back(mesh::PoseStage{&*half, 0, 0.5f});
  stages.push_back(mesh::PoseStage{&*full, 0, 1.0f});
  const auto pose = mesh::poseSkeleton(skeleton, stages);
  CHECK_EQ(pose.size(), std::size_t(2));
  if (pose.size() < 2) return;
  CHECK(std::abs(pose[1].m[13] - 1.0f) < 0.01f);
}

static void testStageTouchesOnlyItsOwnBones() {
  // A clip moves only the bones listed in it — which is exactly why in BF2 the legs
  // and the upper body can come from different clips at once.
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
  bytes[6] = static_cast<std::byte>(1);  // bone 1 only
  bytes[7] = static_cast<std::byte>(0);

  const auto clip = mesh::loadBoneAnimation(bytes);
  CHECK(clip.has_value());
  if (!clip) return;

  const auto pose = mesh::poseSkeleton(skeleton, std::vector<mesh::PoseStage>{
                                                     mesh::PoseStage{&*clip, 0, 1.0f}});
  CHECK_EQ(pose.size(), std::size_t(2));
  if (pose.size() < 2) return;
  // The root stayed in the rest pose.
  CHECK(std::abs(pose[0].m[12]) < 0.001f);
  CHECK(std::abs(pose[0].m[13]) < 0.001f);
  // While bone 1 shifted along X, as the clip said.
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
