# Muting a player's voice: `VoipPlayerMuteEvent` (38)

Source: the Linux server 1.5 (`Game Files/OtherFiles/linuxded-full/bin/amd-64/bf2`),
by symbol. The client's own copy in `BF2.exe` has not been opened yet.

## The event

| offset | bits | written at | meaning |
|---|---|---|---|
| type | `kEventTypeBits` | `serialize` 0x435590 | 38 |
| +0x10 | 1 | 0x435590 | 1 mute, 0 unmute |
| +0x11 | 8 | 0x435590 | player A: the one who mutes (inferred, below) |
| +0x12 | 8 | 0x435590 | player B: the one being muted |

`deSerialize` (0x435680) reads the same three fields in the same order.

## Who does what

* **`executeServer` (0x435330)**: if B is the server's own local player, nothing
  happens. Otherwise the server copies the event (0x18 bytes, the same three
  fields), sends it to **player B only** (`game->vtbl[0x120](B, event)`), and posts
  remote event 0x3b with `(+0x10, +0x11)` to B (`game->vtbl[0x130]`). The server
  does not check who A is.
* **`executeClient` (0x4354d0)**: on a client (`game->vtbl[0x1c0]()` is false)
  whose local player is B, it posts local event 0x3b with `(flag, A)`, then calls
  `playerManager->getPlayer(A)->vtbl[0x510](flag)`. On anyone else it does nothing.

A is taken to be the player who mutes because the event goes to B, and B's client
records the flag on A's player. What slot 0x510 does with it (presumably "A has
muted me", so B's voice is not sent to A) is **not established**. Neither is event
0x3b's handler.

## What we do

* The scoreboard's manage page, column 4 (`Scoreboard.singleClick`), calls
  `RemoteWorld::toggleMute`. It sends `{flag, own player, target}` and keeps the
  target in `mutedPlayers` by id and name.
* `MatchVoice` drops a muted player's relayed frames and closes his stream.
* When the server forwards the event to us (we are B), we do not act on it. Slot
  0x510 is not ported.
