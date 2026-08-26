#pragma once
// Ігрова логіка рівня: `GameModes/<режим>/<розмір>/GamePlayObjects.con`.
//
// Файл описує дві речі, і обидві — через ті самі механізми, що й решта гри:
//
//   * **шаблони** (`ObjectTemplate.create ControlPoint ...`) задають, що
//     таке контрольна точка чи спавнер: радіус, номер, які машини видає;
//   * **розстановка** (`Object.create` + `absolutePosition`) ставить їх у світ.
//
// Тобто це той самий поділ «шаблон і примірник», що й у StaticObjects.con.
// Ми читаємо обидві частини й зшиваємо їх за іменем шаблону.
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "obf2/core/math.h"
#include "obf2/vfs/filesystem.h"

namespace obf2::level {

// Контрольна точка — прапор, який можна захопити.
struct ControlPoint {
  std::string templateName;
  std::string nameKey;   // setControlPointName: ключ локалізації
  int id = 0;            // controlPointId, спільний для точки й спавнерів
  float radius = 10.0f;  // у межах якого захоплюють
  Vec3f position;
  int team = 0;  // 0 — нейтральна
  bool unableToChangeTeam = false;
  int onlyTakeableByTeam = 0;  // 0 — будь-хто

  // Скільки секунд іде підйом і спуск прапора при перевазі в одну людину.
  float timeToGetControl = 20.0f;
  float timeToLoseControl = 20.0f;

  // «Вага площі» точки для витоку квитків — окремо для кожної команди.
  float areaValueTeam1 = 0.0f;
  float areaValueTeam2 = 0.0f;

  // Разова втрата квитків у противника в мить захоплення.
  int enemyTicketLossWhenCaptured = 0;
};

// Спавнер техніки, прив'язаний до контрольної точки.
struct ObjectSpawner {
  std::string templateName;
  Vec3f position;
  Vec3f rotation;
  int controlPointId = 0;
  // Яку машину видавати кожній команді: setObjectTemplate <команда> <шаблон>.
  std::map<int, std::string> templateByTeam;
  int teamOnVehicle = 0;
};

struct GameplayObjects {
  std::string gameMode;
  int size = 0;
  std::vector<ControlPoint> controlPoints;
  std::vector<ObjectSpawner> spawners;

  const ControlPoint* controlPoint(int id) const;
};

// Читає логіку для конкретного режиму й розміру. Рівень має бути змонтований.
std::optional<GameplayObjects> loadGameplayObjects(FileSystem& files, std::string_view levelName,
                                                   std::string_view gameMode, int size,
                                                   std::string* error = nullptr);

}  // namespace obf2::level
