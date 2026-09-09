#!/usr/bin/env python3
"""Pulls the bridge the menu talks to the game with out of `SwiffPlayer.dll`.

    tools/swiff_bridge.py "Game Files/SwiffPlayer.dll"            # the groups
    tools/swiff_bridge.py "Game Files/SwiffPlayer.dll" --markdown # for docs/
    tools/swiff_bridge.py "Game Files/SwiffPlayer.dll" --objects  # objects only

The BF2 menu is Flash, and not one of the names it calls is in `BF2.exe`: the
whole bridge lives in `SwiffPlayer.dll` (DICE's superstructure over the open
`gameswf`; the path to the source lies in the library itself).

The names in it are laid out the way they were written in the code: first the
list of the bridge's **objects** as one block, then the **methods**, grouped by
object, each group a solid piece of `.rdata`. The script makes use of exactly
that: it cuts the stream of strings into groups where a gap appears between
neighbouring strings (the non-ASCII bytes of the pointer table).

## What it does not do

It does not say **which group belongs to whom**: adjacency in `.rdata` is a
strong hint, but the proof is in the registration code. An independent check
comes from the movie itself: `tools/swf_read.py mainMenu.swf --calls Logic`
shows the menu really calls on `Logic` exactly the names in group 0xdd010.

The tail of the list (from 0xe0794) is not the bridge but ActionScript's own
built-in classes (`MovieClip`, `Key`, `Math`, `XML`); by default it is cut off
by the `--end` bound.
"""
import argparse
import re
import string as _string

# The bridge objects lie as one block; its bounds were found by the strings
# `bf2` and `dice`, which stand right after the list.
OBJECTS = [
    "ControlSettings", "EndOfRound", "Player", "Mod", "Cursor", "Sound",
    "Client", "Options", "Profile", "Clans", "Multiplay", "Singleplay",
    "Render", "Logic", "Locale", "MessageHandler", "General",
]

PRINTABLE = set(_string.printable[:-5].encode())
IDENT = re.compile(rb"^[A-Za-z_][A-Za-z0-9_]{1,63}$")


def strings(data, low, high):
    """Null-terminated strings within the given span of the file."""
    out = []
    at = low
    while at < high:
        end = at
        while end < high and data[end] in PRINTABLE:
            end += 1
        if end > at and end < len(data) and data[end] == 0:
            out.append((at, data[at:end]))
        at = max(end + 1, at + 1)
    return out


def groups(items, gap):
    """Strings -> groups; a new group where the gap is larger than `gap`."""
    if not items:
        return []
    out = []
    current = [items[0]]
    for previous, item in zip(items, items[1:]):
        hole = item[0] - (previous[0] + len(previous[1]) + 1)
        if hole > gap:
            out.append(current)
            current = []
        current.append(item)
    out.append(current)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("path")
    parser.add_argument("--start", type=lambda v: int(v, 16), default=0xD8000,
                        help="the start of the name block in the file (hex)")
    parser.add_argument("--end", type=lambda v: int(v, 16), default=0xDF400,
                        help="the end: past it come ActionScript's built-in classes")
    parser.add_argument("--gap", type=int, default=16, help="the gap that separates groups")
    parser.add_argument("--min", type=int, default=4, help="do not show smaller groups")
    parser.add_argument("--objects", action="store_true", help="the object list only")
    parser.add_argument("--markdown", action="store_true", help="a table for docs/")
    args = parser.parse_args()

    with open(args.path, "rb") as handle:
        data = handle.read()

    if args.objects:
        for name in OBJECTS:
            at = data.find(name.encode() + b"\0")
            print(f"{name:16} {hex(at) if at >= 0 else '?'}")
        return

    found = [(at, text.decode()) for at, text in strings(data, args.start, args.end)
             if IDENT.match(text)]
    blocks = [block for block in groups(found, args.gap) if len(block) >= args.min]

    if args.markdown:
        for block in blocks:
            print(f"\n### {hex(block[0][0])}..{hex(block[-1][0])} — {len(block)} names\n")
            print("```")
            line = []
            for _, name in block:
                line.append(name)
                if len(line) == 4:
                    print("  ".join(line))
                    line = []
            if line:
                print("  ".join(line))
            print("```")
        return

    print(f"names {len(found)}, groups {len(blocks)}")
    for block in blocks:
        print(f"  {hex(block[0][0])}..{hex(block[-1][0])}  {len(block):4}  "
              + ", ".join(name for _, name in block[:5]) + (" …" if len(block) > 5 else ""))


if __name__ == "__main__":
    main()
