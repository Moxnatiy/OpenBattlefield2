#!/usr/bin/env python3
"""Find literal 32-bit address references in every PE section, including vtables.

This is a byte search, not instruction decoding: matches may be data or false
positives and do not establish a call. Relative calls are deliberately omitted.
Reuses the existing PE section reader; never executes or modifies the binary.
"""
import argparse
from pathlib import Path
import struct

from exe_xref import sections


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('address', type=lambda value: int(value, 0))
    parser.add_argument('--words', type=int, default=4)
    parser.add_argument('--rtti', action='store_true', help='Inspect the x86 MSVC vtable locator/type-name data instead of searching references')
    args = parser.parse_args()
    if not 0 <= args.address <= 0xffffffff or not 0 <= args.words <= 32:
        parser.error('Expected 32-bit address and 0..32 context words')
    data = args.binary.read_bytes()
    if args.rtti:
        parts = sections(data)
        def read(address, count):
            for _, base, size, raw in parts:
                offset = raw+address-base
                if base<=address and address+count<=base+size and offset+count<=len(data):
                    return data[offset:offset+count]
            raise ValueError(f'Address outside file-backed PE sections: {address:#x}')
        locator = struct.unpack('<I',read(args.address-4,4))[0]
        if not locator:
            print(f'vtable {args.address:#x}: no RTTI locator (preceding word is zero)')
            return
        words = struct.unpack('<5I',read(locator,20))
        print(f'vtable {args.address:#x} locator {locator:#x} words '+str([hex(w) for w in words]))
        name = bytearray()
        for i in range(256):
            value = read(words[3]+8+i,1)[0]
            if not value:break
            name.append(value)
        print(f'type descriptor {words[3]:#x} name {name.decode("ascii",errors="replace")}')
        return
    needle = struct.pack('<I', args.address)
    for name, base, size, raw in sections(data):
        end = min(len(data), raw + size)
        offset = data.find(needle, raw, end)
        while offset >= 0:
            print(f'{name} reference 0x{base + offset - raw:08x} file+0x{offset:x}')
            for at in range(max(raw, offset - args.words * 4),
                            min(end - 3, offset + (args.words + 1) * 4), 4):
                print(f'  0x{base + at - raw:08x}: 0x{struct.unpack_from("<I", data, at)[0]:08x}')
            offset = data.find(needle, offset + 1, end)


if __name__ == '__main__':
    main()
