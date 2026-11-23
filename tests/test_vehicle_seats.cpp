// A vehicle's seats (obf2/game/vehicle_seats.h): the control objects with a
// networkable, in `getAllPlayerControlObjects` order, and each seat's place from
// its `seatInformation`.
#include <cmath>
#include <map>
#include <string>

#include "check.h"
#include "obf2/con/interpreter.h"
#include "obf2/game/vehicle_seats.h"
#include "obf2/game/scene.h"

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

// Modelled on the HMMWV: the hull is the driver; a codriver and a rear seat are
// children, the gunner under the cupola under the rear seat; a camera mount has no
// networkable and is no seat.
constexpr const char* kJeep =
    "ObjectTemplate.create PlayerControlObject jeep\n"
    "ObjectTemplate.setNetworkableInfo BasicInfo\n"
    "ObjectTemplate.addTemplate jeep_codriver\n"
    "ObjectTemplate.setPosition 0/0/-1\n"
    "ObjectTemplate.addTemplate jeep_rear\n"
    "ObjectTemplate.setPosition 0/1/-2\n"
    "ObjectTemplate.seatInformation jeep -0.5/0.5/0.2 0/0/0\n"
    "ObjectTemplate.seatAnimationSystem driver.inc\n"
    "ObjectTemplate.create PlayerControlObject jeep_codriver\n"
    "ObjectTemplate.setNetworkableInfo BasicInfo\n"
    "ObjectTemplate.seatInformation jeep_codriver 0.25/1.3/-1 180/0/0\n"
    "ObjectTemplate.create PlayerControlObject jeep_rear\n"
    "ObjectTemplate.setNetworkableInfo BasicInfo\n"
    "ObjectTemplate.addTemplate jeep_cupola\n"
    "ObjectTemplate.create PlayerControlObject jeep_cupola\n"
    "ObjectTemplate.setNetworkableInfo BasicInfo\n"
    "ObjectTemplate.cockpitSubGeom 1\n"
    "ObjectTemplate.addTemplate jeep_mount\n"
    "ObjectTemplate.setPosition 0/0.5/0\n"
    "ObjectTemplate.create PlayerControlObject jeep_mount\n"
    "ObjectTemplate.addTemplate duck_camera\n"
    "ObjectTemplate.addTemplate gun_camera\n"
    "ObjectTemplate.setPosition 0/0.25/0.5\n"
    "ObjectTemplate.create Camera duck_camera\n"
    "ObjectTemplate.CameraId 1\n"
    "ObjectTemplate.create Camera gun_camera\n"
    "ObjectTemplate.chaseDistance 16\n"
    "ObjectTemplate.chaseAngle 0.2\n"
    "ObjectTemplate.chaseOffset 0/1/7\n"
    "ObjectTemplate.worldFOV 1.1\n"
    "ObjectTemplate.insideFOV 0.9\n";

void testOrderAndPlaces() {
  MemoryFiles files;
  files.files["a.con"] = kJeep;
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("a.con");
  const auto seats = game::vehicleSeats(registry, "jeep");
  CHECK_EQ(seats.size(), std::size_t{4});
  if (seats.size() != 4) return;
  CHECK(seats[0].templateName == "jeep");
  CHECK(seats[1].templateName == "jeep_codriver");
  CHECK(seats[2].templateName == "jeep_rear");
  CHECK(seats[3].templateName == "jeep_cupola");
  CHECK(seats[0].animationSystem == "driver.inc");
  // The driver's place: the hull plus the offset.
  const Vec3f driver = transformPoint(seats[0].transform, Vec3f{0, 0, 0});
  CHECK(std::fabs(driver.x + 0.5f) < 1e-4f && std::fabs(driver.y - 0.5f) < 1e-4f);
  // The codriver's: his own object (0/0/-1) plus his offset.
  const Vec3f codriver = transformPoint(seats[1].transform, Vec3f{0, 0, 0});
  CHECK(std::fabs(codriver.y - 1.3f) < 1e-4f && std::fabs(codriver.z + 2.0f) < 1e-4f);
  CHECK(!seats[2].hasSeatInformation);
  CHECK(!seats[2].camera);
  CHECK(!seats[2].cockpitGeometry);
  CHECK(seats[3].cockpitGeometry == 1);
  CHECK(seats[3].camera.has_value());
  if (const auto& camera = seats[3].camera) {
    const auto anchor = transformPoint(camera->transform, {});
    CHECK(std::abs(anchor.y - 1.75f) < 1e-4f && std::abs(anchor.z + 1.5f) < 1e-4f);
    CHECK_EQ(camera->chaseDistance, 16.0f);
    CHECK_EQ(camera->chaseAngle, 0.2f);
    CHECK_EQ(camera->chaseOffset.z, 7.0f);
    CHECK_EQ(camera->fieldOfView, 1.1f);
    CHECK_EQ(camera->insideFieldOfView, 0.9f);
  }
}

