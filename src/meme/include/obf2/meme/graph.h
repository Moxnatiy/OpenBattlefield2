#pragma once
// Execution of the `MemeFile` graph — the very system the engine animates the HUD with.
//
// The graph does not "describe" the animation, it **performs** it: an update
// event goes through the system every frame, nodes with conditions pass it on or
// not, and actions move named variables. That is why the corner regions travel
// rather than jump, and that is where `BottomLeft_XPos`,
// `BottomRight_alpha` and the rest come from.
//
// What comes from where (all of it from `MemeDll.dll`/`MemeBf.dll`, which export
// full C++ symbols):
//
//   `CullVariableActionNode::onEvent`  0x10004e99
//       no action — nothing; there is a "Variable" and it is zero — nothing;
//       otherwise run the action.
//   `SetVariableSoftAction::onEvent`   0x10004d2c  linearly towards the target
//   `SetVariableSineAction::onEvent`   0x10001050  the same plus braking
//   `CullNode::iterateUpdate`          0x10004a57  the show progress, In/Out time
//
// The update event has the number **0x16** — visible from the check
// `*(int *)param_4 != 0x16` at the start of both actions.
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/meme/file.h"

namespace obf2::meme {

// The step of `SetVariableSineAction::onEvent` (`MemeDll.dll`, 0x10001050):
//
//   distance = |target - value|
//   if distance >= braking:  step = speed * dt
//   else:  step = cos(1.57075 - (distance/braking) * 1.57075)
//                 * speed * dt
//   the value moves towards the target by step, but no further than it
//
// `cos(pi/2 - x)` is `sin(x)`, so near the target the step decays as a sine.
// With zero braking this is exactly `SetVariableSoftAction::onEvent`
// (0x10004d2c), that is linear movement.
void approachVariable(float& value, float target, float speed, float brakingDistance, float dt);

// The graph's variables. Everything is kept as numbers: a boolean in the graph is
// a number too, and `BoolData` is read as one byte and compared against zero.
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
  // Read the file and seed the variables with the initial values from it.
  bool load(const std::vector<std::byte>& data, std::string* error = nullptr);

  // One tick: event 0x16 with the frame's time in seconds.
  void update(float dt);

  Variables& variables() { return variables_; }
  const Variables& variables() const { return variables_; }
  const File& file() const { return file_; }

  // Evaluate a data node by index. -1 means there is nothing to compute, zero.
  float evaluate(int index) const;

  // How many actions ran on the last tick — for checks.
  int lastActions() const { return actions_; }

  // A HUD region as the file defines it. A `BfTransformNode` is the **moving**
  // region: its X and Y are data nodes, so X can be bound to a variable. Its
  // `Next node` is an ordinary `TransformNode` with constant numbers, and that
  // is the static twin of the same region.
  //
  // For `Menu/Ingame` that gives exactly four regions, which until now we kept
  // as numbers in the code:
  //
  //   BottomLeftAnimate   X = BottomLeft_XPos,  Y = 563, 400x64
  //   BottomLeftStatic    X = -1,               Y = 563, 400x64
  //   BottomRightAnimate  X = BottomRight_XPos, Y = 497, 600x100
  //   BottomRightStatic   X = 401,              Y = 563, 400x64
  struct Layer {
    std::string variable;  // the variable the moving region's X is bound to
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
  // The name of the variable an action writes into: a data node with a non-empty name.
  const Object* named(int index) const;

  File file_;
  Variables variables_;
  int actions_ = 0;
};

}  // namespace obf2::meme
