# BF2's network protocol — what is confirmed

Only what has been **verified against a live server** and works. The
chronicle of the search and the wrong guesses is in
`docs/research/09-network-protocol.md`.

Sources: the 1.5.3153 Linux server's symbols, the debug build `BF2_r.exe`,
measurements on the rig (`tools/linuxded/`).

## The packet

Bits are packed low first.

### The header, 72 bits

| field | bits |
|---|---|
| packet type | 4 |
| connection id | 8 |
| packet number | 6 |
| acknowledgement | 6 |
| acknowledgement mask | 32 |
| payload length, bytes | 16 |

The packet number grows with **every** packet sent, ping replies included. A
packet with the same number as the previous one accepted is discarded by the
server as a duplicate.

### Packet types

| # | what it is |
|---:|---|
| 1 | connection request |
| 2 | accepted |
| 3 | denied |
| 4 | acknowledgement of acceptance |
| 5 | disconnect |
| 7 | ping request |
| 8 | ping reply |
| 9 | server info request |
| 15 | data |

### Three streams in a data packet

They come one after another in a fixed order. Each begins with a "has data"
bit, and even an empty stream has to write that bit.

1. **player actions** — 1 bit; if 1, then 4 bits of count, 9 bits, 1 sign bit
   + 31 bits of the base tick, and the actions themselves;
2. **events** — 1 bit; if 1, then 8 bits of count, 5 bits of batch number,
   1 bit, and the events themselves;
3. **ghosts** — 1 bit; if 1, then 32 bits of time (divided by 30), 8 bits of
   record count, 1 bit "there is a controlled-object state".

The event batch number has to start at zero and grow by one with every batch:
the server hands them to the game only in sequence.

## Events

An event's type is 7 bits. The full table of 69 types and each one's field
layout is in `docs/functions/game-events.md`.

## The handshake

Verified end to end: after it the server sends a ghost stream.

```
1. connection request (type 1)
      u32 0x1002, u32 version 0x150C5100, 1 bit PunkBuster,
      u32 token, 32 bytes of password, 32 bytes of mod directory
2. server: accepted (type 2) — connection id, time, PunkBuster
3. client: acknowledgement (type 4)
4. server: a challenge event (type 1 in the event stream)
5. client: the challenge reply (event type 2)
6. client: ClientInfo as a data block (block type 1)
7. server: the level block (block type 5), the player base, world objects
8. client: NELoadComplete
9. client: ContentCheckEvent — three hashes
10. client: NEDatabaseComplete
11. client: NESelectTeam, NESelectKit, NESelectSpawnGroup
12. server: the ghost stream
```

### ClientInfo (data block, type 1)

| field | size |
|---|---|
| length + name | u16 + bytes |
| the name's hash | u32 |
| profile number | 1 sign bit + 31 bits |
| length + clan tag | u16 + bytes |
| length + authentication string | u16 + bytes |
| a flag | 1 bit |

The name's hash: `h = 0x1505`, then for every lower-cased character
`h = h * 0x21 ^ c`. On a server without ranking it is not checked.

The player's final name is assembled by the server as "tag + space + name".

### The level block (block type 5)

| field | size |
|---|---|
| challenge number | 1 sign bit + 31 bits |
| length + level name | u16 + bytes |
| length + game mode | u16 + bytes |
| size | u16 |

The first field is read as "a sign plus 31 bits", not as an ordinary `u32`
(`MapInfo::setFromDataBlock`), so the bytes `01 00 00 00` mean zero.

**But it is not the challenge number.** Verified by experiment: the block says
0, while the content check only passes with the number 5. Where the client
gets the real number has not been found yet.

### ContentCheckEvent (event type 46)

Three 128-bit hashes, in this order:

1. **misc** — an MD5 over the mod's `.con` files
   (`ChecksumContext::runMiscChecksum`). Both sides compute it themselves.

   The shape of the computation: `MD5Init`, then a loop — for every name in a
   list the file is opened through `fileManager` and fed to `MD5Update` — and
   `MD5Final` at the end. Beside it sits a large 16-kilobyte local buffer: the
   files are read in chunks.

   The list itself is not literal in the code: the names are taken from
   memory, and it is filled somewhere earlier. Where exactly has not been
   found yet. The debug build prints "dep: hashing misc con files" and then
   "dep: checksumming file <name>" for each one, so the list can be checked
   off if the client is ever run.

   For now the hash is passed with the `--misc-hash` flag and goes stale after
   every server restart;
