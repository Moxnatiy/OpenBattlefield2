#include "obf2/hud/animation.h"

#include <cmath>

namespace obf2::hud {

void Animator::setVisible(const Node& node, bool visible) {
  auto [it, inserted] = entries_.try_emplace(node.name);
  Entry& entry = it->second;
  if (inserted) {
    // A freshly created `CullNode` has progress -4 (`MemeDll.dll`, 0x10004a57
    // checks against exactly that marker), and **the very first show is a
    // transition**: in that same branch the progress is set to 0 and starts
    // growing at once. So a node visible from the start drives in in the original
    // too — visible on `GlobalHud`, which has `setNodeInTime 2`.
    entry.progress = 0.0f;
  }
  entry.visible = visible;
  entry.inTime = node.inTime;
  entry.outTime = node.outTime;
}

void Animator::advance(float dt) {
  animating_ = false;
  for (auto& [name, entry] : entries_) {
    const float target = entry.visible ? 1.0f : 0.0f;
    const float was = entry.progress;
    if (entry.progress != target) {
      const float time = entry.visible ? entry.inTime : entry.outTime;
      if (time <= 0.0f || dt <= 0.0f) {
        // Zero time is an instant transition: at 0x10004a57 the progress goes
        // straight to the "shown" or "hidden" marker.
        if (time <= 0.0f) entry.progress = target;
      } else {
        // Linearly: `progress += dt / "In time"` and `progress -= dt / "Out time"`,
        // verbatim from `CullNode::iterateUpdate`.
        const float step = dt / time;
        entry.progress += entry.visible ? step : -step;
        if (entry.progress > 1.0f) entry.progress = 1.0f;
        if (entry.progress < 0.0f) entry.progress = 0.0f;
      }
    }
    // "Still moving" means both "has not arrived" and "arrived on this very
    // step": the frame in which a node settles has to be redrawn too.
    if (entry.progress != target || entry.progress != was) animating_ = true;
  }
}

ShowState Animator::state(const Node& node) const {
  ShowState out;
  const auto it = entries_.find(node.name);
  // We have not heard of the node — let the ordinary show condition decide.
  if (it == entries_.end()) return out;
  out.known = true;
  out.progress = it->second.progress;

  // **The alpha is multiplied by the cull node itself, not by the effect.**
  // `dice::meme::CullNode::iteratePaint` (`MemeDll.dll`, 0x1000141a):
  //
  //   progress >= 1  -> the children draw with the parent's pipe, unchanged;
  //   progress <= 0  -> the children do not draw at all;
  //   otherwise      -> a new pipe, and in it `alpha = parent's * progress`.
  //
  // So a node with `setNodeInTime` but **without** `addNodeAlphaShowEffect` still
  // fades — simply because it is under a cull node. Until now we multiplied by
  // the progress only when an alpha effect was present, and a node with no
  // effects at all was switched instantly; because of that the time bar
  // (`TimeItems`, which has only a move effect) drives in and fades in in the
  // original, while for us it only drove in.
  out.alpha *= out.progress;

  for (const ShowEffectInfo& effect : node.showEffects) {
    switch (effect.kind) {
      case ShowEffect::Alpha:
      case ShowEffect::Blend:
        // There is no separate action here: the cull node above already faded it.
        break;
      case ShowEffect::Move: {
        // The formula is verbatim from `dice::meme::MoveEffect::picturePaint`
        // (`MemeDll.dll`, 0x10001b27; the library lies in the mod's directory and
        // exports full C++ symbols):
        //
        //   angle    = "Move direction"
        //   length   = "Move length"
        //   progress = EffectPipe+0x18
        //   offset   = (1 - progress) * length
        //   dx = -cos(angle) * offset
        //   dy = +sin(angle) * offset
        //
        // **Both of our signs were the opposite.** They used to be `(+cos, -sin)`,
        // derived from the reasoning "the voting panel has to drive in from
        // below". The reasoning did not survive a check against the source: our
        // elements flew in from the opposite side to the original's.
        const float left = 1.0f - out.progress;
        out.offsetX += -std::cos(effect.angle) * effect.distance * left;
        out.offsetY += std::sin(effect.angle) * effect.distance * left;
        break;
      }
    }
  }
  return out;
}

}  // namespace obf2::hud
