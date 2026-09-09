#!/usr/bin/env python3
"""The fields of every `dice::meme::*` class — straight from the official library.

    tools/meme_types.py                 # every class, in field order
    tools/meme_types.py TransformNode   # one class
    tools/meme_types.py --slots         # what we take for field types

`MemeDll.dll` and `MemeBf.dll` lie in the mod's directory next to the editor
itself (`MemeEdit.exe`) and, unlike the game, **export full C++ symbols**.
Every class has an `onStream`, and every field there is passed by a separate
stream method — `streamFloat`, `streamInt`, `streamNode`, ... — which takes
**the field's name as a string as its second argument**.

So the file's layout need not be guessed: it is written down in the library
by name. All we do is read the order of the calls.

Stream methods are called virtually, so instead of a name we see an offset in
the table. The offset is constant per method, so it is the type marker; human
names for the known offsets are in SLOTS, the rest stay numbers.
"""
import argparse
import os
import re
import struct
import subprocess
import sys

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "Game Files", "mods", "bf2")
LIBRARIES = ("MemeDll.dll", "MemeBf.dll")

# Offset in the method table -> what it reads. Filled in as each offset gets
# checked against the data; the unknown ones stay numbers.
SLOTS = {
    0x34: "float",       # Alpha, Width, Red
    0x38: "bool",        # Border or not, Focus
    0x3c: "int",         # Col, Frames, Index
    0x48: "string",      # Unicode
    0x4c: "picture",     # Picture, Fill picture
    0x50: "font",        # Font handle
    0x54: "sound",       # Select sound
    0x58: "list",        # Action list, Data list
    0x5c: "index",       # Button type, Source blend func — an enumeration
    0x64: "event",
    0x68: "action",
    0x6c: "data",        # most often: values are taken from separate data nodes
    0x70: "effect",
    0x74: "function",
    0x78: "object",      # Path node, Destination node
    0x7c: "style",
    0x80: "tree",
    0x84: "child",       # Split node, Transformed node
    0x88: "next",        # Next node
}


class Image:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        count = struct.unpack_from("<H", d, pe + 6)[0]
        optional = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.sections = []
        table = pe + 24 + optional
        for i in range(count):
            entry = table + i * 40
            name = d[entry:entry + 8].rstrip(b"\0").decode("latin-1")
            vsize, rva, rsize, raw = struct.unpack_from("<IIII", d, entry + 8)
            self.sections.append((name, rva, max(vsize, rsize), raw))
        self.export_rva = struct.unpack_from("<I", d, pe + 24 + 96)[0]
        self.import_rva = struct.unpack_from("<I", d, pe + 24 + 104)[0]

    def offset(self, rva):
        for _, start, size, raw in self.sections:
            if start <= rva < start + size:
                return raw + (rva - start)
        return None

    def follow(self, address):
        """The exports here lead to the jump table, not to the function itself."""
        at = self.offset(address - self.base)
        if at is not None and self.data[at] == 0xE9:
            return address + 5 + struct.unpack_from("<i", self.data, at + 1)[0]
        return address

    def imports(self):
        """The address of an IAT cell -> a name. MemeBf.dll calls MemeDll exactly
        that way, and without this a call to the parent `onStream` looks like a
        jump into nowhere."""
        if hasattr(self, "_imports"):
            return self._imports
        self._imports = {}
        at = self.offset(self.import_rva)
        if at is None:
            return self._imports
        while True:
            lookup, _, _, name_rva, first = struct.unpack_from("<IIIII", self.data, at)
            if name_rva == 0 and first == 0:
                break
            table = self.offset(lookup or first)
            slot = self.base + first
            while table is not None:
                entry = struct.unpack_from("<I", self.data, table)[0]
                if entry == 0:
                    break
                if not entry & 0x80000000:
                    text = self.string(entry + 2, 512)
                    if text:
                        self._imports[slot] = text
                table += 4
                slot += 4
            at += 20
        return self._imports

    def symbol_at(self, address):
        if not hasattr(self, "_by_address"):
            self._by_address = {}
            self._by_address.update(self.imports())
            for name, raw in self.exports().items():
                # The calls go to the thunk, not to the function itself, so
                # we know both addresses.
                self._by_address.setdefault(raw, name)
                self._by_address.setdefault(self.follow(raw), name)
        return self._by_address.get(address)

    def string(self, rva, limit=128):
        at = self.offset(rva)
        if at is None:
            return None
        end = self.data.find(b"\0", at, at + limit)
        if end < 0:
            return None
        text = self.data[at:end]
        if not text or not all(32 <= c < 127 for c in text):
            return None
        return text.decode("latin-1")

    def exports(self):
        """A name -> an address in memory."""
        d, at = self.data, self.offset(self.export_rva)
        if at is None:
            return {}
        names_count = struct.unpack_from("<I", d, at + 24)[0]
        functions, names, ordinals = struct.unpack_from("<III", d, at + 28)
        out = {}
        for i in range(names_count):
            name_rva = struct.unpack_from("<I", d, self.offset(names) + i * 4)[0]
            name = self.string(name_rva, 512)
            index = struct.unpack_from("<H", d, self.offset(ordinals) + i * 2)[0]
            code = struct.unpack_from("<I", d, self.offset(functions) + index * 4)[0]
            if name:
                out[name] = self.base + code
        return out


