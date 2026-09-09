#!/usr/bin/env python3
"""The BF2 console objects and their static addresses.

    tools/con_objects.py                # all of them
    tools/con_objects.py --grep Hud     # only the ones wanted
    tools/con_objects.py --emit         # docs/functions/con-objects.md

What for: to look into the memory of a live game you have to know *where* to
look. Every object of the `.con` language (`HudBuilder.createTextNode`,
`Scoreboard.…`) is a static instance in the image that registers its own name
at start-up. The registration has a fixed shape, and that is what we catch:

    push  $0            flags
    push  $2
    push  $0x92f2fc     the name — "HudBuilder"
    mov   $0xa18898,%ecx    <- the object itself
    call  registerObject

So the name stands next to the address, and one pass over `.text` yields a
"name -> address" table. The image loads at 0x400000 with no relocations, so
the addresses are the same in the file and in memory.
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_xref  # noqa: E402

# push imm8, push imm8, push <name>, mov <object>,%ecx, call
PATTERN = re.compile(rb"\x6a[\x00-\x03]\x6a[\x00-\x03]\x68(....)\xb9(....)\xe8",
                     re.S)


def objects(exe="BF2.exe"):
    data = open(os.path.join(exe_xref.GAME, exe), "rb").read()
    sections = exe_xref.sections(data)

    def text(address):
        for _, start, size, offset in sections:
            if start <= address < start + size:
                at = address - start + offset
                end = data.index(b"\x00", at)
                raw = data[at:end]
                if 0 < len(raw) < 64 and all(32 <= c < 127 for c in raw):
                    return raw.decode("latin-1")
        return None

    _, start, size, offset = sections[0]
    found = []
    for match in PATTERN.finditer(data[offset:offset + size]):
        name = text(struct.unpack("<I", match.group(1))[0])
        if name and name[0].isalpha():
            found.append((name, struct.unpack("<I", match.group(2))[0]))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default="BF2.exe")
    parser.add_argument("--grep", help="only the names with this inside")
    parser.add_argument("--emit", action="store_true", help="write the notes out")
    args = parser.parse_args()

    found = objects(args.exe)
    if args.grep:
        found = [(n, a) for n, a in found if args.grep.lower() in n.lower()]
    for name, address in found:
        print("%-28s %#x" % (name, address))
    print("\n%d in total" % len(found), file=sys.stderr)

    if args.emit:
        here = os.path.dirname(os.path.abspath(__file__))
        path = os.path.join(here, "..", "docs", "functions", "con-objects.md")
        with open(path, "w") as out:
            out.write("# BF2 console objects and their addresses\n\n")
            out.write("Taken with `tools/con_objects.py` from `%s`. The image loads at\n"
                      "0x400000 with no relocations, so these addresses hold in a live game too\n"
                      "— they can be used to look into memory.\n\n" % args.exe)
            out.write("| object | address |\n|---|---|\n")
            for name, address in found:
                out.write("| `%s` | `%#x` |\n" % (name, address))
        print("written %s" % path, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
