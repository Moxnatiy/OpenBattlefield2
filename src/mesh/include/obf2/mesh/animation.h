#pragma once
// Refractor 2 bone animation — the `.baf` files.
//
// The layout is taken from the engine's code (`dice::anim::BoneAnimation::load`
// and `dice::anim::CompressedAnim::GetValue` in the Linux server), not guessed:
//
//   u32 version (must be 4)
//   u16 bone count
//   u16 bone ids[count]
//   u32 frame count
//   u8  precision
//   for every bone:
//     u16 how many 16-bit words this bone has in total
//     for each of the 7 channels (quaternion x,y,z,w and translation x,y,z):
//       u16 how many words this channel has
//       then a stream of "runs" (see below)
//
// A channel's stream consists of runs. Every run takes one header word and then
// the values:
//
//   byte 0: the run's length in frames (bits 0..6); bit 7 means "constant value"
//   byte 1: how many words to the next run
//   int16 values[constant ? 1 : length]
//
// The values are converted to reals like this: a quaternion is divided by 32767,
// a translation by `(1 << precision) - 1`. A clip's length is frames / 24.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "obf2/mesh/bf2_mesh.h"

namespace obf2::mesh {

// Channels per bone: four for rotation and three for translation.
inline constexpr int kAnimationChannels = 7;
// Frames per second. In the engine the length is `frames / 24.0`.
inline constexpr float kAnimationFramesPerSecond = 24.0f;

struct BoneAnimationTrack {
  // The channel's raw words — unpacked on the fly, as in the original.
  std::vector<std::int16_t> channels[kAnimationChannels];
};

struct BoneAnimation {
  std::uint32_t version = 0;
  std::uint32_t frameCount = 0;
  std::uint8_t precision = 0;
  std::vector<std::uint16_t> boneIds;
  std::vector<BoneAnimationTrack> tracks;

  float duration() const {
    return static_cast<float>(frameCount) / kAnimationFramesPerSecond;
  }

  // One channel value at the given frame.
  float value(std::size_t bone, std::uint32_t frame, int channel) const;

  // A bone's rotation and translation at a frame.
  bool sample(std::size_t bone, std::uint32_t frame, float outRotation[4], Vec3* outPosition) const;

  // The same between two frames, which is what the engine does and what makes a
  // clip look like more than the twenty-four pictures a second it holds.
  // `BoneAnimation::getValue` (Linux server 0x6b4900) turns its time into a whole
  // frame and a fraction with `modff` (0x6b4a34): the whole goes to `+0x18`, the
  // fraction to `+0x1c`, and the frame after it to `+0x20` — **wrapping to zero**
  // past the last one (0x6b4a54), because a looping clip's last frame leads back
  // into its first. The rotation is then slerped and the translation mixed by the
  // fraction.
  //
  // `frame` may be any number: it is wrapped into the clip's own length here, the
  // way a looping animation's time is (0x6b4a0b, `modff` of the normalised time).
  bool sample(std::size_t bone, float frame, float outRotation[4], Vec3* outPosition) const;
};

std::optional<BoneAnimation> loadBoneAnimation(std::span<const std::byte> bytes,
                                               std::string* error = nullptr);

}  // namespace obf2::mesh
