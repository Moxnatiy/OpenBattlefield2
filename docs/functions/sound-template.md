# Sound template: envelopes, pitch and distance — Retail 1.50

Scope: a one-shot 3-D `Sound` template as an explosion bundle plays it
(`S_e_exp_grenade_Start`, the material cell's `S_Impact_Grenade_Dirt`).
Code: `src/app/src/sound_envelope.cpp` and `sound_voices.cpp`.

Status: the envelopes and the voice pool are in. `SmokeFlare::play`, the
world view's pool (`WorldView::voices`) and the OpenAL output with its EAX
reverb named below come with the effects, the world view and the
environment-audio change.

Binaries: BF2Audio.dll SHA-256
`f8f9ae26dc93c929745541ea587e978bb1122d0a093c212edbe81a3116303d28`
(the copy under `evidence/retail-captures/jump-landing-20261120/`), Ghidra
12.1.4 headless, every function dumped with
`tools/ghidra_scripts/DecompileAll.java`; constants read from `.rdata`.
Linux server `linuxded-full/bin/amd-64/bf2` for names and the parser.
Template slot names come from lining BF2Audio's template vtable up with the
Linux `NullSoundTemplate` vtable (0xb9aaa0, `ISoundTemplate` order).

## Game data

`Objects_server.zip:Effects/impacts/explosions/e_exp_grenade/e_exp_grenade.tweak`
lines 41–54, `S_e_exp_grenade_Start`: eight `handgrenade_0N.wav`,
`loopCount 1`, `is3dSound 1`, `stopType 1`, `volume 1`, `pitch 1`,
`reverbLevel 1`, `pitchEnvelope 0/1/0.9/1.1/1/1/0/0.9/1.1/`,
`minDistance 9`, `halfVolumeDistance 20`, `soundRadius 1`.

`Common_server.zip:Material/ImpactSounds.con` lines 505–518,
`S_Impact_Grenade_Dirt`: five `Shell_impact_0N.wav`, `volume 1`, `pan 0.5`,
`minDistance 4`, `halfVolumeDistance 7`,
`pitchEnvelope 0/1/0.9/1.1/1/1/0/0.9/1.05/`,
`volumeEnvelope 0/1/0/1/1/1/0/0.95/1/`.

`Settings/Sound.con` (mod root, not an archive): `max-distance-cutoff 0.015`
(line 69, "the linear gain where playing sounds get a negative bias in the
priority. They may also be muted"), `distance-delay-threshold 5` (62),
`distance-delay-factor 1` (64, "1 means 1/300 s/m").

## Envelope text — Linux parser

| Address | What |
|---|---|
| 0x766150 | `SoundObjectTemplate::setPitchEnvelope(string)`: istringstream, header, resize points to count, read each point, then ISoundTemplate +0x78 (Linux) `setPitchEnvelope(f,f,f,f,bool,uint,points)` |
| 0x7658c0 | `operator>>(istream&, SoundEnvelopeHeader&)`: float +0, float +4, float +8, float +0xc, bool +0x10, uint +0x14; one `char` read after each |
| 0x765970 | `operator>>(istream&, SoundEnvelopePoint&)`: float +0, +4, +8; one `char` after each |

So the grenade text is header (xMin 0, xMax 1, yMin 0.9, yMax 1.1,
randomize true, 1 point) and point (x 0, low 0.9, high 1.1). Stream-failure
handling of a short text is **not established**.

## Envelope object — BF2Audio.dll

Template slots +0x28 `setVolumeEnvelope` → **0x100176f0**, +0x3c
`setPitchEnvelope` → **0x10017760**. Without points the template keeps no
envelope (+0x34 / +0x38 null). Otherwise a 0x38-byte object, constructor
**0x100194e0**, vtable 0x1003b2a0:

| Offset | Size | Field |
|---|---|---|
| +0x04 | 4 | xMin |
| +0x08 | 4 | xMax |
| +0x0c | 4 | yMin |
| +0x10 | 4 | yMax (also the value of an envelope with no points) |
| +0x14 | 1 | randomize |
| +0x18..+0x24 | 16 | point vector (12-byte points: x, low, high) |
| +0x28..+0x34 | 16 | value table (float per point per row) |

**0x10019260** (build): swap xMin/xMax and yMin/yMax into order; clamp each
point's x to [xMin, xMax], low and high to [yMin, yMax]; sort (0x10019f00);
rows = `randomize ? 64 : 1`; row 0 = every point's low; rows 1..63 =
`(high − low) · rand() · (1/32767) + low` (1/32767 = 0x1003b298), one rand()
per point per row, row-major.

**0x10019130** (evaluate x, row): no points → yMax. Row = `row & 0x3f` when
randomized, else 0. x ≤ first x → first value; x ≥ last x → last value;
otherwise the first point i with x < x_i: when |x_i − x_{i−1}| ≤ 2^−23
(0x10039fd8) the value at i, else linear between i−1 and i.

## Pitch and volume of a playing sample

**0x10006880** (schedule): one rand(); low byte → request +0x14, next byte →
+0x15. **0x10015660** sets x, the play position in [0, 1) of the current
loop; at a wrap both bytes are incremented (next row). **0x100134d0**
(per-update source parameters) calls:

* **0x100161a0** pitch: `getPitch` (template slot +0x38) × envelope(x,
  byte+0x14 + floor(x)) when a pitch envelope exists, x replaced by the
  pitch evaluator's value when one is bound (+0x20); clamped to
  **[0.3, 2.0]** → `AL_PITCH` (0x1003).
* **0x10016100** volume: `getVolume` (+0x24) × envelope(x, byte+0x15 +
  floor(x)) × volume-group level; the result is multiplied with further
  factors (0x100042b0, 0x10013130, a reverb/effect term from slot +0xb4)
  into `AL_GAIN` (0x100a). Those further factors: see below.

## Distance — BF2Audio.dll

OpenALProvider init calls `alDistanceModel(0xd002)` =
`AL_INVERSE_DISTANCE_CLAMPED`. **0x10013000** sets `AL_REFERENCE_DISTANCE`
(0x1020) = `getMinDistance` (slot +0x5c) and `AL_ROLLOFF_FACTOR` (0x1021) =
template +0x18 (0x10015fa0). `AL_MAX_DISTANCE` is not set there.

**0x10015fc0** recomputes after `setVolume` (+0x20 → 0x10016420, clamps to
[0, 1] into +0x28), `setMinDistance` (+0x58 → 0x10016480, +0x0c) and
`setHalfVolumeDistance` (+0x60 → 0x100164a0, +0x10):

1. minDistance < 0.01 (0x1003a80c) → 0.01.
2. halfVolumeDistance ≤ minDistance + 0.01 → halfVolumeDistance = 0,
   rolloff 1.
3. else h = volume · 0.511872 (0x1003b118, 0x3f030a0b); if h > 2^−23,
   rolloff = (1/h − 1) · min / (half − min), else 1.
4. +0x14 maxDistance = (1/(cutoff·rolloff) − 1/rolloff + 1) · min, cutoff =
   engine property 6 (`max-distance-cutoff`, 0x10004490(…, 6)): the distance
   where the inverse-clamped gain equals the cutoff.

So the gain at halfVolumeDistance is 0.511872 × volume. Grenade start:
rolloff 0.7802, maxDistance ≈ 766 m. Dirt impact: rolloff 1.2715,
maxDistance ≈ 210 m.

Template constructor **0x10017560** defaults: +0x0c minDistance 1, +0x10
halfVolumeDistance 0, +0x24 1, +0x28 volume 1, +0x2c pitch 1, +0x30 0.5,
+0x64 global effect level 1, cone angles 360.

Engine property defaults (settings constructor **0x100072b0**, value stored
at `+0x0c + 4·index` before its name): 6 `max-distance-cutoff` 0.02,
4 `distance-delay-threshold` 5, 5 `distance-delay-factor` 1. Sound.con
overrides the cutoff to 0.015.

More template slots: +0x18 `setLoopCount` → 0x10015b50 (+0x08, default 1),
+0x34 `setPitch` → 0x10015b80 (+0x2c, below 0.01 → 0.01, default 1),
+0x68 `setIs3dSound` → 0x10015c40 (+0x1c, default 1).

## Distance delay — 0x10006880

A request whose sound is 3-D (template slot +0x6c, via 0x10015610) and
whose sound object +0x28 is above 0 waits
`floor((d − threshold) · factor · 0.002915452 · 30)` ticks past
`distance-delay-threshold`; 0.002915452 = 1/343 s/m (0x1003a4ac), 30 ticks/s
(0x100397d8). Sound object +0x28 is the play's loop count: every play entry
(0x10016e10, 0x10017080, 0x100170d0, 0x10017130, 0x10017180, 0x100171e0,
0x10017230, 0x10017290) stores either its argument or the template's
`getLoopCount` (slot +0x1c) there; the constructor 0x100165b0 sets 0. A
looping sound (count 0) is not delayed. Ported: `soundDelayTicks`.

0x10005d80 (start candidate), sample byte +0x18 = has had a source: a
never-started finite request more than 60 ticks (0x3c) past its due tick is
dropped; otherwise its due tick becomes now. A started (stolen) one compares
`now − due` with its length (0x10015620 → 0x10016d40:
`round(frames(+0x38) · loops · 30/rate(+0x54) · pitch(+0x2c))`, 0x7fffffff
looping) and is dropped once it reaches it. The start position is the
file's loop-start (+0x40) or 0; the WAV/OGG loader 0x1001ad00 starts both
loop points at 0, so a file without loop points always starts at frame 0.
Ported: `soundStillDue`, `soundLengthTicks`; a pending request is retried
every frame.

## Priority past max-distance-cutoff — 0x10005510, 0x10007140

Every update the pending and the playing lists get a priority (sample +0x10):
2 for a 2-D sound; for 3-D, `volume(x = 0)` (0x10016100) divided by
`1 + (d − min)·rolloff/min` when d > min, less 1 when also d ≥ maxDistance
(0x10015fb0). Both lists are sorted (0x1000cf00); 0x10007140 starts pending
samples while sources are free (0x10005d80), then while the best pending
priority beats the worst playing one (equal: the later request wins) it
stops the playing one (0x10006ba0: re-queued 5 ticks on when more than 15
ticks of it remained, else released) and starts the pending one. So a sound
past the cutoff is not muted; it loses its source first. Ported:
`soundPriority`, `soundSteals`, `soundRequeues`.

## Source count — BF2.exe 0x4b1420, BF2Audio.dll 0x10004c60

BF2.exe's audio-settings apply (0x4b1420) calls the sound wrapper's init
(vtable 0x93c028 slot +0x4c, 0x7de290 → engine slot 0, 0x10006fa0) with the
quality table 0x8a3560 {2, 1, 0, 0}, the provider name and voices =
0x8a3550 {16, 32, 32, 128} by the `setSoundQuality` index (names
registered at 0x4b1bc5: Low 0, Medium 1, High 2, index 3 at 0x8a38c4),
replaced by `setForceNumberOfVoices` when above 0. 0x10004c60: 0 → 16,
under 2 → 2, plus `min-free-sources` (property 32, default 4), capped at
the driver's sources − 1 (0x10011eb0 allocates up to 0x80 to count them,
"Max number of voices: %u"); the result bounds the sources allocated
(DAT_10048dd4 via 0x100156f0). The retail profile (`Audio.con` in
`evidence/retail-captures/m4-completion-20261121/retail-binding-launch/`)
has `setSoundQuality "Low"` and no forced count: 20. Ported: `soundSources`.

One pool for every sample: 0x10007140 walks the engine's whole playing list
for the lowest priority, whatever object a sample belongs to. Ported:
`SoundVoices` (`src/app/src/sound_voices.cpp`), one per `WorldView`, claimed
by every `SmokeFlare` (explosions, impacts, material-cell sounds, the smoke
flare) and by the tinnitus sample (2-D, priority 2). A claim that takes a
source pauses the loser's stream at once; the loser sees it in its next
update and is re-queued by `soundRequeues`.

The other game sounds claim from the same pool (`WorldView::voices`, handed
to `InfantryView::setVoices`):

* `CombatFeedback` (weapons, reloads, footsteps, stance, water, the hit and
  death cues) at priority 2: it plays them without a position, so 2-D
  (0x10005510). Its streams share one device, so a steal unbinds the stream
  instead of pausing (`SoundVoices::attach(…, bound)`). A stolen one with
  more than 15 ticks left (`soundRequeues`) waits `kRequeueTicks` and asks
  again each frame while `soundStillDue`, resuming at the frame the play has
  reached by then (`CombatFeedback::releaseFinished`); else it is dropped.
  Its own cap of 12 streams applies only without a pool.
* The infantry impact `SmokeFlare`s (`InfantryView` dirt, hard, per-material).
* Vehicle engines (`WorldView::drawVehicle`): Retail's `EngineSound`
  (`engine-sound.md`); Rpm1 and Rpm2 each claim at `soundPriority` of
  their level (template volume × evaluator scale × volume envelope at the
  engine's x) and distance. Looping, a stolen one is re-queued after
  `kRequeueTicks` (0x10006ba0; endless length). Its inputs (rpm, load,
  gear) are not ported (docs/debt.md).

Ambient sounds and the wind (`sound.windSetup`) are not played by ours; menu
music and VOIP are outside the game's sound engine and stay apart.

## File choice — 0x10016a60

n = min(file count, template maxSamples[quality] at +0x90 + 4·quality,
quality from engine slot +0x0c). n ≤ 1: file 0. Otherwise `rand() % n`,
drawn again while it equals the sound object's previous index (+0x60,
constructor 0x100165b0 sets −1), which is then stored. Ported:
`soundFileIndex`, with a new sound object per bundle instance. maxSamples is
left at its constructor default 0x7fffffff; what BF2.exe sets from
`lowSamples` is **not established**, nor whether BF2.exe reuses one sound
object for successive explosions.

## rand() seed

BF2Audio.dll and BF2.exe both import `rand` from MSVCR71.dll (BF2.exe IAT
0x87f5d8; `srand` at 0x87f5d4). BF2.exe calls `srand` at 0x669407, 0x66f0cf,
0x5b5d7d and 0x66ab57 with the result of 0x67cc40, at 0x5c00c3 with an
import's result XOR 0x33333333, at 0x4cea9e with a rounded float: a clock,
not a fixed seed. Ours seeds the Visual C++ `rand()` sequence from
`SDL_GetPerformanceCounter`. MSVCR71 keeps the seed per thread; which thread
seeds and which plays is **not established**.

## Further gain factors in 0x100134d0

* Volume group and master: 0x10016100 multiplies the group level (engine
  slot +0x28 with template `getVolumeGroup`), then 0x100134d0 multiplies
  engine +0xac (2-D) or +0xb0 (3-D) and clamps to [0, 1] (0x100042b0).
  Ours: the effect volume setting (`setVolume`).
* Obstruction, 0x10013130: without EAX, a 3-D sound gets
  `clamp(1 − (listener-3d-obstruction + o)·listener-3d-obstruction-factor)`,
  properties 28 (default 0) and 29 (default 0.85), o the object's
  obstruction (evaluator at sound object slot +0x58, times template
  `getGlobalEffectLevel`, template +0x64, default 1). The value moves at
  most 0.1 per update toward the evaluator's (sample +0x38), straight to it
  while starting. Ported: `obstructionStep`, `obstructionGain`.
* Tinnitus duck: without EAX, `1 − effectLevel · engine+0x170 · 0.9`
  (0x1003af68). Engine +0x170 is the tinnitus envelope level (below).
  Ported: `tinnitusDuck`.

## Obstruction ray — BF2.exe 0x7df090

The sound object's obstruction evaluator. `sound-listener-radius` (var at
0xa25830, default 0.2, Sound.con 0.15), `sound-obstruction-max` (0xa25158,
default 0.8, Sound.con 1.0), `sound-obstruction-radius` (0xa24498, Sound.con
5). Listener = the camera (wrapper slot +0x24 → +0x30).

