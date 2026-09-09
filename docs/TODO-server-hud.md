# Plan: server logic and the in-game HUD

The source of truth is the **BF2 1.5 Linux server**
(`Game Files/OtherFiles/linuxded/bin/amd-64/bf2`, ELF x86-64, **not
stripped**: 36 076 functions with full C++ signatures). The symbol map is
dumped into `docs/reference/linuxded-symbols.txt` and
`docs/reference/linuxded-functions.txt`. The binary's DWARF covers only
glibc's startup code, so structure layouts come not from it but from
behaviour, `.con` data and the decompilation of individual functions.

The order of work does not change: notes → test → implementation. Copying
decompiled code is not allowed (CLAUDE.md, rule 8).

## What already exists

- a fixed 30 Hz tick, a loopback channel, handing objects out to the client;
- soldier movement with the `phy-soldier-*` constants, collision with
  geometry;
- control points: capture over a fixed time, spawning at your own point;
- parsing of `GamePlayObjects.con`: 4 points, 27 spawners on Dalian Plant.

## The original's skeleton, which we follow

`dice::hfe::ServerGameLogic` — the state machine driven by `update(float)`:

```
uFirstPreGame -> uPreGame        (warm-up, waiting for players)
uFirstPlaying -> uPlaying        (the game; inside it:)
                   uPlayingSpawning       — the spawn queue, waves
                   uPlayingTicketSystem   — tickets and bleed
                   uPlayingInsideGameArea — the combat area's bounds
                   uPlayingWinner         — the win condition
uFirstEndGame -> uEndGame        (results, the next map)
```

The key methods we have to reproduce: `spawnPlayer`, `reSpawnPlayer`,
`killPlayer`, `suicide`, `giveDamage`, `heal`, `resurrect`, `replenishAmmo`,
`selectTeam`, `selectKitAndUnlockLevel`, `getNewKit`, `handlePickup`,
`handleDrop`, `handleExplosion`, `checkPlayerTriggers`,
`setTicket*`/`getTicket*`, `getTicketLimitReachedId`, `loadNextLevel`,
`restartMap`.

## Stages

### S1. Game state and tickets
- [x] `GameStatus` and the state machine in `GameServer::tick`
      (PreGame → Playing → EndGame).
- [x] Tickets per team: `gamelogic.setDefaultNumberOfTickets` is read from
      `GameLogicInit.con`, the multiplier is `sv.ticketRatio` from
      `ServerSettings.con`.
- [x] Ticket bleed by area weight plus a separate "final" rate.
- [x] `enemyTicketLossWhenCaptured` — the one-off loss on capturing a point.
- [x] The win condition by tickets (`endGame`).
- [ ] Warning thresholds `setTicketLimit`/`ticketState` (10, 10 %, 20 %).
- [ ] The round's time limit.
- [x] A player's death costs the team a ticket (`killPlayer`).

### S2. Control points 1:1
- [x] Template parameters instead of our own constants: `timeToGetControl`,
      `timeToLoseControl`, `areaValueTeam1/2`, `unableToChangeTeam`,
      `onlyTakeableByTeam`, `enemyTicketLossWhenCaptured`.
- [x] Neutralisation before capture (the flag goes down, then up).
- [x] The number of players in the radius affects the speed.
- [ ] `radiusOffset` and the hemispherical radius (`isHemisphere`).
- [ ] A player in a vehicle counts only as the first passenger.
- [ ] The link between a point and a vehicle spawner (`teamOnVehicle`,
      `teamFromClosestCP`).

### S3. Teams, kits, spawning
- [ ] Teams 1 and 2 with names (`setTeamName`, `getTeamName`), autobalance.
- [x] `SpawnPoint` from `GamePlayObjects.con` — the real spawn points (24 on
      Dalian) with `setSpawnPositionOffset`, in a circle, only at your own
      points.
- [x] Picking a point 1:1 with the engine: random among the suitable ones,
      the flag must be ours, nobody must be standing nearby
      (`docs/functions/spawn.md`).
- [ ] `SpawnGroup` and spawning next to the commander/squad.
- [ ] Spawning straight into a vehicle when every point is taken
      (`enterOnSpawn`).
- [ ] Kits: `menuTeamManager.addKit/addTeam/addWeapon` (currently without a
      handler), picking a kit on spawn.
- [ ] Spawn waves: `getDefaultTimeToNextAIWave`, the `uPlayingSpawning` queue.
- [ ] The spawn screen: the client picks a point → a packet to the server.

### S4b. Soldier physics (constants from the engine)
- [x] The soldier's shape is a column of spheres (5 standing), radius 0.25,
      height 1.7.
- [x] The ground is computed from objects' geometry too, not only from the
      terrain.
- [x] Steps: below the diameter of the lowest sphere the soldier steps over.
- [x] Vehicles take part in collision.
- [x] Swimming with the `start-float` / `stop-float` thresholds and its own
      speed.
- [x] Speeds from the engine: running 3.9, sprinting 7, swimming 2.1.
- [ ] Poses (crouched, prone) — 3 and 1 spheres, their own heights and speeds.
- [ ] Sprint stamina (`sprint-limit`, `dissipation`, `recover`).
- [ ] Drowning (`soldier-drown-damage` 8/s) and fall damage.
- [ ] Surface slope: right now any surface holds; it has to be cut off by
      `phy-soldier-feet-contact-normal` and slid off.

