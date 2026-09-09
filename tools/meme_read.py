#!/usr/bin/env python3
"""Reads a `MemeFile 2.0` file by the layout taken from the official library.

    tools/meme_read.py Ingame            # the tree
    tools/meme_read.py --check           # check every such file
    tools/meme_read.py Ingame --find BottomRight

The layout is not guessed — it is read out of `MemeDll.dll`, which has full
C++ symbols (see docs/formats/hud-meme.md):

* the file: a version string, then a string dictionary, each with a
  one-byte length, up to an **empty** string (`IStream::streamStaticString`);
* the root: `Object::loadNew` reads **only the two-byte class number** and
  hands the word straight to the class itself;
* a nested object: `Object::load` reads a four-byte **size**, a two-byte
  object name, a two-byte class name, and then the fields. The size is
  measured from itself, and it is what the engine skips the unknown by;
* in the class stream a "string" is a number in the dictionary, zero is empty
  (`ClassIStream::streamStaticString`);
* the fields of each class are listed by its `onStream`, and it starts with
  the parent's — so inherited fields come first.
"""
import argparse
import os
import re
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import meme_types  # noqa: E402

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "Game Files", "mods", "bf2")
ARCHIVES = ("Menu_client.zip", "Common_client.zip")
MAGIC = b"MemeFile"

# How many bytes each stream method reads. The numbers are offsets in the
# `IStream` method table, taken from the .rdata of the library itself.
FIXED = {
    0x1c: 1,  # Ubyte
    0x20: 1,  # Sbyte
    0x24: 2,  # Ushort
    0x28: 2,  # Sshort
    0x2c: 4,  # Ulong
    0x30: 4,  # Slong
    0x34: 4,  # Float
    0x38: 1,  # Bool -> Ubyte
    0x3c: 4,  # Int
    0x48: 2,  # Wchar
    0x5c: 4,  # Index -> Int
}
# A resource name: a one-byte length, then the bytes.
NAMED = {0x4c, 0x50, 0x54}
# A nested object.
OBJECT = {0x60, 0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c, 0x80, 0x84, 0x88}
LIST = 0x58


class Tables:
    """The fields of each class, together with the inherited ones."""

    def __init__(self):
        self.own = {}
        self.images = []
        for name in meme_types.LIBRARIES:
            path = os.path.join(MOD, name)
            if not os.path.exists(path):
                continue
            image = meme_types.Image(path)
            for klass, span in meme_types.classes(image).items():
                self.own[klass] = meme_types.fields(image, span[0], span[1])
            self.images.append(image)
        self._resolved = {}

    def fields(self, klass):
        if klass in self._resolved:
            return self._resolved[klass]
        out = []
        self._resolved[klass] = out  # guard against a cycle
        # In the file a class goes by its full name, in the library by a short one.
        table = self.own.get(klass) or self.own.get(klass.split("::")[-1])
        if table is None:
            # The class has no `onStream` of its own — we ask its method
            # table whose it inherited.
            short = klass.split("::")[-1]
            for image in self.images:
                parent, address = meme_types.inherited(image, short)
                if parent and parent != short:
                    out.extend(self.fields(parent))
                    break
                if address:
                    # It has its own `onStream`, just not exported — we read
                    # it at the address from the method table.
                    for name, slot in meme_types.fields(image, address, address + 0x200):
                        if name.startswith("@"):
                            out.extend(self.fields(name[1:]))
                        else:
                            out.append((name, slot))
                    break
            self._resolved[klass] = out
            return out
        for name, slot in table:
            if name.startswith("@"):
                out.extend(self.fields(name[1:]))
            else:
                out.append((name, slot))
        self._resolved[klass] = out
        return out


