# Projectiles on the wire: `GenericProjectileNetworkable`

Rockets, grenades, missiles, smoke and placed charges travel as ghosts of their
own. Rifle rounds were not seen: in two live runs with bots firing, every
projectile created was a grenade, a missile, smoke or a charge. How a client
learns of another player's rounds is not established here (his firing state is
`PlayerState` 0x40).

## The client's read — `BF2.exe` 0x636ac0

Found by its check: the string
`…\Game\Objects\Networkables\GenericProjectileNetworkable.cpp` (0x8fd4f8) is
referenced once, at 0x636dda, inside 0x636ac0, with "error READING data" and
line 0x109. The Linux server names the method
`GenericProjectileNetworkable::setNetUpdate`; OpenBF2's reconstruction of the
sender (`getNetUpdate`) agrees field for field.

| order | mask bit | read | record (stride 0x50, ring at +0x54) |
|---|---|---|---|
| 1 | — | the mask, 5 bits (`0x6b6470(&mask, 5)`) | |
| 2 | 0x2 | position: compressed vector against the stream's compression vector, 0.01 (`0x6b69f0(stream + 0x54, 0x3c23d70a)`) | +0x08 |
| 3 | 0x1 | rotation: unit quaternion, four 12-bit values (`0x6b7270(0xc)`) | +0x14 |
| 4 | 0x4 | detonated, 1 bit | +0x24 |
| 5 | 0x4, detonated | a signed number, `0x4f9c10(0x10)`: sign + 15 bits | +0x28 → projectile +0x1e4 |
| 6 | 0x4, detonated | high-compressed vector against zero, 0.01 (`0x6b6c50`; zero is 0x6d8f10's static; table 0x9791b4 = 32, 12, 10, 8; level 0 is three raw floats, 0x53c0a0) | +0x2c → projectile +0x1ec..+0x1f4 |
| 7 | 0x4, detonated | has a target, 1 bit | +0x48 |
| 8 | 0x4, detonated, target | the target's network id, 16 bits (looked up with 0x6a6c10) | +0x4c |
| — | 0x8 | nothing: no branch reads it | |
| 9 | 0x10, only with an armor component (object +0x3c) | `0x4f9a10(0, maxWreck + max)` less maxWreck; the bounds are the component's +0x88 and +0x84, rounded | +0x44 → the component's +0x38 setter |

Position is read before rotation, unlike the bits' order.

The armor's range: OpenBF2 names +0x84 and +0x88 `getMaxHitPoints` and
`getMaxWreckHitPoints`. Their defaults are 10 and 10 (Linux
`ArmorGLCompTemplate::ArmorGLCompTemplate` 0x5b5f4c..0x5b5f5e stores 10.0 into
+0x164, +0x168, +0x16c and +0x170). `armor.wreckHitPoints` sets the wreck and
its maximum together (`setWreckHitPoints`, Linux 0x5b8690). In the data only two
projectile templates have armor, `at_mine_Projectile` and
`usmin_claymore_Projectile` (`createComponent Armor`, `maxHitPoints 1000`), so
their field is 0..1010, 10 bits.

### What the client does with it (0x636ac0, after the read)

* **A full update** (`0x4f9d40`, the networkable's "full" flag), when the
  projectile is not attached (+0x28 parent is null and the physics node's +0x100
  / +0x18 tests pass): the predict stamp +0x58 becomes −1, the transform is set
  from the record (0x62ed00), and the node's six motion vectors are zeroed
  (slots +0x64, +0x74, +0x68, +0x78, +0x80, +0x7c).
* **A detonation** (0x4 with the flag 1): unless the projectile is attached
  (0x636120), its transform is set from the record. The number goes to +0x1e4,
  the vector to +0x1ec, the tick (0x4c4420) to +0x1e8, and the target's weak
  pointer is taken. Then the call goes through the object's +0x154 interface,
  slot 0x14, with 1: OpenBF2's `detonate(true)`.
* **Hit points**: the armor's +0x38 is called with the value as a float.

### What the two detonation fields are

**Not established in the binary.** On the live server:

* the vector is **a unit vector**, (0, 1, 0) for a grenade on flat ground and
  slanted on a wall or vehicle. It reads as the surface normal at the hit;
* the number was 4 on the ground, 78 and 12 for smoke, 86, 30 and 1006 for
  missiles on vehicles. A material index is the obvious reading. It is **not
  taken**: 1006 is beyond the 0..255 range our material tables use.

## How we know an object is a projectile

The 5-bit mask names nothing on its own. The client knows the class from the
object's template, and so do we: a `CreateObjectEvent`'s template number
(template_numbers.h) names a template whose class is `GenericProjectile` in the
registry (`WorldView::setTemplateNet`, filled by `RemoteWorld`).

## On our side

* `net::bf2::readProjectileState` (projectile_state.h) reads the record whole.
  The record is read only when the armor is known, and the 0x10 field only when
  the template has armor. `test_projectile_state` covers it.
* `WorldView` counts the records, keeps every detonation (`detonations()`), and
  moves the object like any other ghost: position, rotation, track.
* `RemoteWorld` logs the first 30 detonations
  (`DETONATION object … (template) at … number … vector … target …`) and prints
  the report line `projectiles: created, records (read whole), detonations`.
* `app::WorldView` draws a projectile by its template's mesh. A projectile with
  no geometry (`Smokeflare`) gets no box: what the original shows of it is its
  effects.
* `--watch-projectile <file>`: after our spawn, the camera stands beside a
  projectile that has moved more than a metre and has a mesh. On the sixth such
  frame it saves the screenshot.

### Measured live (strike_at_karkand co-op)

* One 4000-frame run: 4 projectiles, **34 of 34 records read whole to their
  length**, 3 detonations (two `Smokeflare`, one `tow_missile` with target 1830,
  the T-90).
* One 6000-frame run: 32 projectiles, **1302 of 1302**, 30 detonations (M67
  grenades, `at_predator`, `tow_missile`, smoke).
* `--watch-projectile`: an M67 grenade drawn lying on the cobbles, turned by its
  own record.

## Not done

* The detonation's effect, sound and damage: nothing is played; the
  detonation is logged.
* `predict` (OpenBF2: linear interpolation between records and extrapolation up
  to the network manager's time, `local-prediction-lerpTime`,
  `local-prediction-minDelay`), and our own local projectile handed over to the
  server's. We draw through the common ghost track.
* Whether a projectile is an obstacle for a soldier's movement: we put its
  collision mesh into the movable collision like any object's
  (`syncMovableCollision`). **Not established.**
* The two detonation fields' meaning (above).
