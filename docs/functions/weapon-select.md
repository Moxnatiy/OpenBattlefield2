# Taking another weapon out

The keys 1 to 9 are `c_PIWeaponSelect1..9` (`Controls.con`: `ControlMap.addKeyToTriggerMapping
c_PIWeaponSelectN IDFKeyboard IDKey_N`), numbers 14..22 (`tools/control_constants.py`), so key N is
bit N + 5 of the action's button mask (`net::bf2::kButtonWeaponSelect`). A press is sent for the one
tick it was made in, like the jump.

## Which item a key picks

**Not read in the engine.** The data agrees with "key N picks the kit's item whose `itemIndex` is
N": the US Specops kit's children have `itemIndex` 1 (`kni_knife`), 2 (`uspis_92fs[_silencer]`),
3 (`usrif_m4`), 4 (`ushgr_m67`), 5 (`c4_explosives`) — knife, pistol, rifle, grenade, special, the
order the keys are known by. Ours picks by that rule and ignores a key for an item the kit lacks;
`Soldier::setActiveWeapon` (Linux) and `getWeaponFromIndex` are where the original's is.

## What a weapon taken out does

`GenericFireArm::enableWeapon` (Linux 0x5e6640): the weapon is enabled (`0xc0` of every component),
its field +0x310 is set to **now + the template's +0x228**, the `delayToUse` in ticks (seconds × 30:
0.6 for the knife, 0.75 for the pistol, 1.6 for the M24). `busyExcept(…, false)` (0x5e35f8) reports
the weapon busy until that tick, and a busy weapon does not fire (`requestFire`, 0x5e4370 asks the
components). The 1p animation system of the weapon starts anew: its `startup` trigger plays the
`deploy` bundle (`AnimationSystem1p.inc`).

Ours: `FirstPersonView::setWeapon` on a new item builds the model (cached), starts a fresh
animation player — so the deploy plays — and holds a round back until the delay has passed; each
weapon keeps its own magazines while it is put away (`ammoByKey_`), a respawn fills them again.
On the live server (`US_Sniper`): key 2 shows the silenced 92FS with its own numbers (15 rounds,
7 spare), key 3 brings the M24 back with the 5 and 6 it had; the crosshair follows.

## Not read

* the engine's key → item mapping, and what the server does when the item is missing or the
  soldier cannot switch (in a vehicle, mid-reload, a zoomed sniper);
* the weapon that was out: its `disableWeapon` path, and the zoom/bolt state it leaves;
* the item selection HUD (`ItemSelectionHud`, `HudElementsWeaponSelect.con`) that the original
  raises on a key press;
* the third-person side: other soldiers' weapon index (0x1000) is read and drawn, but the deploy
  they play when it changes is not.
