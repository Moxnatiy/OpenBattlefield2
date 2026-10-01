# Choosing a spawn point — how the engine does it

Taken apart in the Linux server (`ia-32/bf2`, symbols present).

## `SpawnGroup::getSpawnPoint(bool forHuman)` — 0x081116c0

```
candidates = []
for each point in the group:
    if point->getActive(forHuman, group->team):
        candidates += point
if candidates is not empty:
    return candidates[rand() % count]
return 0
```

So it is a **uniformly random choice** among the suitable ones, not a
round robin and not "the first that fits". A group is the set of points
belonging to one flag (`SpawnGroup::getControlPointId`).

## `SpawnPoint::getActive(bool forHuman, int team)` — 0x08115d20

In order:

1. the point is disabled (`setActive 0`) → no;
2. if the point is tied to a flag, the flag must belong to `team`,
   otherwise no (plus a separate "no control points" mode);
3. `forHuman` checks the `onlyForAI` flag, otherwise `onlyForHuman`;
4. if the point "hangs" off an object (a vehicle) and that object is
   destroyed → no;
5. `minSpawnHeight != -1`: the point's height above the maximum of
   terrain and water must be at least that;
6. if the point is not occupied (`isOccupied`), the engine looks for
   objects **within 1 m** and refuses if someone is already standing
   there;
7. if it is occupied, it tries to find a vehicle entry nearby (5 m for a
   bot, 15 m for a human) and spawn straight into it.

## `SpawnPoint::isOccupied(float)` — 0x08115cf0

```
worldTime < lastSpawnTime + spawnPreventionDelay
```

The point's constructor sets both fields so that the sum is zero — a fresh
point is never "occupied".

## Defaults

From the `SpawnPointTemplate` constructor (0x08116a30):

| Property | Default |
|---|---|
| `setActive` | 1 |
| `setSpawnPreventionDelay` | **0** |
| `setMinSpawnHeight` | **-1** (do not check) |
| `setControlPointId` | -1 |
| `setOnlyForAI`, `setOnlyForHuman` | 0 |

Levels barely set these properties: in Dalian Plant all 24 points carry
only `setSpawnPositionOffset` and `setControlPointId`. So in practice the
choice comes down to "a random point of your own flag with nobody next to
it".

## The spawn screen on a server: circles, the bar, and coming back

### The circles are the server's groups

The map draws its circles in `BF2.exe` **0x77f6e0**, and the list is not the
level's control points: it is the client `SpawnManager`'s (`[0x9e5680]`)
`vtbl[0x2c](local player)` — Linux `SpawnManager::getGroupsForPlayer`
(0x4b8790) — and each circle stands at the group's own position
(`vtbl[0x30]`). A group is listed when:

* its team (`getTeam`) is the player's;
* it is not bots-only (+0x9a), or the player is a bot (`getIsAIPlayer`);
* it is selectable (+0xa0);
* when it belongs to a squad (`getSquad` != -1), the squad is the player's
  (`getSquadId`) and he does not lead it (`getIsSquadLeader`, Linux Player
  vtable 0x278) — the leader is the point. Whether we lead is not in our
  player's state; it is taken as not.

The groups come from `CreateSpawnGroupEvent` (type 57). Its three bits, in the
order `serialize` (Linux 0x424670) writes them and `createClientSpawnGroup`
(0x4ba600) applies them: `setActive`, bots-only (+0x9a), selectable (+0xa0).
The same number again is the same group (`getCreateSpawnGroup(id, …, true)`), so
a flag that changed hands comes back under its old number with a new team.

`RemoveSpawnGroupEvent` (type 58: the number, 8 bits, and the network id, 16)
drops one (`removeClientSpawnGroup`, Linux 0x42e1b0 → vtable 0x98).

And every group is a networkable under the event's network id: its ghost
(`SpawnGroup::setNetUpdate`, Linux 0x4b9ab0) is a 6-bit mask, then in order
0x10 selectable (1 bit), 0x2 team (4), 0x4 active (1), 0x8 bots-only (1),
0x1 position (8 + 8, packed as the event's), 0x20 squad (8, minus one). Only the
ghost carries the squad. On the live server every one of them reads to exactly
its length (73 of 73 in one run; 100 of 100 over the four captures in
`tests/data`), and the run-time groups from 192 turn out to be the squads'
(groups 194..197: squads 1 and 2 of each team) — the circles a player outside
those squads saw and should not have.

The level's own points with their starting owner are what we drew before; on a
server whose round has been going for a while they were wrong — one circle on
Strike at Karkand where the server listed six for team 2. The flags' icons
still come from the level's file (a captured point keeps its first owner's
flag on our map): the control points' live state is a ghost
(`ControlPointNetworkable`) we do not read yet.

### The bar while dead

`HudInformationLayer`'s per-frame update (**0x4668d0**) picks `SpawnInfoString`
for a dead player (`hud::spawnInfoText`, spawn.h has the branches): select a
point, press DONE, `TIME TO SPAWN: #TIME#`, instant spawn, invalid point. The
time is `getTimeToSpawn` (Linux 0x4944e0): the spawn tick the player's state
carries in bit 0x20 (docs/functions/player-state.md) less the game tick, over
30, rounded up (0x463930). With `sv.spawnTime 15` and `sv.manDownTime 15` the
wait after a death is up to thirty seconds.

### Coming back after a death

The spawn screen is the HUD's state 1, which stands while there is no soldier.
On `--connect` "there is a soldier" is the session's `playerSpawned`, raised by
the server's spawn and dropped by its death event; "DONE was pressed" stood in
for it and never fell again, so after a death the screen did not come back.
