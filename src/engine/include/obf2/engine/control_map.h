#pragma once
// Керування: `Settings/Controls.con`.
//
// Це найбільший блок стартових команд — 232 виклики з 314, тому без нього
// «повноцінний запуск» неможливий. Формат такий:
//
//   ControlMap.create PlayerInputControlMap
//   ControlMap.addKeyToTriggerMapping c_PIFire IDFMouse IDMouseButton0
//   ControlMap.addAxisToAxisMapping   c_PIMouseLookX IDFMouse IDAxis0
//   ControlMap.mouseSensitivity 0.15
//
// Перший аргумент — дія рушія (`c_PI*`), далі пристрій (`IDFMouse`,
// `IDFKeyboard`) і його елемент. Імена лишаємо як в оригіналі: користувач
// редагує ці файли сам, і вони мають лишатися сумісними.
#include <string>
#include <unordered_map>
#include <vector>

#include "obf2/engine/console.h"

namespace obf2::engine {

// Чим саме керує прив'язка. Назви відповідають командам гри.
enum class MappingKind {
  KeyToTrigger,      // клавіша -> дія (стріляти, присісти)
  ButtonToTrigger,   // кнопка пристрою -> дія
  AxisToTrigger,     // вісь -> дія
  AxisToAxis,        // вісь -> вісь (миша на огляд)
  KeysToAxis,        // пара клавіш -> вісь (W/S на рух уперед-назад)
};

struct Mapping {
  MappingKind kind = MappingKind::KeyToTrigger;
  std::string action;   // c_PIFire, c_PIMouseLookX ...
  std::string device;   // IDFMouse, IDFKeyboard ...
  std::vector<std::string> elements;  // IDMouseButton0, IDKey_W ...
};

class ControlMap {
 public:
  // Реєструє обробники ControlMap.* у консолі.
  void bind(Console& console);

  const std::vector<std::string>& maps() const { return maps_; }
  const std::vector<Mapping>& mappings() const { return mappings_; }
  float mouseSensitivity() const { return mouseSensitivity_; }
  bool mouseInvert() const { return mouseInvert_; }

  // Усі прив'язки заданої дії — їх може бути кілька (клавіша плюс кнопка).
  std::vector<const Mapping*> forAction(std::string_view action) const;

  std::size_t size() const { return mappings_.size(); }

 private:
  void add(MappingKind kind, const con::Command& command);

  std::vector<std::string> maps_;  // ControlMap.create
  std::vector<Mapping> mappings_;
  float mouseSensitivity_ = 0.15f;
  bool mouseInvert_ = false;
};

}  // namespace obf2::engine