### S4. Health, damage, death
- [x] The soldier's health (100 from `ObjectTemplate.armor.maxHitPoints`),
      death, a ticket lost, respawn after `sv.spawnTime` (15 s) at full
      health.
- [x] Falling through the world is death, not eternal falling.
- [ ] `giveDamage` with a damage type and a weapon.
- [ ] Fall damage: it is not in the data, the constants sit in the engine —
      `ArmorGLComp::damage` in the Linux server has to be looked at.
- [ ] `suicide`, falling from a height, leaving the combat area.
- [ ] Materials and damage multipliers (needs `materialManager`).

### S5. Vehicles and spawners
- [x] Vehicles appearing from spawners: the template by the owning point's
      team (`setObjectTemplate`), re-placement when the owner changes. 18
      vehicles.
- [ ] The timer that brings a destroyed vehicle back.
- [ ] Entering/leaving a vehicle (`PlayerControlObject`, entry points).

### S6. Score and statistics
- [ ] A player's points: kills, deaths, captures, assists.
- [ ] The scoreboard (`Scoreboard` in the HUD).

### S7. Network
- [ ] New packets: game state, tickets, points, health, score.
- [ ] Client-side prediction of movement; reconciliation with the server.
- [ ] UDP transport (the abstraction is ready, only the loopback works).

## The in-game HUD

The data already parses: `Menu/HUD/HudSetup/HudSetupMain.con` gives **1615
nodes** in 100+ groups. The main group is `IngameHud` (60 nodes), and almost
everything in it is a `split` node, that is a reference to another group.

### H1. The skeleton
- [x] Expanding `split` nodes: `hud::buildTree` walks the tree
      `IngameHud -> sub-groups` with cycle protection.
- [x] Drawing the HUD in a game session — as a second pass over the frame
      (`renderOverlay(..., clear=false)`).
- [x] Interface textures: the path from `Menu/HUD/Texture/`, `.tga` support
      (stb was left with PNG only — which is exactly why the HUD was empty).

### H2. Live data
- [x] Both teams' tickets (`FriendlyTicketsString`, `EnemyTicketsString`): the
      label is rebuilt only when the string changed.
- [ ] The remaining values: health, ammo, the point's name, the time. The full
      list of what the engine supplies is in
      `docs/functions/hud-variables.md`.
- [ ] **The corner layers' anchors.** `GeneralHudSettings.con` declares
      separate root groups `BottomLeftStatic`, `BottomLeftAnimate`,
      `BottomRightStatic`, `BottomRightAnimate`, `TopLayer`, but nowhere gives
      their coordinates: inside them the numbers are counted from an anchor
      the engine sets itself. Because of that the health, stamina and ammo
      bars are not drawn yet. The anchors have to be got out of BF2.exe (the
      `hudManager`/`hudBuilder` group).
- [x] Bars (`Bar`) with fill from `setBarNodeValueVariable`. Along the way it
      turned out a bar has **an extra argument before the rectangle** (the
      growth direction), and every bar we had was being read one position off.
      The two textures (`setBarNodeTexture 0|1`) are now distinguished too.
- [x] The control-point strips under the minimap — from the server's live
      state.
- [ ] Logical show variables (`AND`/`EQUAL` in the data) — currently treated
      as off, so part of the interface is not shown.
- [ ] Bars (`Bar`): health, ammo, point capture.
- [ ] `ObjectMarker` and the compass — they need object positions from the
      server.
- [ ] The minimap: the level's texture plus point and player icons.

### H3. Screens
- [ ] The spawn screen (`SpawnMenu`, `SpawnInfo`) — picking a point and a kit.
- [ ] The scoreboard (`Scoreboard`).
- [ ] Messages (`gamelogic.messages.addMessage`, x288 in the game).

## Graphics

- [x] The near plane: it used to be proportional to the level's size (2.85 m
      on Dalian), so walls right in front vanished and you could see through
      them. Now it is 0.1 m, and the far distance comes from the data — past
      the fog's end (610) there is no visibility anyway.
- [x] `.ske` taken apart (`docs/formats/skeleton.md`), `ske_info` exists.
- [x] `.baf` taken apart (`docs/formats/animation.md`), `baf_info` exists. All
      3470 files in the game read with nothing left over.
- [x] Skinning: a pose from the skeleton and a clip, mesh deformation
      (`obf2::mesh::poseSkeleton` + `skinMesh`, the `--anim/--frame` flags).
- [x] Blending clips per bone following the engine's model (a stack of up to
      5 per bone, weight 1 clears it). Legs from one clip plus a weapon from
      another give a correct run with a weapon.
- [x] Triggers and bundles from the game's data: a tree of 62 triggers,
      selection by pose and speed (`docs/functions/animation-system.md`,
      `anim_info`).
- [ ] The remaining trigger conditions: messages, randomness, idling,
      direction.
- [ ] Timing and transitions: fadeIn/fadeOut, bundle lengths, BundlePlayer.
- [ ] Skinning on the GPU: the vertices are computed on the CPU right now.
- [ ] Other players' soldiers in the world.

## Readiness measures

- `command_audit`: currently **460 232 / 498 035 (92.4 %)**, 243 unique
  commands without a handler. Every stage has to raise that number.
- Tests in `tests/` for every piece of logic (tickets, capture, damage).
