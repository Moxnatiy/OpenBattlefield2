# A weapon's magazines and reload (`AmmoComp`)

The server's component (Linux 0x5ea150..0x5ec340, names from the symbol table),
which the client runs for its own weapon too. Ours: `game::WeaponAmmo`
(`test_weapon_ammo`), driven once per action we play by `FirstPersonView::onAction`.
Not yet read in the client's `BF2.exe`; every address here is the Linux server's.

## The template (`AmmoCompTemplate`)

Constructor 0x5eafa0; `makeScript` 0x5eb940 names each command against its field
(the pairing of a flag with its name read from the branch each `testb` jumps to).
Seconds in the data become ticks by ×30 (the constant at 0xb2efc4), truncated
(`setReloadTime` 0x5ea5c0 and its siblings).

| offset | command | default |
|---|---|---|
| +0x10 | `ammo.ammoType` | 0 |
| +0x14 / +0x18 | `ammo.reloadTime` / ticks | 2.0 / 60 |
| +0x1c / +0x20 | `ammo.firstShotExtraTime` / ticks | 0 |
| +0x24 / +0x28 | `ammo.lastShotExtraTime` / ticks | 0 |
| +0x2c | `ammo.changeMagAt`, clamped 0..1 (0x5ea5a0) | 0.7 |
| +0x30 | `ammo.autoReload` | 0 |
| +0x31 | `ammo.instantReloadOnEnable` | 0 |
| +0x34 / +0x38 | `ammo.minimumTimeUntilReload` / ticks | 0 |
| +0x3c | `ammo.nrOfMags`; -1 never runs out | 5 |
| +0x40 | `ammo.magSize` | 30 |
| +0x44 | `ammo.reloadAmount`; 0 reloads a whole magazine | 0 |
| +0x48 | `ammo.toggleWhenNoAmmo` | 0 |
| +0x49 | `ammo.reloadWithoutPlayer` | 1 |
| +0x50 | `ammo.magLinkWeapon` (string) | empty |

The M4 and the SCAR say nothing of magazines in their tweak files, so they are
30 rounds and 5 magazines. The constructor of `AmmoComp` (0x5eb620) fills the
vector at +0x28 with `nrOfMags` entries of `magSize`: [0] is the one in the weapon,
the rest are spare (`getMagVectorSize() - 1`, which the HUD calls `PrimaryClips`).
With `nrOfMags` -1 it holds one entry.

## The component's fields

| offset | what |
|---|---|
| +0x18 | the weapon (`GenericFireArm`) |
| +0x28 | the magazine vector |
| +0x44 | the wait after a round: tick of the last round + `minimumTimeUntilReload` |
| +0x48 | the tick the magazine changes at |
| +0x4c | the tick the reload ends at |
| +0x54 | a tick written by the auto-reload paths; read only by `getTotalReloadTimeLeftInPercent` — not used by ours |
| +0x60 / +0x64 | queued reloads / requested reloads |
| +0x68 | the state (below) |
| +0x6c | the magazine has been changed in this reload |
| +0x6d | the magazine ran empty after a round |
| +0x6e | signal: reload complete (`getSignalReloadComplete`, read and cleared) |
| +0x6f | signal: start a reload (`getSignalReload`, read and cleared) |
| +0x70 | auto reload pending |
| +0x78 | the weapon's `IWeaponObject` — notified of the magazine's contents, not used |
| +0x80 | the template |

## The states (+0x68)

| state | meaning | where it is set |
|---|---|---|
| 0 | idle | `reloadComplete` 0x5ea650, `updateComponent` |
| 1 | waiting after a round | `fireComplete` 0x5ea7a0 |
| 2 | a reload is asked for, not begun | `reload()` 0x5ea350 when not 2 already |
| 3 | whole-magazine reload under way | `doReload` 0x5ea240, `reloadAmount` 0 |
| 4 | the first step of a partial reload (the shotgun) | `doReload`, queued == requested |
| 5 | a middle step | `doReload`, queued left over |
| 6 | the closing step | `doReload`, queue empty |

`doReload(tick)` with `reloadAmount` 0: state 3, magazine at `tick + (int)(reloadTicks
× changeMagAt)`, end at `tick + reloadTicks`, the queue down by one. With a
`reloadAmount`: state 4 adds `firstShotExtraTime` to both, 5 adds nothing, 6 changes
no magazine and ends at `tick + lastShotExtraTime`.

`updateComponent` 0x5ea680, every tick: in a state above 2 it changes the magazine
once `tick >= +0x48` (`reloadMag` 0x5eb560: erases the last spare, sets the one in the
weapon to `magSize`, or adds one round on a partial reload) and ends the reload at
`+0x4c` (`reloadComplete`: raises the complete signal, raises the start signal again
when the queue is not empty or the state was 4 or 5, then state 0). In state 2 it
raises the start signal as soon as no other component of the weapon is busy. After the
wait (+0x44) state 1 becomes 0, and with `autoReload` an empty magazine with a spare
raises the start signal.

