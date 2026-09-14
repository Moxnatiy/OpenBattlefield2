// What an object the server created is, by where it stands.
//
// The shapes are cut down from Strike at Karkand's `gpm_cq/16`: a machine-gun
// spawner at the US gas station and one at a MEC point, and a barrel from the
// statics. On the live server the two machine guns came with different template
// numbers, 5561 and 5553 — the side's own vehicle, not the spawner.
#include <cmath>

#include "check.h"
#include "obf2/level/placement_index.h"

using namespace obf2;

namespace {

level::GameplayObjects karkand() {
  level::GameplayObjects out;
  level::ControlPoint us;
  us.id = 1;
  us.team = 2;
  level::ControlPoint mec;
  mec.id = 2;
  mec.team = 1;
  out.controlPoints = {us, mec};

  level::ObjectSpawner gas;
  gas.templateName = "CPNAME_SK_16_gasstation_SMG";
  gas.position = Vec3f{-231.0f, 157.0f, -300.0f};
  gas.rotation = Vec3f{90.0f, 0.0f, 0.0f};
  gas.controlPointId = 1;
  gas.templateByTeam = {{1, "mec_hmg"}, {2, "us_hmg"}};

  level::ObjectSpawner market;
  market.templateName = "CPNAME_SK_16_market_SMG";
  market.position = Vec3f{-150.0f, 156.0f, 120.0f};
  market.controlPointId = 2;
  market.templateByTeam = {{1, "mec_hmg"}, {2, "us_hmg"}};

  out.spawners = {gas, market};
  return out;
}

level::Level withBarrel() {
  level::Level out;
  level::StaticObject barrel;
  barrel.templateName = "barrel_yellow";
  barrel.position = Vec3f{-201.5f, 156.4f, -14.7f};
  out.objects.push_back(barrel);
  return out;
}

}  // namespace

// A spawner issues the vehicle of the side holding its point.
static void testSpawnerGivesTheSidesVehicle() {
  const level::GameplayObjects gameplay = karkand();
  CHECK_EQ(level::spawnerVehicle(gameplay, gameplay.spawners[0]), std::string("us_hmg"));
  CHECK_EQ(level::spawnerVehicle(gameplay, gameplay.spawners[1]), std::string("mec_hmg"));
}

static void testIndex() {
  const level::GameplayObjects gameplay = karkand();
  const level::Level lvl = withBarrel();
  const level::PlacementIndex index(gameplay, lvl);

  // Exactly on the gas station spawner: the US machine gun, with the spawner's yaw.
  const level::PlacedAt gun = index.at(Vec3f{-231.0f, 157.0f, -300.0f});
  CHECK(gun.kind == level::PlacedAt::Kind::Spawned);
  CHECK_EQ(gun.templateName, std::string("us_hmg"));
  CHECK(gun.hasRotation);
  CHECK(std::abs(gun.rotation.x - 90.0f) < 0.001f);

  // Within the two metres both sides' data leave room for.
  CHECK(index.at(Vec3f{-150.5f, 156.0f, 121.0f}).templateName == "mec_hmg");

  // A barrel the level already draws.
  const level::PlacedAt barrel = index.at(Vec3f{-201.5f, 156.4f, -14.7f});
  CHECK(barrel.kind == level::PlacedAt::Kind::Static);
  CHECK_EQ(barrel.templateName, std::string("barrel_yellow"));

  // Nothing in the level stands here: another player's soldier, say.
  CHECK(index.at(Vec3f{0.0f, 150.0f, 0.0f}).kind == level::PlacedAt::Kind::Unknown);
  CHECK(index.at(Vec3f{-231.0f, 157.0f, -303.0f}).kind == level::PlacedAt::Kind::Unknown);
}

TEST_MAIN({
  testSpawnerGivesTheSidesVehicle();
  testIndex();
})
