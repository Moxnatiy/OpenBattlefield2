# Vehicle camera view modes

The vehicle cameras' first person, chase and the other external views. Read
from the Linux 1.5 server (`linuxded-full/bin/amd-64/bf2`, symbols), where the
camera code is shared with the client. **Not yet located in `BF2.exe`**:
`Camera::setViewMode` is transplanted there at `0x5a4eab`
(`docs/reference/bf2exe-symbols.txt`); the chase transform, the template
constructor and the C handler are not. REA dossiers:
`ghidra_projects/rea/dossiers/linux-camera-*.json`.

## Modes

`operator<<(std::ostream&, CameraViewMode)`, 0x8c1460:

| value | name | allowed when (`Camera::setViewMode`, 0x579bd0) |
|---|---|---|
| 0 | CVMFirstPerson | treated as 1 |
| 1 | CVMInside | template +0x298 |
| 2 | CVMNose | template +0x29e and serverInfo +0x2a |
| 3 | CVMChase | template +0x299 and serverInfo +0x2e; resets the chase state |
| 4 | CVMFrontChase | template +0x29d and serverInfo +0x2e; resets the chase state |
| 5 | CVMFlyBy | template +0x29a and serverInfo +0x2e; clears the recorded frames |
| 6 | CVMTrace | template +0x29b; eye 20 m along the parent's +0x20..+0x28 vector |
| 7 | CVMExternTrace | template +0x29c and a nonzero +0x35c..+0x364; eye 20 m along +0x34c |

serverInfo +0x2e is taken to be `sv.allowExternalViews` and +0x2a
`sv.allowNoseCam` from their order and use; the field names are **inferred**.
A mode that is not allowed returns 0 and the mode does not change. On a change
the camera calls `changeSubGeometry(mode, -1)`, keeps the previous mode at
+0x36c and sends event 0x1f to the event manager.

## Template defaults

`CameraTemplate::CameraTemplate`, 0x577ab0:

| offset | value | property |
|---|---|---|
| +0x298 | 1 | Inside allowed |
| +0x299 | 1 | `CVMChase` |
| +0x29a | 1 | `CVMFlyBy` |
| +0x29b | 0 | Trace |
| +0x29c | 0 | ExternTrace |
| +0x29d | 1 | `CVMFrontChase` |
| +0x29e | 0 | `CVMNose` |
| +0x2b0 | 1.0 | `followStiffness` |
| +0x2b4 | -1.0 | `chaseDistance` |
| +0x2b8 | 0.3 (0x3e99999a) | `chaseAngle` |
| +0x2bc..+0x2c4 | 0, 0, 0 | `chaseOffset` |

The property names on the right are the `ObjectTemplate.*` lines that occur
in the vehicle tweaks (`CVMChase` 23 times, `CVMFrontChase` 25, `CVMFlyBy` 26,
`CVMNose` 20); which offset each writes is **inferred** from the order of the
flags and the modes they gate.

## Chase and FrontChase

`Camera::getTransformationCVMChase(float dt, bool front)`, 0x57ab80. All in the
**root parent's** (hull's) world matrix M, not the camera's:

1. target = M · chaseOffset (a point; M's translation if the offset is zero).
2. distance = chaseDistance; if it is -1 (or the camera's parent is not of
   class 0x9493), the root's bounding radius (vtable +0x130), at least 2,
   times `*g_cam_dist`. With a non-0x9493 parent the angle becomes 0.3.
3. wanted = normalise(−M.forward · s + chaseAngle · M.up), s = 1, or −1 for
   FrontChase.
4. The stored direction (+0x2d4, as a quaternion with w = 1) is slerped toward
   (wanted, 1) by clamp(dt · followStiffness, 0, 1) and its xyz normalised. An
   empty stored direction takes `wanted` at once.
5. The stored distance (+0x2e4) blends the same way toward
   distance − (velocity · M.forward) · `*g_acc`.
6. eye = target + distance · direction; eye.y is at least
   max(terrain at eye, heightmapCluster +0xd8) + 1.
7. `calcLookAtMatrix(eye, target, up = (0, 1, 0))`.

## FlyBy

`Camera::getTransformationCVMFlyBy`, 0x579980: a point ahead of the root,
M.forward · radius · 1.2 plus velocity · 0.3, scaled by +0x2d0 · 0.4, kept 1 m
above the ground, is recorded with `writeFrame` and replayed by `viewFrame`;
the replayed eye is kept 0.5 m above the water.

`writeFrame(point, delay)`, 0x5779e0: the frames are a list of (point, time)
at +0x2f0, time from the camera's clock +0x2e8. A point is appended when the
list is empty, or when clock ≥ the first frame's time + delay; otherwise it
is dropped. FlyBy passes delay = +0x2d0 · 60.

`readFrame`, 0x579410: drops frames from the front while the next one's time
is below the clock, then interpolates the front frame toward the next by
(clock − t0) / (t1 − t0), clamped below at 0. An empty list logs "trace
buffer size 0." (Camera.cpp:0x19a).

`viewFrame`, 0x5796a0: the eye is `readFrame`'s point; it looks at the
camera's own world position (vtable +0xd8), up (0, 1, 0). In Chase and
FrontChase (mode 3, 4) the point is instead pulled to 1.2 · radius from the
camera. The eye is kept at +0x3b0.

+0x2d0 is the constant 8.0, stored by both `Camera::Camera` constructors
(0x57a13f, 0x57a5cf; no other store in the class): the point is
(forward · radius · 1.2 + velocity · 0.3) · 3.2 from the camera, and the
delay is 480 s. The clock +0x2e8 is the sum of `handleUpdate`'s dt
(0x578ce0). FlyBy runs only while +0x2ec is set: `setRecordingValid`
(0x578130), called with 1 and 0 from `Camera::handleMessage` (0x578bf0) on
entering and leaving.

So a FlyBy camera stands at a point ahead of the vehicle and watches it go
past; with a 480 s delay it stays there until the mode is entered again.

## Cycle on C

`c_PIToggleCameraMode` (C) steps through the allowed modes. The handler is
**not found**. Retail, measured on 22 November 2026
(`build/retail-captures/vehicle-seats-20261122/retail-m1a2-7`, M1A2 driver and
cupola): Inside, C → Chase, C → FrontChase. The full cycle, four presses per
seat (`retail-m1a2-9`, `cycle.jpg`): Inside → Chase → FrontChase → FlyBy (a
low camera beside the wall, looking at the tank) → Inside, the same on both
seats. That is the ascending order of the allowed modes, Nose (2) skipped as
the template disallows it.
F9..F12 are `c_PICameraMode1..4` (`Settings/Profiles/Default/Controls/Land.con`);
which mode each selects is not established beyond F9 = Inside, F10 = Chase.

`PlayerControlObject::nextCamera`, 0x53a1a0, switches between camera objects
(+0x2a8 + 1, wrapping at +0x2a0), not modes.

## Ours

`WorldView::occupiedCamera` (`src/app/src/world_view.cpp`): modes 1, 3, 4 and
5 with the allow flags above; C steps to the next allowed mode. Chase follows
steps 1, 3, 4, 6 and 7. FlyBy plants its point once per entry and looks at
the camera mount. Not ported: the chase distance's velocity term (`g_acc`),
the -1 distance (2 m stands in; `g_cam_dist` not measured), the water floors.
The root's bounding radius (vtable +0x130) is not read; FlyBy uses the drawn
hull mesh's bounding sphere instead.
