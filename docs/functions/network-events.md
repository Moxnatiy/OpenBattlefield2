# Network events

This is a dictionary separate from the game events: with it the two sides drive
the connection itself, not the world. The number travels in `PostRemoteEvent`
(game event type 11) together with a category.

## Where it comes from

`BF2.exe` registers them itself, and both the number and the name are visible
in that same function:

```c
basic_string(local_20, "ECNetworkNELoadComplete");
(**(code **)(*DAT_009ff964 + 0x34))(6, 2, local_20);
```

That is "category 6, number 2, name". There are no symbols in the client, but
there are the strings with the events' names — and they gave the whole table.

Every number we had previously derived by measurement against the server
matched: 2 and 4 from the jump table of `GameServer::handleNetworkEvent`, 6 by
a separate trial, and 11, 13, 17, 18, 19, 20 from what those branches call.

## The table

| # | name | what it means |
|---:|---|---|
| 1 | `NEDataBlockReady` | a data block has been assembled |
| 2 | `NELoadComplete` | the client has loaded the level |
| 3 | `NEStartSimulation` | start counting |
| 4 | `NEDatabaseComplete` | the client has received the player base |
| 5 | `NEReset` | reset |
| 6 | `NESelectSpawnGroup` | the player picked a spawn point |
| 7 | `NESelectTeam` | the player picked a team |
| 8 | `NESelectKit` | the player picked a kit |
| 9 | `NEPlayerSpawned` | the player has spawned |
| 10 | `NEPlayerDead` | the player has died |
| 11 | `NEStringReceived` | a string was received |
| 12 | `NESuicide` | suicide |
| 13 | `NERadioMessageReceived` | a radio message |
| 14–16 | `NETraceComplete`, `NETraceFailed`, `NETraceResourceConnect` | tracing |
| 17 | `NENetworkableDestroyed` | an object is gone — refresh visibility |
| 18 | `NERemoteConsoleCommand` | a command in Python |
| 19 | `NERemoteConsoleFeedback` | the console's answer as a data block |
| 20 | `NEEndOfRoundGuard` | end-of-round acknowledgement |

## How to send them

`PostRemoteEvent` (type 11) carries: 4 bits of category, 32 bits of number,
32 bits of delay (float), 8 bits of data length and the bytes themselves.

The events that carry a value expect a **32-bit number** in the data. That is
visible in `handleNetworkEvent`: for the spawn-point choice it takes the
buffer's first four bytes and passes them to `gameLogic`:

```
rax = node->[0x30]      // pointer to the data
r12d = *(int*)rax       // the value itself
gameLogic->[0xe8](gameLogic, player, r12d)
```

## The player spawning

The server spawns those whose `Player::getSpawnGroup() > 0` — that is visible
in `ServerGameLogic::uPlayingSpawning`, and the check itself sits in entry
0x2b0 of `Player`'s method table. It is `NESelectSpawnGroup` that sets that
field.

So the spawn chain is: `NESelectTeam`, `NESelectKit`, `NESelectSpawnGroup`.
Each of them has been verified to arrive and to be dispatched by the server.

## The NEDatabaseComplete trap

At first it looked as though events were being lost in transit: event 7 sent on
its own worked, but in a queue with others it did not. Measurement showed
otherwise. A breakpoint in `GameEventManager::processReceivedPacket` printed
the number of every batch received:

```
batches 0, 1, 2, 3, 4 — and then silence
```

Batches 5, 6, 7 never even reached the event parsing. At the same time it was
visible that after event 4 the server **stops sending pings**. So it is neither
packet loss nor our packing: after `NEDatabaseComplete` the server simply stops
talking to us.

We removed that event — and the whole chain went through: 2, 7, 8, 6, and after
it `ServerGameLogic::spawnPlayer`. The real client evidently does not send it
here; we do not send it at all.

The more general lesson: when something is "lost", it is worth checking first
whether we broke it ourselves with the previous action.

## Why there are no ghosts: the content check

We measured one thing at a time and got to the end of the chain.

The server **does** send ghosts — a breakpoint on the write branch fires
hundreds of times. But not to us: in our session every packet goes out with
events, and the server puts ghosts into the empty ones. So our connection has
not grown into the required state.

The state is moved by `GameServer::clientSendDatabaseComplete`, and everything
is visible in it:

```
if (client number != 0x81 and serverInfo->[0x4a]):
    if (client->getContentValid())  -> continue
    else -> queue for disconnection, reason 27
client->state = 4                 // exactly what isClientReady wants (> 3)
send the client NEStartSimulation
```

So `NEDatabaseComplete` does not "break the connection" — it drives the server
to a check we do not pass, and it disconnects us. Hence the silence we saw:
after it the server simply stops talking to us.

The flag `serverInfo->[0x4a]` is set by `GameServer::init` from
`loadMiscFingerprints`, and that computes the checksum itself
(`ChecksumContext::runMiscChecksum`) — it cannot be turned off by a setting.

`contentValid` is set by the `ContentCheckEvent` (type 46): three 128-bit
hashes. The server compares them in `GameServer::onContentCheckEvent`, using
`MapInfo::getChallengeOrdinal()`.

So the full chain is:

1. `ClientInfo`
2. **`ContentCheckEvent`** — three hashes
3. `NEDatabaseComplete` — state 4, the server sends `NEStartSimulation`
4. `NELoadComplete`
5. `NESelectTeam`, `NESelectKit`, `NESelectSpawnGroup`
6. and only then the ghost stream

We stopped at the second step: to pass it, the checksum computation has to be
repeated exactly as the game does it.

## Spawning works, and there are still no ghosts

`capture.py --stage spawn` goes the whole way and the server spawns the player
— that is visible from a breakpoint. But the ghost stream stays empty, and the
cause has not been found. The event parsing is nearly exact at that: of 25
packets only one is left with a long unread tail, so it does not look like a
shift.

The likeliest suspicion is the connection's state: `GameServer::isClientReady`
requires it to be above three, and it is moved by `clientLoadComplete` and
`clientSendDatabaseComplete`. The second is exactly the event that cuts our
conversation short. Apparently the server itself is supposed to raise it, not
the client.

## Transplanting names into BF2.exe

BF2.exe has no symbols, but both binaries have the same `Debug` checks, and
each carries the path of a source file with a line number. In the Linux server
the function's name is known next to such a check.

`tools/transplant_symbols.py` matches the "file + line" pairs and gives a table
"address in BF2.exe -> function": 335 matches, 249 distinct functions, seven
seconds of work. With `--script` it produces a ready Ghidra script that renames
everything at once.

A check against something known: the tool named
`ClientConnection::ClientConnection` exactly the function we had previously had
to work out by hand from the number of checks in the constructor.


## The content check: how it works

`GameServer::onContentCheckEvent(client, hash1, hash2, hash3)`:

```
client = getClient(number)
if (client and client->state <= 1)   -> return, do nothing
if (serverInfo->[0x4a] == 0)         -> return
the three hashes -> into strings
number = MapInfo::getChallengeOrdinal()
four lookups in the fingerprint tables by that number
comparison; on a match -> the content is considered valid
```

Two things that cost time:

**The state has to be greater than one.** While the client has only just
registered, its state is exactly 1, and the server simply ignores the check —
silently. So `NELoadComplete` has to be sent first, and only then the check.

**The first hash is not from a file.** The tables `std_archive.md5`,
`std_archive_mod.md5`, `bst_archive.md5`, `bst_archive_mod.md5` lie in the
mod's directory and have the form "number + md5", but the second and third
hashes are compared against them. The first is compared against what the server
computed itself at startup (`loadMiscFingerprints` ->
`ChecksumContext::runMiscChecksum`) and keeps in field `0x208` of its object.

The value for our rig is `1bb87eb2398987fe2111d26dca42e505`; it is the same
between restarts, because it is a checksum of files. It can be read with a
breakpoint, and that is exactly how we verified that a match occurs.

The challenge number from `getChallengeOrdinal()` is **0**, not the number that
stands at the start of the level block. Those are different things.

## The order that works

```
ClientInfo
NELoadComplete          (the state becomes greater than 1)
ContentCheckEvent       (the content check matches)
NEDatabaseComplete
NESelectTeam, NESelectKit, NESelectSpawnGroup
```

That has not produced ghosts yet: after the content check
`clientSendDatabaseComplete` reached neither the "state 4" branch nor the
refusal branch. What exactly stops it is the next question.

## The real obstacle: five batches

After the content check it turned out the matter was not about events at all.

We sent eight identical harmless events in a row. The server accepted
**exactly five batches** — numbers 0, 1, 2, 3, 4 — and fell silent:

```
>>batch field5=0
>>batch field5=1
>>batch field5=2
>>batch field5=3
>>batch field5=4
```

Beyond that the breakpoint no longer fires even in
`ClientConnection::processReceivedPacket`, so the packets vanish at the network
level, before the streams are parsed. And at that:

- **there is no disconnection** — `closeClientConnection` is not called;
- the server stops sending **pings** too, so it is silent in both directions;
- it does not depend on the events' content: the same with
  `NEDatabaseComplete`, with the content check and with eight identical
  `NELoadComplete`.

So everything we had previously explained by the events' content was actually
running into this limit. The spawn chain worked only because it fitted into
five batches.

### What taking the numbering apart showed

`NetServer::handleDataPacket` has a check: a packet with **the same number** as
the previous one accepted is discarded. That is visible directly:

```
>>data seq=0 expects=0   -> discarded
>>data seq=0 expects=0   -> discarded
>>data seq=1 expects=0   -> handed to the client
```

So the six-bit counter has to grow with **every** packet sent, and ping replies
spend it too.

