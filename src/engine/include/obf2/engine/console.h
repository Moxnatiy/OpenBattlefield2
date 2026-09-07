#pragma once
// Консоль — диспетчер команд рушія.
//
// У Refractor 2 це окрема підсистема (`IO/Console/Console.cpp` за шляхами
// з бінаря), і вона є єдиною точкою, куди сходяться всі `.con`: налаштування,
// шаблони об'єктів, рівні, консольний ввід гравця. Тому й у нас команда з
// файлу і команда, набрана в консолі, проходять однаковим шляхом.
//
// З таблиці рядків BF2.exe відомо, що рушій знає 1735 команд
// (docs/reference/con-commands-from-exe.txt). Реалізовувати їх усі не треба —
// але треба **бачити**, які з них зустрілися й лишилися без обробника.
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

#include "obf2/con/interpreter.h"

namespace obf2::engine {

class Console {
 public:
  using Handler = std::function<void(const con::Command&)>;

  // Ім'я у форматі "ціль.метод"; регістр не має значення, як і в грі.
  void bind(std::string_view name, Handler handler);

  // true — обробник знайшовся. Невідомі команди рахуються, а не мовчки
  // ігноруються: цей лічильник і є мірою готовності порту.
  bool execute(const con::Command& command);

  // Той самий виклик, але з текстового рядка: "ціль.метод арг арг".
  // Саме так команду тримає кнопка інтерфейсу (setButtonNodeConCmd).
  bool executeLine(std::string_view line);

  std::size_t handlerCount() const { return handlers_.size(); }
  long long executedCount() const { return executed_; }
  long long unknownCount() const { return unknown_; }

  // Невідомі команди за спаданням частоти — план робіт у чистому вигляді.
  const std::map<std::string, int>& unknownCommands() const { return unknownByName_; }

  // Псевдоніми: `alias <коротко> <ціль>`.
  //
  // Це команда самого рушія, а не наша вигадка: у грі є цілий файл
  // `Settings/AliasedCommands.con` із 79 такими рядками, і саме ними в
  // консолі працюють `fps`, `hud`, `lp`, `suicide` та решта коротких
  // імен. Псевдонім не має крапки, тож у наш розбір «ціль.метод» він не
  // вкладається — його шукаємо окремо, коли обробника не знайшлося.
  //
  // Ланцюжок псевдонімів (`alias a b`, `alias b c.d`) розгортається до
  // справжньої команди; замкнене коло обривається за кількістю кроків.
  void registerAliases();
  std::size_t aliasCount() const { return aliases_.size(); }

 private:
  std::unordered_map<std::string, Handler> handlers_;
  std::unordered_map<std::string, std::string> aliases_;
  std::map<std::string, int> unknownByName_;
  long long executed_ = 0;
  long long unknown_ = 0;
};

}  // namespace obf2::engine
