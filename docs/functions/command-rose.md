# The communication roses (the "Command Rose")

Taken apart in `BF2.exe` (headless Ghidra, `tools/ghidra_at.sh`) and the game's
data, 22 Nov 2026. Code: `hud::CommInterface` (the table and `selectOrder`).
`app::FieldUi` (the rose on screen, `roseFor`, `roseCommand`) and the session's
`communicate`, named under "Ours" below, come with the HUD wiring.

## Five roses, one class, all data

The HUD manager (`[0xa10890]`, vtable 0x926130) builds five objects of one
class — vtable 0x933b10, constructor 0x7a1d40, 0x5c bytes — at 0x757028..0x7570c5.
Each has a console object whose `selectOrder` reaches it through a manager getter:

| console object | getter (manager slot) | field | HUD state that shows it |
|---|---|---|---|
| `SquadLeaderInterface` (cmd vtable 0x92e278) | 0x238 | +0x114 | 7 `SquadLeaderInterfaceShow` |
| `RadioVehicleInterface` (0x92e3a0) | 0x22c | +0x110 | 5 |
| `CommanderInterface` (0x92e4b0) | 0x23c | +0x118 | 8 |
| (0x92e5c0) | 0x234 | +0x10c | not established which |
| (0x92e6d0) | 0x230 | +0x108 | not established which |

(`RadioInterface` and `SquadInterface` are the last two; which is which is not
read.)

What a rose holds is data, `Menu_client.zip`, `HUD/HudSetup/Communication/`:
`<Name>.con` maps keys to a label and a console command,

    squadLeaderInterface.setStringMap Interface_1:1 HUD_COMROSE_SL_attack "SquadLeader.sendOrder 5 0"

and `HudElements<Name>Comm.con` lays out the buttons, each running
`<Name>Interface.selectOrder N`. The console objects: `setStringMap` 0x76e05d
(three arguments → the interface's slot 0x18), `selectOrder` 0x76ded6 (one
string → slot 8).

The squad leader's table (SquadLeader.con), whole:

| N | label | command |
|---|---|---|
| 1 | attack | `SquadLeader.sendOrder 5 0` |
| 2 | move | `SquadLeader.sendOrder 4 0` |
| 3 | need supplies | `SquadLeader.sendRequest 3 0` |
| 4 | destroy | `SquadLeader.sendOrder 8 0` |
| 5 | need orders | `SquadLeader.sendRequestForOrder` |
| 6 | defend | `SquadLeader.sendOrder 6 0` |
| 7 | need artillery | `SquadLeader.sendRequest 0 0` |
| 8 | repair this | `SquadLeader.sendOrder 7 0` |
| 9 | need UAV | `SquadLeader.sendRequest 1 0` |
| 10 | need vehicle | `SquadLeader.sendRequest 2 0` |

Radio.con and RadioVehicle.con (11 each) run `Radio.sendSpottedMessage`,
`Radio.callForAmmo/Medic/Repair` and `game.radioMessage <channel> <voice>`;
Squad.con `game.radioMessage TCSquad …` (orders 1, 2, 6, 7; the rest
`*NOT USED*`); Commander.con `Commander.uav/artillery/supplies/vehicleDrop`.

## `selectOrder` (slot 8, 0x7a0de0)

The key is `Interface_<path>:N`, the path starting at `1` (+0x20). It looks
for `<key>:i` (i from 1): when one exists the rose goes down a level, writes
the level's labels into `%s%iString` HUD variables and stays open. Otherwise it
runs the entry's command (slot 0x2c finds it, the console 0x978f14 slot 8 runs
it with a newline) and closes (+4 = 0). The stock tables have one level only.

## Which rose a key opens

`c_GITacticalComm` (T, `Settings/Controls.con`) reaches manager slot 0x208
(0x758540) from the game input handler 0x758770 (0x758f0a, 0x758f31);
`c_GIRadioComm` (Q) slot 0x204. On opening, 0x758540:

* picks what the crosshair is on: manager slot 0x14c(&object, &point +0x1e8,
  1000.0 m, 0); the pending order type is 4 (move), the point goes to +0x278,
  and when an object is hit its id to +0x274 and the type from
  `FUN_00758430(object)`; the point is put on the map (0x7720e0);
* then by role (player slots 0x138, 0x110, 0x108): squad leader → state 7,
  commander → 8, in a squad → 3; nothing for a player in no squad.

## Sending an order

`SquadLeader.sendOrder t b` (0x76cdf1) → manager slot 0x33c (0x7b1790):
"HUD_HUD_SQUAD_ALIVE_TO_ORDER" without a player; the type to +0x270 (t < 10);
with no point yet (+0x278 still −4000, `FUN_0053c200`) nothing is sent; as a
leader the client squad manager `[0x99ef84]` slot 0x44 sends it
(`IssueSquadOrderEvent` 24). `sendRequest` is slot 0x340 (0x7b19d0; co-op
without a commander answers `no_commander_available`); `sendRequestForOrder`
slot 0x2a8 (0x7afe90) is the radio message `req_new_order` on channel 2.

## Ours, and measured

`FieldUi` loads the five layouts and tables, opens the rose `roseFor` gives
(the order of 0x758540), labels it from the table through the localisation,
and on release over an entry runs the table's command; `roseCommand` routes
it onto the session's `communicate` (`order move`, `request uav`, `radio go`…),
which aims with the camera's ray as before. Repair (7) and destroy (8) were
added to it.

Live (`tools/squads_run.py --rose`, FuShe Co-op, 22 Nov 2026): a squad created,
T held with the pointer up — the rose drawn in Retail's layout with Retail's
labels, MOVE lit — released: `SquadLeader.sendOrder 4 0` sent, the server's
`IssueSquadOrderEvent` came back ("Squad order confirmed"); `sendOrder 6 0`
the same.

Not done:

* the order's target: ours is our own ray, now at 0x758540's 1000 m (Gulf of
  Oman, 22 Nov: a level look found nothing at 350 m, the move and defend orders
  are confirmed at 1000 m), but not manager slot 0x14c's object pick, so no
  object id and no `FUN_00758430` order type;

`game.radioMessage`'s first word is the talk channel, parsed at 0x442179..
0x442239 into the radio call's 5-bit channel: TCCommander 2, TCSquad 3,
0x899950 0x11 (the others not read). `sendRequestForOrder` sends
`req_new_order` on 2 and Squad.con's four calls go on 3; live, the session
logged `RADIO: channel 2 … req_new_order` and `channel 3 … req_new_order`.

* the order marker on the HUD, and a bot squad member taking the order, are not
  checked (the run's squad had only us);
* which keys a release or a click activates is ours: a release over an entry
  runs it.
