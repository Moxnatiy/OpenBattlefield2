#include "obf2/mesh/animation.h"

#include <cmath>
#include <cstring>

namespace obf2::mesh {
namespace {

// The same bounds-checked reader as in the other parsers.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> data) : data_(data) {}

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }

  void fail(std::string why) {
    if (ok_) {
      ok_ = false;
      error_ = std::move(why) + " (offset " + std::to_string(position_) + ")";
    }
  }

  std::uint32_t dword(const char* what) { return read<std::uint32_t>(what); }
  std::uint16_t word(const char* what) { return read<std::uint16_t>(what); }
  std::uint8_t byte(const char* what) { return read<std::uint8_t>(what); }

  // Reads count 16-bit channel words in one go.
  std::vector<std::int16_t> words(std::size_t count, const char* what) {
    std::vector<std::int16_t> values;
    if (!ok_) return values;
    if ((data_.size() - position_) / 2 < count) {
      fail(std::string("file truncated: ") + what);
      return values;
    }
    values.resize(count);
    if (count != 0) std::memcpy(values.data(), data_.data() + position_, count * 2);
    position_ += count * 2;
    return values;
  }

 private:
  template <typename T>
  T read(const char* what) {
    T value{};
    if (!ok_) return value;
    if (data_.size() - position_ < sizeof(T)) {
      fail(std::string("file truncated: ") + what);
      return value;
    }
    std::memcpy(&value, data_.data() + position_, sizeof(T));
    position_ += sizeof(T);
    return value;
  }

  std::span<const std::byte> data_;
  std::size_t position_ = 0;
  bool ok_ = true;
  std::string error_;
};

}  // namespace

float BoneAnimation::value(std::size_t bone, std::uint32_t frame, int channel) const {
  if (bone >= tracks.size() || channel < 0 || channel >= kAnimationChannels) return 0.0f;
  const std::vector<std::int16_t>& words = tracks[bone].channels[static_cast<std::size_t>(channel)];
  if (words.empty()) return 0.0f;

  // A walk over the runs, as in `CompressedAnim::GetValue`: while the given
  // frame does not fall inside the current run, subtract its length and move on.
  std::size_t at = 0;  // the index of the word holding the run's header
  std::uint32_t left = frame;
  const auto header = [&](std::size_t index) {
    return static_cast<std::uint8_t>(words[index] & 0xff);
  };
  const auto skip = [&](std::size_t index) {
    return static_cast<std::uint8_t>((static_cast<std::uint16_t>(words[index]) >> 8) & 0xff);
  };

  std::uint8_t flags = header(0);
  std::uint32_t length = flags & 0x7f;
  int guard = 0;
  while (length > 0 && left > length - 1) {
    left -= length;
    at += skip(at);
    if (at >= words.size() || ++guard > 4096) return 0.0f;  // a damaged stream
    flags = header(at);
    length = flags & 0x7f;
  }

  // Bit 7 means the whole run has one value.
  const std::size_t index = (flags & 0x80) != 0 ? at + 1 : at + 1 + left;
  if (index >= words.size()) return 0.0f;

  // A quaternion is always in 1/32767, a translation by the header's precision.
  const float scale = channel < 4
                          ? 1.0f / 32767.0f
                          : 1.0f / static_cast<float>((1u << (precision & 0x1f)) - 1u);
  return static_cast<float>(words[index]) * scale;
}

bool BoneAnimation::sample(std::size_t bone, std::uint32_t frame, float outRotation[4],
                           Vec3* outPosition) const {
  if (bone >= tracks.size()) return false;
  if (outRotation != nullptr) {
    for (int i = 0; i < 4; ++i) outRotation[i] = value(bone, frame, i);
  }
  if (outPosition != nullptr) {
    outPosition->x = value(bone, frame, 4);
    outPosition->y = value(bone, frame, 5);
    outPosition->z = value(bone, frame, 6);
  }
  return true;
}

