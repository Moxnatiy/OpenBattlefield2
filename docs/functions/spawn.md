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
