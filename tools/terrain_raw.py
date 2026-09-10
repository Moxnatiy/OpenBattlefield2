#!/usr/bin/env python3
"""Read a level's compiled terrain, `Levels/<name>/terraindata.raw`.

This is the file the game loads and the editor writes, and it is the one thing
between us and the terrain the original draws: the heights, the patch grid and —
the reason we came — the terrain **materials**, which name the detail textures
the ground is textured with close up.

The layout is not guessed. `TerrainEditable::save` writes it field by field
(`RendDX9.dll`, FUN_1010cd70, 0x1010cd70; the assert beside it names
`Code\\BF2\\Geom\\TerrainEditable.cpp`), and this script walks the same order:

    u32   version                 0x0001001a
    vec3  primaryWorldScale       terrain.primaryWorldScale
    vec3  secondaryWorldScale     terrain.secondaryWorldScale
    f32   (uninitialised in the writer — 0xcdcdcdcd on every level)
    f32   highest height          the max over every patch
    f32   lowest height           the min over every patch
    u32   patchSize               terrain.patchSize
    u8    subdividePatches        terrain.subdividePatches
    u32   patches per side
    u32   patchColormapSize
    u32   lowDetailmapSize
    str   colormapBaseName        each string ends with a newline, no length
    str   detailmapBaseName
    str   lowDetailmapBaseName
    str   lightmapBaseName
    f32x2 farSideTiling           terrain.farSideTiling
    f32   farTopTilingHi / Low
    f32   farYOffset
    vec3  terrain.sunColor        the same pair Sky.con's `else` branch sets
    vec3  terrain.GIColor
    vec3  terrainWaterColor
    u32   6                       six terrain materials, always six
    6 x { str texture, u8, f32x2 tiling, f32 distance, f32, u8 }
    then one block per patch, and up to eight secondary terrains

Usage:
    tools/terrain_raw.py "Game Files/mods/bf2/levels/strike_at_karkand" [--hex N]
"""
import argparse
import pathlib
import struct
import sys
import zipfile


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def u32(self):
        value = struct.unpack_from("<I", self.data, self.at)[0]
        self.at += 4
        return value

    def i32(self):
        value = struct.unpack_from("<i", self.data, self.at)[0]
        self.at += 4
        return value

    def u8(self):
        value = self.data[self.at]
        self.at += 1
        return value

    def f32(self):
        value = struct.unpack_from("<f", self.data, self.at)[0]
        self.at += 4
        return value

    def vec3(self):
        return (self.f32(), self.f32(), self.f32())

    def string(self):
        """A string is written with a newline after it and nothing before it."""
        end = self.data.index(b"\n", self.at)
        text = self.data[self.at:end].decode("latin-1")
        self.at = end + 1
        return text

    def raw(self, count):
        chunk = self.data[self.at:self.at + count]
        self.at += count
        return chunk


def read_terrain(data, hex_bytes=0):
    r = Reader(data)
    out = {}
    out["version"] = r.u32()
    out["primaryWorldScale"] = r.vec3()
    out["secondaryWorldScale"] = r.vec3()
    out["unknownFloat"] = r.u32()  # as bits: the writer never initialises it
    out["highestHeight"] = r.f32()
    out["lowestHeight"] = r.f32()
    out["patchSize"] = r.u32()
    out["subdividePatches"] = r.u8()
    out["patchesPerSide"] = r.u32()
    out["patchColormapSize"] = r.u32()
    out["lowDetailmapSize"] = r.u32()
    out["strings"] = [r.string() for _ in range(4)]
    # The tilings, in the order the shader's `vFarTexTiling` wants them, and
    # then the two colours the light map is multiplied by, and the water's.
    out["farSideTiling"] = (r.f32(), r.f32())
    out["farTopTilingHi"] = r.f32()
    out["farTopTilingLow"] = r.f32()
    out["farYOffset"] = r.f32()
    out["sunColor"] = r.vec3()
    out["giColor"] = r.vec3()
    out["waterColor"] = r.vec3()

    # The terrain's materials — the reason this file matters. Each names the
    # detail texture the ground is textured with close up, and carries its
    # tilings and the distance it is used to.
    out["materials"] = []
    count = r.u32()
    for _ in range(count):
        material = {
            "texture": r.string(),
            "flagA": r.u8(),
            "tiling": (r.f32(), r.f32()),
            "distance": r.f32(),
            "unknownFloat": r.f32(),
            "flagB": r.u8(),
        }
        out["materials"].append(material)
    out["afterMaterialsAt"] = r.at
    out["afterStringsHex"] = r.data[r.at:r.at + max(hex_bytes, 64)].hex(" ")
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("level", help="a level directory, or the .raw itself")
    parser.add_argument("--hex", type=int, default=64, help="bytes to show after the strings")
    args = parser.parse_args()

    path = pathlib.Path(args.level)
    if path.is_dir():
        client = path / "client.zip"
        if client.exists():
            with zipfile.ZipFile(client) as archive:
                names = [n for n in archive.namelist() if n.lower().endswith("terraindata.raw")]
                if not names:
                    sys.exit(f"no terraindata.raw in {client}")
                data = archive.read(names[0])
        else:
            data = (path / "terraindata.raw").read_bytes()
    else:
        data = path.read_bytes()

    out = read_terrain(data, args.hex)
    print(f"{path}  ({len(data)} bytes)")
    print(f"  version              {out['version']:#010x}")
    print("  primaryWorldScale    %g / %g / %g" % out["primaryWorldScale"])
    print("  secondaryWorldScale  %g / %g / %g" % out["secondaryWorldScale"])
    print(f"  (uninitialised)      {out['unknownFloat']:#010x}")
    print("  heights              %.2f .. %.2f" % (out["lowestHeight"], out["highestHeight"]))
    print(f"  patchSize            {out['patchSize']}")
    print(f"  subdividePatches     {out['subdividePatches']}")
    print(f"  patches per side     {out['patchesPerSide']}")
    print(f"  patchColormapSize    {out['patchColormapSize']}")
    print(f"  lowDetailmapSize     {out['lowDetailmapSize']}")
    for name in out["strings"]:
        print(f"  string               {name!r}")
    print("  farSideTiling        %g / %g" % out["farSideTiling"])
    print("  farTopTiling         hi %g, low %g, yOffset %g"
          % (out["farTopTilingHi"], out["farTopTilingLow"], out["farYOffset"]))
    print("  terrain.sunColor     %g / %g / %g" % out["sunColor"])
    print("  terrain.GIColor      %g / %g / %g" % out["giColor"])
    print("  terrainWaterColor    %g / %g / %g" % out["waterColor"])
    print(f"  materials            {len(out['materials'])}")
    for i, material in enumerate(out["materials"]):
        print("    [%d] %-46s flags %d/%d  tiling %g/%g  distance %g  f %g"
              % (i, material["texture"], material["flagA"], material["flagB"],
                 material["tiling"][0], material["tiling"][1], material["distance"],
                 material["unknownFloat"]))
    print(f"  after the materials  {out['afterMaterialsAt']:#x}")
    print(f"  next bytes           {out['afterStringsHex']}")


if __name__ == "__main__":
    main()