At first I decided the opposite — that the counter counts only data packets and
that pings must not touch it — and even got better numbers on the probe. That
turned out to be wrong: in the client the same change left one packet instead
of twelve, and a measurement showed why — a ping and the next data packet got
the same number, and the second was discarded as a duplicate. The change was
reverted in both places.

### There is no five-packet wall

The packet filter was checked separately (`PacketFilter::processPacket`, which
everything arriving in the socket passes through): it let all 17 of our packets
through and discarded none.

And in a clean experiment the probe calmly goes the whole chain — the level,
the content check, the base, the team, the kit, the spawn point — and receives
the full world stream from the server. So the earlier "the server accepts
exactly five batches" was a consequence of the broken numbering in those
attempts, not a separate limit.

## The connection's state: measured

We read the state field straight out of the connection object on every packet
received:

```
state 0 -> 0 -> 0 -> 1 -> 3
```

- `1` is set by `clientSendPlayerDatabase` during registration;
- `3` is set by `NELoadComplete`;
- `4` is set **only** by `clientSendDatabaseComplete`, and `isClientReady`
  requires exactly more than three.

## There is a five-data-packet limit after all

Measured cleanly: the server processes exactly **five** of our data packets,
after which none reaches `handleDataPacket`. Ping replies do not count and keep
going. The filter (`PacketFilter::processPacket`) lets everything through, so
the packets vanish inside `NetServer::_update` — between the filter and the
parsing. Why exactly is not established yet.

It can be worked around: put **all the events into one packet**. Then the chain
fits into five packets, and `clientSendDatabaseComplete` is finally called.

## Where we stand now

Hard facts, each measured more than once:

- the connection's state grows `0 -> 1 -> 3`; `isClientReady` requires more
  than three, and only `clientSendDatabaseComplete` sets the four;
- the server does write ghosts (dozens of times per session), but **into its
  own local connection**, not to us — because we do not pass `isClientReady`;
- `GameServer::getConnection(0)` works for us: 758 successful lookups in a row.
  Our connection is in the table;
- our data packets stop being processed after the fourth or fifth. The filter
  lets everything through, so they vanish inside `NetServer::_update`.

What has been checked and does **not** explain the loss:

| assumption | result |
|---|---|
| rate limiting | 8-second pauses gave not more but fewer (4 instead of 5) |
| the acknowledgement mask | a zero mask instead of `0xFFFFFFFF` changed nothing |
| the packet number | the server discards only duplicates, our numbers grow correctly |
| the content check | it passes on its own, the hashes do match |

The packet loss is the bottleneck: everything else we took in turn for the
cause — the `NEDatabaseComplete` event, the content check, an empty
`getConnection` — turned out to be a consequence of the needed packet simply
not arriving. Three times in a row a conclusion was drawn from a single
observation and three times it proved wrong; from here on every hypothesis is
worth checking with at least two runs.

The next step: take `NetServer::_update` apart to the end — from the filter to
`handleDataPacket` — and find the branch our packets are discarded by.

## What was learned about the measurement itself

In the end it turned out that part of the observations was an artefact of the
instrumentation. gdb breakpoints stop the process on every hit, and there were
hundreds a second — the server slows down from that and its socket queue
overflows. That is precisely where the packet "disappearances" in some of the
experiments came from.

Without gdb the picture is different: the packets arrive, but the chain still
does not complete.

The second trap was in the temporary scripts. `capture.py --stage player`
registers a player reliably (`admin.listPlayers` shows the entry), while the
scripts I hacked together do not, even though they apparently do the same
thing. So the discrepancy is in them, not in the protocol. The conclusion for
further work: run experiments only through `capture.py`, not through one-off
scripts.

The content check was added to `capture.py --stage spawn` (the first hash is
passed with the `--misc-hash` flag), but an end-to-end run does not yet add up:
the level block does not arrive in time at this stage.

## Spawn groups: the number comes from the server, not from the level

Right after registration the server sends **24** `CreateSpawnGroupEvent` events
(type 57). It is their number that `NESelectSpawnGroup` expects — our control
point numbers from `GamePlayObjects.con` mean nothing to the server.

The layout from `CreateSpawnGroupEvent::deSerialize` (0x424520) is cross-checked
against the constructor, whose signature survived in the symbols:

```
CreateSpawnGroupEvent(unsigned char, unsigned short, int,
                      bool, bool, bool, unsigned char, unsigned char)
```

The wire order differs from the constructor's; it is visible from the sequence
of reads and the offsets they land in:

| bits | offset | what it is |
|---|---|---|
| 8 | +0x10 | the group's number in the server's list |
| 4 | +0x14 | the team (`group->[0x38]()`) |
| 1 | +0x18 | `group->[0x58](0)` |
| 1 | +0x19 | the group's field 0x9a |
| 1 | +0x1a | the group's field 0xa0 |
| 8 | +0x1b | the position, X axis |
| 8 | +0x1c | the position, Z axis |
| 16 | +0x1e | **the network id** — this is what we send |

The event is created by `SpawnManager::createSpawnGroupOnClients` (0x4b96e0).

### Packing the position

`SpawnGroup::getUnsignedWorldPosition` (0x4b94b0):

```
half = GLSWorldSizeX / 2            (1024 by default, the level sets its own)
pos >  half -> 255
pos < -half -> 0
otherwise  byte = (int)((pos + half) / (2 * half) * 255)
```

The multiplier 255 sits as a constant at 0xb355bc. In reverse:
`pos = byte / 255 * worldSize - worldSize / 2`.

### What a live server gives

Dalian Plant, 16 slots:

```
id 515, team 1, position -84 -269     powerplant       (-92.3, -260.8)
id 516, team 2, position -173 -68     constructionsite (-151.8, -58.9)
id 517, team 0, position  76  -60     reactors         (88.0, -40.0)
id 518, team 0, position -237 -245    mainentrance     (-254.0, -210.0)
```

Two independent signs agree: the team field gives 1, 2, 0, 0 — exactly like
`gameLogic.setTeamName` and the flag owners in `Init.con`, and the unpacked
positions land 12..39 m from the flags. The discrepancy is expected: a group's
position is the average of its spawn points, not the flag itself.

The other 20 groups have ids 578..597, a "first" field of 192..211, alternating
teams (1, 2, 1, 2, ...) and a position exactly at the centre — those are the
squad groups.

## The server breaks the connection on two events

For a long time it looked as though there was no ghost stream because of a
parsing error of ours. In fact the server was simply **disconnecting us**, and
we were counting its packet as "other".

The disconnect packet is `05 b0 01 00 00 00 00 10`, kind 5 (`Disconnect`), and
it arrives three times in a row. Measured from the server's side too: in
`admin.listPlayers` our player vanishes on the same second.

Two experiments (`--no-content`, `--no-database` in our client):

| what we send | when the break happens |
|---|---|
| the content check + `NEDatabaseComplete` | right after the check |
| `NEDatabaseComplete` alone | right after it |
| neither | **no break** |

Without those two events the whole spawn chain goes through, the connection
stays alive (14 pings over 30 seconds against 4-5 with them), and the server
keeps the player in the list.

## The challenge number: found, where to take it from

That was the whole blockage. The server picks the number **at random when it
loads the level** — `GameServer::loadPath` (0x45e490) does `srand(time)` and
`rand() % 10`, and then `MapInfo::setChallengeOrdinal`. Then
`onContentCheckEvent` (0x462390) takes `getChallengeOrdinal()` and looks for
exactly that line in the four fingerprint tables. No match — the player is
disconnected.

We were taking the number from the first number of block 5 and always got 1. In
fact block 5 (28 bytes) is simple: `u32`, level name, mode, size. There is no
number in it at all.

The number lies in **block type 2** — that is the real `MapInfo`, the one
`MapInfo::updateNetBuffer` (0x4168b0) assembles. The field order from there:

```
u16 length + string   the game mode
u16 length + string   the path to the levels
u16 length + string   the level's name
1 sign bit + 31 bits  how many slots
1 bit                 whether there is a commander
1 sign bit + 31 bits  the challenge number
```

The numbers are written not as a whole word but as a sign and 31 bits — which
is exactly why they were invisible when the block was read as bytes.

A live block (40 bytes):

```
06 00 "gpm_cq"  07 00 "Levels/"  0c 00 "dalian_plant"  20 00 00 00 11 00 00 00 00
```

The tail `20 00 00 00 11 00 00 00 00` gives, in bits, **16 slots**, **there is a
commander**, **challenge number 4**. The first two match `sv.maxPlayers 16` and
the fact that the commander is enabled on the server — three independent
confirmations on one tail.

With the right number the content check passes, there is no break, and the
server starts sending the world at once: 63 ghost-stream packets, 166 state
updates, 41 objects, and in the very first packet "there is a controlled-object
state" — that is our soldier.

## How it looked while the number was unknown

The ghost-stream flag in a data packet is now read separately (`ghostFlag`),
and it answers unambiguously: in 8 packets out of 9 the server **sets it to zero
itself**. So our parsing is sound, and the world is absent because the server
does not send it.

So the chain `NESelectTeam`/`NESelectKit`/`NESelectSpawnGroup` alone is not
enough for spawning: something else the real client does is missing. The next
step is to find the condition under which `ServerGameLogic` starts sending
state, and what exactly the server dislikes about the content check.

## The spawn condition on the server: fully reversed

`ServerGameLogic::uPlayingSpawning` (0x4ab5a0) is a loop over the players, and
it holds exactly two checks:

```
for every player:
    if player->getIsAlive()          -> skip   (slot +0xd8)
    if player->getSpawnGroup() <= 0  -> skip   (slot +0x2b0)
    group = spawnManager->getSpawnGroup(player->getSpawnGroup())
    point = group->getSpawnPoint(player->getIsAIPlayer())
```

