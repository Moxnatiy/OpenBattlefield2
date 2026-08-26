#include "obf2/mesh/skeleton.h"

#include <cstring>

namespace obf2::mesh {
namespace {

// Той самий підхід, що й у решті парсерів: файл із архіву користувача,
// довіри йому нема, тож кожне читання перевіряє межі.
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
  std::int16_t sword(const char* what) { return read<std::int16_t>(what); }
  float real(const char* what) { return read<float>(what); }

  std::string text(std::size_t length, const char* what) {
    if (!ok_) return {};
    if (data_.size() - position_ < length) {
      fail(std::string("файл обірвано: ") + what);
      return {};
    }
    const char* start = reinterpret_cast<const char*>(data_.data() + position_);
    position_ += length;
    // Довжина в файлі рахує й завершальний нуль — у рядок його не беремо.
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

int Skeleton::find(std::string_view name) const {
  for (std::size_t i = 0; i < bones.size(); ++i) {
    if (bones[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

std::optional<Skeleton> loadSkeleton(std::span<const std::byte> bytes, std::string* error) {
  Reader reader(bytes);
  Skeleton skeleton;
  skeleton.version = reader.dword("версія");
  const std::uint32_t count = reader.dword("кількість кісток");

  // Обмеження від здорового глузду: у солдата 80 кісток, найбільший
  // скелет гри — прапор на 40. Мільйон означає пошкоджений файл.
  if (reader.ok() && count > 4096) {
    reader.fail("забагато кісток: " + std::to_string(count));
  }

  skeleton.bones.reserve(reader.ok() ? count : 0);
  for (std::uint32_t i = 0; i < count && reader.ok(); ++i) {
    SkeletonBone bone;
    const std::uint16_t nameLength = reader.word("довжина імені");
    bone.name = reader.text(nameLength, "ім'я кістки");

    const std::int16_t parent = reader.sword("батько");
    // -1 позначає корінь; будь-яке інше значення має вказувати на вже
    // прочитану кістку, інакше ієрархія некоректна.
    if (parent >= 0 && static_cast<std::uint32_t>(parent) >= i) {
      reader.fail("батько " + std::to_string(parent) + " у кістки " + std::to_string(i));
      break;
    }
    bone.parent = parent;

    for (float& value : bone.rotation) value = reader.real("поворот");
    bone.position.x = reader.real("зсув x");
    bone.position.y = reader.real("зсув y");
    bone.position.z = reader.real("зсув z");

    skeleton.bones.push_back(std::move(bone));
  }

  if (!reader.ok()) {
    if (error) *error = reader.error();
    return std::nullopt;
  }
  return skeleton;
}

}  // namespace obf2::mesh
