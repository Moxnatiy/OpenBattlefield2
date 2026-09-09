#!/usr/bin/env python3
"""The value of a global variable straight out of the BF2 Linux server's ELF.

The `linuxded/bin/amd-64/bf2` binary is not stripped, so it holds the
variable's name, its address and its size. When a variable lies in a data
section (`.data`), its initial value can simply be read — with no Ghidra and
no decompilation.

    tools/elf_symbol.py g_localPredictionLerpTime --type f32

Variables in `.bss` have no initial value in the file: there are zeroes there,
and the script says so plainly, so a zero is not taken for a measured number.
"""
import argparse
import struct
import sys
from pathlib import Path

DEFAULT_BINARY = Path("Game Files/OtherFiles/linuxded/bin/amd-64/bf2")


def read_sections(data):
    (e_shoff,) = struct.unpack_from("<Q", data, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, stype, flags, addr, offset, size, link, info, align, entsize = struct.unpack_from(
            "<IIQQQQIIQQ", data, off
        )
        sections.append(
            dict(name=name, type=stype, addr=addr, offset=offset, size=size, link=link,
                 entsize=entsize)
        )
    strtab = sections[e_shstrndx]
    for section in sections:
        end = data.index(b"\0", strtab["offset"] + section["name"])
        section["label"] = data[strtab["offset"] + section["name"] : end].decode()
    return sections


def symbols(data, sections):
    for section in sections:
        if section["type"] not in (2, 11):  # SYMTAB, DYNSYM
            continue
        names = sections[section["link"]]
        count = section["size"] // section["entsize"]
        for i in range(count):
            off = section["offset"] + i * section["entsize"]
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from(
                "<IBBHQQ", data, off
            )
            if st_name == 0:
                continue
            start = names["offset"] + st_name
            end = data.index(b"\0", start)
            yield data[start:end].decode(errors="replace"), st_value, st_size, st_shndx


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pattern")
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--type", default="f32", choices=["f32", "f64", "u32", "i32", "u8"])
    args = parser.parse_args()

    data = args.binary.read_bytes()
    sections = read_sections(data)

    fmt = {"f32": "<f", "f64": "<d", "u32": "<I", "i32": "<i", "u8": "<B"}[args.type]

    found = False
    for name, value, size, shndx in symbols(data, sections):
        if args.pattern not in name:
            continue
        found = True
        section = sections[shndx] if shndx < len(sections) else None
        label = section["label"] if section else f"#{shndx}"
        line = f"{name}\n  address {value:#x}, size {size}, section {label}"
        if section and section["type"] == 8:  # NOBITS = .bss
            print(line + "\n  in .bss: there is no value in the file (zero until it runs)")
            continue
        if section and size >= struct.calcsize(fmt):
            offset = section["offset"] + (value - section["addr"])
            (number,) = struct.unpack_from(fmt, data, offset)
            print(line + f"\n  value ({args.type}): {number}")
        else:
            print(line)
    if not found:
        print("no such symbol", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
