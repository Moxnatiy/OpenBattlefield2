#pragma once
// How the map gathers strategic objects into the holders it draws one icon for
// (docs/functions/hud-map.md, "One difference stands").
//
// `GameLogic::addStrategicObject` (client `BF2.exe` 0x4f1750; Linux server
// 0x482e00), called for every object with a `StrategicObject` component as it
// is created (`StrategicObjectComp::init`, Linux 0x5be100). The holders live in
// a map keyed by the component's `strategicObjectName` plus a running index
// n = 0, 1, 2, ... For each n whose key exists:
//
//   * an object whose `StrategicObject.isOnePiece` is set (template +0x44,
//     default 0 in the template's constructor 0x5be840) never joins: n goes up;
//   * otherwise it joins when the holder has no first object, or when the
//     distance between their positions is below 2 x (r_new + r_first), r each
//     object's `getPhysicBoundingRadius` (Linux 0x687770);
//   * otherwise n goes up.
//
// With no key left a new holder is made under index n. A holder's position is
// the mean of its objects' positions (`StrategicObjectHolder::getPosition`,
// Linux 0x5be230); the icon is drawn from its first object (0x77e500).
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "obf2/core/math.h"

namespace obf2::hud {

struct StrategicCandidate {
  std::string name;  // `StrategicObject.strategicObjectName`
  bool onePiece = false;
  Vec3f position;
  float radius = 0.0f;  // `Object::getPhysicBoundingRadius`
};

struct StrategicHolder {
  std::string name;
  int index = 0;
  std::vector<std::size_t> members;  // into the candidates, in creation order
  Vec3f position;                    // the members' mean
};

// The candidates in creation order; the holders in the order they were made.
std::vector<StrategicHolder> groupStrategicObjects(std::span<const StrategicCandidate> candidates);

}  // namespace obf2::hud
