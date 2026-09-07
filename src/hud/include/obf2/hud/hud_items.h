#pragma once
// Значення, з яких HUD бере вміст: прапорці показу, підписи, заповнення
// смуг і прозорість.
//
// Назва не наша: в оригіналі це `Code/BF2/Menu/Hud/HudItems.cpp` (шлях
// видно в `BF2_r.exe`), і команда, якою інтерфейс міняє свої прапорці,
// зветься `hudItems.setBool`.
//
// Тут немає ні вікна, ні геометрії — самі лише значення за іменами. Саме
// тому воно й винесене: така штука перевіряється тестом.
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "obf2/engine/console.h"
#include "obf2/hud/states.h"

namespace obf2::hud {

class HudItems {
 public:
  // `hudItems.setBool <ім'я> <0|1>` — цим інтерфейс вмикає власні
  // прапорці, зокрема `SetSpawnPoint`.
  void bind(engine::Console& console);

  VariableMap& flags() { return flags_; }
  const VariableMap& flags() const { return flags_; }

  void setText(std::string name, std::string value) { text_[std::move(name)] = std::move(value); }
  void setValue(std::string name, float value) { values_[std::move(name)] = value; }
  void setAlpha(std::string name, float value) { alpha_[std::move(name)] = value; }

  std::string_view text(std::string_view name) const;
  float value(std::string_view name) const;
  // nullopt — про таку змінну ми нічого не знаємо. Вузол тоді лишається
  // видимим: більшість цих змінних — плавні згасання, і типово вони
  // ввімкнені. **Це наше рішення, а не поведінка рушія**: в оригіналі
  // значення пише сам HUD (`BF2.exe`, 0x789480 реєструє їх полями свого
  // об'єкта), і хто саме їх пише — ще не розібрано.
  std::optional<float> alpha(std::string_view name) const;

  // Чи змінилося щось відтоді, як екран малювали востаннє.
  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }
  void markDirty() { dirty_ = true; }

 private:
  VariableMap flags_;
  std::map<std::string, std::string, std::less<>> text_;
  std::map<std::string, float, std::less<>> values_;
  std::map<std::string, float, std::less<>> alpha_;
  bool dirty_ = false;
};

}  // namespace obf2::hud
