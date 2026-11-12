# A weapon's zoom (`DefaultZoomComp`)

The server's component (Linux `DefaultZoomComp` 0x629490..0x62a7d0 and `ZoomComp`
0x62b8c0..0x62c120), which the client runs for its own weapon too. Ours:
`game::WeaponZoom` (`test_weapon_zoom`), driven once per action by
`FirstPersonView::onAction`. All addresses are the Linux server's.

## The template (`DefaultZoomCompTemplate`)

Constructor 0x629d40, `makeScript` 0x62a030 (each command's string read against its
field), setters 0x629620 (`setZoomDelay`), 0x629640 (`setChangeFovDelay`), 0x629ea0
(`addZoomFactor`). Seconds become ticks by ×30 (0xb2efc4), truncated.

| offset | command | default |
|---|---|---|
| +0x10 (`ZoomCompTemplate`) | the input that toggles it | 0x1f = `c_PIAltFire` (31) |
| +0x20 | `zoom.addZoomFactor` (a vector; a value already in it is skipped) | [0.0] |
| +0x38 / +0x3c | `zoom.zoomDelay` / ticks | 0.5 (0xb2f234) |
| +0x40 | `zoom.zoomLod` | 0 |
| +0x44 | `zoom.zoomOutAfterFire` | 0 |
| +0x48 / +0x4c | `zoom.changeFovDelay` / ticks | -1.0 (0xb2f3c0) → -30 |
| +0x50 | `zoom.mouseSpeedMod` | 1.0 |
| +0x54 | `zoom.disableMuzzleWhenZoomed` | 0 |

The M24 (`usrif_m24.tweak`) is `zoomDelay 0.125`, `zoomLod 1`, factors 0 and **0.2**,
`changeFovDelay 0.08`, `zoomOutAfterFire 1`, `disableMuzzleWhenZoomed 1`. The M4 and
the SCAR have no zoom component at all.