bool BoneAnimation::sample(std::size_t bone, float frame, float outRotation[4],
                           Vec3* outPosition) const {
  if (bone >= tracks.size() || frameCount == 0) return false;

  // Into the clip's own length, the way a looping time is wrapped.
  const auto count = static_cast<float>(frameCount);
  float wrapped = std::fmod(frame, count);
  if (wrapped < 0.0f) wrapped += count;

  const float whole = std::floor(wrapped);
  const float fraction = wrapped - whole;
  const auto first = static_cast<std::uint32_t>(whole);
  // The frame after the last is the first again (0x6b4a54).
  const std::uint32_t second = first + 1 < frameCount ? first + 1 : 0;

  if (outRotation != nullptr) {
    float a[4], b[4];
    for (int i = 0; i < 4; ++i) {
      a[i] = value(bone, first, i);
      b[i] = value(bone, second, i);
    }
    // The two quaternions of a clip's neighbouring frames are close, so the
    // shortest path is the one to take; q and -q are the same rotation.
    float dot = 0.0f;
    for (int i = 0; i < 4; ++i) dot += a[i] * b[i];
    if (dot < 0.0f) {
      for (float& value : b) value = -value;
      dot = -dot;
    }
    float weightA = 1.0f - fraction;
    float weightB = fraction;
    if (dot < 0.9995f) {
      const float angle = std::acos(dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot));
      const float sine = std::sin(angle);
      if (sine > 1e-6f) {
        weightA = std::sin((1.0f - fraction) * angle) / sine;
        weightB = std::sin(fraction * angle) / sine;
      }
    }
    float length = 0.0f;
    for (int i = 0; i < 4; ++i) {
      outRotation[i] = a[i] * weightA + b[i] * weightB;
      length += outRotation[i] * outRotation[i];
    }
    length = std::sqrt(length);
    if (length > 1e-6f) {
      for (int i = 0; i < 4; ++i) outRotation[i] /= length;
    }
  }

  if (outPosition != nullptr) {
    const auto mix = [&](int channel) {
      const float a = value(bone, first, channel);
      const float b = value(bone, second, channel);
      return a + (b - a) * fraction;
    };
    outPosition->x = mix(4);
    outPosition->y = mix(5);
    outPosition->z = mix(6);
  }
  return true;
}

std::optional<BoneAnimation> loadBoneAnimation(std::span<const std::byte> bytes,
                                               std::string* error) {
  Reader reader(bytes);
  BoneAnimation animation;
  animation.version = reader.dword("version");
  if (reader.ok() && animation.version != 4) {
    reader.fail("unknown version " + std::to_string(animation.version));
  }

  const std::uint16_t boneCount = reader.word("bone count");
  animation.boneIds.reserve(reader.ok() ? boneCount : 0);
  for (std::uint16_t i = 0; i < boneCount && reader.ok(); ++i) {
    animation.boneIds.push_back(reader.word("bone id"));
  }

  animation.frameCount = reader.dword("frame count");
  animation.precision = reader.byte("precision");

  animation.tracks.resize(reader.ok() ? boneCount : 0);
  for (std::uint16_t i = 0; i < boneCount && reader.ok(); ++i) {
    // How many words this bone has in total — the engine uses that number to
    // allocate in one go. We need it as a check.
    const std::uint16_t total = reader.word("words per bone");
    std::uint32_t seen = 0;
    for (int channel = 0; channel < kAnimationChannels && reader.ok(); ++channel) {
      const std::uint16_t count = reader.word("words per channel");
      animation.tracks[i].channels[static_cast<std::size_t>(channel)] =
          reader.words(count, "channel values");
      seen += count;
    }
    if (reader.ok() && seen != total) {
      reader.fail("bone " + std::to_string(i) + ": words " + std::to_string(seen) + ", header says " +
                  std::to_string(total));
    }
  }

  if (!reader.ok()) {
    if (error) *error = reader.error();
    return std::nullopt;
  }
  return animation;
}

}  // namespace obf2::mesh
