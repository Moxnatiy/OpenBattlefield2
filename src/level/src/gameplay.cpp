#include "obf2/level/gameplay.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

#include "obf2/con/interpreter.h"

namespace obf2::level {
namespace {

// Опис шаблону, зібраний із ObjectTemplate.*. Розстановка приходить
// окремо, тому спершу збираємо шаблони, а потім прив'язуємо до них позиції.
struct TemplateInfo {
  std::string className;
  std::string nameKey;
  int controlPointId = 0;
  float radius = 10.0f;
  int team = 0;
  bool unableToChangeTeam = false;
  int onlyTakeableByTeam = 0;
  float timeToGetControl = 20.0f;
  float timeToLoseControl = 20.0f;
  float areaValueTeam1 = 0.0f;
  float areaValueTeam2 = 0.0f;
  int enemyTicketLossWhenCaptured = 0;
  int teamOnVehicle = 0;
  Vec3f spawnOffset;
  bool spawnActive = true;
  bool onlyForAI = false;
  bool onlyForHuman = false;
  float spawnPreventionDelay = 0.0f;
  float minSpawnHeight = -1.0f;
  std::map<int, std::string> templateByTeam;
};

class Builder {
 public:
  void operator()(const con::Command& command) {
    const std::string& path = command.lowerPath;

    // --- шаблони ---
    if (path == "objecttemplate.create" || path == "objecttemplate.activesafe") {
      if (command.args.size() < 2) return;
      activeTemplate_ = command.args[1];
      TemplateInfo& info = templates_[activeTemplate_];
      if (info.className.empty()) info.className = command.args[0];
      return;
    }

    if (!activeTemplate_.empty() && path.rfind("objecttemplate.", 0) == 0) {
      TemplateInfo& info = templates_[activeTemplate_];
      const std::string method = path.substr(std::string("objecttemplate.").size());

      if (method == "setcontrolpointname") info.nameKey = std::string(command.argStr(0));
      else if (method == "controlpointid" || method == "setcontrolpointid") {
        info.controlPointId = command.argInt(0).value_or(info.controlPointId);
      } else if (method == "radius") {
        info.radius = command.argFloat(0).value_or(info.radius);
      } else if (method == "team") {
        info.team = command.argInt(0).value_or(info.team);
      } else if (method == "unabletochangeteam") {
        info.unableToChangeTeam = command.argBool(0).value_or(false);
      } else if (method == "onlytakeablebyteam") {
        info.onlyTakeableByTeam = command.argInt(0).value_or(0);
      } else if (method == "timetogetcontrol") {
        info.timeToGetControl = command.argFloat(0).value_or(info.timeToGetControl);
      } else if (method == "timetolosecontrol") {
        info.timeToLoseControl = command.argFloat(0).value_or(info.timeToLoseControl);
      } else if (method == "areavalueteam1") {
        info.areaValueTeam1 = command.argFloat(0).value_or(info.areaValueTeam1);
      } else if (method == "areavalueteam2") {
        info.areaValueTeam2 = command.argFloat(0).value_or(info.areaValueTeam2);
      } else if (method == "enemyticketlosswhencaptured") {
        info.enemyTicketLossWhenCaptured = command.argInt(0).value_or(0);
      } else if (method == "setactive") {
        info.spawnActive = command.argBool(0).value_or(true);
      } else if (method == "setonlyforai") {
        info.onlyForAI = command.argBool(0).value_or(false);
      } else if (method == "setonlyforhuman") {
        info.onlyForHuman = command.argBool(0).value_or(false);
      } else if (method == "setspawnpreventiondelay") {
        info.spawnPreventionDelay = command.argFloat(0).value_or(0.0f);
      } else if (method == "setminspawnheight") {
        info.minSpawnHeight = command.argFloat(0).value_or(-1.0f);
      } else if (method == "setspawnpositionoffset") {
        if (const auto offset = command.argVec3(0)) {
          info.spawnOffset = Vec3f{offset->x, offset->y, offset->z};
        }
      } else if (method == "teamonvehicle") {
        info.teamOnVehicle = command.argInt(0).value_or(info.teamOnVehicle);
      } else if (method == "setobjecttemplate") {
        // setObjectTemplate <команда> <шаблон машини>
        const auto team = command.argInt(0);
        if (team) info.templateByTeam[*team] = std::string(command.argStr(1));
      }
      return;
    }

    // --- розстановка ---
    // Бойова зона: `CombatArea.addAreaPoint -49.101/-466.631` — пара
    // світових координат x/z одним словом через скісну риску. Саме нею
    // гра обрізає карту на екрані появи.
    if (path == "combatarea.addareapoint") {
      const std::string_view text = command.argStr(0);
      const std::size_t slash = text.find('/');
      if (slash != std::string_view::npos) {
        const std::string first(text.substr(0, slash));
        const std::string second(text.substr(slash + 1));
        combatArea_.points.push_back(
            Vec3f{std::strtof(first.c_str(), nullptr), 0.0f, std::strtof(second.c_str(), nullptr)});
      }
      return;
    }
    if (path == "object.create") {
      placements_.push_back(Placement{std::string(command.argStr(0)), {}, {}, 0});
      return;
    }
    if (placements_.empty()) return;
    Placement& placement = placements_.back();

    if (path == "object.absoluteposition") {
      if (const auto position = command.argVec3(0)) {
        placement.position = Vec3f{position->x, position->y, position->z};
      }
    } else if (path == "object.rotation") {
      if (const auto rotation = command.argVec3(0)) {
        placement.rotation = Vec3f{rotation->x, rotation->y, rotation->z};
      }
    } else if (path == "object.setcontrolpointid") {
      placement.controlPointId = command.argInt(0).value_or(0);
    }
  }

