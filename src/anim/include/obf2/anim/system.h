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
#include <map>
#include <optional>
#include <string>
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

// The player's state, by which the animations are chosen.
struct State {
  Pose pose = Pose::Stand;
  float speed = 0.0f;  // movement speed, m/s
};

class System {
 public:
  // Reads the script (and everything it includes) through the `.con` interpreter.
  static std::optional<System> load(FileSystem& files, const std::string& scriptPath,
                                    std::string* error = nullptr);

  // A walk of the tree from the root: which bundles to play in this state.
  std::vector<const Bundle*> select(const State& state) const;

  const std::map<std::string, Animation>& animations() const { return animations_; }
  const std::map<std::string, Bundle>& bundles() const { return bundles_; }
  const std::map<std::string, Trigger>& triggers() const { return triggers_; }
  const std::map<std::string, ValueHolder>& valueHolders() const { return valueHolders_; }

  // The tree's roots are the triggers that are nobody's child.
  std::vector<std::string> roots() const;

  void feed(const con::Command& command);

 private:
  bool visit(const Trigger& trigger, const State& state, std::vector<const Bundle*>& out,
             int depth) const;

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
