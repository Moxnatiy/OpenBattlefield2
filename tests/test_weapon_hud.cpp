// A weapon's crosshair (`weapon_hud.h`, docs/functions/hud-crosshair.md). The
// text is the game's own: `objects/weapons/handheld/rurif_ak101/rurif_ak101.tweak`.
#include <cmath>
#include <map>
#include <string>

#include "check.h"
#include "obf2/game/weapon_hud.h"

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

const char* kAk101 =
    "ObjectTemplate.create GenericFireArm rurif_ak101\n"
    "ObjectTemplate.createComponent WeaponHud\n"
    "ObjectTemplate.weaponHud.guiIndex 4\n"
    "ObjectTemplate.weaponHud.deviationFactor 5\n"
    "ObjectTemplate.weaponHud.maxVisualDeviation 28\n"
    "ObjectTemplate.weaponHud.minVisualDeviation 7\n"
    "ObjectTemplate.createComponent SoldierDeviationComp\n"
    "ObjectTemplate.deviation.setFireDev 2 0.2 0.05\n"
    "ObjectTemplate.deviation.minDev 0.45\n"
    "ObjectTemplate.deviation.setTurnDev 0 0 0 0\n"
    "ObjectTemplate.deviation.setSpeedDev 1 0.25 0.25 0.1\n"
    "ObjectTemplate.deviation.setMiscDev 3 1.5 0.05\n"
    "ObjectTemplate.deviation.devModStand 1.3\n"
    "ObjectTemplate.deviation.devModCrouch 1.2\n"
    "ObjectTemplate.deviation.devModZoom 0.8\n"
    "ObjectTemplate.itemIndex 3\n";

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

// The template's numbers land where the engine reads them, and what the file
// leaves out keeps the constructor's default (`devModLie` 1).
static void testTemplates() {
  const game::Registry registry = registryOf(kAk101);
  const game::ObjectTemplate* ak = registry.find("rurif_ak101");
  CHECK(ak != nullptr);
  if (ak == nullptr) return;
  const game::WeaponHudTemplate hud = game::weaponHudOf(*ak);
  CHECK_EQ(hud.guiIndex, 4);
  CHECK_EQ(hud.deviationFactor, 5);
  CHECK_EQ(hud.maxVisualDeviation, 28);
  CHECK_EQ(hud.minVisualDeviation, 7);
  const game::DeviationTemplate dev = game::deviationOf(*ak);
  CHECK(near(dev.fireMax, 2.0f) && near(dev.fireAdd, 0.2f) && near(dev.fireFall, 0.05f));
  CHECK(near(dev.minDev, 0.45f));
  CHECK(near(dev.speedMax, 1.0f) && near(dev.speedForward, 0.25f));
  CHECK(near(dev.speedStrafe, 0.25f) && near(dev.speedFall, 0.1f));
  CHECK(near(dev.miscMax, 3.0f) && near(dev.miscAdd, 1.5f) && near(dev.miscFall, 0.05f));
  CHECK(near(dev.modStand, 1.3f) && near(dev.modCrouch, 1.2f) && near(dev.modZoom, 0.8f));
  CHECK(near(dev.modLie, 1.0f));

  // A weapon with no `weaponHud` keeps `WeaponHudCompTemplate`'s defaults.
  const game::Registry bare = registryOf("ObjectTemplate.create GenericFireArm knife\n");
  const game::WeaponHudTemplate defaults = game::weaponHudOf(*bare.find("knife"));
  CHECK_EQ(defaults.guiIndex, 5);
  CHECK_EQ(defaults.deviationFactor, 10);
  CHECK_EQ(defaults.maxVisualDeviation, 1000);
}

// Standing still the deviation is the floor times the stance (0.45 × 1.3); the
// ticks sit `minVisualDeviation + ceil(0.585 × 5)` = 10 out from the centre.
static void testStandingStill() {
  const game::Registry registry = registryOf(kAk101);
  const game::ObjectTemplate* ak = registry.find("rurif_ak101");
  const game::DeviationTemplate dev = game::deviationOf(*ak);
  game::SoldierDeviation deviation;
  deviation.update(dev, {});
  CHECK(near(deviation.deviation(), 0.585f));
  const game::CrosshairTicks ticks = game::crosshairTicks(game::weaponHudOf(*ak), deviation.deviation());
  CHECK_EQ(ticks.up, 300 - 8 - 10);
  CHECK_EQ(ticks.down, 300 + 10 + 1);
  CHECK_EQ(ticks.left, 400 - 8 - 10 - 1);
  CHECK_EQ(ticks.right, 400 + 10);
}

// Running: the speed term grows by 0.25 a tick to its ceiling of 1 and stays;
// let go, it falls by 0.1 a tick to nothing. Crouching takes the crouch mod.
static void testRunningAndStopping() {
  const game::Registry registry = registryOf(kAk101);
  const game::DeviationTemplate dev = game::deviationOf(*registry.find("rurif_ak101"));
  game::SoldierDeviation deviation;
  game::DeviationInput run;
  run.throttle = 1.0f;
  for (int tick = 0; tick < 3; ++tick) deviation.update(dev, run);
  CHECK(near(deviation.speed(), 0.75f));
  for (int tick = 0; tick < 10; ++tick) deviation.update(dev, run);
  CHECK(near(deviation.speed(), 1.0f));
  CHECK(near(deviation.deviation(), (0.45f + 1.0f) * 1.3f));
  // Strafing too adds its own share — but the term is already at its ceiling,
  // where the engine lets it fall by its decay, not below the ceiling.
  run.strafe = -1.0f;
  deviation.update(dev, run);
  CHECK(near(deviation.speed(), 1.0f));

  game::DeviationInput still;
  still.pose = 1;
  deviation.update(dev, still);
  CHECK(near(deviation.speed(), 0.9f));
  CHECK(near(deviation.deviation(), (0.45f + 0.9f) * 1.2f));
  for (int tick = 0; tick < 20; ++tick) deviation.update(dev, still);
  CHECK(near(deviation.speed(), 0.0f));
}

// A jump adds `miscAdd − miscFall` and the term then falls back; a large
// deviation stops the ticks at `maxVisualDeviation`.
static void testJumpAndCeiling() {
  const game::Registry registry = registryOf(kAk101);
  const game::ObjectTemplate* ak = registry.find("rurif_ak101");
  const game::DeviationTemplate dev = game::deviationOf(*ak);
  game::SoldierDeviation deviation;
  game::DeviationInput jump;
  jump.jumpOrLie = true;
  deviation.update(dev, jump);
  CHECK(near(deviation.misc(), 1.45f));
  deviation.update(dev, jump);
  deviation.update(dev, jump);
  CHECK(near(deviation.misc(), 3.0f));
  deviation.update(dev, {});
  CHECK(near(deviation.misc(), 2.95f));

  const game::CrosshairTicks wide = game::crosshairTicks(game::weaponHudOf(*ak), 100.0f);
  CHECK_EQ(wide.up, 300 - 8 - 28);
  CHECK_EQ(wide.right, 400 + 28);
}

TEST_MAIN({
  testTemplates();
  testStandingStill();
  testRunningAndStopping();
  testJumpAndCeiling();
})
