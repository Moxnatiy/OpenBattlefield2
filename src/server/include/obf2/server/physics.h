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
  // `soldier-lookMaxAngle`, a vector registered at `BF2.exe` 0x854280 as
  // (40, 85, 0): how far the aim turns off the body, and the pitch's limit
  // (read by 0x5a8630). Measured on the live server: a standing soldier's aim
  // offset stops at -40.
  float lookMaxYaw = 40.0f;
  float lookMaxPitch = 85.0f;
  // `soldier-lookSideRestore`, 0x854260 (`PUSH 0x40800000`, 4.0): degrees per tick
  // the body turns to take the aim offset back (0x5ae863).
  float lookSideRestore = 4.0f;
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

  // `p-pos-damp` and `p-pos-damp-water`: `BF2.exe` registers them at 0x8607a0
  // (`PUSH 0x3f7d70a4`, 0.99) and 0x8607c0 (`PUSH 0x3f666666`, 0.9).
  // `SoldierPhysicsNode::updatePositionalPhysics` (Linux server 0x6f1de0)
  // multiplies the new velocity by them, mixed by the share under water. Measured
  // on the live server: a soldier running at 3.9 with an axis of 0.979 reports a
  // speed of 3.78 = 3.9 * 0.979 * 0.99.
  float positionalDamping = 0.99f;
  float positionalDampingWater = 0.9f;  // not used yet: the water share is not modelled

  // The node's drag (`SoldierPhysicsNode::updatePositionalDragAdvanced`, Linux
  // 0x6f1890): the template's `ObjectTemplate.drag 1.0` and `ObjectTemplate.mass
  // 100` (`Objects/Soldiers/*/*_soldier.tweak`, every kit), and the coefficients
  // `updatePhysics` passes along the node's rows (0x6f2190): 0.25 forward and up
  // (0xb4355c), 0.9 to the side (0xb34a78).
  float drag = 1.0f;
  float mass = 100.0f;
  float dragForward = 0.25f;
  float dragUp = 0.25f;
  float dragRight = 0.9f;

  // `phy-soldier-jump-length-factor`: the share of the ground speed a jump keeps.
  // `BF2.exe` registers it at 0x853b20 with 0.98 (`PUSH 0x3f7ae148`); the game's
  // data does not set it.
  float jumpLengthFactor = 0.98f;
  // The delays a jump starts (`Soldier::handlePlayerInput`, Linux 0x54fbbb). The
  // engine's defaults are 1.0 (`BF2.exe` 0x854400, 0x854420, 0x8544a0); the values
  // are the game's data, `Objects/Soldiers/Common/Common.con`.
  float fireDelayAfterJump = 0.7f;          // fire-delay-after-jump
  float proneDelayAfterJump = 0.3f;         // prone-delay-after-jump
  float sprintRechargeDelayAfterJump = 0.7f;  // sprint-recharge-delay-after-jump
  // `jump-delay-after-prone` (0x854440 registers 1.0; the data sets 0.8). No pose
  // but standing is modelled, so nothing starts it yet.
  float jumpDelayAfterProne = 0.8f;

  // The ground contact's material means (`SoldierResponsePhysics::internalImpulseOn`,
  // Linux 0x6f2930..0x6f28cf: `+0xf4` friction and `+0xfc` resistance are the
  // means of the two materials'). The soldier's is `Human_body`, friction 1.1 and
  // resistance 0.01 (`Common/Material/materialManagerDefine.con`, material 24).
  // The terrain's is **not read** — the terrain's material map is not taken — and
  // `Grass` (material 5: 0.8, 0.06) stands in for it: measured on the live server
  // on Dalian Plant, a landing's friction stops at 44.78 m/s², the dynamic limit of
  // friction 0.95, and a sprint's resistance term is 0.035 of the slip.
  float groundFriction = 0.95f;
  float groundResistance = 0.035f;

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
  // How far above the feet the soldier's position is. `BF2.exe` registers the
  // variable at 0x860f00 with 1.0 (the handle at 0xa086f0), and `FUN_006ed4c0`
  // takes the extent as `[y - pivot, y + pose height - pivot]`. Measured on the
  // live server too: our standing soldier's networked height is the terrain's
  // plus 1.00 (docs/functions/network-events.md).
  float pivotHeight = 1.0f;    // coll-soldier-pivot-height

  // How gentle a surface still holds: 0.5 is a slope of up to 60 degrees.
  float feetContactNormal = 0.5f;  // phy-soldier-feet-contact-normal
  float feetLevel = -0.04f;        // phy-soldier-feet-level
  // How far back the object sweep starts, as a share of the radius times one half
  // (`checkSoldierVsMesh`, Linux 0x6f4976, `BF2.exe` `DAT_00a08a34`); the engine's
  // default, docs/reference/soldier-vars.txt.
  float extendRay = 0.9f;          // coll-soldier-extend-ray

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
  // The smoothed movement axes, `Soldier +0x22c` (forward) and `+0x228` (strafe)
  // — they travel in the soldier's state under 0x400 and 0x800 (0x62d4e0's apply
  // block), so a correction sets them too.
  float forwardAxis = 0.0f;
  float strafeAxis = 0.0f;

  // The rest of the physics node (`SoldierPhysicsNode`, docs/functions/
  // soldier-physics.md, "The physics node, whole"); `velocity` above is its
  // positional speed `+0x2c`. The controlled state carries all three.
  Vec3f acceleration;  // +0x38, state 0x100
  Vec3f friction;      // +0xac, state 0x200
  Vec3f linearSpeed;   // +0x44, state 0x40000
  int frictionContacts = 0;  // +0xb8

  // The response physics (`SoldierResponsePhysics`): the input's surface speed
  // `+0xcc`, which the friction turns into the next tick's velocity; the contacts'
  // velocity `+0xd8`; the ground normal `+0xe4`; sticking, `+0x100` bit 0x40.
  // `onGround` above is its `+0x11a`.
  Vec3f surfaceSpeed;
  Vec3f contactSpeed;
  Vec3f groundNormal{0.0f, 1.0f, 0.0f};
  bool sticking = false;
  // The contacts' pending adjustments, `+0xb4` position and `+0xc0` speed, and how
  // many contacts `+0xf0` — collected by `internalImpulseOn`, applied by
  // `solveImpulse` (soldier_node.h).
  Vec3f positionAdjust;
  Vec3f speedAdjust;
  int contacts = 0;

  // The soldier's jump timers. `Soldier::resetInstance` (Linux 0x549df0) starts the
  // two air timers at -1; the input counts them down to -1 (0x54f1b1).
  float noAirControl = -1.0f;  // +0x460
  float airControl = -1.0f;    // +0x464
  // Counted down by `Soldier::handleFrameUpdate` (0x548f1d..0x548fd8), each only
  // while above zero.
  float fireDelay = 0.0f;              // +0x5a0
  float proneDelay = 0.0f;             // +0x5a4
  float jumpDelay = 0.0f;              // +0x5a8
  float sprintRechargeDelay = 0.0f;    // +0x5b4
};

