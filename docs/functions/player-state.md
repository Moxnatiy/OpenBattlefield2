# A player's network state: `Player::setNetUpdate`

A player — not his soldier, the player: score, ping, team, squad, kit, whether he
is alive — is a networkable of its own and travels in the ghost stream like a
soldier or a jeep. This is where the scoreboard's numbers come from
(docs/functions/hud-scoreboard.md), and where the sprint state is set.

Sources:

* the client's reader: `BF2.exe` **0x500570** (it cites
  `…\Code\BF2\Game\GameLogic\Player.cpp`, line 0x7c3, "read error"); its
  networkable vtable holds it at 0x8ac508;
* the Linux server's `dice::hfe::Player::setNetUpdate` (0x49c580) — the same
  function with names on everything it calls; the widths agree bit for bit;
* the player's network id: `CreatePlayerEvent` (Linux `serialize` 0x423df0,
  `executeClient` 0x423fa0).

## Which ghost is a player's

`CreatePlayerEvent` carries, in order: team (3 bits, `vtbl[0x1d0]`), 4 bits
(`vtbl[0x2b0]`), 1 bit (the constructor's second argument), the player's index
(8 bits, `vtbl[0xa8]`), **the player's network id** (16 bits, `vtbl[800]()+8`),
16 bits always 0, 1 bit (`vtbl[200]`), the name (32 bytes).

`executeClient` creates the player and calls
`networkManager->addNetworkable(player's networkable, that id)`. So a ghost
record with that network id is the player's, and its content is
`Player::setNetUpdate`. A full record's mask is `0xffffffff`
(`NetworkableBase::getGhostStateMask`, Linux 0x4a2110): all 28 bits set.

## The layout

The ranged reads are `0x4f9a10(min, max)` — the smallest n with
2ⁿ−1 ≥ max−min+1 — and the signed ones `0x4f9c10(n)`: a sign bit, then n−1
bits of magnitude. The inline loops `do n++ while ((1<<n)-1 < X)` give the
same widths.

| bit | read | where it goes (client / Linux) | what |
|---|---|---|---|
| — | 28 bits | | the mask |
| 0x1 | 1 bit | `vtbl[0x64]` / `setIsAlive` (0xd0) | alive |
| 0x2 | 0..255, 9 bits | `vtbl[0x150]` / `setSpawnGroup` (0x2a8) | the spawn group |
| 0x8 | signed 12 (sign + 11) | score block `[0]` | score |
| | 0..2047, 12 bits | score block `+0x10` | deaths |
| | 0..2047, 12 bits | score block `+0x14` | kills |
| | signed 12 | score block `+0x04` | teamwork (rplScore) |
| | 0..2047, 12 bits | score block `+0x18` | team kills |
| 0x200000 | 1 bit | `vtbl[0x174]` / `setFlagHolder` (0x2f0) | carries the flag |
| 0x10 | 1 bit | `0x4fa000` / record +0x51 | purpose not established |
| 0x80000 | — | +0x138 := the tick (`0x4c4420`) | a time stamp |
| 0x100000 | — | +0x13c := the tick | a time stamp |
| 0x2000000 | 2 bits | +0x22c | a counter modulo 4; a change raises +0x228 — purpose not established |
| 0x40, 0x80, 0x20000 (or 0x10000 for all three) | per state: 1 bit on/off; then, when on or 0x10000: 7 bits (value − 1), 2 bits, 1 bit, 1 bit choosing 8 bits × 10/255 or 6 bits / 63, 4 bits | `setFiringState`, `setAltFiringState`, `setFlareFiringState` (Linux 0x340, 0x350, 0x360) | the three firing states; the inner fields' purpose is not established |
| 0x40000 | 1 bit, 1 bit | `setSprintState`, `setSprintStateLastTick` (Linux 0x370, 0x380; client `vtbl[0x1b4]`, `[0x1bc]`) | **the sprint** |
| 0x100 | 11 bits (0..1023+) | player +0xf8 (client) / +0x118 (Linux `getPing` 0x4a2440) | the ping |
| 0x20 | 32 bits, 1 bit | +0xd8, +0xdc (Linux +0x104, +0x108) | the tick the player may spawn at, and "man down": `getTimeToSpawn` (Linux 0x4944e0) is that tick less the game tick, over 30, not below 0 — the spawn bar's countdown |
| 0x200 | 3 bits, value − 1 | `vtbl[0xe0]` / `setTeam` (0x1c8) | the team |
| 0x400 | 4 bits, then 1 bit | `vtbl[0x17c]()->vtbl[0x18]` / `getCamera()->…` | a camera setting — purpose not established |
| 0x800 | 9 bits (0..255) | +0x270 as a float | purpose not established |
| 0x1000 | 9 bits (0..255) | `setKit(low 4)` (0x2d8), `setChosenUnlockLevel(low 4, …, high 4)` (0x4a0) | the kit and its unlock |
| 0x2000 | 5 bits (0..16) | `vtbl[0x104]` / `setSquadId` (0x210) | the squad |
| 0x4000 | 6 bits, value − 1; then three of 9 bits (0..255) | `setRank` (0x5cca00); score block +0x54, +0x58, +0x5c | rank; first, second and third places (`pmgr_getScore`'s `firstPlace`…) |
| 0x8000 | 1 bit | `vtbl[0xa8]` / `setFreeCameraEnabled` (0x158) | |
| 0x400000 | 1 bit | `vtbl[0xb4]` / `setOverheadCameraEnabled` (0x170) | |
| 0x800000 | 8 bits | +0x12c | purpose not established |
| 0x1000000 | 17 bits, value − 1 | +0x350 | purpose not established |
| always | 1 bit | +0x105 | purpose not established |
| 0x4000000 | 1 bit; when set, five single bits and three 8-bit fractions (/255, anything under 1/512 is 0) | `vtbl[0x10c]` / `setIsCommander` (0x220); +0x106..+0x114 | commander, and his fields |
| always | 1 bit | +0x19c | purpose not established |
| 0x8000000 | 3 bits | `0x4faa00` / `setMaskSetting` | not applied to the local player |

Bit 0x4 is not read at all.

Only the mask's fields are applied (after the reads, 0x500f3a on); the local
player skips the firing and sprint states, which he owns.

## The measure

A layout that is right ends every record exactly at the length the ghost stream
gives it. `net::bf2::readPlayerState` does, on every player record:

* all four captures in `tests/data` — 39, 7, 22 and 7 records
  (`test_bf2_world`, `testPlayerRecordsReadToTheirLength`);
* a live run on the rig (Strike at Karkand, twelve bots, 2400 frames): 1477 of
  1477 (`player records:` in the `--connect` report).

And a check that owes nothing to us: in BF2 a kill is two points, so a player's
score is 2 × kills + teamwork. It holds for every player whose score came, in
every capture and on the live server — which fixes the score block's order
(score, deaths, kills, teamwork, team kills).
