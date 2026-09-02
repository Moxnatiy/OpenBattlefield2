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
      {0, {"ShowIngameHud", "MapShow", "MapBorderShow"}},
      {1, {"ShowIngameHud", "MapBorderAlternateShow", "SpawnShow", "KitsShow"}},
      {2, {"MapShow"}},
      {3, {"SquadInterfaceShow"}},
      {4, {"RadioInterfaceShow"}},
      {5, {"RadioVehicleInterfaceShow"}},
      {6, {"SpottedInterfaceShow"}},
      {7, {"SquadLeaderInterfaceShow"}},
      {8, {"CommanderInterfaceShow"}},
      {9, {"ScoreboardShow", "LevelsListShow"}},
      {15, {"CommanderShow"}},
      {16, {"CommanderRadioShow"}},
      {17, {"MapShow", "SpawnShow", "MembersShow"}},
      {18, {"MembersShow", "SpawnShow"}},
      {19, {"MapMenuShow"}},
      {20, {"SquadLeaderMenuShow"}},
      {21, {"CommanderMenuShow"}},
      {26, {"InviteListShow"}},
      {27, {"ChoiceMenuShow"}},
      {29, {"SetupShow"}},
      {30, {"DemoCameraInterfaceShow"}},
      {31, {"DemoRecInterfaceShow"}},
  };
  return table;
}

bool applyState(VariableMap& variables, int state) {
  // Спершу складаємо, чого хочемо, і лише потім пишемо. Проміжного
  // «усе вимкнено» назовні бути не має: одна змінна належить кільком
  // станам (MapShow є і в 0, і в 2, і в 17), і якби ми гасили та вмикали
  // її двома проходами, повторний перехід у той самий стан щоразу
  // виглядав би як зміна — і геометрія перебудовувалася б щокадру.
  VariableMap target;
  for (const StateEntry& entry : hudStates()) {
    for (const char* name : entry.on) target.emplace(name, false);
  }
  for (const StateEntry& entry : hudStates()) {
    if (entry.id != state) continue;
    for (const char* name : entry.on) target[name] = true;
  }

  bool changed = false;
  for (const auto& [name, value] : target) changed |= set(variables, name.c_str(), value);
  return changed;
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
