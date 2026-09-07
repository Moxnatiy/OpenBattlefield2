#pragma once
// `MemeFile 2.0` — граф, яким рушій анімує інтерфейс.
//
// Це не наш винахід і не «файл налаштувань»: у Refractor 2 весь HUD і всі
// меню — це дерево вузлів `dice::meme::*`, і воно **виконується**. Вузли
// вмикають і вимикають гілки за умовами, зсувають шари, фарбують їх і
// щокадру рухають іменовані змінні. Саме тому кутові ділянки їдуть, а
// табло напівпрозоре: це не окремий код, це файл `Menu/Ingame`.
//
// Розкладка не вгадана. `MemeDll.dll` і `MemeBf.dll` із теки мода
// експортують повні символи C++, і в кожного класу є `onStream`, який
// перелічує свої поля, передаючи назву рядком. Таблицю з них знімає
// `tools/meme_read.py --cpp` — вона лежить у `classes.inc`, і руками її
// не пишуть.
//
// Сам формат (читається `dice::meme::IStream`):
//
//   * заголовок: рядок версії, далі словник рядків — кожен із
//     однобайтовою довжиною, до **порожнього**;
//   * корінь: лише двобайтовий номер класу, без розміру
//     (`Object::loadNew`);
//   * вкладений об'єкт: чотирибайтовий **розмір**, двобайтове ім'я,
//     двобайтовий клас, далі поля (`Object::load`). Розмір міряється від
//     себе — саме ним рушій пропускає незнайоме, і саме він, а не
//     таблиця полів, тут головний;
//   * «рядок» у класовому потоці — це номер у словнику, нуль означає
//     порожньо.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obf2::meme {

// Який метод потоку читає поле. Назви — за зсувами в таблиці методів
// `IStream`; людські імена звірені з даними (див. `meme_types.py --slots`).
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
  Name,     // ресурс: однобайтова довжина, далі байти
  List,     // об'єкти підряд, край дає розмір власника
  Object,   // вкладений об'єкт
  Unknown,  // ширина невідома — далі за нього клас не читається
};

// Значення одного поля.
struct Value {
  Kind kind = Kind::Unknown;
  float number = 0.0f;
  std::int64_t integer = 0;
  std::string text;
  int object = -1;  // індекс у File::objects, -1 — порожньо
  std::vector<int> list;
};

struct Object {
  // Повне ім'я з файлу, як `dice::meme::TransformNode`.
  std::string klass;
  // Ім'я самого об'єкта, як `BottomLeft/BottomLeft_XPos`. Порожнє —
  // об'єкт безіменний, і змінної за ним не стоїть.
  std::string name;
  std::map<std::string, Value, std::less<>> fields;

  // Коротке ім'я класу, без `dice::meme::`.
  std::string_view type() const;
};

class File {
 public:
  // Прочитати. Повертає false і пише причину, якщо файл не той або
  // розбір зайшов за край.
  bool load(const std::vector<std::byte>& data, std::string* error = nullptr);

  const std::string& version() const { return version_; }
  const std::vector<Object>& objects() const { return objects_; }
  int root() const { return root_; }
  const Object* at(int index) const {
    if (index < 0 || index >= static_cast<int>(objects_.size())) return nullptr;
    return &objects_[static_cast<std::size_t>(index)];
  }

  // Поле об'єкта; nullptr — такого поля немає.
  static const Value* field(const Object& object, std::string_view name);
  // Вкладений об'єкт у полі; nullptr — поле порожнє або не об'єкт.
  const Object* child(const Object& object, std::string_view name) const;

  // Класи, яких немає в таблиці. Не помилка: розмір дозволяє їх
  // пропустити, як це робить рушій, — але знати про них корисно.
  const std::vector<std::string>& unknownClasses() const { return unknown_; }
  // Скільки байтів лишилося непрочитаними в кожному класі: таблиця
  // полів неповна саме там.
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
