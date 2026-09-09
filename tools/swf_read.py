#!/usr/bin/env python3
"""Reads the game's menu — it is Flash, and these files lie uncompressed.

    tools/swf_read.py <file.swf>                 # header and labels
    tools/swf_read.py <file.swf> --symbols       # exported names
    tools/swf_read.py <file.swf> --text          # text fields
    tools/swf_read.py <file.swf> --actions       # ActionScript strings
    tools/swf_read.py <file.swf> --actions --grep disconnect

What for. The Refractor 2 menu is not `.con` but Macromedia Flash: the engine
turns it with a player of its own (`dice.hfe.geom.FSMoviePlayer` in the string
table of `BF2.exe`), and the movies themselves lie in
`mods/bf2/Menu_client/External/FlashMenu/`. We are not going to play Flash
back (docs/research/03-startup-and-menu.md), but **the structure of the menu
is exactly there**: which screens exist, what the buttons are called, which
localisation keys they show and which commands they send into the game.

Both of the game's movies are `FWS`, that is **uncompressed**: the body is
read right after the header. `CWS` (zlib) and `ZWS` (LZMA) are recognised too,
zlib is inflated, LZMA is not (the game has none of it).

## The layout we read by

The header: the signature (3 bytes), the version (1), the file length (4).
Then the stage `RECT` (a bit field: 5 bits of width, then four fields of that
many bits), the frame rate (16.16) and the frame count (2).

The body is a sequence of tags: a `u16` whose top 10 bits are the tag code and
the bottom 6 the length; a length of 0x3f means the real length follows in the
next `u32`.

ActionScript 2 lives inside the `DoAction` (12) and `DoInitAction` (59) tags:
byte code where an opcode < 0x80 has no body and one >= 0x80 has a `u16`
length. Two actions interest us:

* `ActionConstantPool` (0x88) — the movie's string table;
* `ActionPush` (0x96) — values onto the stack, among them strings and
  references into the table.

Together they give almost all the readable content: function names,
localisation keys, screen names.

## The limit

This is **not a decompiler**. The order of the strings here is the order they
appear in the byte code, not the menu's logic; who calls whom is not visible
from it. For how the engine opens the menu and what it is allowed to call,
look at `BF2.exe` (rule 12).
"""
import argparse
import struct
import sys
import zlib

# The tags that interest us. The rest are counted but not taken apart.
TAGS = {
    0: "End", 1: "ShowFrame", 9: "SetBackgroundColor", 12: "DoAction",
    26: "PlaceObject2", 37: "DefineEditText", 39: "DefineSprite",
    43: "FrameLabel", 56: "ExportAssets", 59: "DoInitAction",
    69: "FileAttributes", 76: "SymbolClass", 82: "DoABC",
}


class Reader:
    def __init__(self, data, at=0):
        self.d = data
        self.at = at

    def u8(self):
        value = self.d[self.at]
        self.at += 1
        return value

    def u16(self):
        value = struct.unpack_from("<H", self.d, self.at)[0]
        self.at += 2
        return value

    def u32(self):
        value = struct.unpack_from("<I", self.d, self.at)[0]
        self.at += 4
        return value

    def f32(self):
        value = struct.unpack_from("<f", self.d, self.at)[0]
        self.at += 4
        return value

    def f64(self):
        # Flash puts a double as two words the other way round — high word first.
        high = self.d[self.at:self.at + 4]
        low = self.d[self.at + 4:self.at + 8]
        self.at += 8
        return struct.unpack("<d", low + high)[0]

    def text(self):
        end = self.d.index(b"\0", self.at)
        value = self.d[self.at:end]
        self.at = end + 1
        return value.decode("utf-8", "replace")

    def more(self):
        return self.at < len(self.d)


def unpack(path):
    """A file -> (version, the body after the size header)."""
    with open(path, "rb") as handle:
        raw = handle.read()
    signature, version, length = raw[:3], raw[3], struct.unpack_from("<I", raw, 4)[0]
    body = raw[8:]
    if signature == b"CWS":
        body = zlib.decompress(body)
    elif signature == b"ZWS":
        raise SystemExit("ZWS (LZMA) is not read — the game has none")
    elif signature != b"FWS":
        raise SystemExit(f"not SWF: {signature!r}")
    return signature.decode(), version, length, body


def rect(reader):
    """RECT: 5 bits of width, then four fields of that many bits."""
    bits = reader.d[reader.at] >> 3
    total = 5 + bits * 4
    size = (total + 7) // 8
    chunk = int.from_bytes(reader.d[reader.at:reader.at + size], "big")
    chunk >>= size * 8 - total
    values = []
    for i in range(4):
        shift = bits * (3 - i)
        value = (chunk >> shift) & ((1 << bits) - 1)
        if value & (1 << (bits - 1)):  # signed
            value -= 1 << bits
        values.append(value / 20.0)  # twips -> pixels
    reader.at += size
    return values


def tags(body):
    """A body -> a list of (code, name, bytes)."""
    reader = Reader(body)
    rect(reader)
    reader.u16()  # frame rate, 16.16
    reader.u16()  # frame count
    out = []
    while reader.more():
        head = reader.u16()
        code, length = head >> 6, head & 0x3F
        if length == 0x3F:
            length = reader.u32()
        chunk = reader.d[reader.at:reader.at + length]
        reader.at += length
        out.append((code, TAGS.get(code, str(code)), chunk))
        if code == 0:
            break
    return out


