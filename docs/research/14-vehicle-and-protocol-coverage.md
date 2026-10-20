# Vehicle replication, control, and protocol coverage

This note compares the implementation at `48078865c89ab6c3a98af8ecb06a40349b640e34`
with additional BF2 network and vehicle-control findings. It is an
implementation aid, not a claim that the engine already drives vehicles or
implements local bot AI.

The server-side protocol facts below come from static analysis of the stock
1.5.3153 Linux dedicated-server build. A separate live-capture parser check
closed 98,503 ordinary vehicle component records across 15 captures. This
validates record boundaries and decoded values for those captures; it does not
prove every record variant, client rendering behavior, or gameplay semantics.
No installer, game asset, capture, or executable is included here.

## What OpenBattlefield2 already covers

The current tree already assembles vehicle meshes, creates vehicles from map
spawners, includes vehicle geometry in soldier collision, and draws networked
vehicles using tracked simple-object positions ([server spawn and collision
code](../../src/server/src/game_server.cpp), [world view](../../src/app/src/world_view.cpp)).
It parses enter/exit events and displays bots supplied by an original server.
The [live-server README examples](../../README.md#on-a-live-server) show those
remote bots' replicated poses, weapons, animations, and ragdolls.

The current local server does not simulate vehicle driving. The remote vehicle
view does not yet apply its streamed orientation or place a player at a seat
transform. The local server also has no bot decision or movement controller;
the bots shown in live-server screenshots are controlled by the original
server. Those are distinct gaps from vehicle asset assembly and remote-bot
rendering. See the [server TODO](../TODO-server-hud.md),
[vehicle assembly notes](../formats/vehicle-assembly.md), and
[network-event analysis](../functions/network-events.md).

## Vehicle descriptors and state

A vehicle is not just its stationary map spawner. The server creates a network
object for the vehicle root, then exposes each player-control object (PCO) in
the vehicle tree as a descriptor: the root PCO is the driver's seat, with
child PCOs for gunner and passenger seats. Descriptor IDs follow the PCO tree
in pre-order. A renderer can associate the created root's template with a
vehicle recipe, then use that root's replicated state for its world transform.
The spawner supplies creation/template context; it is not the moving hull.

Each descriptor's component payload is a concatenation of networkable groups,
without per-component tags or separators. Template child order determines the
group order. The first member of a group supplies the serialized bits, which
the receiver applies to the group's members. This is why repeated wheels,
rotors, or similar components do not necessarily produce one independent
state field apiece. A vehicle recipe must preserve template traversal and
grouping to decode the following bits correctly.

The root `SimpleObjectNetworkable` update begins with a 19-bit mask. In this
server build, the relevant bits are:

| Bit | State | Notes |
|---:|---|---|
| 0 | Orientation | Unit quaternion; four 14-bit values in `w, x, y, z` order, mapped to `[-1, 1]` and normalized. |
| 1 | Position | Compressed vector at precision `0.0005`, relative to the packet's compression origin. |
| 2 | Linear velocity | Physical-object state; emitted with full-precision local-control updates. |
| 3 | Angular velocity | Physical-object state; compressed-vector variant at precision `0.01` in ordinary ghost updates. |
| 4 | Hit points | Present when the object has the armor interface; range comes from its authored HP and wreck HP. |
| 10 | Two-bit value | Meaning is unresolved; preserve it without assigning semantics. |
| 11, 12, 17 | Active weapon per channel | Selection indices for the three PCO firing channels. |
| 16 | Ammunition status | Five-bit state on a PCO. |
| 18 | Ducking | One-bit PCO state. |

Other mask bits carry physical-object vectors and sleep/supply state. Their
presence depends on the object and its physical interface; a decoder should
not infer that every simple object has every field. The quaternion convention
matches row-vector matrices: after normalizing `(w, x, y, z)`, the object's
forward row is `(2(xz - wy), 2(yz + wx), 1 - 2(x² + y²))`. Converting that
orientation rather than retaining the spawner's rotation is necessary for a
moving or turning vehicle.

The 98,503-record check above included moving ground vehicles and aircraft.
Decoded parked-vehicle orientation agreed with its creation rotation, and a
driven tracked hull's decoded heading followed its travel bearing. These are
capture-specific checks, not a proof that all vehicle templates share one
component recipe or that the renderer's transform convention is already
correct.

## Inputs useful for vehicle and seat tests

The native control IDs used by an external player controller are:

| Control | ID | Effect |
|---|---:|---|
| `Use` | 10 | Toggle vehicle entry/exit; the server chooses the lowest free seat. |
| `PositionSelect1`–`PositionSelect8` | 23–30 | Request seat indices 0–7. |
| `Fire` | 8 | Primary firing channel. |
| `AltFire` | 31 | Secondary firing channel. |
| `FlareFire` | 47 | Third firing channel, commonly countermeasures. |

These are action-stream controls, not new GameEvent types. `EnterVehicleEvent`
and `ExitVehicleEvent` report occupancy changes; they do not encode a seat's
world transform. To render a passenger in the right place, resolve the
descriptor to its authored seat and transform that seat with the replicated
vehicle root.

## What the driving and bot evidence establishes

An independent network-controlled player agent has driven wheeled and tracked
vehicles on the stock server, occupied driver and gunner seats, and used
replicated vehicle state for feedback. Its navigation and steering are an
independent controller: routes come from a vehicle-specific collision grid,
steering uses a bounded pure-pursuit target, and stuck recovery applies reverse
and alternate-route commands. These are useful test-harness techniques, not
recovered retail physics or code for a server-native NPC.

Live control also qualified and flew UH-60, Mi-17, and Z-8 helicopters through
takeoff, transit, landing, and exit. Plane flight, attack hover, aircraft
weapon hits, and bailout/parachute behavior were not established by those
runs. A network-controlled player can demonstrate that input and state
replication work while the original server runs its own simulation; that does
not demonstrate that OpenBattlefield2 has implemented the same vehicle
physics, or that it has implemented NPC pathfinding and tactical decisions.

## Protocol comparison and an event-traversal gap

OpenBattlefield2 already documents the BF2 packet header, handshake, streams,
event layouts, and the original client's `SimpleObjectNetworkable` reader in
the [protocol format](../formats/network-protocol.md) and
[network-event analysis](../functions/network-events.md). The findings here
are not a separate wire dialect. The additional value is a complete bounded
event-body traversal audit and server-side vehicle state details.

At the compared revision, `src/net/src/bf2_events.cpp::skipEvent` handles
variable bodies for IDs 0, 4, 6, 11, 21, and 64, then returns `false` for the
other entries marked branchy in `bf2_events.inc`. For the 63 registered event
classes in IDs 1–69 (holes: 18, 33, 44, 52, 53, and 67), a native serializer
audit recovered 51 fixed bodies and 12 conditionally sized bodies. A bounded
parser traverses all 63 without interpreting every event's gameplay meaning.

The branchy table also marks six fixed-width events as branchy: IDs 2, 19, 20,
26, 34, and 50. Seven variable bodies remain beyond the six branches in
`skipEvent`: IDs 1, 15, 22, 24, 30, 32, and 49. The schema audit checks
lengths and branch selectors before consuming data, refuses native-invalid
array lengths, and preserves exact bit boundaries. Its synthetic tests cover
all starting bit alignments and truncations; they do not prove live server
acceptance or event effects.

The event ID 0 `StringManagerEvent` special case is outside the 63 registered
classes. Keep that distinction when comparing registry counts.

The same server-side audit distinguishes DataBlock types that are easy to
conflate: the server's type 5 is a `MapList` record (entry count, selected
index, then map, mode, and player-parameter entries); type 2 is `MapInfo` (mode,
level path, map name, player parameter, commander flag, challenge ordinal, and
two unresolved flags). The current tree's selected-level parser for type 5 is
a useful consumer of the selected row; the wire record itself is a map list.
In type 2, the two integer fields use sign-and-magnitude encoding. The five
trailing padding bits are not extra fields. The selected entry in type 5 can
identify which map to mount; its first word is not the challenge ordinal.
Existing event notes already describe the separate type 2 challenge field;
this distinction is included here because both records participate in level
admission.

## Evidence limits

Static server analysis establishes serialization and handler structure for
that build. Live captures establish only the records and control outcomes
actually observed. The external steering policy is not a measurement of
Refractor's internal controller. Desktop-client rendering and native local
bot AI still require their own source-backed analysis and tests.
