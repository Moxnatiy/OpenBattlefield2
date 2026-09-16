#include "obf2/anim/system.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "obf2/con/interpreter.h"

namespace obf2::anim {
namespace {

// An animation's name in the script is a path, and it is referred to by a path
// too, so the key is the string as it is, only lower-cased.
std::string key(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  }
  return out;
}

}  // namespace

TriggerType triggerTypeFromName(std::string_view name) {
  // The same twelve names `TriggerManager::createTrigger` compares against
  // (`BF2.exe` 0x7fc620); an unknown one is a plain trigger, where the engine
  // instead refuses to create it at all and says "type not found".
  if (name == "PoseTrigger") return TriggerType::Pose;
  if (name == "RandomTrigger") return TriggerType::Random;
  if (name == "MessageTrigger") return TriggerType::Message;
  if (name == "SwitchMessageTrigger") return TriggerType::SwitchMessage;
  if (name == "MovementTrigger") return TriggerType::Movement;
  if (name == "ForwardTrigger") return TriggerType::Forward;
  if (name == "SideTrigger") return TriggerType::Side;
  if (name == "UpTrigger") return TriggerType::Up;
  if (name == "TurnTrigger") return TriggerType::Turn;
  if (name == "LookAroundTrigger") return TriggerType::LookAround;
  if (name == "IdleTrigger") return TriggerType::Idle;
  return TriggerType::Plain;
}

bool ValueHolder::contains(float value) const {
  // The order of the bounds in the data is arbitrary: for negative ranges they
  // are written the other way round. The engine tells them apart by the first
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
    // createTrigger <type> <name>
    activeTrigger_ = key(command.argStr(1));
    activeAnimation_.clear();
    activeBundle_.clear();
    activeValueHolder_.clear();
    Trigger trigger;
    trigger.type = std::string(first);
    trigger.kind = triggerTypeFromName(first);
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
    } else if (path == "animationtrigger.message") {
      // A plain number in the data: `animationTrigger.message 4`.
      trigger.message = static_cast<std::uint32_t>(command.argFloat(0).value_or(0.0f));
    } else if (path == "animationtrigger.idletime") {
      // Written as one argument, `5/10`, so the lexer gives it as a vector.
      if (const auto pair = command.argVec3(0)) {
        trigger.idleMin = pair->x;
        trigger.idleMax = pair->y;
      } else {
        trigger.idleMin = command.argFloat(0).value_or(trigger.idleMin);
        trigger.idleMax = command.argFloat(1).value_or(trigger.idleMax);
      }
    } else if (path == "animationtrigger.triggeronacceleration") {
      trigger.triggerOnAcceleration = command.argBool(0).value_or(false);
    } else if (path == "animationtrigger.usedirection") {
      trigger.useDirection = command.argBool(0).value_or(false);
    }
    return;
  }

  if (!activeValueHolder_.empty() && path.rfind("animationvalueholder.", 0) == 0) {
    ValueHolder& holder = valueHolders_[activeValueHolder_];
    if (path == "animationvalueholder.values") {
      holder.low = command.argFloat(0).value_or(0.0f);
      holder.high = command.argFloat(1).value_or(0.0f);
      holder.extra = command.argFloat(2).value_or(0.0f);
    } else if (path == "animationvalueholder.passonmessage") {
      holder.passOnMessage = static_cast<std::uint32_t>(command.argFloat(0).value_or(0.0f));
    } else if (path == "animationvalueholder.stoponmessage") {
      holder.stopOnMessage = static_cast<std::uint32_t>(command.argFloat(0).value_or(0.0f));
    }
  }
}

std::optional<System> System::load(FileSystem& files, const std::string& scriptPath,
                                   std::string* error) {
  if (!files.exists(scriptPath)) {
    if (error) *error = "no " + scriptPath;
    return std::nullopt;
  }

  System system;
  con::Interpreter interpreter(files,
                               [&](const con::Command& command) { system.feed(command); });
  interpreter.runFile(scriptPath);

  // The ranges lie in a separate file next to it and are included nowhere — the
  // engine reads them itself. We do the same: take the neighbouring ValueHolders.inc.
  const std::size_t slash = scriptPath.find_last_of("/\\");
  if (slash != std::string::npos) {
    const std::string neighbour = scriptPath.substr(0, slash + 1) + "ValueHolders.inc";
    if (files.exists(neighbour)) interpreter.runFile(neighbour);
  }

  if (system.triggers_.empty()) {
    if (error) *error = "the script has no triggers at all";
    return std::nullopt;
  }
  return system;
}

