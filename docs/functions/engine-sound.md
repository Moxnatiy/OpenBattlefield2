# Engine sound — `EngineSound`, Retail 1.50

Scope: the sound a vehicle's engine makes (`ObjectTemplate.activeSafe Engine
…`). Code: `src/app/src/engine_sound.cpp` (`EngineSound`).

Status: `EngineSound` and the engine update it reads (`game/engine_update.h`)
are in. `WorldView::drawVehicle`, which feeds it rpm, gear and load below,
comes with the world view.

Binary: Linux server `linuxded-full/bin/amd-64/bf2` (names; the sound code
is compiled in although `createSoundFromTemplate` returns null there).
Ghidra MCP was down for this pass: the constructor was read with REA
(`tools/rea.sh function … 0x7632c0`, evidence
`ev_73f7a6c06ef263c3487b55332ccb9adead87a64392303aeb5f8627366016af5e`), the
rest with `objdump -d` and the constants from `.rodata`. A second look in
Ghidra at `update`, `updateAudioRpm` and `updateSwayEffect` is owed
(docs/debt.md). BF2.exe's copy of these functions is not located yet.

## Game data

`Objects_server.zip:Vehicles/Land/JEEP_FAAV/JEEP_FAAV.tweak`, lines
411–545: `JEEP_FAAV_Engine` adds `S_JEEP_FAAV_Engine_Idle`, `…_Rpm1`,
`…_Rpm2`, `…_Load`, sets `audio.newStyleAudio 1`. Idle is
`start_idle_stop.wav`, `stopType 2`, volume 0.65, pitch 1.4; Rpm1
`rpm_unload.wav`, volume 0.64, pitch 0.35; Rpm2 `rpm_load.wav`, volume 0.72,
pitch 1.4; all three `loopCount 0`, `minDistance 3`, `halfVolumeDistance 6`,
with pitch and volume envelopes over x ∈ [0, 1]. Load is five gear-shift
files, `loopCount 1`, volume 0.4. No template sets `soundPriority`; no
`.con` in the archives or `Settings/` sets an `engine-audio-*`, `rpm-wheel`,
`rpm-k` or `engine-sound-smooth-time` var (searched every `.con`, `.tweak`,
`.inc`, `.py` in `mods/bf2/*.zip`), so the defaults below hold.
`audio.transformationRelativeRoot 1` is set only by helicopters.

## Objects

`EngineSoundTemplate` (inside `EngineTemplate` at +0x6f0), reset 0x7626d0,
serialize 0x763250:

| Offset | Size | Field | Default (Vars) |
|---|---|---|---|
| +0x00 | 1 | `audio.newStyleAudio` | 0 |
| +0x01 | 1 | `audio.transformationRelativeRoot` | 0 |
| +0x04 | 4 | `engine-audio-sway-frequency-first` | 80 (0xb4fa5c) |
| +0x08 | 4 | `engine-audio-sway-frequency-last` | 35 (0xb4718c) |
| +0x0c | 4 | `engine-audio-sway-amplitude-first` | 0.05 (0xb38948) |
| +0x10 | 4 | `engine-audio-sway-amplitude-last` | 0.01 (0xb33208) |
| +0x14 | 4 | `engine-audio-sway-time-first` | 0.7 (0xb35e38) |
| +0x18 | 4 | `engine-audio-sway-time-last` | 0.7 (0xb35e38) |
| +0x1c | 4 | `engine-audio-material-tweak` | 10 (0xb2f3cc) |

Static init 0x762900: `engine-sound-smooth-time` 0.1 (0xb317dc) →
`g_smoothTime`, `rpm-wheel` 0.06 (0xba08e8) → `g_wheelRotationFactor`,
`rpm-k` 10 → `g_rpmK` (not read by EngineSound).

`EngineSound` (0xf8 bytes, `new` at 0x63051a), constructor 0x7632c0 /
0x7635f0 (args: EngineTemplate, EngineBase, IObject, EngineSoundTemplate,
float const* rpm):

| Offset | Size | Field |
|---|---|---|
| +0x08 | 8 | EngineSoundTemplate |
| +0x10 | 8 | Idle sound (`createSoundFromTemplate("Idle")`) |
| +0x18 | 8 | Rpm1 sound |
| +0x20 | 8 | Rpm2 sound |
| +0x28 | 8 | Load sound |
| +0x30 | 8 | rpm pointer (EngineBase +0x2d4; null → `g_zero`) |
| +0x38 | 4 | rpm copy (old-style audio) |
| +0x3c, +0x40, +0x44 | 4 each | x of Idle, Rpm1, Rpm2 |
| +0x50, +0x68, +0x80 | 0x18 each | `ScaleEvaluator` {vtable, sound, scale = 1} |
| +0xb0 | 12 | velocity (`setVelocity` 0x762b60) |
| +0xc0 | 8 | EngineBase |
| +0xc8 | 8 | IObject |
| +0xd0 | 1 | newStyleAudio (EngineTemplate +0x6f0) |
| +0xd4 | 4 | load fade |
| +0xd8 | 4 | jitter countdown (frames) |
| +0xdc | 4 | jitter |
| +0xe0 | 4 | smoothed rpm |
| +0xe4 | 4 | zeroed by resetAudioRpm, purpose not established |
| +0xe8 | 4 | sway time |
| +0xec | 4 | last gear (0 after reset) |
| +0xf0 | 4 | smoothing velocity |

`resetAudioRpm` 0x762780 zeroes +0xd4…+0xf0.

Constructor: the three ScaleEvaluators; Rpm1 and Rpm2 get their template's
`setStopType(0)` (template slot +0xe0); for Idle, Rpm1, Rpm2:
`setVolumeEvaluator(&evaluator[i], &x[i])` (sound slot +0x88) and
`setPitchEvaluator(null, &x[i])` (+0x98); all four `setParent(this)` (+0x78).
The sound name is `makeSoundTemplateName(suffix)` (0x58c8f0): the data
names them `S_<engine template>_<suffix>`.

`ScaleEvaluator::getValue(x, row)` 0x763ca0: scale × the template's volume
evaluator (template slot +0x60) at (x, row) when there is one, else scale.

## update(running, load, material) — 0x763040

Called by `EngineBase::updateEngineSound` 0x630290 while the engine is on
(+0x2c8): running = EngineBase +0x260 ≠ 0, load = +0x2f8, material =
`getWheelMaterials` (−2 none). It first sets the rpm (+0x2d4) to
clamp(+0x2d8, −1, 1), or with engine flag 0x20 to 2·+0x2ec mapped through
0x6302d9…0x6303b8. +0x2d8, +0x2f8 and gear +0x2fc are written by
`Engine::handleUpdate` 0x62f020.

* Old style (+0xd0 = 0): x = |rpm / 1.2| (0xb47184); below 1e-4 (0xb355d0)
  → 0; all three x equal.
* New style: v = (running ? 1 : 0) · load − fade; fade += clamp(v, −0.2,
  0.2) (0xba08f4, 0xb34b58). Rpm1 scale = clamp(1 − fade, 0, 1), Rpm2 scale
  = clamp(fade, 0, 1) (+0x78, +0x90). x = `updateAudioRpm` / 1.1
  (0xb34a7c), below 0 → 0, above 1 → 1, below 1e-4 → 0; all three equal.

## Where a ghost's rpm comes from — EngineNetworkable

`EngineNetworkable::setNetUpdate` (Linux 0x5c9bd0, REA dossier
`linux-EngineNetworkable-setNetUpdate.json`) writes the EngineBase
directly after reading a record (its apply, from 0x5ca4a4): mask 0x2 →
gear +0x2fc, 0x20 → rpm +0x2d8, 0x40 → +0x2e8, 0x80 → +0x2dc, 0x100 →
+0x2ec, 0x200 → +0x328 (±1 or 0), and inside the 0x400 block the 8-bit
load → +0x2f8. So for a vehicle someone else drives, `updateEngineSound`
reads the server's rpm, not the client's `Engine::handleUpdate` (0x62f020).
Whether BF2.exe's `handleUpdate` also runs for a ghost and overwrites +0x2d8
(it does write it, 0x62f3ab and 0x62f43e) is **not established**.

Ported: `readEngineNetworkable` (`src/net/src/bf2_events.cpp`) decodes the
member, `Bf2World::vehicleEngine` keeps the fields per vehicle as the apply
does (each only when its bit came), `engineSoundRpm` is the step at
0x6302c2, and `WorldView::drawVehicle` feeds rpm, gear and load to
`EngineSound`. `OBF2_TRACE_ENGINE=1` prints each new engine record.

The FAAV (NewCar2, type 0xc2) sends mask 0x327: no 0x400 block, so its load
stays 0 on the wire and Rpm2's scale with it.

## The driven vehicle's rpm — Engine::handleUpdate 0x62f020

REA, `tools/rea.sh function … 0x62f020` (Ghidra MCP was down); field names
from `RotationalBundleTemplate::makeScript` (Linux 0x588720) and
`EngineTemplate::makeScript` (0x631180…), the stores from an
`objdump -d -C` scan of the whole Linux server. Ported:
`engineHandleUpdate` (`src/game/src/engine_update.cpp`).

The engine is a RotationalBundle. Its instance holds the axis angles at
+0x240/+0x244/+0x248 (`calculateAndClipAngle` 0x585a40 indexes them by axis,
and `snapToZeroOnNoInput` zeroes them at 0x585b2c) and the axis inputs at
+0x258/+0x25c/+0x260 (yaw, pitch, roll). Its template holds:

| Offset | Field |
|---|---|
| +0x1f4 | setMinRotation |
| +0x200 | setMaxRotation |
| +0x20c | setPivotPosition |
| +0x218 | setMaxSpeed |
| +0x224 | setMaxResetSpeed |
| +0x230 | setAcceleration (setter 0x588440) |
| +0x23c | setDeAcceleration |
| +0x248 | setContinousRotationSpeed |
| +0x254/+0x258/+0x25c | setInputToYaw/Pitch/Roll |

and EngineTemplate: +0x448 setNumberOfGears, +0x44c setDifferential, +0x450
setTorque, +0x454 setGearUp, +0x458 setGearDown, +0x45c setGearChangeTime.

So "running" (+0x260) is the roll input, the throttle for the FAAV
(`setInputToRoll PIThrottle`), and w (+0x248) is the roll **angle**. With
`setAutomaticReset 1` that angle moves toward throttle × maxRotation.z at
|acceleration| (the FAAV: toward 1 at 10/s, JEEP_FAAV.tweak lines 432–440).
For an engine type without bit 0x20 (the FAAV's NewCar2 0xc2):

* Type bit 0x10 (rocket, torpedo): the input is held at 1.
* `calculateAndClipAngle` for each axis whose maxSpeed is nonzero; then w =
  angle.z, maxW = maxRotation.z (template +0x208), +0x2e8 = angle.x /
  maxRotation.x when that is > 0, else 0.
* Without flag bit 2 of the second argument, load (+0x2f8) −= dt /
  setGearChangeTime, not below 0.
* +0x2f0 = 1 when running < −0.1 and rpm > 0, or > 0.1 and rpm < 0.
* rpm' = rpm + inertia · (w / maxW − fb − 0.5 · rpm), fb = +0x2dc. Inertia is
  `g_phyEngineInertia`, default 0.05 (0xb38948). A tank type (& 6 == 6) uses
  `g_phyTankEngineInertia` 0.07 (0xb56710) and fb · `g_feedbackMod` 0.95
  (0xb56720) · clamp(c1 − |v| / (setDifferential · 0.75), 0, 1), c1 = 1
  (0xf67948). Static init 0x62db40 loads each default before the previous
  var's store; the var names were not read. Type 0x20 with template +0x648:
  rpm' = 1 (not ported).
* Engine off (+0x2c8 = 0): rpm 0, +0x2ec 0. Else rpm' clamped to [−1, 1.2].
  Overheat (template +0x614 > 0): +0x2cc += +0x614·|rpm'| − +0x618; at 1 the
  engine stops. The property names of +0x614/+0x618 are not established.
* rpm = rpm' when |rpm'| ≥ 0.01, else 0 (+0x2d8). Gear (+0x2fc) +1 when rpm
  > setGearUp, load = 0 and gear < setNumberOfGears; −1 when rpm <
  setGearDown and gear > 1. Feedback (+0x2dc) → +0x2e0, then 0.
* Then `updateEngineSound` unless flag bit 2 is set.

Held throttle on the FAAV: the angle reaches 1 in three ticks, rpm → 2·w
clamped to 1.2, and from about tick 23 (rpm 0.85) one gear a tick up to 5,
since nothing here sets the load. Who sets the load on a gear change for
`Engine` (CarEngine has `setGearChange`) is not established.

`WorldView::drawVehicle` runs this each 1/30 tick for the vehicle we drive:
`stepRotationalBundle` on our key input (`pendingKeys`), then
`engineHandleUpdate`, then `EngineSound::update` with running = the roll
input ≠ 0. The engine-on flag +0x2c8 is set by `EngineBase::handleMessage`
(stores of 1 at 0x63013e and 0x63024d); which message ids reach them goes
through a jump table and is not read, so ours turns it on while the vehicle
is occupied. Check: `test_engine_update` (full throttle 2 s: rpm 1.2,
gear 5, Rpm1 x 0.909, level 0.562); live, `OBF2_TRACE_ENGINE=1` prints our
engine and each sound's x and level.

## updateAudioRpm(running, load, material) — 0x762d00

base = |rpm|. Not running or load = 0: jitter = 0. Else when the countdown
is ≤ 0:

* amp = 6.5 (0xba08ec) / (5·base + 1) · 0.1 (0xb317dc) · material-tweak ·
  `rpm-wheel`.
* jitter = amp · (r₁ − 0.3) (0xb4fa54), r = ((rand >> 7) | 1) + 1) · 2⁻²⁴.
* countdown = trunc((|velocity| / 30 · 23 + 2) · r₂) (0xb2efc4, 0xba08f0,
  0xb43564).
* material < 0 and ≠ −2 (wheels in the air): jitter += 0.3 (0xb2f3d4).

`rand` is the server's generator at 0x10a5128; BF2.exe's is not
established. Then countdown −= 1, sway = `updateSwayEffect(running)`,
target = base + jitter + sway. |target − smoothed| < 0.001 (0xb2f3c4):
smoothed = target, its velocity 0; else `smooth_in_out(smoothed, target,
&velocity, g_smoothTime, 1/30)` (0xb2fcc0). Returns smoothed.

`smooth_in_out` 0x733f40: ω = 2/time, k = ω·dt, e = 1/(1 + k + 0.48k² +
0.235k³) (0xb97b50, 0xb97b54); change = current − target, 0 under 1e-4;
t = (velocity + ω·change)·dt; velocity = (velocity − ω·t)·e, 0 under 1e-4;
result target + (change + t)·e.

## updateSwayEffect(running) — 0x762b80

gear = max(EngineBase +0x2fc, 1). A new gear: last gear = gear, sway time =
time-first + (gear − 1)/5 · (time-last − time-first), and `playGearShift`
(0x7628b0: Load sound `play()`). Same gear: sway time −= 1/30. Sway time < 0
→ 0. Else amp = (amp-first + (amp-last − amp-first) · gear) · sway time
(gear, not (gear − 1)/5, as the binary reads), halved when the gear went
down, else times running; result amp · sin(fmod(systemTime, 2π) · 0.85
(0xb3adc8) · (freq-first + (freq-last − freq-first) · gear)). `systemTime`
is `g_system` slot +0x38, a double, seconds not established.

## setIsRunning(on, force, flag) — 0x7629b0

On: nothing while Rpm1 or Rpm2 plays; a playing Idle is `forceStop`ped;
`resetAudioRpm`; Idle's sound flag 0x200 is cleared when `flag`, else set
(purpose not established); Idle's evaluators bound again to x[0]; then
velocity = 0 and all four `play()`. Off: Idle's evaluators bound to the
constant 0 (0xba06f4), then every playing sound `forceStop` when `force`,
else `stop`. Callers: `EngineBase::handleMessage` 0x6300cd, `reset`
0x63048d.

## Priority

No engine template sets `soundPriority`: each of the three sounds is a
3-D sample whose priority is `soundPriority` (sound-template.md,
0x10005510) on its own level and distance.
