#pragma once
// `MemeFile 2.0` — the graph the engine animates the interface with.
//
// This is not our invention and not a "settings file": in Refractor 2 the whole
// HUD and every menu is a tree of `dice::meme::*` nodes, and it is **executed**.
// Nodes switch branches on and off by conditions, shift layers, tint them and
// move named variables every frame. That is why the corner regions travel and
// the scoreboard is translucent: it is not separate code, it is `Menu/Ingame`.
//
// The layout is not guessed. `MemeDll.dll` and `MemeBf.dll` from the mod's
// directory export full C++ symbols, and every class has an `onStream` that
// lists its fields, passing each name as a string. The table is taken from them
// by `tools/meme_read.py --cpp` — it lives in `classes.inc` and is never
// written by hand.
//
// The format itself (read by `dice::meme::IStream`):
//
//   * the header: a version string, then a string dictionary — each with a
//     one-byte length, up to an **empty** one;
//   * the root: only a two-byte class number, no size
//     (`Object::loadNew`);
//   * a nested object: a four-byte **size**, a two-byte name, a two-byte class,
//     then the fields (`Object::load`). The size is measured from itself — it is
//     exactly what the engine skips the unknown with, and it, not the field
//     table, is what governs here;
//   * a "string" in a class stream is an index into the dictionary, and zero
//     means empty.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obf2::meme {

// Which stream method reads a field. The names follow the offsets in `IStream`'s
// method table; the human names are cross-checked against the data (see `meme_types.py --slots`).
enum class Kind {
  Ubyte,
  Sbyte,
  Ushort,
  Sshort,
  Ulong,
  Slong,
  Float,
  Bool,
  Int,
  Wchar,
  Index,
  Name,     // a resource: a one-byte length, then bytes
  List,     // objects back to back, the owner's size gives the end
  Object,   // a nested object
  Unknown,  // the width is unknown — the class is not read past it
};

// One field's value.
struct Value {
  Kind kind = Kind::Unknown;
  float number = 0.0f;
  std::int64_t integer = 0;
  std::string text;
  int object = -1;  // an index into File::objects, -1 means empty
  std::vector<int> list;
};

struct Object {
  // The full name from the file, like `dice::meme::TransformNode`.
  std::string klass;
  // The object's own name, like `BottomLeft/BottomLeft_XPos`. Empty means the
  // object is unnamed and no variable stands behind it.
  std::string name;
  std::map<std::string, Value, std::less<>> fields;

  // The short class name, without `dice::meme::`.
  std::string_view type() const;
};

class File {
 public:
  // Read it. Returns false and writes the reason when the file is the wrong one
  // or the parse ran past the end.
  bool load(const std::vector<std::byte>& data, std::string* error = nullptr);

  const std::string& version() const { return version_; }
  const std::vector<Object>& objects() const { return objects_; }
  int root() const { return root_; }
  const Object* at(int index) const {
    if (index < 0 || index >= static_cast<int>(objects_.size())) return nullptr;
    return &objects_[static_cast<std::size_t>(index)];
  }

  // An object's field; nullptr when there is no such field.
  static const Value* field(const Object& object, std::string_view name);
  // The nested object in a field; nullptr when the field is empty or not an object.
  const Object* child(const Object& object, std::string_view name) const;

  // Classes that are not in the table. Not an error: the size allows them to be
  // skipped, as the engine does — but knowing about them is useful.
  const std::vector<std::string>& unknownClasses() const { return unknown_; }
  // How many bytes were left unread in each class: that is exactly where the
  // field table is incomplete.
  const std::map<std::string, int>& shortRead() const { return short_; }

 private:
  std::string version_;
  std::vector<Object> objects_;
  std::vector<std::string> words_;
  std::vector<std::string> unknown_;
  std::map<std::string, int> short_;
  int root_ = -1;
};

}  // namespace obf2::meme
