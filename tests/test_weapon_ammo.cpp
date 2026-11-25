// A hand weapon's magazines and reload (`obf2/game/weapon_ammo.h`): the template
// from its own `ammo.*` lines, the defaults of `AmmoCompTemplate`'s constructor,
// and the state machine of `AmmoComp` tick by tick (Linux 0x5ea680 and its
// neighbours). Also the HUD numbers drawn from it (`obf2/app/ammo_hud.h`).
#include <map>
#include <string>

#include "check.h"
#include "obf2/app/ammo_hud.h"
#include "obf2/game/weapon_ammo.h"

using namespace obf2;

namespace {

class MemoryFiles : public con::FileProvider {
 public:
  std::map<std::string, std::string> files;
  std::optional<std::string> loadText(std::string_view path) override {
    const auto it = files.find(std::string(path));
    return it == files.end() ? std::nullopt : std::optional<std::string>{it->second};
  }
};

game::Registry registryOf(const std::string& text) {
  MemoryFiles files;
  files.files["all.con"] = text;
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("all.con");
  return registry;
}

}  // namespace

// The scar's own lines say nothing of magazines: the constructor's 30 and 5
// stand. The M24's say 5 rounds in 7 mags.
static void testTemplate() {
  const game::Registry registry = registryOf(
      "ObjectTemplate.create GenericFireArm scar\n"
      "ObjectTemplate.createComponent DefaultAmmoComp\n"
      "ObjectTemplate.ammo.ammoType 1\n"
      "ObjectTemplate.ammo.reloadTime 4\n"
      "ObjectTemplate.ammo.reloadWithoutPlayer 1\n"
      "ObjectTemplate.create GenericFireArm sniper\n"
      "ObjectTemplate.createComponent DefaultAmmoComp\n"
      "ObjectTemplate.ammo.magSize 5\n"
      "ObjectTemplate.ammo.nrOfMags 7\n"
      "ObjectTemplate.ammo.changeMagAt 0.8\n"
      "ObjectTemplate.ammo.autoReload 1\n");
  const game::ObjectTemplate* scar = registry.find("scar");
  const game::ObjectTemplate* sniper = registry.find("sniper");
  CHECK(scar != nullptr && sniper != nullptr);
  if (scar == nullptr || sniper == nullptr) return;
  const game::AmmoTemplate a = game::ammoOf(*scar);
  CHECK_EQ(a.magSize, 30);
  CHECK_EQ(a.nrOfMags, 5);
  CHECK_EQ(a.reloadTicks(), 120);  // 4 s at 30 ticks
  CHECK(!a.autoReload);
  CHECK(a.reloadWithoutPlayer);
  const game::AmmoTemplate b = game::ammoOf(*sniper);
  CHECK_EQ(b.magSize, 5);
  CHECK_EQ(b.nrOfMags, 7);
  CHECK(b.autoReload);
  CHECK_EQ(b.reloadTicks(), 60);  // the constructor's 2 s
  CHECK_EQ(b.changeMagTicks(), 48);  // 60 × 0.8
}

// A fresh weapon: the magazine in it and the rest spare.
static void testMagazines() {
  game::WeaponAmmo ammo;
  ammo.setWeapon(game::AmmoTemplate{});
  CHECK_EQ(ammo.roundsInMag(), 30);
  CHECK_EQ(ammo.spareMags(), 4);
  CHECK(ammo.canFire());
  ammo.onRound(10);
  ammo.onRound(13);
  CHECK_EQ(ammo.roundsInMag(), 28);
  // A full magazine is not reloaded.
  game::WeaponAmmo full;
  full.setWeapon(game::AmmoTemplate{});
  CHECK(!full.requestReload(5));
}

static void testServerResupply() {
  game::AmmoTemplate grenade;
  grenade.magSize = 1;
  grenade.nrOfMags = 4;
  game::WeaponAmmo ammo;
  ammo.setWeapon(grenade);
  ammo.synchronize(0, 1);
  CHECK(!ammo.canFire());
  ammo.synchronize(0, 5);
  CHECK_EQ(ammo.spareMags(), 0);
  ammo.synchronize(1, 4);
  CHECK(ammo.canFire());
  CHECK_EQ(ammo.spareMags(), 3);
  CHECK_EQ(app::ammoHud(ammo, 0, 1.0f).values.at("PrimaryClips"), 3.0f);
}

