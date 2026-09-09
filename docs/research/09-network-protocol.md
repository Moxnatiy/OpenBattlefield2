# BF2's network protocol: connecting

Taken apart from the Linux server (`dice::hfe::io::NetServer`) and verified
against a live server in a container. **Our client connects to an original
server and holds the connection.**

## The basic header — 12 bits

`readBasicHeader` / `writeBasicHeader`:

```
4 bits   packet type
8 bits   connection id (0 in the client before connecting)
```

Bits are packed low first — the same as in our `BitStream`.

The types from `NetServer::_update`'s dispatcher:

| Type | What it is |
|---|---|
| 1 | connection request |
| 2 | connection accepted |
| 3 | denied |
| 4 | receipt acknowledged |
| 5 | disconnect |
| 7 / 8 | ping: request and reply |
| 9 | info request (a stub in the dedicated server) |
| 15 | data |

## The connection request (type 1)

`handleConnectRequest` reads exactly this:

```
u32   0x1002              the protocol constant
u32   the game's version
1 bit PunkBuster on the client
u32   the reconnection token
32 bytes password
32 bytes mod directory
```

The refusals possible here: `0x16` banned, `0x1f` PunkBuster required, `0x24` a
different directory (the server adds its own to the reply), `0x11` wrong
password, `0x1e`/`0x02` no slots, `0x17`/`0x18` the client is too old or too
new.

## The version

`checkVersion` requires the first number to be exactly `0x1002` and the second
to match the server's version. The number itself is not in the data, so we
found it with a **binary search against the live server**: it answers "too old"
(0x17) or "too new" (0x18), and after 32 steps a single value is left:

```
0x150C5100
```

The bytes `15 0C 51 00` read as **1.5** and build **0x0C51 = 3153** — exactly
`bf2-linuxded-1.5.3153.0`. That is not a coincidence but a confirmation.

An important caution: UDP replies arrive with a delay, and the first "success"
turned out to be a stale reply to the previous request. The right way is a fresh
socket for each attempt and a double check.

## Accepted (type 2) and denied (type 3)

```
type 2:  u8 the assigned connection id, u32 the server's time, 1 bit PunkBuster
type 3:  u32 the reason, 1 bit "a directory follows", [32 bytes of directory, only for 0x24]
```

After the acceptance the client has to send **type 4** — only then does the
connection go into its working state (in `_update` state 1 -> 2).

## The extended header

Pings and data carry one more header (`writeExtendedHeader`), 44 bits:

```
6 bits   packet number (wrapping at 64)
6 bits   the number of the last one received
32 bits  a mask of which of the previous ones arrived
```

Then a ping request carries a flag and the server's time; it has to be returned
unchanged — the server computes the latency from the difference.

## What works now

```bash
openbf2 --connect 127.0.0.1:16567
```

```
connecting: 127.0.0.1:16567
  request sent: protocol 0x1002, version 0x150c5100
  ACCEPTED: connection 0, server time 4551784 ms, PunkBuster off
  acknowledgement sent
  over 30 seconds: pings 9 (all answered), data packets 9 (234 bytes), other 0
```

So the handshake is complete, and the server does not drop us for silence.

## Data packets (type 15)

`handleDataPacket` is only the reliability layer: it reads the extended header,
checks the number's freshness (`(seq - last) & 0x3f < 0x20`), acknowledges what
arrived and queues the packet. The content itself is parsed by the game.

Inside is an event stream. `GameEventManager::processReceivedPacket` reads:

```
1 bit    are there events
8 bits   the count
5 bits + 1 service bit
then:    each event — its type in N bits, then its own fields
```

N is the smallest number for which `(1<<N)-1` covers the size of the event
registry (`readGameEvent`). Against a live server it comes out as **7**.

Before the event stream the packet has another **17 bits** of stream framing —
not taken apart yet, but the offset is stable and verified on live packets.

## The first event: the challenge

`GameServer::onNewConnection` immediately creates a `ChallengeEvent` and sends
it to the client. `ChallengeEvent::serialize` writes:

```
N bits   the type (in ChallengeEvent::getType it is 1)
80 bits  the challenge string (ten bytes with a zero at the end)
8 bits   the length of the mod's name
then     the name itself
```

A decoded live packet:

```
0f 10 00 00 00 00 00 11 00 06 00 01 b7 3c ba b5 35 b1 33 3d 35 80 01 31 33 19
```

breaks down exactly like this: 12 bits of the basic header, 44 of the extended
one, 17 of framing, 15 of the event framing, 7 of the type (= 1), 80 of the
challenge, 8 of the length (= 3), 24 of the name (`bf2`) and 1 padding bit —
152 bits in all, that is all 19 bytes of payload with nothing left over.

Our client already reads this:

```
  challenge event: uqymfsofr, mod bf2
```

## Three streams in one packet

`ClientConnection::processReceivedPacket` hands the packet to three managers
**in a fixed order** — no identifiers, each simply bites off its own bits:

1. `PlayerActionManager` — the player's actions;
2. `GameEventManager` — the events;
3. `GhostManager` — the world's state.

That is exactly why the "17 bits of framing" from the server's side are its
player-action block. In our direction it is simpler: if there are no actions,
`PlayerActionManager` reads **exactly one bit** and stops. Otherwise — 4 bits of
count, 9 bits of tick, a sign and 31 bits of the base number, then the actions
themselves.

## The challenge reply

Event type 2 (`ChallengeResponseEvent`), the layout from `serialize`:

```
584 bits  the response block (73 bytes)
u32       ?
u32       the network version
1 bit     sign + 31 bits of the product number (0x423 in BF2)
```

Without an authenticator (`sv.internet 0`) `GameServer::challengeResponse`
checks **only the network version**. It comes from
`BuildNrUtil::getNetVersionNumber()`, and that returns `0x150C5100` — exactly
the number we had found earlier by binary search. Two independent roads met.

### The acknowledgement trap

At first the server sent the challenge again and again, even though the reply
was apparently right. The cause is not in the event but in the reliability: in
the extended header the acknowledgement mask was zero, so the server considered
its event unacknowledged and repeated it endlessly. With the mask `0xFFFFFFFF`
the challenge arrives **exactly once** — and that is the sign the reply was
accepted.

Now the client does this itself:

```
  challenge event: enmemivfp, mod bf2
  challenge reply sent
  over 30 seconds: pings 9 (all answered), data packets 1 (26 bytes)
  challenges received: 1 (the reply was accepted)
```

## The data packet: 72 bits of header

For a long time it would not add up: the server was failing the check at
`Game/Common/GhostManager.cpp:1658` "Failed to receive ghostmanager". Guesses
did not help, so the server was brought up **natively on x86** (a Linux box at
home, the same container but without emulation) and run under `gdb` — on Apple
Silicon ptrace does not work, so there were neither stacks nor breakpoints
there.

A stop at the entry of each of the three streams showed both the read position
and the bytes the server sees. In our packet the block type and the size lay
exactly **16 bits earlier** than the server looked for them. Those 16 bits were
found in the server's own packet too: on 26 bytes it held `0x0011` = 17, which
is exactly 26 − 9. So it is **the payload's length in bytes**, and the data
packet's header is 72 bits:

| field | bits |
|---|---|
| packet type | 4 |
| connection id | 8 |
| packet number | 6 |
| acknowledgement | 6 |
| acknowledgement mask | 32 |
| payload length (bytes) | 16 |

Then come the three streams in a fixed order — player actions, events, ghosts.
The order is set by `ClientConnection::ClientConnection`: that is how it calls
`addStreamManager` three times. Every stream begins with a "has data" bit, and
even when there is no data that bit has to be written — otherwise the next
stream reads someone else's.

## The event batch number

The events were being read but not executed. `GameEventManager` puts batches
into a tree by a 5-bit number and hands them to the game **only in sequence**:
if the number is not the one expected, the batch lies in the tree and nothing
happens. The count has to start at zero and grow by one with every batch — not
with the packet number. As soon as that was fixed,
`DataBlockManager::newDataBlock` worked the first time.