def actions(chunk, with_pool=True):
    """Byte code -> the strings in the order they appear.

    `with_pool=False` leaves **only what is really put onto the stack**.
    Then the order becomes meaningful: `Options.getAutoReload()` in
    ActionScript 2 is "push the object name, push the method name, call", so
    neighbouring strings in this list are the pair "object, method".
    """
    reader = Reader(chunk)
    pool = []
    out = []
    while reader.more():
        code = reader.u8()
        if code == 0:
            break
        if code < 0x80:
            continue
        length = reader.u16()
        end = reader.at + length
        if code == 0x88:  # ActionConstantPool
            count = reader.u16()
            pool = []
            while reader.at < end and len(pool) < count:
                pool.append(reader.text())
            if with_pool:
                out.extend(pool)
        elif code == 0x96:  # ActionPush
            while reader.at < end:
                kind = reader.u8()
                if kind == 0:
                    out.append(reader.text())
                elif kind == 1:
                    reader.f32()
                elif kind in (2, 3):
                    pass
                elif kind in (4, 5, 8):
                    index = reader.u8()
                    if kind == 8 and index < len(pool):
                        out.append(pool[index])
                elif kind == 6:
                    reader.f64()
                elif kind == 7:
                    reader.u32()
                elif kind == 9:
                    index = reader.u16()
                    if index < len(pool):
                        out.append(pool[index])
                else:
                    break
        reader.at = end
    return out


def walk(items, want, seen=None):
    """Tags, including those nested in DefineSprite (39)."""
    for code, name, chunk in items:
        if code == want:
            yield chunk
        if code == 39:  # DefineSprite: a tag stream of its own inside
            inner = Reader(chunk)
            inner.u16()  # identifier
            inner.u16()  # frames
            nested = []
            while inner.more():
                head = inner.u16()
                sub, length = head >> 6, head & 0x3F
                if length == 0x3F:
                    length = inner.u32()
                piece = inner.d[inner.at:inner.at + length]
                inner.at += length
                nested.append((sub, TAGS.get(sub, str(sub)), piece))
                if sub == 0:
                    break
            yield from walk(nested, want)


def edit_text(chunk):
    """DefineEditText: the variable's name and the initial text."""
    reader = Reader(chunk)
    reader.u16()
    rect(reader)
    flags = reader.u16()
    has_text = bool(flags & 0x0080)
    has_font = bool(flags & 0x0001)
    has_colour = bool(flags & 0x0400)
    has_max = bool(flags & 0x0200)
    has_layout = bool(flags & 0x2000)
    if has_font:
        reader.u16()
        reader.u16()
    if has_colour:
        reader.at += 4
    if has_max:
        reader.u16()
    if has_layout:
        reader.at += 1 + 2 + 2 + 2 + 2
    variable = reader.text()
    initial = reader.text() if has_text else ""
    return variable, initial


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("path")
    parser.add_argument("--symbols", action="store_true", help="exported names (ExportAssets)")
    parser.add_argument("--text", action="store_true", help="text fields (DefineEditText)")
    parser.add_argument("--actions", action="store_true", help="ActionScript strings")
    parser.add_argument("--calls", metavar="OBJECT", action="append",
                        help="methods called on this bridge object")
    parser.add_argument("--labels", action="store_true", help="frame labels (FrameLabel)")
    parser.add_argument("--grep", help="keep the lines with this substring (case-insensitive)")
    parser.add_argument("--unique", action="store_true", help="no repeats, alphabetical")
    args = parser.parse_args()

    signature, version, length, body = unpack(args.path)
    items = tags(body)

    if not (args.symbols or args.text or args.actions or args.labels or args.calls):
        print(f"{signature}, version {version}, {length} bytes, {len(items)} tags")
        counts = {}
        for code, name, _ in items:
            counts[name] = counts.get(name, 0) + 1
        for name, count in sorted(counts.items(), key=lambda pair: -pair[1]):
            print(f"  {count:6}  {name}")
        return

    lines = []
    if args.symbols:
        for chunk in walk(items, 56):
            reader = Reader(chunk)
            for _ in range(reader.u16()):
                reader.u16()
                lines.append(reader.text())
    if args.labels:
        for chunk in walk(items, 43):
            lines.append(Reader(chunk).text())
    if args.text:
        for chunk in walk(items, 37):
            try:
                variable, initial = edit_text(chunk)
            except (IndexError, ValueError):
                continue
            if variable or initial:
                lines.append(f"{variable}\t{initial}")
    if args.actions:
        for want in (12, 59):
            for chunk in walk(items, want):
                lines.extend(actions(chunk))
    if args.calls:
        wanted = set(args.calls)
        for want in (12, 59):
            for chunk in walk(items, want):
                pushed = actions(chunk, with_pool=False)
                for first, second in zip(pushed, pushed[1:]):
                    if first in wanted:
                        lines.append(f"{first}.{second}")

    if args.grep:
        needle = args.grep.lower()
        lines = [line for line in lines if needle in line.lower()]
    if args.unique:
        lines = sorted(set(lines))
    for line in lines:
        print(line)


if __name__ == "__main__":
    main()