The slot names are taken from `dice::hfe::Player`'s method table (0xb34da0),
not guessed.

`ServerGameLogic::selectSpawnGroup` (0x4a4160) does nothing but
`player->setSpawnGroup(value)` (slot +0x2a8). So the number we send in
`NESelectSpawnGroup` lands in the field **as is**, without translation.

The key into the group map is visible from `SpawnManager::addSpawnPoint`
(0x4ba820): when the group number is not given (-1), it takes
`spawnPoint->[0x60]()` and calls `getCreateSpawnGroup` with it. So the key is
the identifier of the spawn point itself, not the network id from
`CreateSpawnGroupEvent`.

### The group number: the small one, not the network one

Captured traffic of the original client (`sudo tcpdump -i any -w /tmp/bf2.pcap
"udp port 16567"`, parsed with `tools/linuxded/pcap_bf2.py`) answered this in
one go:

```
23.54s  cl->sv  NELoadComplete
24.67s  cl->sv  NEDatabaseComplete
24.96s  sv->cl  NEStartSimulation        <- this the server sends, not the client
25.06s  cl->sv  NESelectKit = 2
32.44s  cl->sv  NESelectKit = 4          <- the player picked the engineer
34.38s  cl->sv  NESelectSpawnGroup = 2   <- DONE was pressed
34.48s  sv->cl  NEPlayerSpawned = 1      <- the server confirms the spawn
```

So `NESelectSpawnGroup` expects a **small number** — the same one that arrives
as the first field of `CreateSpawnGroupEvent` (1..4 for Dalian's four flags).
Neither the group's network id (515..518) nor the level's control point number
(401..404) will do, plausible though both looked.

Two more conclusions from the same dump:

* `NEStartSimulation` is sent by the **server to the client**, not the other way
  round — our `--start-sim` experiment was a wrong guess;
* the original does not send `NESelectTeam` at all in this pass: it accepts the
  team the server assigned.

With the right number everything works: the server answers `NEPlayerSpawned`,
and a new soldier object appears next to the flag.

### Which object is ours

A soldier in BF2 is a controlled object too, and the player "occupies" it just
as they do a vehicle. The server sends the spawn reply in **one packet**, and it
holds everything needed:

```
34.48s  server->client  120b
   CreateObjectEvent: template 3284, id 1794
   EnterVehicleEvent: player 1 -> object 1794
   CreateKitEvent; HandlePickupEvent
   NEPlayerSpawned = 1
```

So: our own player number comes from `CreatePlayerEvent` (the server assembles
the name as "clan tag + space + name", so we compare by the tail), and our own
object from `EnterVehicleEvent` by that number. `ExitVehicleEvent` (type 10)
clears it. No distances to a flag and no guesses.

The same `CreatePlayerEvent` also says the **team** the server assigned. That is
the source of truth: the original client does not send `NESelectTeam` at all in
this pass but takes what it is given. So the spawn screen has to show the
circles on the flags of exactly that team — otherwise DONE asks for another
team's flag, and the server is entitled to refuse.

### Which kit a soldier wears

The rest of that spawn packet names the kit, for every player's spawn alike:

| event | reader (Linux server) | fields |
|---|---|---|
| `CreateKitEvent` (31) | `deSerialize` 0x422b30 | template 32 bits (+0x10), network id 16 (+0x14), position 3 raw floats (+0x18), 4 bits (+0x24), 4 bits stored minus one (+0x28) — the last two purpose not established |
| `HandlePickupEvent` (14) | `deSerialize` 0x428a60 | player 8 bits (+0x10), id 16 (+0x12), id 16 (+0x14) |

`HandlePickupEvent::executeClient` (0x428860) looks both ids up in the network
manager and calls `GameLogic::handlePickup(object +0x12, player, object +0x14,
true)` (vtable 0x1d8). Measured on the live co-op server over 21 pickups: the
+0x12 id was always a kit `CreateKitEvent` had just named, and +0x14 a soldier —
heavy soldiers took AT, Support and Assault kits, light ones Medic, Engineer,
Specops and Sniper, as the levels' `Init.con` pairs them. A soldier that spawned
before we joined has no known kit until it spawns again.

### Our own soldier is moved by the client, not by the server

That is the main thing we did not understand. After spawning the server **does
not send us the position of our own soldier**: it is not in the ghost stream's
records at all (verified — object 1794 is absent from the record list), and the
controlled-object state arrives only once.

The explanation is simple and confirmed by the method's name:
`PlayerControlObjectNetworkable::predict(float)`. The client computes its own
soldier's movement **itself** from its own input, and the server only corrects
it when the divergence is too large. So:

* to move, the player-action stream has to be sent (the first bit in a data
  packet is exactly that, and in the client's packets it is set);
* to avoid hanging in the air, that movement has to be computed by us: the
  server puts the soldier on the spawn point, and the fall from it is our
  business.

Both tasks are the same task.

## The controlled-object state: the start of the layout

`GhostManager::readControlObjectState` (0x445c30), called from
`processReceivedPacket` (0x446cc0) right after the stream's header, when the
flag is set in it:

```
12 bits                 a number at the start
1 sign bit + 31 bits    a counter
32, 32, 32              the position — also the compression reference point
16 bits                 the network id of the controlled object
1 bit ...               the rest of the state
```

Two fields are named not by guesswork but by what the function does with them:

* **the triple of numbers is the compression reference point.** Before it comes
  `BitStream::resetCompressionVector` (0x445cf7), after it
  `setCompressionVector` with that same triple (0x445d4b). Further along the
  stream the vectors are written as a difference from it. At the same time it is
  the soldier's position;
* **the 16 bits are our object's id.** The engine hands it straight to
  `NetworkManager::getObject` (0x445dc3), and the result to `getSoldier`
  (0x445e01).

The second closes the debt "recognising one's own soldier is a guess". On a
live server the id from the state matches the one the enter event gave (both
1794), and before spawning the controlled object is a different one — the spawn
screen's camera (257/258 on Dalian, with the position from
`setBeforeSpawnCamera`). So the soldier's position may be corrected by the
controlled-object state only when the ids match: otherwise spawning produced a
232-metre jump — from the camera to the spawn point.

### Why the controls were jittering

Three causes, and none of them in the protocol:

1. **The socket queue was growing.** We took **one** packet from it per frame,
   while the server sends more. The difference accumulated: over 30 seconds we
   managed to parse 41 data packets and 17 controlled-object states instead of
   ~286 and 268. We were looking at the world as it had been several seconds
   earlier, and every correction threw the soldier back.
2. **The prediction step was fixed at 1/60**, regardless of how long the frame
   actually lasted.
3. **We corrected before spawning too** — with the spawn screen camera's
   position.

After the fix the divergence between our prediction and the server is
**0.69 m on average, 1.0 m at most** (it was 16.3 and 232). That is small
enough that there is no need to smooth the position substitution yet — and so
no need to invent a constant for it.

About smoothing in the engine it is known that it exists, but **not for our
soldier**: `PlayerControlObjectNetworkable::predict` is empty (`rep retq` at
0x5d44e0 in the Linux server), and `local-prediction-lerpTime` (1.0 by default)
and `local-prediction-minDelay` (0.15) are read only by
`GenericProjectileNetworkable::predict`. The values were taken from
`Vars::getFloat` in a static initialiser (0x5cd025 and 0x5cd037) with the script
`tools/elf_symbol.py`.

The decoding was verified by an exact match: the first such packet on Dalian
gives `-50.0 185.0 -285.0`, and the level's `Init.con` has
`gameLogic.setBeforeSpawnCamera -50/185/-285`. So before spawning the server
holds us on the spawn screen's camera and sends exactly that.

### The spawn delay was ours

We separated the three selection events with a three-second pause, and after
DONE almost ten seconds passed before spawning. In the captured traffic the
original sends them when the player presses, and the server answers
`NEPlayerSpawned` within 100 ms. A pause is left only between the handshake's
steps — and even there the original sends `NELoadComplete` and the content check
**in one packet**, with `NEDatabaseComplete` 1.1 s later.

### Why we hang above the ground

Exactly and without guesses: all 24 of Dalian's spawn points have
`setSpawnPositionOffset 0/1.25/0`. The server puts the soldier 1.25 m above the
ground, and in the original he falls at once — but the fall is computed by the
client, and we do not compute it yet.

## The player-action stream

This is what the client moves its soldier with, and what our parser used to
stumble on in the client's packets: the first bit after the header is exactly
it.

It is read by `PlayerActionManager::processReceivedPacket` (0x44d670):

```
1 bit                  are there actions
4 bits                 how many sets in the packet
9 bits                 a number (200, 232, 334 in the dump — it varies)
1 sign bit + 31 bits   the input counter, +1 per packet
for every set:
    6 times: 1 sign bit + 15 bits of value
    32 bits  the button mask
    9 bits   (always zero in the dump)
    1 bit    a flag (one in the dump)
```

The set's size in memory is 28 bytes, and the offsets match exactly: six words
at +4..+0xe, a `u32` at +0x10, a `u32` at +0x14, a byte at +0x18.

### One action per tick, and the client plays the quantized one

`BF2.exe`, `FUN_005c0260` — the client's per-tick input step:

1. takes **one** `PlayerInput` off the queue at `+0x80` (or a zeroed one when
   the queue is empty);
2. hands it to the controlled object's interface 0xc4c5, vtable `+0x15c`;
3. `PlayerAction::set` (0x5bc890): every axis goes through 0x5bc5f0 —
   `value * 100.0` (0x8e9474), clamped to `±32767.0` (0x8e9470 / 0x8e9478),
   then `_ftol2` (0x83d84c), which **truncates** — into an int16; the buttons
   whose input is above 0.5 become mask bits (`c_PI*` number − 8);
4. `PlayerAction::get` (0x5bc6a0) turns the int16 straight back into
   `value * 0.01`;
5. that **quantized** input is set on the player (vtable `+0x40`, flag 1).

So the original simulates its own soldier with exactly the numbers the server
will receive, one action per game tick. A client that turns its camera with the
raw mouse, or sends one action per wall-clock interval, drifts from the server:
ours built the action every frame and sent whichever was current every 33 ms,
so at 60 frames half of the mouse movement never reached the server — a 360°
turn on our screen was about 180° in the original client watching us.

### What the axes mean

The captured traffic answered directly. The original sends three sets per packet
(a reserve against loss) thirty times a second, and in the seconds when the
player was running:

```
38.58s  forward 99, buttons 0     started running
38.88s  forward 99, buttons 32    pressed sprint
39.45s  forward 99, buttons 0     released
39.95s  forward 99, buttons 32    sprint again
40.21s  forward 0                 stopped
```

So **the third axis is forward movement**, and full movement is exactly 99.
**The fifth and sixth** twitch slightly all the time between −169 and 169 — that
is the mouse. **The button mask: bit 5 (value 32) is sprint.** The other three
axes never once departed from zero in the whole dump, so we do not name them.

### And the server began correcting us at once

While we sent no input, the controlled-object state arrived **once** — the spawn
screen's camera. As soon as the action stream started, the server began sending
it again and again (counter 0, 6, 12 ...) with the soldier's real position:
`-149.2 158.6 -47.4` with the constructionsite flag at `-151.8, -58.9`.

And that is what we set the camera to — so the 1.25 m hover disappeared too: the
position from the server has already settled, rather than being the spawn point
with its offset.

### What we still cannot do

Our player **does not spawn** on a live server, even though the conversation
goes through completely: there is no break, the ghost stream flows, the server
keeps us in `admin.listPlayers`. We tried 1, 2, 401, 402, 515, 516 as the group
number — none produced a soldier.

What follows from this: most likely our `PostRemoteEvent` does not reach the
event handler at all, in which case neither the team choice nor the spawn choice
takes effect. An indirect proof is that the server puts us in team 2 regardless
of whether we ask for 1 or 2.

The experiments that remain as flags: `--start-sim` (adds `NEStartSimulation`),
`--block-ready` (acknowledges every assembled block with a `NEDataBlockReady`
event). Neither changed the result, but neither caused a break either.

The next step is to check the delivery itself: send an event whose consequence
is visible from outside (a chat message, say) and look in the original client to
see whether it arrived.

## Ghost-stream records: what is already known for certain

`GhostManager::readData` (0x445820 in the Linux server):

```
2 bits    kind
16 bits   network id
kind 1 — a state update:
    1 bit     whether this is a full state (baseline) rather than a difference
    N bits    the content's length; N is a field of GhostManager itself (+0x4298),
              not a constant. On our server it equals 11
    then      content of exactly that many bits
kind 3 — the object disappears: disableObject + removeActiveDescriptor
kind 2 — the engine treats this as a stream error (puts 2 into its field +0x20)
```

The length in the record is what allows an unknown object to be skipped — that
is exactly what the engine does, and exactly why our parser can walk the whole
stream without understanding the content.

Then the engine takes `NetworkManager::getDescriptor(id)`, the object from the
descriptor, and calls `setNetUpdate` of its networked class.

### Why another soldier's position is not read yet

`SoldierNetworkable::setNetUpdate` (0x5dc640) is not a simple layout:

* the state lies in a ring of **0xa0-byte** structures
  (`getUpdateAndMarkUsed` / `getUpdate` with a `memcpy` between them), that is
  the client keeps several of the latest states and interpolates between them
  (`SoldierNetworkable::setPredictedState`);
* its own fields are not read as one block: 21 bits, 8, 3, 3, and beyond that
  `dice::anim::RagDoll::readCurrentState` runs separately;
* part of the fields is determined by the state mask (`getGhostStateMask`) and
  `BaseLineData` — that is, without a full state the difference cannot be read.

So another player's soldier placeholder still stands where the object was
**created**: we have the position from `CreateObjectEvent`, but not the
movement. The next step is precisely that function, and it is worth starting
from which of the fields the state mask gives.

## The template number: what it is **not**

The server sends a template number in `CreateObjectEvent`, not a name. To draw
real geometry instead of a placeholder, that number has to be translated into a
name.

What has been established:

* **it is not sent as a list.** Over the whole conversation the server sends
  only three data blocks: 0, 2 and 5 (`--connect` prints each). There is no
  "number -> name" table among them;
* **the engine takes the template from
  `ObjectTemplateManager::getTemplate(unsigned)`** (Linux server, 0x6a1110) —
  and that is an ordinary `std::map` by number. So the numbers are handed out
  during loading and must land identically on both sides;
* the client creates an object by calling the factory with that number, and on
  failure says "Failed to create requested object templ id" (`BF2.exe`,
  `CreateObjectEvent::executeClient`, line 0x9f in `CreateObjectEvent.cpp`).

The simplest assumption — "the number is the order of template creation" — has
been **checked and is wrong**: `--calibrate` matches numbers to names by an
object's position, and of 21 pairs **not one** matched (we have 10594
templates).

