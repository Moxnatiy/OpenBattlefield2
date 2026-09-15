# Other players' soldiers: model, kit, pose, weapon

How a soldier seen from outside is put together, and what of it is done.

## What the data assembles

```
ObjectTemplate.create Soldier us_light_soldier          objects/soldiers/us/us_light_soldier.con
  geometry us_light_soldier                             a SkinnedMesh, two sub-geometries
  skeleton3P Objects/Soldiers/Common/Animations/3p_setup.ske
  animationSystem3P Objects/Soldiers/Common/Animations/AnimationSystem3p.inc

ObjectTemplate.create Kit US_Specops                    objects/kits/us/US_Specops.con
  geometry US_Kits                                      17 sub-geometries
  geometry.kit 2                                        the piece this kit wears
  geometry.dropGeom 10                                  the piece a dropped kit shows
  addTemplate <weapons>                                 GenericFireArm, each with
                                                        itemIndex, geometry, animationSystem3P
```

| piece | where from | notes |
|---|---|---|
| body | the soldier's mesh, sub-geometry **1** | 0 is textured `1p_*` (the arms seen in first person, one LOD); 1 is textured `*_3p_*` and the head (three LODs) — read from `soldiers/mec/meshes/mec_light_soldier.skinnedmesh`. The code in `RendDX9.dll` (`SkinnedMeshRenderer`) that picks it is **not found** |
| kit | the kits' mesh, sub-geometry `geometry.kit` | skinned on the same skeleton: `MEC_Kits` piece 2's rig names bones 3, 6, 47 and 73–79 — the legs, the head and the gear bones `mesh9`..`mesh16` at the end of `3p_setup.ske`. The command `geometry.kit` (`BF2.exe` 0x525d62) writes its number to +0x280 of the object `FUN_0071d140` returns; who reads it is not followed |
| skeleton | `skeleton3P` | 80 bones (`ske_info`) |
| legs | the soldier's `AnimationSystem3p.inc` | bundles such as `stand_rightFootBack` |
| upper body | the out weapon's `AnimationSystem3p.inc` | bundles such as `stand_still`, `stand_run`, `stand_sprint`; without one the arms stand in the bind pose |
| weapon slot | `itemIndex` | the specops kit: knife 1, pistol 2, rifle 3, grenade 4, C4 5, parachute 9 |

Which kit a player wears comes from the network: `CreateKitEvent` and
`HandlePickupEvent` (network-events.md, "Which kit a soldier wears").

## Done — stage 1

`--connect` draws every other soldier as its body and kit, textured, standing on
its pivot minus `coll-soldier-pivot-height` and turned by its body yaw. One mesh per
soldier template and kit, skinned once on the CPU (`obf2::mesh::appendSkinned`,
`skinMesh`). Code: `obf2/game/soldier_model.h`, the glue in `main.cpp`
(`soldierMeshFor`). Measure: `--watch-soldier` puts the camera beside the nearest
other soldier; the screenshot shows a US soldier with helmet, vest and pouches.

What stands in for now, each one a stage below:

* the pose is standing for everyone — the legs' `stand_rightFootBack` and the
  weapon's `stand_still` at their first frame, chosen by bundle name;
* the upper body's weapon is the kit's slot 3, not the one out;
* no weapon mesh in the hands;
* the skinned mesh is lit by the static mesh shader, not `SkinnedMesh.fx`;
* the mesh keeps every vertex of the file's shared buffer (35 000 for a soldier with
  a kit, of which the two pieces use a fraction).

## Stage 2 — animation

The trigger tree walk (`anim::System::select`) takes too much: hit, death, face and
skydive bundles all come out for a standing soldier, because `MessageTrigger`,
`RandomTrigger`, `IdleTrigger`, `ForwardTrigger` and `SideTrigger` are not
reversed, and neither is `BundlePlayer`'s timing (fade in and out, lengths). The
state it needs is in the ghost: speed and direction from the velocity, pose, the
sprint flag.

Measure: a running bot's legs run and a standing one's stand, and a clip's length on
screen matches its frame count at 24 frames per second (`kAnimationFramesPerSecond`).

## Stage 3 — weapon

The ghost's soldier state carries the weapon out (0x1000), a value whose width is
the kit's weapon count (`soldier_state.h`); with the kit known it can be read. Then:
the weapon's 3p mesh (sub-geometry not established) attached to the hand bone, and
its animation system for the upper body.

Measure: a bot's rifle is in his hands, and switching to the pistol changes it.

## Stage 4 — first person

Our own arms (the soldier's sub-geometry 0, `skeleton1P`) and weapon, driven by
the `AnimationSystem1p.inc` files.
