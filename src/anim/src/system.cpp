#include "obf2/anim/system.h"

#include <algorithm>
#include <set>

#include "obf2/con/interpreter.h"

namespace obf2::anim {
namespace {

// Ім'я анімації в скрипті — це шлях; посилаються на неї теж шляхом, тому
// ключем беремо сам рядок як є, лише в нижньому регістрі.
std::string key(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  }
  return out;
}

}  // namespace

bool ValueHolder::contains(float value) const {
  // Порядок меж у даних довільний: для від'ємних діапазонів вони записані
  // навпаки. Рушій розрізняє це за знаком першої (`isWithinRange`).
  if (low == high) return true;
  const float first = low >= 0.0f ? low : high;
  const float second = low >= 0.0f ? high : low;
  return !(value < first || second < value);
}

void System::feed(const con::Command& command) {
  const std::string& path = command.lowerPath;
  const std::string_view first = command.argStr(0);

  if (path == "animationsystem.createanimation") {
    activeAnimation_ = key(first);
    activeBundle_.clear();
    activeTrigger_.clear();
    activeValueHolder_.clear();
    Animation animation;
    animation.path = std::string(first);
    animations_[activeAnimation_] = std::move(animation);
    return;
  }
  if (path == "animationsystem.createbundle") {
    activeBundle_ = key(first);
    activeAnimation_.clear();
    activeTrigger_.clear();
    activeValueHolder_.clear();
    Bundle bundle;
    bundle.name = std::string(first);
    bundles_[activeBundle_] = std::move(bundle);
    return;
  }
  if (path == "animationsystem.createtrigger") {
    // createTrigger <тип> <ім'я>
    activeTrigger_ = key(command.argStr(1));
    activeAnimation_.clear();
    activeBundle_.clear();
    activeValueHolder_.clear();
    Trigger trigger;
    trigger.type = std::string(first);
    trigger.name = std::string(command.argStr(1));
    triggers_[activeTrigger_] = std::move(trigger);
    return;
  }
  if (path == "animationsystem.createvalueholder") {
    activeValueHolder_ = key(first);
    activeAnimation_.clear();
    activeBundle_.clear();
    activeTrigger_.clear();
    ValueHolder holder;
    holder.name = std::string(first);
    valueHolders_[activeValueHolder_] = std::move(holder);
    return;
  }

  if (!activeAnimation_.empty() && path.rfind("animationmanager.", 0) == 0) {
    Animation& animation = animations_[activeAnimation_];
    if (path == "animationmanager.looping") animation.looping = command.argBool(0).value_or(true);
    else if (path == "animationmanager.length") animation.length = command.argFloat(0).value_or(0.0f);
    else if (path == "animationmanager.fadeintime") {
      animation.fadeInTime = command.argFloat(0).value_or(0.0f);
    }
    return;
  }

  if (!activeBundle_.empty() && path.rfind("animationbundle.", 0) == 0) {
    Bundle& bundle = bundles_[activeBundle_];
    if (path == "animationbundle.addanimation") bundle.animations.emplace_back(first);
    else if (path == "animationbundle.fadeintime") {
      bundle.fadeInTime = command.argFloat(0).value_or(0.0f);
    } else if (path == "animationbundle.fadeouttime") {
      bundle.fadeOutTime = command.argFloat(0).value_or(0.0f);
    } else if (path == "animationbundle.islooping") {
      bundle.looping = command.argBool(0).value_or(true);
    }
    return;
  }

  if (!activeTrigger_.empty() && path.rfind("animationtrigger.", 0) == 0) {
    Trigger& trigger = triggers_[activeTrigger_];
    if (path == "animationtrigger.addchild") trigger.children.emplace_back(first);
    else if (path == "animationtrigger.addbundle") trigger.bundles.emplace_back(first);
    else if (path == "animationtrigger.valueholder") trigger.valueHolder = std::string(first);
    else if (path == "animationtrigger.fadeintime") {
      trigger.fadeInTime = command.argFloat(0).value_or(0.0f);
    }
    return;
  }

  if (!activeValueHolder_.empty() && path == "animationvalueholder.values") {
    ValueHolder& holder = valueHolders_[activeValueHolder_];
    holder.low = command.argFloat(0).value_or(0.0f);
    holder.high = command.argFloat(1).value_or(0.0f);
    holder.extra = command.argFloat(2).value_or(0.0f);
  }
}