A zoom factor is the camera's field of view in radians, vertical (0.2 ≈ 11.5°
against the soldier camera's own 1.1): `changeZoom` hands it to `Player::setFov(fov,
time)` (Linux 0x495e00; the player's +0xdc end value, +0xe0 the value now, +0xe4
the rate `|now − fov| / time`); a `fov` of 0 means the camera's own
(`getActiveFOV`), and a `time` of 0 sets both at once. `getZoomFactor` (0x629d00)
answers -1 past the end of the list.

## The component's fields

| offset | what |
|---|---|
| +0x34 | the level (index into the factors); 0 is not zoomed |
| +0x38 | the tick of the last change |
| +0x3c | a zoom-lod change is waiting (a render detail; not used) |
| +0x3d | signal: toggle (`getSignalToggle` 0x62a7d0, read and cleared) |
| +0x3e | the field of view is waiting to be set |
| +0x44 | a toggle is waiting (set only by `fireComplete`, 0x6295a0, with `zoomOutAfterFire` and a level above 0) |
| +0x48 | the template |

## What it does

* **`toggle(TemplateMessage)`** 0x629c70: for message 7 and a list of at least two
  levels, the next level, round to 0 past the last — `changeZoom(next, false)`.
  `GenericFireArm::internalHandleMessage` (0x5e7506..0x5e753a) calls it when the
  component's input (`getInput`, 0x62bc20) is the one the player pressed.
* **`changeZoom(index, instant)`** 0x629990: nothing when the level is the one it is
  at. It stamps +0x38 with the tick, then: with `changeFovDelay` -1, the camera is
  sent to the factor over `zoomDelay` seconds (0x629b13); with a delay set, level 0
  sets the factor at once (0x629b40) and any other level waits. +0x3e and the level
  are set; a zoom-lod change and the `eventManager` notification 0x1f follow.
* **`updateComponent`** 0x6297e0: with +0x3e set, once `tick >= +0x38 + changeFovDelay
  ticks` it sets the camera's field of view to the level's factor, instantly
  (0x6298df..0x6298f6), and clears the flag — so the default -30 ticks makes the
  blend of `changeZoom` last one update; with +0x3e clear it keeps the camera's end
  field of view equal to the level's factor. With +0x44 set and nothing else of the
  weapon busy it raises the toggle signal (0x629896..0x6298ad), which
  `GenericFireArm::handleUpdate` (0x5e7bc8..0x5e7bdb) turns into `toggleCamera(7)`
  (0x5e2b70).
* **`busy`** 0x629500: a lod change or a toggle waiting, or `tick < +0x38 + zoomDelay
  ticks`.
* **`reload(state)`** 0x629b70: with a level above 0, `changeZoom(0, false)`.
* **`getMouseSpeedMod`** 0x6295c0: the template's +0x50 above level 0, else 1.

## The zoom lod: the inside of the scope

The M24's mesh has two levels in its first-person geometry (`mesh_info`): lod 0, the
rifle (4836 vertices, `usrif_m24_c.dds`), and lod 1 (1022 vertices,
`usrif_m24_scopetexture_c.dds`) — the inside of the scope, with the reticle. `changeZoom`
(0x6299f6..0x629a39) sets it with `zoomLod 1`: level 0 sets lod 0 at once; a zoom in sets
lod `zoomLod` at once when `zoomDelay` is 0 and otherwise leaves a flag (+0x3c) that
`updateComponent` (0x629910..0x62993b) turns into the lod `zoomDelay` ticks after the
change. Ours builds the arms a second time with the weapon at that lod and draws it
instead (`FirstPersonView`, `Model::gpuZoom`). On the live server the scoped sniper
shows the reticle over a view narrowed to 0.2 radians.

## The bolt action (`DefaultAnimationComp`)

`animation.useShiftAnimation` (+0x18, default 0) and `animation.shiftDelay` (+0x1c,
ticks +0x20, default 0) — the M24's are 1 and 1.8 s. `fireComplete` (0x60a840): with the
flag and rounds left (`setAmmoLeft`, +0x54, above 0) a shift is asked for (+0x48).
`updateComponent` (0x60a740): once `GenericFireArm::busy(false)` says nothing else is
busy, `shift()` (0x60a860) raises **message 32** once on the weapon's system (the M24's
`shift` bundle), the cycle starts (+0x4c = the tick) and ends `shiftDelay` ticks later.
`requestFire` (0x60a710) is false while a shift is asked for or under way, and
`GenericFireArm::requestFire` (0x5e4370) asks every component — so a bolt-action rifle
cannot fire again for 1.8 s. Ours: `game::WeaponShift`; on the live server 3 shifts
for 3 rounds. **Not read:** `fire.busyUntilButtonRelease` (the M24 sets it: the fire
component stays busy until the trigger is let go, so the shift would wait for that);
`setAmmoLeft`'s caller; the shift while zoomed.

## A renderer fix this came with

The M24's body was invisible: its material is `Alpha_TestEnvMapColormapGloss` and our
renderer cut every pixel of its colour map with alpha under 127, the static-mesh shader's
reference (`RaShaderSTM.fx`). `BundledMesh.fx` — the shader of weapons and vehicles — sets
`AlphaRef = 0` with `AlphaFunc = GREATER` in every technique (lines 430, 483, 506, 679,
710), so only pixels with an alpha of exactly zero are cut; the colour map's alpha there is
the gloss, mostly low. Ranges of a `BundledMesh.fx` material are now tested against 0.

## Ours, measured

On the live server with the sniper kit (`US_Sniper`, item 3 = `usrif_m24`): a
`--zoom-at 2200:2` press set the field of view to 0.2 — the screenshot shows the
street narrowed about five times, the weapon's hands in the `zoom` pose — a round
fired 300 frames later zoomed out by itself (`zoomOutAfterFire`), and the end report
says `zoom: level 0 of the weapon's 2, field of view 0.000, changes 2`.

## Not read

* (resolved) **Who raises message 4**: `GenericFireArm::handleUpdate` calls, on every update
  where no toggle is due (0x5e7be1..0x5e7c32), `getZoom()` and hands the level to three components:
  the deviation's `setZoom` (+0x2b8, slot 0x118: the crosshair's zoom term), the animation's
  `updateOnZooming` (+0x2c8, slot 0x120: message 4 held at a level above 0, let go at 0 — when
  `animateOnZoom` is on, its default) and the recoil's (+0x2c0, slot 0x128). Ours holds message 4
  while zoomed, which is the same;
* where the **mouse speed mod** is applied to the look; ours scales the axes before
  they are quantized (the M24's is the default 1, so nothing here is measured);
* the **scope's HUD**: the weapon's `altGuiIndex` group (52 for the M24), the
  crosshair's other half (the scope itself is the weapon's lod, below);
* the busy test across the other components (`busyExcept`) before a toggle made by a
  round — ours assumes nothing else is busy — and the weapon-switch reset;
* the field-of-view **blend** for a weapon with the default `changeFovDelay`: the
  camera moves over `zoomDelay`, but the next update sets it outright, so ours steps.
* the zoomed state in the **controlled-object state** (`GhostManager::readControlObjectState`
  calls the weapon's `setZoom`, 0x446a97) — the server's view of our zoom.
