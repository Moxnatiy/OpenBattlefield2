# The `ObjectTemplate` registry

Status: **implemented** — `src/game`. **10 182 templates** built from BF2
1.5's data: 55 classes, 16 348 attachments, 381 220 property assignments.

`ObjectTemplate` is Refractor 2's central abstraction. Everything is
described with it: weapons, vehicles, projectiles, effects, sounds, HUD
elements. It accounts for **87 % of all commands** in the game's files
(433 778 of 497 195).

## A state machine, not a tree

The `.con` language is a stream of commands, so the registry works as a
state machine:

```
ObjectTemplate.create PlayerControlObject apc_btr90   ← the template becomes active
ObjectTemplate.geometry apc_btr90                     ← written into the active one
ObjectTemplate.addTemplate APC_BTR90__Turret          ← an attachment
ObjectTemplate.setPosition 0/1.6324/0.9962            ← applies to the LAST child
ObjectTemplate.create RotationalBundle APC_BTR90__Turret  ← a new active one
```

| Command | Calls | What it does |
|---|---:|---|
| `create <class> <name>` | 8 902 | creates a template and makes it active |
| `activeSafe <class> <name>` | 14 392 | **reopens an existing** template |
| `addTemplate <name>` | 16 348 | attaches a child |
| `setPosition` / `setRotation` | 6 197 | applies to the last added child |
| `createComponent <name>` | 5 337 | starts a sub-object |
| `<component>.<property>` | — | writes into the sub-object |
| everything else (855 distinct names) | — | a property of the active template |

## Three things that shape the design

**1. `activeSafe` is the normal way of working, not an exception.** Its
14 392 calls against 8 902 `create`s: the `.con` creates a template and the
`.tweak` reopens it and fills in the details. So the registry must not
create a second template with the same name — half the properties would be
lost.

**2. There are 855 distinct properties — there can be no typed structure
here.** The registry keeps a generic bag of values; typed views (weapon,
vehicle) are built on top when their turn comes.

**3. Some properties accumulate.** `mapMaterial` is called several times
per template (10 561 calls in total). So we keep **every** assignment in
order rather than the last one: `property()` returns the effective value,
`propertyHistory()` the whole history with files and lines. That answers
"where did this value come from", which otherwise means grepping the
archives.

## The vehicle hierarchy

`addTemplate` + `setPosition` assembles a tree of parts, and `geometryPart`
binds each to a part of the BundledMesh:

```
apc_btr90 (PlayerControlObject)
  components Armor (22 properties), VehicleHud, WarningHud, HelpHud, Radio
  -> APC_BTR90_hudPass @ 0/0.0763/0
  -> APC_BTR90__Turret @ 0/1.6324/0.9962
       -> APC_BTR90__BarrelBase @ -0.0091/0.2448/0.9827
            -> APC_BTR90__Barrel @ 0.0628/0.0068/0.0186
```

This is where it becomes clear why `.bundledmesh` carries no node
matrices: the part positions live in the `.con`, not in the mesh.

## Classes

55 distinct ones. The most frequent are `Sound` (3984),
`SpriteParticleSystem` (1283), `SimpleObject` (1136), `EffectBundle`
(532), `Emitter` (508). The gameplay ones: `PlayerControlObject` (129, all
vehicles), `GenericFireArm` (169, weapons), `GenericProjectile` (145),
`RotationalBundle` (304, moving parts).

## Checking

```bash
./build/macos-arm64-debug/tools/object_info/object_info "Game Files/mods/bf2" --all
./build/macos-arm64-debug/tools/object_info/object_info "Game Files/mods/bf2" --tree apc_btr90
```

Out of the game's 497 195 commands only **3** turned out to have no active
template — they are counted separately rather than attributed to a random
object.
