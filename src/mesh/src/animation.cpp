#include "obf2/mesh/animation.h"

#include <cstring>

namespace obf2::mesh {
namespace {

// Той самий читач із перевіркою меж, що й у решті парсерів.
class Reader {
 public:
  explicit Reader(std::span<const std::byte> data) : data_(data) {}

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }

  void fail(std::string why) {
    if (ok_) {
      ok_ = false;
      error_ = std::move(why) + " (зсув " + std::to_string(position_) + ")";
    }
  }

  std::uint32_t dword(const char* what) { return read<std::uint32_t>(what); }
  std::uint16_t word(const char* what) { return read<std::uint16_t>(what); }
  std::uint8_t byte(const char* what) { return read<std::uint8_t>(what); }

  // Читає count 16-бітних слів каналу одним шматком.
  std::vector<std::int16_t> words(std::size_t count, const char* what) {
    std::vector<std::int16_t> values;
    if (!ok_) return values;
    if ((data_.size() - position_) / 2 < count) {
      fail(std::string("файл обірвано: ") + what);
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
      fail(std::string("файл обірвано: ") + what);
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

  // Прохід по пробігах, як у `CompressedAnim::GetValue`: поки заданий кадр
  // не влучає в поточний пробіг, віднімаємо його довжину й переходимо далі.
  std::size_t at = 0;  // індекс слова, де лежить заголовок пробігу
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
    if (at >= words.size() || ++guard > 4096) return 0.0f;  // пошкоджений потік
    flags = header(at);
    length = flags & 0x7f;
  }

  // Біт 7 означає, що весь пробіг має одне значення.
  const std::size_t index = (flags & 0x80) != 0 ? at + 1 : at + 1 + left;
  if (index >= words.size()) return 0.0f;

  // Кватерніон завжди в 1/32767, зсув — за точністю з заголовка.
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

std::optional<BoneAnimation> loadBoneAnimation(std::span<const std::byte> bytes,
                                               std::string* error) {
  Reader reader(bytes);
  BoneAnimation animation;
  animation.version = reader.dword("версія");
  if (reader.ok() && animation.version != 4) {
    reader.fail("невідома версія " + std::to_string(animation.version));
  }

  const std::uint16_t boneCount = reader.word("кількість кісток");
  animation.boneIds.reserve(reader.ok() ? boneCount : 0);
  for (std::uint16_t i = 0; i < boneCount && reader.ok(); ++i) {
    animation.boneIds.push_back(reader.word("номер кістки"));
  }

  animation.frameCount = reader.dword("кількість кадрів");
  animation.precision = reader.byte("точність");

  animation.tracks.resize(reader.ok() ? boneCount : 0);
  for (std::uint16_t i = 0; i < boneCount && reader.ok(); ++i) {
    // Скільки всього слів у цієї кістки — рушій використовує це число, щоб
    // виділити пам'ять одним шматком. Нам воно потрібне для перевірки.
    const std::uint16_t total = reader.word("слів у кістки");
    std::uint32_t seen = 0;
    for (int channel = 0; channel < kAnimationChannels && reader.ok(); ++channel) {
      const std::uint16_t count = reader.word("слів у каналі");
      animation.tracks[i].channels[static_cast<std::size_t>(channel)] =
          reader.words(count, "значення каналу");
      seen += count;
    }
    if (reader.ok() && seen != total) {
      reader.fail("кістка " + std::to_string(i) + ": слів " + std::to_string(seen) + ", у заголовку " +
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
