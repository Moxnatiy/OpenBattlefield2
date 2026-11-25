# Wind — BF2.exe WindManager and WindAffector

The world's wind is a game-side system: the WindManager object
(`dice.hfe.world.WindManager.Standard`, factory 0x715bf9, ctor 0x729ff0,
vtable 0x920470, global 0xa08f64) and `WindAffector` template components.
RendDX9.dll finds it by name (`WindManager`, 0x100d33b0 → particle manager
+0x8c0c) and calls its slot +0x3c for the wind at a particle
(docs/functions/sprite-particles.md). Names come from the Linux server's
symbols (`dice::hfe::world::WindManager::getWind`, …); behaviour from BF2.exe.
Taken apart with `tools/ghidra_scripts/StringRefs.java`, `DecompileAt.java`,
`DisasmRange.java` over `ghidra_projects/headless/BF2`.

Port: `src/game/include/obf2/game/wind.h`, test `tests/test_wind.cpp`.

## WindManager, vtable 0x920470

| field | ctor | meaning |
|---|---|---|
| +0x0c | set | the affectors (a tree) |
| +0x24 | set | the scratch result of the range query |
| +0x2c | 0 | global speed (`globalWindSpeed`, setter 0x729f20) |
| +0x30 | 0/0/1 | global direction (`globalWindDirection`, setter 0x729f60) |
| +0x3c | 0 | global wind = speed · direction, recomputed by both setters |

| slot | address | |
|---|---|---|
| +0x10 | 0x7296b0 | update(dt): every affector's +0x38 |
| +0x18 | 0x729cd0 | affectors near (pos, r): active (+0x3c) and `|affector pos − pos|² < (bounding radius + r)²` |
| +0x24 | 0x729f20 | set global speed |
| +0x28 | 0x729f60 | set global direction |
| +0x2c | 0x729ec0 | global wind (+0x3c) |
| +0x30 | 0x729ed0 | wind speed at a point: |slot +0x3c| |
| +0x34 | 0x729f10 | global direction (+0x30) |
| +0x38 | 0x7297f0 | direction and speed at a point |
| +0x3c | 0x729720 | **wind at a point**: global wind + Σ affector +0x50 over slot +0x18(pos, 0) |
| +0x40 | 0x729cb0 | add affector |
| +0x44 | 0x729fb0 | remove affector |

No shipped level sets `globalWindSpeed` or `globalWindDirection` (searched
every `Levels/*/client.zip` and `server.zip` `.con`), so the global wind is 0.

## WindAffectorTemplate, ctor 0x640af0

The template-writer 0x640810 names each field (its `this` is the object +4):

| field | ctor | data |
|---|---|---|
| +0x0c | 1 | `windIsDynamic` (not read by the wind query) |
| +0x10 | Directional | `windType`: Directional, Point, RadialDir, TurbulenceField (table 0x9767e0) |
| +0x30 | 0/−1/0 | `windDirection` |
| +0x3c | 0 | `windSpeed` (getter slot +0x4c, 0x6407c0) |
| +0x40 | 10 | `windFalloff` (slot +0x54, 0x6407d0; also the bounding radius) |
| +0x44 | 1 | `windRadialFalloff` (slot +0x5c, 0x6407e0) |
| +0x48 | −1 | `windBlowTime` (slot +0x64, 0x65a0a0) |

Slot +0x28 (0x6407f0) creates the affector (0x67ea10).

## WindAffector, vtable 0x909980

Fields: +0x08 template, +0x0c object, +0x10 active (ctor 1), +0x14 time left.

| slot | address | |
|---|---|---|
| +0x14 | 0x67e600 | init(object): time left = blow time; manager add |
| +0x18 | 0x67e680 | destroy: manager remove |
| +0x38 | 0x67e630 | update(dt): if active and time left > 0 (0x93c850): time left −= dt; at ≤ 0 manager remove, inactive |
| +0x3c | 0x68a8a0 | active |
| +0x44 | 0x67e5f0 | bounding radius = template falloff |
| +0x48 | 0x67e5c0 | position = the object's (virtual +0x68) |
| +0x4c | 0x67e740 | direction and speed at a point |
| +0x50 | 0x67e6d0 | wind at a point = direction · speed; 0 when inactive |

Direction and speed (0x67e740), inactive → speed 0; `p` the point, `o` the
affector position, `M` the object's matrix (virtual +0x80, applied as a row
vector by 0x5a1770), normalize 0x4639b0 (|v|² within 2⁻²³ of 1 left, below
2⁻²³ zero):

