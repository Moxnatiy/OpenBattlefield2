# Weapon recoil — `SoldierBasedRecoilComp`

A hand weapon's kick on the view, per round fired. Code: `obf2/game/weapon_recoil.h`.

## Template — `ObjectTemplate.recoil.*`

Read only when the weapon has a `SoldierBasedRecoilComp` and `hasRecoilForce 1`.

| property | meaning | default |
|---|---|---|
| `recoilForceUp` | pitch force: `CRD_UNIFORM/low/high/…` draws in [low, high]; `CRD_NONE/v/…` is v | none |
| `recoilForceLeftRight` | yaw force, same form | none |
| `zoomModifier` | forces × this while zoomed (applied at 0x608d10) | 1 (`SoldierBasedRecoilCompTemplate` constructor, 0x609739..0x60974f) |
| `recoilGraphFrameCount` | samples in the graph | 8, **not measured** |
| `recoilGraphExponent` | sample i is (i + 1)^exponent | 2, **not measured** |
| `recoilGraphTotalMovement` | the graph's total | 2, **not measured** |
| `goBackOnRecoil` | add the return samples below | on |

Other distributions than `CRD_UNIFORM` and `CRD_NONE` are not read; such a force is zero.

## The graph — 0x609800, 0x6098a4..0x6099cd

1. Sample i = (i + 1)^exponent for i in [0, count), then each × −totalMovement / sum (0x609800). The newest sample is read from the end of the graph.
2. With `goBackOnRecoil`, count / 4 return samples are put in front (0x6098a4..0x6099cd):
   - offset = last sample × −0.5;
   - return sample i = offset − sample[2 · returns − i] × 1.5;
   - the original samples become sample × 1.5 + offset;
   - the positive samples are then scaled by −(sum of negatives) / (sum of positives), so the graph sums to zero and the view comes back.

## A round — 0x609024, 0x43ffa0

- The seed is the input index of the action that fired: `GameClient`'s getter 0x615910, set from the replayed action at 0x4d4c3b. One draw is discarded at 0x609032.
- The generator is 0x43ffa0, two modular multiplications per sample: seed = (seed & 0x7fffffff) × 0x5e30 × 0x661f mod 0x7fffffff. 0x440030 turns the upper 24 bits, plus one, into a uniform value in (0, 1].
- The pitch force is drawn first, then the yaw force, each × `zoomModifier` while zoomed.

## Per input — 0x60923c, 0x609269

The delta on input `a` is the force × graph[length − 1 − age], where age = a − the first input after the round; index 0 is never read. The graph starts on the input after the round's: the firearm confirms the round after that action's look has already been applied.

## Not established

- The graph defaults (8, 2, 2) used when a template sets none: the constructor's stores for them are not read. It matters: weapons that leave one or more of the three unset include `CHAT_ERYX`, `eurif_famas`, `gbgr_sa80a2_l85`, `gbrif_sa80a2_l85` and `RUPIS_Baghira` (`Objects_server.zip`, their `.tweak`).
- Where the delta is added to the look on the client: the soldier's yaw/pitch recoil getters (`getYawRecoil`, `getPitchRecoil`, `soldier-model.md`) are read, and the camera spring's fire part is not wired.
- Distributions other than `CRD_UNIFORM` and `CRD_NONE`.
