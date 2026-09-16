#pragma once
// The soldier animation system: what to play depending on the state.
//
// All of it is described in the game's data — `soldiers/Common/Animations/
// AnimationSystem3p.inc` (728 lines) and `ValueHolders.inc`. It is ordinary
// `.con`, so we read it with our own interpreter:
//
//   animationSystem.createAnimation <path.baf>   [animationManager.looping 0]
//   animationSystem.createBundle <name>
//     animationBundle.addAnimation <animation>
//     animationBundle.fadeInTime / fadeOutTime / isLooping
//   animationSystem.createTrigger <type> <name>
//     animationTrigger.addChild <trigger>
//     animationTrigger.addBundle <bundle>
//     animationTrigger.valueHolder <range>
//   AnimationSystem.createValueHolder <name>
//     AnimationValueHolder.values <a> <b> <c>
//
// The triggers form a tree rooted at `completeTree`. How it is walked is in
// `docs/functions/animation-system.md`; in short: an ordinary trigger asks its
// children and then adds its own bundles; a `PoseTrigger` picks a child by
// pose; a `MovementTrigger` fires only when the speed falls inside its
// valueHolder's range.
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/con/interpreter.h"
#include "obf2/con/lexer.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::anim {

// The poses come in the same order as the children of the `pose` trigger in the
// data, and match the pose numbers in the physics (`SoldierResponsePhysics::getSoldierHeight`).
enum class Pose { Stand = 0, Crouch = 1, Prone = 2, Swim = 3 };

struct Animation {
  std::string path;
  bool looping = true;
  float length = 0.0f;    // 0 = from the file itself
  float fadeInTime = 0.0f;
};

struct Bundle {
  std::string name;
  std::vector<std::string> animations;
  float fadeInTime = 0.0f;
  float fadeOutTime = 0.0f;
  bool looping = true;
};

// The value range a MovementTrigger fires on. The first two numbers are the
// bounds (the order may be reversed for negative ones), and the engine uses the
// third separately, for the playback speed.
//
// The holder also carries two message masks, which `MovementTrigger::update`
// reads before the range (`BF2.exe` 0x7ff430, the fields at +0x10 and +0x0c):
// `AnimationValueHolder.passOnMessage` has to be present in the state's messages
// and `stopOnMessage` must not be.
// The one file every animation system's ranges come from. The engine holds it as
// `dice::anim::valueHolderFilename` and loads it once, before any script
// (`ValueHolderManager::loadValueHolders`, Linux server 0x6d1700, called from
// `AnimationSystemTemplate::loadScript` at 0x6b339f).
inline constexpr const char* kValueHolderFile =
    "Objects/Soldiers/Common/Animations/ValueHolders.inc";

struct ValueHolder {
  std::string name;
  float low = 0.0f;
  float high = 0.0f;
  float extra = 0.0f;
  std::uint32_t passOnMessage = 0;
  std::uint32_t stopOnMessage = 0;

  bool contains(float value) const;
};

// The twelve types `TriggerManager::createTrigger` knows (`BF2.exe` 0x7fc620).
// What each one tests is in docs/functions/animation-system.md.
enum class TriggerType {
  Plain,          // Trigger
  Pose,           // PoseTrigger
  Random,         // RandomTrigger — plays one of its bundles, not all
  Message,        // MessageTrigger
  SwitchMessage,  // SwitchMessageTrigger
  Movement,       // MovementTrigger — the speed
  Forward,        // ForwardTrigger — speed * direction.z
  Side,           // SideTrigger — speed * direction.x
  Up,             // UpTrigger — direction.y
  Turn,           // TurnTrigger — the turn value
  LookAround,     // LookAroundTrigger — nothing of its own in `update`
  Idle,           // IdleTrigger
};

TriggerType triggerTypeFromName(std::string_view name);

