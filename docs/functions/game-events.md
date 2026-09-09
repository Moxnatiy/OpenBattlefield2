# The game-event registry

Source: `dice::hfe::*Event::getType()` in the 64-bit Linux server — every
class returns its own constant. The list was taken in bulk: the addresses
from `info functions ::getType`, then `x/2i` on each (`mov $N,%eax; ret`).

The type number travels in the packet in 7 bits: `GameEventManager::readGameEvent`
takes the smallest N for which `(1<<N)-1` covers the registry's size, and here
there are 69 types.

Several numbers have two classes each — those are events that do not travel over
the network but only notify the game inside the process (`DataBlockReadyEvent`,
`StringReceivedEvent`, `RadioMessageReceivedEvent`, `PostRemoteEvent`).

`StringManagerEvent` returns zero through `xor %eax,%eax`, so it was not visible
on the first pass — but it is exactly what arrives at the tail of the first
packets after registration.

| # | class |
|---:|---|
| 0 | `StringManagerEvent` |
| 1 | `ChallengeEvent`, `DataBlockReadyEvent` |
| 2 | `ChallengeResponseEvent` |
| 3 | `ConnectionTypeEvent` |
| 4 | `DataBlockEvent` |
| 5 | `CreatePlayerEvent` |
| 6 | `CreateObjectEvent` |
| 7 | `DestroyObjectEvent` |
| 8 | `DestroyPlayerEvent` |
| 9 | `EnterVehicleEvent` |
| 10 | `ExitVehicleEvent` |
| 11 | `PostRemoteEvent`, `StringReceivedEvent` |
| 12 | `ChangePlayerNameEvent` |
| 13 | `HandleDropEvent`, `RadioMessageReceivedEvent` |
| 14 | `HandlePickupEvent` |
| 15 | `StringBlockEvent` |
| 16 | `JoinSquadEvent` |
| 17 | `LeaveSquadEvent` |
| 19 | `CommanderEvent` |
| 20 | `RadioMessageEvent` |
| 21 | `KilledByEvent` |
| 22 | `ChangeSquadNameEvent` |
| 23 | `SetPrivateSquadEvent` |
| 24 | `IssueSquadOrderEvent` |
| 25 | `InviteEvent` |
| 26 | `RankEvent` |
| 27 | `SetAcceptOrderEvent` |
| 28 | `SetSquadLeaderEvent` |
| 29 | `SpottedEvent` |
| 30 | `ArtilleryEvent` |
| 31 | `CreateKitEvent` |
| 32 | `StickyProjectileEvent` |
| 34 | `AmbientEffectAreaEvent` |
| 35 | `VoipOnOffEvent` |
| 36 | `CommanderCamEvent` |
| 37 | `SupplyDropEvent` |
| 38 | `VoipPlayerMuteEvent` |
| 39 | `VoteEvent` |
| 40 | `ToggleFreeCameraEvent` |
| 41 | `MedalEvent` |
| 42 | `UnlockEvent` |
| 43 | `MissileInitEvent` |
| 45 | `UAVEvent` |
| 46 | `ContentCheckEvent` |
| 47 | `TargetDirectionEvent` |
| 48 | `EndOfRoundEvent` |
| 49 | `PythonCommandEvent` |
| 50 | `RequestEvent` |
| 51 | `DropVehicleEvent` |
| 54 | `VoipSessionEvent` |
| 55 | `KickBanEvent` |
| 56 | `BeginRoundEvent` |
| 57 | `CreateSpawnGroupEvent` |
| 58 | `RemoveSpawnGroupEvent` |
| 59 | `UpdateTriggerEvent` |
| 60 | `GrapplingHookContainerCreateEvent` |
| 61 | `GrapplingHookContainerUpdateEvent` |
| 62 | `GrapplingHookContainerDetachEvent` |
| 63 | `GrapplingHookCreateEvent` |
| 64 | `GrapplingHookUpdateEvent` |
| 65 | `PlayerTearGassedEvent` |
| 66 | `SetNightVisionEvent` |
| 68 | `VerifyPlayerTeamEvent` |
| 69 | `FixPlayerTeamEvent` |

## Field layouts

Taken in bulk: `tools/linuxded/bitfields.py <Class>::deSerialize`. The
instrument takes the function in the 64-bit server apart and writes out every
`BitStream::readBits` call together with the bit count — the third argument
travels in `%edx`, so a field's size is visible right in the code.

What has already been verified against a live server:

