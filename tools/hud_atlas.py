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
# The whole call's piece of texture, and the quads it is made of. A quad is
# `x,y,w,h` in pixels and, since `mtld3d_frame_dump_uv.patch`, `@u,v,w,h` in
# texture coordinates. Both are absent in dumps taken before that patch.
WHOLE_UV = re.compile(r" uv=\[([\d.,-]*)\]")
QUADS = re.compile(r"quads=\[([^\]]*)\]")


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
    named_quads = 0
    total_quads = 0
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
        tint = ""
        if len(values) >= 7:
            r, g, b, a = values[3:7]
            if (r, g, b, a) != (1.0, 1.0, 1.0, 1.0):
                tint = f"  tint {r:.2f}/{g:.2f}/{b:.2f}/{a:.2f}"

        textures = ", ".join(f"s{s}=#{i}" for s, i, _, _, _ in bound)
        header = "draw %4s  %7.1f %7.1f  %7.1f x %-7.1f  %-12s" % (
            found.group(1), x + args.width / 2, y + args.height / 2, w, h, textures)

        if page is None:
            print(f"{header} not an atlas page — a font or a texture of its own{tint}")
            continue

        # Every rectangle of the call, with the piece of texture it shows. A
        # dump taken before `mtld3d_frame_dump_uv.patch` has neither, and then
        # only the whole call's first coordinate is known.
        pieces = []
        quads = QUADS.search(rest)
        if quads and "@" in quads.group(1):
            for quad in quads.group(1).split():
                screen, _, uv = quad.partition("@")
                sx, sy, sw, sh = (float(f) for f in screen.split(","))
                uu, vv, uw, vh = (float(f) for f in uv.split(","))
                pieces.append((sx + args.width / 2, sy + args.height / 2, sw, sh, uu, vv, uw, vh))
        else:
            whole = WHOLE_UV.search(rest)
            if whole and whole.group(1):
                uu, vv, uw, vh = (float(f) for f in whole.group(1).split(","))
                pieces.append((x + args.width / 2, y + args.height / 2, w, h, uu, vv, uw, vh))
            elif len(values) >= 9:
                # The oldest dumps: one corner, no extent.
                pieces.append((x + args.width / 2, y + args.height / 2, w, h,
                               values[7], values[8], None, None))

        if not pieces:
            print(f"{header} no texture coordinates in this dump{tint}")
            continue

        print(f"{header} {len(pieces)} quad(s){tint}")
        for sx, sy, sw, sh, uu, vv, uw, vh in pieces:
            total_quads += 1
            match = name_for(index, uu, vv, args.tolerance, page)
            if match is None:
                print("        %7.1f %7.1f  %7.1f x %-7.1f  in %s, no entry at uv %.4f,%.4f"
                      % (sx, sy, sw, sh, page, uu, vv))
                continue
            art_w = round(match[3] * 2048)
            art_h = round(match[4] * 2048)
            # The entry's own size against the one the call cut out. They agree
            # for a picture shown whole; a disagreement means the coordinate
            # landed on a neighbour in the atlas, and a name would be a guess.
            fits = uw is None or (abs(match[3] - uw) < 0.002 and abs(match[4] - vh) < 0.002)
            if fits:
                named_quads += 1
                note = "" if match[0] < 1e-6 else f", corner off by {match[0]:.4f}"
                print("        %7.1f %7.1f  %7.1f x %-7.1f  %s  %dx%d%s"
                      % (sx, sy, sw, sh, match[1], art_w, art_h, note))
            else:
                print("        %7.1f %7.1f  %7.1f x %-7.1f  uv %.4f,%.4f %.4fx%.4f — "
                      "nearest is %s (%dx%d), which is not this size"
                      % (sx, sy, sw, sh, uu, vv, uw, vh, match[1], art_w, art_h))

    print(f"# {total} two-dimensional calls, {named_quads} of {total_quads} quads named")


if __name__ == "__main__":
    main()
