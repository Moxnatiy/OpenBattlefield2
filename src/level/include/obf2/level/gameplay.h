#pragma once
// The level's game logic: `GameModes/<mode>/<size>/GamePlayObjects.con`.
//
// The file describes two things, both through the same mechanisms as the rest of the game:
//
//   * **templates** (`ObjectTemplate.create ControlPoint ...`) define what a
//     control point or a spawner is: the radius, the id, which vehicles it issues;
//   * **placement** (`Object.create` + `absolutePosition`) puts them into the world.
//
// So it is the same "template and instance" split as in StaticObjects.con.
// We read both parts and stitch them together by the template's name.
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::level {

// A control point — a flag that can be captured.
struct ControlPoint {
  std::string templateName;
  std::string nameKey;   // setControlPointName: a localisation key
  int id = 0;            // controlPointId, shared by the point and its spawners
  float radius = 10.0f;  // within which it is captured
  Vec3f position;
  int team = 0;  // 0 is neutral
  bool unableToChangeTeam = false;
  int onlyTakeableByTeam = 0;  // 0 means anyone

  // How many seconds raising and lowering the flag takes with one person's advantage.
  float timeToGetControl = 20.0f;
  float timeToLoseControl = 20.0f;

  // The point's "area weight" for the ticket bleed — separately for each team.
  float areaValueTeam1 = 0.0f;
  float areaValueTeam2 = 0.0f;

  // The one-off ticket loss for the enemy at the moment of capture.
  int enemyTicketLossWhenCaptured = 0;
};

// A vehicle spawner bound to a control point.
struct ObjectSpawner {
  std::string templateName;
  Vec3f position;
  Vec3f rotation;
  int controlPointId = 0;
  // Which vehicle to issue to each team: setObjectTemplate <team> <template>.
  std::map<int, std::string> templateByTeam;
  int teamOnVehicle = 0;
};

// A soldier's spawn point. Bound to a control point: you can only spawn where
// the flag is already yours.
struct SpawnPoint {
  std::string templateName;
  Vec3f position;
  Vec3f rotation;
  int controlPointId = 0;
  Vec3f offset;  // setSpawnPositionOffset: the soldier stands slightly above the ground

  // The rest are SpawnPointTemplate's properties. The defaults are taken from
  // the engine's constructor (`SpawnPointTemplate::SpawnPointTemplate`), not
  // invented: the levels almost never set them.
  bool active = true;               // setActive, on by default
  bool onlyForAI = false;           // setOnlyForAI
  bool onlyForHuman = false;        // setOnlyForHuman
  float spawnPreventionDelay = 0.0f;  // setSpawnPreventionDelay, 0 by default
  float minSpawnHeight = -1.0f;       // setMinSpawnHeight, -1 = do not check
};

// The round's combat area — a polygon that must not be left.
// It also decides which piece of the map is visible on the spawn screen: in
// Dalian_plant's 16-slot modes that is roughly a third of the world, and the map
// there is noticeably closer than the level's whole picture.
struct CombatArea {
  std::vector<Vec3f> points;  // x and z; y is unused
  bool empty() const { return points.empty(); }
  // The bounds along the x and z axes.
  void bounds(float& minX, float& maxX, float& minZ, float& maxZ) const;
};

struct GameplayObjects {
  std::string gameMode;
  int size = 0;
  CombatArea combatArea;
  std::vector<ControlPoint> controlPoints;
  std::vector<ObjectSpawner> spawners;
  std::vector<SpawnPoint> spawnPoints;

  const ControlPoint* controlPoint(int id) const;
};

// Reads the logic for a particular mode and size. The level has to be mounted.
std::optional<GameplayObjects> loadGameplayObjects(FileSystem& files, std::string_view levelName,
                                                   std::string_view gameMode, int size,
                                                   std::string* error = nullptr);

}  // namespace obf2::level