2. **archives** — a line from `mods/bf2/std_archive.md5`;
3. **the level** — a line from
   `mods/<mod>/levels/<level>/archive.md5`.

The line number in both files is the "challenge number". It changes **with
every round**, not only between server restarts: the server picks a new one at
every level load. The source for the client has not been found yet, so the
probe brute-forces it (there are exactly ten options — as many as there are
lines in the fingerprint files).

Because of this the rig has to be kept on a single round, otherwise the number
changes in the middle of an experiment:

```
sv.notEnoughPlayersRestartDelay 36000
```

The files' format: `number md5` for the archives, `name number md5` for the
level.

The server considers the check only when the connection's state is **greater
than one**, that is after `NELoadComplete`. Otherwise it silently ignores it.

### Network events

They travel in `PostRemoteEvent` (event type 11): 4 bits of category, 32 bits
of number, 32 bits of delay (float), 8 bits of data length, then the bytes.
The network events' category is **6**. The ones that carry a value expect a
32-bit number in the data.

The full table of numbers is in `docs/functions/network-events.md`.

Important: network events execute on the **next tick**, not in place. The
content check, on the contrary, executes immediately. So they cannot be put
into a single packet — the order would break.

## The ghost stream

The stream's header (verified, reads correctly):

| field | bits |
|---|---|
| has data | 1 |
| time (divided by 30) | 32 |
| record count | 8 |
| has a controlled-object state | 1 |

The time grows evenly by about 8 units between packets — those are ticks of
1/30 of a second. A packet holds anywhere from one to a couple of dozen
records.

### A record

| field | bits |
|---|---|
| kind | 2 |
| network id | 16 |

Then, by kind (`GhostManager::readData`):

| kind | what it is |
|---:|---|
| 0 | nothing more is read |
| 1 | a state update: 1 bit, **11 bits of content length**, the content |
| 2 | the engine treats this as a stream error and stops parsing |
| 3 | the object disappears (the engine looks it up in `NetworkManager`) |

The key thing here is the **length inside the record**: thanks to it a record
can be skipped without understanding its content. That is exactly what the
engine does when it does not know the object.

The engine computes the length field's width on the fly, and on the wire it is
exactly eleven bits. Verified by trial on a sample: with eleven all 132
packets parse to the last byte, with any other width not one does.

The network id matches the one that came in `CreateObjectEvent`. In the sample
there are 306 records for 41 objects, and two of them (1842, 1849) are updated
almost every packet — those are the moving ones.

When the header says "there is a controlled-object state", it comes before the
records; its layout has not been worked out yet, so we skip such packets.

A 172-packet sample with ghosts: `tests/data/bf2-ghosts.bin`, taken with

```bash
tools/linuxded/capture.py --stage spawn --misc-hash <hash> --out <file>
```

## The connection's state

`0 -> 1 -> 3 -> 4`. The one is set by `clientSendPlayerDatabase`, the three by
`NELoadComplete`, the four by `clientSendDatabaseComplete`. The last only
works when the client has `contentValid` set, otherwise the server queues it
for disconnection with reason 27.

The ghost stream goes only to those whose state is greater than three
(`GameServer::isClientReady`).


## About debug messages

The Linux server is a release build: of the whole set only the string
" for using modified data" survives in it. The switches `GSDebugNetwork`,
`GSDebugGhostManager`, `GSDebugBitStream` are there as settings, but the
messages themselves are not.

`BF2_r.exe`, on the other hand, is a debug build, and it is the best source:
448 source files against 163 and a full set of messages with the `dep:`
prefix. Several of them have already yielded answers:

- `in level fingerprint map` — the content check's third hash is the level's
  fingerprint;
- `dep: hashing misc con files` — the first hash is computed over `.con`;
- `dep: kicking client N (name) for using modified data` — this one the Linux
  server prints too, so the log is always worth looking at.