// The soldier's direction from its smoothed axes: forward * forwardAxis + right *
// strafeAxis, divided by the axes' length when that is over 1 (`BF2.exe` 0x5a7c50).
Vec3f soldierAxesDirection(float forwardAxis, float strafeAxis, const Vec3f& forward,
                           const Vec3f& right);

// The movement a soldier asks for this tick — `Soldier::updateSoldierSpeed`
// (`BF2.exe` 0x5a7c50):
//
//   axis += (input - axis) * (input != 0 ? phy-soldier-acceleration (0x9ec260)
//                                        : phy-soldier-deceleration (0x9ec2dc))
//   an axis under 0.001 becomes 0
//   direction = forward * forwardAxis + right * strafeAxis, divided by the axes'
//   length when that is over 1
//
// once per tick, not scaled by the step. The caller multiplies the direction by
// the speed and `phy-soldier-speed-factor` (0x9ec2f0) and hands it to the body
// (physics `+0x84`), which takes it as its velocity on the next tick: the velocity
// follows the soldier's facing, and only the axes are smoothed. Ours used to smooth
// the world velocity, which lagged behind every fast turn and was corrected by the
// server.
//
// Measured on the live server: from standing, forward and strafe held, the state's
// axes read 0.195, 0.578, 0.774, 0.895 … 0.979.
Vec3f soldierMoveDirection(BodyState& body, float forwardInput, float strafeInput,
                           const Vec3f& forward, const Vec3f& right,
                           const PhysicsConstants& constants);

// One step of a soldier's movement — our own local server's, not the engine's.
//
// wish is the desired direction in the plane (already rotated by the look angle),
// of length 0..1. groundHeight is the terrain's height under the body.
//
// The engine's own tick is `tickSoldier` (soldier_move.h); this blend is what the
// hosted game still runs, and nothing of it is reversed.
void stepSoldier(BodyState& body, const Vec3f& wish, float maxSpeed, bool jump,
                 const PhysicsConstants& constants, float groundHeight, float step);

// The centres of the soldier's spheres above foot level. The count and the step
// follow the engine's formula: spacing = (height - 2 * radius) / (count - 1).
std::vector<float> soldierSphereHeights(const PhysicsConstants& constants);

}  // namespace obf2::server