CALL = re.compile(r"calll\s+\*(0x[0-9a-f]+)\(%\w+\)")
DIRECT = re.compile(r"calll\s+\*?(0x[0-9a-f]+)$")
PUSH = re.compile(r"pushl\s+\$(0x[0-9a-f]+)")
BASE = re.compile(r"\?onStream@([A-Za-z0-9_]+)@meme@dice@@")
HELPER = re.compile(r"\?stream@(Coordinate2|Rectangle|Color)@meme@dice@@")

# The helpers write several numbers in a row and not through the method table,
# so in the disassembly they look like an ordinary call. What exactly they
# write was read out of themselves: Coordinate2 is two floats, Rectangle is
# two Coordinate2, Color is four floats with these very names.
HELPER_FIELDS = {
    "Rectangle": ["X", "Y", "Width", "Height"],
    "Color": ["Red", "Green", "Blue", "Alpha"],
}


def fields(image, address, end=None):
    """The fields in the order they appear: (name, stream method offset).

    The function's bound is taken from the next export, not "up to the first
    retl": `onStream` often hands the work to the parent class first and comes
    back from the middle, while the fields continue after that.
    """
    if end is None or end <= address:
        end = address + 0x600
    text = subprocess.run(
        ["objdump", "-d", "--no-show-raw-insn",
         "--start-address=%#x" % address, "--stop-address=%#x" % end,
         image.path], capture_output=True, text=True).stdout

    out, pending = [], []
    for line in text.splitlines():
        push = PUSH.search(line)
        if push:
            name = image.string(int(push.group(1), 16) - image.base)
            # Field names are short capitalised words; the other constants
            # (sizes, flags) are of no interest here.
            # A name can carry a note too: "Value <do not edit>".
            if name and re.fullmatch(r"[A-Za-z][A-Za-z0-9 _<>/.-]{0,40}", name):
                pending.append(name)
            continue
        call = CALL.search(line)
        if call and pending:
            out.append((pending[-1], int(call.group(1), 16)))
            pending = []
            continue
        # The class hands the work to the parent `onStream` first — which is
        # exactly why inherited fields stand in the file before its own.
        direct = DIRECT.search(line.split("<")[0].strip())
        if direct and not CALL.search(line):
            target = int(direct.group(1), 16)
            symbol = image.symbol_at(target)
            base = BASE.match(symbol or "")
            if base:
                out.append(("@" + base.group(1), -1))
                continue
            helper = HELPER.match(symbol or "")
            if helper:
                kind = helper.group(1)
                if kind == "Coordinate2":
                    # Arguments are pushed right to left, so the call's first
                    # name is the last of the ones pushed.
                    pair = pending[-2:][::-1] if len(pending) >= 2 else ["X", "Y"]
                    for field in pair:
                        out.append((field, 0x34))
                else:
                    for field in HELPER_FIELDS[kind]:
                        out.append((field, 0x34))
                pending = []
    return out


def symbols(image):
    """An address -> a short name. This is exactly what we take the DLL for: the
    game has no such names, and here they are there for every function."""
    out = {}
    for name, address in image.exports().items():
        target = image.follow(address)
        match = re.match(r"\?([A-Za-z0-9_]+)@([A-Za-z0-9_]+)@meme@dice@@", name)
        short = "%s::%s" % (match.group(2), match.group(1)) if match else name
        out.setdefault(target, short)
    return out


def dump(image, wanted, span=0):
    """A disassembly of a function with the calls labelled.

    The names are the point here: `calll 0x100432a0` says nothing, while
    `calll ... ; IStream::streamInt` says everything. Without them, reading
    someone else's binary is guesswork.
    """
    names = symbols(image)
    starts = sorted(names)
    picked = [(n, a) for n, a in image.exports().items() if wanted in n]
    if not picked:
        return
    for full, address in sorted(picked):
        start = image.follow(address)
        after = [s for s in starts if s > start]
        end = start + span if span else min(after[0] if after else start + 0x400,
                                            start + 0x400)
        text = subprocess.run(
            ["objdump", "-d", "--no-show-raw-insn",
             "--start-address=%#x" % start, "--stop-address=%#x" % end, image.path],
            capture_output=True, text=True).stdout
        print("=== %s  (%s, %#x..%#x)" % (full, os.path.basename(image.path), start, end))
        for line in text.splitlines():
            if ":" not in line[:12]:
                continue
            line = line.strip()
            call = re.search(r"call[lq]?\s+(0x[0-9a-f]+)", line)
            if call and int(call.group(1), 16) in names:
                line += "    ; " + names[int(call.group(1), 16)]
            push = re.search(r"pushl\s+\$(0x[0-9a-f]+)", line)
            if push:
                text_at = image.string(int(push.group(1), 16) - image.base)
                if text_at:
                    line += '    ; "%s"' % text_at
            print("   " + line)
        print()


