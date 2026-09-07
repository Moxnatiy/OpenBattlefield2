#pragma once
// Виконання графа `MemeFile` — тієї самої системи, якою рушій анімує HUD.
//
// Граф не «описує» анімацію, він її **робить**: щокадру системою йде
// подія оновлення, вузли з умовами пропускають її далі або ні, а дії
// рухають іменовані змінні. Саме через це кутові ділянки їдуть, а не
// стрибають, і саме звідти беруться `BottomLeft_XPos`,
// `BottomRight_alpha` та решта.
//
// Що з чого (усе з `MemeDll.dll`/`MemeBf.dll`, які експортують повні
// символи C++):
//
//   `CullVariableActionNode::onEvent`  0x10004e99
//       немає дії — нічого; є «Variable» і воно нуль — нічого;
//       інакше виконати дію.
//   `SetVariableSoftAction::onEvent`   0x10004d2c  рівномірно до цілі
//   `SetVariableSineAction::onEvent`   0x10001050  те саме + гальмування
//   `CullNode::iterateUpdate`          0x10004a57  хід показу, In/Out time
//
// Подія оновлення має номер **0x16** — це видно з перевірки
// `*(int *)param_4 != 0x16` на початку обох дій.
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/meme/file.h"

namespace obf2::meme {

// Крок дії `SetVariableSineAction::onEvent` (`MemeDll.dll`, 0x10001050):
//
//   відстань = |ціль - значення|
//   якщо відстань >= гальмування:  крок = швидкість * dt
//   інакше:  крок = cos(1.57075 - (відстань/гальмування) * 1.57075)
//                   * швидкість * dt
//   значення йде до цілі на крок, але не далі за неї
//
// `cos(pi/2 - x)` — це `sin(x)`, тобто біля цілі крок згасає синусоїдою.
// З нульовим гальмуванням це рівно `SetVariableSoftAction::onEvent`
// (0x10004d2c), тобто рівномірний рух.
void approachVariable(float& value, float target, float speed, float brakingDistance, float dt);

// Змінні графа. Усе тримаємо числами: булеве в самому графі теж число,
// `BoolData` читається одним байтом і порівнюється з нулем.
class Variables {
 public:
  float get(std::string_view name) const;
  void set(std::string_view name, float value);
  bool has(std::string_view name) const;
  const std::map<std::string, float, std::less<>>& all() const { return values_; }

 private:
  std::map<std::string, float, std::less<>> values_;
};

class Graph {
 public:
  // Прочитати файл і засіяти змінні початковими значеннями з нього.
  bool load(const std::vector<std::byte>& data, std::string* error = nullptr);

  // Один такт: подія 0x16 з часом кадру в секундах.
  void update(float dt);

  Variables& variables() { return variables_; }
  const Variables& variables() const { return variables_; }
  const File& file() const { return file_; }

  // Обчислити вузол-дані за номером. -1 — нема чого рахувати, нуль.
  float evaluate(int index) const;

  // Скільки дій виконано за останній такт — для перевірок.
  int lastActions() const { return actions_; }

  // Ділянка HUD, як її задає файл. `BfTransformNode` — це **рухома**
  // ділянка: її X і Y — вузли-дані, тож X може бути прив'язаний до
  // змінної. Її ж `Next node` — звичайний `TransformNode` із сталими
  // числами, і це нерухомий двійник тієї самої ділянки.
  //
  // Для `Menu/Ingame` виходить рівно чотири ділянки, які ми доти
  // тримали числами в коді:
  //
  //   BottomLeftAnimate   X = BottomLeft_XPos,  Y = 563, 400x64
  //   BottomLeftStatic    X = -1,               Y = 563, 400x64
  //   BottomRightAnimate  X = BottomRight_XPos, Y = 497, 600x100
  //   BottomRightStatic   X = 401,              Y = 563, 400x64
  struct Layer {
    std::string variable;  // до якої змінної прив'язано X рухомої
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool hasTwin = false;
    float twinX = 0.0f;
    float twinY = 0.0f;
    float twinWidth = 0.0f;
    float twinHeight = 0.0f;
  };
  std::vector<Layer> layers() const;

 private:
  void seed();
  void walk(int index, float dt);
  void collectLayers(int index, std::vector<Layer>& out) const;
  void run(int action, float dt);
  // Ім'я змінної, у яку пише дія: це вузол-дані з непорожнім іменем.
  const Object* named(int index) const;

  File file_;
  Variables variables_;
  int actions_ = 0;
};

}  // namespace obf2::meme