// The reload, tick by tick: the message makes state 2 at once, the update of the same
// tick starts it (state 3, message 2 once), the magazine changes at `changeMagAt`, and the
// reload ends at the reload time.
static void testReloadTimeline() {
  game::WeaponAmmo ammo;
  ammo.setWeapon(game::AmmoTemplate{});  // 2 s: 60 ticks, magazine at 42
  for (std::uint32_t t = 100; t < 103; ++t) ammo.onRound(t);
  CHECK_EQ(ammo.roundsInMag(), 27);
  CHECK(ammo.requestReload(110));
  CHECK_EQ(ammo.state(), 2);
  CHECK(!ammo.requestReload(110));  // asked twice in a tick: nothing (0x5e75a1)
  CHECK(!ammo.canFire());
  CHECK(game::ReloadMessage::Once == ammo.update(110));  // doReload: state 3, message 2 once
  CHECK_EQ(ammo.state(), 3);
  CHECK(ammo.reloading());
  for (std::uint32_t t = 111; t < 152; ++t) {
    CHECK(game::ReloadMessage::None == ammo.update(t));
    CHECK_EQ(ammo.roundsInMag(), 27);
    CHECK(!ammo.canFire());
  }
  // 110 + 42 = 152: the magazine changes, a spare is used up.
  CHECK(game::ReloadMessage::None == ammo.update(152));
  CHECK_EQ(ammo.roundsInMag(), 30);
  CHECK_EQ(ammo.spareMags(), 3);
  // 110 + 60 = 170: over.
  for (std::uint32_t t = 153; t < 170; ++t) {
    ammo.update(t);
    CHECK_EQ(ammo.state(), 3);
  }
  ammo.update(170);
  CHECK_EQ(ammo.state(), 0);
  CHECK(ammo.canFire());
}

// A weapon with `autoReload` refuses the key (`requestReload(true)`, 0x5eab77).
static void testAutoReloadRefusesKey() {
  game::AmmoTemplate tmpl;
  tmpl.autoReload = true;
  game::WeaponAmmo ammo;
  ammo.setWeapon(tmpl);
  ammo.onRound(1);
  CHECK(!ammo.requestReload(5));
}

// No spare magazine, no reload; an empty one cannot fire.
static void testNoSpare() {
  game::AmmoTemplate tmpl;
  tmpl.magSize = 2;
  tmpl.nrOfMags = 1;
  game::WeaponAmmo ammo;
  ammo.setWeapon(tmpl);
  CHECK_EQ(ammo.spareMags(), 0);
  ammo.onRound(1);
  CHECK(!ammo.requestReload(5));
  ammo.onRound(4);
  CHECK_EQ(ammo.roundsInMag(), 0);
  CHECK(!ammo.canFire());
}

// `autoReload`: the round that empties the magazine starts a reload by itself,
// without a key.
static void testAutoReload() {
  game::AmmoTemplate tmpl;
  tmpl.magSize = 2;
  tmpl.nrOfMags = 3;
  tmpl.autoReload = true;
  game::WeaponAmmo ammo;
  ammo.setWeapon(tmpl);
  ammo.onRound(10);
  ammo.onRound(14);
  CHECK_EQ(ammo.roundsInMag(), 0);
  int onces = 0;
  for (std::uint32_t t = 14; t < 40; ++t) {
    if (ammo.update(t) == game::ReloadMessage::Once) ++onces;
  }
  CHECK_EQ(onces, 1);
  for (std::uint32_t t = 40; t < 120; ++t) ammo.update(t);
  CHECK_EQ(ammo.roundsInMag(), 2);
  CHECK_EQ(ammo.spareMags(), 1);
}

// The HUD: the fraction, the string, the magazines, and the count frozen while
// the reload bar is up.
static void testHud() {
  game::WeaponAmmo ammo;
  ammo.setWeapon(game::AmmoTemplate{});
  ammo.onRound(1);
  ammo.onRound(4);
  app::AmmoHud hud = app::ammoHud(ammo, 5, 1.0f);
  CHECK_EQ(hud.strings["PrimaryAmmoString"], std::string("28"));
  CHECK(hud.values["PrimaryAmmo"] > 0.93f && hud.values["PrimaryAmmo"] < 0.94f);
  CHECK_EQ(hud.values["PrimaryClips"], 4.0f);
  CHECK(hud.flags["PrimaryClip1Show"] && hud.flags["PrimaryClip2Show"] && hud.flags["PrimaryClip3Show"]);
  CHECK_EQ(hud.strings["PrimaryClipString"], std::string("4"));
  CHECK(!hud.flags["PrimaryReloadTimeShow"]);
  // While the last frame's reload value is between 0 and 1, the block is skipped.
  hud = app::ammoHud(ammo, 5, 0.5f);
  CHECK(hud.strings.find("PrimaryAmmoString") == hud.strings.end());
  CHECK(hud.values.find("PrimaryReloadTime") != hud.values.end());
}

TEST_MAIN({
  testTemplate();
  testMagazines();
  testServerResupply();
  testReloadTimeline();
  testAutoReloadRefusesKey();
  testNoSpare();
  testAutoReload();
  testHud();
})