## ClientInfo

The block the engine reads in `ClientInfo::setFromDataBlock`:

| field | size |
|---|---|
| length + name | u16 + bytes |
| the name's hash | u32 |
| profile number | 1 sign bit + 31 bits |
| length + clan tag | u16 + bytes |
| length + authentication string | u16 + bytes |
| a flag | 1 bit |

The name's hash is `h = 0x1505`, then for every lower-cased character
`h = h * 0x21 ^ c`. On a server without ranking (`sv.ranked 0`) neither the hash
nor the authentication string is checked: `GameServer::handleClientInfo` takes
the short path straight away and assembles the final name as "tag + space +
name". Because of that an empty tag gives a name with a leading space — and that
is how it shows in the player list.

The block travels as event type 4 (`DataBlockEvent`): first the header (1 bit =
1, then u32 block type and u32 size), then the chunks (1 bit = 0, u8 length,
bytes). Block type 1 is ClientInfo (`GameServer::handleDataBlock`).

## A player on the original server

```
$ openbf2 --connect 192.168.100.100 --name OpenBF2
  ACCEPTED: connection 0, ...
  challenge event: ..., mod bf2
  challenge reply sent
  ClientInfo sent: name OpenBF2, 22 bytes

$ rcon admin.listPlayers
Id:  0 -  OpenBF2 is remote ip: 192.168.100.112:53234
```

## What the server sends after registration

Two data blocks arrive at once (`sendDataBlock`):

| type | contents |
|---:|---|
| 0 | the server's settings: the name "OpenBF2 reference" and a heap of numbers |
| 5 | the level's details: `dalian_plant`, mode `gpm_cq`, size 16 |

Then a packet with events: `CreatePlayerEvent` with our name,
`VoipSessionEvent`, `UnlockEvent` and two empty `StringManagerEvent`.

## The readiness step

Everything stalled here: there were no world objects and the ghost stream came
in empty. The cause is that `GameServer::isClientReady` compares the
connection's state against three (`state > 3`), and the state itself is moved by
`clientSendDatabaseComplete` and `clientLoadComplete`. Both sit in the jump table
of `GameServer::handleNetworkEvent`: number 2 is "the level is loaded", number 4
is "the player base was received".

The client raises them through `PostRemoteEvent` (type 11) — the general "raise
this event on your side" event:

| field | bits |
|---|---|
| category | 4 |
| event number | 32 |
| delay (float) | 32 |
| data length | 8 |

The category is visible in `GameServer::handleEvent`: it compares it against 6
and hands it to `handleNetworkEvent` (two there means HUD events). An attempt to
guess the category by trying 0..15 ended with the server quietly shutting down:
`PostRemoteEvent` raises any internal event, so trying them out on a live server
is unwise.

The moment category 6, event 2 was sent, the server poured out the world:

```
  data packet: 11 events, the first of type 35
  data packet: 12 events, the first of type 6     <- CreateObjectEvent
  data packet: 36 events, the first of type 6
  data packet: 14 events, the first of type 11
  data packet:  1 event,  the first of type 56    <- BeginRoundEvent
```

In the client that shows as a rise from one packet of 26 bytes to seven of 1358.

## Next

Take `CreateObjectEvent` apart to the end: the field sizes have been taken
(32, 16, 2, 1, 8, 1, 1, three position numbers, three bits, three rotation
numbers — exactly 256 in all), but the positions still come out implausible, so
the interpretation of the fields is not final. After that, the ghost stream
itself.

## How to investigate this quickly

The rig and the experiments come down to two commands:

```bash
tools/linuxded/serverctl.sh up        # bring the server up (it restarts itself)
tools/linuxded/capture.py --stage world --out tests/data/bf2-world.bin
```

`capture.py` drives the connection to the required stage (`connect`,
`challenge`, `player`, `world`) and records everything that arrived. After that
the parsing is checked without the rig and without the network:

```bash
tools/linuxded/capture.py --replay tests/data/bf2-world.bin
```

That is 0.03 seconds instead of a minute of a live session. And, more
importantly, the captured packets stay forever: every experiment becomes a
permanent sample rather than a one-off script.

The parser reports where exactly it stumbled. An event type is never greater
than 69, so anything above that means the previous event was read at the wrong
length — and the one after which it happened is printed beside it:

```
not parsed yet: {80: 1, 87: 1, 32: 2, 106: 1}
went astray after: {'StringManagerEvent': 1, 'CreateObjectEvent': 2}
```

That is, exactly those two events whose length is so far taken from an
assumption rather than from the code.

### Taking the binary apart — locally

`bitfields.py` no longer goes anywhere: the server's binary lies in the game's
directory, it does not have to be executed, and `objdump` on macOS takes an ELF
x86-64 apart without trouble. It used to be about a second per query over ssh
into a container with gdb — now it is 0.19 s right here. The symbol table is
read once and cached.

Together that gives this cycle: look at a function's shape (0.2 s), fix the
parsing, check against the captured packets (0.03 s). The rig is needed only
when something new has to be captured.

## The event table is made from the binary

```bash
tools/linuxded/gen_events.py > src/net/src/bf2_events.inc
```

Seven seconds — and we have all 63 events: the type number from `getType()`, the
field sizes from `deSerialize`. We do not write this by hand.

47 of the 62 events are simple, their fields lie consecutively, and by the table
they can be skipped by exactly the right number of bits. The other 15 are marked
`BF2_EVENT_BRANCHY`: there part of the fields sits behind a condition, and a
size table would lie. The generator says so honestly rather than emitting wrong
code — the parsing of those is written by hand (`skipEvent` in
`bf2_events.cpp`).

Being able to skip an event matters more than parsing it: a packet carries
events one after another, and stumbling on an unknown one turns everything after
it into rubbish.

## A test on real packets

`tests/test_bf2_events.cpp` reads packets captured from a live server
(`tests/data/bf2-world.bin`) and checks that the parse runs to the end:

- the type number is not greater than 69 (anything above is a shift);
- there are no fewer than forty objects with positions;
- the height is within the map's terrain, not 1e38.

The last catches a one-bit error: a shifted float immediately produces
impossible numbers.

## The client sees the world

```
$ openbf2 --connect 192.168.100.100 --name OpenBF2
  player:  OpenBF2 (id 0, team 2)
  object: template 5068, id 1859, position -108.1 153.9 -253.7
  object: template 5217, id 1871, position -179.2 153.9 -33.8
  over 30 seconds: data packets 9 (2007 bytes)
  events parsed: 85, of them world objects: 32
```

## Where the template number comes from

`ServerConnection::clientSendDatabase` walks the objects and makes for each a
`CreateObjectEvent(template number, network id, ..., position, rotation)`. It
takes the template number from `IObjectTemplate` (entry 0x88 in the method
table), and that is simply a counter:

```
esi = manager->[0x7c]      // the counter's current value
template->setId(esi)       // entry 0x80
manager->[0x7c]++
```

So **the number is the order in which the template was created**, not a hash of
its name. To match a number to a name, the same files have to be read in the
same order as the game does: both sides do it identically, which is why they
agree.

That the numbers are deterministic was verified in two separate sessions: all 27
objects got the same template numbers. The positions matched in 23 of 27: four
objects are moving ones.

The rotations arrive as ZXY Euler angles **in degrees** — exactly as in the
`.con` (`-90`, `180`, `4.3`). In `clientSendDatabase` a `getRotationZXY` call is
visible right there, and the three numbers after it also flip sign.

## Calibrating the numbers by positions

The name cannot be got from the number itself, because it is a counter. But the
positions match: we read the level ourselves and know what stands where, and the
server gives the numbers for those same places.

```bash
openbf2 --level dalian_plant --calibrate tests/data/bf2-world.bin
```

