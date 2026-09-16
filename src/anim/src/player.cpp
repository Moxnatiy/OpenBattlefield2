#include "obf2/anim/player.h"

#include <algorithm>
#include <cmath>

namespace obf2::anim {
namespace {

// `MovementTrigger::applyAnimations` (`BF2.exe` 0x7ff490): the dead zone the
// direction's components are compared against, and the factor that turns the
// angle into a weight (0.63661975 = 2/pi).
constexpr float kDeadZone = 0.01f;
constexpr float kAngleToWeight = 0.63661975f;

float clamp(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

}  // namespace

float Player::timeOf(const std::string& bundle) const {
  const auto found = time_.find(bundle);
  return found == time_.end() ? 0.0f : found->second;
}

void Player::update(const System& system, const State& state, float step,
                    const System::Random& random) {
  playing_.clear();
  const std::vector<const Bundle*> chosen = system.select(state, random);

  std::map<std::string, float> next;
  for (const Bundle* bundle : chosen) {
    if (bundle == nullptr || bundle->animations.empty()) continue;

    // The playback speed: a movement bundle runs at the ground speed divided by
    // the speed its clips were animated at — the third number of the trigger's
    // value holder. Which holder belongs to a bundle is not in the bundle itself,
    // so the trigger that asked for it is found by its name.
    float speed = 1.0f;
    if (const ValueHolder* holder = system.holderForBundle(bundle->name);
        holder != nullptr && holder->extra != 0.0f) {
      speed = state.speed / holder->extra;
    }

    float time = timeOf(bundle->name) + step * speed;

    // The engine's list is built from the front, so index 0 is the **last**
    // animation the data added (Bundle::getAnimation, Linux 0x6b6460).
    const auto animationAt = [&](std::size_t index) -> const std::string& {
      return bundle->animations[bundle->animations.size() - 1 - index];
    };
    const auto lengthOf = [&](const std::string& path) {
      const float length = lengthOf_ ? lengthOf_(path) : 0.0f;
      return length > 0.0f ? length : 0.0f;
    };

    // Wrap around the longest clip of the bundle: a bundle's own length is the
    // longest of its animations (`Bundle::adjustLength`, Linux 0x6b6490).
    float longest = 0.0f;
    for (const std::string& path : bundle->animations) longest = std::max(longest, lengthOf(path));
    if (bundle->looping && longest > 0.0f) {
      time = std::fmod(time, longest);
      if (time < 0.0f) time += longest;
    } else if (longest > 0.0f) {
      time = std::min(time, longest);
    }
    next[bundle->name] = time;

    if (bundle->animations.size() == 4) {
      // Four clips are a movement set: the run pair by the sign of the forward
      // component, the strafe pair by the side one, weighed by the angle.
      const float weight = std::asin(clamp(std::abs(state.direction[2]), -1.0f, 1.0f)) *
                           kAngleToWeight;
      const std::size_t strafe = state.direction[0] >= kDeadZone ? 2 : 3;
      const std::size_t run = state.direction[2] < -kDeadZone ? 1 : 0;
      playing_.push_back(PlayingAnimation{animationAt(run), time, weight, bundle->name});
      playing_.push_back(
          PlayingAnimation{animationAt(strafe), time, 1.0f - weight, bundle->name});
      continue;
    }

    // Anything else plays its first clip. A bundle of two with no value holder is
    // the engine's left-foot/right-foot pair, picked by a phase the system keeps
    // (0x7ff490); that phase is not kept here yet.
    playing_.push_back(PlayingAnimation{animationAt(0), time, 1.0f, bundle->name});
  }

  time_ = std::move(next);
}

}  // namespace obf2::anim
