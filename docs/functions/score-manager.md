# `ScoreManager`: where the client gets the tickets from

The ticket counts above the minimap are neither an event nor a field of the
soldier. They arrive in the **`ScoreManager`** state, together with the rest
of the score.

Source: `BF2.exe`, **0x5c9650** — the read of that state. That it really is
`ScoreManager` the binary says itself: two checks inside it hold the path
`Code\BF2\Game\GameLogic\ScoreSystem\ScoreManager.cpp`, lines 0x340
("Failed to read mask") and 0x3c6 ("read error").

## The layout

```
mask     32 bits            (FUN_006b6470(&mask, 0x20))
then, for every bit set — an 18-bit number (FUN_004f9c10(0x12))
```

The values are gathered into a **0xa4-byte** record (`+0x1c4` is the array,
index `+0x38`) and only then spread over the manager's fields. Two of the
thirty-two bits carry the teams' tickets:

| bit | what it reads | where it goes |
|---|---|---|
| `0x400` | tickets (18 bits), **a flag (1 bit)**, ticket state (18 bits) | team 1 |
| `0x400000` | the same | team 2 |

The order inside the block is exactly that: first the 18-bit number, then a
single bit, then another 18 bits. After that the client calls

```
0x5c7590  setTickets(team, number)     — clamps to [0, 9999]
0x5c75f0  the flag
0x5c7620  setTicketState(...)          — sends event 0x47 on a change
```

The remaining thirty bits are other score numbers; they are read the same
way, 18 bits each, and what they mean has not been worked out yet.

## Where this sits in the team record

`0x4dbb60` — `getTickets(team)`: takes the team from the manager
(`[0x9c5da0]->vtbl[0x10]`) and returns **`+0x84`**. The HUD uses the same
record at 0x4646b0:

* `+0x84` — the tickets, from which `FriendlyTicketsString` is made;
* `+0x8c` — the ticket state, on which the game plays `lowOnTickets`;
* `+0x90` — a byte the HUD puts into its own field 0x88 (next to
  `FriendlyTicketBleed` 0x8c — **whether it is the same one is not
  established**).

## Read whole, and on screen (`--connect`)

The blocker below ("the walk over ghost records goes astray") is gone, and the
record is now read whole (`obf2/net/score_state.h`, `readScoreState`), from
OpenBF2's `ScoreManager::setNetUpdate`:

* **the id is fixed**: `Game::registerStaticNetworkables` registers the score
  manager as networkable **0x0201** on both sides, so its records are told by id —
  no create event, no guessing by mask;
* **the numbers are a sign and a magnitude** — 18 bits for the score fields, 3 for
  the round's winner and victory type (`readSignedMagnitude`), not plain 18-bit
  numbers as the first note above had it;
* **the order is the writer's, not the bits'**: per team `0x1, 0x2, 0x1000000`
  (two fields), `0x4000000, 0x10000000, 0x4, 0x8, 0x10` (two fields), `0x20 …
  0x200`, `0x400` (tickets, bleed bit, ticket state), `0x800` (rounds won); team 2
  the same on its own bits; then `0x40000000` the winner and `0x80000000` the
  victory type with both teams' `+0x38`. The table is in the header.

The fields `TeamScore +0x04..+0x40` are read and kept, their purpose **not
established** (OpenBF2: `m_block[16]` "[UNRESOLVED]").

**`+0x90` → HUD `0x88` is settled**: it is the gate of a `HudFloatBlinker` whose
output is `FriendlyTicketBleed` (+0x8c), period 1, smooth — and `0x90` / `0x94`
the same for the enemy (`HudInformationLayer` registration, OpenBF2). While the
team bleeds the variable runs a 0→1→0 triangle each second; when it stops, the
stroke finishes at 1 (`obf2/hud/blinker.h`). `updateTickets` (PE 0x4646b0) is
`hud::Manager::updateTickets`; the `lowOnTickets` sound is logged, there is no
sound.

Live, `strike_at_karkand` co-op: 7 score records, **7 read to exactly their
length**; tickets 1947 / 2073 with team 2 bleeding, rounds won 2 / 2; the frame
shows 2074 (ours, team 2) and 1947 above the minimap. Not compared against the
original on the same server (it still does not connect there).