1. r = template `getRadius` (slot +0x94), 0.05 when ≤ 0. d = |source −
   listener| − r − listenerRadius; d ≤ 0 → 0.
2. Rays: one when d > 40 (0x8ebba4), else three; offsets dir, a = cross(up,
   dir), b = cross(dir, a) with up = (0,1,0), or (1,0,0) when |dir.y| ≥ 0.99
   (0x8f7b70); each from listener + offset·listenerRadius to source +
   offset·r (the dir ray ends r short of the source).
3. The first clear ray → 0, and its index goes to the sound's user data 1.
   With `sound-obstruction-opt0` on, the rays start at that index: i from
   the stored index (0 when there is one ray) to index + count, casting ray
   i % count — so one ray with a stored 2 is cast three times.
4. All blocked: if user data 0 (the sound's own object) is set and its root
   (`getRootParent` 0x71ae20) is the hit object's root → 0 (0x7df620–0x7df644:
   `getUserData(0)` on the sound, ecx the hit from intersectLine's first
   out-argument at ebp−0x3c). Else `clamp((1 − (atan(h/R) + atan((d −
   h)/R)) · (1/π)) · max, 0, 1)`, 1/π at 0x913304, h the hit distance read
   from the first ray's hit slot.
5. With `sound-obstruction-opt1` on: a call whose user data 2 has its high
   16 bits above 0 counts them down and returns the low 16 bits × 1/65535
   (0x920c3c, 0x37800080) before any ray; else user data 2 is cleared, and
   after the rays (also after the own-object 0) set to round(o · 65535)
   (0x8fcbf8) | n << 16, n = min(round(e^(0.01 · min(d, 1000) + 1)) +
   reuse-frames, max-reuse-frames); 0.01 at 0x892adc, 1000 at 0x8aa3c8.

