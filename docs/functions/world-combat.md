# The combat the server reports, on screen and heard

Code: `WorldView::showDetonations` and `WorldView::gunSound`
(`src/app/src/world_view.cpp`).

## Grenade detonations

A projectile's detonation record (projectiles.md) carries its position, the
surface material (the number) and the surface normal (the vector). Linux
`DefaultDetonationComp::detonate` 0x61e0a0 reads them that way. For the hand
grenade (`ushgr_m67_projectile`):

* **The end effect.** `e_exp_grenade` (`endEffectTemplate`, `ushgr_m67.tweak`)
  plays as a `SmokeFlare` at the point.
* **The cell.** With the detonation component's `useMMOnEndEffect`,
  `GameLogic::doImpact` looks up the cell of its `explosionMaterial` and the
  surface in `materialManagerSettings.con`, slot 0 (own-impacts.md). The cell's
  effect and its sound play beside the end effect. A `SmokeFlare` rooted at a
  `Sound` template plays just that sound, with the template's own distances. The
  cell's decal is the scorch, laid along the normal.
* **Tinnitus.** BF2.exe 0x5cd940 starts it for our soldier on foot within
  `explosionRadius` × `sound-tinnitus-range` (1.05, Settings/Sound.con line 51;
  sound-template.md). While it rings it ducks the other effects' sounds.

Every record is taken once, then the list is cleared.

**Not shown yet:**

* every other projectile's detonation;
* the blasts of vehicles and placed charges as they are destroyed.

engine-source shows those last two only within 150 m and 120 m of the eye,
cut-offs with no source, so they were not taken.

## Other soldiers' rounds

When another soldier's weapon gets a round message (the cadence is in
CLAUDE.md, "soldiers as models": inferred, not read), his weapon's
third-person fire sound plays where he stands:

* the `Sound` child named `…fire3p…`, the data's naming;
* as a positioned `SmokeFlare`, so with BF2Audio's distance model and the
  template's `minDistance` and `halfVolumeDistance` (sound-template.md);
* from the world view's one pool of sources.

engine-source's own gain formula (`0.65 / (1 + d² / 225)`) has no source and
was not taken.

**Not done:** the flash at his muzzle, and where his rounds land.

The end-of-run report counts all of this:
`combat: grenade detonations shown N, scorches N, other soldiers' rounds heard N`.