| event | fields |
|---|---|
| `StringManagerEvent` (0) | 1 bit; if zero, that is all |
| `ConnectionTypeEvent` (3) | 3 |
| `DataBlockEvent` (4) | 1 bit kind; header: u32 type, u32 size; chunk: u8 length + bytes |
| `CreatePlayerEvent` (5) | 3, 4, 1, 8, 16, 16, 1 bits and 32 bytes of the name |
| `CreateObjectEvent` (6) | 32, 16, 2, 1, 8, 1, 1 bits and six 32-bit numbers (position and rotation) |
| `DestroyPlayerEvent` (8) | 8 |
| `UnlockEvent` (42) | 2, 8, 4 |
| `VoipSessionEvent` (54) | 16 |
| `BeginRoundEvent` (56) | 32, 32 |
| `CreateSpawnGroupEvent` (57) | 8, 4, 1, 1, 1, 8, 8, 16 |

The first packet the server sends after registration parses completely:

```
CreatePlayerEvent  team 2, id 0, name ' OpenBF2'
VoipSessionEvent   session 13413
UnlockEvent        kind 1, player 0
StringManagerEvent empty
StringManagerEvent empty
```

The leading space in the name is not a parsing error: on a server without
ranking `GameServer::handleClientInfo` assembles it as "clan tag + space +
name", and our tag is empty.

## Conditional fields

A flat list of reads lies where fields sit behind a condition. Both of our
mistakes were exactly that, so the instrument was taught to show the shape:

```bash
tools/linuxded/bitfields.py --blocks CreateObjectEvent::deSerialize
```

```
block 0x4237d0   32, 16, 2, 1     -> 0x4238a4 (conditional)
block 0x423862   8
block 0x4238a4   1                -> 0x423910 (conditional)
block 0x4238d9   1                -> 0x423970 (conditional)
block 0x423910   32, 32, 32       -> 0x4238d9
block 0x423970   32, 32, 32       -> 0x423881
```

From this it is visible that `CreateObjectEvent` has two **mutually exclusive**
branches, and the jump's polarity has to be looked up in the code itself
(`cmpl $0x1; jne` — that is, the eight-bit branch is taken when the flag equals one):

```
32  template
16  network id
 2  a field
 1  a flag
     if 1: 8 bits, and that is all
     if 0: 1 bit -> [position 3x32], 1 bit -> [rotation 3x32]
```

Until this was accounted for, parsing went astray on the next event in the
packet. After the fix the whole world stream reads with not a single unknown
type: 50 objects with positions, 24 spawn points.

## Field layouts of every event

Taken in bulk: `tools/linuxded/bitfields.py <Class>::deSerialize` over the
whole registry — 63 events in ten seconds. The numbers are field sizes in
bits, in the order they are read.

A few events write more than they read (`CreatePlayerEvent` — two extra bits,
`CreateObjectEvent` — three between the position and the rotation): a
comparison with `::serialize` shows it, and on the wire it is the write that
matters.