  GameplayObjects build() const {
    GameplayObjects out;
    out.combatArea = combatArea_;
    for (const Placement& placement : placements_) {
      const auto found = templates_.find(placement.templateName);
      if (found == templates_.end()) continue;
      const TemplateInfo& info = found->second;

      if (info.className == "ControlPoint") {
        ControlPoint point;
        point.templateName = placement.templateName;
        point.nameKey = info.nameKey;
        // Номер може бути і в шаблоні, і в розстановці.
        point.id = info.controlPointId != 0 ? info.controlPointId : placement.controlPointId;
        point.radius = info.radius;
        point.position = placement.position;
        point.team = info.team;
        point.unableToChangeTeam = info.unableToChangeTeam;
        point.onlyTakeableByTeam = info.onlyTakeableByTeam;
        point.timeToGetControl = info.timeToGetControl;
        point.timeToLoseControl = info.timeToLoseControl;
        point.areaValueTeam1 = info.areaValueTeam1;
        point.areaValueTeam2 = info.areaValueTeam2;
        point.enemyTicketLossWhenCaptured = info.enemyTicketLossWhenCaptured;
        out.controlPoints.push_back(std::move(point));
      } else if (info.className == "SpawnPoint") {
        SpawnPoint spawn;
        spawn.templateName = placement.templateName;
        spawn.position = placement.position;
        spawn.rotation = placement.rotation;
        spawn.controlPointId =
            info.controlPointId != 0 ? info.controlPointId : placement.controlPointId;
        spawn.offset = info.spawnOffset;
        spawn.active = info.spawnActive;
        spawn.onlyForAI = info.onlyForAI;
        spawn.onlyForHuman = info.onlyForHuman;
        spawn.spawnPreventionDelay = info.spawnPreventionDelay;
        spawn.minSpawnHeight = info.minSpawnHeight;
        out.spawnPoints.push_back(std::move(spawn));
      } else if (info.className == "ObjectSpawner") {
        ObjectSpawner spawner;
        spawner.templateName = placement.templateName;
        spawner.position = placement.position;
        spawner.rotation = placement.rotation;
        spawner.controlPointId = placement.controlPointId;
        spawner.templateByTeam = info.templateByTeam;
        spawner.teamOnVehicle = info.teamOnVehicle;
        out.spawners.push_back(std::move(spawner));
      }
    }
    return out;
  }

 private:
  struct Placement {
    std::string templateName;
    Vec3f position;
    Vec3f rotation;
    int controlPointId = 0;
  };

  std::map<std::string, TemplateInfo> templates_;
  std::vector<Placement> placements_;
  std::string activeTemplate_;
  CombatArea combatArea_;
};

}  // namespace

const ControlPoint* GameplayObjects::controlPoint(int id) const {
  for (const ControlPoint& point : controlPoints) {
    if (point.id == id) return &point;
  }
  return nullptr;
}

std::optional<GameplayObjects> loadGameplayObjects(FileSystem& files, std::string_view levelName,
                                                   std::string_view gameMode, int size,
                                                   std::string* error) {
  const std::string path = "Levels/" + std::string(levelName) + "/GameModes/" +
                           std::string(gameMode) + "/" + std::to_string(size) +
                           "/GamePlayObjects.con";
  if (!files.exists(path)) {
    if (error) *error = "немає " + path;
    return std::nullopt;
  }

  Builder builder;
  con::Interpreter interpreter(files, [&](const con::Command& command) { builder(command); });
  // Розстановка тут за гілкою "host", а не "BF2Editor": саме так її
  // запускає Init.con рівня (`run Editor/GamePlayObjects.con host`).
  // З аргументом BF2Editor файл читається, але жодного Object.create
  // не виконується — і логіка виходить порожньою.
  interpreter.runFile(path, {"host"});

  GameplayObjects objects = builder.build();
  objects.gameMode = std::string(gameMode);
  objects.size = size;
  return objects;
}

}  // namespace obf2::level

namespace obf2::level {

void CombatArea::bounds(float& minX, float& maxX, float& minZ, float& maxZ) const {
  minX = minZ = std::numeric_limits<float>::max();
  maxX = maxZ = std::numeric_limits<float>::lowest();
  for (const Vec3f& point : points) {
    minX = std::min(minX, point.x);
    maxX = std::max(maxX, point.x);
    minZ = std::min(minZ, point.z);
    maxZ = std::max(maxZ, point.z);
  }
}

}  // namespace obf2::level
