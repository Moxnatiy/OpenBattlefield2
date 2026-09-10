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

# The dump prints the vertices the game handed to Direct3D 9, and D3D9 puts a
# pixel's centre at an integer coordinate, so a rectangle covering pixels 10..255
# is written 9.5..255.5. Metal — and every API after D3D9 — puts the centre at
# integer + 0.5, so the same rectangle is written 10.0..256.0. mtld3d's own vertex
# shader adds exactly that half pixel back for pre-transformed geometry
# (`dxso/ff.rs`, the XYZRHW branch: "+ 1.0 / vp.x", "- 1.0 / vp.y").
#
# So the dump's numbers are half a pixel below what lands on screen, and half a
# pixel below what our renderer writes for the same node. We add it here, and the
# two sides then agree exactly. Reading the raw numbers instead made every one of
# our rectangles look shifted by +0.5, +0.5 against the original — a difference
# that never existed.
HALF_PIXEL = 0.5

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


def read_ours(path):
    """Our own `--hud-rects` lines, the last rebuild of the spawn screen.

    A rebuild starts again at the first node, so the last run of the list is
    the state the frame ended in.
    """
    rows = []
    for line in open(path, encoding="utf-8", errors="replace"):
        if not line.startswith("RECT SpawnMenu"):
            continue
        f = line.split(None, 7)
        texture = f[7].split()[0] if len(f) > 7 else ""
        rows.append((float(f[3]), float(f[4]), float(f[5]), float(f[6]), texture, f[2]))
    if not rows:
        return rows
    first = rows[0][5]
    starts = [i for i, r in enumerate(rows) if r[5] == first]
    return rows[starts[-1]:]


def art_key(path):
    """The file name without its directory or extension — the two sides spell
    the same picture differently (`Menu/HUD/Texture/Ingame/...` against
    `Ingame/...`, `.tga` against `.dds`), and the name itself is unique."""
    return path.replace("\\", "/").rsplit("/", 1)[-1].rsplit(".", 1)[0].lower()


def pair(theirs, ours):
    """The two sides by the art each draws: matched, missing, surplus."""
    print("\n# paired by the art, theirs %d named quads against ours %d rectangles" %
          (len(theirs), len(ours)))
    taken = set()
    missing = []
    print("\n# the same picture, and where each side puts it")
    for x, y, w, h, name in theirs:
        key = art_key(name)
        candidates = [(i, o) for i, o in enumerate(ours) if art_key(o[4]) == key and i not in taken]
        if not candidates:
            missing.append((x, y, w, h, name))
            continue
        i, o = min(candidates, key=lambda p: abs(p[1][0] - x) + abs(p[1][1] - y))
        taken.add(i)
        print("  %-28s theirs %6.1f,%6.1f %5.1fx%-5.1f  ours %6.1f,%6.1f %5.1fx%-5.1f  "
              "off by %+.1f,%+.1f  %s" %
              (key, x, y, w, h, o[0], o[1], o[2], o[3], o[0] - x, o[1] - y, o[5]))
    print("\n# the original draws these and we draw them nowhere")
    for x, y, w, h, name in missing:
        print("  %-28s %6.1f,%6.1f %5.1fx%-5.1f" % (art_key(name), x, y, w, h))
    print("\n# we draw these and the original's named quads have no such picture")
    for i, o in enumerate(ours):
        if i in taken or "fonts/" in o[4].lower():
            continue
        print("  %-28s %6.1f,%6.1f %5.1fx%-5.1f  %s" % (art_key(o[4]), o[0], o[1], o[2], o[3], o[5]))


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
    parser.add_argument("--ours", help="our own `--hud-rects` output, to pair the two sides by art")
    args = parser.parse_args()

    index = atlas_index(args.mod)
    ours = read_ours(args.ours) if args.ours else None
    named_rows = []
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
        x += HALF_PIXEL
        y += HALF_PIXEL
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
                sx += HALF_PIXEL
                sy += HALF_PIXEL
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
                named_rows.append((sx, sy, sw, sh, match[1]))
                note = "" if match[0] < 1e-6 else f", corner off by {match[0]:.4f}"
                print("        %7.1f %7.1f  %7.1f x %-7.1f  %s  %dx%d%s"
                      % (sx, sy, sw, sh, match[1], art_w, art_h, note))
            else:
                print("        %7.1f %7.1f  %7.1f x %-7.1f  uv %.4f,%.4f %.4fx%.4f — "
                      "nearest is %s (%dx%d), which is not this size"
                      % (sx, sy, sw, sh, uu, vv, uw, vh, match[1], art_w, art_h))

    print(f"# {total} two-dimensional calls, {named_quads} of {total_quads} quads named")
    if ours is not None:
        pair(named_rows, ours)


if __name__ == "__main__":
    main()