class Reader:
    def __init__(self, data, tables):
        self.data = data
        self.at = 0
        self.tables = tables
        self.words = []
        self.unknown = set()
        self.short = {}
        self.path = []
        # The bound of the current object: past it no field is read. The
        # engine does the same — here the size outranks the field table.
        self.limit = None

    def ubyte(self):
        value = self.data[self.at]
        self.at += 1
        return value

    def ushort(self):
        value = struct.unpack_from("<H", self.data, self.at)[0]
        self.at += 2
        return value

    def ulong(self):
        value = struct.unpack_from("<I", self.data, self.at)[0]
        self.at += 4
        return value

    def raw_string(self):
        length = self.ubyte()
        text = self.data[self.at:self.at + length]
        self.at += length
        return text.decode("latin-1")

    def word(self):
        """A string in the class stream is a number in the dictionary."""
        index = self.ushort()
        return self.words[index] if 0 < index < len(self.words) else ""

    def header(self):
        version = self.raw_string()
        self.words = [""]
        while True:
            text = self.raw_string()
            if not text:
                break
            self.words.append(text)
        return version

    def value(self, slot):
        if slot in FIXED:
            raw = self.data[self.at:self.at + FIXED[slot]]
            self.at += FIXED[slot]
            if slot == 0x34:
                return round(struct.unpack("<f", raw)[0], 4)
            return int.from_bytes(raw, "little")
        if slot in NAMED:
            return self.raw_string()
        if slot == LIST:
            # The list has no counter: the objects follow one another, and
            # the edge is given by the size of the list's owner. It is visible
            # in the bytes — an object record starts right after the header.
            out = []
            while self.limit is not None and self.at + 8 <= self.limit:
                out.append(self.object())
            return out
        if slot in OBJECT:
            return self.object()
        raise ValueError("unknown stream method %#x" % slot)

    def object(self):
        """A nested object: size, name, class, fields."""
        start = self.at
        size = self.ulong()
        name = self.word()
        klass = self.word()
        node = {"class": klass, "name": name, "fields": {}}
        if klass:
            outer, self.limit = self.limit, start + size
            node["fields"] = self.body(klass)
            self.limit = outer
            # The size lets the tail be skipped — and so hides an incomplete
            # field table. So we check: how much was read against how much
            # there was. A difference means the class is not fully taken apart.
            left = (start + size) - self.at
            if left:
                self.short[klass] = max(self.short.get(klass, 0), left)
        # The size is measured from its own field — that is how the engine
        # skips what it does not know. We do the same: here it is in charge.
        self.at = start + size
        return node if klass else None

    def body(self, klass):
        out = {}
        table = self.tables.fields(klass)
        if not table and klass:
            self.unknown.add(klass)
        for name, slot in table:
            if self.limit is not None and self.at >= self.limit:
                break
            self.path.append("%s.%s" % (klass.split("::")[-1], name))
            out[name] = self.value(slot)
            self.path.pop()
        return out

    def root(self):
        """The root reads differently: only the class number, no size."""
        klass = self.word()
        return {"class": klass, "name": "", "fields": self.body(klass)}


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
                if data[1:1 + len(MAGIC)] == MAGIC:
                    return name, candidate, data
    return None


def every():
    for name, archive in archives():
        for candidate in archive.namelist():
            info = archive.getinfo(candidate)
            if info.file_size < 16 or info.file_size > 1 << 20:
                continue
            data = archive.read(candidate)
            if data[1:1 + len(MAGIC)] == MAGIC:
                yield name, candidate, data


def show(node, depth, out, path=""):
    if node is None:
        return
    label = node["class"] + (" '%s'" % node["name"] if node["name"] else "")
    plain = {k: v for k, v in node["fields"].items()
             if not isinstance(v, (dict, list)) and v not in ("", 0, 0.0)}
    out.append("  " * depth + label + ("  " + str(plain) if plain else ""))
    for key, value in node["fields"].items():
        if isinstance(value, dict):
            out.append("  " * (depth + 1) + key + ":")
            show(value, depth + 2, out)
        elif isinstance(value, list):
            out.append("  " * (depth + 1) + "%s (%d):" % (key, len(value)))
            for item in value:
                show(item, depth + 2, out)


# Offset in the method table -> the type's name in C++. The same offsets as
# in meme_types.SLOTS, only by the names of our own enumeration.
CPP_SLOTS = {
    0x1c: "Ubyte", 0x20: "Sbyte", 0x24: "Ushort", 0x28: "Sshort",
    0x2c: "Ulong", 0x30: "Slong", 0x34: "Float", 0x38: "Bool",
    0x3c: "Int", 0x48: "Wchar", 0x5c: "Index",
    0x4c: "Name", 0x50: "Name", 0x54: "Name",
    0x58: "List",
}


