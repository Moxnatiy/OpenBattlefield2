# Assembling a vehicle: BundledMesh + ObjectTemplate

Status: **implemented** — `src/game/scene.cpp`. The BTR-90 and the AH-1Z
assemble completely: hull, wheels, turret, barrel, pylons — every part in
its place.

## How it works

A vehicle in BF2 is **one** `.bundledmesh` in which all the parts sit in
**their own local coordinates**, plus an `ObjectTemplate` tree that says
where each part goes. There is exactly one link between them:

```
ObjectTemplate.geometryPart 2      ←→   the BLENDINDICES attribute == 2 in the vertices
```

That is why a `.bundledmesh` carries no node matrices even though it has a
counter for them (see [mesh.md](mesh.md)): the transforms live in the
`.con`, not in the mesh. Two independent findings — "bundled has no
matrices" and ".con has setPosition" — meet here.

## BLENDINDICES is stored as D3DCOLOR

The attribute with `usage = 2` has `vartype = 4`, that is **D3DCOLOR —
four bytes packed into one float slot**, not a floating-point number. The
part number is in the low byte; the high one is used for animated UVs
(track and wheel scrolling).

For the BTR-90 that gives 21 parts:

```
part:vertices  0:18679  1:881  2:5548  3:472  4:586  5:911  6:16 ...
```

Part 0 is the hull (it is the root, with no `geometryPart`); the rest are
the turret, the barrel, six wheels, the engine, HUD details.

## Transform order

`setPosition`/`setRotation` after `addTemplate` apply to the **last added
child**, and the transforms multiply top down:

```
apc_btr90                     identity
  -> APC_BTR90__Turret        T(0, 1.6324, 0.9962)
       -> APC_BTR90__BarrelBase   T(0, 1.6324, 0.9962) * T(-0.0091, 0.2448, 0.9827)
```

The parent's rotation also rotates the child's offset — otherwise the
turret would spin in place while the barrel stayed off to the side. There
is a test specifically for this.

Since every vertex belongs to exactly one part, assembly comes down to
transforming the vertex buffer in place: indices and material ranges are
not touched at all.

## Which geom to take: the cockpit has exactly one lod

A vehicle's `.bundledmesh` holds several geoms, and the file carries no
explicit marker for which is which. But there is a reliable sign — **the
cockpit view has exactly one lod**: the player is always right there and
needs no detail chain.

| | geom 0 | geom 1 | geom 2 |
|---|---|---|---|
| `ahe_ah1z` | 1 lod (cockpit) | 4 lods (exterior) | 3 lods (wreck) |
| `apc_btr90` | 1 lod (cockpit) | 4 lods (exterior) | 4 lods (wreck) |

So the rule is: take the geom with the longest lod chain, and among equal
lengths the most detailed. On the BTR both "non-cockpit" geoms have four
levels, and what tells the exterior from the wreck is the triangle count
(6323 against 1844).

The first attempt — "just take the most detailed" — gave the helicopter's
cockpit with its instruments instead of the machine: in the AH-1Z the
cockpit is more detailed than the hull.

## Triangle winding: counter-clockwise

A question that stayed open for a long time was settled by measurement.
For every triangle of all 1635 meshes in the game we compared the
geometric normal (the cross product of the edges) with the vertex normals
the artist set:

```
triangles: 2 218 586 (502 degenerate)
  winding agrees with the vertex normals: 2 211 069 (99.66 %)
  opposite:                                   7 517 (0.34 %)
```

So the winding is **counter-clockwise**, and back-face culling is on
(`CULL_BACK` + `FRONTFACE_COUNTER_CLOCKWISE`). The BTR's silhouette did
not change when culling was switched on — had the direction been wrong,
the machine would have turned inside out.

## Cycle protection

The tree walk does not let the same template appear twice on **one path**
from the root (`A -> B -> A` would loop until the depth limit), but leaves
repeats in different branches alone — six identical wheels are normal.

There are no cycles in the real data: the BTR-90 gives 2345 nodes at depth
5, the AH-1Z 1318 at depth 6. The large node count is effects, sounds and
projectiles, not an unrolled cycle.

## Checking

```bash
./build/macos-arm64-debug/src/app/openbf2 --object apc_btr90
./build/macos-arm64-debug/src/app/openbf2 --object ahe_ah1z --frames 60 --screenshot heli.bmp
./build/macos-arm64-debug/tools/mesh_info/mesh_info "Game Files/mods/bf2" --winding
```
