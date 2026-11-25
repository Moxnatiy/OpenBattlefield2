// When a hand weapon fires (`obf2/game/weapon_fire.h`): the M4's template from
// its own lines of `usrif_m4.tweak`, the automatic rate's cadence, and a single
// rate that needs a new press.
#include <map>
#include <cmath>
#include <string>

#include "check.h"
#include "obf2/game/weapon_fire.h"
#include "obf2/game/weapon_recoil.h"

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

const char* kM4 =
    "ObjectTemplate.create GenericFireArm usrif_m4\n"
    "ObjectTemplate.createComponent SingleFireComp\n"
    "ObjectTemplate.fire.addFireRate 2\n"
    "ObjectTemplate.fire.addFireRate 0\n"
    "ObjectTemplate.fire.fireInCameraDof 1\n";

}  // namespace

static void testM4Template() {
  const game::Registry registry = registryOf(kM4);
  const game::ObjectTemplate* m4 = registry.find("usrif_m4");
  CHECK(m4 != nullptr);
  if (m4 == nullptr) return;
  const game::FireTemplate fire = game::fireOf(*m4);
  CHECK_EQ(fire.roundsPerMinute, 600);  // the constructor's, the tweak sets none
  CHECK_EQ(fire.ticksBetweenRounds(), 3);
  CHECK_EQ(fire.fireRates.size(), std::size_t(2));
  if (fire.fireRates.size() == 2) {
    CHECK_EQ(fire.fireRates[0], 2);
    CHECK_EQ(fire.fireRates[1], 0);
  }
}

// Held on automatic: a round every third tick, from the first tick on.
static void testAutomaticCadence() {
  game::FireTemplate m4;
  m4.fireRates = {2, 0};
  game::WeaponFire fire;
  fire.setWeapon(m4);
  CHECK_EQ(fire.fireRate(), 2);
  std::string pattern;
  for (std::uint32_t t = 100; t < 110; ++t) pattern += fire.tick(t, true) ? 'x' : '.';
  CHECK_EQ(pattern, std::string("x..x..x..x"));
  CHECK_EQ(fire.roundsFired(), 4u);
  // Let go: nothing more.
  CHECK(!fire.tick(110, false));
}

// On the single rate a held trigger fires once; a new press fires again, but not
// before the weapon is ready.
static void testSingleNeedsNewPress() {
  game::FireTemplate m4;
  m4.fireRates = {2, 0};
  game::WeaponFire fire;
  fire.setWeapon(m4);
  fire.switchFireRate();
  CHECK_EQ(fire.fireRate(), 0);
  CHECK(fire.tick(10, true));
  CHECK(!fire.tick(11, true));
  CHECK(!fire.tick(12, true));
  CHECK(!fire.tick(13, true));
  CHECK(!fire.tick(14, false));
  CHECK(fire.tick(15, true));   // a new press, ready since 13
  CHECK(!fire.tick(16, false));
  CHECK(!fire.tick(17, true));  // pressed again too soon: 15 + 3
  fire.switchFireRate();
  CHECK_EQ(fire.fireRate(), 2);  // round the list
}

static void testBurstAndConfirmedMode() {
  game::FireTemplate rifle;
  rifle.fireRates = {game::kFireRateBurst, 0};
  rifle.roundsPerMinute = 900;
  game::WeaponFire fire;
  fire.setWeapon(rifle);
  CHECK(fire.tick(10, true));
  CHECK(!fire.tick(11, false));
  CHECK(fire.tick(12, false));
  CHECK(!fire.tick(13, false));
  CHECK(fire.tick(14, false));
  CHECK(!fire.tick(16, false));

  fire.setFireRate(0);
  CHECK(fire.tick(17, true));
  CHECK(!fire.tick(19, true));
  fire.setFireRate(2);
  CHECK_EQ(fire.fireRate(), 0);
  CHECK(fire.switchFireRate());
  CHECK_EQ(fire.fireRate(), game::kFireRateBurst);
}

// A template's own `fire.burstSize` (decoy_flare_launcher.tweak sets 5): one
// press fires that many rounds; without it, three.
static void testBurstSizeFromTemplate() {
  const auto registry = registryOf(
      "ObjectTemplate.create GenericFireArm flare_launcher\n"
      "ObjectTemplate.createComponent SingleFireComp\n"
      "ObjectTemplate.fire.addFireRate 1\n"
      "ObjectTemplate.fire.burstSize 5\n");
  const auto* launcher = registry.find("flare_launcher");
  CHECK(launcher != nullptr);
  if (!launcher) return;
  auto flares = game::fireOf(*launcher);
  CHECK_EQ(flares.burstSize, 5u);
  game::WeaponFire fire;
  fire.setWeapon(flares);
  int rounds = 0;
  for (std::uint32_t t = 0; t < 60; ++t) rounds += fire.tick(t, t == 0) ? 1 : 0;
  CHECK_EQ(rounds, 5);
  CHECK_EQ(game::fireOf(*registryOf(kM4).find("usrif_m4")).burstSize, 3u);
}

static void testShotRecoilUsesWeaponGraphAndZoom() {
  const auto registry = registryOf(std::string(kM4) +
      "ObjectTemplate.createComponent SoldierBasedRecoilComp\n"
      "ObjectTemplate.recoil.hasRecoilForce 1\n"
      "ObjectTemplate.recoil.recoilForceUp CRD_NONE/2/0/0\n"
      "ObjectTemplate.recoil.recoilForceLeftRight CRD_NONE/-1/0/0\n"
      "ObjectTemplate.recoil.recoilGraphFrameCount 4\n"
      "ObjectTemplate.recoil.recoilGraphExponent 1\n"
      "ObjectTemplate.recoil.recoilGraphTotalMovement 1\n"
      "ObjectTemplate.recoil.goBackOnRecoil 0\n"
      "ObjectTemplate.recoil.zoomModifier 0.5\n");
  game::WeaponRecoil recoil;
  recoil.setWeapon(registry.find("usrif_m4"));
  CHECK_EQ(recoil.delta(1).pitch, 0.0f);
  recoil.onRound(100, false);
  CHECK_EQ(recoil.delta(100).pitch, 0.0f);
  CHECK(std::abs(recoil.delta(101).pitch + 0.8f) < 0.0001f);
  CHECK(std::abs(recoil.delta(102).yaw - 0.3f) < 0.0001f);
  CHECK_EQ(recoil.delta(104).pitch, 0.0f);
  recoil.onRound(101, true);
  CHECK(std::abs(recoil.delta(102).pitch + 0.4f) < 0.0001f);
  recoil.setWeapon(nullptr);
  CHECK_EQ(recoil.delta(102).pitch, 0.0f);
}

TEST_MAIN({
  testM4Template();
  testAutomaticCadence();
  testSingleNeedsNewPress();
  testBurstAndConfirmedMode();
  testBurstSizeFromTemplate();
  testShotRecoilUsesWeaponGraphAndZoom();
})