The variables (registered in the static initialisers at 0x86d31f–0x86d3ff,
name pointer in ecx, default in edx or pushed; none set by Sound.con):

| var | address | name (string at) | default |
|---|---|---|---|
| listener radius | 0xa25830 | `sound-listener-radius` (0x93c544) | 0.2 (Sound.con 0.15) |
| opt0 | 0xa25b08 | `sound-obstruction-opt0` (0x93c590) | 1 |
| opt1 | 0xa25bc0 | `sound-obstruction-opt1` (0x93c5a8) | 1 |
| disable | 0xa2449c | `sound-obstruction-disable` (0x93c5c0) | 0 — on, the evaluator returns 0 first |
| reuse | 0xa25a44 | `sound-obstruction-reuse-frames` (0x93c5dc) | 0 |
| cap | 0xa249f8 | `sound-obstruction-max-reuse-frames` (0x93c5fc) | 15 |

The sound's user data (ISound slots +0x64 `setUserData`, +0x68
`getUserData`: Linux `NullSound` vtable) is set when the sound wrapper creates
the sound (0x7de130: 0 = the object the sound is created for, 1 = 0, 2 = 0;
the obstruction evaluator, wrapper +0x10, is set only for a 3-D template,
slot +0x6c). An effect bundle creates its sound for itself (Linux
`EffectBundle::EffectBundle`, 0x594e96: `createSoundFromTemplate("Start",
this)`).