* Directional: `dir = templateDir · M`; `speed = (1 − min(|p−o|²/falloff², 1)) · windSpeed`.
* Point: `dir = normalize(p − o)`; speed as Directional.
* RadialDir: `a = templateDir · M`, `r = p − o`, `t = r·a`;
  `speed = (1 − min(t/falloff, 1)) · windSpeed`; `q = r − a·t`; if
  `radialFalloff² ≤ |q|²` the speed is 0 (direction left as `r`); else
  `w = min(|q|²/radialFalloff² + 0.2, 1)` (0.2 at 0x978180),
  `dir = normalize(q)·w + a·(1 − w)`.
* TurbulenceField: no branch; the caller's zero stays.

## The owner's matrix `M`

`M` is the matrix of the object that owns the component (virtual +0x80 on
affector +0x0c), applied as a row vector, so `templateDir · M` is
`x·row0 + y·row1 + z·row2`.

**Effect bundle.** A bundle spawned on a surface gets its matrix through its
slot +0x88, called with the spawn matrix by 0x5ca910 (and by the projectile
update 0x646540, `+0x88(this + 0x5e)` after 0x5ca5f0). When the bundle has a
parent (+0x28 ≠ 0), 0x5ca910 calls slot +0xe8 instead. The spawn matrix is
built by 0x5ca5f0(point, normal n, forward f) in a static at 0x973f98. Its
fallback is the identity with the point as row 3. When `|n·f| ≤` *0x8eaa8c,
the rows are as follows (0x4639b0 normalizes):

| row | value |
|---|---|
| 0 | `normalize(n × f)` |
| 1 | `n` as given (its normalized copy is not stored) |
| 2 | `row0 × n` |
| 3 | the point |

That slot +0x88 is the matrix slot +0x80 returns is **not taken apart**.

**What ships.** Every `WindAffector` in `mods/bf2/Objects_client.zip` (22,
counted with `unzip -p … '*.tweak' | grep windType`) is one of two kinds:

* 10 `Point`, on effect bundles such as `e_exp_grenade`. Point does not read
  `M`.
* 12 `RadialDir`, on the rotor or lift `Engine` of the helicopters and the
  F-35B. An example is `ahe_ah1z_Rotor`: direction 0/0/−1, speed 20, falloff
  30, radial 20, dynamic, blow time −1.

No shipped file uses `Directional`. So the bundle orientation changes no
shipped wind, and the one `M` that matters is the rotor part's world matrix.

## In the port

`game::WindManager`, `game::WindAffector` and `game::ScopedWindAffector`
(`obf2/game/wind.h`) model the two classes above. An affector takes its
template (`e_exp_grenade`: Point, speed 10, falloff 10, radial 5, blow time
−1), its position and its matrix `M` (`setOrientation`); `tests/test_wind.cpp`
checks the AH-1Z rotor under a turned matrix. For a smoke bundle `M` is the
identity, which is exact for every shipped bundle, because all of them are
Point.

Nothing registers affectors yet: the smoke bundles and the vehicle parts
that own one (for example `ahe_ah1z_Rotor`, an `Engine` under the rotor head)
are wired in with the world view. The plan there: one `ScopedWindAffector`
per such part while the vehicle is drawn, its `M` the vehicle × the part's
rest placement, switched by the enter and exit edges of the seats above the
part, as the engine messages below do.

**The rotor's spin does not move the downwash.** Every rotor `Engine` with
a wind affector turns about its own z only. The data says so (`ahe_ah1z_Rotor`,
`ahe_havoc_Rotor`, `ahe_z10_Rotor`, `chthe_z8_Rotor`: `setMaxRotation
0/0/150`, `setMaxSpeed 0/0/1`, `setInputToRoll PIThrottle`;
`AIR_F35B_HelicopterEngine` the same with `0/0/±150`). The engine update
0x680200 calls 0x66c860(template, axis, dt) only for the axes whose
template rate (+0x250/+0x254/+0x258) is non-zero. Roll is the turn about
the part's z, and the downwash axis is `0/0/−1 · M`, along that same z. So
the spin leaves it unchanged, and RadialDir reads `M` through that axis
alone. `testRotorSpinInvariant` in `tests/test_wind.cpp` checks three spin
angles. The port applies no spin, which is exact.

What does turn the axis is the **rotor head** above the engine. In
`ahe_ah1z`, `ahe_ah1z_Rotorhead2` is a RotationalBundle that pitches and
rolls with `PIPitch`/`PIRoll` (`setMinRotation 0/-8/-7`, `setMaxRotation
0/8/6`); `chthe_z8_RotorHead1/2` do the same at ±6.5/±3.5. Retail's `M` is
the engine's world matrix and carries that tilt. The port uses the rest
placement, so its downwash axis ignores the head's few degrees: **debt**.

