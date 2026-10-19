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

bool Player::advance(Active& active, float step) const {
  const Bundle& bundle = *active.bundle;
  const float length = active.length;

  if (!bundle.useSpeedAsTime) {
    active.time += step * active.speed;
    if (active.time > length) {
      if (bundle.looping) {
        active.time -= length;
      } else if (bundle.playForever) {
        active.time = length;
      } else {
        return false;
      }
    } else if (active.time < 0.0f) {
      if (!bundle.looping) return false;
      active.time += length;
    }
  } else {
    active.time = clamp(active.speed, 0.0f, 1.0f) * length;
  }

  // Fading out: a one-shot in its last `fadeOutTime` (its first, played
  // backwards); a looping or speed-driven bundle as soon as no trigger touched it.
  bool fadingOut;
  if (!bundle.useSpeedAsTime && !bundle.looping) {
    fadingOut = active.speed < 0.0f ? active.time < bundle.fadeOutTime
                                    : length - bundle.fadeOutTime < active.time;
  } else {
    fadingOut = !active.touched;
  }
  if (fadingOut) {
    if (active.weight <= 0.0f || bundle.fadeOutTime <= 0.0f) return false;
    active.fade -= step / bundle.fadeOutTime;
    if (active.fade <= 0.0f) return false;
    active.fade = clamp(active.fade, 0.0f, 1.0f);
    active.weight = active.fade * active.fade;
    return true;
  }
  if (bundle.fadeInTime > 0.0f && active.weight < 1.0f) {
    active.fade = clamp(active.fade + step / bundle.fadeInTime, 0.0f, 1.0f);
    active.weight = active.fade * active.fade;
    return true;
  }
  active.fade = 1.0f;
  active.weight = 1.0f;
  return true;
}

void Player::update(const System& system, const State& state, float step,
                    const System::Random& random) {
  playing_.clear();

  const auto lengthOf = [&](const std::string& path) {
    const float length = lengthOf_ ? lengthOf_(path) : 0.0f;
    return length > 0.0f ? length : 0.0f;
  };

  // The walk (`AnimationSystem::update`, `BF2.exe` 0x7f7390): `startup` once, on
  // the first update (+0x59, 0x7f746a..), then `root` and `postRoot` — `select`.
  std::vector<const Bundle*> chosen;
  if (!started_) {
    started_ = true;
    chosen = system.selectNamed("startup", state, random);
  }
  for (const Bundle* bundle : system.select(state, random)) chosen.push_back(bundle);

  // Every bundle the walk asks for is played (`playBundle`, 0x7f7a20): one that
  // already has a player is touched again, not restarted — unless it is
  // `abruptPlayback` and still touched from before, which starts it over
  // (`setIsPlaying(true)`, 0x7fdfb0). Otherwise a player is made.
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

    auto found = std::find_if(active_.begin(), active_.end(),
                              [&](const Active& a) { return a.bundle == bundle; });
    if (found == active_.end()) {
      Active fresh;
      fresh.bundle = bundle;
      // A bundle's own length is the longest of its animations
      // (`Bundle::adjustLength`, Linux 0x6b6490).
      for (const std::string& path : bundle->animations) {
        fresh.length = std::max(fresh.length, lengthOf(path));
      }
      fresh.speed = speed;
      // `setParameters` (0x7fdf40): a one-shot asked to play backwards starts at
      // its own end.
      if (!bundle->looping && speed < 0.0f) fresh.time = fresh.length;
      fresh.touched = true;
      active_.push_back(fresh);
      continue;
    }
    if (found->touched && bundle->abruptPlayback) found->time = 0.0f;
    found->touched = true;
    if (!bundle->jumpToLastAnimationAtStop) found->speed = speed;
  }

  // Every player one step (`BundlePlayer::update`, 0x7fe3c0); one that has
  // ended is dropped, and a looping one has to be touched again next time
  // (0x7f75c3).
  std::vector<Active> kept;
  for (Active& active : active_) {
    if (active.length <= 0.0f) continue;
    if (!advance(active, step)) continue;
    if (active.bundle->looping) active.touched = false;
    kept.push_back(active);
  }
  active_ = std::move(kept);

  std::map<std::string, float> next;
  for (const Active& active : active_) {
    const Bundle* bundle = active.bundle;
    const float time = active.time;
    next[bundle->name] = time;

    // The engine's list is built from the front, so index 0 is the **last**
    // animation the data added (Bundle::getAnimation, Linux 0x6b6460).
    const auto animationAt = [&](std::size_t index) -> const std::string& {
      return bundle->animations[bundle->animations.size() - 1 - index];
    };

    if (bundle->animations.size() == 4) {
      // Four clips are a movement set: the run pair by the sign of the forward
      // component, the strafe pair by the side one, weighed by the angle.
      const float weight = std::asin(clamp(std::abs(state.direction[2]), -1.0f, 1.0f)) *
                           kAngleToWeight;
      const std::size_t strafe = state.direction[0] >= kDeadZone ? 2 : 3;
      const std::size_t run = state.direction[2] < -kDeadZone ? 1 : 0;
      playing_.push_back(
          PlayingAnimation{animationAt(run), time, weight * active.weight, bundle->name});
      playing_.push_back(PlayingAnimation{animationAt(strafe), time,
                                          (1.0f - weight) * active.weight, bundle->name});
      continue;
    }

    // Anything else plays its first clip. A bundle of two with no value holder is
    // the engine's left-foot/right-foot pair, picked by a phase the system keeps
    // (0x7ff490); that phase is not kept here yet.
    playing_.push_back(PlayingAnimation{animationAt(0), time, active.weight, bundle->name});
  }

  time_ = std::move(next);
}

}  // namespace obf2::anim