| # | class | fields (bits) |
|---:|---|---|
| 0 | `StringManagerEvent` | 1, 6, string, 1, 1, 1, 8 |
| 1 | `ChallengeEvent` | 80, 8, string |
| 2 | `ChallengeResponseEvent` | 584, 32, 32, 1, 31, 31 |
| 3 | `ConnectionTypeEvent` | 3 |
| 4 | `DataBlockEvent` | 1, 32, 32, 8, string |
| 5 | `CreatePlayerEvent` | 3, 4, 1, 8, 16, 16, 1, 256 |
| 6 | `CreateObjectEvent` | 32, 16, 2, 1, 8, 1, 1, 32, 32, 32, 32, 32, 32 |
| 7 | `DestroyObjectEvent` | 16 |
| 8 | `DestroyPlayerEvent` | 8 |
| 9 | `EnterVehicleEvent` | 8, 16, 1 |
| 10 | `ExitVehicleEvent` | 8, 1 |
| 11 | `PostRemoteEvent` | 4, 32, 32, 8, string |
| 12 | `ChangePlayerNameEvent` | 8, 256 |
| 13 | `HandleDropEvent` | 8, 16, 16, 32, 32, 32 |
| 14 | `HandlePickupEvent` | 8, 16, 16 |
| 15 | `StringBlockEvent` | 1, 8, 8, string |
| 16 | `JoinSquadEvent` | 8, 8, 8 |
| 17 | `LeaveSquadEvent` | 8, 8, 8, 1 |
| 19 | `CommanderEvent` | 4, 8, 1, 15, 15 |
| 20 | `RadioMessageEvent` | 8, 8, 2, 8, 5, 3 |
| 21 | `KilledByEvent` | 8, 8, 1, 16, 32, 32, 8 |
| 22 | `ChangeSquadNameEvent` | 8, 8, 8, 8, string |
| 23 | `SetPrivateSquadEvent` | 8, 8, 1 |
| 24 | `IssueSquadOrderEvent` | 8, 8, 8, 1, 16, 8, 1, 32, 32, 32, 32, 32, 32, 32, 32, 32 |
| 25 | `InviteEvent` | 8, 8, 8, 1 |
| 26 | `RankEvent` | 2, 6, 32, 8 |
| 27 | `SetAcceptOrderEvent` | 8, 8, 1, 8, 8 |
| 29 | `SpottedEvent` | 16, 16, 32, 32, 8, 32, 32 |
| 30 | `ArtilleryEvent` | 2, 32, 32, 32, 16, 32 |
| 31 | `CreateKitEvent` | 32, 16, 32, 32, 32, 4, 4 |
| 32 | `StickyProjectileEvent` | 16, 16, 8, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 8, 8 |
| 34 | `AmbientEffectAreaEvent` | 16, 8, 16, 32, 32, 32 |
| 35 | `VoipOnOffEvent` | 8, 1 |
| 36 | `CommanderCamEvent` | 32, 32, 32 |
| 37 | `SupplyDropEvent` | 32, 32, 32 |
| 38 | `VoipPlayerMuteEvent` | 1, 8, 8 |
| 39 | `VoteEvent` | 8, 8, 8, 8, 8, 8, 8, 8, 8, 32 |
| 40 | `ToggleFreeCameraEvent` | — |
| 41 | `MedalEvent` | 32, 8, 4 |
| 42 | `UnlockEvent` | 2, 8, 4 |
| 43 | `MissileInitEvent` | 16, 8, 4, 1 |
| 45 | `UAVEvent` | 32, 32, 8, 1 |
| 46 | `ContentCheckEvent` | 128, 128, 128 |
| 47 | `TargetDirectionEvent` | 16, 8, 32, 32, 32, 32, 32, 32 |
| 48 | `EndOfRoundEvent` | 32, 32, 32 |
| 49 | `PythonCommandEvent` | 32, 8, 4, 32 |
| 50 | `RequestEvent` | 8, 8, 3 |
| 51 | `DropVehicleEvent` | 32, 32, 8, 1 |
| 54 | `VoipSessionEvent` | 16 |
| 55 | `KickBanEvent` | 8, 1, 8 |
| 56 | `BeginRoundEvent` | 32, 32 |
| 57 | `CreateSpawnGroupEvent` | 8, 4, 1, 1, 1, 8, 8, 16 |
| 58 | `RemoveSpawnGroupEvent` | 8, 16 |
| 59 | `UpdateTriggerEvent` | 32, 16, 32, 32, 32, 32, 32, 32, 8, 8 |
| 60 | `GrapplingHookContainerCreateEvent` | 8, 16, 8, 32 |
| 61 | `GrapplingHookContainerUpdateEvent` | 8, 8, 32, 32 |
| 62 | `GrapplingHookContainerDetachEvent` | 8, 32, 32, 32 |
| 63 | `GrapplingHookCreateEvent` | 16, 8, 32, 32, 32, 32, 32, 32, 32 |
| 64 | `GrapplingHookUpdateEvent` | 16, 8, 1 |
| 65 | `PlayerTearGassedEvent` | 8, 32, 32, 32 |
| 66 | `SetNightVisionEvent` | 8, 1 |
| 68 | `VerifyPlayerTeamEvent` | 8, 8, 8, 8 |
| 69 | `FixPlayerTeamEvent` | 8, 8, 8, 8 |

## The player-action stream

This is what the client sends input with
(`PlayerActionManager::processReceivedPacket`, taken via `--blocks`):

```
 1  are there actions  -> if 0, that is all
 4  how many actions
 9  a field
 1  sign + 31 bits     the base tick
 then, for every action:
    (1 bit "axis present" -> 15 bits of value) several times
    32 bits, 9 bits, 1 bit
```

## Network events

These are no longer game events but a separate dictionary — what the two sides
drive the connection itself with. The number travels in `PostRemoteEvent` with
category 6, and the jump table of `GameServer::handleNetworkEvent` sorts them
out like this:

| # | what it does |
|---:|---|
| 1 | a data block |
| 2 | the client has loaded the level |
| 4 | the client has received the player base |
| 6, 7, 8, 12 | actions on the connection itself |
| 11 | a string was received |
| 13 | a radio message |
| 17 | an object became visible to the connection |
| 18 | an event in Python (a number + a string) |
| 19 | send a data block |
| 20 | end-of-round acknowledgement |

There is no explicit "player spawn" here: the server spawns those who already
have the corresponding field set, in `ServerGameLogic::uPlayingSpawning`. What
the client asks to spawn with has not been found yet.
