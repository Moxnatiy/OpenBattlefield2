#pragma once
// Movement physics — on the game's own constants.
//
// BF2 keeps them in data rather than in code: `objects/soldiers/common/common.con`
// sets
//
//   Vars.Set phy-soldier-acceleration        0.2
//   Vars.Set phy-soldier-deceleration        0.4
//   Vars.Set phy-soldier-air-movement-factor 0.05
//   Vars.Set phy-soldier-speed-factor        1.0
//   Vars.Set phy-soldier-jump-factor         1.0
//
// These values are read by our own interpreter and end up here — so the
// behaviour matches the original as far as the data defines it at all.
//
// **What is deliberately not here.** The base walking speed in BF2 is set not by
// a number but by **animation**: the `AnimationSystem3p.inc` system moves the
// soldier along with the clip's playback. Until skeletal animation exists, the
// speed comes from the server's settings, and that is the one place where we
// depart from the original. Noted in docs/TODO.md.
#include <vector>

#include "obf2/core/math.h"
#include "obf2/engine/console.h"

namespace obf2::server {

struct PhysicsConstants {
  // The share of the maximum speed added per tick. 0.2 means the soldier
  // accelerates over roughly five ticks.
  float acceleration = 0.2f;
  float deceleration = 0.4f;
  // How controllable movement stays in the air: 0.05 is next to nothing.
  float airMovementFactor = 0.05f;
  float speedFactor = 1.0f;

  // The look multipliers: the engine adds the angle as `axis * factor`.
  //
  //   yaw   += axis_x * lookFactorX
  //   pitch += axis_y * lookFactorY
  //
  // In the client this is visible directly (`BF2.exe`, 0x5a99a0): two global
  // float constants at 0x9ec248 and 0x9ec344, both read from `Vars` with a
  // default of 5.0 (0x853aa0 and 0x853ac0, `PUSH 0x40a00000`), and between them
  // there is also the sight's multiplier (0x5a61a0), which is 1.0 outside zoom.
  //
  // The angle is in degrees: in radians a full turn would need an axis sum of
  // 1.26, that is the tiniest twitch of the mouse.
  float lookFactorX = 5.0f;  // phy-soldier-look-factor-x
  float lookFactorY = 5.0f;  // phy-soldier-look-factor-y
  // `phy-soldier-jump-factor`. The engine's default is 0.98 (the constant at
  // 0xb4e8d4 in the same `Vars::getFloat` list), but the game's data sets 1.0
  // (`objects/soldiers/common/common.con`), and the data wins.
  float jumpFactor = 1.0f;

  // The acceleration of free fall. **Not a physical constant and not 9.81**: the
  // engine writes it in `BasicPhysicsSystem`'s constructor (Linux server,
  // 0x6d6ae4: `movl $0xc16bae14, 0xc(%rdi)`), and `getGravity` (0x6d69d0)
  // returns exactly that field. 0xc16bae14 as a float is -14.73.
  //
  // It used to hold 9.81 "because that is the physical constant", and that is
  // exactly why a jump hung in the air half again as long as the original's.
  float gravity = 14.73f;

  // The jump's initial speed. Also from the binary: `Soldier::handlePlayerInput`
  // (0x54fda6) takes the constant 6.0 at 0xb355c8, multiplies it by
  // `g_soldierJumpFactor` and puts it into the **Y** of the vector passed to the body.
  //
  // The jump comes out as: height 6^2/(2*14.73) = 1.22 m, in the air
  // 2*6/14.73 = 0.81 s.
  float jumpSpeed = 6.0f;

  // --- the engine's constants (docs/functions/soldier-physics.md) ---
  //
  // These are the engine's own defaults, pulled from the Linux server. The game's
  // data may override them through those same Vars.Set — and then the data wins,
  // as in the original.
  float walkSpeed = 1.5f;      // phy-soldier-walk-speed
  float runSpeed = 3.9f;       // phy-soldier-run-speed
  float sprintSpeed = 7.0f;    // phy-soldier-sprint-speed
  float crouchSpeed = 2.0f;    // phy-soldier-crouch-speed
  float crawlSpeed = 0.8f;     // phy-soldier-crawl-speed
  float swimSpeed = 2.1f;      // phy-soldier-swim-speed
  float inAirSpeed = 2.0f;     // phy-soldier-inair-speed

  // The soldier's shape: a column of spheres. How many depends on the pose.
  float radius = 0.25f;        // coll-soldier-radius
  float standHeight = 1.7f;    // coll-soldier-stand-height
  float crouchHeight = 1.4f;   // coll-soldier-crouch-height
  float proneHeight = 0.8f;    // coll-soldier-prone-height

  // How gentle a surface still holds: 0.5 is a slope of up to 60 degrees.
  float feetContactNormal = 0.5f;  // phy-soldier-feet-contact-normal
  float feetLevel = -0.04f;        // phy-soldier-feet-level

  // The share of the soldier's height in water at which he floats up and at which
  // he stands on the bottom again. Hysteresis, so he does not jitter at the boundary.
  float startFloat = 0.99f;  // phy-soldier-start-float
  float stopFloat = 0.9f;    // phy-soldier-stop-float

  // How many spheres are in the column per pose — from the engine's `getSoldierHeight`.
  int standSpheres = 5;
  int crouchSpheres = 3;
  int proneSpheres = 1;

  // How high a step the soldier walks over. This is not a variable of the engine
  // but a consequence of the shape: the lowest sphere has a diameter of
  // 2 * radius, and anything lower it passes, being pushed up along the edge.
  float stepHeight() const { return radius * 2.0f; }

  // Registers the Vars.Set handlers in the console: the values come from the game's .con.
  void bind(engine::Console& console);
};

// The movement state of one body.
struct BodyState {
  Vec3f position;
  Vec3f velocity;
  bool onGround = false;
};

// One step of a soldier's movement.
//
// wish is the desired direction in the plane (already rotated by the look angle),
// of length 0..1. groundHeight is the terrain's height under the body.
void stepSoldier(BodyState& body, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& constants, float groundHeight, float step);

// The centres of the soldier's spheres above foot level. The count and the step
// follow the engine's formula: spacing = (height - 2 * radius) / (count - 1).
std::vector<float> soldierSphereHeights(const PhysicsConstants& constants);

}  // namespace obf2::server
