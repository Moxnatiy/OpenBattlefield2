# In-game: the map, movement physics, vehicles — the work list

Started 2026-11-10 against the live server at 192.168.100.100
(`strike_at_karkand`, `gpm_coop`, 16, with bots). Each item has a measure
(CLAUDE.md, "How the work goes") and the place to look first. B2BF's
reconstruction (`reference/OpenBF2`, GPL-3.0 — hints only, verified in the
binary and cited by address, rule 4) is named where it has the piece.

State at the start (`--connect 192.168.100.100 --frames 1500 --click-at
600:1110:833`): spawn works, 98 corrections from the server, mean divergence
0.00 m, largest 0.25 m (the first, the spawn's height); the weapon in hand, the
HUD, the minimap and other soldiers and vehicles on screen.

## 1. The map (HUD)

| # | what | measure | where to look |
|---|---|---|---|
| 1.1 | **streaks gone**: the circle is cut at the picture's edge (`hud/render.cpp`, `disc`), as the big map is — the same draw call, `BF2.exe` 0x774A60, from the minimap's paint at 0x7CEA42 and the big map's at 0x784D50. Live run by the Gas Station: no streaks, the scene shows below the edge. **Not compared with the original** at the same spot: what shows through there (the scene, or a backing node) is not measured | a frame of the original by the map's edge matches ours | — |
| 1.2 | **the zoom is the engine's now**: the index picks an exponent from three tables by the expanded flag (0x781ad5, 0x77cb03, docs/functions/hud-map.md), starts at 1, the spawn screen and the commander reset it to 0, M keeps it; the hatch follows the zoomed window. Live: M shows the whole combat area, N then 2.3× with the hatch and icons in place. Left: the N key's own handler (we assume `cycleZoom`), the vehicle types' saved zooms, the centre's blend (0x773870), the big map's alphas (`MenuMapAlpha`) | a frame of the original's big map at each zoom matches ours | `applyZoom` 0x776670, `setExpanded` 0x776a40 |
| 1.3 | icons on the maps: own soldier, teammates, vehicles, control points with their live owners (`ControlPointNetworkable`, not read — CLAUDE.md debt) | a captured point changes its flag on the map | B2BF `Bf2MenuMapNode.cpp`; `ControlPointNetworkable::setNetUpdate` |
| 1.4 | the minimap's rotation (`MinimapRotate`) and zoom | the profile's setting turns the map with the player | `GeneralSettings.getMinimapRotate`; B2BF `Bf2MiniMenuMapNode.cpp` |

## 2. Movement physics (our soldier)

| # | what | measure | where to look |
|---|---|---|---|
| 2.1 | the spawn's first state off by 0.25 m in height | the first correction after a spawn is under 1 cm | `SoldierPhysicsNode` (B2BF `Physics/SoldierPhysicsNode.cpp`); the spawn's own placement |
| 2.2 | the server repeating our last action, and the actions it drops (CLAUDE.md debt rows) | a run reports how many actions were dropped; a turn gives the same angle | `GameServer::clearPlayerActions` Linux 0x455780; `FUN_004cc400` |
| 2.3 | sprint: our flag against the server's (`PlayerState::sprint`) and the stop from a sprint | sprint and stop draw no correction | `Player::setNetUpdate` 0x500570; `SprintState` |
| 2.4 | soldiers against soldiers (`checkSoldierVsSoldier` 0x6f4150) | walking into a bot draws no correction | B2BF `Physics/SoldierResponse.cpp` |
| 2.5 | `--hosted` still runs the old blend (`moveSoldier`), not `tickSoldier` | `--hosted` moves the soldier through `tickSoldier` | `server/` |
| 2.6 | **done**: the speed state (zoomed → walk, Linux 0x54e8d0), the look's `getMouseSpeedMod` applied by the engine and not by us before the wire (0x5a61a0 at 0x5ae27b) | a zoomed run and turn draw no correction | docs/functions/soldier-physics.md |
| 2.7 | **done**: crouch (Ctrl) and prone (Z) — the keys, the delays, the change, the speed blend, the sphere column, the eye (`server/soldier_pose.h`). Live: the server's pose and ours agree on every state, 1 of 314 states over 1 cm | — | `expandSoldierCollision` not read |
| 2.8 | recoil in the look (`getYawRecoil` / `getPitchRecoil`, 0x5a5f40, 0x5a5fa0, `SoldierBasedRecoilComp`) — firing turns the server's view and ours does not | a burst draws no yaw/pitch correction | Linux `SoldierBasedRecoilComp`, 0x60c820 |
| 2.10 | **done**: the bottom-left bars — health from the soldier state's 0x40 (`ceil(hp)/max × 255`, Linux 0x5db45b), stamina, their colours (0x97ddd0/0x97dde0 at 0.3 and 0.05), `KilledState` (0x7af439); the ammunition's colour (0x7a8b62) (`app/player_hud.h`). Left: the blinkers (0x7718a0), `DeathMessage` | — | — |
| 2.11 | **done**: weapon animations — the value holders' order (min, normal, max; Linux 0x6d1dca), the switch trigger's previous mask (0x7f7390), the idle time. Live: `run`, `sprint`, `zoom_run`, `fire`, `zoom_fire`, `reload`, `idle1` all play. Left: the zoom transition plays longer than its clip (78 frames for two toggles), `idle2` never picked | — | — |
| 2.9 | the weapon's fire state (`GenericFireArm::getFireState`) — inferred as "trigger held on a loaded weapon" for the crosshair | the fire state read | Linux `GenericFireArm` |

## 3. Vehicles

| # | what | measure | where to look |
|---|---|---|---|
| 3.1 | other players' vehicles: interpolation between updates (now the newest state), wheels, turret and the driver's controls | a moving jeep on screen moves smoothly with its wheels turning | `BF2FrameInterpolator`; the vehicle's state tail (CLAUDE.md debt: control object fields 0x800..0x40000) |
| 3.2 | a soldier in a seat drawn in his seat (now not drawn) | a passenger sits in his seat on screen | `Soldier::onEnter` Linux 0x54b2f0; the vehicle's seat positions from its .con |
| 3.3 | entering a vehicle ourselves (the enter key, `c_PIUse`) | E by a jeep puts us in its driver's seat on the server | `GameClient` enter event 0x4d6c00, 0x4d6820 |
| 3.4 | driving: the vehicle's physics on our side (prediction) and the server's corrections | a drive along a road draws corrections under 10 cm | B2BF `Physics/RotationalPointPhysicsNode.cpp`, `PhysicsWing.cpp`, `ResponsePhysics*`; the engine's Engine/Wheel templates in the .con |
| 3.5 | the vehicle HUD (speed, altitude, the weapons) — 477 unfilled HUD variables include `SpeedString`, `AltitudeString` | the vehicle HUD shows our speed | `--hud-vars`; B2BF `BF2HudManager*.cpp` |