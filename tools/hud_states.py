#!/usr/bin/env python3
"""The HUD state table — read out of BF2.exe rather than by hand.

    tools/hud_states.py            # the state table in markdown
    tools/hud_states.py --cpp      # the same list as C++

Two engine functions switch the HUD state through 32-entry jump tables:
0x7862ce (table 0x786f88) and 0x786751 (table 0x787008).
Each handler is a chain of identical 25-byte blocks:

    8B 1E              mov  ebx, [esi]        ; vtable
    6A vv              push the value (0 or 1)
    83 EC 1C           sub  esp, 0x1c         ; room for an std::string
    8B CC              mov  ecx, esp
    68 <addr>          push the address of the variable's name
    FF 15 6C F4 87 00  call std::string(char*)
    8B CE              mov  ecx, esi
    FF 53 0C           call [ebx+0xc]         ; setVariable(name, value)

The handlers stand one after another and fall through, so they have to be read
from the state's entry to the first instruction that is not one of these blocks.
"""
import argparse
import os
import struct
import sys

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Game Files", "BF2.exe")
TABLE = 0x787008          # the jump table of the second function
SLOTS = 32
END = 0x786f59  # the shared exit: past it the blocks are not ours



class Image:
    """A PE in memory: turning a virtual address into an offset in the file."""

    def __init__(self, path):
        self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3c)[0]
        sections = struct.unpack_from("<H", self.data, pe + 6)[0]
        opt = struct.unpack_from("<H", self.data, pe + 20)[0]
        self.base = struct.unpack_from("<I", self.data, pe + 24 + 28)[0]
        self.sections = []
        at = pe + 24 + opt
        for _ in range(sections):
            name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", self.data, at)
            self.sections.append((va, vsize, raw, rsize))
            at += 40

    def offset(self, va):
        rva = va - self.base
        for sva, vsize, raw, rsize in self.sections:
            if sva <= rva < sva + max(vsize, rsize):
                return raw + (rva - sva)
        raise KeyError(hex(va))

    def read(self, va, size):
        at = self.offset(va)
        return self.data[at:at + size]

    def va(self, offset):
        """The reverse of offset: an offset in the file -> a virtual address."""
        for sva, vsize, raw, rsize in self.sections:
            if raw <= offset < raw + rsize:
                return self.base + sva + (offset - raw)
        return 0

    def string(self, va):
        at = self.offset(va)
        end = self.data.index(b"\0", at)
        return self.data[at:end].decode("ascii", "replace")


# A tiny linear decoder: the handlers are a flat chain of seven kinds of
# instruction, and those are all we need. Anything else means the end.
def handler(image, va, limit=8192):
    """The (variable, value) pairs from the handler's entry to the shared exit."""
    out = []
    value = None
    name = None
    for _ in range(limit):
        if va == END:
            break
        code = image.read(va, 6)
        if code[0:2] in (b"\x8b\x1e", b"\x8b\x3e", b"\x8b\xcc", b"\x8b\xce"):
            va += 2                       # mov ebx/edi,[esi] | mov ecx,esp/esi
        elif code[0] == 0x6A:
            value = code[1]               # push the value
            va += 2
        elif code[0:3] == b"\x83\xec\x1c":
            va += 3                       # sub esp,0x1c — room for the string
        elif code[0] == 0x68:
            name = image.string(struct.unpack_from("<I", code, 1)[0])
            va += 5                       # push the name's address
        elif code[0:2] == b"\xff\x15":
            va += 6                       # call std::string(char*)
        elif code[0:3] in (b"\xff\x53\x0c", b"\xff\x57\x0c"):
            if name is not None and value is not None:
                out.append((name, value))  # setVariable(name, value)
            name = value = None
            va += 3
        elif code[0] == 0xE9:
            va += 5 + struct.unpack_from("<i", code, 1)[0]  # jmp into the shared tail
        elif code[0] == 0xEB:
            va += 2 + struct.unpack_from("<b", code, 1)[0]
        else:
            # Other calls turn up inside a handler too (sound, a request to
            # the player). We jump over them to the next block — but only
            # within the handler itself: the branches here do not fall into
            # one another, each one ends with a jump into the shared tail.
            block = image.read(va, 160)
            step = -1
            for i in range(1, len(block) - 5):
                if block[i] == 0x6A and block[i + 2:i + 5] == b"\x83\xec\x1c":
                    step = i
                    break
            if step < 0:
                break
            va += step
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp", action="store_true")
    args = parser.parse_args()
    if not os.path.exists(EXE):
        sys.exit("BF2.exe not found: " + EXE)
    image = Image(EXE)
    table = struct.unpack_from("<%dI" % SLOTS, image.read(TABLE, SLOTS * 4))
    empty = max(set(table), key=table.count)  # the shared empty handler

    states = []
    for state, entry in enumerate(table):
        states.append((state, entry, handler(image, entry)))

    if args.cpp:
        print("// Created by tools/hud_states.py from BF2.exe, table 0x%x." % TABLE)
        print("static const HudState kHudStates[] = {")
        for state, entry, pairs in states:
            if entry == empty:
                continue
            print("    // state %d, handler 0x%x" % (state, entry))
            print("    {%d," % state)
            print("     {%s}}," % ", ".join(
                '{"%s", %d}' % (name, value) for name, value in pairs))
        print("};")
        return

    print("| state | handler | turns on | turns off |")
    print("|---|---|---|---|")
    for state, entry, pairs in states:
        if entry == empty:
            print("| %d | 0x%x | *(empty)* | |" % (state, entry))
            continue
        on = [n for n, v in pairs if v]
        off = [n for n, v in pairs if not v]
        print("| %d | 0x%x | %s | %d: %s |" %
              (state, entry, ", ".join("`%s`" % n for n in on) or "—",
               len(off), ", ".join("`%s`" % n for n in off) or "—"))


if __name__ == "__main__":
    main()
