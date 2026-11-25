# Critically wounded (man down)

Retail reads two fields on the client `Player` to tell wounded from dead. The
reading below was done without a screen capture of the original; what was seen
on screen afterwards is under "Seen live".

Status: `death_camera.h` (`ManDownView`, `setRotationZXY`, `getRotationZXY`) is
in. Reading the fields off the wire, the HUD lines and driving the view each
frame come with the session and HUD wiring.

## Fields (BF2.exe; Player vtable base = 0x8ac6c4 − 0x194)

| field | getter / setter | what |
|---|---|---|
| +0xe4 spawnAtTick | get +0xe8 (0x504a60), set +0xec (0x65c780) | tick the player may spawn / the window ends |
| +0xe8 manDown | set +0xf8 (0x4f9fc0), get +0xfc (0x504a90) | wounded flag |
| +0xec manDownAtTick | get +0x100 (0x504aa0) | tick it was set |
| +0xd5 alive | getIsAlive +0x68 (0x504b40) | |

`getTimeToSpawn` (+0xf0, 0x4f9f80) = (+0xe4 − tick) / 30; Linux 0x4944e0 adds `maxss 0` (30.0 at 0xb2efc4).
`getManDownTimeLeft` (+0xf4, 0x504a70) = manDown ? getTimeToSpawn : 0 (Linux 0x4a2320).
Wounded = alive && manDown. Dead = !alive.

## Window length (2.2)
Server side `ServerSettings::getManDownTime` (Linux 0x468830): 15.0 (0xb317f8) if +0x145 is set, else the setting at +0x94.
Host path `Soldier::handleMessage` 0x5a7570, message 0x18: setIsManDown(1), setSpawnAtTick(tick + round(FUN_0044b800() × 0x899d44)).
The client does not compute the length: it receives spawnAtTick (below).

## Wire (2.1)
Server `getStateSync` Linux 0x4a18b0 writes spawnAtTick (+0x104) in 32 bits, then manDown (+0x108) at 0x4a18cf.
Client 0x5018a0 tail (0x501c53): 32 bits → +0xe4, 1 bit → +0xe8, sign + 8 bits health, then 3 flags → +0x124, +0x187, +0x188 (purpose unknown).
The player-state bit 0x20 is applied at 0x501567/0x501573 to ebx+0xd8/+0xdc; ebx = player+0xc is *inferred*, so these are the same fields (inferred).

## HUD (2.2, 2.5)
HUD layer 0x4668d0 when `getBeginRoundState` (GameLogic +0x15c) == 3 (0x466d46..0x466e30):
t = alive ? getManDownTimeLeft : getTimeToSpawn; ceil, clamp 0..120;
`sprintf(L"%s %2d", localize("HUD_TEXT_MENU_TIME_TO_RESPAWN"), t)` (format 0x89e894, key 0x89e8a4) → TimeToSpawnString (+0x50c).
SuicideButtonShow (+0x1db) = MapFullSizeAndSpawnShow && alive && !hudManager vfn 0x34c() (0x4673a0). **0x34c unknown; taken as false.**
DeathMessage keys: kill by another living player → HUD_TEXT_MENU_YOU_WERE_KILLED (0x754347, `handleGameLogicEvent` 0x753de0 case 3); suicide event 8 (0x753b10, 0x753c1b); going man down → HUD_TEXT_MENU_YOU_ARE_IN_MAN_DOWN (0x925c10) from HUD vtable 0x926130 +0x350 = 0x751460, which sets HudState 0 (`setState(0,1)`) so KilledInfo shows. That the writer targets DeathMessage (+0xa0) via varmanager vfn +0x60 is *inferred*; that +0x350 runs on a client (needs local_5 in 0x5a7570) is *inferred*.

## Give up early (2.5)
Spawn screen SuicideButton runs `spawnManager.commitSuicide` → NESuicide (12). Server `ServerGameLogic::suicide` (Linux 0x4a8440): setSuicide(1), kills via vfn 0xd8(player,1,1); no man-down check, so it works while wounded.

## Revive / timeout (2.4, 2.6)
Revive: GameLogic message 0x19 clears +0x2e8 and posts event (3,7); in our client `readRevivedSoldier` sets alive/manDown=false. Timeout: no client code found that ends the window; the server does (inferred); the client countdown just floors at 0.

