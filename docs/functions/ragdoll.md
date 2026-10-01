# The ragdoll: a dead soldier's body

`dice::anim::RagDoll` and `RagDollTemplate` (`Animation/BoneAnimation/RagDoll.cpp`
by the server's symbols). Names and structure come from the Linux server, which
compiles the same engine code; the client's copies are named where they were
checked. What travels on the network is in network-events.md ("The ragdoll
branch").

## The tuning variables

Registered at start-up through `Vars::getInt`/`getFloat` (Linux 0x4ee6b0,
0x4eea40) by RagDoll.cpp's static initialiser; `tools/linuxded/vars_defaults.py
0x6baa00 0x6bb000` prints the table. The client registers the same names at
`BF2.exe` 0x86db40, and the defaults checked there agree
(`ragdoll-max-inherit-speed` 40, `_net_interpolate` 0.1, `_gravityOverTime`
0.5, `_dT` 1/90 = 0x3c360b61, `_slowMotion` 1).

| variable | default | global (Linux) |
|---|---|---|
| ragdoll-max-inherit-speed | 40.0 | rd_maxInheritSpeed |
| ragdoll_net_debug | 0 | rd_netDebug |
| ragdoll_net_boneEnable | 15 | rd_netBoneEnable |
| ragdoll_net_interpolate | 0.1 | rd_netInterpolate |
| ragdoll_gravityOverTime | 0.5 | rd_gravityOverTime |
| ragdoll_dT | 0.0111111 (1/90) | rd_dT |
| ragdoll_slowMotion | 1.0 | rd_slowMotion |
| ragdoll_enableForce | 1 | rd_enableForce |
| ragdoll_iterations | 2 | rd_iterations |
| ragdoll_clientMaxUpdateTime | 0.002 | rd_clientMaxUpdateTime |
| ragdoll_sleep | 2.5 | rd_sleep |
| ragdoll_sleepDelta | 0.02 | rd_sleepDelta |
| ragdoll_useObjectCollision | 1 | rd_useObjectCollision |
| ragdoll_useCapsuleCollision | 1 | rd_useCapsuleCollision |
| ragdoll_completeCollRadius | 1.5 | rd_completeCollRadius |
| ragdoll_useLandCollision | 1 | rd_useLandCollision |
| ragdoll_useConstraints | 1 | rd_useConstraints |
| ragdoll_useAngularConstraints | 1 | rd_useAngularConstraints |
| ragdoll_expRotateForce | 13.0 | rd_expRotateForce |
| ragdoll_boneSize | 0.1 | rd_boneSize |
| ragdoll_kneeBoneSize | 0.05 | rd_kneeBoneSize |
| ragdoll_bodyBoneSize | 0.12 | rd_bodyBoneSize |
| ragdoll_collMid | 0.9999 | rd_collMid |
| ragdoll_legBackAngle | -0.1 | rd_legBackAngle |
| ragdoll_legForwardAngle | 10.0 | rd_legForwardAngle |
| ragdoll_legForwardFactor | 0.0 | rd_legForwardFactor |
| ragdoll_legPointForwardAngle | 0.5 | rd_legPointForwardAngle |
| ragdoll_legPointForwardFactor | 0.01 | rd_legPointForwardFactor |

## A particle (0x58 bytes)

`Particle::Particle(Vec3, int, float, float)` (Linux 0x6ba720) and the readers:

| offset | what |
|---|---|
| +0x00 | position |
| +0x0c | the previous position (Verlet) |
| +0x18 | purpose not established (zeroed on `setIsClient(false)`) |
| +0x24 | a vector `addHitImpact` dots with the hit direction — purpose not established |
| +0x30 | the network's position (`readCurrentState` writes it) |
| +0x3c | purpose not established (zeroed on `setIsClient(false)`) |
| +0x48 | the bone |
| +0x4c | the mass (`addParticle`'s second argument) |
| +0x50 | the size: `rd_kneeBoneSize` for bones 2 and 7, `rd_bodyBoneSize` for 1, 6, 15, 31, `rd_boneSize` otherwise (`addParticle`, 0x6bd8e0) |
| +0x54 | sent over the network |

**Which particles go out** (`RagDoll::reset`, Linux 0x6bef00): bone 1 when
`rd_netBoneEnable & 1`, bone 6 on `& 2`, bone 15 on `& 4`, bone 31 on `& 8`. With
the default 15 that is all four — the hips, the thighs' tops and the shoulders
(`ragDollInit.con` puts particles on bones 1, 6, 15, 31, mass 1 each), and the
live server sends exactly four. `reset` places each particle at its bone's world
position (the skeleton's bone matrix through the given matrix), and the previous
position 0.015 × the given velocity behind it.

## The template from the data

`objects/soldiers/common/animations/ragDollInit.con` and
`ragDollConstraints.inc`. Each `ragDoll.*` command is a console class of
`ModuleBoneAnimation.cpp` whose `executeObjectMethod` calls one template method
(found by their calls; `ragDoll.addConstraint` is `consoleObject107`, whose name
string is at 0xc91650):

| command | console class | method (Linux) | what it keeps |
|---|---|---|---|
| `skeleton <file>` | 101 | `skeleton` 0x6bdf50 | the template's skeleton (+0x108), loaded once |
| `addParticle <bone> <mass>` | 102 | `addParticle` 0x6bd8e0 | a particle (table above); the bone's particle index in the 128-entry table at +0x158 (−1 where none) |
| `lockBone <first> <last>` | 104 | `lockBone` 0x6bb3d0 | sets bones first..last in a 128-bit set (+0x50) |
| `toSkeleton <bone> <a> <b>` | 105 | `toSkeleton` 0x6bb1c0 | a `BoneMapping {bone, a, b}` appended to +0x8, as given |
| `addConstraint <a> <b>` | 107 | `addDistanceConstraint` 0x6bbf90 | list +0xa8: particles, rest length = the bones' distance |
| `addCapsuleCollision <a> <b>` | 108 | `addCapsuleCollision` 0x6bb210 | particles, 0 |
| `addAngularConstraint <a> <b> <c> <deg>` | 109 | `addAngularConstraint` 0x6bc2b0 | list +0xd8: particles, d1 = |ab|, d2 = |bc|, t = ±√(d1² + d2² − 2·d1·d2·cos|deg|) with the sign of deg |
| `addDihedralAngleConstraint <a> <b> <c> <d> <deg>` | 110 | `addDihedralAngleConstraint` 0x6bc080 | list +0xe8: particles, |ab|, |bc|, |cd|, t = √(|ab|² + |cd|² − 2·|ab|·|cd|·cos deg) |
| `addForce <bone> <vec>` | 112 | `addForce` 0x6c0a80 | not in the data |
| `setParticleCollisionCriteria <speed> <delay>` | 113 | 0x6ba5c0 | +0x118, +0x11c (−1 and 0 by default) |
| `addJointConstraint <a> <b>` | 115 | `addJointConstraint` 0x6bb0d0 | not in the data |
| `addDistanceLessThanConstraint <a> <b> <len>` | 117 | 0x6bb020 | list +0xf8: particles, len |

A constraint naming a bone with no particle is dropped. An existing distance,
angular or dihedral constraint on the same particles is updated, not doubled.

The distances are between the bones of the template's skeleton **in model
space**: `getBoneDistance` (0x6bbef0) takes the translations of the skeleton's
+0x20 matrices, and `Skeleton::transform` (0x6c8760) fills those as the bone's
local matrix times its parent's +0x20. Loading (`Skeleton::Skeleton`, 0x6c8da0)
zeroes every quaternion and translation component under 0.001, and gives a bone
whose name contains `mesh` (0xb9167a) an identity matrix — **we do neither yet**
(`mesh::loadSkeleton`).

The template's own constants (`RagDollTemplate::RagDollTemplate`, 0x6bd2a0):
+0x4 the gravity, **−30** (0xc1f00000); +0x118 −1, +0x11c 0.

## One step

`accumulateForces` (0x6bd540), for every particle: acceleration (+0x18) +=
force (+0x3c) / mass, and its y += gravity × ramp, where the ramp is +0x68 / +0x64
while +0x64 > 0 (else 1) — `update` grows +0x68 by the step up to +0x64.

`verlet(dt)` (0x6bdff0), for every particle:

* networked and in the client mode: the previous position becomes the position,
  and the position moves `rd_netInterpolate` (0.1) of the way to the network's
  (+0x30);
* otherwise `d = pos − prev + accel·dt²`, `prev = pos`, `pos += (d + d_last)/2`,
  `d_last = d` (+0x24);

then zeroes the acceleration and the force.

`satisfyConstraints` (0x6c12e0): in the client mode it saves every networked
particle's position first. `rd_iterations` (2) times: if `rd_useConstraints`,
the angular, dihedral, leg-angular, distance-less-than and distance checks, in
that order; then `checkCollisionConstraints`; then, in the client mode, every
networked particle goes back to its saved position (and its previous one too).
Before the loop it collects the colliding objects when `rd_useObjectCollision`
or `rd_useCapsuleCollision`, and restores them after.

Every check below moves a particle only when it is not pinned (the client mode
and +0x54); `w` is 1/mass, and a pair is corrected only when its distance is over
0.0001.

* **distance** (0x6bc4d0): `k = (len − rest) / (len·(wa + wb))`,
  `a += diff·k·wa`, `b −= diff·k·wb` with `diff = b − a`;
* **distance less than** (0x6bc6a0): the same, only while len < its length;
* **angular** (0x6bc870, under `rd_useAngularConstraints`): the distance a–c
  against t — at least t when t ≥ 0, at most |t| when t < 0 (the bound 9999 on
  the other side). When it is out, a and c are corrected to |t|, then a–b to d1
  and b–c to d2. **Both of those are weighted by c's mass**, b's own is not used
  (`(wa + wc)` and `(wc + wc)`) — kept as the engine has it;
* **dihedral** (0x6bccf0, same switch): `v = (d − c) + (b − a)`; while
  |v| < t, a and d are corrected to t (d is pinned-checked **by c's flag**), then
  a–b, c–d and b–c back to their lengths.

## The client's mode

`RagDoll::setIsClient(bool)` (Linux 0x6c1540): leaving the client mode moves
every networked particle onto its network position (+0x30 into +0x0, the old +0x0
into +0xc), zeroes +0x18 and +0x3c, and satisfies the constraints once.

## `RagDoll::update(float)` (Linux 0x6c1850)

The step is `rd_dT`, ×1.5 when +0x6c > 0; the frame time is scaled by
`rd_slowMotion`. Awake (+0x31), it accumulates time at +0x60 and runs whole
steps of `accumulateForces`, `verlet(dT)`, `satisfyConstraints`; then it compares
the centre with the last one (+0x34) — moved more than `rd_sleepDelta` wakes it,
otherwise +0x4c counts down to sleep. Asleep, it checks the colliding objects
every 1/30 s and wakes for a soldier template (0x9c50) or a moving physical
object. In the client mode with +0x76 clear, +0x70 counts down and then leaves
the client mode.

## The body's own frame

`getCenter` (0x6bf800) is bone 6's particle; `getRight` (0x6c0aa0) is
normalize(p1 − p6); `getForward` (0x6c06a0) is normalize((p6 − p31) × (p31 − p1)).
A normalisation here and below is the engine's (`Vec3::normalize`): a vector whose
squared length is within 1.19e-7 of 1 is left alone, one within 1.19e-7 of 0
becomes zero.

## The leg check (0x6c0c00)

No pin check. For each leg of the static table `leg` = {1, 6} (0xf68b94), the
particles on bones L, L+1, L+3 — hip, knee, foot — with `right` and `forward`
of the body (`getRight` lands in the frame's local_58, `getForward` in local_48,
read from the call sites 0x6c0c1f and 0x6c0c2f):

* `n = normalize(right × (knee − hip))`, `c = dot(normalize(knee − hip), forward)`;
* while `rd_legForwardFactor` ≠ 0 (it is 0 by default) and c is under
  `rd_legBackAngle` or over `rd_legForwardAngle`: `s = (that bound − c) × factor`,
  hip −= n·s, knee += n·s;
* while `rd_legPointForwardFactor` ≠ 0 (0.01): `c2 = −dot(n,
  normalize(foot − knee))`; under `rd_legPointForwardAngle` (0.5),
  `s = (0.5 − c2) × 0.01`, knee += n·s, foot −= n·s.

## Against the ground (`checkCollisionConstraints`, 0x6bf920)

First, while +0x6c < 2, `checkCapsuleConstraints` (0x6baa50): **the shins only**,
2–4 against 7–9, as capsules of `rd_boneSize` (`capsuleVsCapsuleCollision`
0x724400: the closest points of the two segments,
`getClosestDistanceBetweenLines` 0x723cf0; under 2r apart, the normal from the
second to the first). Each shin moves a quarter of the overlap apart along it, no
pin test.

`getClosestDistanceBetweenLines` is the classic segment–segment distance with
both fractions in [0, 1], walked region by region; for segments that are not
parallel its two points are the unique closest ones. Nearly parallel ones
(|a·c − b²| < 0.001 — an absolute bound, so for shins of 0.46 m anything within
about 9°) take the engine's own endpoints: same direction — s = −d/a while d ≤ 0,
else t = d/(d1·d2) or 1; opposite — s, t = 0 while d ≥ 0, else s = 1 and
t = −(a + d)/b capped at 1, or s = −d/a.

The objects are collected once per `satisfyConstraints` by `getCollidingObjects`
(0x6be1f0): the object manager's objects within `rd_completeCollRadius` (1.5 m)
of the centre that pass `RagDollObjectPredicator` (0x6c3a40: a collision mesh of
type 2, a physics node whose flags (+0xa8) lack 0x10, and the object flags the
predicate holds, 0x100 — **what 0x100 and 0x10 are is not established**), and
whose type-2 bounding box meets that sphere. Type 2 is the soldiers' collision,
the one `checkSoldierVsMesh` asks for.

With `rd_useObjectCollision`, every particle (except, once +0x6c ≥ 2, bones 2,
7, 16, 32, 47) that is not pinned: `prev` and `dir` as for the ground below, the
segment from `prev` along `(pos − prev) + dir·size` against each object's mesh
(`CollisionMesh::getDistance`, vtable +0xb8, 0x7187a0 →
`CollisionMeshTemplate::getDistance` 0x71bee0 → `Bsp::getClosestFace` 0x709b20 →
`BspNode::getClosestFace` 0x709700 → `checkFaceAndEdgeCollision` 0x724ae0,
one-sided, the walk stopping at the first face it accepts). A face is crossed when
the segment's end is on or under its plane, its start on or above it, the motion
goes against the normal and the crossing lies in the triangle. What the ragdoll
reads back is the face's normal and `(r − 1)·|motion|` — how far before the end
the plane was crossed. On a hit facing the particle (`dot(dir, n) < 0`) it moves
**along the normal by that length** (`pos − n·along`), its velocity is gone, it
is marked hit (+0x55) so the ground leaves it this pass, and an impact is
reported; the first object that answers so ends the particle's search.

Ours: `CollisionWorld::segmentContact` over the session's collision world (every
static object's and every spawned vehicle's type-2 faces, near the centre),
taking of all the faces crossed the one crossed first; the predicate's flags are
not applied.

With `rd_useLandCollision`, every particle not hit and not pinned:

* `prev` = its previous position, or, when it has not moved, the position
  raised by half its size;
* `dir = normalize(pos − prev)`; the segment from `prev` to `pos + dir·size` is
  cast at the terrain (`HeightmapCluster::intersectRayInWorldCoords`, its vtable
  +0x120, Linux 0x701030, with the water flag set);
* a hit that is not water (material 1) and faces the segment
  (`dot(dir, normal) < 0`): `pos −= normal·t`, the previous position too (the
  velocity is gone), and an impact;
* otherwise, when the particle is under the terrain's height there
  (`getHeightInWorldCoords`, +0x138): it rises **1% of the gap** per pass, and its
  previous position follows.

Last, while +0x6c < 2, `checkCollisionCapsulesAgainstObjects` (0x6bb450, under
`rd_useCapsuleCollision`): every `addCapsuleCollision` pair, as a capsule of the
first particle's size, against each colliding object
(`CollisionMesh::getDistanceToCapsules`, vtable +0xd0 → 0x719ba0: the faces
`Bsp::getCollidingFacesInsideCapsule` finds, each tested by
`checkCapsuleEdgesCollision` 0x727770 — the capsule against the triangle's three
**edges** through `getIntersectionOfCapsAndEdgeNew` 0x726540 and
`...Internal` 0x725b90). Every contact whose value is under zero moves both
particles along its normal by that much, their velocity gone, no pin test.
**Not ported**: the two intersection routines are not reversed — a limb can still
cross an edge between two particles that stay outside.

**The cast** comes down to `Heightmap::intersectRay` (0x6fc510), a descent
through the height map's levels of maxima to the cells the segment crosses,
nearest first. In a cell the two triangles are the ones `getHeightAndNormal`
uses (`Level::groundContactAt`): for the triangle under the segment's start,
`t = (end − corner)·n` — the end's signed distance from the triangle's plane —
and while `t ≤ 0` the crossing (or the start itself, when the start is under the
plane too) is the hit if it lies in that triangle; otherwise the other triangle
of the cell is tried. **t is in grid units**: everything in the leaf is scaled by
the heightmap's +0x48 (1 / spacing), and neither `intersectRayInLocalCoords`
(0x6fea10) nor the world wrapper (0x6f9d70) scales it back. So the ragdoll is
pushed out by depth / spacing — kept as the engine has it.

## Onto the skeleton (`applyOnSkeleton`, 0x6c1d90)

1. Every particle's position less the centre's (bone 6) — the "local space
   coordinates". The skeleton is posed **relative to the centre**.
2. `forward` and `right` as above (`getForward` lands in local_58, `getRight`
   in local_68, by the frame of 0x6c1d9f: `subq $0x168`, six pushes).
3. For each `BoneMapping {bone, a, b}` in the order `toSkeleton` added them, the
   bone's matrix starts as its current one in the skeleton (+0x20), and, with
   `P(x)` the local coordinate of x's particle or, when x has none, the
   translation of bone |x|'s matrix as it stands (a bone set earlier in this
   loop — which is what the negative numbers in the data are for):
   * a bone **without** a particle: `y = normalize(P(b) − P(a))`; when a and b
     are both ≥ 0 the translation is `P(a) + y × (rest distance a → bone)`
     (`getBoneDistance(a, bone)`), otherwise it is kept;
   * a bone **with** a particle: when a and b are both ≥ 0 the translation is
     its particle; `y = normalize(P(b) − P(a))`; for bones 1 and 6 the leg's side
     is recomputed, `normalize((foot − T) × (knee − T))` with T the translation
     (bone+1 the knee, bone+3 the foot);
   * the x hint, by bone: 1–4 and 6–9 the leg's side (the last computed — it
     carries over between bones); **5 keeps whatever the previous bone had**;
     0, 11, 12, 13, 45, 46, 47 −right; 30 forward; 14 −forward; 31–34
     `n × y` with `n = normalize((p34 − p31) × (p32 − p31))`; 15–19 the same with
     15, 16, 19;
   * `Vec3::makeOrthonormalBasis(z, x, y)` (0x734110): x and y normalised,
     `z = x × y`, `y = z × x`, `x = y × z`, each normalised, components under
     1.19e-7 zeroed. The hint survives exactly; the bone's direction is bent to
     it;
   * a bone mapped to itself (a = b, e.g. `4 4 4`): instead its rest local
     matrix times its parent's current one, at the translation found;
   * the result goes into the skeleton's +0x20 for that bone.
4. Every bone in the locked set (`lockBone`) then follows its parent at its rest
   local matrix: `local × parent` (a root takes the next bone's matrix).

## Making one, and the client's mode

`RagDoll::RagDoll` (0x6bbd70): awake (+0x31), +0x5c = 2 × the template's +0x11c,
the gravity ramp +0x64 = `rd_gravityOverTime` and its clock +0x68 = 0, +0x6c =
+0x70 = 0, +0x74 and +0x75 clear, **+0x76 set** (no network state yet), +0x77
clear, then `wakeUp` (0x6ba5e0: the sleep timer +0x4c = `rd_sleep`, awake).

`RagDollTemplate::makeInstance(matrix, velocity, skeleton)` (0x6bf380): the
velocity capped at `rd_maxInheritSpeed`, a new ragdoll, `reset`.

`Soldier::enableRagDoll` (0x556bb0): the velocity from the physics (+0x298, or
+0xc0 when that is 1.5 times longer, or a stored one while its tick lasts); the
skeleton posed by his animation and transformed with the identity moved to
y = −1 (his pivot); `makeInstance` with the object's matrix; **`setIsClient(true)`
when this is not the server and the object is remote**; a stored force; then the
soldier's own matrix becomes **the identity at the ragdoll's centre**
(`update(−1)` returns it). So the body is drawn as the skeleton posed relative to
the centre, at the centre, with no rotation.

`Soldier::triggerDieAnimation` (0x5572b0) calls it, from `Soldier::handleUpdate`
(0x54bd81) when +0x4a0 is set. **Who sets +0x4a0 is not established.**

`readCurrentState` (`BF2.exe` 0x7ea290) is the client's 32-bit code; its fields
are the server's moved by 0x14 (the vector and the two pointers before them are
half as wide): +0x5c = 0.3 is the client timer (+0x70), +0x61 the client flag
(+0x75), +0x62 "no network state yet" (+0x76), +0x1d awake (+0x31), +0x38 the
sleep timer (+0x4c) set to `rd_sleep` (`*0xa26178`). So every state keeps the
client's mode 0.3 s longer, wakes the body, and the first one puts the networked
particles where the server has them at once. In `update`, a client body with no
state yet is not stepped; once its timer runs out it leaves the client's mode
(`setIsClient(false)`) and goes on by itself.

`RagDollTemplate::updateInstances` (0x6c35e0) steps every body each frame, sorted
by distance to the camera (×3 when behind it): +0x6c = 1 past the third awake one
(a step 1.5 times longer, no capsules), 2 in the client's mode once
`rd_clientMaxUpdateTime` (0.002 ms, by `rdtsc`) is spent. On the client
`applyOnSkeleton` comes from `Soldier::handleVisualUpdate` (0x546fa0).

## Ours

`obf2::anim::Ragdoll` (src/anim/src/ragdoll.cpp): the template from
`ragDollInit.con` (`RagdollTemplate::load`), the step, every constraint check,
the ground and `applyOnSkeleton` as above. `Level::castSegment` is the terrain's
cast. `app::WorldView` makes a body at a soldier's first ragdoll record, from the
pose he was drawn in and his last velocity, in the client's mode; reads every
state after; steps it with the frame; and draws the soldier's mesh posed by it at
its centre.

Measured:

* the template (`test_ragdoll`, from the installation): 13 particles, 18
  distance, **12** angular (16 commands — four triples are given twice and the
  engine keeps the second), 4 dihedral, 2 less-than, 12 capsules, 30 mappings, 75
  locked bones; a thigh 0.375 m, a shin 0.458 m;
* the server's 52 states of one dead soldier (`tests/data/bf2-ragdoll.bin`)
  replayed into a client body: the networked particles end within 9 mm of the
  server's, every distance constraint within 3% of its rest length, nothing
  under the ground; the head lies 0.62 m from the hips;
* on screen, live (`--watch-ragdoll`): a dead soldier lying on his front on the
  cobbles of Karkand, arms out, his launcher on his back.

* a body thrown at 4 m/s at a wall 0.8 m away (`test_ragdoll`, `testWall`): its
  furthest particle stops at 0.460 m against the wall at 0.5 m; without the
  objects it ends 2.413 m out;
* live, 6000 frames on Karkand, three deaths: 8263 body-frames, none with a
  particle a face separates from the body's centre (the end-of-run report's
  `ragdoll bodies` line). Those three lay in the open, so this says the
  object pass does no harm, not that a wall stops a body — the test does.

Not ported, each a debt in CLAUDE.md: the limbs' capsules against objects'
edges (`checkCollisionCapsulesAgainstObjects`), the object predicate's flags,
forces and impacts (`addForce`, `addHitImpact`), water, `updateInstances`' distance
order and time budget (every body steps at the full rate), the skeleton loader's
zeroing under 0.001 and its `mesh` bones, and the local matrices a self-mapped
bone takes (the engine's are the last animation's; ours are the rest pose's).
Who starts the ragdoll on a client is not established either: ours starts at the
first ragdoll record.
