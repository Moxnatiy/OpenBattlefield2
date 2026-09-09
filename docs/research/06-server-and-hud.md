# Server, client and the game's interface

## A single-player game is a client and a server too

The key thing, without which the port makes no sense: **in BF2 the server
owns the world even offline**. The engine brings up a local server and
connects to it. So the server side is not a "multiplayer add-on" that can be
deferred but the core: the client only shows what it was sent.

The same is visible in the source tree recovered from `BF2_r.exe`:
`BF2/Game/GameServer/`, `BF2/IO/Network/ProtocolLayer/{NetServer,NetClient}.cpp`.

## How it is done here

The channel is abstract (`net::Connection`): for a single-player game it is
an in-memory loop, for the network it will be UDP. **The packet format is the
same** — otherwise a single-player game would exercise code other than the
one that later runs in multiplayer.

```
client -> server : ConnectionRequest      protocol version + player name
server -> client : ConnectionAccept       player id, level, mode
                   or ConnectionDenied    reason code
client -> server : ConnectionAcknowledge
server -> client : Data                   object spawns and updates
```

World state goes out **only after the acknowledgement** — otherwise the
client would receive objects before it learned its own id and the level's
name. There is a separate test for that.

Positions travel as the compressed vector from
`docs/research/04-netcode.md`, angles as 16 bits per axis.

## Checking: the level is drawn from the network

```bash
./build/macos-arm64-debug/src/app/openbf2 --level Dalian_plant --hosted
```

```
local server: InWorld, players 1, packets 77/2
client received objects: 907 of 907
unique geometry: 132, placed: 906
```

With `--hosted` the placement comes **not from the level's file** but from
the packets that arrived from the server. The picture matches a direct load —
so the protocol loses and distorts nothing.

A real bug turned up along the way: template names were being cut at 32
characters, and three objects (`fence_corrugated_3x12m_broken_parts` among
them) arrived with no geometry. The length was raised to 64, and the batch of
spawns per packet reduced from 24 to 12 so the packet stays within 1200
bytes.

## Input and simulation

The server runs a **fixed step** 30 times a second, as the original does. A
frame may last however long — the accumulator splits it into equal ticks, so
movement does not depend on how loaded the machine is. For a long pause there
is a ceiling of eight ticks per frame: better to fall behind than to wind up
hundreds.

```
client -> server : Data (subtype 1) — input: axes, look angles, buttons, id
server           : applies the input, moves the soldier, sends updates
server -> client : Data (subtype 0) — positions of moving objects
```

Statics are sent once on spawn, moving things every tick. Without that split
907 buildings would go over the network thirty times a second.

The input id is 16-bit and wraps, so stale packets are discarded with the
wrap taken into account — otherwise after tick 65535 the player would be
stuck for good.

The client **smooths** other players' positions between packets: the server
sends 30 times a second and we draw more often. Client-side prediction of
one's own movement does not exist yet, so on a high ping the controls will
feel sluggish.

### Covered by tests

- a second of walking at speed 4 gives roughly four units of distance;
- running is noticeably faster than walking;
- **moving diagonally is not faster than moving straight** — the classic bug
  if you forget to normalise the direction;
- the same second at 30 and at 120 frames gives the same distance.

### First-person play

```bash
./build/macos-arm64-debug/src/app/openbf2 --level Dalian_plant --hosted
```

WASD moves, the mouse looks, Shift runs. The camera stands where the
**server** put it: input goes over the network, the simulation runs on the
server, and the client only draws what it was sent. In a single-player game
the other side of the loop is a local server — exactly as in the original.

Culling by the ground plane removes 775 instances out of 958.

## The interface: Flash only in the main menu

Checking `Menu_client.zip` changed the picture we had:

| Directory | Files | What it is |
|---|---:|---|
| `HUD/` | 1145 | **a declarative GUI in `.con`** |
| `External/FlashMenu/` | 1093 | Flash: 5 `.swf` plus PNG assets |
| `Nametag/`, `Atlas/`, `Console/` | 27 | odds and ends |

So **the whole in-game interface is `.con`, not Flash**: the HUD, the
scoreboard, the spawn screen, the level list, the server details. Flash is
left exclusively in the main menu, before entering the game.

A node is declared like this:

```
hudBuilder.createPictureNode IngameHud WarningIcon 701 292 32 32
hudBuilder.setPictureNodeTexture Ingame/GeneralIcons/.../icon.tga
hudBuilder.setNodeShowVariable WarningIconShow
hudBuilder.setNodeInTime 0.2
```

After that every `set*` applies to the **last node created** — the same
principle as in `ObjectTemplate`.

Node types: `PictureNode` (2375 creations), `TextNode` (1025),
`ButtonNode` (504), `SplitNode` (825), `BarNode`, `ObjectMarkerNode`.
Buttons have `setButtonNodeConCmd` — **the interface drives the game with
console commands**, that is through the very console that reads `.con`.

`obf2::hud` parses this into a node tree: 1615 nodes in 100+ groups
(`IngameHud` 60, `Scoreboard` 38, `MapFilterKits` 32...). 1574 commands still
have no handler — that is the next batch of work, and it shows in the
counter.

```bash
./build/macos-arm64-debug/tools/hud_dump/hud_dump "Game Files/mods/bf2"
./build/macos-arm64-debug/tools/hud_dump/hud_dump "Game Files/mods/bf2" Scoreboard
```

## What this means for the plans

The Flash decision stands, but **its urgency dropped**: everything shown
during play is drawn by our own interface out of `.con`. Flash is needed only
for the main menu. (The bet on gameswf later gave way to Ruffle — see
`docs/research/10-gameswf-on-arm64.md` and
`docs/research/11-ruffle-menu.md`.)
