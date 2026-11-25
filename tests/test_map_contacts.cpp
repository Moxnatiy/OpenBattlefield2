#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "check.h"
#include "obf2/app/map_contacts.h"
#include "obf2/con/interpreter.h"
#include "obf2/hud/map_node.h"

using namespace obf2;

// The map's live icons (obf2/app/map_contacts.h), how the map node draws them,
// and the spot blink that gates the position-only spots (docs/functions/hud-map.md).

static void testVehicleFollowsItsOccupants() {
  const auto fixture = std::filesystem::temp_directory_path() / "obf2-map-contacts-test";
  std::filesystem::create_directories(fixture);
  {
    std::ofstream out(fixture / "vehicle.con");
    out << "ObjectTemplate.create PlayerControlObject jeep\n"
           "ObjectTemplate.setNetworkableInfo BasicInfo\n"
           "ObjectTemplate.createComponent VehicleHud\n"
           "ObjectTemplate.vehicleHud.minimapIcon Ingame\\mini_jeep.dds\n";
  }
  FileSystem files;
  CHECK(files.mountDirectory(fixture));
  session::Settings settings;
  settings.playerName = "map-test";
  session::RemoteWorld remote(settings, files);
  con::Interpreter script(files, [&](const con::Command& c) { remote.registry.feed(c); });
  script.runFile("vehicle.con");
  remote.templateNumbers.add("jeep");
  net::bf2::Event vehicle, driver, enter;
  vehicle.object = net::bf2::CreateObject{.templateId = 0, .networkId = 100, .position = Vec3f{10, 0, 20}};
  driver.player = net::bf2::CreatePlayer{.team = 1, .id = 1, .name = "map-test"};
  remote.world.feedEvents({vehicle, driver});

  // Empty: white, the template's icon with forward slashes, 16 px.
  auto contacts = app::minimapContacts(remote);
  CHECK_EQ(contacts.size(), std::size_t(1));
  if (contacts.size() != 1) return;
  CHECK(contacts[0].kind == hud::MapContact::Kind::Neutral);
  CHECK_EQ(contacts[0].texture, std::string("Ingame/mini_jeep.dds"));
  CHECK_EQ(contacts[0].size, 16.0f);

  // A teammate aboard: our team's colour.
  enter.enter = net::bf2::EnterVehicle{1, 100, false};
  remote.world.feedEvents({enter});
  contacts = app::minimapContacts(remote);
  CHECK(contacts.size() == 1 && contacts[0].kind == hud::MapContact::Kind::Team);

  // It moves with the vehicle's state.
  net::bf2::SimpleObjectState moved;
  moved.position = Vec3f{40, 0, 60};
  CHECK(remote.world.feedControlledVehicle(100, 200, moved));
  contacts = app::minimapContacts(remote);
  CHECK(contacts.size() == 1 && contacts[0].worldX == 40 && contacts[0].worldZ == 60);

  net::bf2::Event leave;
  leave.exitPlayer = 1;
  remote.world.feedEvents({leave});
  contacts = app::minimapContacts(remote);
  CHECK(contacts.size() == 1 && contacts[0].kind == hud::MapContact::Kind::Neutral);

  net::bf2::Event destroyed;
  destroyed.destroyObject = 100;
  remote.world.feedEvents({destroyed});
  CHECK(app::minimapContacts(remote).empty());
  std::filesystem::remove_all(fixture);
}