VTABLE = re.compile(r"movl\s+\$(0x[0-9a-f]+),\s*\(%\w+\)")


def inherited(image, klass):
    """Whose `onStream` the class got when it has none of its own.

    We take its constructor, from there the method table's address, and from
    the table cell 0x30. That is the very one the engine calls, so this is not
    a guess but what will really happen.
    """
    picked = [a for n, a in image.exports().items()
              if n.startswith("??0%s@meme@dice@@" % klass)]
    if not picked:
        return None, None
    start = image.follow(picked[0])
    text = subprocess.run(
        ["objdump", "-d", "--no-show-raw-insn",
         "--start-address=%#x" % start, "--stop-address=%#x" % (start + 0x80),
         image.path], capture_output=True, text=True).stdout
    for line in text.splitlines():
        found = VTABLE.search(line)
        if not found:
            continue
        table = int(found.group(1), 16)
        at = image.offset(table - image.base)
        if at is None:
            continue
        slot = struct.unpack_from("<I", image.data, at + 0x30)[0]
        symbol = image.symbol_at(slot) or ""
        match = re.match(r"\?onStream@([A-Za-z0-9_]+)@meme@dice@@", symbol)
        # There can be a name, and there can be just an address: not every
        # onStream is exported. Then we go by address — it is from the table too.
        return (match.group(1) if match else None, image.follow(slot))
    return None, None


def classes(image):
    """A class -> (the start of onStream, the start of the next function)."""
    starts = sorted({image.follow(a) for a in image.exports().values()})
    out = {}
    for name, address in image.exports().items():
        match = re.match(r"\?onStream@([A-Za-z0-9]+)@meme@dice@@", name)
        if not match:
            continue
        start = image.follow(address)
        after = [s for s in starts if s > start]
        out[match.group(1)] = (start, after[0] if after else start + 0x600)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("klass", nargs="?", help="show one class")
    parser.add_argument("--slots", action="store_true", help="which offsets occur")
    parser.add_argument("--emit", help="write the full list into a file")
    parser.add_argument("--dump", help="disassemble a function by the start of its symbol name")
    parser.add_argument("--span", type=lambda s: int(s, 0), default=0,
                        help="how many bytes to disassemble (0 = up to the next symbol)")
    args = parser.parse_args()

    images = []
    for name in LIBRARIES:
        path = os.path.join(MOD, name)
        if os.path.exists(path):
            images.append(Image(path))
    if not images:
        print("no %s in %s" % (" / ".join(LIBRARIES), MOD), file=sys.stderr)
        return 1

    found = {}
    for image in images:
        for name, span in classes(image).items():
            found[name] = (image, span)

    if args.dump:
        for image in images:
            dump(image, args.dump, args.span)
        return 0

    if args.slots:
        counts = {}
        for name, (image, span) in sorted(found.items()):
            for _, slot in fields(image, span[0], span[1]):
                counts[slot] = counts.get(slot, 0) + 1
        for slot, count in sorted(counts.items()):
            print("  %#-6x %-16s %d fields" % (slot, SLOTS.get(slot, "?"), count))
        return 0

    if args.emit:
        with open(args.emit, "w", encoding="utf-8") as out:
            out.write("# `dice::meme::*` classes and their fields\n\n")
            out.write("Generated by `tools/meme_types.py --emit`. Do not edit by hand.\n\n")
            out.write("The field order is the order in the file: `onStream` calls the\n")
            out.write("stream methods one after another, passing each the field name as\n")
            out.write("a string. The type follows from which method was called.\n\n")
            for name in sorted(found):
                image, span = found[name]
                rows = fields(image, span[0], span[1])
                out.write("## %s (%s)\n\n" % (name, os.path.basename(image.path)))
                if not rows:
                    out.write("No fields of its own.\n\n")
                    continue
                for field, slot in rows:
                    out.write("* `%s` — %s\n" % (field, SLOTS.get(slot, "?%#x" % slot)))
                out.write("\n")
        print("written: %d classes into %s" % (len(found), args.emit))
        return 0

    wanted = sorted(found) if not args.klass else [
        n for n in found if n.lower() == args.klass.lower()]
    if not wanted:
        print("no such class; there are %d" % len(found), file=sys.stderr)
        return 1

    for name in wanted:
        image, span = found[name]
        rows = fields(image, span[0], span[1])
        print("%s (%s, %#x): %d fields" %
              (name, os.path.basename(image.path), span[0], len(rows)))
        for field, slot in rows:
            print("    %-28s %s" % (field, SLOTS.get(slot, "offset %#x" % slot)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
