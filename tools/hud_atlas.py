#!/usr/bin/env python3
"""Name every HUD call of a frame dump by the art it samples.

    tools/hud_atlas.py /path/to/BF2-<pid>.log

The interface of the original does not read its pictures from files. Everything
under `Menu/HUD/Texture/` is packed into two atlas pages — `Menu/Atlas/
MemeAtlas_010.dds` and `_020.dds` — and `Menu/Atlas/MemeAtlas.tai` is the index:
one line per original file with the page and the rectangle inside it, in UV
units. It is the same `.tai` the object light maps use.

So a draw call in a frame dump carries the name of its art in its own vertices:
the first texture coordinate is the corner of that rectangle. This looks it up
and prints, for every two-dimensional call of the last dumped frame, what the
original drew and where — by name.

That is what makes the interface comparable. Matching our rectangles against the
original's by position is guesswork: two icons of the same size a few pixels
apart are indistinguishable. Matching by the art is exact — our own nodes name
their texture in the `.con`, and the atlas index names the original's.

The vertex layout the dump prints for the interface is eleven floats, stride 44:

    x  y  z   r g b a   u0 v0   u1 v1

so the atlas coordinate is at 7 and 8. Colours are the node's tint, which the
game multiplies the art by (`setNodeColor`).
"""
import argparse
import os
import re
import sys
import zipfile

DRAW = re.compile(r"\[dump\] draw (\d+): (.*)")
GEOM = re.compile(r"geom=\[(-?[\d.]+),(-?[\d.]+) ([\d.]+)x([\d.]+) stride=(\d+) verts=(\d+)")
FIRST = re.compile(r"v0=\[([^\]]+)\]")
TEXTURE = re.compile(r"s(\d+)=TextureId\((\d+)\)/(\w+)/(\d+)x(\d+)")


def atlas_index(mod_dir):
    """`Menu/Atlas/MemeAtlas.tai` — the file name of every packed picture."""
    archive = os.path.join(mod_dir, "Menu_client.zip")
    with zipfile.ZipFile(archive) as zf:
        names = [n for n in zf.namelist() if n.lower().endswith("memeatlas.tai")]
        if not names:
            sys.exit(f"no MemeAtlas.tai in {archive}")
        text = zf.read(names[0]).decode("latin-1")

    out = []
    for line in text.splitlines():
        if not line or line.startswith("#") or "\t\t" not in line:
            continue
        name, rest = line.split("\t\t", 1)
        fields = [f.strip() for f in rest.split(",")]
        if len(fields) < 6:
            continue
        page = fields[0].split("\\")[-1]
        u, v, w, h = (float(f) for f in fields[2:6])
        out.append((name.strip(), page, u, v, w, h))
    return out


def name_for(index, u, v, tolerance, page=None):
    """The entry whose rectangle starts at this texture coordinate.

    `page` narrows the search to one atlas page, which halves the wrong answers:
    the two pages are the same size and their rectangles overlap in UV space.
    """
    best = None
    for name, entry_page, eu, ev, ew, eh in index:
        if page is not None and entry_page.lower() != page.lower():
            continue
        distance = abs(eu - u) + abs(ev - v)
        if best is None or distance < best[0]:
            best = (distance, name, entry_page, ew, eh)
    if best is None or best[0] > tolerance:
        return None
    return best


# Which texture id is which atlas page, by the format and size the dump prints.
# `MemeAtlas.tac` says both pages are 2048x2048 and the group is written as
# `dxt5`; what is in the archive is one DXT3 and one DXT5 page, and over every
# `.dds` of the game the pair (DXT3, 2048, 2048) belongs to `MemeAtlas_020`
# alone. Everything else the interface samples is a font page or a picture the
# atlas does not hold.
ATLAS_BY_FORMAT = {
    ("0x33545844", 2048, 2048): "MemeAtlas_020.dds",  # 'DXT3'
    ("0x35545844", 2048, 2048): "MemeAtlas_010.dds",  # 'DXT5'
}


def last_frame(path):
    lines = open(path, "rb").read().decode("latin-1").splitlines()
    starts = [i for i, l in enumerate(lines) if "[dump] frame start" in l]
    return lines[starts[-1]:] if starts else []


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", help="the game's log with a frame dump")
    parser.add_argument("--mod", default="Game Files/mods/bf2", help="the mod directory")
    parser.add_argument("--width", type=float, default=800.0)
    parser.add_argument("--height", type=float, default=600.0)
    parser.add_argument("--tolerance", type=float, default=0.004,
                        help="how far a texture coordinate may sit from an entry's corner")
    args = parser.parse_args()

    index = atlas_index(args.mod)
    print(f"# {len(index)} pictures in MemeAtlas.tai")
    named = 0
    total = 0
    for line in last_frame(args.dump):
        found = DRAW.search(line)
        if not found:
            continue
        rest = found.group(2)
        geom = GEOM.search(rest)
        if not geom:
            continue
        x, y, w, h = (float(v) for v in geom.groups()[:4])
        # The interface lies within the screen; everything else is the world.
        if not (2 < w < args.width + 10 and 2 < h < args.height + 10):
            continue
        total += 1

        # Which page slot 0 samples, if it is an atlas page at all.
        bound = TEXTURE.findall(rest)
        page = None
        for slot, _id, fmt, tw, th in bound:
            if slot == "0":
                page = ATLAS_BY_FORMAT.get((fmt, int(tw), int(th)))

        vertex = FIRST.search(rest)
        values = [float(t) for t in vertex.group(1).split()] if vertex else []
        art = "?"
        tint = ""
        if len(values) >= 9:
            u, v = values[7], values[8]
            if page is None:
                art = "not an atlas page — a font or a texture of its own"
            else:
                match = name_for(index, u, v, args.tolerance, page)
                if match is not None:
                    # The entry's own size, and the call's. A picture drawn
                    # whole matches both; where they disagree the corner belongs
                    # to another quad of the same call — the interface batches a
                    # frame, its icon and its caption into one — and naming it
                    # after the first one would be a guess.
                    art_w = round(match[3] * 2048)
                    art_h = round(match[4] * 2048)
                    fits = abs(art_w - w) <= 2 and abs(art_h - h) <= 2
                    if fits:
                        named += 1
                        art = f"{match[1]}  {art_w}x{art_h}"
                        if match[0] > 1e-6:
                            art += f", corner off by {match[0]:.4f}"
                    else:
                        art = (f"first quad only: {match[1]} {art_w}x{art_h} "
                               f"— the call draws {w:.0f}x{h:.0f}")
                else:
                    art = f"in {page}, no entry at uv {u:.4f},{v:.4f}"
            r, g, b, a = values[3:7]
            if (r, g, b, a) != (1.0, 1.0, 1.0, 1.0):
                tint = f"  tint {r:.2f}/{g:.2f}/{b:.2f}/{a:.2f}"

        textures = ", ".join(f"s{s}=#{i}" for s, i, _, _, _ in bound)
        print("draw %4s  %7.1f %7.1f  %7.1f x %-7.1f  %-12s %s%s" %
              (found.group(1), x + args.width / 2, y + args.height / 2, w, h, textures, art, tint))

    print(f"# {named} of {total} two-dimensional calls named")


if __name__ == "__main__":
    main()