static void testMapDrawsTheContacts() {
  hud::Node map;
  map.type = hud::NodeType::Map;
  map.mapView = hud::MapView::Mini;
  map.width = map.height = 192.0f;
  hud::Context context;
  context.mapTexture = "map.tga";
  context.mapWorldSize = 100.0f;
  using Kind = hud::MapContact::Kind;
  hud::MapContact order{0, 10, Kind::Order};
  order.order = 5;
  hud::MapContact pending{10, 0, Kind::PendingOrder};
  pending.order = 6;
  pending.commander = true;
  hud::MapContact spot{20, 20, Kind::Spotted};
  spot.blinks = true;
  hud::MapContact radio{-20, 20, Kind::Team};
  radio.radioBlink = true;
  context.minimapContacts = {{-10, 0, Kind::Team}, {0, -10, Kind::Squad}, order, pending,
                             spot,                 radio,                  {0, 0, Kind::Self}};
  const auto has = [](const std::vector<hud::DrawPiece>& pieces, std::string_view texture) {
    return std::ranges::any_of(pieces, [&](const auto& piece) { return piece.texture == texture; });
  };
  auto pieces = hud::buildNode(map, font::Font{}, "", {800, 600}, context);
  // The picture, then every contact but ourselves.
  CHECK_EQ(pieces.size(), std::size_t(7));
  if (pieces.size() != 7) return;
  // Our team's blue (table 0xa1ccc8's own entry) and our squad's green.
  CHECK(pieces[1].tint.b > pieces[1].tint.g && pieces[1].tint.g > pieces[1].tint.r);
  CHECK(pieces[2].tint.g > pieces[2].tint.b && pieces[2].tint.g > pieces[2].tint.r);
  // North is up: z 0 → 10 is a smaller screen y, so a larger NDC y.
  CHECK(pieces[3].geometry.vertices[0].position.y > pieces[1].geometry.vertices[0].position.y);
  CHECK(has(pieces, "Ingame/Minimap/Icons/orderAttack.tga"));
  CHECK(has(pieces, "Ingame/Minimap/Icons/orderDefendCommander.tga"));
  CHECK(has(pieces, "Ingame/Minimap/Icons/unitSpotted.dds"));
  // A radio mark is yellow while the blink is on (0x97e170).
  CHECK(pieces[6].tint.r == 1.0f && pieces[6].tint.g == 1.0f && pieces[6].tint.b == 0.0f);

  // With the blink off the position spot goes, and the radio mark is blue again.
  context.mapSpotsVisible = false;
  pieces = hud::buildNode(map, font::Font{}, "", {800, 600}, context);
  CHECK_EQ(pieces.size(), std::size_t(6));
  CHECK(!has(pieces, "Ingame/Minimap/Icons/unitSpotted.dds"));
  if (pieces.size() == 6) CHECK(pieces[5].tint.b > pieces[5].tint.r);

  // An enemy under a kind-2 mark is red (0x97dfc0) at the mark's fade.
  hud::MapContact swept{0, 0, Kind::Spotted};
  swept.swept = true;
  swept.alpha = 0.5f;
  swept.texture = "Ingame/Player/Icons/Minimap/mini_Soldier.tga";  // the mark's own
  context.minimapContacts = {swept};
  pieces = hud::buildNode(map, font::Font{}, "", {800, 600}, context);
  CHECK_EQ(pieces.size(), std::size_t(2));
  if (pieces.size() == 2) {
    CHECK(pieces[1].tint.r == 1.0f && pieces[1].tint.g == 0.0f && pieces[1].tint.a == 0.5f);
    CHECK_EQ(pieces[1].texture, std::string("Ingame/Player/Icons/Minimap/mini_Soldier.tga"));
  }

  // A world-sized icon spans its metres: 50 m of a 100 m world on a 192-unit map.
  hud::MapContact uav{0, 0, Kind::CommanderAsset};
  uav.texture = "Ingame/Minimap/map_UAV.tga";
  uav.worldSize = 50.0f;
  context.minimapContacts = {uav};
  pieces = hud::buildNode(map, font::Font{}, "", {800, 600}, context);
  CHECK_EQ(pieces.size(), std::size_t(2));
  if (pieces.size() == 2) {
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& vertex : pieces[1].geometry.vertices) {
      minX = std::min(minX, vertex.position.x);
      maxX = std::max(maxX, vertex.position.x);
    }
    const float pixels = (maxX - minX) * 0.5f * 800.0f;
    CHECK(std::abs(pixels - 96.0f) < 0.5f);
  }
}

static void testSpotBlink() {
  // +0xa30 adds dt: on at or below 0.5 s, off above, back to 0 past 1 s
  // (0x77c43c..0x77c471).
  hud::MapNode node;
  CHECK(!node.spotsVisible());
  node.update(0.25f);
  CHECK(node.spotsVisible());
  node.update(0.5f);
  CHECK(!node.spotsVisible());
  node.update(0.5f);
  CHECK(!node.spotsVisible());
  node.update(0.1f);
  CHECK(node.spotsVisible());
}

int main() {
  testVehicleFollowsItsOccupants();
  testMapDrawsTheContacts();
  testSpotBlink();
  if (obf2test::g_failures != 0) return 1;
  std::puts("OK");
  return 0;
}