The query: DAT_00a08f58 is the `ObjectManager`; slot +0x68 is
`ObjectManager::intersectLine(obj&, pos&, normal&, material&, start, delta,
predicator, b1, b2, b3, b4, mask)` (Linux vtable, msvc +0x68; Linux
0x696030). The sound passes (predicator 0x93c12c, b1 = near, true, false,
true, 0); near is the three-ray case.

| argument | in 0x696030 | for the sound |
|---|---|---|
| static culler (+0x20) | always searched | static objects |
| b1 | also the dynamic culler (+0x10); the constructor 0x690e60 names +0x20 "static culler", +0x10 "dynamic culler" | dynamic objects only on the near rays |
| b2 | to `intersectLineWithObject` 0x6958f0 → `CollisionMesh::getDistance` 0x7187a0 → `CollisionMeshTemplate::getDistance` 0x71bee0 → `Bsp::getClosestFace` 0x709b20 as its last bool: return the first face a leaf accepts (a file tree is a `BspV8`: 0x70ff70, context +0x2a; docs/functions/collision-bsp.md) | first face of a mesh |
| b3 | the heightmap test (`heightmapCluster` slot +0x120) | **no terrain** |
| b4 | the heightmap's own bool; with b3 false unused | — |
| mask | compared with the root's (and the object's) +0x28 slot +0x20 | 0 |
| lod | `getOverrideLineTestLod`, ObjectManager +0x2b0, −1 in the constructor → 0 | **Projectile** collision (`ColType` 0) |

