// Хто вмикає змінні показу HUD.
//
// Перевіряємо саме те, що зреверсили: таблицю станів із BF2.exe і дві
// функції, які рушій крутить щокадру (docs/functions/hud-states.md).
#include "check.h"
#include "obf2/hud/states.h"

using namespace obf2;

namespace {

bool on(const hud::VariableMap& variables, const char* name) {
  const auto found = variables.find(name);
  return found != variables.end() && found->second;
}

}  // namespace

// Стан 0 — звичайний бій, стан 1 — екран появи. Обидва з таблиці
// переходів 0x787008.
static void testStateTurnsOnItsOwn() {
  hud::VariableMap variables;

  CHECK(hud::applyState(variables, 0));
  CHECK(on(variables, "ShowIngameHud"));
  CHECK(on(variables, "MapShow"));
  CHECK(on(variables, "MapBorderShow"));
  CHECK(!on(variables, "SpawnShow"));

  CHECK(hud::applyState(variables, 1));
  CHECK(on(variables, "SpawnShow"));
  CHECK(on(variables, "KitsShow"));
  CHECK(on(variables, "MapBorderAlternateShow"));
}

// Перехід не лишає хвостів: старий стан прибирає за собою. У грі це
// робить перший switch у HudObject::setState.
static void testStateLeavesNoTail() {
  hud::VariableMap variables;
  hud::applyState(variables, 9);  // табло
  CHECK(on(variables, "ScoreboardShow"));
  CHECK(on(variables, "LevelsListShow"));

  hud::applyState(variables, 0);
  CHECK(!on(variables, "ScoreboardShow"));
  CHECK(!on(variables, "LevelsListShow"));
  CHECK(on(variables, "ShowIngameHud"));
}

// Повторний перехід у той самий стан нічого не міняє — і має про це
// сказати, інакше ми перебудовували б геометрію щокадру.
static void testRepeatedStateChangesNothing() {
  hud::VariableMap variables;
  CHECK(hud::applyState(variables, 1));
  CHECK(!hud::applyState(variables, 1));
}

// Без гравця бойовий набір гаситься — саме через це в оригіналі за
// екраном появи не видно смуг здоров'я й набоїв.
static void testCombatSetNeedsAPlayer() {
  hud::VariableMap variables;

  hud::applyDerived(variables, hud::WorldView{false, true});
  CHECK(!on(variables, "PlayerHealthShow"));
  CHECK(!on(variables, "PlayerStaminaShow"));
  CHECK(!on(variables, "PrimaryAmmoShow"));

  CHECK(hud::applyDerived(variables, hud::WorldView{true, false}));
  CHECK(on(variables, "PlayerHealthShow"));
  CHECK(on(variables, "PlayerStaminaShow"));
  CHECK(on(variables, "PrimaryAmmoShow"));
}

// Розмір карти дає три змінні одразу, і MapBorderAlternateShow — це
// заперечення MapMinSize (0x4669ae).
static void testMapSizeDrivesThree() {
  hud::VariableMap variables;

  hud::applyDerived(variables, hud::WorldView{false, true});
  CHECK(on(variables, "MapFullSize"));
  CHECK(!on(variables, "MapMinSize"));
  CHECK(on(variables, "MapBorderAlternateShow"));

  hud::applyDerived(variables, hud::WorldView{false, false});
  CHECK(!on(variables, "MapFullSize"));
  CHECK(on(variables, "MapMinSize"));
  CHECK(!on(variables, "MapBorderAlternateShow"));
}

// Складені змінні — просто «і» двох інших (0x466935, 0x466950). Саме
// MapFullSizeAndSpawnShow тримає кнопки DONE і SUICIDE.
static void testCombinedVariablesAreAnAnd() {
  hud::VariableMap variables;

  // Екран появи: карта на весь екран і SpawnShow від стану 1.
  hud::applyState(variables, 1);
  hud::applyDerived(variables, hud::WorldView{false, true});
  CHECK(on(variables, "MapFullSizeAndSpawnShow"));
  CHECK(!on(variables, "MapFullSizeAndNotSpawnShow"));

  // Карта на весь екран у бою: появи немає, отже й кнопок немає.
  hud::applyState(variables, 0);
  hud::applyDerived(variables, hud::WorldView{true, true});
  CHECK(!on(variables, "MapFullSizeAndSpawnShow"));
  CHECK(on(variables, "MapFullSizeAndNotSpawnShow"));
}

// Таблиця — та сама, що в бінарі: 22 стани, а порожні позиції в ній не
// значаться.
static void testTableMatchesTheBinary() {
  CHECK_EQ(hud::hudStates().size(), std::size_t(22));
  bool hasEmptySlot = false;
  for (const hud::StateEntry& entry : hud::hudStates()) {
    if (entry.id == 10 || entry.id == 28) hasEmptySlot = true;
    CHECK(!entry.on.empty());
  }
  CHECK(!hasEmptySlot);
}

TEST_MAIN({
  testStateTurnsOnItsOwn();
  testStateLeavesNoTail();
  testRepeatedStateChangesNothing();
  testCombatSetNeedsAPlayer();
  testMapSizeDrivesThree();
  testCombinedVariablesAreAnAnd();
  testTableMatchesTheBinary();
})
