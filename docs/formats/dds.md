# Textures: DDS

Status: **implemented** — `src/texture`. **2229 of BF2 1.5's 2230** files
parse, 838 MB of pixels. The one exception is a volume texture (below).

## What is actually in the game

A sweep over every archive gave exactly six variants:

| Format | Files | What it is |
|---|---:|---|
| DXT5 (BC3) | 999 | base colours with alpha |
| DXT1 (BC1) | 502 | base colours without alpha, effects |
| A4R4G4B4 | 337 | mostly fonts and interface elements |
| A8R8G8B8 | 251 | sky, glints, sprites |
| DXT3 (BC2) | 131 | textures with hard alpha |
| R5G6B5 | 8 | water displacement maps |
| 8-bit | 1 | one interface icon |

**No DX10 headers and no cube maps** — which is exactly why the parser can
stay small. The single volume texture is
`common/textures/watervolume.dds`; for now it is rejected with an explicit
explanation rather than silently.

1400 of the 2229 files carry a full mip chain.

## Layout

A classic DDS: the `"DDS "` signature, a 124-byte header, then the level
data one after another. The format is determined from `DDS_PIXELFORMAT` at
offset 76:

- `DDPF_FOURCC` (0x4) → `DXT1`/`DXT3`/`DXT5`;
- `DDPF_RGB` (0x40) → look at the bit count and the channel masks;
- otherwise 8 bits → single channel.

## Two traps

**Compressed levels are counted in 4×4 blocks even when smaller than a
block.** A 1×1 DXT5 level takes a whole 16 bytes, not part of a block.
Count "by pixels" and every following level's offset slides — turning the
texture into noise.

**The header's level count cannot be trusted.** The game has files with
`mipCount = 0` that actually have one level. The parser takes as many
levels as physically fit in the file and stops — so a truncated file gives
a shorter chain rather than a read past the buffer.

## Which material slot is the base colour

Mesh materials have up to four texture slots. The distribution of suffixes
across the 4524 materials of all the game's `.staticmesh` files leaves no
doubt:

| Slot | Suffix | Times | What it is |
|---|---|---:|---|
| 0 | `_c` | 4524 | base colour |
| 1 | `_de` | 4391 | detail |
| 2 | `_deb` / `_di` | 3251 / 945 | detail normal / dirt |
| 3 | `_pow36` / `_deb` / `_cr` | 2604 / 939 / 131 | specular / normal / cracks |

The technique names confirm it: `BaseDetailNDetail`,
`BaseDetailDirtNDetail`, `BaseDetailDirtCrackNDetailNCrack` — they
literally list the slots in order. The renderer currently takes slot 0
only.

## Checking

```bash
./build/macos-arm64-debug/tools/texture_info/texture_info "Game Files/mods/bf2" --all
./build/macos-arm64-debug/tools/mesh_info/mesh_info "Game Files/mods/bf2" --materials
```

A textured object on screen:

```bash
./build/macos-arm64-debug/src/app/openbf2 \
  --mesh "objects/staticobjects/common/com_objects/crates/woodencrate_3m/meshes/woodencrate_3m.staticmesh"
```

BC1/BC2/BC3 go to the GPU as they are — Metal on Apple Silicon supports
them, so nothing has to be decompressed on the CPU. The format is checked
at run time through `SDL_GPUTextureSupportsFormat`.