But the direction is right. It is visible that the numbers run consecutively in
the data's order: Dalian's control points got 5679, 5680, 5681, 5682 — exactly
the order in which they stand in `GamePlayObjects.con`. So what differs is not
the idea but **the set and the order** of what we load.

The measure for the next attempt is simple and checked with one command:

```bash
openbf2 --level dalian_plant --calibrate tests/data/bf2-world.bin
```

it has to show "matched 21, unmatched 0".

## Why another player's soldier does not move

Three things have been established, and one remains.

**A soldier's layout differs from a simple object's.** Its networked class is
its own — `SoldierNetworkable` (0x5dc640):

| | simple object | soldier |
|---|---|---|
| mask width | 19 (`readBits ..., 0x13`) | **21** (`readBits ..., 0x15`) |
| `getGhostStateMask` | 0x5849b (0x5d6990) | 0x1950ff (0x5dabe0) |
| position bit | 1 (`testb $0x2`) | **7** (`testb %dl,%dl; js` at 0x5dc941) |
| the reference | from `BaseLineData` | **`nullVec`**, that is zero (0x5dd367) |
| precision | 0.0005 | **0.01** (0xb6d934) |

The mask's width is not arbitrary: it equals the number of bits in that class's
`getGhostStateMask`.

**Which object is a soldier need not be guessed.** `CreatePlayerEvent` gives the
player's team, `EnterVehicleEvent` the object they occupied.

**And here is what is missing.** We read ghost records only in packets
**without** a controlled-object state — because we cannot skip it. After a
player spawns such a state travels in almost every packet: 389 of 393 in the
measured run. So we throw away nearly the whole stream, and other players'
soldiers stay where they were created.

So the next step is concrete: read
`GhostManager::readControlObjectState` (0x445c30) to the end — not for the
fields' sake but to know **where it ends**. After it come ordinary records,
which we already handle.

## Walking past the controlled-object state

Written out with `tools/linuxded/bitfields.py --blocks
GhostManager::readControlObjectState`, branch by branch:

```
12                 a number at the start
1 + 31             a counter (sign and magnitude; both branches 31)
32, 32, 32         the compression reference point -> setCompressionVector
16                 the network id of the controlled object
1                  flag A; if 1 -> 16 more bits (0x445f93)
1                  flag B (0x445db3, after getObject)
                   if 1 -> 1 bit (0x445f33); if that is 1 -> 16 (0x445f66)
1                  flag C (0x445e52)
3                  both branches read 3 bits (0x445e80 / 0x445fde)
```

Verified with data rather than by eye: on the capture
`tests/data/bf2-spawned.bin` (taken after spawning, with our own input) after
the walk exactly as many records are read as the header named — **199 packets of
200**. One packet takes a branch we have not worked out yet: the function has a
read of 10 bits in a loop (0x44633a). Such a packet simply yields no records.

The consequence is visible at once on a live server: state updates in the
stream went to **521 instead of 48**.

## Why another player is still absent

Now it is known for certain that this is **not** the parsing. The other
player's object (1602) **never once appears in the ghost records**, while our
own (1794) does:

```
player 1 -> object 1602: NOT in the ghost records
player 2 -> object 1794: IS in the ghost records
```

So the server does not send it to us. The next question is not the layout but
the **visibility scope**: under what conditions the engine puts an object into
the stream for a particular client. To be looked for in `GhostManager` (who is
added to the active descriptors and when) and in
`Player::getUpdateFrequencyType` / `Object::getUpdateFrequency`.

## The compressed vector's reference is the object's own previous position

In the soldier's layout the reference argument is `dice::hfe::nullVec`
(0x5dd367), and at first we took it literally. Measurement showed otherwise.

The experiment is simple and repeatable: two of our clients on one server, both
spawned, and the second prints what it read about the first.

```
with zero as the reference:      other soldier 1794 -> -0.3 0.0 13.4
with the stream's reference:     other soldier 1794 -> -50.3 185.0 -272.8
with the previous position:      other soldier 1794 -> -186.3 154.3 -35.5
```

