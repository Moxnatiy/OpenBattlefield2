# Collision mesh BSP — load and line walk

Linux server 1.5 (`Game Files/OtherFiles/linuxded-full/bin/amd-64/bf2`),
read with `objdump` while the Ghidra MCP was down; constants with
`tools/elf_symbol.py <address>`. BF2.exe addresses not taken yet.

## Which tree

A `.collisionmesh` lod whose BSP marker is `'1'` goes to `Bsp::load`
(0x708ff0). It does not fill the `BspNode` tree: it allocates a `BspV8`
(0x70 bytes, constructor 0x70f5f0), stores it at `Bsp+0x78` and returns
`BspV8::load` (0x7107b0). `Bsp::getClosestFace` (0x709b20) forwards to
`BspV8::getClosestFace` (0x70ff70) with its arguments in order whenever
`+0x78` is set (0x709b5d); only a tree built at run time (`Bsp::build`
0x707be0) goes to `BspNode::getClosestFace` (0x709700). Every tree read from
the game's files is therefore a `BspV8`.

## File layout — `BspV8::load` 0x7107b0

| field | size | stored at | read at |
|---|---|---|---|
| bounds min | 12 | +0x18 | 0x7107e7 |
| bounds max | 12 | +0x24 | 0x7107fb |
| node count n | 4 | — | 0x710815 |
| nodes | n × 16 | vector +0x30 | 0x710878 |
| face reference count m | 4 | — | 0x71088a |
| face references | m × u16 | vector +0x48 | 0x7108d3 |

`+0x60` holds the lod's vertex vector (0x7107c8). Before reading, 0x70f2e0
builds the face vector (`+0x00`, 0x18 bytes a face) from the lod's faces.

### Node (16 bytes) — read by `getCollidingFaces` 0x70fc40

| offset | size | meaning |
|---|---|---|
| +0x0 | f32 | split position along the axis |
| +0x4 | u32 | bits 0–1 axis (0 x, 1 y, 2 z); bit 2: child 0 is a leaf; bit 3: child 1 is a leaf; bits 16–23 child 0's face count; bits 24–31 child 1's face count; bits 4–15 not read |
| +0x8 | u32 | child 0 (below the split): node index, or for a leaf the first face reference |
| +0xc | u32 | child 1 (at or above the split): the same |

Child `s` is a leaf when `flags & (4 << s)`; its count is
`(flags >> (16 + 8·s)) & 0xff` (0x70fd7f).

### Face (0x18 bytes) — 0x70f2e0

| offset | size | meaning |
|---|---|---|
| +0x0 | 4 | written 0 (0x70f375); purpose not established |
| +0x4 | 12 | normal: `(v3 − v2) × (v2 − v1)`; left alone when `|len² − 1| < 1.1920929e-7` (0xb2bfb0, 1.0 at 0xb2f3bc), zero when `len² < 1.1920929e-7`, otherwise divided by `sqrt(len²)` |
| +0x10 | u16 | material |
| +0x12 | 3 × u16 | v1, v2, v3 |

## The search — `BspV8::getClosestFace` 0x70ff70

`(outPoint, outDistance, outDepth, start, motion, twoSided, flip, stopFirst)`.

1. No nodes → null (0x70ffca).
2. Context: mode 0 (+0x0, 0x71000b; mode 1 is `getCollidingFaces`' face
   copy, 0x70f6e0), done = false (+0x4), start (+0x8), motion (+0x14),
   `1e-4 / |motion|` (+0x20; 1e-4 at 0xb355d0), radius 0 (+0x24),
   flip (+0x28), twoSided (+0x29), stopFirst (+0x2a), best face null
   (+0x38), point (+0x40), best distance `FLT_MAX` (+0x4c, 0x710085),
   depth (+0x50).
3. `|motion|² ≤ 1.4210855e-14` (0xb3e054) → null.
4. `rayRadiusIntersectsAABB` (0x72c640) of the segment, radius 0, against
   the tree's bounds: missed → null. Otherwise t0 = 0 when below 0, else
   `min(1, t0)`; t1 = `min(1, t1)` when ≥ 0, else 0 (0x710112–0x71015b).
5. `getCollidingFaces(nodes[0], t0, t1)` (0x70fc40).
6. A face found: point, distance and depth go to the outputs that are not
   null; the face is returned.