## What was missing (history)

We fill `FriendlyTicketsString`/`EnemyTicketsString` **only from our own
hosted server**. On a real server nobody fills them, so there are two empty
plates above the minimap where the original has numbers.

What is left to do: find this state in the ghost stream (the `ScoreManager`
record among those we currently skip by length) and read the two blocks out
of it. The measure: with `--connect` the numbers match what the original
shows on the same server.

## It is not a separate packet but an ordinary ghost

Who calls 0x5c9650 is visible from the virtual method table: the address
sits in `BF2.exe` at **0x8eaa40**, that is the **12th entry** of the table
starting at 0x8eaa10. Two places put that table into the object's `+4`
field — the constructor 0x5c92d0 and the destructor 0x5c9600, both from the
same 0x5c9xxx family as `setTickets` 0x5c7590. So 0x8eaa10 is the score
manager's **`Networkable` interface**, not a separate packet-parsing table.

The Linux server names these methods outright:
`dice::hfe::world::ScoreManager::setNetUpdate(io::BitStream&, float,
bool, io::BaseLineData*, int)` and its pair `getNetUpdate`. So 0x5c9650 is
`ScoreManager::setNetUpdate`.

The conclusion that settles "where to look": **the score arrives in the
ghost stream**, in a record shaped exactly like a soldier's or a vehicle's
state — through `dice::hfe::GhostManager::readData(io::BitStream&)`. There
is no separate score packet, and scanning the stream over bit offsets makes
no sense.

So the tickets on `--connect` are **blocked by the very same debt** as
another player's position: while our walk over ghost records goes astray,
the turn never comes to the score manager's `setNetUpdate`. It is the same
row in the debt table ("ghost records of other players' soldiers"), not a
separate task.

## An attempt to find this state in the stream — and why it does not count

Traffic from a real server was captured (`--record`, 76 packets) and a
search run over it: at every bit offset read a 32-bit mask, then the fields
in that same order, and check whether bits `0x400` and `0x400000` give
plausible numbers. The instrument is `tools/score_scan.py`.

A stable pair turned up: **bit 282, tickets 143 and 89**, identical in
packets 49–54 with the identical mask `0xeffffe0b`.

**That is not proof, and we do not take such things.** The check failed
twice:

* an attempt to read a ghost record header before that spot (2 bits kind,
  16 id, 1 baseline, 11 length) gave kind 0 and a length of 1068 bits, while
  the parse itself eats 580 — it does not add up;
* our own walk over ghost records on those same packets (all 128 bytes)
  finds only two records with kind 3 and nonsensical ids 57343 and 511 — so
  on these packets it goes astray by itself.

So the recurring 143/89 may simply be the identical contents of repeated
packets rather than tickets.

**What would settle it.** A side-by-side comparison with the original on the
same server: if it shows the same numbers at the same moment, it is proven.
The attempt failed for a different reason: the original client does not
connect to 192.168.100.100 at all — "You have failed to connect", right on
the main menu, with the `defaultPlayer` profile. So for the comparison the
server wants something our profile does not have.

## A second attempt: our own server with a known number — also does not count

To avoid comparing against someone else's server, we brought up the
**original dedicated server** from the game itself
(`bf2_w32ded.exe +modPath mods/bf2 +dedicated 1`) on Dalian_plant. There the
number is known in advance:
`gameLogic.setDefaultNumberOfTicketsEx 16 1 100` and `sv.ticketRatio 100`,
so a fresh round starts at **100/100**. Had the search found a stable
100/100 pair in the captured traffic, the layout and the location would have
been proven at once.

There is no 100/100 pair in the capture. But that is **not a refutation of
the layout**: there is nothing in the capture to read it from. Over 30
seconds of our client being connected the server sent 22 packets, 12 of them
with data (2150 bytes), and **not a single ghost record** ("packets with a
ghost stream: 0"). So the server never began handing us the world, rather
than "handed the score over somewhere else".

That agrees with the conclusion above: the score travels as a ghost, and
there are no ghosts in the capture. The measure stays the same, but it can
only be carried out once the walk over ghost records works.