The first is a pure difference in metres. The second is that difference added to
the wrong point (the reference then was the spawn screen's camera). The third
lands on the map and at the same height as the other soldiers (154.3).

So the compressed vector's base is **the last known position of that same
object**, not a single point for the whole stream. Until the object has been
seen, the position from `CreateObjectEvent` serves as the base.

The measure is passed: another player's position arrives from the ghost stream
and lands on the map.

## Correction: we can NOT read another soldier's position yet

The previous section ("the reference is the object's own previous position") was
done from **one** successful sample, and that turned out to be a premature
conclusion. A measurement with two clients, both standing still, showed
otherwise:

```
other soldier 1602 -> -138.0 -128.8 22.1        (the terrain there is 154)
other soldier 1794 -> -6.7e27 0.0 -0.0
```

The cause is visible in the function itself. In
`SoldierNetworkable::setNetUpdate` the position does **not** lie immediately
after the mask: other fields under other mask bits come before it, and at least
one of them is read first —

```
mask 21 bits
if mask & 0x40  -> 8 bits     (0x5dc753)
if mask & 0x20  -> 3 bits     (0x5dc7f3, 0x5dc83f)
...
the position                   (0x5dd35f, under a different bit)
```

`bitfields.py --blocks SoldierNetworkable::setNetUpdate` shows over twenty read
sites, strings among them. So this is not "adjust a bit" but reading the whole
function through — a separate pass under rule 3.

Until that is done, the client **does not show what it cannot read**: a
soldier's position is accepted only if it is finite, lies within the map's
bounds and is no more than a few metres from the ground. How many were rejected
is printed in the report, so the debt does not hide behind the filter.

The measure stays the same: another soldier's track has to have "above the
ground 0.0 m on average", and when he stands still, "travelled 0.0 m".

## The soldier state's layout — from the client

Read through in `BF2.exe`, `SoldierNetworkable::setNetUpdate` (0x62d4e0). The
client's decompilation gives the field order directly, with the branches — as
opposed to reading the disassembly by addresses, which we got wrong twice.

```
mask                       21 bits
if mask & 0x40             8 bits, then 1 bit
if mask & 0x20             3 bits, then 3 bits       (range 0..4)
if mask & 0x8000           a different branch (ragdoll) — not ours from here
if mask & 0x1              THE POSITION: a compressed vector, precision 0.001
if mask & 0x80             velocity (precision 0.001 / 0.01)
if mask & 0x2              12 bits -> yaw, expanded to +-360
if mask & 0x4              12 bits -> pitch, +-90
if mask & 0x8              12 bits -> +-180
if mask & 0x10             12 bits -> +-90
then                       a dozen more fields
```

Two mistakes this fixed:

* **the position is enabled by bit 0, not bit 7.** Bit 7 is velocity. Reading
  from it we were getting numbers like -6.7e27;
* **other fields come before the position.** Without skipping 8+1 bits (mask
  0x40) and 3+3 bits (0x20) the read is shifted.

The reference is the **stream's compression vector**, the same one the
controlled-object state sets: `FUN_0062bd60` reads the vector from the field
`stream+0x54`. It is the neighbouring fields that are passed `nullVec`, not the
position.

A check on a live server: player 1602 gives a stable track —

```
other soldier 1602 -> -177.9 154.2 -106.4
other soldier 1602 -> -177.6 154.2 -106.7
soldier 1602's track: 4 updates, travelled 0.4 m, above the ground 1.00 m on average
```

The numbers are on the map and the height is steady. Exactly 1.00 m above the
terrain is a constant difference, not a drift. The metre is
`coll-soldier-pivot-height` — see "The soldier's position is its pivot" below.

## A life on a live server, measured

Taken with `openbf2 --connect` against the original server on Strike at Karkand,
one unattended run: `--exec-at 700:openbf2.spawnAt 6`,
`--exec-at 1900:spawnManager.commitSuicide`, `--exec-at 2900:openbf2.spawnAt 6`.

| step | what arrives | note |
|---|---|---|
| before the first spawn | control state names object **258** | the spawn screen's camera |
| `NESelectSpawnGroup` 6 | `EnterVehicleEvent` → object **1795**, then `NEPlayerSpawned` (9) with value 1 | |
| first control state for 1795 | the compression reference is the creation position, ~1.2 m above the terrain | the spawn point's offset; the body falls onto the ground |
| client sends `NESuicide` (12) | control state names **258** again, then `NEPlayerDead` (10) | the camera comes back one packet **before** the death event |
| second `NESelectSpawnGroup` | `EnterVehicleEvent` → object **1795 again**, `NEPlayerSpawned` | **the soldier's object is reused across lives**, with a new creation position |

Two consequences, both built into the client:

* **a life ends on `NEPlayerDead`, not on a new object id.** The id does not
  change, so anything keyed on "a different soldier" misses every respawn. The
  predicted body was placed once per connection, behind `!bodyReady`, and on
  respawn the new soldier was simulated from wherever the old one died — the
  "hanging in the air" seen in play. It is now reset on `NEPlayerDead` and placed
  again at the new creation position; the run above shows it settling on the
  terrain in both lives.
* **the camera is not a soldier.** Because 258 returns one packet ahead of
  `NEPlayerDead`, the client briefly took it for its soldier. The controlled
  object seen before the first spawn is remembered and never adopted.

The capture of that run is `tests/data/bf2-karkand-lives.bin`.

## The controlled-object state, read whole

`GhostManager::readControlObjectState`, `BF2.exe` 0x5b9860 (the writer is
0x5b9230). The earlier sections walked it field by field from the Linux server
and never found the end; the client's function says where it is — the very first
field.

```
12 bits            the size of everything after this field, in bits
1 + 31             a counter (FUN_004f9c10(32): sign, then magnitude)
32, 32, 32         the compression reference -> setCompressionVector
16                 the network id of the controlled object
1                  flag A; if 1 -> 16 bits
1                  flag B; if 1 -> 1 bit; if that is 1 -> 16 bits (a vehicle's id)
                   -- only if the object exists on our side --
1                  a baseline flag
3                  a baseline index
                   every networkable of the object, in order, through its vtable
                   +0x30 with type 3 (FUN_005b81c0 / FUN_005b8550) — unframed
10                 a length, then a part read by NetworkManager's vtable +0x194
                   the prediction component
                   "Size differs bits read ... indicated size" when the count is off
```

So a reader that does not understand the contents skips `12 + size` bits and
lands exactly on the ghost records: `tests/test_bf2_events.cpp` walks **200 of
200** packets of `tests/data/bf2-spawned.bin` that way (it was 199 by the field
walk).

For a soldier the first networkable is `SoldierNetworkable`, whose slot +0x30
(vtable 0x8fcba8, slot 12) is `setNetUpdate` 0x62d4e0. Type 3 selects its
**controlled** layout. Both layouts, every field, are in
`src/net/include/obf2/net/soldier_state.h`:

| bit | Ghost | Controlled |
|---|---|---|
| mask | 21 bits | 21 bits |
| 0x40 | 8 bits, 1 bit | same |
| 0x20 | two values 0..4, 3 bits each | same |
| 0x8000 | ragdoll branch (FUN_007e5440 / FUN_007ea290); the rest is skipped | same |
| 0x1 | position, compressed from the stream's reference, 0.001 | same |
| 0x80 | velocity, from zero, 0.01 | velocity, from zero, 0.001 |
| 0x100, 0x200 | — | vectors from zero, 0.0001 — purpose not established |
| 0x40000 | — | vector from zero, 0.001 — purpose not established |
| 0x2, 0x4, 0x8, 0x10 | 12 bits each, expanded to ±360, ±90, ±180, ±90 | 32 bits each, the float itself |
| always | 0..3 (3 bits), 0..1 (2 bits), three single bits | same |
| 0x400, 0x800 | 10 bits, expanded to ±50 | 16 bits, expanded to ±50 |
| always | 1 bit | same |
| 0x4000 | 5 bits / 31 | 7 bits / 127, then 1 bit |
| 0x1000 | 0..(weapon count + 1), minus one: the weapon index | same |
| 0x2000 | 2 bits | same |
| 0x20000 | — | 7 bits, expanded to ±1 |
| 0x10000 | 0..255 (9 bits); if not zero, 12 bits / 4095 | same |
| 0x100000 | 16 bits; if not zero, 12 bits / 4095 | same |
| 0x80000 | 0..15 (5 bits) | same |

Two traps in reading it:

* **the ranged read takes one bit more than the range needs.**
  `FUN_004f9a10(min, max)` takes the smallest n with `2^n - 1 >= max - min + 1`:
  0..1 is two bits, 0..3 three, 0..255 nine. Read as `ceil(log2)` everything
  after the first ranged field is shifted;
* **0x1000's width is not in the stream.** It is the soldier's weapon count,
  asked of its inventory (`*(object+0x14)+0x22c` → `+0x10`). Our own soldier's
  mask on Karkand is 0x1f7fff, with 0x1000 set, so our reader stops there
  (`SoldierState::complete` stays false) until the kit's weapon count is known.
  Everything before it — position, velocity, angles — is read.

Measured on `tests/data/bf2-karkand-lives.bin` (`tests/test_soldier_state.cpp`):
**47 of 47** controlled states of our soldier, across both lives, put it on the
x and z its `CreateObjectEvent` named, to 5 cm, while the player stood still.

## The soldier's position is its pivot

The height in that state is **not the feet**. In both lives:

| life | created at y | server's y at rest | terrain |
|---|---|---|---|
| 1 | 164.649 | 164.399 | — |
| 2 | 164.663 | 164.412 | — |
| live run | 164.6 | 164.40 | 163.40 |

The creation point is the spawn point's `setSpawnPositionOffset 0/1.25/0`; the
soldier falls 0.25 m and rests with its position **1.00 m** above the terrain.

The metre is `coll-soldier-pivot-height`. `BF2.exe` registers it at 0x860f00 with
the default 1.0 (the handle at 0xa086f0), and `FUN_006ed4c0`
(`Physics/SoldierResponse.cpp`, soldier against soldier) takes a soldier's
vertical extent as `[matrix.y - pivot, matrix.y + pose height - pivot]`, the pose
height being `coll-soldier-stand/crouch/prone-height`. The same handle is read by
0x6ee040, 0x6ee4d0, 0x6eeef0 and 0x6efc30.

With the pivot taken off, `openbf2 --connect` puts the server's feet on our
predicted body: `correction: server -253.53 163.40 -139.59, us -253.53 163.40
-139.59, ground 163.40 (dy 0.00)`, 28 samples, 0.04 m on average (the one 1.00 m
sample is the first tick, while the body was still falling from the creation
point).

The replay that makes the position usable is the next section.

## Playing our own soldier: actions, the server's answer, the replay

### What a packet of actions is

`PlayerActionManager` in `BF2.exe`: constructor 0x5bffb0 (vtable 0x8e9968), three
28-byte action slots at +0x18, +0x34, +0x50.

* **transmit** 0x5bfbe0: when the manager has actions (`+0x6c`, "numNewActions"),
  1 bit set, 4 bits of count, 9 bits of `+0x80`, then `FUN_004f9bc0(slot[0].tick,
  32)` — the counter is the **first** set's tick — and every set (0x5bfaf0);
  "PlayerActionManager failed to write stream size %d numNewActions %d" on
  failure;
* **processReceivedPacket** 0x5bfea0: reads the same, numbers set i as
  `counter + i` (+0x88, stride 28), then 0x5bf940 plays only the sets whose tick
  is newer than the last one played (`+0x78`) and pushes them into the player's
  action buffer (`FUN_005bc590`).

So a packet carries the last actions oldest first, and a lost packet costs
nothing. We used to send three **copies** of the current action with the counter
+1 per packet; the receiver took them for three consecutive ticks.

### The client's tick

`FUN_005c0260` (one per game tick): one `PlayerInput` off the local queue, into
`PlayerAction::set` (0x5bc890, see above), straight back through
`PlayerAction::get` (0x5bc6a0), and that quantized input is what the soldier is
played with. See "One action per tick" in the action-stream section.

### The server's tick for a remote player

`FUN_004cc400`: the player's action buffer (player vtable +0x94).

* not empty: the head action is played; `+0xec` = its tick; the buffer's
  repeat count `+0x30` is reset;
* **empty: the last action is played again**, `+0xec` = last tick + 1, up to 30
  times (`+0x30 < 0x1e`); after that a zeroed input.

A repeated action repeats its mouse movement too. Measured with `--look-at`:
a 90° turn on our screen became 101.95° on the server, once our ticks were
bursty against its own; the replay below shows that on our screen as a jump of
up to 6°. Whether the original client does anything to keep the buffer from
running dry is **not established**.

### The replay

The controlled-object state ends with the prediction component: 0x5b9860 calls
`(*DAT_0099e348→+0xc)→+0x10(stream, control id, counter, …, 1/30)`, which is
`FUN_004d4b30`:

1. the networkables have already put the server's state on the soldier
   (0x62d4e0 type 3: position, velocity, angles);
2. `FUN_005bc530(counter)` drops every action older than the counter from the
   player's action list and keeps the one equal to it;
3. every action left is played again: `PlayerAction::get`, set on the player,
   the object simulated with the tick (vtable +0x3c), then 0x5b2f30, 0x5b30a0,
   0x5b3130.

Ours does the same (`RemoteWorld::reconcile` in `src/app/main.cpp`): the feet
(position − pivot), the velocity, the look, the sent list trimmed by the counter,
the rest played. Measured on the live server, running forward for 180 frames
through a turn: **the replay lands 0.02 m from the prediction on average**
(it was the full lag before); two lives, a suicide in between, the same.

### Where the angles go

The apply block of 0x62d4e0 writes the four angles into the soldier:

| bit | field | what `FUN_005a8630` does with it |
|---|---|---|
| 0x2 | +0x224 | the body's yaw |
| 0x4 | +0x240 | the aim's yaw offset from the body; the look is `+0x240 + +0x244` (the mouse's, `0x5a99a0` on the +0x15c sub-object), clamped by the handle at 0x9ec2a8 |
| 0x8 | +0x248 | purpose not established |
| 0x10 | +0x238 | the pitch; the look is `+0x238 + +0x23c` (the mouse's), clamped by 0x9ec2a8[1] |

Measured: during a turn the body (0x2) lags and the offset (0x4) carries the rest;
their sum follows the turn and settles when the offset returns to zero. A mouse
movement down of 60° gave 0x10 = +59.95, so the pitch's sign is the opposite of
ours. The spawn's yaw arrives in the first state — 100.52° on one run, where we
used to start at 0.

## Other players: the class decides the layout

A ghost record's layout is the object's networkable class. We decided "soldier"
by `EnterVehicleEvent` — whoever a player entered — and a player drives jeeps.
Recorded with the original client in the gas station's jeep on Karkand
(`tests/data/bf2-karkand-other-player.bin`):

| object | template | full record's mask | position |
|---|---|---|---|
| 1841, the jeep | 5184 | 0x5849b | bit 19, 0.0005 |
| 1731, his soldier | 3283 | 0x1950ff | bit 21, `SoldierNetworkable` ghost layout |

Both masks are the classes' `getGhostStateMask` (Linux server 0x5d6990,
0x5dabe0) — a full record carries the whole mask, so it names the class. A
soldier record with mask 0 is exactly 30 bits: 21 + the nine bits every soldier
state has.

