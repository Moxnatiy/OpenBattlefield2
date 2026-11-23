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

## Selecting another seat

Retail `Settings/Controls.con` binds F1–F8 to `c_PIPositionSelect1` through
`c_PIPositionSelect8`. BF2.exe registers those inputs as 23–30 at
0x6909e9–0x690af2. The table at 0x8e93c8 admits them to
`PlayerAction::set` (0x5bc890), which subtracts 8 at 0x5bca41. The resulting
bits 15–22 go in the existing 32-bit button mask at 0x5bfb61–0x5bfb6f.
OpenBF2 queues a press for one action tick while seated. The server decides
whether to move the player and its enter and exit events update both rosters.

On FuShe Pass Co-op on an original 1.50 server, Windows held the FAAV driver
seat (1828) while macOS switched from the rear passenger seat (1829) to
the front gun (1830) with F3, then back with F2. Both captures carried the
exit and new enter assignments for each switch. The server left both
assignments in place when macOS requested the occupied driver seat.
After Windows dismounted, F1 moved macOS to 1828 and both clients saw
the new driver.

## Returning to infantry

The seat roster uses EnterVehicle (type 9) as the authoritative control
assignment. Assigning a new controlled object releases the player's previous
assignment. ExitVehicle (type 10) releases the exiting player's assignment.

In a two-client run on a hosted FuShe Pass Co-op server, Windows controlled
the FAAV driver at object 1828
and macOS controlled its rear passenger at object 1829. The driver received
the passenger's enter and exit events. The passenger's capture at tick 281
carried ExitVehicle and the controlled state of the original infantry
soldier, object 1793, in the same packet. The server's action counter was
206 and the soldier's pivot was (132.969, 78.613, 166.042), beside the FAAV.

The session applies that controlled state after processing the seat events.
While waiting for an infantry position, the client holds infantry prediction.
The controlled state provides the exit position and seeds reconciliation
before the client resumes infantry input handling.

## FAAV turret aim

An FAAV gun is controlled by its seat's descriptor. For the front gun
(group 2), the stream contains the four-bit player-control mask first
(BF2.exe 0x635b40), followed by three RotationalBundle masks and their
fields (0x6322e0), in the order in
`Vehicles/Land/JEEP_FAAV/JEEP_FAAV.con`. These bundles are
`JEEP_FAAV_Front_gunpos`, `JEEP_FAAV_Front_gunbase` and
`JEEP_FAAV_Front_gunmount`. The rear group has the cupola and gunmount
in that order.

The observer's ghost record encodes rotations in 12 bits. The gunner's
controlled state uses 16 bits, including on packets with the controlled
baseline's delta flag set, followed by the 10-bit Player::getStateSync
length. The decoder validates both descriptor boundaries and retains each
previous angle when a ghost delta omits it. On FuShe Pass the front mount
reported yaw in the first component of gunpos and pitch in the second
component of gunmount. Their observed limits agree with
`Vehicles/Land/JEEP_FAAV/JEEP_FAAV.tweak`.

The shared renderer separates the moving bundled geometry and applies
the replicated rotation around each joint's rest transform. Child parts
follow their parent joints. Other vehicle descriptors still need their
networkable order measured before enabling turret aim for them.

## FAAV firing and ammo feedback

The front M249 and rear M2HB have unlimited belts (`ammo.magSize -1` in the
weapon templates). The controlled gunner's `Player::getStateSync` uses slot 0
for the gun. Its camera, flags, fire rate, round count and magazine count use
the same widths as the soldier's. The gun's eight-bit heat follows the count
fields. A front-gun capture on FuShe Pass carried an additional active slot
without a heat byte, for 124 bits in the player payload with neither optional
hit or camera field present. During the test bursts the server reported
rounds −1 and one loaded belt. The HUD shows the unlimited-ammo icon and the
server's live heat meter.

The gunner and the driver both receive the gunner's firing transitions in
`PlayerState` (mask 0x40). The client uses those ticks, the weapon
template's firing interval and the replicated turret pose for the muzzle
flash, firing sound and visible impacts on the map. The packets captured
during FAAV bursts carry no individual bullet impact coordinates. The
client's impacts come from a line cast against the local terrain and map
collision. The retail server resolves damage independently.

## M1A2 turret and guns

The driver controls the main turret on the tank's root descriptor. The yaw and
barrel pitch bundles immediately follow the complete `SimpleObjectNetworkable`
state, which includes the ranged five-bit field at mask 0x10000. The observer
gets 12-bit rotations and the driver gets 16-bit rotations. The cupola is the
next seat ID and uses two bundles after its player-control mask. Its mesh
follows both the driver's turret and its own yaw and pitch. The driver uses the
controlled packet's compression reference as the hull position. On the retail
FuShe Pass server it matched the observer's hull position after firing moved
the tank away from its baseline.

The driver's controlled `Player::getStateSync` payload has three present slots.
The cannon slot has one loaded round and starts with 40 magazines including
the loaded one. Firing changes the chamber count to zero. The next loaded shell
reduces the magazine count. The coaxial slot has unlimited rounds and a heat
byte. The third slot carries the smoke launcher. The cupola uses the same
unlimited-ammo and heat layout as the FAAV's M2HB.

The primary and alternate firing states carry the cannon and coaxial trigger
respectively. Retail sends a trigger transition for empty cannon presses too.
Both clients use the cannon's firing and reload cycle to suppress repeat shell
effects during reload. The cupola uses primary firing from its own occupant.

The captured firing packets carried trigger and aim instead of individual
impact locations. The client sweeps the cannon shell along a ballistic path
using the retail template's velocity and gravity modifier. It plays
`e_exp_medium` at the intersection with the terrain or map collision. Coaxial
and cupola bullet effects use the same line cast as the FAAV. The retail server
resolves damage.

