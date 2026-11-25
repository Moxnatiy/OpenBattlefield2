# Where our own rounds land

Code: `src/app/src/own_impacts.cpp` and `src/app/src/bullet_decals.cpp`. The
decal's geometry, placement and lifetime are covered in `decal_placement.h`,
`decal_projection.h` and `decal_lifetime.h`.

Test: `test_bullet_decals`.

## The material cell

`common/material/materialManagerSettings.con` holds one cell per pair of
projectile material and surface material:

```
MaterialManager.createCell <projectile> <surface>
MaterialManager.setDecalTemplate <slot> <template>
MaterialManager.setEffectTemplate <slot> <template>
MaterialManager.setSoundTemplate <slot> <template>
```

* Slot 0 is the first contact, and the only slot read.
* The other slots describe a ricochet or an exit. Telling those apart needs the
  projectile's path, which is not ported.
* The round's material is the weapon's `projectileTemplate`'s `material`.
* The surface's material is the face's global material. It comes from the
  owner's `ObjectTemplate.mapMaterial` (`CollisionWorld`'s `worldMaterial`).

## A round of ours

When `FirstPersonView::onAction` answers that a round left, `OwnImpacts::shot`
follows the view's line from this action's eye (`soldierEye`, `lookDirection`).
It takes the nearer of two contacts:

* the collision world's first contact (`segmentContact`);
* the terrain's (`castSegment`).

**Not ported:**

* The original flies a projectile and its contact gives the point. Ours has no
  gravity, drag or flight time in it.
* The reach, 125 m: ours, **not measured**.
* The terrain's material map (`HeightmapPrimary.mat`). A round into the ground is
  counted and leaves nothing.

A contact with an object whose face has a global material is drawn in the next
frame:

* **The effect.** The cell's `setEffectTemplate`, as a `SmokeFlare` bundle at
  the point. It plays its own sounds from the world view's source pool. It stands
  off the surface by 0.05 m: ours, **not measured**.
* **The mark.** The cell's `setDecalTemplate`, lifted 0.01 m
  (RendDX9.dll 0x101adc70) and faded by its lifetime.
  * The surface projection and clipping are not ported: the mark is a flat quad.
  * Only the 128 newest marks are kept. Retail keeps 32 or 64 per receiver
    (0x101acc80), and which receivers get the 64 is not measured.

**Not done:**

* other soldiers' rounds;
* the muzzle flash;
* the cell's own sound (`setSoundTemplate`) beside the effect's sounds;
* static receivers for projected marks.
