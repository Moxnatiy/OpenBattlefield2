#!/usr/bin/env python3
"""Значення глобальної змінної просто з ELF лінукс-сервера BF2.

Бінар `linuxded/bin/amd-64/bf2` не стрипнутий, тож у ньому є і ім'я
змінної, і її адреса, і розмір. Коли змінна лежить у секції з даними
(`.data`), її початкове значення можна просто прочитати — без Ghidra і
без декомпіляції.

    tools/elf_symbol.py g_localPredictionLerpTime --type f32

Змінні в `.bss` початкового значення у файлі не мають: там нулі, і
скрипт про це прямо каже, щоб нуль не сплутали з виміряним числом.
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
        line = f"{name}\n  адреса {value:#x}, розмір {size}, секція {label}"
        if section and section["type"] == 8:  # NOBITS = .bss
            print(line + "\n  у .bss: у файлі значення немає (нуль до запуску)")
            continue
        if section and size >= struct.calcsize(fmt):
            offset = section["offset"] + (value - section["addr"])
            (number,) = struct.unpack_from(fmt, data, offset)
            print(line + f"\n  значення ({args.type}): {number}")
        else:
            print(line)
    if not found:
        print("немає такого символу", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
