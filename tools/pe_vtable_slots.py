#!/usr/bin/env python3
"""Print a 32-bit PE vtable slot by slot, naming trivial field accessors.

    tools/pe_vtable_slots.py RendDX9.dll 0x10221928
    tools/pe_vtable_slots.py BF2.exe 0x646f10 --forwarders 0x6473f0

For each slot the tool prints the target and, when the body is one of the
plain MSVC accessor shapes, the object field it reads or writes:

    fld dword [ecx+X]; ret            -> get float +X
    mov eax,[ecx+X]; ret              -> get dword +X
    mov al,[ecx+X]; ret               -> get byte +X
    lea eax,[ecx+X]; ret              -> address +X
    push ebp; mov ebp,esp; mov eax,[ebp+8]; mov [ecx+X],eax; pop ebp; ret 4
                                      -> set dword +X
    ... mov al,[ebp+8]; mov [ecx+X],al ...  -> set byte +X

Anything else is printed as "other". The table ends at the first entry that
is not inside the executable section.

--forwarders START STOP scans a code range instead of a table for the BF2.exe
wrapper shape `cmp [ecx+0x64],0 ... call/jmp [eax+S]`, which forwards a game
template's property to slot S of its renderer object. Each function's start
and the slot it reaches are printed.

The patterns are byte patterns: a compiler variant, a different register or a
frame-pointer-less body is "other", never a guess.
"""
import argparse
import re
import struct
import sys


class Pe:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        count = struct.unpack_from("<H", self.data, pe + 6)[0]
        optional = struct.unpack_from("<H", self.data, pe + 20)[0]
        self.base = struct.unpack_from("<I", self.data, pe + 24 + 28)[0]
        self.sections = []
        for i in range(count):
            o = pe + 24 + optional + 40 * i
            name = self.data[o:o + 8].rstrip(b"\0").decode()
            vsize, va, rsize, raw = struct.unpack_from("<IIII", self.data, o + 8)
            flags = struct.unpack_from("<I", self.data, o + 36)[0]
            self.sections.append((name, self.base + va, max(vsize, rsize), raw, rsize, flags))

    def offset(self, address):
        for _, va, size, raw, rsize, _ in self.sections:
            if va <= address < va + size and address - va < rsize:
                return raw + address - va
        return None

    def executable(self, address):
        return any(va <= address < va + size and flags & 0x20000000
                   for _, va, size, _, _, flags in self.sections)

    def read(self, address, n):
        o = self.offset(address)
        return None if o is None else self.data[o:o + n]


def disp(b, at, wide):
    return struct.unpack_from("<I", b, at)[0] if wide else b[at]


def accessor(code):
    if code is None or len(code) < 16:
        return "other"
    for op, mod8, mod32, what in ((b"\xd9", 0x41, 0x81, "get float"), (b"\x8b", 0x41, 0x81, "get dword"),
                                  (b"\x8a", 0x41, 0x81, "get byte"), (b"\x8d", 0x41, 0x81, "address")):
        if code[0:1] == op and code[1] == mod8 and code[3] == 0xC3:
            return "%s +%#x" % (what, code[2])
        if code[0:1] == op and code[1] == mod32 and code[6] == 0xC3:
            return "%s +%#x" % (what, disp(code, 2, True))
    # push ebp; mov ebp,esp; mov eax|al,[ebp+8]; mov [ecx+X],eax|al; pop ebp; ret 4
    if code[0:3] == b"\x55\x8b\xec":
        if code[3:6] in (b"\x8b\x45\x08", b"\x8a\x45\x08"):
            kind = "dword" if code[3] == 0x8B else "byte"
            store = code[6:8]
            if store in (b"\x89\x41", b"\x88\x41") and code[9:13] == b"\x5d\xc2\x04\x00":
                return "set %s +%#x" % (kind, code[8])
            if store in (b"\x89\x81", b"\x88\x81") and code[12:16] == b"\x5d\xc2\x04\x00":
                return "set %s +%#x" % (kind, disp(code, 8, True))
    return "other"


def forwarders(pe, start, stop):
    o0, o1 = pe.offset(start), pe.offset(stop)
    if o0 is None or o1 is None:
        sys.exit("range outside the file")
    blob = pe.data[o0:o1]
    # Function starts: `cmp dword [ecx+0x64],0` or `push esi; mov esi,ecx; cmp [esi+0x64],0`.
    for m in re.finditer(rb"\x83\x79\x64\x00|\x56\x8b\xf1\x83\x7e\x64\x00", blob):
        body = blob[m.start():m.start() + 48]
        slot = re.search(rb"\xff([\x50\x60])(.)|\xff([\x90\xa0])(....)", body, re.S)
        if not slot:
            continue
        if slot.group(1):
            kind, value = slot.group(1)[0], slot.group(2)[0]
        else:
            kind, value = slot.group(3)[0], struct.unpack("<I", slot.group(4))[0]
        verb = "call" if kind in (0x50, 0x90) else "jmp"
        print("%#x  %s slot +%#x" % (start + m.start(), verb, value))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("binary")
    parser.add_argument("address", type=lambda s: int(s, 0))
    parser.add_argument("--forwarders", type=lambda s: int(s, 0), metavar="STOP",
                        help="scan ADDRESS..STOP for renderer-slot forwarders instead")
    parser.add_argument("--max", type=int, default=128, help="largest slot count to print")
    args = parser.parse_args()
    pe = Pe(args.binary)
    if args.forwarders is not None:
        forwarders(pe, args.address, args.forwarders)
        return
    for i in range(args.max):
        raw = pe.read(args.address + 4 * i, 4)
        if raw is None:
            break
        target = struct.unpack("<I", raw)[0]
        if not pe.executable(target):
            print("end at slot +%#x (%#x)" % (4 * i, target))
            break
        print("+%#05x  %#x  %s" % (4 * i, target, accessor(pe.read(target, 16))))


if __name__ == "__main__":
    main()