void testSeatAndCameraFollowTurretAncestors() {
  MemoryFiles files;
  // The bundles carry a networkable, as in the game's data
  // (Objects_server.zip: Vehicles/Land/USTNK_M1A2/USTNK_M1A2.tweak, lines 160
  // and 561), which is what makes them descriptor members of their seats.
  files.files["tank.con"] =
      "ObjectTemplate.create PlayerControlObject USTNK_M1A2\n"
      "ObjectTemplate.setNetworkableInfo BasicInfo\n"
      "ObjectTemplate.addTemplate USTNK_M1A2_Turret\n"
      "ObjectTemplate.setPosition 0/1/0\n"
      "ObjectTemplate.addTemplate passenger\n"
      "ObjectTemplate.create RotationalBundle USTNK_M1A2_Turret\n"
      "ObjectTemplate.setNetworkableInfo BasicInfo\n"
      "ObjectTemplate.addTemplate gunner\n"
      "ObjectTemplate.create PlayerControlObject gunner\n"
      "ObjectTemplate.setNetworkableInfo BasicInfo\n"
      "ObjectTemplate.seatInformation USTNK_M1A2_Cupola 0/0/0.25 0/0/0\n"
      "ObjectTemplate.addTemplate USTNK_M1A2_Cupola\n"
      "ObjectTemplate.setPosition 0.5/0.4/0\n"
      "ObjectTemplate.create RotationalBundle USTNK_M1A2_Cupola\n"
      "ObjectTemplate.setNetworkableInfo BasicInfo\n"
      "ObjectTemplate.addTemplate gunner_camera\n"
      "ObjectTemplate.create Camera gunner_camera\n"
      "ObjectTemplate.create PlayerControlObject passenger\n"
      "ObjectTemplate.setNetworkableInfo BasicInfo\n"
      "ObjectTemplate.seatInformation ustnk_m1a2_cupola -0.2/0/0 0/0/0\n";
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("tank.con");
  const auto seats = game::vehicleSeats(registry, "USTNK_M1A2");
  CHECK_EQ(seats.size(), std::size_t{3});
  if (seats.size() != 3) return;
  CHECK(seats[0].joints.empty());
  CHECK_EQ(seats[1].joints.size(), std::size_t{2});
  CHECK_EQ(seats[2].joints.size(), std::size_t{2});
  CHECK(seats[1].camera.has_value());
  if (!seats[1].camera || seats[1].joints.size() != 2 || seats[2].joints.size() != 2) return;
  CHECK_EQ(seats[1].camera->joints.size(), std::size_t{2});
  CHECK_EQ(seats[1].joints[0].seat, 0u);
  CHECK_EQ(seats[1].joints[1].seat, 1u);
  CHECK_EQ(seats[2].joints[1].seat, 1u);
  CHECK_EQ(transformPoint(seats[1].joints[0].rest, {}).y, 1.0f);
  CHECK_EQ(transformPoint(seats[1].joints[1].rest, {}).x, 0.5f);
}

void testUnlistedMountedWeapon() {
  MemoryFiles files;
  files.files["boat.con"] = R"CON(
ObjectTemplate.create PlayerControlObject test_boat
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.addTemplate gunner
ObjectTemplate.create PlayerControlObject gunner
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.seatInformation gunner 0/1/0 0/0/0
ObjectTemplate.addTemplate yaw_mount
ObjectTemplate.create RotationalBundle yaw_mount
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.setInputToYaw PIYaw
ObjectTemplate.addTemplate pitch_mount
ObjectTemplate.create RotationalBundle pitch_mount
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.addTemplate gun
ObjectTemplate.addTemplate camera
ObjectTemplate.create GenericFireArm gun
ObjectTemplate.geometry mounted_gun
ObjectTemplate.create Camera camera
)CON";
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("boat.con");
  const auto joints = game::vehicleTurretBundles(registry, "test_boat", 1);
  CHECK_EQ(joints.size(), std::size_t{2});
  CHECK_EQ(joints[0], std::string("yaw_mount"));
  CHECK_EQ(joints[1], std::string("pitch_mount"));
  const auto seats = game::vehicleSeats(registry, "test_boat");
  CHECK_EQ(seats.size(), std::size_t{2});
  CHECK_EQ(seats[1].joints.size(), std::size_t{1});
  CHECK(seats[1].camera.has_value());
  CHECK_EQ(seats[1].camera->joints.size(), std::size_t{2});
}

