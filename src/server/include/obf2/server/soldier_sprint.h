#pragma once
// A soldier's sprint — `SprintState` (Linux server: `handleTMSprint` 0x43deb0,
// `handleUpdate` 0x43ded0, the constructor 0x43de70, `setConstants` 0x43e0e0).
// Notes: docs/functions/soldier-physics.md, "Sprint".
//
// Measured on the live server: the soldier state's 0x4000 carries `sprinting`
// (the flag) and the stamina (the value, 7 bits over 127).
namespace obf2::server {

struct SprintState {
  // `setConstants` (+0x0, +0x4, +0x8), from the kit's template:
  // `ObjectTemplate.SprintDissipationTime` / `SprintRecoverTime` / `SprintLimit`.
  // The defaults are the light kits' (`soldiers/*/*_light_soldier.tweak`: 10, 17,
  // 0.05); the heavy kits set 8, 20, 0.05. Which kit we play is not taken yet.
  float dissipationTime = 10.0f;
  float recoverTime = 17.0f;
  float limit = 0.05f;
  float drainScale = 1.0f;  // +0xc, 1.0 from the constructor; halved on a no-vehicles server
  float stamina = 1.0f;     // +0x10
  bool wants = false;       // +0x14, set by a message, cleared by every update
  bool sprinting = false;   // +0x16
};

// The sprint message a tick sends (`BF2.exe` `FUN_005c0460`: 0x2a to start, 0x29
// to go on — the Linux server's 0x28 and 0x29). goOnOnly is the 0x29 one: it keeps a
// sprint that runs but does not start one.
void sprintMessage(SprintState& sprint, bool goOnOnly);

// One update (`handleUpdate(blocked, rechargeDelay, step)`):
//
//   sprinting: stamina -= drainScale * step / dissipationTime, not under 0; the
//     sprint ends unless a message came this tick and stamina is above 0, or when
//     blocked
//   otherwise: stamina += step / recoverTime, not over 1, while the recharge delay
//     is not above 0 (recoverTime not above 0: stamina is 1); a message this tick
//     with stamina at or over the limit starts the sprint, unless blocked
//   the message is used up either way.
void updateSprint(SprintState& sprint, bool blocked, float rechargeDelay, float step);

}  // namespace obf2::server