const ValueHolder* System::holderForBundle(std::string_view bundle) const {
  const std::string wanted = key(bundle);
  for (const auto& [name, trigger] : triggers_) {
    if (trigger.valueHolder.empty()) continue;
    bool asks = false;
    for (const std::string& asked : trigger.bundles) {
      if (key(asked) == wanted) {
        asks = true;
        break;
      }
    }
    if (!asks) continue;
    const auto holder = valueHolders_.find(key(trigger.valueHolder));
    if (holder != valueHolders_.end()) return &holder->second;
  }
  return nullptr;
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

void System::applyBundles(const Trigger& trigger, const Random& random,
                          std::vector<const Bundle*>& out) const {
  const auto push = [&](const std::string& name) {
    const auto bundle = bundles_.find(key(name));
    if (bundle != bundles_.end()) out.push_back(&bundle->second);
  };
  if (trigger.bundles.empty()) return;
  // `RandomTrigger::applyAnimations` (`BF2.exe` 0x7fefa0) plays **one** of its
  // bundles, `rand() % count`; every other trigger plays all of them (0x7fe940).
  if (trigger.kind == TriggerType::Random) {
    const std::size_t index = random ? random(trigger.bundles.size()) % trigger.bundles.size() : 0;
    push(trigger.bundles[index]);
    return;
  }
  for (const std::string& name : trigger.bundles) push(name);
}

float System::triggerValue(const Trigger& trigger, const State& state) const {
  float value = 0.0f;
  switch (trigger.kind) {
    case TriggerType::Movement: return state.speed;      // 0x7ff430
    case TriggerType::Turn: return state.turn;           // Linux 0x6cd530
    case TriggerType::Forward:                           // 0x7ff680
      value = trigger.triggerOnAcceleration ? state.second[2] : state.speed * state.direction[2];
      break;
    case TriggerType::Side:                              // Linux 0x6cd5e0
      value = trigger.triggerOnAcceleration ? state.second[0] : state.speed * state.direction[0];
      break;
    case TriggerType::Up:                                // Linux 0x6cd580
      value = trigger.triggerOnAcceleration ? state.second[1] : state.direction[1];
      break;
    default: return 0.0f;
  }
  // The flag the data spells `useDirection`: the value is taken without its sign.
  return trigger.useDirection ? std::abs(value) : value;
}

bool System::visit(const Trigger& trigger, const State& state, const Random& random,
                   std::vector<const Bundle*>& out, int depth) const {
  if (depth > 16) return true;  // a guard against a cycle in the data

  const auto childAt = [&](std::size_t index) -> const Trigger* {
    if (index >= trigger.children.size()) return nullptr;
    const auto found = triggers_.find(key(trigger.children[index]));
    return found == triggers_.end() ? nullptr : &found->second;
  };
  const auto holderOf = [&]() -> const ValueHolder* {
    if (trigger.valueHolder.empty()) return nullptr;
    const auto found = valueHolders_.find(key(trigger.valueHolder));
    return found == valueHolders_.end() ? nullptr : &found->second;
  };

  switch (trigger.kind) {
    case TriggerType::Pose: {
      // `PoseTrigger::update` (0x7fef20): the child by pose number, the last one
      // for a number past the end, and its own bundles without walking the rest.
      if (!trigger.children.empty()) {
        std::size_t index = static_cast<std::size_t>(state.pose);
        if (index >= trigger.children.size()) index = trigger.children.size() - 1;
        const Trigger* child = childAt(index);
        if (child != nullptr && !visit(*child, state, random, out, depth + 1)) return false;
      }
      applyBundles(trigger, random, out);
      return true;
    }
    case TriggerType::Message: {
      // Linux 0x6cd6d0: a mask that is not in the state's messages plays nothing.
      if (trigger.message != 0 && (state.messages & trigger.message) == 0) return true;
      break;
    }
    case TriggerType::SwitchMessage: {
      // Linux 0x6cd9e0: the child is [1] while the message is absent and [0]
      // while it is there; the bundle plays only on the tick the flag changes.
      if (trigger.children.size() >= 2) {
        const std::size_t index = (state.messages & trigger.message) == 0 ? 1 : 0;
        const Trigger* child = childAt(index);
        if (child != nullptr && !visit(*child, state, random, out, depth + 1)) return false;
      } else if (const Trigger* child = childAt(0); child != nullptr) {
        if (!visit(*child, state, random, out, depth + 1)) return false;
      }
      const bool now = (state.messages & trigger.message) != 0;
      const bool before = (state.previousMessages & trigger.message) != 0;
      if (now != before) applyBundles(trigger, random, out);
      return true;
    }
    case TriggerType::Movement:
    case TriggerType::Forward:
    case TriggerType::Side:
    case TriggerType::Up:
    case TriggerType::Turn: {
      const ValueHolder* holder = holderOf();
      if (trigger.kind == TriggerType::Movement && holder != nullptr) {
        // 0x7ff430 reads the holder's two masks before the range.
        if (holder->passOnMessage != 0 && (state.messages & holder->passOnMessage) == 0) {
          return true;
        }
        if (holder->stopOnMessage != 0 && (state.messages & holder->stopOnMessage) != 0) {
          return true;
        }
      }
      if (holder != nullptr && !holder->contains(triggerValue(trigger, state))) return true;
      break;
    }
    case TriggerType::Idle: {
      // `IdleTrigger::update` (`BF2.exe` 0x7ff0c0): with no idle time, or outside
      // its own range, the trigger stands aside entirely — it does not even walk
      // its children. When it fires it plays one random bundle and then goes on as
      // a plain trigger.
      //
      // The threshold it rolls anew each time (+0x38, `min + rand() % 100 / 100 *
      // (max - min)`) is state of the running system, which a selection has
      // nowhere to keep; here any idle time inside the range fires.
      const bool fires = state.idleTime != 0.0f && trigger.idleMax > trigger.idleMin &&
                         state.idleTime >= trigger.idleMin && state.idleTime <= trigger.idleMax;
      if (!fires) return true;
      // The engine plays one random bundle of its own here and then goes through
      // the plain path, which plays them all. In the game's data the question never
      // arises: every IdleTrigger there carries a RandomTrigger child and no
      // bundles of its own (`soldiers/Common/Animations/AnimationSystem3p.inc`,
      // `face_idle`; the weapons' `stand_idle`).
      applyBundles(trigger, random, out);
      break;
    }
    case TriggerType::Plain:
    case TriggerType::Random:
    case TriggerType::LookAround:
      break;
  }

  // `Trigger::update` (0x7feec0): every child is asked, and one that says no
  // stops this trigger; then its own bundles.
  for (const std::string& name : trigger.children) {
    const auto child = triggers_.find(key(name));
    if (child == triggers_.end()) continue;
    if (!visit(child->second, state, random, out, depth + 1)) return false;
  }
  applyBundles(trigger, random, out);
  return true;
}

std::vector<const Bundle*> System::select(const State& state, const Random& random) const {
  // A tick walks two triggers by name and no others (`AnimationSystem::update`,
  // `BF2.exe` 0x7f7390): the active one — `root`, or `startup` the first time —
  // and then `postRoot`. Everything else in the file (`hit`, `die`,
  // `specialMoves`) is reached only when something plays it by name, which is why
  // `completeTree` listing them all is not a reason to play them.
  std::vector<const Bundle*> out;
  const auto walk = [&](const char* name) {
    const auto trigger = triggers_.find(key(name));
    if (trigger != triggers_.end()) visit(trigger->second, state, random, out, 0);
  };
  if (triggers_.count(key("root")) != 0) {
    walk("root");
    walk("postRoot");
    return out;
  }
  // A system without a `root` is not the engine's shape; walking whatever roots
  // it has is ours, so that such a file still says something.
  for (const std::string& name : roots()) {
    const auto trigger = triggers_.find(key(name));
    if (trigger != triggers_.end()) visit(trigger->second, state, random, out, 0);
  }
  return out;
}

}  // namespace obf2::anim