Both positions are packed against the stream's compression vector (`FUN_0062bd60`
reads `stream+0x54`): raw floats while we had no soldier, differences from our
soldier's position after. The jeep lands on its creation spot in 18 updates of
18 either way; read with the soldier's layout, the same bits gave metres of
scatter one packet and 4.9e29 the next.

A second recording showed the other trap: the server had sent `EnterVehicleEvent`
only for an older object of that player, so his live soldier had **no team** on
our side, and was drawn as level property. The class does not depend on that
event.

What is still not read: a soldier record with the 0x1000 bit (the weapon index)
stops the reader there, as in our own state.

## Objects the server creates, measured

The same live server, `CreateObjectEvent` by `CreateObjectEvent`, each matched to
what the level places at that spot (`openbf2 --connect`, the `created:` lines).
The template **number** is stable per template across objects — every
`trestle01_dest` came as 3763, every yellow barrel as 3741 — and the objects fall
into two groups:

| group | examples (number → what stands there) | what to draw |
|---|---|---|
| the level's destructibles | 3741 barrel_yellow, 3763 trestle01_dest, 3762 trafficlight_dest, 3696 lrg_stonebridge, 3770 highway_bridge_low_segment | nothing — the level already draws them |
| what spawners issue | 5184 `…gasstation_Jeep`, 5217 `…gasstation_HeavyTank`, 5134 `…gasstation_APC`, 5007 `…market_HeavyJeep`, 5035 `…market_HeavyTank`, 3970/3972 UAV, 3989/3991 radar | the vehicle the spawner issues |

**The number belongs to the vehicle, not to the spawner.** The same kind of spawner
gives different numbers on different sides: the machine gun on the three MEC
points came as **5553** and the one at the US gas station as **5561**; artillery
5498 on MEC ground and 5575 on US; the AT emplacement 5503 against 5510. So an
object on a spawner's spot is the vehicle `ObjectTemplate.setObjectTemplate`
names for the side holding that spawner's control point. Resolved that way on
the same run, every one came out on the right side:

| MEC points | US gas station |
|---|---|
| `JEP_VODNIK`, `RUTNK_T90`, `MEC_BIPOD` (5553), `ARS_D30` (5498), `ATS_HJ8` (5503), `aircontroltower_mec`, `mobileradar_mech_dest` | `USJEP_HMMWV` (5184), `USTNK_M1A2` (5217), `USAPC_LAV25` (5134), `US_BIPOD` (5561), `USART_LW155` (5575), `ATS_TOW` (5510), `aircontroltower`, `mobileradar_us_dest` |

These pairs are also the first ground truth for the template-number table the
protocol debt asks for: fifteen numbers with names, from the server itself. The
limit of the position rule is the spawner's side: it is the control point's
owner at the round's start, and a point that changes hands would change the
answer until capture events are read.

In our code: `obf2::level::PlacementIndex` (with a test), used by the client's
draw loop — spawned objects get their vehicle's mesh, static ones are left to
the level, anything else keeps the grey placeholder.

## How often the server sends: the connection type

`ConnectionTypeEvent` (game event 3): the event type, then the connection type in
3 bits (`ConnectionTypeEvent::serialize`, Linux server 0x422360). On the server
`GameServer::setConnectionType` (0x45f5c0) clamps the value to its
`maxConnectionType` (+0x11c — it calls itself with the maximum, it does not kick)
and copies two numbers of the type's row of `g_connectionTypes` (0xb308c0, six
rows of six u32, read with `tools/elf_symbol.py _ZN4dice3hfe17g_connectionTypesE
--binary …/linuxded-full/… --type u32 --count 36 --columns 6`) into the client's
connection, +0x20 and +0x24:

```
type  +0   +4   +8    +0xc  +0x10 +0x14
0     10    4   160   320   200   200
1     10    4   160   320   200   200
2     30   10   160   350   200   200
3     30   20   320   400   200   200
4     30   20   320   512   200   200
5     30   20   320   640   200   200
```