Inside the mesh test, getDistance's own bools are (false, …, true, b2):
0x71bee0 passes `!true` = false as the flip (face normals not negated,
`BspV8` context +0x28) and false to
`checkFaceAndEdgeCollision` 0x724ae0 (context +0x29): the ray counts only
running against the normal — one-sided, as `CollisionWorld::segmentContact`
already tests. Of the objects, the nearest hit wins (0x696030 keeps the
smallest distance). A soldier would go through `checkRayVsSkeleton`, but the
predicator rejects soldiers first.

The predicator (vtable 0x93c12c): slot 0 (0x7df780) rejects an object whose
template class is `CID_SoldierTemplate` (0x9493, read from 0x91c240), one
without flag 0x100 (byte +5 bit 0; slot +4, 0x7df7c0, returns that mask
0x100), and one whose root (`getRootParent` 0x71ae20) is the predicator's +4
— the sound object's +0xcc object (0x45e060), its owner. Slot +0xc
(0x5a1860) accepts every material.

Flag 0x100 is `ObjectTemplate.hasCollisionPhysics` (Linux, object flags at
+8, msvc +4; `ObjectFlagPredicator::predicate` 0x68c4b0 tests `flags & mask
== mask`): `SimpleObject::SimpleObject` calls `updateFlags(0x100, 0)` when
the template's +0xe8 bit 2 is on (0x58a50c → 0x58a6e5), the bit
`SimpleObjectTemplate::makeScript` writes as `hasCollisionPhysics` (0x58d04b,
string 0xb5ccf0; bit 1 is `hasMobilePhysics`, 0x5077a0). The template
constructor clears it (0x58ba4d), `DynamicBundleTemplate`'s sets bits 1 and 2
(0x57ccb5); no other template constructor touches the byte. Run-time changes:
`DestroyableWindow::setIsBroken` (0x515aa9/0x515af5), `Ladder::attachSoldier`
(0x52a084), `GrapplingHookRope::initContainer` (0x51ce60), `Kit::init`
(0x526e0c, clears it). The +0xcc owner is, as far as the code above shows,
the same object as user data 0; that they are one is inferred, not read.

