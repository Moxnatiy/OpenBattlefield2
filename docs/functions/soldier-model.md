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
* the skinned mesh is lit by the static mesh shader, not `SkinnedMesh.fx`.

## Done — stage 2: the soldiers move

Every trigger type is reversed (docs/functions/animation-system.md), a tick walks
`root` and `postRoot` rather than the whole file, and `anim::Player` keeps each
bundle's time, its playback speed and the four-clip blend. On `--connect` each
soldier gets his own posed copy of the mesh: the state comes from his ghost — the
speed and the direction of the velocity the server reports, turned into his own
frame by the yaw the ghost carries — and the pose is skinned again every frame
(`obf2::mesh::skinMesh`, `MeshRenderer::updateVertices`).

Measure: `--watch-soldier` with `--trace-frames` prints the clips, their times and
their weights; the screenshots of the same run show the stride changing.

### Where the deformation happens

In the vertex shader, as in the original. `Shaders_client.zip:SkinnedMesh.fx:46`
declares

```
mat4x3 mBoneArray[26] : BoneArray;//  : register(c15) < bool sparseArray = true; int arrayStart = 15; >;
```

and `skinSoldier` (`SkinnedMesh.fx:120`) moves the vertex by two of them:
the position and the normal are each transformed by both bones, mixed by the
weight — `BLENDWEIGHT` is the first bone's share and the second takes
`1.0 - LastWeight` (`SkinnedMesh.fx:105`) — and the normal is normalised
afterwards. The pair of ids comes in as `BLENDINDICES`, a `D3DCOLOR` the shader
unpacks with `D3DCOLORtoUBYTE4` for hardware without UBYTE4
(`SkinnedMesh.fx:88`).

The array is 26 long because a vertex's ids index **its own material's rig**,
not the skeleton: one draw call, one rig, at most 26 bones. So the geometry never
changes and only the bones do — which is why one soldier template and kit is a
single mesh on the GPU however many players wear it, each supplying his own
skeleton.

Ours: `obf2::mesh::rigPalette` builds one range's matrices
(`world(bone) * inverse_bind`, the same product `skinMesh` uses), the renderer
hands them to the vertex stage as three rows per bone, and `DrawItem::pose`
carries the skeleton. `obf2::mesh::skinMesh` stays: it is the same answer on the
processor, and it is what the test compares the palette against.

The measure, on a live server with bots: skinning and re-uploading every
soldier's vertices took 1187 samples of 5233 on the main thread (`sample`, 10 s,
about 2 ms of every frame); through the shader the whole soldier path is 65
samples of 3205 (8 s, about 0.14 ms). The picture is the same — the same
mid-stride screenshot.

What is still a stand-in here:

* the pose is always standing — the ghost's own pose (crouch, prone, swim) is one
  of the soldier state's unnamed fields and is not read yet;
* `BundlePlayer::update` is not reversed, so there are no fades between clips and a
  one-shot bundle is not carried to its end;
* the weapon's system is asked with the same state, but the weapon's own messages
  (firing, reloading, zooming) are not known, so the upper body only walks and runs.

## Stage 3 — weapon

The ghost's soldier state carries the weapon out (0x1000), a value whose width is
the kit's weapon count (`soldier_state.h`); with the kit known it can be read. Then:
the weapon's 3p mesh (sub-geometry not established) attached to the hand bone, and
its animation system for the upper body.

Measure: a bot's rifle is in his hands, and switching to the pistol changes it.

## Stage 4 — first person

Our own arms (the soldier's sub-geometry 0, `skeleton1P`) and weapon, driven by
the `AnimationSystem1p.inc` files.
