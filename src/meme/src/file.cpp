#include "obf2/meme/file.h"

#include <cstring>

namespace obf2::meme {
namespace {

struct FieldSpec {
  const char* name;
  Kind kind;
};

struct ClassSpec {
  const char* name;
  const FieldSpec* fields;  // закінчується записом із name == nullptr
};

// Таблиця з `classes.inc` — згенерована з бібліотек мода. Розгортаємо її
// двічі: спершу масиви полів, потім перелік класів.
// Останній запис — вартовий: класів без власних полів теж вистачає
// (FloatRefData успадковує все й не додає нічого), а порожній масив у
// C++ оголосити не можна.
#define MEME_CLASS(name) static const FieldSpec kFields_##name[] = {
#define MEME_FIELD(text, kind) {text, Kind::kind},
#define MEME_CLASS_END()          \
  { nullptr, Kind::Unknown }      \
  }                               \
  ;
#include "obf2/meme/classes.inc"
#undef MEME_CLASS
#undef MEME_FIELD
#undef MEME_CLASS_END

#define MEME_CLASS(name) {#name, kFields_##name},
#define MEME_FIELD(text, kind)
#define MEME_CLASS_END()
static const ClassSpec kClasses[] = {
#include "obf2/meme/classes.inc"
};
#undef MEME_CLASS
#undef MEME_FIELD
#undef MEME_CLASS_END

const ClassSpec* findClass(std::string_view shortName) {
  for (const ClassSpec& spec : kClasses) {
    if (shortName == spec.name) return &spec;
  }
  return nullptr;
}

// Скільки байтів читає метод потоку. Нуль — не число.
int fixedWidth(Kind kind) {
  switch (kind) {
    case Kind::Ubyte:
    case Kind::Sbyte:
    case Kind::Bool:
      return 1;
    case Kind::Ushort:
    case Kind::Sshort:
    case Kind::Wchar:
      return 2;
    case Kind::Ulong:
    case Kind::Slong:
    case Kind::Float:
    case Kind::Int:
    case Kind::Index:
      return 4;
    default:
      return 0;
  }
}

}  // namespace

std::string_view Object::type() const {
  const std::size_t last = klass.rfind(':');
  if (last == std::string::npos) return klass;
  return std::string_view(klass).substr(last + 1);
}

const Value* File::field(const Object& object, std::string_view name) {
  const auto found = object.fields.find(name);
  return found == object.fields.end() ? nullptr : &found->second;
}

const Object* File::child(const Object& object, std::string_view name) const {
  const Value* value = field(object, name);
  if (value == nullptr) return nullptr;
  return at(value->object);
}

namespace {

// Читач іде рівно так, як `dice::meme::IStream`: розмір об'єкта головніший
// за таблицю полів, і зайти за нього не можна.
class Reader {
 public:
  Reader(const std::vector<std::byte>& data, std::vector<Object>& objects,
         std::vector<std::string>& words, std::vector<std::string>& unknown,
         std::map<std::string, int>& shortRead)
      : data_(data),
        objects_(objects),
        words_(words),
        unknown_(unknown),
        short_(shortRead) {}

  bool failed() const { return failed_; }
  std::size_t position() const { return at_; }
  const std::string& error() const { return error_; }

  std::string header() {
    const std::string version = rawString();
    words_.clear();
    words_.emplace_back();
    for (;;) {
      const std::string text = rawString();
      if (text.empty() || failed_) break;
      words_.push_back(text);
    }
    return version;
  }

  // Корінь читається інакше: лише номер класу, без розміру.
  int root() {
    Object object;
    object.klass = word();
    const int index = static_cast<int>(objects_.size());
    objects_.push_back(std::move(object));
    body(index, objects_[static_cast<std::size_t>(index)].klass);
    return index;
  }

 private:
  bool need(std::size_t bytes) {
    if (at_ + bytes > data_.size()) {
      fail("файл коротший, ніж каже розмір");
      return false;
    }
    return true;
  }

  void fail(const char* why) {
    if (!failed_) {
      failed_ = true;
      error_ = why;
    }
  }

  std::uint8_t ubyte() {
    if (!need(1)) return 0;
    return static_cast<std::uint8_t>(data_[at_++]);
  }

  std::uint16_t ushort() {
    if (!need(2)) return 0;
    const std::uint16_t value = static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(data_[at_]) |
        (static_cast<std::uint16_t>(static_cast<std::uint8_t>(data_[at_ + 1])) << 8));
    at_ += 2;
    return value;
  }

