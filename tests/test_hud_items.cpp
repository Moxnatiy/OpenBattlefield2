// HUD values by name — without a window and without geometry.
#include <string>

#include "obf2/hud/hud_items.h"
#include "check.h"

using namespace obf2;

// `hudItems.setBool` — this is what the interface turns its own flags on with. The
// command's name comes from the data (setButtonNodeConCmd), not from us.
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

  // A command without a second argument changes nothing and does not crash.
  items.clearDirty();
  console.executeLine("hudItems.setBool Lonely");
  CHECK(!items.dirty());
}

void testTextValueAlpha() {
  hud::HudItems items;
  items.setText("PlayerHealthString", "100");
  CHECK_EQ(std::string(items.text("PlayerHealthString")), std::string("100"));
  CHECK(items.text("NoSuchThing").empty());

  items.setValue("PlayerHealth", 0.75f);
  CHECK(items.value("PlayerHealth") > 0.74f && items.value("PlayerHealth") < 0.76f);
  CHECK(items.value("NoSuchThing") == 0.0f);

  // An unknown alpha means exactly "I do not know" rather than zero: otherwise the
  // node would disappear entirely.
  CHECK(!items.alpha("BottomLeftHealthAlpha").has_value());
  items.setAlpha("MenuBackgroundAlpha", 0.8f);
  CHECK(items.alpha("MenuBackgroundAlpha").has_value());
}

TEST_MAIN({
  testSetBoolFromConsole();
  testTextValueAlpha();
});