`postFire` 0x5ead60 takes the round out of mag [0] (a fire with nothing left is a
debug error, no more) and sets +0x6d when it reaches 0; with `autoReload` it sets +0x70
when empty. `fireComplete` sets the wait and state 1, and an auto reload raises the
start signal. `requestFire` 0x5ea150: no when the magazine is empty or the state is 2
(`busy` 0x5ec230: state above 2 or the start signal up).

`canReload` 0x5eb520: the magazine in the weapon is not full, and a spare exists or
`nrOfMags` is -1. `requestReload` 0x5eab70 refuses while reloading or when `canReload`
says no; on success it queues one reload (`reloadAmount` 0) or `magSize - mag[0]`.

## What the weapon does with the signals

`GenericFireArm::handleUpdate` (0x5e7a24..0x5e7b44), straight after the component's
`updateComponent`: the complete signal tells every component `postReload`; the start
signal, when every component's `requestReload(false)` accepts, runs
`GenericFireArm::doReload` 0x5e2ce0 — `ammo->reload()` (state 2 when not yet 2, else
`doReload`) and then every component hears `reload(state)`. `getSignalReload` 0x5eab20
holds the signal back until the wait is over.

`DefaultAnimationComp::reload(state)` (0x60aad0, jump table 0xb78b58) raises message
2 on the weapon's animation system: state 3 `setTriggerOnceMessage` (slot +0x68), state
4 `setTriggerMessage` (+0x60), states 0 and 6 `clearTriggerMessage` (+0x70); 1, 2 and
5 do nothing. The M4's `reload` bundle is `MessageTrigger reload`, message 2.

**The key.** The reload message (message 10, `GenericFireArm::internalHandleMessage`
0x5e758c..0x5e75ba) does nothing when the reload state is already 2; otherwise it calls
`GenericFireArm::requestReload(true)` (0x5e2c10: every other component's `requestReload`, then
`AmmoComp::requestReload(true)` — **refused for a weapon with `autoReload`**, 0x5eab77, and while
reloading; the queue is set and `canReload` answers) and, when that accepts, `doReload()` at
once: `ammo->reload()` makes the state 2 and every component hears `reload(2)` (the zoom
goes to level 0). The same tick's `updateComponent` sees state 2, raises the start signal
(0x5ea756), and the glue runs `doReload` again: state 3 and the once-message. So the reload
begins in the tick of the key.

## The HUD (`BF2.exe` 0x7a85f0, 0x7a5800)

| variable | source |
|---|---|
| `PrimaryAmmo` (+0xb4) | rounds in the magazine ÷ `magSize` |
| `PrimaryAmmoString` (+0x144) | `"%i"` (0x89e620) of the rounds |
| `PrimaryClips` (+0x140) | spare magazines; 0x7a5800 sets `PrimaryClip1/2/3Show` to n>0, n>1, n>2, `PrimaryClipStringShow` to n≥0 and `PrimaryClipString` to `"%i"` |
| `PrimaryReloadTime` (+0xb8) | 1 − `getReloadTimeLeftInPercent` (0x5ea380: `(+0x4c − now) / reloadTicks` while reloading) |
| `PrimaryReloadTimeShow` (+0xbc) | that value below 1 |

The ammunition block is skipped while `PrimaryReloadTime` is between 0 and 1 (0x7a8b2f),
so the count and the magazines hold the values they had when the reload began until it
ends. On the live server (a frame at the middle of a reload): 25 rounds and 4
magazines still shown, the reload bar filling, the left hand drawn changing the
magazine; at the end 30 and 3 (`ammunition:` line of the report).

## Not read

* whether every other component accepts `requestReload(false)` (a fire in the same
  tick) — ours assumes yes;
* the partial reload's interruption by the fire button; the shotgun's states 4..6 are
  coded and tested only on the component's own ticks;
* `toggleWhenNoAmmo` (switch to the previous weapon, 0x5ea860..), `instantReloadOnEnable`,
  `magLinkWeapon`, refills (`refillPercent`, `refillAbsolute`, ammo boxes), the heat;
* the ammunition colour (`PrimaryAmmoColor*`: interpolated by the effects object's
  slot 0x8c between 0x97ddd0 and 0x97dde0), the blink flags, `PrimaryEternalShow`,
  and the paths of the icons (`getHudIcons`);
* what the server tells the client about the magazine: ours counts its own rounds, so a
  round the server dropped (the action buffer, CLAUDE.md) leaves the two apart.