def emit_cpp(tables, path):
    """The "class -> fields" table for the C++ reader.

    It is taken from the same libraries as everything else: nothing here is
    written by hand, and it can be repeated with one command.
    """
    # There are more classes than exported `onStream`s: some have none and
    # inherit another's (NameNode, ShowEffectNode). So we take **every**
    # name that occurs in the library's symbols at all, and run each of them
    # through the same resolution the reader uses.
    seen = set()
    for image in tables.images:
        for symbol in image.exports():
            for name in re.findall(r"@([A-Za-z0-9]+)@meme@dice@@", symbol):
                seen.add(name)
    for klass in sorted(seen):
        tables.fields(klass)
    names = sorted(n for n in tables._resolved if n in seen)
    lines = [
        "// `dice::meme::*` classes and their fields, in reading order.",
        "//",
        "// Generated by `tools/meme_read.py --cpp`. **Do not edit by hand.**",
        "//",
        "// The source is `MemeDll.dll` and `MemeBf.dll` from the mod's directory:",
        "// they export full C++ symbols, and every `onStream` passes the field's",
        "// name as a string, while the field's type is whichever stream method was called.",
        "// Inherited fields are already expanded in place.",
        "",
    ]
    total = 0
    for name in names:
        rows = tables._resolved[name]
        total += 1
        lines.append("MEME_CLASS(%s)" % name)
        for field, slot in rows:
            kind = CPP_SLOTS.get(slot)
            if kind is None:
                kind = {0x60: "Object", 0x64: "Object", 0x68: "Object",
                        0x6c: "Object", 0x70: "Object", 0x74: "Object",
                        0x78: "Object", 0x7c: "Object", 0x80: "Object",
                        0x84: "Object", 0x88: "Object"}.get(slot)
            if kind is None:
                # An unknown stream method: nothing past it can be read,
                # because the width is unknown. We mark it and stop the class.
                lines.append("  MEME_FIELD(\"%s\", Unknown)" % field)
                break
            lines.append("  MEME_FIELD(\"%s\", %s)" % (field, kind))
        lines.append("MEME_CLASS_END()")
        lines.append("")
    with open(path, "w", encoding="utf-8") as out:
        out.write("\n".join(lines))
    print("written: %d classes into %s" % (total, path))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("entry", nargs="?")
    parser.add_argument("--check", action="store_true", help="check every file")
    parser.add_argument("--find", help="show only branches with this in the name")
    parser.add_argument("--cpp", help="write the class table for C++")
    args = parser.parse_args()

    tables = Tables()

    if args.cpp:
        return emit_cpp(tables, args.cpp)

    if args.check or not args.entry:
        good = bad = 0
        for archive, entry, data in every():
            reader = Reader(data, tables)
            try:
                reader.header()
                reader.root()
                left = len(data) - reader.at
                mark = "whole" if left == 0 else "%d left" % left
                good += left == 0
                bad += left != 0
            except Exception as error:  # noqa: BLE001
                mark = "failed: %s" % error
                bad += 1
            print("%-22s %-24s %6d b  %s" % (archive, entry, len(data), mark))
            if reader.unknown:
                print("      classes with no table: %s" % ", ".join(sorted(reader.unknown)))
            if reader.short:
                for klass, left in sorted(reader.short.items(), key=lambda kv: -kv[1]):
                    print("      short by %-42s %d b" % (klass.split("::")[-1], left))
        print("\nfully taken apart: %d, with a remainder or a failure: %d" % (good, bad))
        return 0 if bad == 0 else 1

    found = find(args.entry)
    if not found:
        print("not found: %s" % args.entry, file=sys.stderr)
        return 1
    archive, entry, data = found
    reader = Reader(data, tables)
    version = reader.header()
    tree = reader.root()
    print("%s / %s: %s, %d words, read %d of %d" %
          (archive, entry, version, len(reader.words) - 1, reader.at, len(data)))
    lines = []
    show(tree, 0, lines)
    for line in lines:
        if not args.find or args.find.lower() in line.lower():
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
