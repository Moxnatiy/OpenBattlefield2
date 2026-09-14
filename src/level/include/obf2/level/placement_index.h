#pragma once
// What an object the server created is, by where it stands.
//
// A live server tells the client about every object it creates with
// `CreateObjectEvent`: a network id, a template **number** and a position. The
// number is the creation order in the server's registry, and matching it to a
// name is protocol debt (CLAUDE.md). The position is not: both sides take it from
// the same level files, so an object the server creates stands exactly where the
// level placed something.
//
// Measured on the live server on Strike at Karkand (docs/functions/network-events.md,
// "Objects the server creates"): the created objects fall into two groups.
//
//   * the level's own destructibles — barrels, fences, the bridge, traffic
//     lights. They stand on a `StaticObjects.con` placement, and the level
//     already draws them;
//   * what the spawners issue — jeeps, tanks, the APC, the machine guns, the
//     AT and AA emplacements, the radar, the UAV trailer, the artillery. They
//     stand on an `ObjectSpawner` from `GamePlayObjects.con`, and the template
//     number differs by side: the machine gun on the MEC points came as 5553 and
//     the one at the US gas station as 5561. So the object is the vehicle the
//     spawner issues to the side that holds its control point.
#include <optional>
#include <string>
#include <vector>

#include "obf2/level/gameplay.h"
#include "obf2/level/level.h"

namespace obf2::level {

struct PlacedAt {
  enum class Kind {
    Unknown,  // nothing in the level stands here
    Static,   // a `StaticObjects.con` placement — the level draws it already
    Spawned,  // an `ObjectSpawner` — the object is the vehicle it issues
  };
  Kind kind = Kind::Unknown;
  std::string templateName;  // the placed template, or the spawner's vehicle
  Vec3f rotation;            // the placement's, in degrees — the same form the scene uses
  bool hasRotation = false;
};

class PlacementIndex {
 public:
  // Both sides read the position from the same data, so the match has to be
  // exact; a wider tolerance would start inventing correspondences. The same
  // two metres the connection's diagnostics use.
  static constexpr float kTolerance = 2.0f;

  PlacementIndex(const GameplayObjects& gameplay, const Level& level);

  // A spawner wins over a static placement at the same spot: the spawners are
  // few and deliberate, and nothing in the level's statics is a vehicle.
  PlacedAt at(const Vec3f& position) const;

 private:
  struct Spawner {
    Vec3f position;
    Vec3f rotation;
    std::string vehicle;
  };
  struct Static {
    Vec3f position;
    Vec3f rotation;
    bool hasRotation = false;
    std::string name;
  };
  std::vector<Spawner> spawners_;
  std::vector<Static> statics_;
};

// Which vehicle a spawner issues. The side is the one holding the spawner's
// control point at the round's start — the level's own `ControlPoint.team`. A
// point that changes hands in play would change the answer; we read no capture
// events yet, and that is written down as the limit. A neutral point, or a side
// the spawner names no vehicle for, falls back to the first vehicle it lists.
std::string spawnerVehicle(const GameplayObjects& gameplay, const ObjectSpawner& spawner);

}  // namespace obf2::level
