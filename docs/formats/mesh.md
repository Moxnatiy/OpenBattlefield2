# Meshes: `.staticmesh` / `.bundledmesh` / `.skinnedmesh`

Status: **implemented** — `src/mesh`. **1635 of BF2 1.5's 1635** meshes
parse (1105 static, 506 bundled, 24 skinned), 8.94 M vertices, 2.22 M
triangles in lod0. Not a single error.

Source of the layout: [Project Dalian](https://github.com/chronic8000/ProjectDalian)
(MIT, `engine/formats/mesh`) and
[BfMeshView](http://www.bytehazard.com/bfstuff/bfmeshview/). The
implementation is our own — with bounds checks on every read.

## One family, three extensions

All three formats are the same container; the type **cannot be determined
from the contents**, only from the file extension. The difference comes
down to a few branches:

| | static | bundled | skinned |
|---|---|---|---|
| `alphaMode` in the material | yes | yes | **no** |
| nodes (matrices) in a lod | yes | the counter is there, the matrices **are not** | no |
| bone rigs | no | no | yes |
| `u2` after the indices | yes | yes | **no** |
| material `bounds` (v11) | yes | yes | no |

In a BundledMesh the part transforms (turret, barrel, wheels) live not in
the mesh but in the `.con` (`geometryPart`) — hence a node counter with no
matrices.

## Read order (little-endian)

```
Header      u32 u1, u32 version, u32 u3, u32 u4, u32 u5
u8          game marker (1 = Battlefield Play4Free)
u32         geomCount
  u32         lodCount           ← counters only; the lods themselves are at the end of the file
u32         attributeCount
  u16 flag, u16 offset, u16 vartype, u16 usage
u32         vertexFormat         ← component size, always 4
u32         vertexStride         ← bytes per vertex
u32         vertexCount
float[]     vertexCount * stride/format
u32         indexCount
u16[]       indexCount
u32         u2                   ← except skinned
for each geom, for each lod:
  float3 min, float3 max
  float3 pivot                   ← version <= 6 only
  skinned: u32 rigCount, for each: u32 boneCount, {u32 id, float[16]}[]
  otherwise: u32 nodeCount, float[16][nodeCount]   ← matrices only in static
for each geom, for each lod:
  u32 materialCount
    u32 alphaMode                ← except skinned
    string fxFile, string technique      (string = u32 length + bytes)
    u32 mapCount, string maps[]
    u32 vertexStart, indexStart, indexCount, vertexCount
    u32 nodeIndex, u16 u5, u16 u6
    float3 boundsMin, float3 boundsMax   ← version == 11 only, and not skinned
```

The key surprise: **the geom/lod tables are split**. Only the counters sit
at the start of the file, while the lods' contents come right at the end,
in two separate passes (all the nodes first, then all the materials). They
have to be read in exactly that order.

## Vertex attributes

`usage` is DirectX 9's `D3DDECLUSAGE`: `0` = POSITION, `3` = NORMAL,
`5` = TEXCOORD, `6` = TANGENT. `flag != 0` (255 occurs in the files) means
a disabled channel — such attributes must be skipped or the offsets slide.

`usage` also encodes the channel number: `0x105` is TEXCOORD1, `0x205` is
TEXCOORD2 (the baked light map). TEXCOORD0 is plain `5`; for geometry that
is enough.

**BLENDINDICES (`usage = 2`) has `vartype = 4`, that is D3DCOLOR** — four
bytes in one float slot, not a number. In a BundledMesh the low byte is
the part number (`geometryPart`) by which a vehicle is assembled — see
[vehicle-assembly.md](vehicle-assembly.md).

A material's indices are counted **from that material's `vertexStart`**,
not from the start of the buffer — they have to be made absolute when
unpacking.

## Triangle winding

Counter-clockwise. Measured over all 2 218 586 triangles in the game: the
geometric normal agrees with the vertex normals in 99.66 % of cases. So
back-face culling is on.

## Safety

The files come from the user's archives, so the parser is built around a
bounds-checked reader: any read past the end puts it into an error state
for good, and every counter is checked against how many bytes are
physically left in the file. The tests cover both cases — a file truncated
at every offset, and a counter replaced with `0xFFFFFFFF`.

## Checking

```bash
./build/macos-arm64-debug/tools/mesh_info/mesh_info "Game Files/mods/bf2" --all
```

One mesh in detail:

```bash
./build/macos-arm64-debug/tools/mesh_info/mesh_info "Game Files/mods/bf2" \
  objects/water/meshes/waterplane_128.staticmesh
```

It shows 4 vertices, 2 triangles, bbox `-64/0/-64 .. 64/0/64` — exactly a
128×128 water plane, as it should be. A handy way to be sure the parser is
not lying.
