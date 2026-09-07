// Значення HUD за іменами — без вікна й без геометрії.
#include <string>

#include "obf2/hud/hud_items.h"
#include "check.h"

using namespace obf2;

// `hudItems.setBool` — цим інтерфейс вмикає власні прапорці. Ім'я
// команди з даних (setButtonNodeConCmd), не наше.
void testSetBoolFromConsole() {
  engine::Console console;
  hud::HudItems items;
  items.bind(console);

  CHECK(!items.dirty());
  console.executeLine("hudItems.setBool SetSpawnPoint 1");
  CHECK(items.dirty());
  CHECK(items.flags()["SetSpawnPoint"]);

  console.executeLine("hudItems.setBool SetSpawnPoint 0");
  CHECK(!items.flags()["SetSpawnPoint"]);

  // Команда без другого аргумента нічого не міняє й не падає.
  items.clearDirty();
  console.executeLine("hudItems.setBool Lonely");
  CHECK(!items.dirty());
}

void testTextValueAlpha() {
  hud::HudItems items;
  items.setText("PlayerHealthString", "100");
  CHECK_EQ(std::string(items.text("PlayerHealthString")), std::string("100"));
  CHECK(items.text("НемаєТакої").empty());

  items.setValue("PlayerHealth", 0.75f);
  CHECK(items.value("PlayerHealth") > 0.74f && items.value("PlayerHealth") < 0.76f);
  CHECK(items.value("НемаєТакої") == 0.0f);

  // Невідома прозорість — це саме «не знаю», а не нуль: інакше вузол
  // зник би зовсім.
  CHECK(!items.alpha("BottomLeftHealthAlpha").has_value());
  items.setAlpha("MenuBackgroundAlpha", 0.8f);
  CHECK(items.alpha("MenuBackgroundAlpha").has_value());
}

TEST_MAIN({
  testSetBoolFromConsole();
  testTextValueAlpha();
});