struct Trigger {
  std::string name;
  std::string type;  // as the data spells it
  TriggerType kind = TriggerType::Plain;
  std::vector<std::string> children;
  std::vector<std::string> bundles;
  std::string valueHolder;
  float fadeInTime = 0.0f;
  // `animationTrigger.message` — the mask a Message or SwitchMessage trigger
  // watches (the trigger's +0x30).
  std::uint32_t message = 0;
  // `animationTrigger.idleTime <min>/<max>`, an IdleTrigger's two numbers. The
  // engine's own defaults are 5 and 10 (the constructor, `BF2.exe` 0x7ffb50).
  float idleMin = 5.0f;
  float idleMax = 10.0f;
  // The trigger's +0x34 and +0x35. The names in the data are the other way round
  // from what they do: `triggerOnAcceleration` switches the direction triggers to
  // the state's second vector, `useDirection` makes them take the absolute value
  // (the client's own `.con` writer, 0x7ff330, against the readers at 0x7ff680).
  bool triggerOnAcceleration = false;
  bool useDirection = false;
};

// The state the conditions read — the animation system's own fields, as
// `setTriggerMovement` and the messages fill them.
struct State {
  Pose pose = Pose::Stand;
  float speed = 0.0f;  // movement speed, m/s
  // A unit vector in the soldier's own frame: x to the side, y up, z forward.
  float direction[3] = {0.0f, 0.0f, 1.0f};
  // The state's second vector, read instead of `speed * direction` by a trigger
  // with `triggerOnAcceleration`. Only the ladder and the first-person turns use
  // it in the game's data.
  float second[3] = {0.0f, 0.0f, 0.0f};
  float turn = 0.0f;
  // The message masks of this tick and of the one before. Who raises them is not
  // established; in the data they are plain numbers (`animationTrigger.message 4`).
  std::uint32_t messages = 0;
  std::uint32_t previousMessages = 0;
  // How long the soldier has been idle. Zero switches the IdleTrigger off, which
  // is what an unknown idle time should do.
  float idleTime = 0.0f;
};

class System {
 public:
  // Reads the script (and everything it includes) through the `.con` interpreter.
  static std::optional<System> load(FileSystem& files, const std::string& scriptPath,
                                    std::string* error = nullptr);

  // A walk of the tree from the root: which bundles to play in this state.
  //
  // `random` is what a RandomTrigger and an IdleTrigger pick with: the engine
  // calls `rand()`, and a caller that wants the same answer twice (a test, a
  // screenshot) passes its own. Its argument is the number of choices.
  using Random = std::function<std::size_t(std::size_t)>;
  std::vector<const Bundle*> select(const State& state, const Random& random = {}) const;

  const std::map<std::string, Animation>& animations() const { return animations_; }
  const std::map<std::string, Bundle>& bundles() const { return bundles_; }
  // The value holder of the trigger that asks for this bundle, or null. The
  // holder's third number is the speed a movement bundle's clips were animated
  // at (`MovementTrigger::applyAnimations`, `BF2.exe` 0x7ff490).
  const ValueHolder* holderForBundle(std::string_view bundle) const;

  const std::map<std::string, Trigger>& triggers() const { return triggers_; }
  const std::map<std::string, ValueHolder>& valueHolders() const { return valueHolders_; }

  // The tree's roots are the triggers that are nobody's child.
  std::vector<std::string> roots() const;

  void feed(const con::Command& command);

 private:
  // One trigger's `update`: false is the engine's "my child said no", which stops
  // the parent. Everything else — a condition that does not hold — answers true
  // and simply plays nothing.
  bool visit(const Trigger& trigger, const State& state, const Random& random,
             std::vector<const Bundle*>& out, int depth) const;
  void applyBundles(const Trigger& trigger, const Random& random,
                    std::vector<const Bundle*>& out) const;
  // The value a direction trigger compares against its range.
  float triggerValue(const Trigger& trigger, const State& state) const;

  std::map<std::string, Animation> animations_;
  std::map<std::string, Bundle> bundles_;
  std::map<std::string, Trigger> triggers_;
  std::map<std::string, ValueHolder> valueHolders_;

  // Where the following properties go.
  std::string activeAnimation_;
  std::string activeBundle_;
  std::string activeTrigger_;
  std::string activeValueHolder_;
};

}  // namespace obf2::anim
