#!/usr/bin/env python3
"""A look inside a `MemeFile 2.0` file — the HUD layer graph.

    tools/meme_dump.py Ingame            # the string dictionary and the numbers
    tools/meme_dump.py Ingame --walk     # an attempt to walk the records
    tools/meme_dump.py --list            # which such files the archives hold

This is not .con but DICE's binary scene graph (`dice::meme::*`). This is where
the corner layers of the HUD lie — BottomLeftStatic, TopLayer and the rest: in
the `.con` they are mentioned as groups but positioned nowhere, because the
anchor is set by this file, not by the interface description.

What is certain already:

* the file begins with a list of strings, each with a one-byte length: first
  the class names (`dice::meme::TransformNode`), then the names of layers and
  variables (`BottomRight/BottomRight_XPos`);
* then comes a body of one root `NameNode`, and a record looks like "a two-byte
  string number + a four-byte size". On an empty file (`BottomLeftStatic`, 45
  bytes) that adds up exactly; on `Ingame` the root covers the whole body to
  the byte;
* 800.0 and 600.0 turn up inside — the same virtual screen the whole HUD is
  laid out in.

What we do not know yet: how a node's own data and its children are separated
inside a record. So --walk gets as far as the first TransformNode and honestly
shows where exactly it lost the thread instead of inventing a layout.
"""
import argparse
import os
import struct
import sys
import zipfile

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "Game Files", "mods", "bf2")
ARCHIVES = ("Menu_client.zip", "Common_client.zip")
MAGIC = b"MemeFile"


def archives():
    for name in ARCHIVES:
        path = os.path.join(MOD, name)
        if os.path.exists(path):
            yield name, zipfile.ZipFile(path)


def find(entry):
    for name, archive in archives():
        for candidate in archive.namelist():
            if candidate.lower() == entry.lower() or candidate.lower().endswith(
                    "/" + entry.lower()):
                data = archive.read(candidate)
                if data.startswith(bytes([len(MAGIC) + 4]) + MAGIC):
                    return name, candidate, data
    return None


def strings(data):
    """The list of strings from the start of the file; also where it ended."""
    out, at = [], 0
    while at < len(data):
        length = data[at]
        if not 1 <= length < 64 or at + 1 + length > len(data):
            break
        text = data[at + 1:at + 1 + length]
        if not all(32 <= c < 127 for c in text):
            break
        out.append(text.decode("latin-1"))
        at += 1 + length
    return out, at


def walk(names, body, at, end, depth, out):
    while at + 6 <= end:
        index, size = struct.unpack_from("<HI", body, at)
        if index >= len(names) or size < 4 or at + 2 + size > end:
            out.append("  " * depth + "lost the thread at %d: next %s" % (
                at, " ".join("%02x" % c for c in body[at:min(end, at + 24)])))
            return
        out.append("  " * depth + "%-34s size %d" %
                   (names[index].replace("dice::meme::", ""), size))
        walk(names, body, at + 8, at + 2 + size, depth + 1, out)
        at += 2 + size


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("entry", nargs="?", help="the file name in the archive, e.g. Ingame")
    parser.add_argument("--list", action="store_true", help="find every such file")
    parser.add_argument("--walk", action="store_true", help="walk the records")
    args = parser.parse_args()

    if args.list or not args.entry:
        for name, archive in archives():
            for candidate in archive.namelist():
                if archive.getinfo(candidate).file_size < 8:
                    continue
                with archive.open(candidate) as handle:
                    if MAGIC in handle.read(16):
                        print("%-22s %-40s %d b" % (name, candidate,
                                                    archive.getinfo(candidate).file_size))
        return 0

    found = find(args.entry)
    if not found:
        print("not found: %s" % args.entry, file=sys.stderr)
        return 1
    archive, entry, data = found
    names, at = strings(data)
    print("%s / %s: %d bytes, %d strings, body from %d" %
          (archive, entry, len(data), len(names), at))

    classes = [n for n in names if n.startswith("dice::meme::")]
    others = [n for n in names if not n.startswith("dice::meme::")]
    print("\nclasses (%d):" % len(classes))
    for name in classes:
        print("  %s" % name.replace("dice::meme::", ""))
    print("\nnames and variables (%d):" % len(others))
    for name in others:
        print("  %s" % name)

    body = data[at:]
    numbers = []
    for i in range(0, len(body) - 4):
        value = struct.unpack_from("<f", body, i)[0]
        if 0.01 < abs(value) < 4096 and abs(value - round(value, 3)) < 1e-9:
            numbers.append((i, value))
    if numbers:
        print("\nnumbers that look like coordinates:")
        for i, value in numbers[:40]:
            print("  +%-5d %g" % (i, value))

    if args.walk:
        print("\nrecords:")
        out = []
        walk(names, body, 1, len(body), 0, out)
        print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
