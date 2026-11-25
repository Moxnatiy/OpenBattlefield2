# Soldier movement sound — Retail 1.50 client

Observed in the installed `BF2.exe`, SHA-256
`5a72e2396a441201a07b7a743643c04e750784673fdbc126a909ce2582c5f6a1`,
through Ghidra 12.1.4 / REA. Complete dossiers are local under
`build/retail-captures/footsteps-resume-20261120/rea-session/`. Third-party
source supplied search hints only; these notes come from the client dossiers.
Unresolved virtual-interface identities are stated below rather than guessed.

## Material change, 0x7e2e00 (208 bytes)

Evidence `ev_3e5853c1a8e44eed5fd56ab182cf3bf2793ec1c92a2e7a491e2e877a5a7162a7`.
The requested material is compared with cached `+0x2c`; an equal request returns.
Otherwise release and null all four sound pointers at `+8,+12,+16,+20`, store
the new material, and process slots 0 through 3 in order. MaterialManager global
`0x9c6c88`, virtual `+0x64`, is called with `(5500, material, slot)`.

An absent slot 0 remains null. An absent nonzero slot aliases slot 0, adding a
reference when nonnull. A present template invokes its `+0x60` component's
virtual `+0x78` with the owning soldier (`this+4`), then sound global `0xa23dc0`
virtual `+0x1c` constructs the sound. A nonnull result gets parent virtual
`+0x3c`, volume evaluator virtual `+0x44` and pitch evaluator virtual `+0x4c`;
the evaluator index is 0 and both evaluator pointers are `this+0x30`.
Final spatial/evaluator/mixer behavior is **not measured**.

## Constructor, 0x7e2920 (379 bytes)

Evidence `ev_729d5a5ad07a75a47f83448c635a25a1545277c97522870102fc2adf55ce9a0a`.
The parent vtable is **0x93c8e8**, owner +4, cached material −1, evaluator 0,
half-cycle latch **true**, drowning byte +0x35 false. Slots 0..3 are null.
The named sounds `Death`, `Revive`, `Swim`, `Swim_Sprint`, `Swim_Still` populate
slots 4..8, with parent attachment on created sounds. Death's volume and pitch
evaluators both reference +0x30. The read-only probe pins the executable hash,
locates this vtable in private writable memory and requires exactly one valid
SoldierSound on the zero-bot server; it retains only the verified owner/material/
evaluator/latch fields, never a whole memory dump.

## Sound update, 0x7e2f90 (675 bytes)

Evidence `ev_dfdd82f4c44c98372cafd92f825460e745d4f27bb19b08e2548aaede4da0ed03`.
Object fields used by this entire function:

| Offset | Use |
| --- | --- |
| +4 | Owning soldier |
| +8..+20 | Four material sounds, slots 0..3 |
| +0x28 | Stationary swim sound; swimming is outside this pass |
| +0x2c | Cached contact material |
| +0x30 | Full 3D velocity magnitude used as sound evaluator |
| +0x34 | Previous half-cycle latch |

The optional output material is initially −1. An opening vehicle/occupancy
branch walks the soldier's `+0x154` interface virtual `+0x10`, that object's
`+0x44`, and the result's `+0x14`; interface query ID `0xc4c4`, type ID `0x9493`
and a virtual `+0xf4` float test against float epsilon can invoke sound-global
virtual `+0x88`. Its audio meaning is **not measured**.

The movement branch requires an occupying player and either no parent object
or the soldier's `+0x15c` interface virtual `+0x7c` to be nonzero. It obtains
movement duration/time from **0x61e790** and full velocity magnitude from
physics-interface virtual `+0x148`. With a response object present, pose is
queried through the soldier's `+0x15c` interface virtual `+0x38`.

Pose 3 or response byte `+0xfa` permits material processing; otherwise an
unattached/non-ladder soldier returns. Nonzero `+0x7c` routes to material
**6600**; otherwise virtual `+0x5c` supplies the last-contact material.
The embedded interface is at soldier +0x15c (not a pointer stored there).
Its +0x5c getter **0x5ab320** is `mov eax,[ecx+0x198]; ret`, hence reads
root +0x2f4. The call at **0x7e3103..0x7e310e** supplies that embedded
interface address (`material-getter-bytes.json`).
Call **0x7e2e00** to update the sounds.

