#!/usr/bin/env python3
"""The game's control actions: every `c_GI*` / `c_PI*` name with its number.

    tools/control_ids.py              # the whole table, sorted by number
    tools/control_ids.py c_GIShowScoreboard 0x37
                                      # only these names or numbers

`Settings/Controls.con` binds keys to action **names**; the engine's input
handlers test action **numbers** (`InputEvent_pressed(action)`, BF2.exe
0x404580). The table between the two is built by BF2.exe at 0x68f87b, one
block per action:

    68 <addr>          push the address of the name
    8D 4D C8           lea  ecx, [ebp - 0x38]
    FF 15 6C F4 87 00  call std::string(char*)
    6A nn / 68 nnnnnnnn push the number
    5A                 pop  edx
    ...                call 0x68f099, register the pair

So the numbers are read here from the instructions, not retyped. A handler
that tests `push 0x37` is then named by this list: 0x37 is
`c_GIShowScoreboard`, the key `Controls.con` puts on Tab.
"""
import argparse
import os
import struct
import sys

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Game Files", "BF2.exe")
START = 0x68F87B  # the function that registers the pairs
SPAN = 0x3000     # past its end; the scan stops being fed pairs well before it
STRING_CTOR = bytes.fromhex("ff156cf48700")


class Image:
    """A PE in memory: a virtual address to an offset in the file and back."""

    def __init__(self, path):
        self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        self.base = struct.unpack_from("<I", self.data, pe + 0x34)[0]
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        optional = struct.unpack_from("<H", self.data, pe + 0x14)[0]
        table = pe + 0x18 + optional
        self.sections = []
        for i in range(count):
            at = table + i * 40
            vsize, vaddr, rsize, raw = struct.unpack_from("<IIII", self.data, at + 8)
            self.sections.append((vaddr, max(vsize, rsize), raw))

    def offset(self, address):
        rva = address - self.base
        for vaddr, size, raw in self.sections:
            if vaddr <= rva < vaddr + size:
                return raw + rva - vaddr
        return None

    def cstring(self, address):
        at = self.offset(address)
        if at is None:
            return None
        end = self.data.find(b"\0", at, at + 128)
        if end < 0:
            return None
        text = self.data[at:end]
        if not text or any(c < 0x20 or c > 0x7E for c in text):
            return None
        return text.decode("ascii")


def read_table(image):
    """Every (number, name) pair the registration function sets up, in order."""
    start = image.offset(START)
    code = image.data[start:start + SPAN]
    pairs = []
    i = 0
    while i < len(code) - 16:
        if code[i] != 0x68:
            i += 1
            continue
        address = struct.unpack_from("<I", code, i + 1)[0]
        name = image.cstring(address)
        ctor = code.find(STRING_CTOR, i + 5, i + 5 + 12)
        if name is None or not name.startswith("c_") or ctor < 0:
            i += 1
            continue
        j = ctor + len(STRING_CTOR)
        if code[j] == 0x6A and code[j + 2] == 0x5A:        # push imm8; pop edx
            number = code[j + 1]
        elif code[j] == 0x68 and code[j + 5] == 0x5A:      # push imm32; pop edx
            number = struct.unpack_from("<I", code, j + 1)[0]
        else:
            i += 1
            continue
        pairs.append((number, name, START + i))
        i = j
    return pairs


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("wanted", nargs="*", help="names or numbers (0x.. or decimal) to show")
    args = parser.parse_args()

    if not os.path.exists(EXE):
        sys.exit(f"no BF2.exe at {EXE}")
    pairs = read_table(Image(EXE))
    if not pairs:
        sys.exit("no pairs found at 0x68f87b: the layout above does not match")

    wanted_numbers = set()
    wanted_names = set()
    for item in args.wanted:
        try:
            wanted_numbers.add(int(item, 0))
        except ValueError:
            wanted_names.add(item.lower())

    for number, name, at in sorted(pairs, key=lambda p: (p[1][:4], p[0])):
        if args.wanted and number not in wanted_numbers and name.lower() not in wanted_names:
            continue
        print(f"0x{number:02x} {number:3d}  {name:32s} registered at 0x{at:06x}")
    if not args.wanted:
        print(f"{len(pairs)} actions")


if __name__ == "__main__":
    main()