```
level dalian_plant: known objects 1333
objects from the server: 50, matched: 50
      47  barrel_blue
    3747  fence_corrugated_3x12m_broken_parts
    3751  fueltankwagon
    3970  CPNAME_DP_16_constructionsite_UAV
    5498  CPNAME_DP_16_powerplant_ART1
    5679  CPNAME_DP_16_powerplant
    5680  CPNAME_DP_16_constructionsite
    ...
```

At first 40 of 50 matched: the comparison table held only control points and
spawners. Three unrecognised numbers were found by searching the coordinates
through `StaticObjects.con` — they turned out to be a barrel, a fence and a
tanker. So the server sends **destructible statics** too, not only flags and
vehicles. As soon as the level's static objects were added, everything matched.

The tolerance is deliberately narrow — two metres. Both sides take the position
from the same data, so the match must be exact; a wider tolerance would start
inventing correspondences where there are none.

From this something notable follows: **everything the server sends we already
have in the level's data**. Flags, vehicles from spawners, destructible statics —
all of it reads from the same `.con`. So objects are missing on the map not
because they "come from the server": we have them, they are simply not all drawn
yet.

## The level comes from the server

`--connect` no longer needs `--level`: the server sends the level as a data
block of type 5 right after registration, and the client mounts exactly that.
The order is the same as in the original:

1. request, acknowledgement, challenge reply;
2. the `ClientInfo` block — the server creates the player;
3. the server sends the level block (`dalian_plant`, `gpm_cq`, 16);
4. the client mounts the level and **only then** says "the level is loaded";
5. the server sends the world.

At first we said "loaded" straight after `ClientInfo` — and the objects arrived
before the level, so there was nowhere to put them.

### Does the client read server.zip

It does, and that is verified in `BF2.exe` itself. In the level-mounting
function (`FUN_004f30d0`) one sees:

```
mount <level>/server.zip          — always
if (not a dedicated server):
    mount <level>/client.zip
```

The flag comes from the setting `GSDedicated`. So it is the opposite of what one
expects: **both sides mount server.zip**, and it is the dedicated server that
skips `client.zip`. And so it should be: `server.zip` holds
`StaticObjects.con`, `Init.con`, the collision and the AI — without them the
client has nothing to draw, because the server sends only networked objects (50
of them against 1333 in the level's files).

## What reaches the screen

The client now walks every received object down the same path the game does, and
says where exactly it is lost:

```
distinct templates: 25
  drawn                       3
  no geometry anywhere       18
  geometry in a child         4
```

- **4 with geometry in a child** are the control points. In the `.con` they have
  no mesh of their own: `ObjectTemplate.create ControlPoint ...` and then
  `ObjectTemplate.addTemplate flagpole`. The flag comes from the child template,
  and we take geometry only from the root — which is why it is not visible.
- **18 with no geometry anywhere** are vehicle spawners (`*_UAV`, `*_AT0` and
  the like). They are not supposed to be visible: a spawner only says which
  vehicle to issue (`setObjectTemplate 1 aircontroltower_chi`), and the vehicle
  itself arrives as a separate object.

So of what the server sends it is precisely the flags that are missing, and the
cause is one and specific: geometry in the tree may sit somewhere other than the
root.

## The flags appeared

There turned out to be two separate faults, and each on its own hid the flag.

**Geometry in a child.** Assembling an object took the mesh only from the root.
Now, when the root has none, the object is assembled from the children's meshes,
each with its own accumulated matrix. For a control point that is exactly the
flag from `addTemplate flagpole`.

**The loading order.** Control points in our server lived in a separate list and
never reached the client at all, even though the original sends them as ordinary
`CreateObjectEvent`s alongside vehicles. We added them — and they still did not
appear: `loadWorld` starts with a clean object list, and it was being called
**after** `setGameplay`, so it swept away the flags that had just been placed.
The order was fixed to the engine's: the level's statics first, then the game
logic.

```
control points that reached the client: 4
unique geometry: 167 (was 163), placed: 1279 (was 1275)
```

## Next

The ghost stream — the movement of objects and players.