Compute `half = time < duration * 0.5`. For speed **at least 0.1**, only a
change from the previous half latch enters cue dispatch. Set the optional
output material, then select:

| Pose | Sound slot |
| --- | --- |
| 0 standing | 3 when occupying-player interface virtual +0xf8 is true; else 0 |
| 1 crouch | 1 |
| 2 prone | 2 |
| 3 swim | 7 when +0xf8 is true; else 6 — not implemented/measured here |
| other | no cue |

For dry movement, a nonnull selected sound invokes virtual **+0x14**. The
previous half latch is updated even when the selected sound is missing or
pose is unsupported. A low-speed dry soldier does not emit. The low-speed
swimming branch instead tests the same latch change and uses `+0x28` with
position helper **0x7e2f60**, invokes sound virtual `+0x1c`, then updates the
latch. Higher-speed swimming uses position-based playback for slots 6/7.
Water behavior, spatialization and mixing remain **not measured** and out of
scope. This function does not independently play a dry stance-change cue;
that observation does not exclude other callers/event paths.

## Movement duration/time, 0x61e790 (385 bytes)

Evidence `ev_a8e2f25821f1ce6c6e2fc546f69e5f6017822acbbc0623f2f8c264bdb35e32f8`.
Initialize both output floats to zero; a nonzero soldier `+0x254` leaves them
zero. Otherwise start with animation system at `+0x28c + index*8`, index
`+0x2b8`. Index 1 with an attached object (`+0x208`, its `+4`) may use interface
ID `0xc49e`, set its system index (virtual `+0x18`) and get the system (virtual
`+0x1c`). Index 0 may instead use the selected item through `+0x158` virtual
`+0x14`, provided its flag bit 0 is clear and the same interface is present.
A negative duration restores the soldier system on that path.

On a nonnull chosen system call virtual **+0x3c** with `(duration,time,true)`.
Negative duration falls back to zero duration/time when interface predicates
`+0x7c`, `+0x8c` and `+0x84` are all zero. Otherwise duration becomes **0.9**
and time comes from the world clock modulo that duration. Those fallback
predicates concern unsupported movement contexts; do not apply the 0.9 value
as dry-land footstep cadence.

## Animation movement-time getter, 0x7f6b60 (135 bytes)

Evidence `ev_dd6ecb2454340a700280891b3e6f9a6e6ee893534607c748aaa23780f85b9f0d`.
The animation-system vtable begins at **0x940d80**; its +0x3c entry points here
(`animation-vtable-bytes.json`). Initialize duration/time to **−1**. Walk the
player list at system `+0x60` **backward**: start at the sentinel, then follow
node `+4` before checking player `+8`. In contrast, `playBundle` **0x7f7a20**
searches forward through node `+0`. This reverse query selects the last
movement player in ascending presentation order, including a fading player.
Select the first player encountered, or, with the third
argument true, the first with player byte **+0x36** set. If a selected player
exists, speed is player **+0x38**, length is its bundle's **+0x1c**, and current
time is player **+0x10**. Positive speed returns `(speed*length, speed*time)`.
Nonpositive speed returns `(abs(speed)*length,
abs(speed)*length - abs(speed)*time)`. Zero speed yields zero/zero. The
half-cycle test therefore preserves playback phase, including reverse playback.
**0x7ff490**, evidence
`ev_00c7e015c079210a482c5269c9f2eebbce1ce52b0f53c7eaa62ec5ec56a096ce`,
marks player +0x36 on the normal movement-trigger branch only when the bundle
loops. The no-holder two-bundle foot-choice branch returns before marking it.
The player constructor **0x7fe190** initializes +0x34..+0x37 false
(`ev_c484deba1d27bc8990f902666770bd67c8054366d9e6cc39f1874de1e5479508`).

## Primary terrain material access

`terrain-material-grid-read.json`, **0x6f3c80**, 159 bytes, evidence
`ev_9587ab685eda7329a621bf21635bef1fa1a24554ec6569b334595b5862a95618`:
missing material pointer +0xd4 returns 0. Grid x/z are offset by +0x3c/+0x40;
unsigned bounds compare against width +0x30 and height +0x34 (negative indices
also fail), returning 0 outside. In range, material byte index is
`((width * z * materialScale) + x) * materialScale`, scale +0xd0. It checks
registration through MaterialManager virtual +0x28: if material 0 exists but
the selected ID does not, the function traps; a defined material whose virtual
+0x60 returns true also traps. These are assertions, not fallback selection.
For defined dry materials 8/4 the raw byte is returned.

