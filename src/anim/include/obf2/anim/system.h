#pragma once
// Система анімацій солдата: що саме програвати залежно від стану.
//
// Уся вона описана в даних гри — `soldiers/Common/Animations/
// AnimationSystem3p.inc` (728 рядків) і `ValueHolders.inc`. Це звичайний
// `.con`, тож читаємо його нашим же інтерпретатором:
//
//   animationSystem.createAnimation <шлях.baf>   [animationManager.looping 0]
//   animationSystem.createBundle <ім'я>
//     animationBundle.addAnimation <анімація>
//     animationBundle.fadeInTime / fadeOutTime / isLooping
//   animationSystem.createTrigger <тип> <ім'я>
//     animationTrigger.addChild <тригер>
//     animationTrigger.addBundle <бандл>
//     animationTrigger.valueHolder <діапазон>
//   AnimationSystem.createValueHolder <ім'я>
//     AnimationValueHolder.values <a> <b> <c>
//
// Тригери утворюють дерево з коренем `completeTree`. Як воно обходиться —
// див. `docs/functions/animation-system.md`; коротко: звичайний тригер
// питає дітей, потім додає свої бандли; `PoseTrigger` вибирає дитину за
// позою; `MovementTrigger` вмикається, лише коли швидкість потрапляє в
// діапазон його valueHolder.
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "obf2/con/interpreter.h"
#include "obf2/con/lexer.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::anim {

// Пози йдуть у тому ж порядку, що й діти тригера `pose` у даних, і збігаються
// з номерами поз у фізиці (`SoldierResponsePhysics::getSoldierHeight`).
enum class Pose { Stand = 0, Crouch = 1, Prone = 2, Swim = 3 };

struct Animation {
  std::string path;
  bool looping = true;
  float length = 0.0f;    // 0 = з самого файлу
  float fadeInTime = 0.0f;
};

struct Bundle {
  std::string name;
  std::vector<std::string> animations;
  float fadeInTime = 0.0f;
  float fadeOutTime = 0.0f;
  bool looping = true;
};

// Діапазон значень, за яким вмикається MovementTrigger. Перші два числа —
// межі (порядок може бути зворотним для від'ємних), третє рушій використовує
// окремо, для швидкості програвання.
struct ValueHolder {
  std::string name;
  float low = 0.0f;
  float high = 0.0f;
  float extra = 0.0f;

  bool contains(float value) const;
};

struct Trigger {
  std::string name;
  std::string type;  // Trigger, PoseTrigger, MovementTrigger, RandomTrigger ...
  std::vector<std::string> children;
  std::vector<std::string> bundles;
  std::string valueHolder;
  float fadeInTime = 0.0f;
};

// Стан гравця, за яким вибираються анімації.
struct State {
  Pose pose = Pose::Stand;
  float speed = 0.0f;  // швидкість руху, м/с
};

class System {
 public:
  // Читає скрипт (і все, що він підключає) через інтерпретатор `.con`.
  static std::optional<System> load(FileSystem& files, const std::string& scriptPath,
                                    std::string* error = nullptr);

  // Обхід дерева від кореня: які бандли грати в цьому стані.
  std::vector<const Bundle*> select(const State& state) const;

  const std::map<std::string, Animation>& animations() const { return animations_; }
  const std::map<std::string, Bundle>& bundles() const { return bundles_; }
  const std::map<std::string, Trigger>& triggers() const { return triggers_; }
  const std::map<std::string, ValueHolder>& valueHolders() const { return valueHolders_; }

  // Корені дерева — тригери, які нікому не діти.
  std::vector<std::string> roots() const;

  void feed(const con::Command& command);

 private:
  bool visit(const Trigger& trigger, const State& state, std::vector<const Bundle*>& out,
             int depth) const;

  std::map<std::string, Animation> animations_;
  std::map<std::string, Bundle> bundles_;
  std::map<std::string, Trigger> triggers_;
  std::map<std::string, ValueHolder> valueHolders_;

  // Куди йдуть наступні властивості.
  std::string activeAnimation_;
  std::string activeBundle_;
  std::string activeTrigger_;
  std::string activeValueHolder_;
};

}  // namespace obf2::anim