## The wounded view (2.3) — traced
Read in BF2.exe (Ghidra decompile and disassembly), names from the Linux server, 2026-11-22.
* `Soldier::handleMessage` 0x5a7570 message 0x18 (man down) contains **no view-mode or camera call**. It does: HUD/sound start `FUN_005a5d20` (own player only: controlled-object vfn +0x18(1,0,0), sound global 0xa23dc0 vfn +0x7c, event (0,0xd)), `FUN_007e2cb0` (soldier+0x2ec effect, vfn +0x14), setIsManDown, setSpawnAtTick, event (3,6), the killer id to +0x2fc, `NEPlayer`-side HUD vfn +0x350. Message 0x19 (revive) calls `FUN_005a5da0` (sound +0x80), `FUN_007e2cf0`, clears +0x2e8, posts (3,7). So **retail does not switch to the death camera (mode 6) on going man down**: the soldier camera stays. (A negative read of those two functions; the camera code itself was not read.)
* The two `Vars` `soldier-mandown-speed` (default 3.0, global 0x9ec2a4, registration 0x854300) and `soldier-mandown-rotspeed` (default 1.4 = 0x3fb33333, global 0x9ec358, registration 0x854320) are read only in the client soldier update `FUN_005a63a0` (vtable 0x8e81a0 slot). When the soldier's camera object (+0x254) has its flag +0x1d set, a vector is eased with `dt × 3.0` (`FUN_0045b0a0`) and the rotation fields +0x35c..+0x370 are eased with `dt × 1.4`. What this object is and how the eased value reaches the eye is **not traced**.
* `manDownCameraOffsetInDof/InUp` (registered 0x50bcb0/0x50bdd9) write the BF2.exe globals 0x9a97ec/0x9a97f0 (setters 0x50bd10, 0x50be39) and **nothing else references them**: they are unused by the client.
* **The fields** (BF2.exe soldier / Linux soldier): head position +0x350 / +0x51c, angles +0x35c / +0x528, angle target +0x368 / +0x534, last first-person camera matrix +0x374 / +0x540 (its position +0x3a4 / +0x570). Linux globals: `g_manDownHeadInterpolateSpeed` (0x108ea70, `soldier-mandown-speed`), `g_manDownRotInterpolateSpeed` (0x108ea68, `soldier-mandown-rotspeed`).
* **The camera matrix** is saved each frame while the soldier is up: `Soldier::applyCameraAnimation`, no-ragdoll branch, copies its result to +0x540 (Linux 0x5526f4).
* **Going down** — `Soldier::enableRagDoll` (BF2.exe 0x61f9e0, writes at 0x61fc4c–0x61fdbf; Linux 0x556f28–0x557133):
  * target = (0, −90, +90) (0xc2b40000, 0x42b40000), then multiplied as a vector by `setRotationZXY(0, 0, c)`, where c is the third output of `getRotationZXY` on the camera matrix (the roll). For our camera, which has no roll, that matrix is the identity.
  * head = the camera matrix's position.
  * angles = −getRotationZXY(camera matrix) = −(yaw, pitch, roll).
* **Each frame** (BF2.exe 0x5a63a0; Linux `Soldier::handleFrameUpdate` 0x548e60, 0x549010):
  * angles += (target − angles) · dt · 1.4 (0x5a6525–0x5a6579).
  * Only while the ragdoll is active (Linux `+0x3f0`→`+0x31`): head += (head target − head) · dt · 3.0 (`FUN_0045b0a0`, 0x5a651d).
  * The head target is the object's world position plus the skeleton's vector at +0xbf0, not rotated (Linux 0x548d71–0x548e54).
* **The camera** — `Soldier::applyCameraAnimation` (BF2.exe 0x61cb60, vtable 0x8e7f80 slot 0x138, `this` = soldier + 0x154; Linux 0x5523e0):
  * while the ragdoll exists, R = `setRotationZXY(−angles.z, −angles.y, −angles.x)` (0x61cbfa–0x61cc23).
  * out = out × R (0x6d9cc0), then out's position = head (0x61cc43–0x61cc5d).
  * The caller (0x5a2986) passes an identity matrix and copies the result back, so R is the camera's world rotation.
* **`setRotationZXY(m, z, x, y)`** (Linux 0x732060; BF2.exe 0x6da3d0), rows in degrees:
  * Z = [(c, −s, 0), (s, c, 0), (0, 0, 1)] (0x732418).
  * X = [(1, 0, 0), (0, c, −s), (0, s, c)] (0x73237d).
  * Y = [(c, 0, s), (0, 1, 0), (−s, 0, c)] (0x7322f1).
  * Combined Z·(X·Y) (0x732200–0x732229). Row 2 is the view's forward, row 1 its up, so +x looks up.
* **`getRotationZXY`** (Linux 0x730c30) returns (yaw, pitch, roll). Pitch is asin(m[9]); at ±90 pitch the result is atan2(m[2], m[0]), 90, 0.
* **Settled**, R = setRotationZXY(−90, +90, 0): forward (0, 1, 0), straight at the sky; up (−1, 0, 0). The start is the last first-person view itself, so there is no jump.
* **Not identified:** which skeleton point +0xbf0 is, i.e. where the eye ends up.

Ours (`death_camera.h` `ManDownView`, `setRotationZXY`):
* the angles, their target, the 1.4 and 3.0 per-frame eases, and the camera rotation and up as above.
* the eye eases from the last first-person eye toward the body on the ground + 0.22 m. That point is **not measured**.

## Seen live (2.3, 2.4)
On an original 1.50 server (Gulf of Oman, Conquest 32), with the full client:
* Going down: half a second in, the view is turning up and rolling; after about 12 s it looks straight up; the HUD and the countdown stay.
* After the fall the server sent no control state for our soldier until the revive; the last health seen was 0.
* The revive: the medic's `usingshockpaddles` radio, GameLogic 14 naming our soldier, the player state's spawn tick set to 0, then a control sync with man down 0 and health 1.000, and the `get_up` radio. The client must then drop the health of 0 it kept, or it still counts itself incapacitated.
* After it: the wounded box gone, first person back, the HUD in soldier mode.
* Two clients need two CD keys: a second client on the same key is refused at the challenge (disconnect reason 20).
* A bot revive was not seen: bots never wounded the client in six 2.5-minute runs.
* The yellow cross after a revive, on the medic's screen, is the defibrillator's crosshair (`HudElementsDefibrillator.con`, `medicCross.tga` at 388,288, shown while the paddles are out), not a revive marker. A revived soldier's head not following his look is not a revive defect: no remote soldier's head followed it; see soldier-look-3p.md.

Not done: the help hint box; LMB "call medic"; the second centre line in capitals (retail draws `YOU WILL RESPAWN IN:`).