### `rayRadiusIntersectsAABB` 0x72c640

The box grown by the radius on each side; `inv[i] = 1 / motion[i]`. For x
the near/far times are `(min − p)·inv` and `(max − p)·inv`, swapped when
`inv < 0` (0x72c6fb). y the same; the segment misses when `t0x > t1y` or
`t0y > t1x`; then `t0 = maxss(t0y, t0x)`, `t1 = minss(t1y, t1x)`; z the same
against those. Both are stored (0x72c7a9); it hits when `1 ≥ t0` and
`t1 ≥ 0`.

### `getCollidingFaces(node, t0, t1)` 0x70fc40 — the walk

1. `a = max(t0 − e, 0)`, `b = min(t1 + e, 1)` with e = context +0x20; `a ≥ b`
   → return.
2. `p = start + motion·a`, `span = motion·(b − a)`; axis = flags & 3.
3. near side `s = p[axis] ≥ split`. Child s: a node → recurse with (t0, t1);
   a leaf → `findClosestFace(node, s, first, count)` (0x70fd8a).
4. done (context +0x4) → return.
5. Far side `1 − s`:
   * s = 0: x = split − radius. When `x > p[axis]`: `motion[axis] ≤ 0` →
     return; `x − p[axis] > span[axis]` → return; t = `(x − start[axis]) /
     motion[axis]`. Otherwise t = t0.
   * s = 1: x = split + radius. When `p[axis] ≥ x`: `motion[axis] ≥ 0` →
     return; `span[axis] ≥ x − p[axis]` → return; t = the same. Otherwise
     t = t0.
6. Child `1 − s` with (t, t1), a node recursing and a leaf going to
   `findClosestFace` with side `1 − s`.

The near child is always taken first; the far child only when the segment
reaches the plane.

### `findClosestFace(node, side, first, count)` 0x70f960 — a leaf

For each face reference in `[first, first + count)` in order:

1. Unless twoSided: `d = n · motion`; the face is taken only when
   `(d ≥ 0) == flip` (0x70fa10, 0.0 at 0xb2d910) — one-sided, against the
   normal.
2. The normal, negated when flip (0x70fa9b), goes with v1, v2, v3, start and
   `start + motion` to `checkFaceAndEdgeCollision` 0x724ae0 (its last bool
   twoSided). Outputs: point, depth (+0x88), distance (+0x8c).
3. Taken when `best > distance` (0x70fb63) and the point lies on the leaf's
   side: side 1 needs `point[axis] > split − 1e-4`, side 0 `split + 1e-4 >
   point[axis]` (double 1e-4 at 0xb97318).
4. Kept: face, distance, depth, point. stopFirst → done = true and the leaf
   returns (0x70fbd5); otherwise the leaf goes on.

With stopFirst the answer is the first face accepted in walk order: the
nearest leaf first, but inside a leaf by reference order. Without it, every
leaf the segment reaches is tried and the smallest distance kept.

## Who passes what

`CollisionMeshTemplate::getDistance` 0x71bee0 calls 0x709b20 at 0x71c109
with (its third argument, its `0x1a0` argument, its last argument): the
sound's obstruction ray passes (false, false, b2 = true)
(`sound-template.md`, "Obstruction ray"). The ragdoll
(`RagDoll::checkCollisionConstraints`, the call through `CollisionMesh::getDistance`
vtable +0xb8 after 0x6bfbab) passes lod 2, false, and last bools 1 (0x6bfbfb) and 1
(0x6bfbf3) — the same pair as the sound, so stopFirst is true.

## Ours

`mesh::CollisionBsp` (`collision.h`) keeps every field above, read by
`loadCollisionMesh` where the tree used to be skipped; `mesh::closestFace`
walks it, with stopFirst as a parameter. `tests/test_collision_bsp.cpp`
checks the tree's shape and the walk against every face on a real mesh.
The world's queries (`server::CollisionWorld::segmentContact`) do not use it
yet: that, and keeping the nearest object (0x696030), come with the
collision world. Not ported: mode 1 (`copyCollidingFaces`),
the sphere walk (0x70f7e0), the run-time `BspNode` tree (0x709700), and
twoSided in `checkFaceAndEdgeCollision` (ours is one-sided only).