void testWingNetworkables() {
  // A Wing carries RotationalBundle's networkable (Linux Wing vtable,
  // RotationalBundle::initNetworkableComp). FUN_006a5e20 compares rotation
  // only within one class id: two equal Wings share (F35B ElevatorL follows
  // ElevatorR); a Wing equal to a RotationalBundle (0x9483 vs 0x947d) does not.
  MemoryFiles files;
  files.files["wing.con"] = R"CON(
ObjectTemplate.create PlayerControlObject jet
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.addTemplate bundle
ObjectTemplate.addTemplate elevator_r
ObjectTemplate.addTemplate elevator_l
ObjectTemplate.create RotationalBundle bundle
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.setInputToPitch PIPitch
ObjectTemplate.setMaxRotation 0/8/0
ObjectTemplate.create Wing elevator_r
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.setInputToPitch PIPitch
ObjectTemplate.setMaxRotation 0/8/0
ObjectTemplate.create Wing elevator_l
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.setInputToPitch PIPitch
ObjectTemplate.setMaxRotation 0/8/0
)CON";
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("wing.con");
  const auto members = game::vehicleDescriptorMembers(registry, "jet", 0);
  CHECK_EQ(members.size(), std::size_t{2});
  if (members.size() != 2) return;
  CHECK(members[0].sharing.empty());
  CHECK(members[1].kind == game::DescriptorMember::Kind::Rotational);
  CHECK_EQ(members[1].templateName, std::string("elevator_r"));
  CHECK_EQ(members[1].sharing.size(), std::size_t{1});
}

void testSharedNetworkables() {
  // BF2.exe 0x6aaa10 / 0x6a5e20: a bundle with the same rotation settings as
  // an earlier one has no networkable of its own (M1A2 Barrel_Base follows
  // Hud_Spring, FAAV nav_LF follows nav_RF). The engine's gear count defaults
  // to 1 (Linux 0x632538), a 2-bit gear field.
  MemoryFiles files;
  files.files["shared.con"] = R"CON(
ObjectTemplate.create PlayerControlObject tank
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.addTemplate turret
ObjectTemplate.addTemplate engine
ObjectTemplate.create RotationalBundle turret
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.setInputToYaw PIMouseLookX
ObjectTemplate.setMaxSpeed 30/0/0
ObjectTemplate.addTemplate hud_spring
ObjectTemplate.addTemplate barrel_base
ObjectTemplate.create RotationalBundle hud_spring
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.setInputToPitch PIMouseLookY
ObjectTemplate.setMinRotation 0/-25/0
ObjectTemplate.setMaxSpeed 0/30/0
ObjectTemplate.create RotationalBundle barrel_base
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.setInputToPitch PIMouseLookY
ObjectTemplate.setMinRotation 0/-25/0
ObjectTemplate.setMaxSpeed 0/30/0
ObjectTemplate.create Engine engine
ObjectTemplate.setNetworkableInfo BasicInfo
ObjectTemplate.setEngineType c_ETTank
)CON";
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("shared.con");
  const auto members = game::vehicleDescriptorMembers(registry, "tank", 0);
  CHECK_EQ(members.size(), std::size_t{3});
  if (members.size() != 3) return;
  CHECK_EQ(members[0].templateName, std::string("turret"));
  CHECK(members[0].sharing.empty());
  CHECK_EQ(members[1].templateName, std::string("hud_spring"));
  CHECK_EQ(members[1].sharing.size(), std::size_t{1});
  CHECK(members[2].kind == game::DescriptorMember::Kind::Engine);
  CHECK_EQ(members[2].gearBits, 2u);
  const auto objects = game::vehicleTurretBundleObjects(registry, "tank", 0);
  CHECK_EQ(objects.size(), std::size_t{2});
  if (objects.size() == 2 && objects[1].size() == 2) CHECK_EQ(objects[1][1], std::string("barrel_base"));
}

void testZeroCockpitGeometry() {
  // ahe_havoc.bundledmesh has an interior at index 0; absence is different.
  MemoryFiles files;
  files.files["cockpit.con"] =
      "ObjectTemplate.create PlayerControlObject helicopter\n"
      "ObjectTemplate.setNetworkableInfo BasicInfo\n"
      "ObjectTemplate.cockpitSubGeom 0\n"
      "ObjectTemplate.addTemplate passenger\n"
      "ObjectTemplate.create PlayerControlObject passenger\n"
      "ObjectTemplate.setNetworkableInfo BasicInfo\n";
  game::Registry registry;
  con::Interpreter interpreter(files, [&](const con::Command& c) { registry.feed(c); });
  interpreter.runFile("cockpit.con");
  const auto seats = game::vehicleSeats(registry, "helicopter");
  CHECK_EQ(seats.size(), std::size_t{2});
  if (seats.size() != 2) return;
  CHECK(seats[0].cockpitGeometry == 0);
  CHECK(!seats[1].cockpitGeometry);
}

}  // namespace

TEST_MAIN({
  testSharedNetworkables();
  testWingNetworkables();
  testZeroCockpitGeometry();
  testUnlistedMountedWeapon();
  testOrderAndPlaces();
  testSeatAndCameraFollowTurretAncestors();
})
