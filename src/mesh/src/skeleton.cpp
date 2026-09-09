#include "obf2/mesh/skeleton.h"

#include <cstring>

namespace obf2::mesh {
namespace {

// The same approach as in the other parsers: the file comes from the user's
// archive and is not to be trusted, so every read checks its bounds.
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
  std::int16_t sword(const char* what) { return read<std::int16_t>(what); }
  float real(const char* what) { return read<float>(what); }

  std::string text(std::size_t length, const char* what) {
    if (!ok_) return {};
    if (data_.size() - position_ < length) {
      fail(std::string("file truncated: ") + what);
      return {};
    }
    const char* start = reinterpret_cast<const char*>(data_.data() + position_);
    position_ += length;
    // The length in the file counts the terminating zero — it is not put into the string.
    std::size_t used = length;
    while (used > 0 && start[used - 1] == '\0') --used;
    return std::string(start, used);
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

int Skeleton::find(std::string_view name) const {
  for (std::size_t i = 0; i < bones.size(); ++i) {
    if (bones[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

std::optional<Skeleton> loadSkeleton(std::span<const std::byte> bytes, std::string* error) {
  Reader reader(bytes);
  Skeleton skeleton;
  skeleton.version = reader.dword("version");
  const std::uint32_t count = reader.dword("bone count");

  // A sanity limit: a soldier has 80 bones, and the game's largest skeleton is
  // a flag with 40. A million means a corrupt file.
  if (reader.ok() && count > 4096) {
    reader.fail("too many bones: " + std::to_string(count));
  }

  skeleton.bones.reserve(reader.ok() ? count : 0);
  for (std::uint32_t i = 0; i < count && reader.ok(); ++i) {
    SkeletonBone bone;
    const std::uint16_t nameLength = reader.word("name length");
    bone.name = reader.text(nameLength, "bone name");

    const std::int16_t parent = reader.sword("parent");
    // -1 marks the root; any other value has to point at an already read bone,
    // otherwise the hierarchy is malformed.
    if (parent >= 0 && static_cast<std::uint32_t>(parent) >= i) {
      reader.fail("parent " + std::to_string(parent) + " on bone " + std::to_string(i));
      break;
    }
    bone.parent = parent;

    for (float& value : bone.rotation) value = reader.real("rotation");
    bone.position.x = reader.real("translation x");
    bone.position.y = reader.real("translation y");
    bone.position.z = reader.real("translation z");

    skeleton.bones.push_back(std::move(bone));
  }

  if (!reader.ok()) {
    if (error) *error = reader.error();
    return std::nullopt;
  }
  return skeleton;
}

}  // namespace obf2::mesh
