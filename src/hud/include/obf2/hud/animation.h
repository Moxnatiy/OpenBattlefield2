#pragma once
// The appearing and disappearing of HUD nodes — what makes it feel alive.
//
// In the game this is not separate code but the same MemeFile graph as the menu.
// That is visible straight from the builder (`Menu/Bf2HudBuilder.cpp`, at
// 0x79b2c0 and 0x79db80): every effect command looks for an already existing
// node `<name>CullNode` and attaches a new graph node to it, named
// `<name>MoveEffect` or `<name>AlphaShowEffect`. The movement node's class is
// called `dice::meme::Bf2MoveEffect` (RTTI at 0x937a44), and beside it lies a
// separate `dice::meme::Bf2SinMoveEffect` — so ordinary movement is precisely
// linear, and the sine curve is a different, separate class.
//
// So here: the cull node gives us "show or not", and the effect stretches that
// transition over time — `setNodeInTime` seconds in and `setNodeOutTime`
// seconds out.
//
// The direction of movement is taken from the code, not from reasoning:
// `dice::meme::MoveEffect::picturePaint` (`MemeDll.dll`, 0x10001b27)
// computes the offset as **(-cos a, +sin a) * length * (1 - progress)**.
//
// It used to hold `(+cos a, -sin a)`, derived from the reasoning "the voting
// panel has to drive in from below". The signs turned out to be the opposite, so
// every element flew in from the wrong side.
//
// The show progress is driven by `dice::meme::CullNode::iterateUpdate`
// (`MemeDll.dll`, 0x10004a57), and it is **linear**: `progress += dt / "In time"`
// while showing and `progress -= dt / "Out time"` while hiding. The `In time`/
// `Out time` fields belong to `CullNode` itself — `setNodeInTime` and
// `setNodeOutTime`.
#include <string>
#include <unordered_map>

#include "obf2/hud/hud.h"
#include "obf2/meme/graph.h"

namespace obf2::hud {

// --- the HUD's corner regions ------------------------------------------
//
// This is a thing apart from the nodes: the regions travel not by
// `setNodeInTime` but by the `Menu/Ingame` graph's variables. They can be read with
// `tools/meme_read.py Ingame --find BottomRight`:
//
//   SetVariableSineAction {Speed: 600}
//     Variable: FloatData 'BottomRight/BottomRight_XPos'    503
//     Data:     ToggleData 'BottomRight/BottomRight_NextPos'
//                 Data 1: 'BottomRight_newXPos'             503
//                 Data 2: 'BottomRight_oldXPos'             201
//
// These two numbers in the file are only the variables' **initial** values: both
// are bound to fields of the HUD object, and the game rewrites them. The binding
// is done by 0x7a62c0 (`BF2.exe`): `BottomRight_oldXPos` -> field +0x28,
// `BottomRight_newXPos` -> +0x2c, `BottomRight_direction` -> +0x18.
//
// **On the right there are three positions, not two** — the same as on the left.
// They are set by the same object's constructor (`BF2.exe`, 0x7a5b10), in the fields
// +0x1c, +0x20, +0x24:
//
//   0x43fb8000 = 503   hidden
//   0x43a88000 = 337   on foot
//   0x43250000 = 165   in a vehicle
//
// A frame dump of the original (`Ctrl+Shift+D`,
// docs/research/03-frame-dump.md) gave 336.5, and it is the same number: the
// dump measures the quads' corners, while the whole HUD is drawn with a half
// pixel offset (the same dump has `MapFrame` at 595.5 against 596 in the data).
// Now the number comes from the binary rather than from the screen.
inline constexpr float kBottomRightHiddenX = 503.0f;
inline constexpr float kBottomRightShownX = 337.0f;
inline constexpr float kBottomRightVehicleX = 165.0f;

// **On the left there are three positions, not two.** They are set by the HUD
// object's constructor (`BF2.exe`, 0x78c560), in the fields +0x178, +0x17c, +0x180:
//
//   0xc3938000 = -295   hidden
//   0xc3090000 = -137   on foot
//   0x42580000 =   54   in a vehicle
//
// The current and target positions are the neighbouring fields +0x184
// (`BottomLeft_XPos`) and +0x188 (`BottomLeft_nextXPos`), registered as graph
// variables (0x7895f5 and 0x78963f). The choice among the three is made by 0x78b600.
//
// It used to hold the placeholder -1, and that is exactly why the plate under
// the health stretched over the full width as if the player were in a vehicle:
// the node `BottomLeftBar` (400 wide, offset -103) reached x = 296 at -1, while
// on foot it has to reach 160.
inline constexpr float kBottomLeftHiddenX = -295.0f;
inline constexpr float kBottomLeftFootX = -137.0f;
inline constexpr float kBottomLeftVehicleX = 54.0f;

// The regions' movement speed comes from the same file (`SetVariableSineAction`).
inline constexpr float kCornerMoveSpeed = 600.0f;

// The same regions' alpha is driven by a **different** action —
// `SetVariableSoftAction` at speed 10 (four of them: BottomLeft_alpha1/2, BottomRight_alpha).
inline constexpr float kCornerAlphaSpeed = 10.0f;

// Both actions the graph moves HUD variables with live in `obf2::meme`:
// it is the same `Menu/Ingame` that drives the corner regions
// (obf2/meme/graph.h, `approachVariable`).

// One node's transition state.
struct ShowState {
  // Whether the animator has heard of this node at all. If not, an ordinary show
  // condition governs it rather than a transition's progress.
  bool known = false;
  float progress = 1.0f;  // 0 is hidden, 1 is in place
  float alpha = 1.0f;     // the alpha multiplier from the alpha effect
  float offsetX = 0.0f;   // the offset from the move effect, in the base 800x600
  float offsetY = 0.0f;
};

class Animator {
 public:
  // Advance time. `visible` is the cull node's answer for each node.
  void advance(float dt);

  // The target for a node on this frame. Calling it before `advance` is not
  // required: a node we have not heard of appears straight in its final state
  // when it is visible and in the zero state when it is not.
  void setVisible(const Node& node, bool visible);

  ShowState state(const Node& node) const;

  // Whether some node is mid-transition right now. While it is, the screen has
  // to be rebuilt every frame.
  bool animating() const { return animating_; }

 private:
  struct Entry {
    float progress = 0.0f;
    bool visible = false;
    float inTime = 0.0f;
    float outTime = 0.0f;
  };
  std::unordered_map<std::string, Entry> entries_;
  bool animating_ = false;
};

}  // namespace obf2::hud
