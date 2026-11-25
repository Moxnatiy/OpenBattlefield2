# Third-person look: spine and head

How a soldier seen from outside bends towards where he looks. Code:
`src/app/src/third_person_look.cpp`, `mesh::BonePost` in `src/mesh`.

Status: the posts and `mesh::BonePost` are in. Feeding them from the ghost
track in the world view, and the idle-clip and `RandomTrigger` changes under
"Ours", come with the world view and animation changes.

## `Soldier::updateThirdPersonAnimations` (Linux server 0x555020)

Dossier: `ghidra_projects/rea/dossiers/linux-updateThirdPersonAnimations.json`.

```
cam  = getSoldierCamera()'s matrix (vfn +0x108 after query 0xc378)
body = this's matrix (vfn +0x108)
pitch = -asin(clamp(body.row1 . cam.row2, -1, 1)) deg       0x555020..   positive down
yaw   =  asin(clamp(body.row0 . cam.row2, -1, 1)) deg       the aim yaw while |aim| < 90
if pose (+0x3c8) == 3: pitch = 0                            0x55515d
idle head yaw (+0x404..+0x414, soldier-lookIdleTime/Angle/lookSmooth),
  reset by resetHeadMovements (0x5517b0)                    not ported
pose 2: pitch clamped to [proneLookUp, proneLookDown]       0x556189
else:   pitch clamped to [lookUp, lookDown]                 0x5552e2
pitch *= 0.25; yaw *= 1/3                                   0x55531d, 0x555325
post(bone) = rotMult(rotX(pitch), rotY(yaw)) for spine2, spine3, torso
post(head) = rotMult(rotX(pitch), rotY(idle yaw))
post(mesh15) = rotX(-pitch)                                 0x5558fa..0x555901
then IK for the hands (applyIk 0x12/0x13, 0x22) and Skeleton::transform with
  a model matrix of identity rotation, y -1                 not taken apart here
```

The post pointer is Bone +0x40, bones 0x50 apart from the skeleton's +8:

| store | bone | `3p_setup.ske` |
|---|---|---|
| +0x3b0 | 11 | spine2 |
| +0x400 | 12 | spine3 |
| +0x450 | 13 | torso |
| +0xef0 | 47 | head |
| +0x1760 | 74 | mesh15 (back gear part) |

Defaults (`tools/linuxded/vars_defaults.py 0x551a00 0x551c00`; the game's data
sets none): `soldier-lookUp` -70, `soldier-lookDown` 70, `soldier-proneLookUp`
-30, `soldier-proneLookDown` 5.

## `Skeleton::transform` (Linux 0x6c8760)

Dossier: `ghidra_projects/rea/dossiers/linux-Skeleton-transform.json`. For a bone
with a parent and a post: `tmp = local * parentWorld`, `world = rotMult(tmp,
post)` (rotation only), position put back from `tmp` (0x6c8a8d..0x6c8ab4).
Children are built on the rotated bone, so the four pitch quarters add up to the
whole pitch at the head and the three yaw thirds to the whole aim.

`setRotateXDeg` (0x731a10) and `setRotateYDeg` (0x731ae0) write the same sixteen
floats as core's `rotationX` / `rotationY`; `rotMult(out, a, b)` is `a * b` row
major, `b * a` in our column order.

## Ours

* Remote soldiers: pitch is the soldier state's 0x10 (positive down), yaw its
  0x4 (aim from the body), pose its pose. The mesh stands on the body's yaw
  (the track's `bodyYaw + aimYaw` less the aim); the aim is in the spine.
* The angles come from the interpolated ghost track (`GhostPose::pitch`,
  `aimYaw`, interpolated like the position, `GSInterpolationTime`), not the
  newest record: the first version read the newest and stepped at the packet
  rate.
* Remote idle clips: each system's idle time now grows while one bundle plays
  (`AnimationSystem::update` 0x7f7390, as first_person.cpp); before, the remote
  soldiers' `IdleTrigger` never fired. Seen live 2026-11-22 (the client's
  `remote idle:` log lines): the medic idling
  with the knife played `3p_Knife_Idle1.baf` three times — the knife's
  `AnimationSystem3p.inc` `stand_Idle1`, from `IdleTrigger stand_idle`
  (`idleTime 5/15`) through `RandomTrigger rnd_idle`. The finger balance the
  player sees is `1p_Knife_Idle1.baf`, first person only; other players see the
  third-person clip, which is a different movement. Which idle and when is
  `rand()` on each client, not networked, so two clients never agree.
  Only `Idle1` ever came up (every one of a dozen and more starts): `System::applyBundles` took bundle 0
  when no random source was passed, and no caller passes one. It now draws
  `std::rand()` as `RandomTrigger::applyAnimations` (`BF2.exe` 0x7fefa0) does;
  this changes every RandomTrigger (idles, hits, first person too). Seen live
  after the change: the medic's knife
  played `3p_Knife_Idle1.baf` and `3p_Knife_idle2.baf`, and our own support
  soldier `3p_m249saw_idle2.baf`.
* A seated soldier gets no posts (his seat's clips own the upper body) —
  whether the engine skips them in a seat is not established.
* Not ported: the head's idle yaw; the hands' IK after the posts.
* Seen live 2026-11-22 on an original 1.50 server: a medic in front of our
  client looking down bends torso and head forward. Looking up and down was
  also confirmed by eye by the player driving the medic; no captured frame
  shows the up look clearly.
