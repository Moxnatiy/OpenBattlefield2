// A vehicle's seats (obf2/game/vehicle_seats.h): the control objects with a
// networkable, in `getAllPlayerControlObjects` order, and each seat's place from
// its `seatInformation`.
#include <cmath>
#include <map>
#include <string>

#include "check.h"
#include "obf2/con/interpreter.h"
#include "obf2/game/vehicle_seats.h"

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
    "ObjectTemplate.create PlayerControlObject jeep_mount\n";

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
}

}  // namespace

TEST_MAIN({ testOrderAndPlaces(); })