std::optional<System> System::load(FileSystem& files, const std::string& scriptPath,
                                   std::string* error) {
  if (!files.exists(scriptPath)) {
    if (error) *error = "немає " + scriptPath;
    return std::nullopt;
  }

  System system;
  con::Interpreter interpreter(files,
                               [&](const con::Command& command) { system.feed(command); });
  interpreter.runFile(scriptPath);

  // Діапазони лежать окремим файлом поруч і ніде не підключаються — рушій
  // читає їх сам. Робимо так само: беремо сусідній ValueHolders.inc.
  const std::size_t slash = scriptPath.find_last_of("/\\");
  if (slash != std::string::npos) {
    const std::string neighbour = scriptPath.substr(0, slash + 1) + "ValueHolders.inc";
    if (files.exists(neighbour)) interpreter.runFile(neighbour);
  }

  if (system.triggers_.empty()) {
    if (error) *error = "у скрипті немає жодного тригера";
    return std::nullopt;
  }
  return system;
}

std::vector<std::string> System::roots() const {
  std::set<std::string> children;
  for (const auto& [name, trigger] : triggers_) {
    for (const std::string& child : trigger.children) children.insert(key(child));
  }

  std::vector<std::string> out;
  for (const auto& [name, trigger] : triggers_) {
    if (children.find(name) == children.end()) out.push_back(trigger.name);
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool System::visit(const Trigger& trigger, const State& state, std::vector<const Bundle*>& out,
                   int depth) const {
  if (depth > 16) return false;  // захист від кільця у даних

  if (trigger.type == "PoseTrigger") {
    // Дитина вибирається номером пози, а зайвий номер зводиться до останньої
    // — рівно як у `PoseTrigger::update`.
    if (trigger.children.empty()) return false;
    std::size_t index = static_cast<std::size_t>(state.pose);
    if (index >= trigger.children.size()) index = trigger.children.size() - 1;

    const auto child = triggers_.find(key(trigger.children[index]));
    if (child == triggers_.end()) return false;
    if (!visit(child->second, state, out, depth + 1)) return false;
  } else {
    // MovementTrigger вмикається лише в межах свого діапазону.
    if (trigger.type == "MovementTrigger" || trigger.type == "ForwardTrigger" ||
        trigger.type == "SideTrigger" || trigger.type == "TurnTrigger") {
      if (!trigger.valueHolder.empty()) {
        const auto holder = valueHolders_.find(key(trigger.valueHolder));
        if (holder != valueHolders_.end() && !holder->second.contains(state.speed)) return false;
      }
    }

    for (const std::string& name : trigger.children) {
      const auto child = triggers_.find(key(name));
      if (child != triggers_.end()) visit(child->second, state, out, depth + 1);
    }
  }

  // Свої бандли додаються після дітей — так само, як `Trigger::update`
  // спершу питає дітей, а потім кличе applyAnimations.
  for (const std::string& name : trigger.bundles) {
    const auto bundle = bundles_.find(key(name));
    if (bundle != bundles_.end()) out.push_back(&bundle->second);
  }
  return true;
}

std::vector<const Bundle*> System::select(const State& state) const {
  std::vector<const Bundle*> out;
  for (const std::string& name : roots()) {
    const auto trigger = triggers_.find(key(name));
    if (trigger != triggers_.end()) visit(trigger->second, state, out, 0);
  }
  return out;
}

}  // namespace obf2::anim