Ported: `soundObstruction` with `SoundRay(…, dynamic)`;
`SmokeFlare::obstructionTarget` asks `RemoteWorld::projectileCollision` — a
second `CollisionWorld` of every placed object's Projectile lod
(`buildCollisionWorld(…, ColType::Projectile)`), with the spawned vehicles
as its movables, searched only on the near rays (`segmentContact(…,
movables)`); no terrain. Dalian Plant (`object_info --level-collision`):
478 831 Projectile triangles from 1224 objects against 56 514 soldier ones,
built in 238 ms; a 40 m ray costs 0.25 ms in a debug build. Flag 0x100:
the sound's world takes only pieces whose object `hasCollisionPhysics`
(`buildCollisionWorld(…, collisionPhysicsOnly)`, `collisionPhysicsPieces`
for the vehicles; class default on only for DynamicBundle). On Dalian Plant
every collidable piece sets it (all 1224 objects and 478 831 triangles stay;
the LAV-25's every part, `object_info --collision usapc_lav25`); the run-time
flag changes are not ported. User data 1 and 2: `SoundObstructionMemory`, one
per `SmokeFlare` instance (a new sound object per bundle instance), with
opt0/opt1 on and the evaluator called once per sound update so the cache
counts as Retail's does. The owner exclusions (predicator +4 and step 4's
user data 0) concern the sound's own object; for the sounds ours plays
through `SmokeFlare` that is an unattached effect bundle, which has no
collision, so neither can fire, and `SoundRay` does not return the hit
object. Not ported: the BSP's own face order (ours takes a mesh's nearest
face). Corrected: an earlier note here read step 4's test as the local
player's own vehicle; 0x7de130 shows user data 0 is the sound's object. A networked
destructible static stays in our static grid; which culler Retail keeps it in
is not established.

