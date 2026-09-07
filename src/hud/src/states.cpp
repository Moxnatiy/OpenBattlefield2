#include "obf2/hud/states.h"

namespace obf2::hud {
namespace {

// Змінити значення й сказати, чи воно справді змінилося.
bool set(VariableMap& variables, const char* name, bool value) {
  bool& slot = variables[name];
  if (slot == value) return false;
  slot = value;
  return true;
}

}  // namespace

const std::vector<StateEntry>& hudStates() {
  // Створено за виводом `tools/hud_states.py`, який читає таблицю
  // переходів 0x787008 просто з BF2.exe. Позиції 10, 12-14, 22-25 і 28
  // ведуть до спільного порожнього обробника, тож їх тут немає.
  static const std::vector<StateEntry> table = {
      {0,
       {"ShowIngameHud", "MapShow", "MapBorderShow"},
       {"ScoreboardShow", "SpawnShow", "CommanderInterfaceShow", "CommanderShow"}},
      {1,
       {"ShowIngameHud", "MapBorderAlternateShow", "SpawnShow", "KitsShow", "MapMenuShow"},
       {"ScoreboardShow", "MembersShow"}},
      {2, {"MapShow"}, {}},
      {3, {"SquadInterfaceShow"}, {}},
      {4, {"RadioInterfaceShow"}, {}},
      {5, {"RadioVehicleInterfaceShow"}, {}},
      {6, {"SpottedInterfaceShow"}, {}},
      {7, {"SquadLeaderInterfaceShow"}, {}},
      {8, {"CommanderInterfaceShow"}, {}},
      {9, {"ScoreboardShow", "LevelsListShow"}, {}},
      {11,
       {"SetupShow"},
       {"ShowIngameHud", "SpawnShow", "RadioInterfaceShow", "SpottedInterfaceShow",
        "RadioVehicleInterfaceShow", "SquadInterfaceShow", "SquadLeaderInterfaceShow",
        "CommanderInterfaceShow", "MapMenuShow", "SquadLeaderMenuShow", "CommanderMenuShow",
        "ChoiceMenuShow", "CommanderRadioShow", "ScoreboardShow", "LevelsListShow",
        "RenameSquadShow", "VictoryShow", "VictoryRankShow", "VoipListShow", "InviteListShow",
        "CommanderShow"}},
      {15, {"CommanderShow"}, {"SpawnShow"}},
      {16, {"CommanderRadioShow"}, {}},
      {17, {"MapShow", "SpawnShow", "MembersShow"}, {"KitsShow"}},
      {18, {"MembersShow", "SpawnShow"}, {"KitsShow"}},
      {19, {"MapMenuShow"}, {}},
      {20, {"SquadLeaderMenuShow"}, {}},
      {21, {"CommanderMenuShow"}, {}},
      {26, {"InviteListShow"}, {}},
      {27, {"ChoiceMenuShow"}, {}},
      {29, {"SetupShow"}, {}},
      {30, {"DemoCameraInterfaceShow"}, {}},
      {31, {"DemoRecInterfaceShow"}, {}},
  };
  return table;
}

const std::vector<StateEntry>& hudLeaveStates() {
  // Перший switch у 0x786260 — за старим станом. Виписано з розбору
  // самої функції (адреси гілок у дужках); таблиці переходів у нього
  // немає, тож `hud_states.py` його не бачить.
  //
  // Три рядки перед switch (0x786289, 0x7862a2, 0x7862bb) гасять
  // `SetupShow`, `DemoRecInterfaceShow` і `DemoCameraInterfaceShow` за
  // будь-якого переходу — вони в `kAlwaysOff` нижче.
  static const std::vector<StateEntry> table = {
      {0, {}, {"VoipListShow"}},
      // 1: `SpawnShow`, але з умовою на **новий** стан — див. applyState.
      {1, {}, {}},
      {3, {}, {"SquadInterfaceShow"}},
      {4, {}, {"RadioInterfaceShow", "RadioVehicleInterfaceShow"}},                        // 0x786456
      {5, {}, {"RadioInterfaceShow", "RadioVehicleInterfaceShow"}},
      {6, {}, {"SpottedInterfaceShow", "RadioInterfaceShow", "RadioVehicleInterfaceShow"}},  // 0x78643d
      {7, {}, {"SquadLeaderInterfaceShow"}},
      {8, {}, {"CommanderInterfaceShow"}},
      {9, {}, {"ScoreboardShow", "LevelsListShow", "ServerInfoSelected"}},  // 0x7864be
      {11, {}, {"VictoryShow", "VictoryRankShow"}},                        // 0x786501
      {12, {}, {"ScoreboardShow", "LevelsListShow", "ServerInfoSelected"}},
      // 15: `CommanderShow`, але лише коли `[0xa10890]->vtbl[0x1f4]()`
      // хибний (0x78649d). Що це за перевірка — **не з'ясовано**, тож
      // гасимо завжди: інакше командирський екран не закривався б.
      {15, {}, {"CommanderShow"}},
      {16, {}, {"CommanderRadioShow"}},
      // 17, 18: `SpawnShow` з умовою на новий стан — див. applyState.
      {17, {}, {}},
      {18, {}, {}},
      {19, {}, {"MapMenuShow"}},
      {20, {}, {"SquadLeaderMenuShow", "InviteListShow"}},  // 0x786350
      {21, {}, {"CommanderMenuShow"}},
      {26, {}, {"InviteListShow"}},
      {27, {}, {"ChoiceMenuShow"}},
      {29, {}, {"SetupShow"}},
      {30, {}, {"DemoCameraInterfaceShow"}},
      {31, {}, {"DemoRecInterfaceShow"}},
  };
  return table;
}

namespace {

// Гілка `default` першого switch (0x78653c..0x786735): стану ще не було
// або він поза таблицею — прибрати геть усе.
const std::vector<const char*> kLeaveEverything = {
    "MapShow",           "MapBorderShow",      "SpawnShow",
    "RadioInterfaceShow", "SpottedInterfaceShow", "RadioVehicleInterfaceShow",
    "SquadInterfaceShow", "SquadLeaderInterfaceShow", "CommanderInterfaceShow",
    "MapMenuShow",       "SquadLeaderMenuShow", "CommanderMenuShow",
    "ChoiceMenuShow",    "CommanderRadioShow", "ScoreboardShow",
    "LevelsListShow",    "RenameSquadShow",    "VictoryShow",
    "VictoryRankShow",   "VoipListShow",       "InviteListShow",
    "CommanderShow"};

// Три виклики перед switch (0x786289, 0x7862a2, 0x7862bb).
const std::vector<const char*> kAlwaysOff = {"SetupShow", "DemoRecInterfaceShow",
                                             "DemoCameraInterfaceShow"};

const StateEntry* find(const std::vector<StateEntry>& table, int id) {
  for (const StateEntry& entry : table) {
    if (entry.id == id) return &entry;
  }
  return nullptr;
}

}  // namespace

bool applyState(VariableMap& variables, int previous, int state) {
  // Спершу складаємо, чого хочемо, і лише потім пишемо: інакше змінна,
  // яку старий стан гасить, а новий одразу вмикає, виглядала б як дві
  // зміни, і геометрія перебудовувалася б двічі на кадр.
  std::map<std::string, bool> target;
  const auto off = [&](const char* name) { target[name] = false; };
  const auto on = [&](const char* name) { target[name] = true; };

  for (const char* name : kAlwaysOff) off(name);

  // --- перший switch: за старим станом -------------------------------
  const StateEntry* leaving = find(hudLeaveStates(), previous);
  if (leaving == nullptr) {
    // Порожні гілки (2, 10, 13, 14, 22-25, 28) у таблиці є з порожнім
    // `off`; сюди потрапляє лише стан поза 0..31 — тобто «стану ще не
    // було».
    if (previous < 0 || previous > 31) {
      for (const char* name : kLeaveEverything) off(name);
    }
  } else {
    for (const char* name : leaving->off) off(name);
  }

  // Дві умовні гілки першого switch. Обидві гасять `SpawnShow`, але не
  // тоді, коли новий стан і сам його показує чи ним керує.
  if (previous == 1 && state != 9 && state != 12) off("SpawnShow");       // 0x7862ea
  if ((previous == 17 || previous == 18) && state != 9 && state != 12 &&  // 0x78631a
      state != 20 && state != 26 && state != 13 && state != 19) {
    off("SpawnShow");
  }

  // --- другий switch: за новим станом --------------------------------
  if (const StateEntry* entering = find(hudStates(), state); entering != nullptr) {
    for (const char* name : entering->off) off(name);
    for (const char* name : entering->on) on(name);
  }

  bool changed = false;
  for (const auto& [name, value] : target) changed |= set(variables, name.c_str(), value);
  return changed;
}

float showValue(const VariableMap& flags, const std::map<std::string, float>& values,
                std::string_view name) {
  const std::string key(name);
  const auto number = values.find(key);
  if (number != values.end()) return number->second;
  const auto flag = flags.find(key);
  return flag == flags.end() ? 0.0f : (flag->second ? 1.0f : 0.0f);
}

bool applyDerived(VariableMap& variables, const WorldView& view) {
  bool changed = false;

  // 0x466986: MapFullSize і MapMinSize — це два прапорці самого об'єкта
  // карти (поля 0x68c і 0x68d), а 0x4669ae робить із другого
  // MapBorderAlternateShow запереченням.
  changed |= set(variables, "MapFullSize", view.mapFullSize);
  changed |= set(variables, "MapMinSize", !view.mapFullSize);
  changed |= set(variables, "MapBorderAlternateShow", view.mapFullSize);

  // 0x466935 і 0x466950: складені змінні — просто «і» двох інших.
  const bool spawn = variables["SpawnShow"];
  changed |= set(variables, "MapFullSizeAndSpawnShow", view.mapFullSize && spawn);
  changed |= set(variables, "MapFullSizeAndNotSpawnShow", view.mapFullSize && !spawn);

  // 0x78d154 вмикає, 0x78d2f1 гасить — за наявністю керованого гравця.
  changed |= set(variables, "PlayerHealthShow", view.hasPlayer);
  // 0x78acf1: у грі це ще й порівняння самої витривалості зі сталою
  // (поле 0x1ac), тобто смуга з'являється, коли витривалість не повна.
  // Самої витривалості в нас поки немає — лишається гравець.
  changed |= set(variables, "PlayerStaminaShow", view.hasPlayer);
  // 0x7a5bae, 0x7a5bb5, 0x7a8a18: набої вмикає оновлення зброї. Зброї ми
  // поки не моделюємо, тож теж за гравцем. Борг.
  changed |= set(variables, "PrimaryAmmoShow", view.hasPlayer);
  changed |= set(variables, "PrimaryAmmoBarShow", view.hasPlayer);
  changed |= set(variables, "PrimaryClipsShow", view.hasPlayer);

  // 0x78adc5, 0x78ae8c проти 0x78af97: загін. Ми в загоні не буваємо.
  changed |= set(variables, "SquadInfoBarShow", false);
  changed |= set(variables, "ShowCommanderIcon", false);
  changed |= set(variables, "ShowSquadIcon", false);
  return changed;
}

}  // namespace obf2::hud
