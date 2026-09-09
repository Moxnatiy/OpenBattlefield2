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
terrain is a constant difference, not a drift: it looks as though a soldier's
networked position is the object's origin rather than his feet. Where exactly
that metre comes from is **not established yet**, so the placeholder is drawn
where it arrived.
