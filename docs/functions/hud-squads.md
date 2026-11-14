# The squads: the spawn screen's squad tab and the scoreboard's squad page

Taken from OpenBF2's reconstruction (`Menu/HudSquad.cpp`, `Menu/HudScoreboard.cpp`,
`Game/GameLogic/SquadManager/*`, `Game/GameEvents/*Squad*`), each point with the
`BF2.exe` address OpenBF2 gives it, and checked against the live server.

## Who is in which squad

Nothing on the client keeps a squad's members. Every player's own networkable says
it (`Player::setNetUpdate`): the squad (mask 0x2000, `setSquadId`) and, unmasked in
every record, `m_isSquadLeader` — the bit we had read as "always A, purpose not
established". `net::bf2::PlayerState::squadLeader` and `RemotePlayer::squadLeader`
carry it now; the next unmasked bit is `m_isConnected`.

What the client does keep is each squad's **name** and **lock**, from the events the
server sends (`SquadManager::sendSquadManagerState` on a join, and on every change):

| event | type | fields (OpenBF2 `deSerialize`) |
|---|---:|---|
| `JoinSquadEvent` | 16 | team 8, player 8, squad 8 |
| `LeaveSquadEvent` | 17 | team 8, player 8, squad 8, clear the invite 1 |
| `ChangeSquadNameEvent` | 22 | squad 8, team 8, player 8, length 8, the name |
| `SetPrivateSquadEvent` | 23 | squad 8, team 8, private 1 |
| `InviteEvent` | 25 | team 8, player 8, squad 8, invited 1 |
| `SetSquadLeaderEvent` | 28 | `JoinSquadEvent`'s codec: team 8, player 8, squad 8 |

**Type 28 was missing from our table** (`tools/linuxded/gen_events.py` does not see
it: the class has no serializer of its own). Every packet a new squad's leader rode
in broke at it, and the name and the "joined" behind it were lost — the squad was
created on the server and never on our screen. It is in `bf2_events.inc` now, by hand.

The default names are the engine's 27 (`kSquadNames`: Unassigned, Alpha … Zulu).

## What the client sends

`ClientSquadManager` (OpenBF2), each one game event:

* **join** — `JoinSquadEvent(team, us, squad)`; the server's `addToSquad` refuses a
  dead player with a score, a man-down one, a full squad (six) and a locked one we
  are not invited to;
* **leave** — `LeaveSquadEvent(team, us, squad, false)`;
* **create** — there is no "create": `BF2HudManager::createSquad` names an empty
  squad (`ChangeSquadNameEvent`), and the server's `setSquadName` puts the naming
  player in it as its leader (HUD event 14, `ECHudHESquadCreationSuccess`);
* **lock** — `SetPrivateSquadEvent(team, squad, flag)`, the leader only.

Measured on the live server: a click on JOIN put us in Bravo (`our squad: 2`, HUD
events 21 and 22); a click on CREATE made squad 3 "Charlie" with us leading it
(`squad named: team 2 squad 3 'Charlie' by player 1`, HUD event 14,
`our squad: 3 (leader)`), and the tab showed it with LEAVE and MANAGE SQUAD.
Leave and lock are sent the same way; their answers have not been watched yet.

## The squad tab (`hud::SquadList`)

`SquadMemberList` is list data 5. The columns, colours, textures and the four row
types are `HudSquad`'s: 0 the commander band, 2 a squad, 3 the create row, 4 a
player; hidden columns 13 and 14 carry the key and the type. A click picks the row
and the column the way the list node does (`listAt`, render.cpp) and runs the
list's `setListNodeConCmd 1` — `SquadMenu.singleClickNew`, whose answer
(`SquadList::click`) is: column 10 the button (join / leave / create), 8 the lock,
12 the arrow, anything else the selection.

**Not done, and said in the log when reached** (`NOT DONE: …`): the commander's
apply, resign and mutiny; the squad name edit box (the default name is used); the
invite list (`SquadMenu.setShowInviteList`, data 8); the chat lines the original
prints on a refusal; voice icons; the vehicle and kit icons of a player row.

## The scoreboard's pages

`scoreboard.setToggleShow` is `HudScoreboard::setPage`: 0 the score, 1 our team
grouped (the commander, squads 1..9, the unassigned — each behind a heading with
its sums), 2 manage (the rank, "name - id", the voice box; the kick vote stays
empty without the vote manager, as the original's does while voting is off).

The mouse: with the scoreboard held, the right button switches the HUD to its
mouse mode (12, `BF2HudManager`'s filter) — the data's "Right click to activate
mouse" — until the key is let go. `--scoreboard-mouse` turns it on for a
screenshot. The original's scoreboard has **no close button**: it is the key held.

The server info's strings are `HudInformationLayer::setServerInfo` (the menu calls
it on a join): the server's game name (block 0's head, `parseServerInfo`), the
address and the port we connected to, and `ActiveLevel`, "HUD_LEVELNAME_<map>" —
which the dictionary has as `HUD_LEVELNAME_Strike_at_Karkand`: the game's lexicon
ignores case (`ExcelLexicon` keeps keys in upper case), and ours does now too.

The map vote opens only while the server has voting on (block 0's
`votingEnabled`); the vote itself needs the vote manager — `NOT DONE`.