## Tinnitus — BF2.exe 0x5cd940, 0x7df93e; BF2Audio.dll 0x10004760, 0x10004830, 0x10004ce0

BF2.exe 0x5cd940 (explosion feedback) triggers it (wrapper slot +0x74,
0x7de490) when the local player's object is a soldier on foot and the
blast is within explosionRadius × `sound-tinnitus-range` (var 0x9ee900,
default 0.6, Sound.con 1.05). The trigger passes the four values that
`sound.tinnitusSetup` stored (0x7df93e: attack, hold, release, depth;
defaults 0.5, 2, 2, 0.8 at 0x7ddfb7; Sound.con 1 2 4 0.88).

0x10004760 builds the sound from the setup's files (first:
`Common/Sound/tinnitus.wav`): 2-D, loopCount 0, stopType 0, reverb 0,
global effect level 0. 0x10004830: times under 0.1 → 0.1, depth clamped to
[0, 1], volume envelope (0,1,0,1, no randomize, 4 points) 0 → 1 → 1 → 0 at
0, attack/T, (attack+hold)/T, 1 (T the sum), length max(20, round(T·30))
ticks, its volume evaluator bound to engine +0x16c. 0x10004ce0 each update:
x = (now − start)/(end − start) into +0x16c, engine +0x170 = envelope(x) ·
depth; the sample stops once that is ≤ 2^−23 after the start. Ported:
`Tinnitus`, `TinnitusSound`; the man-down file (second in the setup) is not
used here.

## Following the play

Ours streams the file in pieces, 0.25 s ahead: each piece is attenuated
(inverse-clamped gain) and panned for the listener of that frame
(`SmokeFlare::listen`). Every frame `SmokeFlare::follow` takes x =
played/total frames and sets the stream's frequency ratio to the pitch
(0x100161a0) times the Doppler factor and its gain to the volume
(0x10016100) × tinnitus duck × obstruction gain at that x. Reverb is **not
ported**.

## OpenAL's source parameters — BF2OpenAL.dll 0x100464d0

Retail's OpenAL is BF2OpenAL.dll (SHA-256 in `soldier-sound.md`), whose
software mixer (0x10047570, `audio-output-boundary.md`) asks 0x100464d0 for
each playing source's channel gains and pitch. Dumped whole with
`DecompileAll.java`.

The listener: BF2Audio.dll never sets its position (origin) and sets its
orientation once, at (0,0,−1) / (0,1,0) (0x100118f0); it moves every source's
position and velocity into the listener's space itself (0x10005300 builds the
matrix at engine +0xc4 from the listener's at/up, 0x100050f0 applies it, z
negated for OpenAL), and gives the listener velocity in that space through
0x10011650, scaled to 300 when longer (0x1003aa18, 300² at 0x1003aa1c).
Source positions with a component of 20000 or more go to 20000 (0x1003af70),
source velocities with a component of 300 or more go to 0 (0x100116f0).

For a mono source (every 3-D sound) in a stereo context:

1. relative = source − listener (no AL_SOURCE_RELATIVE: BF2Audio sets
   none); distance gain by the model (BF2Audio sets
   AL_INVERSE_DISTANCE_CLAMPED, 0xd002, 0x100118f0; the context default is
   the same, alcOpenDevice 0x10036056), times the source gain, clamped to
   [min gain, max gain].