## T-90 turret and guns

The T-90 uses the same tank root decoder for turret yaw and barrel pitch.
The full T-90 ghost in the original client's Karkand capture
(`tests/data/bf2-karkand-lives.bin`, object 1830) reads both joints at their
rest angles. The cupola's two networked joints follow the main turret.

The T-90 uses the shared cannon chamber and reserve display, coaxial heat
display and cupola heat display. The cannon's `T90GunProjectile` template
supplies the shell velocity and gravity modifier. The coaxial MG's effect
is attached at the weapon origin. The cupola KORD uses the muzzle position
and rate of fire from its own template.

The stock Gulf of Oman and Strike at Karkand Co-op layers can spawn
`RUTNK_T90`. FuShe Pass Co-op uses `TNK_TYPE98` for the Chinese tank.

On hosted Strike at Karkand Co-op, Windows operated the cannon, coaxial gun and
cupola while macOS observed from the ground. Both rendered two cannon impacts,
18 coaxial impacts and 18 cupola impacts. The largest separations were 35 cm,
4.3 cm and 1.2 mm respectively. A press during cannon reload produced no impact.
The HUD showed the chamber emptying, the reserve dropping from 39 to 37 across
two reloads, and both machine guns heating and cooling to zero.

## Vehicle cameras and local tank motion

Vehicle cameras start in first person when a seat is entered. C switches between
the cockpit and chase views. F9 selects the cockpit and F10 selects the chase
view. The seat requests are F1–F8. The chase view requires the `sv.allowExternalViews` bit in the
retail `ServerInfo` block. First person uses the seat's camera mount,
`insideFOV` and `cockpitSubGeom` when the template supplies them.

The exterior view uses the seat's default `Camera` node, its chase distance,
angle, offset and field of view. The camera follows the replicated joint chain
when the camera is mounted on a supported turret. The display interpolates
those joints on the same server timeline used for hull motion. Terrain and map
collision shorten the camera's path when it passes behind an obstacle.

For the local driver of a mobile ground vehicle or boat, the camera and hull
sample the controlled-state history on the same presentation clock as the
other actors.
The action queue and its acknowledgements no longer shift the displayed hull.
The renderer services vehicle input during long frames through the action
clock. The server runs vehicle physics in all cases.

On Gulf of Oman, the FAAV driver's packet compression reference matched the
observer's hull position within 0.8 mm across 142 shared server ticks. The
compressed position decoded against older baselines could lag by 9.94 m.

A Windows and macOS run on an original 1.50 server confirmed the server's
exterior-view permission. F10 and C put the camera about 15.6 m from the hull.
F9 and C returned it to the cockpit, 0.81 m from the hull. The observer drew
the driver in his seat throughout the moving test. Dismount returned the
driver to the same infantry object. In this run the driver and observer hull
positions agreed within 0.7 mm at 59 shared server ticks.

Aircraft pilot input follows the stock `Air.con` and `Helicopter.con` mappings.
The jet routes mouse motion to roll and inverted pitch. Helicopters use the
mouse-look axes. The pilot can hold the crouch binding to look around.
The controlled aircraft uses the absolute hull position in the packet's
compression reference. The aircraft's vehicle category sets the motion limit
for the pilot's exterior camera and remote hull rendering.

On hosted Gulf of Oman 32-player Conquest, the Windows pilot took off and
steered both the Havoc and Su-34. The macOS observer agreed on both aircraft's
position and attitude. During flight, the maximum position difference was
under 1 mm for each aircraft.

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

OpenBF2 composes the hull transform submitted during the current frame, the
parent part's rest transform and the seat matrix. The seat matrix places the
soldier's physics pivot. Shifting the mesh down by `coll-soldier-pivot-height`
along the seat's local up axis places the driver in the seat. The soldier keeps
his association with the player when the player takes control of the vehicle.
The seated mesh follows replicated joints in its named attachment's ancestry.
The Vodnik gunner's attachment is the fixed cupola base, and the gunner follows
the tower's yaw. Gun elevation stays on the gunmount.

His pose is the seat's `seatAnimationSystem` (`3p_driver_A_animSys.inc`,
`3p_gunturret_A_animSys.inc`, …): the same format as a soldier's, a plain tree
whose `root` plays `still` (`3p_driver_A_static.baf`, `3p_vodnik_gunner_sit.baf`).
The weapon layer does not play in a seat.

A seat with no `seatInformation` (a tank's driver, a LAV's passengers) gives no
place; such a soldier is not drawn — **inferred**, the engine's own rule for
hiding him is not read.

## What the screen shows, and what is open

On hosted Gulf of Oman Conquest, the Windows driver entered and drove the FAAV
in first person. The server disabled exterior views. Replaying the macOS
observer's traffic reproduced the driver above the roll cage before the pivot
correction and in the cockpit after it. Both hosted Karkand Co-op captures
read the Vodnik cupola as two joints in seat 1825. The decoded joint angles
matched at all 1,058 shared server ticks. Replaying those packets draws the
gunner inside the hatch.

Also seen: a soldier in the bind pose high in the air near the vehicles — not
identified (a stationary weapon's seat, a parachute?).

## The registry ran every tweak twice

Both registry builders (the session's `buildRegistry`, `object_info`) ran every
`.con` and every `.tweak` on its own. A vehicle's `.con` ends with `include
<vehicle>.tweak`, so each tweak ran twice, and each of its `addTemplate`s added
the child twice: the HMMWV had its codriver and rear seat twice, the LAV its four
passengers. The builders now run the `.con`s first and only the tweaks no `.con`
included (`con::RecordingProvider` says what a run read).