We never sent it. The player's profile (`Documents/Battlefield 2/Profiles/<default
user>/General.con`) says `GeneralSettings.setConnectionType 5`; sending that after
`ClientInfo`, on the live server, over the same 1500 frames:

| | ghost packets | step between them |
|---|---|---|
| without the event | 58 | 7–8 ticks (~250 ms) |
| with type 5 | 311 | 1–2 ticks |

That the +4 column is an update rate is read from this measurement, not from the
code that uses it. Without a profile the event is not sent.

## Drawing between updates

`SoldierNetworkable::predict` (0x62d130) and `SimpleObjectNetworkable::predict`
(0x62f7d0), in `src/net/include/obf2/net/ghost_track.h` with every address:
a ring of four updates stamped with the server's time (the ghost header's tick,
0x5b9ee0), drawn at `now − GSInterpolationTime` (100 ms) — interpolated between
the two updates around it when they are 0–500 ms apart, extrapolated along the
velocity for at most `GSExtrapolationTime` (1200 ms) past the newest, the newest
otherwise. Defaults registered at 0x4077cd, read into the game object's
+0x50/+0x54 by 0x6a5f60.

### Who calls `predict`, and with what time

`FUN_004d5460` walks every networkable of the world (skipping two objects it asks
the player manager for — the controlled one and the one being entered) and calls
slot +0x14 of each, `predict`, with **one** number: `FUN_004c4400() * 1000`.

That number is the game clock, and it is two globals:

| address | what |
|---|---|
| `DAT_009a7428` | the game tick, an int |
| `_DAT_009a7420` | the same as seconds: `tick * _DAT_00970398` (the tick time, 1/30) |
| `FUN_004c4400` | reads the seconds — the getter every caller uses |
| `FUN_004c4440(tick)` | sets the tick outright, and the seconds with it |
| `FUN_004c4470(n)` | adds `n` ticks |

Who moves it:

* `FUN_004d4b30` — the answer to a controlled-object state: `FUN_004c4440` at
  0x4d4bc9 sets the clock to **the packet's server tick**, and `FUN_004c4470` at
  0x4d4c15 adds one tick for every action replayed on top of it;
* `FUN_004e0530` and `FUN_004e1f00` — a tick of the client's own loop, one each.

So the client's clock is the newest packet's server tick plus the actions the
server has not played yet, and the ghosts of other players are drawn at that
minus `GSInterpolationTime`. A client whose actions are answered after 6 ticks
therefore draws everyone 100 ms **past** the newest update — extrapolated, by
design. Ours does the same (`world.setGameTick(header->time + sent.size())`), so
this is no longer a stand-in.

### Who gets records: relevance, and what a client without a soldier gets

The rate is not the question — the server keeps its pace whatever the client
does. What changes is **which objects** get records in the packets it is already
sending.

`ServerConnection::updateGhostManager(float)` (Linux server 0x43ca90) runs this
every pass:

1. `this->vtable[0x30]()` — the connection's player;
2. `GhostManager::resetGhostStates(false)` (0x442c80);
3. `this->vtable[0x40]()` — true for a connection that takes everything, and then
   `getAllObjects()` (0x43bd30) and nothing else;
4. **the player is null → return** (0x43cc60, a bare `retq`);
5. `player->vtable[0x300]()` — the player's controlled object.
   **It is null → the same return**;
6. `object->vtable[0x20](0.0f)` — its transform, copied out as the Mat4 the pass
   scores against;
7. `player->vtable[0x188]()` — true takes the squad branch (0x43ccdd): the squad
   leader from `squadManager` (+0xe8, with the player's squad from
   `vtable[0x1d0]`), queried for `IID_ICameraObject`, and **his** transform is
   used instead;
8. `getRelevantObjects(mat, radius)` (0x43c790), which scores every descriptor
   through `calculateObjectPriority` (0x43c220).

So a player who has not spawned has no controlled object, and step 5 returns
before anything is scored at all. Nothing is marked relevant that pass; what the
client still receives is the residue of the ghost manager's own active set,
handed out in rotation.

`ServerConnection::calculateObjectPriority` (0x43c220), as far as it is read:

* counts itself in `connection+0x1e8`;
* a null descriptor, or an object whose owner is this player, scores **0**
  (0x43c31d);
* the object's position comes from `getRootParent` (0x69a1e0) and its
  `vtable[0xd8]`; the player's from the matrix's translation (`mat+0x30`);
* **beyond a radius the score is 0**: the squared distance is tested against the
  call's second-to-last float (0x43c318);
* inside it, the distance is compared with `FLT_EPSILON` (0xb2bfb0), and the
  direction to the object is dotted with the matrix's forward axis (`mat+0x20`) —
  so where the player is looking counts. The constants around that dot are 0.001,
  1000.0, 10.0 and 1.0 (0xb2f3c4, 0xb2f3c8, 0xb2f3cc, 0xb2f3bc); **how they
  combine into the final score is not established**, only that distance and the
  view direction are both in it.

Measured on the live server (900 frames), the same soldier and the same server:

| | the server's pace | the pace's worst gap | updates on one soldier | newest sample at draw | largest step between frames |
|---|---|---|---|---|---|
| not spawned | 9.8 packets a second, 1.9 ticks apart | 53 ticks | ~1 a second, in bursts of 28 | 1.4–3.9 s old | 10–22 m |
| spawned (`--group 4`) | 14.2 a second, 1.5 ticks apart | 5 ticks | ~20 a second | 167–233 ms old | 0.33–0.40 m |

1.5 ticks between packets is the twenty a second the connection type asks for, and
the client gets it either way. It is the records inside them that the relevance
pass decides, and 0.33 m is a sprinting soldier's 7 m/s over one frame — the
measure the debt row asks for. The extrapolation stays (the clock above says it
should) and it stops jumping, because the updates arrive.

A simple object's own rotation (a quaternion in its update slot, slerped by
0x6d9290 in its `predict`) is not read from its record: moving vehicles keep
their spawner's rotation.

## A vehicle's update, read whole: `SimpleObjectNetworkable::setNetUpdate`

`BF2.exe` 0x6306b0 (the path `…\Networkables\SimpleObjectNetworkable.cpp` in its
error call, line 0x20b). It writes into the object's ring slot (0xbc bytes at
`+0x58`, slot `+0x38`; copied from the previous slot first, 0x2f ints), stamps
the slot's time (+0), and reads, for a ghost record (`param_5` — a baseline
buffer — is 0 there; with a buffer the vectors are read against it instead, and
the buffer's cursor `+0x400` advances by 0x24):

| order | condition | read | slot field | applied as |
|---|---|---|---|---|
| mask | always | 19 bits | | |
| 1 | 0x2 | compressed vector from the stream's vector (`param_2+0x54`), 0.0005 (0x3a03126f) | +0xc position | `FUN_0062ed00` (the object's position) |
| 2 | 0x4, **physical** | compressed vector from zero (0x9f6768), 0.001 | +0x38 | physics +100 (linear velocity) |
| 3 | 0x8, **physical** | type 3: compressed vector 0.001; otherwise the **wide** vector (0x6b6f50) from zero, 0.01 | +0x5c | physics +0x68 (angular velocity) |
| 4 | 0x1 | quaternion, `FUN_006b7270(14)` | +0x18 rotation | |
| 5 | 0x4000, **physical** | compressed vector 0.001, then another | +0x80, +0x8c | physics +0x184, +0x18c |
| 6 | 0x20, **physical** | compressed vector 0.0001 (0x38d1b717) | +0x44 | physics +0x74 |
| 7 | 0x40, **physical** | compressed vector 0.0001 | +0x68 | physics +0x78 |
| 8 | 0x100, **physical** | compressed vector 0.0001 | +0x50 | physics +0x7c |
| 9 | 0x200, **physical** | compressed vector 0.0001 | +0x74 | physics +0x80 |
| 10 | 0x80, **physical** | 1 bit | +0x9c | asleep: physics +300 / +0x120(-1) |
| 11 | 0x8000, **physical** | 1 bit | +0xb0 | `FUN_005fe210` when the template class is 0x9c47 |
| 12 | 0x10, the object has the interface at `+0x3c` | `FUN_004f9a10(min, max)` with the range from that interface (+0x88, +0x84) | +0x98 | +0x38 of that interface (a health-like value; purpose not established) |
| 13 | 0x400 | 2 bits | +0x9d | object +0x14c |
| 14 | an object up the chain answers 0xc4c5 (a player control object), template class ≠ 0xc5a8 | 0x800, 0x1000, 0x20000: `ranged(0, 8) − 1` each; 0x10000: `ranged(0, 15)`; 0x40000: 1 bit | +0xa0, +0xa4, +0xa8, +0xb4, +0xb8 | control object +0x78(slot N) per index, +0x150, +300 |
| 15 | the same, template class = 0xc5a8 | 0x2000: `ranged(0, 7)` | +0xac | object +0x158 → +0x10 |

**physical** is `(*(object+0x44))->vtable+0x130()`, asked of the object on the
client; the stream does not say it. Fields 12–15 depend on the object too.

The readers:

* **the quaternion** `FUN_006b7270(n)`: four values, each `n` bits over
  `2^n − 1` (`FUN_004f9c70`) mapped to `2u − 1` — `n = 32` reads raw floats
  instead; read in the order w, x, y, z, stored as (x, y, z, w) and normalised by
  0x62ebc0 (a length under 1.19e-7 becomes the identity);
* **the wide vector** `FUN_006b6f50(base, precision)`: a 3-bit level; 0 — three
  raw floats; 7 — the base itself; 1..6 — sign and magnitude of 24, 20, 16, 12,
  10, 8 bits per component (the table at 0x9791c4 starts with 28 at level 0), times
  the precision, plus the base.

Still to check on data: which objects are physical (a quaternion read from the
right place has a length of 1 before normalising — the test for it), and the
quaternion's convention against a spawner's rotation from the level.

## The client's clock and its tick budget

Found the caller the previous section did not:

* `FUN_004d5740` — the client's frame. `param_2` is the number of ticks due.
  With an allowance at `+0x148`: more ticks due than allowed → run **one**, the
  allowance drops to 1, the rest are dropped; otherwise the allowance grows by one
  per frame up to 3. Each tick runs `FUN_005c0460`. It also reads
  `GSUseClientSidePrediction` (default 1) into `+0xfd`.
* `FUN_005c0460` — one client tick: … `FUN_005c0260` (our actions) … then
  `FUN_004d5460`, which calls `predict(gameTime * 1000)` (slot +0x14 of the
  networkable's interface, 0x8fcb8c for a soldier) on every active descriptor
  except our controlled object and its vehicle.
* the game time: `FUN_004c4400` returns 0x9a7420 = the game tick (0x9a7428) ×
  1/30 (0x970398). `FUN_004c4440` sets the tick, `FUN_004c4470` adds to it;
  `FUN_004ee9f0` resets it to 0 at a level's start.
* `FUN_004d4b30` (the replay) sets the tick to the packet's server tick
  (`GhostManager +0x1080`, the 4th argument, 0x4d4bc9) and adds one per action
  played again (0x4d4c15, network mode 1).

Measured on the live server with that clock: the game tick runs 5–9 ticks ahead
of the newest packet, which is the number of our actions the server has not
answered (5–7, steady — not growing). The replay lands 0.00 m from the prediction
over 504 corrections while running. Frame blending between ticks is ours
(`BF2FrameInterpolator` exists, not reversed).

## Template numbers, reproduced

`tools/template_order.py --walk lower` and `obf2::game::TemplateNumbers`
(`tools/template_numbers/template_numbers <modDir> [name…]`) give the server's
numbers for the mod's templates; both agree line by line (6294), and with every
pair measured on the live Karkand server at offset 0:

| name | server | ours |
|---|---|---|
| us_heavy_soldier / us_light_soldier | 3283 / 3284 | 3283 / 3284 |
| lrg_stonebridge, barrel_yellow, trafficlight_dest, trestle01_dest, highway_bridge_low_segment | 3696, 3741, 3762, 3763, 3770 | the same |
| aircontroltower(_mec), mobileradar_mech/us_dest | 3970, 3972, 3989, 3991 | the same |
| jep_vodnik, RUTNK_T90, USAPC_LAV25, USJEP_HMMWV, USTNK_M1A2 | 5007, 5035, 5134, 5184, 5217 | the same |
| ARS_D30, ATS_HJ8, ATS_TOW, MEC_BIPOD, US_BIPOD, USART_LW155 | 5498, 5503, 5510, 5553, 5561, 5575 | the same |

The rules, each found by what it fixed:

1. archives in `ServerArchives.con` order, each walked on its own;
2. directory entries sorted by lower-case name — `_asia` before `ambient…` (the
   zip's order put `_asia` and `_middle-east`, 266 templates, after `military`:
   offset +241 against −25);
3. a name created again takes no new number (25 repeats before the soldiers,
   one before `ARS_D30`: exactly the −25 and −26);
4. the files are run at the script runner's **highest level**: `BF2.exe`
   0x69ec30 compares keywords with `_stricmp` (0x737eb0) and, at scope depth 0
   (`+0x89c`, pushed by `if` at 0x69eb90, popped at 0x69ebe0), refuses `beginRem`
   ("\"beginRem\" not allowed on the highest level!", 0x69eed7) and `if` (0x69f625)
   — the line is dropped, the block runs. Three templates inside `beginrem` blocks
   (`em_vExp_Su30_Fuselage`, `p_vExp_Su30_Fuselage`, `em_vexp_mig29_fuselage`)
   are numbered by the server; skipping them left us 3 short.

Not covered: the level's own templates (control points, 5720/5721 on Karkand) and
where `Common_server.zip` and `Booster_server.zip` fall relative to them.

## Between ticks: `BF2FrameInterpolator` and the camera

The client's frame, `FUN_0040ca80`:

1. the ticks due this frame (`FUN_006a41c0` of the timer);
2. when there are any, `storeObjectStates(ticks)` (`FUN_0045bbb0`, the path
   `…\Main\BF2FrameInterpolator.cpp` in its checks) stores every visible object's
   transform and the tick count (`+4`) **before** the ticks run
   (`FUN_0045b1f0` per object tree);
3. the ticks (`DAT_0099e348 +0x2c(ticks, 1/30)`);
4. before drawing, `interpolateObjectStates` (`FUN_0045c190(now, tick start)`):
   `f = (now − tick start) / (ticks × 1/30)`, clamped to −0.1..1.1 (0xbdcccccd,
   0x3f8ccccd), each stored object blended by `FUN_0045bfc0(object, f)`; the
   soldier's camera (class 0x9493) is not blended — it gets this frame's mouse
   movement from the input device (+0x38) through its component 0xc4d7 (`+0x94`);
5. draw, then `FUN_0045c4b0` restores the states.

Measured with `--trace-frames` while turning and running: before, the look and the
eye stood still for 3–5 frames and jumped each tick; with the body blended by `f`
and the unsent mouse added to the look every frame, the yaw grows by exactly 0.5°
a frame (5 px × 0.02 × 5) and the eye moves every frame. Steps of 0.5–1° remain
where a server state corrects the look; where they come from (the mouse offset at
`+0x244`, which the state does not carry) is not established.

## Two replay details and whose death it is

**The counter's own action is dropped too.** `FUN_004d4b30` calls `FUN_005bc530`
(drops actions older than the counter) and then `FUN_004cbf60` on the same buffer
(0x4d4bce), which takes the head off as well. The counter is the tick of the
action the server has **played**: `FUN_005b7390` writes `+0x20e4` from the
player's action buffer `+0x10`, which `FUN_004cbf60` sets when it pops an action
to play (`FUN_004cc400`); a counter already sent goes as −1 (against `+0x20e8`).
Keeping the counter's action played it twice: on the live server a fast turn
(`--look-at …:25:0`, 2.5° a frame) left 4–9 actions unanswered and corrected the
look by degrees; dropping it, 1 action is unanswered and the replay lands on the
prediction exactly (`look: … ours X -> X`), 482 corrections at 0.01 m.

**`NEPlayerSpawned` and `NEPlayerDead` go to every client.** On a co-op server
with bots, every bot's death reset our body. `GameLogic::killPlayer` (Linux server
0x47a6b0) posts `PostRemoteEvent(6, 10, data, 8)` (0x47a91c): +0 the player's
`vtable+0xa8` (the id — bots 244..255 on the live server, ours 2), +4 a byte
from the third argument (purpose not established). `NEPlayerSpawned` carries the
id in its four bytes. Only events naming our player id are ours.

Still not found: once in a while a ghost header's tick reads as garbage
(4067420318) — not in a 563-packet capture, so rare; the packet it comes from is
not identified.

## "Pressed DONE quickly and did not appear": we took someone else's id

Reproduced with `--click-at <frame>:727:546` (it now reaches the spawn screen
too): 2 of 4 runs did not spawn. In both, the server listed two players of ours —
`OpenBF2` (id 2), the previous run's session not yet timed out, and `OpenBF2_0`
(id 0), this one, renamed because the name was taken. We found ourselves by the
name's tail, took id 2, and ignored `NEPlayerSpawned = 0` — which arrived right
after our `NESelectSpawnGroup`: the server had spawned us.

Our player id is our connection's: `ConnectAccept`'s connection id equalled the
player id in every one of seven runs (0, 2, 3), the renamed one included. After
the change both failing cases spawn.

While looking, `ServerGameLogic::uPlayingSpawning` (Linux 0x4ab5a0) turned out to
hold a third check the notes above missed: when the group is missing or
`group->vtable+0x58(isAI)` refuses, the server clears the player's group
(`setSpawnGroup(0)`, 0x4ab66a) and sends that player `PostRemoteEvent(6, 6)` —
`NESelectSpawnGroup` without data, "choose again" (0x4ab696). We do nothing with
it yet; it did not occur in these runs.

## A flag's group is never a run-time one

The maintainer's run on the co-op server: the right player id, DONE on the gas
station, `step: spawn point 197 (at 29 m …)` — and no spawn. The server had sent
26 groups: the level's (1..6, flags `101`) and run-time ones from 192 (`000` at
the map's centre while empty, or at a vehicle, like 194..197). The LAV's group 197
stood 29 m from the flag, the flag's own group 3 36 m, and the nearest won.

`SpawnManager::createDynamicSpawnGroup` (Linux 0x4ba5a0) numbers the groups it
makes at run time from 192 (`movl $0xc0`) to 255 (`cmpl $0x100`), the first free
one. A flag's group is the level's, so below 192: `nearestSpawnGroup` skips the
rest (`kDynamicSpawnGroupFirst`). Two runs after the change: group 3, spawned.

`CreateSpawnGroupEvent`'s fields, from its construction in
`SpawnManager::createSpawnGroupOnClients` (0x4b96e0): the group's number
(`+0xa4`), the network id (`+0x10`), the team (`vtable+0x38`), then three flags —
the first is `group->vtable+0x58(false)`, the same "can a human spawn here" the
spawn loop asks; the second `+0x9a` and the third `+0xa0`, purposes not
established — and the packed position.

Still not handled: when that check refuses later, the server clears the player's
group and sends `NESelectSpawnGroup` without data (0x4ab66a / 0x4ab696); our join
chain does not start again on it.
