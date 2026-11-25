// The vehicle HUD's flight values (app/vehicle_flight_hud.h; BF2.exe 0x78cb10,
// 0x78ce60): angle, banking, altitude, speed and the torque's easing.
#include "check.h"
#include "obf2/app/vehicle_flight_hud.h"
#include "obf2/app/player_hud.h"
#include <cmath>
using namespace obf2;
using namespace obf2::app;

static void run() {
  VehicleFlightInput input;
  input.world = translation({10.0f, 164.4f, -90.0f});
  input.seaLevel = 145.0f;
  input.velocity = Vec3f{3.0f, 0.0f, 4.0f};  // |v| = 5 m/s
  input.throttle = 1.0f;
  const auto level = vehicleFlightHud(input, 0.0f);
  CHECK(std::fabs(level.angle) < 1e-6f);                      // forward is +z
  CHECK(std::fabs(level.banking) < 1e-6f);                    // right's y is 0
  CHECK(level.angleOfAttack == 296);                          // ceil(0 * 100 + 296)
  CHECK(level.altitude == "20");                              // ceil(164.4 - 145)
  CHECK(level.speed && *level.speed == "50");                 // trunc(5) * 10
  CHECK(level.elevationSpeedAngle && std::fabs(*level.elevationSpeedAngle) < 1e-6f);  // vy = 0
  input.velocity = Vec3f{0.0f, 5.0f, 0.0f};
  CHECK(std::fabs(*vehicleFlightHud(input, 0.0f).elevationSpeedAngle - 0.6283185f) < 1e-5f);  // 4π/10·5·0.1
  input.velocity = Vec3f{3.0f, 0.0f, 4.0f};
  CHECK(std::fabs(level.torque - 0.01f) < 1e-6f);             // 1% of the gap
  CHECK(level.torqueText == "1");                             // ceil(1.0)
  // A negative throttle is a target of 0: the torque eases down.
  input.throttle = -1.0f;
  const auto down = vehicleFlightHud(input, 0.5f);
  CHECK(std::fabs(down.torque - 0.495f) < 1e-6f);
  CHECK(down.torqueText == "50");                             // ceil(49.5)
  // No velocity: the speed text is left as it was.
  input.velocity.reset();
  CHECK(!vehicleFlightHud(input, 0.0f).speed);
  // Yawed 90 degrees: the forward row is +x, atan2(1, 0) = pi/2.
  input.world = rotationYawPitchRoll(90.0f, 0.0f, 0.0f);
  CHECK(std::fabs(std::fabs(vehicleFlightHud(input, 0.0f).angle) - 1.5707964f) < 1e-4f);
  // The round clock (0x464870; Linux getTimeToNextStateChange 0x4754c0).
  CHECK(std::fabs(timeToNextStateChange(36001, 1) - 1200.0f) < 1e-3f);
  CHECK(timeToNextStateChange(10, 20) == 0.0f);
  CHECK(levelTimeText(1200.0f) == "20:00");
  CHECK(levelTimeText(1147.4f) == "19:07");
  CHECK(levelTimeText(42.0f) == "42");          // no minutes: no "00:"
  CHECK(levelTimeText(3725.0f) == "1:02:05");
}

TEST_MAIN(run())
