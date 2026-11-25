#include <cstdio>
#include <filesystem>
#include <fstream>

#include "check.h"
#include "obf2/app/field_ui.h"

using namespace obf2;

// The communication roses (obf2/app/field_ui.h, docs/functions/command-rose.md):
// which rose a key opens, the pointer over the game's own layout, the entry
// chosen on release and its command routed onto the session's line.
int main() {
  // `roseFor`, in 0x758540's order: Q the radio (in a vehicle its own); T the
  // squad leader's, then the commander's, then the squad member's, else none.
  CHECK(app::roseFor(false, false, false, false, false) == app::RoseKind::Radio);
  CHECK(app::roseFor(false, true, false, true, true) == app::RoseKind::RadioVehicle);
  CHECK(app::roseFor(true, true, true, true, false) == app::RoseKind::SquadLeader);
  CHECK(app::roseFor(true, false, true, true, false) == app::RoseKind::Commander);
  CHECK(app::roseFor(true, false, false, true, false) == app::RoseKind::Squad);
  CHECK(!app::roseFor(true, false, false, false, false));

  // The tables' commands on our line.
  CHECK(app::roseCommand("SquadLeader.sendOrder 5 0") == "order attack");
  CHECK(app::roseCommand("SquadLeader.sendOrder 7 0") == "order repair");
  CHECK(app::roseCommand("SquadLeader.sendOrder 9 0") == std::nullopt);
  CHECK(app::roseCommand("SquadLeader.sendRequest 1 0") == "request uav");
  CHECK(app::roseCommand("SquadLeader.sendRequestForOrder") == "radio new-order");
  CHECK(app::roseCommand("game.radioMessage TCSquad req_new_order") == "radio squad-orders");
  CHECK(app::roseCommand("game.radioMessage TCLocalTeam PLAYER_TACTICS_gogogo") == "radio go");
  CHECK(app::roseCommand("Radio.sendSpottedMessage 1 VTNoVehicleType") == "spot");
  CHECK(app::roseCommand("Commander.vehicleDrop") == "commander vehicle");

  // The radio's layout and table, cut to their middle and top entries
  // (HudElementsRadioComm.con, Radio.con).
  const auto fixture = std::filesystem::temp_directory_path() / "obf2-field-ui-test";
  const auto folder = fixture / "menu/HUD/HudSetup/Communication";
  std::filesystem::create_directories(folder);
  {
    std::ofstream out(folder / "HudElementsRadioComm.con");
    out << "hudBuilder.createSplitNode IngameHud RadioRose\n"
           "hudBuilder.createButtonNode RadioRose RadioMiddle 345 245 108 108\n"
           "hudBuilder.setButtonNodeMouseArea -20 -20 148 148\n"
           "hudBuilder.setButtonNodeTexture 2 Ingame\\Communication\\Rose\\Selected_circle.tga\n"
           "hudBuilder.setButtonNodeTexture 1 Ingame\\Communication\\Rose\\unSelected_circle.tga\n"
           "hudBuilder.setButtonNodeConCmd \"RadioInterface.selectOrder 1\" 0\n"
           "hudBuilder.createButtonNode RadioRose RadioUp 285 222 228 26\n"
           "hudBuilder.setButtonNodeMouseArea 40 -100 148 103\n"
           "hudBuilder.setButtonNodeConCmd \"RadioInterface.selectOrder 2\" 0\n"
           "hudBuilder.createTextNode RadioRose RadioUpLabel 303 225 190 20\n"
           "hudBuilder.setTextNodeStringVariable Radio2String\n";
  }
  {
    std::ofstream out(folder / "Radio.con");
    out << "radioInterface.setStringMap Interface_1:1 HUD_COMROSE_GRUNT_spotted "
           "\"Radio.sendSpottedMessage 1 VTNoVehicleType\"\n"
           "radioInterface.setStringMap Interface_1:2 HUD_COMROSE_GRUNT_go "
           "\"game.radioMessage TCLocalTeam PLAYER_TACTICS_gogogo\"\n";
  }
  FileSystem files;
  CHECK(files.mountDirectory(fixture));
  app::FieldUi rose;
  rose.prepareRadio(files);
  const auto radio = app::RoseKind::Radio;
  // Opens at the comm pointer (400, 300), over the middle entry.
  const int closed = rose.revision();
  CHECK(!rose.radioRelative(true, 0, 0, false, 2, radio));
  CHECK(rose.radioOpen());
  CHECK(rose.revision() != closed);
  CHECK(!rose.radioRelative(true, 0, 0, false, 2, radio));
  CHECK_EQ(rose.radioSelection(), 1);
  // 200 pixels up at two pixels a unit: 100 units, into the top entry's area.
  CHECK(!rose.radioRelative(true, 0, -200, false, 2, radio));
  CHECK_EQ(rose.radioSelection(), 2);
  // Let go over it: its command, and the rose closes.
  const auto go = rose.radioRelative(false, 0, 0, false, 2, radio);
  CHECK(go && *go == "game.radioMessage TCLocalTeam PLAYER_TACTICS_gogogo");
  CHECK(!rose.radioOpen());
  // A key with no rose for the role opens nothing.
  CHECK(!rose.radioRelative(true, 0, 0, false, 1, std::nullopt));
  CHECK(!rose.radioOpen());
  // A release in the middle runs the middle entry.
  rose.radioRelative(true, 0, 0, false, 1, radio);
  const auto spot = rose.radioRelative(false, 0, 0, false, 1, radio);
  CHECK(spot && app::roseCommand(*spot) == "spot");
  std::filesystem::remove_all(fixture);

  if (obf2test::g_failures != 0) return 1;
  std::puts("OK");
  return 0;
}
