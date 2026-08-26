#include "obf2/level/gameplay.h"

#include <algorithm>

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