  std::uint32_t ulong() {
    if (!need(4)) return 0;
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i) {
      value = (value << 8) | static_cast<std::uint8_t>(data_[at_ + static_cast<std::size_t>(i)]);
    }
    at_ += 4;
    return value;
  }

  std::string rawString() {
    const std::uint8_t length = ubyte();
    if (!need(length)) return {};
    std::string text(length, '\0');
    for (std::uint8_t i = 0; i < length; ++i) {
      text[i] = static_cast<char>(static_cast<std::uint8_t>(data_[at_ + i]));
    }
    at_ += length;
    return text;
  }

  // Рядок у класовому потоці — це номер у словнику.
  std::string word() {
    const std::uint16_t index = ushort();
    if (index == 0 || index >= words_.size()) return {};
    return words_[index];
  }

  Value value(Kind kind) {
    Value out;
    out.kind = kind;
    if (const int width = fixedWidth(kind); width > 0) {
      if (!need(static_cast<std::size_t>(width))) return out;
      std::uint32_t raw = 0;
      for (int i = width - 1; i >= 0; --i) {
        raw = (raw << 8) | static_cast<std::uint8_t>(data_[at_ + static_cast<std::size_t>(i)]);
      }
      at_ += static_cast<std::size_t>(width);
      if (kind == Kind::Float) {
        float number = 0.0f;
        std::memcpy(&number, &raw, sizeof(number));
        out.number = number;
      } else {
        out.integer = static_cast<std::int64_t>(raw);
        out.number = static_cast<float>(raw);
      }
      return out;
    }
    if (kind == Kind::Name) {
      out.text = rawString();
      return out;
    }
    if (kind == Kind::List) {
      // Лічильника в списку немає: об'єкти йдуть підряд, а край дає
      // розмір самого власника.
      while (limit_ != 0 && at_ + 8 <= limit_ && !failed_) {
        const int child = object();
        if (child >= 0) out.list.push_back(child);
      }
      return out;
    }
    if (kind == Kind::Object) {
      out.object = object();
      return out;
    }
    fail("поле невідомої ширини");
    return out;
  }

  // Вкладений об'єкт: розмір, ім'я, клас, поля.
  int object() {
    const std::size_t start = at_;
    const std::uint32_t size = ulong();
    if (failed_) return -1;
    if (size < 8 || start + size > data_.size()) {
      fail("розмір об'єкта виходить за файл");
      return -1;
    }
    Object object;
    object.name = word();
    object.klass = word();
    int index = -1;
    if (!object.klass.empty()) {
      index = static_cast<int>(objects_.size());
      objects_.push_back(std::move(object));
      const std::size_t outer = limit_;
      limit_ = start + size;
      body(index, objects_[static_cast<std::size_t>(index)].klass);
      limit_ = outer;
      // Розмір ховає неповну таблицю полів: він дозволяє пропустити
      // хвіст. Тому звіряємо, скільки лишилося непрочитаним.
      const std::size_t end = start + size;
      if (at_ < end) {
        const std::string& klass = objects_[static_cast<std::size_t>(index)].klass;
        const int left = static_cast<int>(end - at_);
        int& worst = short_[klass];
        if (left > worst) worst = left;
      }
    }
    at_ = start + size;
    return index;
  }

  void body(int index, const std::string& klass) {
    std::string_view shortName = klass;
    if (const std::size_t last = shortName.rfind(':'); last != std::string_view::npos) {
      shortName = shortName.substr(last + 1);
    }
    const ClassSpec* spec = findClass(shortName);
    if (spec == nullptr) {
      if (!klass.empty()) {
        bool seen = false;
        for (const std::string& name : unknown_) seen = seen || name == klass;
        if (!seen) unknown_.push_back(klass);
      }
      return;
    }
    for (std::size_t i = 0; spec->fields[i].name != nullptr && !failed_; ++i) {
      if (limit_ != 0 && at_ >= limit_) break;
      if (spec->fields[i].kind == Kind::Unknown) break;
      Value read = value(spec->fields[i].kind);
      objects_[static_cast<std::size_t>(index)].fields.emplace(spec->fields[i].name,
                                                               std::move(read));
    }
  }

  const std::vector<std::byte>& data_;
  std::vector<Object>& objects_;
  std::vector<std::string>& words_;
  std::vector<std::string>& unknown_;
  std::map<std::string, int>& short_;
  std::size_t at_ = 0;
  std::size_t limit_ = 0;
  bool failed_ = false;
  std::string error_;
};

}  // namespace

bool File::load(const std::vector<std::byte>& data, std::string* error) {
  objects_.clear();
  words_.clear();
  unknown_.clear();
  short_.clear();
  root_ = -1;

  Reader reader(data, objects_, words_, unknown_, short_);
  version_ = reader.header();
  if (version_.rfind("MemeFile", 0) != 0) {
    if (error != nullptr) *error = "не MemeFile: " + version_;
    return false;
  }
  root_ = reader.root();
  if (reader.failed()) {
    if (error != nullptr) *error = reader.error();
    return false;
  }
  if (reader.position() != data.size()) {
    if (error != nullptr) {
      *error = "лишилося " + std::to_string(data.size() - reader.position()) + " байтів";
    }
    return false;
  }
  return true;
}

}  // namespace obf2::meme