`drawVehicle` composes the rotor's placement as vehicle × turret joints ×
rest, so the tilt is applied once the heads are decoded bundles (`ac00f49`).
They are not yet (24 Nov, `object_info --descriptor`). Both heads carry
`setNetworkableInfo BasicInfo` and belong to seat 0. Decoding stops at an
earlier seat-0 member whose layout is not read:

* `ahe_ah1z`: at `AHE_AH1Z_HydraLauncher` (GenericFireArm). Seat 0 decodes
  0 members.
* `chthe_z8`: at `chthe_z8_mainrotor` (class `Rotor`, `BasicInfo`). Seat 0
  decodes 5 members and stops there.

So the head angles depend on two networkables being read: Rotor's and
GenericFireArm's (debt, "shared networkables"). Until then `joints` is empty
and the downwash uses the rest placement.

**24 November, Ghidra:** GenericFireArm has no networkable, and Rotor's is
SimpleObjectNetworkable (network-events.md, "Which classes are
networkable"). The seat-0 cut now reaches the heads: `ahe_ah1z_Rotorhead`,
`Rotorhead2`, `chthe_z8_RotorHead1` and `RotorHead2`. This is not measured
on a live helicopter record. The decoder throws away a cut that does not end
on the record's last bit, so until a live record is measured the downwash
may still fall back to the rest placement.

**25 November, live (a dedicated server with an AH-1Z placed on Fushe Pass):**

* **Parked records:** the AH-1Z root cut `XXRREE` ends on the record's last
  bit (24 Nov). The head angles arrive as bundles 0 and 1 of seat 0.
* **Controlled record:** the record of the AH-1Z we pilot used to keep only
  the leading rotational members. That is none here, because the cut opens
  with the two rotors. Now the whole cut is read, followed by
  `Player::getStateSync` (10-bit length, ending on the record's last bit),
  and it ends in all 4672 records of a run (`members XXRREE controlled ...
  END`).
* **Downwash trace:** `OBF2_TRACE_WIND` now measures the axis against the
  untilted rest placement, and it reads 0.00° at rest.
* **Why the heads read 0:** our input, not the server. A bundle takes
  input only from the axes its template names (Linux
  `RotationalBundle::handlePlayerInput` 0x586e80: `getInputToYaw/Pitch/Roll`
  index the PlayerInput axes). The heads name `PIPitch` and `PIRoll`. The
  port sent a helicopter pilot's mouse as `PIMouseLookX/Y`, following
  `Settings/Default/Controls/Helicopter.con`. That file is a preset with no
  pitch or roll axis at all. The shipped `Settings/Controls.con`
  (`HelicopterPlayerInputControlMap`) maps the mouse with the same two lines
  as the jet map: `c_PIPitch IDFMouse IDAxis_1 0 0` and
  `c_PIRoll IDFMouse IDAxis_0 0 0`. `RemoteWorld` now maps the mouse that way
  for both jets and helicopters.
* **After the fix:** the heads arrive at ±7.9° pitch (limit ±8) and ±5.9° roll
  (limit +6/−7).
* **The heads cancel:** `ahe_ah1z_Rotorhead2` (child of `Rotorhead`, same
  frame) always arrives as the exact negative of `Rotorhead`. The −86° is the
  engine's own placement under Rotorhead2 (`addTemplate ahe_ah1z_Rotor`,
  `setRotation 0/-86/0`), so the two heads are coaxial and cancel in both
  axes. The drawn downwash tilt stays 0.00–0.03°, which is what the
  networked angles compose to.
* **Not seen:** what makes the server negate the child head. Nothing in the
  templates says so: both are identical. The on-screen comparison with
  Retail is still to do.

## The engine turns its affector on and off

An object keeps its `WindAffector` component at +0x70. The component setter
is 0x7201d0 and the getter 0x71f3b0; the interface id is *0x892f34, matched
by queryInterface 0x67e590. Every call of slot +0x40 (`setActive`, 0x6d87b0,
byte +0x10) through an object's +0x70 was listed with
`tools/ghidra_scripts/FieldSlotCalls.java 0x70 0x40`. All of them are in the
engine classes:

* **EngineBase** (ctor 0x67f270, vtable 0x909cf0)
* **Engine** (ctor 0x680060, vtable 0x90a0a8)
* a second subclass (ctor 0x681720, vtable 0x90a368)

The two subclasses are made by factory 0x650e10.

| where | when | active |
|---|---|---|
| slot +0x100 0x67f560 (`EngineBase::handleMessage`) | message 4, unless +0x1cd is set; also sets +0x1cc (running) = 1 and +0x20c = −1 | on |
| same | message 0 when the engine type (+0x2c4 slot +0x14) has bit 0x10: as message 4 | on |
| same | message 5; message 1 with bit 0x10; message 0x18 | off |
| slots +0xac 0x680200 (Engine) and 0x6819b0 (the other subclass) | every update while +0x1cc (running) is 0 | off |
| slots +0x1c 0x67f9d0 and +0x24 0x67ef30, and the fragment 0x67fad6 | — | off |

Case 5, for engine types with bit 0x1 or 0x21, can leave the engine running
(+0x1cc = 1) when 0x71b560 fails, so an aircraft left in the air keeps its
engine. The wind is still turned off on that path (0x67f770). The affector
has no speed of its own: `windSpeed` comes from the template only, so no
throttle or rotor speed scales it.

Who sends the messages, found with `tools/ghidra_scripts/PushSlotCalls.java
0x100 4` and `… 0x100 5`:

* 0x4fdc30 stores the object in player +0x80, then sends 2 and 4 to it with
  the player. This is the player entering.
* 0x5fc9c0 sends 5 and then 3 to itself with its player (+0x5c). This is the
  exit.

These scans only see constant pushes.

**Down the tree.** `EngineBase::handleMessage` first calls its base
0x66e230. That base handles 2, 3 and 0x18 for itself, then calls
`Bundle::handleMessage` 0x626f70 (the `Bundle.cpp` debug string is in it).
0x626f70 sends the same message to every child: the first child through
slot +0xec, then each sibling through +0x2c, each through its own +0x100.
So a message travels down from the object it was sent to, never up. It
reaches an Engine exactly when the Engine is at or below the seat object the
player entered or left.

In the shipped helicopters the rotor sits below both seats. For example, in
`ahe_ah1z` the main rotor is under `AHE_AH1Z_COgunner`, which is under the
root `ahe_ah1z` (the same holds for `ahe_havoc`, checked with `object_info
--tree`). So:

* entering either seat turns the downwash on;
* leaving either seat turns it off, even with the other seat still taken,
  until someone enters again.

The F-35B has one seat, the root.

The engine ctor 0x67f270 clears +0x1cc (running) and +0x1cd. So an empty
vehicle's first engine update turns the affector off, although the affector
ctor (0x67ea10) sets it active.

Port: `VehicleParts::Wind::seats` lists the seats whose control object is
on the part's path from the root. `drawVehicle` compares each seat's owner
(`ownerOf(vehicle + index)`, or our own object) with the last frame and
calls `setActive` on each edge.

**+0x1cd, the dead engine.** On message 0x18 the engine walks up from
itself through the parents (+0x28) to the first object with a component at
+0x3c. That slot is the `ArmorGLComp`: the setter 0x7201d0 stores interface
*0x892d78 there, matched by queryInterface 0x6893a0 of vtable 0x90b570. The
engine calls the component's slot +0x50, which is
`ArmorGLComp::isSendingMessage()` (0x689410, byte +0x14). The names come
from `tools/linuxded/vtable.py dice::hfe::world::ArmorGLComp --msvc`; slot
+0x54 is `isDestroyed` (+0x15) and +0x58 is `isCriticalDamaged` (+0x16).
If it answers true, the engine sets +0x1cd.

`ArmorGLComp::_setHitPoints` (0x687930) works as follows:

* On entry it sets +0x14 = 1; it clears it at 0x687e41.
* When the hit points fall to 0 or below and the armor is not destroyed
  yet, it sets +0x15 and +0x16 and calls 0x686c80.
* If it was asked to send and the object is local, it then sends 0x18 to the
  object it belongs to. `Bundle::handleMessage` passes that down to every
  Engine, and each one finds its armor still sending.
* When a destroyed armor's hit points rise above 0, it sends 0x19 instead.
  The engine handler has no case for 0x19.

So +0x1cd means **the vehicle was destroyed**. The engine ctor is the only
thing that clears it, so after that message 4 never starts the engine again
and the downwash stays off.

Port: a vehicle whose `vehicleHealth` is ≤ 0 draws its wreck mesh
(`WorldView::objectMesh`), which has no `VehicleParts`. So `drawVehicle`
returns before the wind code, and the vehicle's entry, with its affectors,
is erased on the next frame. That matches +0x1cd for as long as the object
stays a wreck. A wreck whose health rises above 0 again is not modelled. When Retail's EffectBundle object — and with it the
component — is destroyed is **not taken apart**; the port drops the affector
when the instance is erased.