**0x6f4770**, 85 bytes, evidence
`ev_08cb0405d7243535781363ed91b946db14f713c0b2d49b1b8dde0f937c12882c`,
converts world x/z by subtracting integer map origins +0x28/+0x2c and multiplying
inverse x/z scales +0x50/+0x58, then calls virtual +0x78 (0x6f3c80).
Instructions at **0x6f4784** and **0x6f47a0** set x87 rounding toward zero
(`OR AH,0xc`) before FISTP; the decompiler's `ROUND` is not nearest rounding.
The Heightmap vtable is **0x9186b0**, with grid material at +0x78 and world
material at +0x7c. The existing centered primary-height grid is also the
material grid; materialScale=1 indexes the corresponding raw byte.

**0x6f3d20**, 53 bytes, evidence
`ev_8a58c0d04e452281c38ad9ae0c271df651786cbe1616939f01984254840dd7c5`,
writes a byte using the same origin/scale index if the map pointer exists; it
does not perform the reader's bounds or material assertions.
**0x6f6cf0**, 107 bytes, evidence
`ev_b2835fda66131757eb23807d029c51c346bbfc9024243a07f532ac880e72f398`,
frees the previous map, opens the requested path through the file manager,
allocates width*height*scale*scale bytes, reads that many bytes, then releases
the file. `Heightdata.con` supplies `setMaterialScale` / `loadMaterialData`;
archives are read in place. Secondary terrain material queries remain outside
the scoped primary-map cases. Actual contact precedence over collision meshes
must still be verified on each captured path with the Retail material probe.

## Contact writes and jump/landing

Continuation measurements: `build/retail-captures/jump-landing-20261120/`.
`retail-hard-preflight/contact.csv.layout.txt` reads the runtime float pointers:
0x9f4a44 → **−0.5**, 0x9f4a5c → **1.0**, 0x9f4a7c → **2.5**.
Ghidra `jump-setting-initializers.json` records defaults at **0x85afd0**
(PUSH 1.0, name 0x8fa5b8), **0x85aff0** (PUSH −0.5, name 0x8fa5d0),
**0x85b010** (PUSH 2.5, name 0x8fa5e8). Names in `jump-setting-names.json`
are **anim_startLandAtHeight**, **anim_startLandAtSpeed**,
**anim_startJumpAtSpeed**, respectively. These are recovered settings, not
onset-delay fits.

Full dry branch of **0x61ded0**: if landed byte root +0x2b0 is already true,
return vertical velocity without another cue. At vertical velocity ≥ land
speed, a false jumped byte +0x2b1 becomes true only at ≥ jump speed; clear
landed and play jump unless byte +0x2b4 suppresses it. Below land speed, with
positive land height, start the segment at root absolute position minus
**(0,1,0)**. Begin with velocity times land height. If its squared length is
greater than height², normalize and scale to height; if less, first replace
Y by −1, then normalize and scale; equality retains the vector. Query objects
first; query HeightmapCluster only without an object hit. A hit sets landed,
stores hit material at root +0x2f4 and calls **0x7e3260** unless +0x2b4 suppresses.
With jumped false, return velocity Y only when not landed and Y≤0; otherwise
return 0. With jumped true, return 1 while not landed, else 0. The opening
already-landed return is distinct from the frame which first finds a hit.

Caller **0x61e1a0**, evidence
`ev_f290cff7db518c97a59defd7630db9eaa0ac8a712eeb826a71b1a7dc52b80914`,
clears jumped and landed on response +0xfa (ground contact), and also on root
+0x2f0 (predicate identity not established). The helper runs only without an
ordinary parent and without that latter predicate. Physics-component velocity
getter **0x658450** is the complete `LEA EAX,[ECX+0x78]; RET`; it is not the
raw node's pending gravity impulse. `retail-hard-state-1` observes jumped at
input +3.1504 s, landed at +3.7749 s, then both cleared at +4.0115 s: landing
animation starts about **237 ms before ground contact** on the concrete route.