2. Cone gain (no cone set: 1).
3. Doppler, when the factor is not 0: u = normalize(−relative), vl =
   u·listener velocity, vs = u·source velocity, each set to SS/DF − 1 at or
   above SS/DF and to 1 − SS/DF at or below −SS/DF; pitch × (SS − vl·DF) /
   (SS − vs·DF), SS = speed of sound × doppler velocity. Context defaults:
   DF 1 and doppler velocity 1 (`fld1` at 0x10036012), speed of sound 343.3
   (0x1004cd70); BF2Audio.dll calls only alDopplerFactor (loader 0x10003180,
   no alDopplerVelocity or alSpeedOfSound), from `sound.dopplerEffect`
   (BF2.exe registers it at 0x7dacf0, a float; Settings/Sound.con line 73
   sets 1), clamped below at 0 (0x10004710) and passed by provider slot +0x24
   (0x10011570). So DF = 1.
4. The pan: relative in the listener frame (right = normalize(at × up), up,
   −at), normalized; p = (x + 1)·0.5 (0x1004b27c), 0.5 at distance 0. Left
   gain √(1 − p), right √p, each times the cone, distance and listener gains.
   With four channels the z term splits front and back.

A 2-D sound stays at (0,0,0) (0x10014260 sets position and velocity to 0
and the direction to (0,0,−1) at the start): distance 0, p = 0.5.

The listener's velocity in BF2.exe: the sound wrapper (DAT_00a23dc0, vtable
0x93c028) slot +0x18 (0x7de9a0) passes, while the local player
`hasFirstPersonView()` (Player msvc +0x184), the positional speed
(PhysicsNode msvc +0x5c, `getPositionalSpeed`) of the root of
`Player::getVehicle()` (msvc +0x44) — the soldier on foot, the vehicle when
seated (its root) — when that root has a physics node (+0x44), else 0, to slot
+0x14 (0x7deae0), which stores it at +0xb8 and hands it to the engine's
`setListenerVelocity` (engine slot +0x40, 0x10004670). The other caller,
0x4d5971, passes 0.

The source velocity: zero at the start (0x10014260), then 0x10015a30 asks
the sound's parent node (sound +0x10) slot +4 and, when that answers null,
uses the DLL's default vector 0x10046464. An effect bundle is its sound's
parent (Linux `EffectBundle::EffectBundle` calls the sound's `setParent`,
slot 0x78, with its node at +0x238, vtable 0xb5e9b0: getTransform,
getVelocity), and `EffectBundle::getVelocity` (Linux 0x592d80) returns null.
So an effect's sound has no velocity of its own. The BF2.exe copy of that
node was not located; the Linux one is cited.

Ported: `soundPan`, `dopplerPitch`, `soundListenerVelocity`; `SmokeFlare`
pans each queued piece with `soundPan` and multiplies the pitch by
`dopplerPitch` every frame. The listener velocity is
`RemoteWorld::soundListenerVelocity`: the predicted body's velocity on foot;
seated, the newest networked velocity of the vehicle's hull (the controlled
hull's track while driving, the vehicle ghost's otherwise), 0 until one
arrives. `Player::hasFirstPersonView` (Linux 0x4985f0): false unless the
player is the local one (`playerManager` slot 0x60); with neither +0xa9 nor
+0xa8 (free camera) set, the player's camera (`getCamera`, slot 0x300);
otherwise the first `IPlayerControlObject` up from `getVehicle()` (slot
0x90, parents at +0x50) and its camera (interface slot 0x88); true when that
camera's `getViewMode` (ICameraObject slot 0x38, the `Camera` vtable's
sub-table at +1168, entry 0x57c2e0) is 1 (CVMInside) or 2 (CVMNose,
`vehicle-camera-modes.md`). Ours: seated, the seat camera's mode
(`WorldView::cameraViewMode_`); on foot the soldier camera's mode is not
modelled and is taken as Inside. The
source velocity is 0, as above. The mixer's 16-bit linear interpolation and filters are not ported; SDL resamples.
