# `.baf` — bone animation

The layout was taken **from the engine's code**, not guessed:
`dice::anim::BoneAnimation::load` (0x08338820) and
`dice::anim::CompressedAnim::GetValue` (0x0833ec30) in the Linux server.
Verified on **all 3470 files in the game** — they read with not a single
byte left over.

## The header

```
u32  version (4 everywhere; the engine rejects anything else)
u16  bone count
u16  bone ids[count]              -- indices into the .ske skeleton
u32  frame count
u8   precision
```

The clip's length is `frames / 24`, that is **24 frames per second**.

## The body

For every bone:

```
u16  how many 16-bit words this bone has in total
for each of the 7 channels:
    u16  how many words this channel has
    ...  the channel's words
```

Seven channels in a fixed order: the quaternion's **x, y, z, w**, then the
translation's **x, y, z**. The sum of the channels' words always equals the
number in the bone's header — that is exactly what verifies the layout.

## Compression: runs

A channel's words are a sequence of runs. Each begins with a header word:

```
byte 0:  the run's length in frames (bits 0..6)
         bit 7 — "the whole run has one value"
byte 1:  how many words to the next run's header
then:    int16 values[constant ? 1 : length]
```

Reading the value for a frame: while the frame does not fall inside the
current run, subtract its length and move on by the given step. Then take
either the run's single value or the value at the frame's index inside it.

## Converting to reals

| Channels | Divisor |
|---|---|
| 0..3 (quaternion) | **32767** — always, regardless of precision |
| 4..6 (translation) | `(1 << precision) - 1` |

That this is right is visible at once: after the division the quaternions
come out **of unit length**. Checked on a sample of 40 random animations —
not a single deviation above 0.02.

## An example

`soldiers/Common/Animations/head/face_insane.baf` — 103 bytes: 2 bones (48
and 49), 88 frames, precision 15. All 14 channels are constant, so each takes
exactly 2 words: the header `0xd8 0x02` (length 0x58 = 88 frames, bit 7 set,
step 2 words) and the value itself.

```bash
baf_info "Game Files/mods/bf2" objects/soldiers/Common/Animations/head/face_insane.baf 0
```

## How this becomes a pose

The bone ids in a `.baf` are indices into the `.ske`. Bones the clip does not
have stay in the rest pose.

A `.skinnedmesh` vertex holds **one weight and a pair of bone ids**, packed
into the first two bytes of the D3DCOLOR slot (`BLENDINDICES`); the second
bone gets `1 - weight`. The ids are indices **into the material's rig**, not
into the skeleton: for every bone the rig holds its index in the skeleton and
the inverse bind matrix. A lod has exactly as many rigs as materials — that
holds for every skinned mesh in the game.

The vertex's matrix: `world(bone) * inverse_bind(rig)`.

### The quaternion is transposed

The rotation matrix from a quaternion is **transposed** here relative to the
usual formula: under DirectX the engine works with row vectors and we work
with columns. This is not theory — with the untransposed formula the soldier
comes out with his arms and head inside out, while with the transposed one
the clip `3p_ak47_stand` gives exactly the stance it is supposed to.
`rotationY` in `core/math.h` is transposed the same way.

## Blending clips

One clip is not the whole pose. `3p_ak47_stand` has 45 tracks over 80 bones:
the weapon clips move the upper body, while the legs come from a separate
movement animation (`soldiers/Common/Animations/3P/3p_runForward.baf` —
exactly 16 tracks). The engine lays them on together.

How exactly is visible in `Skeleton::applySimpleAnimationStage`
(0x0834df10):

* every **bone** has its own stack of applied clips, **no more than five**;
* a clip only walks its own `boneIds` list — it does not touch other bones at
  all;
* if a clip's weight is **1 or more**, that bone's stack is **cleared**: the
  clip owns it entirely;
* otherwise the clip is put on top and the oldest entry is evicted.

The weight comes from `BundlePlayer::applyAnimationOnSkeleton`, where it is
multiplied by the transition factor (`getSlerpFactor`), while
`Skeleton::applyAnimation` only queues the entry and clamps the weight to
one.

It can be checked like this — legs from one clip, weapon from another:

```bash
openbf2 --object ch_heavy_soldier \
        --anim objects/soldiers/Common/Animations/3P/3p_runForward.baf \
        --anim objects/Weapons/Handheld/RURIF_AK47/animations/3p/3p_ak47_runforward.baf \
        --frame 4
```

## Cross-checking against Project Dalian

Dalian (MIT) says in its `docs/formats/README.md` that the rotation channels
are **inverted on read**. I checked against the original: in `GetValue` the
multiplier is loaded by the instruction `flds 0x88cc0e8`, and at that address
in `.rodata` lies a **positive** `1/32767`. So the engine inverts nothing.

There is no contradiction here: inverting a quaternion's vector is the same as
transposing the rotation matrix, and that is needed because of DirectX's row
vectors. Both implementations give the same result, they just explain it from
different sides. We keep the transposition because it makes the reason
visible.

## What is still missing

Triggers and bundles (`dice::anim::AnimationSystem`, `Bundle`,
`BundlePlayer`) — that is, what decides **which** clip to play and at what
weight depending on the player's state. For now clips are given by hand.

A pose can be looked at like this:

```bash
openbf2 --object ch_heavy_soldier \
        --anim objects/Weapons/Handheld/RURIF_AK47/animations/3p/3p_ak47_stand.baf \
        --frame 0
```