Collision callback **0x5a6810**, evidence
`ev_9885e326a789859f9dbda4f60af2d8023bbee5cb09ca15b8ac9ef9d39734d9b6`,
writes delivered contact material (argument 6) to root +0x2f4, then calls
base callback 0x626f00. This is a retained collision result, not an upward
nearest-surface query. Exact object/terrain precedence beyond the captured
primary-terrain paths is **not measured**.

**0x61ded0**, evidence
`ev_773903df2d7cfbfd7eea77874568342fdbfe56bbc1a330018f897f51cfd05d6e`,
also writes the material on its landing trace. It tries object collision first
and the heightmap cluster only without an object hit. The full dry jump/landing
state thresholds and their capture timing are recovered above; other parent,
water and observer cases remain **not measured**.
Jump helper **0x7e2f00** selects material sound slot 3, falling back to slot 0,
and invokes playback +0x14 for an occupying player without an ordinary parent
(`ev_f7c628d36e214128e02f39e608acf0106ec73c7770885d3f793fdbcde93fb20f`).
Landing wrapper **0x7e3260** calls that helper only after player virtual +0x184
succeeds (`ev_69cad9237cb028703673bbc1efc9a804d2e8589ffe2908c4f8665a39341c129a`).

The probe excludes pooled/unused sound instances using the occupying-player
getter **0x5a6bc0**: embedded interface +0x154, weak reference at +0x14,
player pointer at weak reference +4 (`occupying-player-getter-bytes.json`).

## Bundle events

Revalidated **0x7fe3c0**, 792 bytes, evidence
`ev_604160d75ad58de30f0a24efe7a62813c7aa9b69dd632824d8e9b7dc339c4064`.
The full existing branch notes remain in `animation-system.md`, “Playing a
bundle”. Time-crossed bundle events invoke the owning object's virtual +0x100;
that consumer and jump/landing routing remain **not measured**. This event
path must not be substituted for the separately verified half-cycle footstep
dispatch above.

The event callback is now resolved: soldier constructor **0x5ac3a0** installs
root vtable **0x8e80f8**; its +0x100 entry is **0x5a7570**
(`soldier-animation-event-vtable.json`, evidence
`ev_775c0b724d42985a1a9eaf21bfe9e09c91c13f601dc83ce3627faa44d2bb453c`).
In **0x7fe3c0**, a crossed event descriptor of kind +0x2c == 1 dispatches
descriptor index +0x28 plus **0x36**; kind 2 dispatches index plus **0x2c**.
Soldier event **0x36** invokes jump helper **0x7e2f00** when root +0x2b4 is
zero and the third event argument is zero. That helper's slot 3/fallback 0
selection is recovered above. The literal suffix-to-descriptor parser remains
**not measured**; do not infer every `sound:N_first` mapping from this case.

Other events delegate to base **0x626f70** (evidence
`ev_9d1c78a779946780bb857d73c973689376bd562b33178ab615b33a8df7194c25`),
which forwards to child objects and then **0x628260** (evidence
`ev_1828355cdfec337d4e09ea6fddc303e612d2c33410ec96e84463ebf67c9f8bc7`).
The latter routes event 0x27/0x28 to component-map +0x100 entries' virtual
+0x34. Those terminal component handlers are **not measured** here.

Actual concrete landing evidence is retained separately from dispatch inference:
`retail-hard-short-2/landing-sprint-matches.json` isolates WAV seconds 7.3..8.5
and identifies `Sprint_tarmac_01.wav` at **7.562083 s**, correlation **0.953**.
The later walk-tarmac cue corresponds to an additional half-latch transition
at input +4.045 s, speed 0.831. It is not evidence that the explicit landing
cue uses slot 0. Jump/landing absolute input-to-output timing and first-person
buffering are not interchangeable with the verified movement half-cycle rule.

## Ours

`app/infantry_sounds.h`: `JumpLandingSoundState` (0x61ded0 with its caller
0x61e1a0, the `anim_startLandAtHeight`/`…Speed`/`anim_startJumpAtSpeed`
constants in `server::PhysicsConstants`), `MovementSoundLatch` (0x7e2f90, its
half-cycle latch from `anim::Player::movementTime`, 0x7f6b60) and
`InfantrySounds` (the material's cue, 0x7e2e00). Tested by
`tests/test_infantry_sounds.cpp`. Playing the cues through the voice pool, on
the soldier's terrain material, comes with the infantry view.
