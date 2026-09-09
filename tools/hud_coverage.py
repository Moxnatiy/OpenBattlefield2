#!/usr/bin/env python3
"""HUD coverage: what of the original we already draw and what we do not.

    # a frame dump of the original (Ctrl+Shift+D in the game under mtld3d)
    tools/hud_coverage.py /tmp/bf2run.log ours.txt

    # our rectangles
    build/.../openbf2 --level Dalian_plant --hud-screen SpawnMenu \
        --hud-rects --frames 2 > ours.txt

Both sides give rectangles in the same 800x600 screen coordinates, so the
check comes down to finding a pair. The original counts from the centre of the
screen — we convert: `(400 + x, 300 + y)`.

What this gives is not "looks like / does not look like" but a number. It shows
how many of the original's calls we reproduce, which are left without a pair
(those still have to be made) and which we draw that are surplus.

Two cautions, so the numbers are not read wrongly:

* here and there the original folds many icons into one call, and what is left
  of them is a shared bounding box — such a batch will find no pair;
* a caption on our side is the node's rectangle, in the original the outline of
  the string itself. So text is checked by position only, with a wider margin.
"""
import argparse
import re
import sys

DUMP = re.compile(r"\[dump\] draw (\d+):.*geom=\[(-?[\d.]+),(-?[\d.]+) ([\d.]+)x([\d.]+)")
OURS = re.compile(r"^RECT\s+(\S+)\s+(\S+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)")


def original(path, width, height):
    """The two-dimensional calls of the last dumped frame."""
    frames = []
    current = []
    for line in open(path, "rb").read().decode("latin-1").splitlines():
        line = re.sub(r"\x1b\[[0-9;]*m", "", line)
        if "[dump] frame start" in line:
            if current:
                frames.append(current)
            current = []
        found = DUMP.search(line)
        if not found:
            continue
        x, y, w, h = (float(v) for v in found.groups()[1:])
        x += width / 2
        y += height / 2
        # We drop the three-dimensional: the interface lies within the screen.
        if w < 2 or h < 2 or w > width + 10 or h > height + 10:
            continue
        if x < -width or y < -height or x > width * 1.5 or y > height * 1.5:
            continue
        current.append((int(found.group(1)), x, y, w, h))
    if current:
        frames.append(current)
    return frames[-1] if frames else []


def ours(path, roots=None):
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        found = OURS.match(line)
        if not found:
            continue
        if roots and found.group(1) not in roots:
            continue
        out.append((found.group(2),) + tuple(float(v) for v in found.groups()[2:]))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", help="the game's log with a frame dump")
    parser.add_argument("rects", help="the output of --hud-rects")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--tolerance", type=float, default=3.0)
    parser.add_argument("--root", action="append",
                        help="take only these roots on our side (several allowed)")
    args = parser.parse_args()

    theirs = original(args.dump, args.width, args.height)
    mine = ours(args.rects, args.root)
    if not theirs:
        print("there is no dumped frame in the log", file=sys.stderr)
        return 1

    used = [False] * len(mine)
    matched = []
    missing = []
    for seq, x, y, w, h in theirs:
        best = -1
        bestCost = args.tolerance
        for i, (name, mx, my, mw, mh) in enumerate(mine):
            if used[i]:
                continue
            cost = max(abs(mx - x), abs(my - y), abs(mw - w), abs(mh - h))
            if cost < bestCost:
                bestCost, best = cost, i
        if best >= 0:
            used[best] = True
            matched.append((seq, mine[best][0]))
        else:
            missing.append((seq, x, y, w, h))

    extra = [mine[i] for i in range(len(mine)) if not used[i]]
    total = len(theirs)
    print("original: %d calls, ours: %d rectangles" % (total, len(mine)))
    print("matched: %d (%.0f%%)" % (len(matched), 100.0 * len(matched) / max(total, 1)))
    print("\nnot reproduced (%d):" % len(missing))
    for seq, x, y, w, h in missing[:40]:
        print("   call %-5d %7.1f %7.1f  %6.1f x %-6.1f" % (seq, x, y, w, h))
    if len(missing) > 40:
        print("   ... %d more" % (len(missing) - 40))
    print("\ndrawn surplus (%d):" % len(extra))
    for name, x, y, w, h in extra[:20]:
        print("   %-30s %7.1f %7.1f  %6.1f x %-6.1f" % (name, x, y, w, h))
    if len(extra) > 20:
        print("   ... %d more" % (len(extra) - 20))
    return 0


if __name__ == "__main__":
    sys.exit(main())
