#pragma once
// What is playing right now, and where in its clip.
//
// The selection (`system.h`) says which bundles a state asks for; this keeps the
// time of each of them and turns a bundle into the one or two clips the engine
// poses the skeleton with. The rules are `AnimationSystem::playBundle` and
// `MovementTrigger::applyAnimations` — docs/functions/animation-system.md,
// "Playing a bundle".
//
// What is deliberately not here: the fades, the events and `BundlePlayer`'s own
// end-of-clip behaviour. `BundlePlayer::update` is not reversed yet.
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "obf2/anim/system.h"

namespace obf2::anim {

// One clip of one bundle at this moment.
struct PlayingAnimation {
  std::string path;     // the clip's path, as the data names it
  float time = 0.0f;    // seconds into the clip
  float weight = 1.0f;  // how much of it, against the other clip of the bundle
  std::string bundle;   // which bundle asked for it
};

class Player {
 public:
  // How long a clip is, in seconds. The player needs it to wrap the time around;
  // the caller knows it because the caller reads the `.baf` files.
  using LengthOf = std::function<float(const std::string& path)>;

  void setLengthOf(LengthOf lengthOf) { lengthOf_ = std::move(lengthOf); }

  // One step: picks the bundles for the state, advances their times and fills
  // `playing`.
  void update(const System& system, const State& state, float step,
              const System::Random& random = {});

  const std::vector<PlayingAnimation>& playing() const { return playing_; }
  // The time a bundle has been playing — kept between steps, and dropped as soon
  // as a step does not ask for that bundle (the engine drops the player).
  float timeOf(const std::string& bundle) const;

 private:
  LengthOf lengthOf_;
  std::map<std::string, float> time_;
  std::vector<PlayingAnimation> playing_;
};

}  // namespace obf2::anim
