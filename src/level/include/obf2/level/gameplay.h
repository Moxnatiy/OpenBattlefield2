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

// Точка появи солдата. Прив'язана до контрольної точки: з'явитися можна
// лише там, де прапор уже твій.
struct SpawnPoint {
  std::string templateName;
  Vec3f position;
  Vec3f rotation;
  int controlPointId = 0;
  Vec3f offset;  // setSpawnPositionOffset: солдат стає трохи вище землі

  // Решта — властивості SpawnPointTemplate. Значення за замовчуванням узяті
  // з конструктора рушія (`SpawnPointTemplate::SpawnPointTemplate`), а не
  // з голови: рівні їх майже ніколи не задають.
  bool active = true;               // setActive, типово увімкнено
  bool onlyForAI = false;           // setOnlyForAI
  bool onlyForHuman = false;        // setOnlyForHuman
  float spawnPreventionDelay = 0.0f;  // setSpawnPreventionDelay, типово 0
  float minSpawnHeight = -1.0f;       // setMinSpawnHeight, -1 = не перевіряти
};

// Бойова зона раунду — багатокутник, за межі якого виходити не можна.
// Вона ж вирішує, який шматок карти видно на екрані появи: у 16-місцевих
// режимах Dalian_plant це приблизно третина світу, і карта там помітно
// ближча, ніж уся картинка рівня.
struct CombatArea {
  std::vector<Vec3f> points;  // x і z; y не використовується
  bool empty() const { return points.empty(); }
  // Габарити по осях x і z.
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

// Читає логіку для конкретного режиму й розміру. Рівень має бути змонтований.
std::optional<GameplayObjects> loadGameplayObjects(FileSystem& files, std::string_view levelName,
                                                   std::string_view gameMode, int size,
                                                   std::string* error = nullptr);

}  // namespace obf2::level
