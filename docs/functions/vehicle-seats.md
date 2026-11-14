# Vehicle seats and the soldiers in them

## Which seat a player entered

`EnterVehicleEvent` names a network id. A vehicle is one descriptor per
`PlayerControlObject` of its template tree: `NetworkManager::analyseObject`
(OpenBF2 IO/Network/NetworkManager.cpp) walks `getAllPlayerControlObjects` — the
object, then each child subtree depth-first in tree order — and numbers the
control objects that have properties from the vehicle's own id upwards
(`requestedId = assigned + 1`). So seat *i* is the vehicle's id + *i*.
`game::vehicleSeats` lists them from the registry: the parts of class
`PlayerControlObject` with `setNetworkableInfo`. Live: a T-90 created as 1830 and
a bot entering 1831 (its cupola gunner); a Vodnik 1824 with seats 1824..1827.

`object_info <mod> --seats <vehicle>` prints them. Karkand's: the HMMWV 4 (driver,
cupola, codriver, rear), the T-90 2 (driver, cupola), the LAV-25 5 (driver and
four rear passengers, none with `seatInformation`).

## Where he sits

`PlayerControlObjectTemplate::seatInformation <parent> <position> <rotation>`
(Linux 0x539610) keeps the parent's name (+0x208) and a matrix at +0x210:
`setRotationZXY(−rotation)` and the position — the convention child templates'
`setRotation` use. `PlayerControlObject::enter` (Linux 0x53f070) makes the soldier
a child of the seat's parent object (PCO +0x408, found by that name) and gives
him that matrix (`setTransformation`, slot 0x1d8). `Soldier::updateTransformation`
(Linux 0x548370) keeps it so while he is in a vehicle (+0x398):
`rotateY(+0x3bc) × getSeatTransformation()` — his turn within the seat, then the
seat.

Ours (`WorldView::seatOf`): the vehicle's drawn place × the parent part's rest
transform in the template tree × the seat matrix. **Not ours:** the parent's live
rotation (a turret's, `RotationalBundleNetworkable`), the soldier's turn in the
seat (+0x3bc).

His pose is the seat's `seatAnimationSystem` (`3p_driver_A_animSys.inc`,
`3p_gunturret_A_animSys.inc`, …): the same format as a soldier's, a plain tree
whose `root` plays `still` (`3p_driver_A_static.baf`, `3p_vodnik_gunner_sit.baf`).
The weapon layer does not play in a seat.

A seat with no `seatInformation` (a tank's driver, a LAV's passengers) gives no
place; such a soldier is not drawn — **inferred**, the engine's own rule for
hiding him is not read.

## What the screen shows, and what is open

Live (Karkand co-op, `--watch-seated`): every bot in an open seat was drawn in it
in his seat pose — Vodnik driver, cupola gunner, rear passenger, the M1A2's
cupola gunner. **The Vodnik's cupola gunner stands about a metre too high**, his
legs above the roof. The placement is the engine's formula, so the difference is
elsewhere — the parent's rest transform in our tree, the clip's root, or where
our soldier mesh's origin is against his object's (we draw a soldier on foot at
his networked pivot less `coll-soldier-pivot-height`). Not measured: it wants a
frame of the original with a gunner in the same seat.

Also seen: a soldier in the bind pose high in the air near the vehicles — not
identified (a stationary weapon's seat, a parachute?).

## The registry ran every tweak twice

Both registry builders (the session's `buildRegistry`, `object_info`) ran every
`.con` and every `.tweak` on its own. A vehicle's `.con` ends with `include
<vehicle>.tweak`, so each tweak ran twice, and each of its `addTemplate`s added
the child twice: the HMMWV had its codriver and rear seat twice, the LAV its four
passengers. The builders now run the `.con`s first and only the tweaks no `.con`
included (`con::RecordingProvider` says what a run read).
