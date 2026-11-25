// The live HUD's two resolvers: a kill's weapon name (kill_feed.h, the order of
// `BF2HudManager::handleGameLogicEvent` case 3) and a chat string's sender
// (live_hud.h).
#include <map>
#include <string>

#include "check.h"
#include "obf2/app/kill_feed.h"
#include "obf2/app/live_hud.h"
#include "obf2/con/interpreter.h"

using namespace obf2;

namespace {

// The game's own lines (Objects_server.zip): soldiers/us/us_light_soldier.con:3,
// weapons/handheld/usrif_m4/usrif_m4.tweak:12,16 and
// vehicles/land/usjep_hmmwv/usjep_hmmwv.tweak:59,60. The knife has no weapon HUD
// here, to see the "no weapon" mark.
class Files : public con::FileProvider {
  std::optional<std::string> loadText(std::string_view) override {
    return "ObjectTemplate.create Soldier us_light_soldier\n"
           "ObjectTemplate.create GenericFireArm USRIF_M4\n"
           "ObjectTemplate.createComponent WeaponHud\n"
           "ObjectTemplate.weaponHud.hudName \"KILLMESSAGE_WEAPON_m4\"\n"
           "ObjectTemplate.create PlayerControlObject USJEP_HMMWV\n"
           "ObjectTemplate.createComponent VehicleHud\n"
           "ObjectTemplate.vehicleHud.hudName \"KILLMESSAGE_VEHICLE_hmmwv\"\n"
           "ObjectTemplate.create GenericFireArm kni_knife\n";
  }
};

struct World {
  game::Registry registry;
  game::TemplateNumbers numbers;
  World() {
    Files files;
    con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
    interpreter.runFile("objects.con");
    for (const char* name : {"us_light_soldier", "USRIF_M4", "USJEP_HMMWV", "kni_knife"}) {
      numbers.add(name);
    }
  }
  std::uint32_t number(const char* name) const { return *numbers.numberOf(name); }
};

void testWeaponOnFoot() {
  World world;
  net::bf2::Event::KilledBy kill;
  kill.vehicleTemplate = world.number("us_light_soldier");
  kill.weaponTemplate = world.number("USRIF_M4");
  const auto name = app::killWeaponName(kill, world.numbers, world.registry);
  CHECK_EQ(name.key, std::string("KILLMESSAGE_WEAPON_m4"));
  CHECK(!name.missingWeapon);
}

void testWeaponWithoutHudOnFoot() {
  World world;
  net::bf2::Event::KilledBy kill;
  kill.vehicleTemplate = world.number("us_light_soldier");
  kill.weaponTemplate = world.number("kni_knife");
  const auto name = app::killWeaponName(kill, world.numbers, world.registry);
  CHECK(name.key.empty());
  CHECK(name.missingWeapon);
}

void testVehicleNamesTheKill() {
  World world;
  net::bf2::Event::KilledBy kill;
  kill.vehicleTemplate = world.number("USJEP_HMMWV");
  kill.weaponTemplate = world.number("USRIF_M4");
  const auto name = app::killWeaponName(kill, world.numbers, world.registry);
  CHECK_EQ(name.key, std::string("KILLMESSAGE_VEHICLE_hmmwv"));
}

void testChatSender() {
  std::map<std::uint32_t, net::bf2::RemotePlayer> players;
  players[1].name = "Us";
  players[1].team = 1;
  players[1].squad = 2;
  players[4].name = "Mate";
  players[4].team = 1;
  players[4].squad = 2;
  players[5].name = "Other";
  players[5].team = 2;
  players[5].squad = 2;

  net::bf2::ChatString chat;
  chat.channel = 1;
  chat.type = 2;
  chat.sender = 4;
  chat.text = "hello";
  auto line = app::chatLineForHud(chat, players, 1);
  CHECK(line.senderKnown);
  CHECK_EQ(line.senderName, std::string("Mate"));
  CHECK(line.speaker.squadColour);

  chat.sender = 5;  // the same squad number on the other side is not our squad
  line = app::chatLineForHud(chat, players, 1);
  CHECK(line.senderKnown);
  CHECK(!line.speaker.squadColour);

  chat.sender = -1;  // a server line
  line = app::chatLineForHud(chat, players, 1);
  CHECK(!line.senderKnown);
  CHECK_EQ(line.text, std::string("hello"));
}

}  // namespace

TEST_MAIN({
  testWeaponOnFoot();
  testWeaponWithoutHudOnFoot();
  testVehicleNamesTheKill();
  testChatSender();
})
