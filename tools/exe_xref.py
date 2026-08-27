#!/usr/bin/env python3
"""Рядок у бінарі -> хто на нього посилається -> код навколо.

    tools/exe_xref.py setBarNodeSnapDir
    tools/exe_xref.py --exe BF2.exe "Failed to receive" --window 60

Це заміна ручному ходінню по Ghidra для найчастішого питання: «де
обробляється ось ця команда». Працює локально через objdump і розбирає
лише вікно навколо посилання, тож відповідь приходить за пів секунди, а
не за хвилини.

Розбір PE тут свій і навмисно куций: нам треба тільки перекласти зсув у
файлі на адресу в пам'яті й назад.
"""
import argparse
import os
import re
import struct
import subprocess
import sys

GAME = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Game Files")


def sections(data):
    """(ім'я, адреса в пам'яті, розмір, зсув у файлі) для кожної секції."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]  # ImageBase
    table = pe + 24 + optional
    out = []
    for i in range(count):
        entry = table + i * 40
        name = data[entry:entry + 8].rstrip(b"\0").decode("latin-1")
        virtual_size, address, raw_size, raw = struct.unpack_from("<IIII", data, entry + 8)
        out.append((name, base + address, max(virtual_size, raw_size), raw))
    return out


def to_address(parts, offset):
    for _, address, size, raw in parts:
        if raw <= offset < raw + size:
            return address + (offset - raw)
    return None


def find_string(data, text):
    """Усі місця, де лежить рівно цей рядок (із нулем у кінці)."""
    needle = text.encode("latin-1") + b"\0"
    out, at = [], data.find(needle)
    while at >= 0:
        before = data[at - 1] if at else 0
        if before == 0 or not (32 <= before < 127):
            out.append(at)
        at = data.find(needle, at + 1)
    return out


def find_refs(data, parts, address):
    """Де в коді трапляється ця адреса чотирма байтами."""
    needle = struct.pack("<I", address)
    out = []
    for name, start, size, raw in parts:
        if name != ".text":
            continue
        at = data.find(needle, raw, raw + size)
        while at >= 0:
            out.append(start + (at - raw))
            at = data.find(needle, at + 1, raw + size)
    return out


def disassemble(path, start, stop):
    text = subprocess.run(
        ["objdump", "-d", "--no-show-raw-insn",
         "--start-address=%#x" % start, "--stop-address=%#x" % stop, path],
        capture_output=True, text=True).stdout
    return [l for l in text.split("\n") if re.match(r"^\s+[0-9a-f]+:", l)]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("text", help="рядок, який шукаємо")
    parser.add_argument("--exe", default="BF2_r.exe")
    parser.add_argument("--window", type=int, default=20,
                        help="скільки команд показати навколо посилання")
    parser.add_argument("--near", type=int, default=0,
                        help="показати ще й сусідні рядки в таблиці (штук)")
    args = parser.parse_args()

    path = args.exe if os.path.exists(args.exe) else os.path.join(GAME, args.exe)
    if not os.path.exists(path):
        print("немає такого файлу: %s" % path, file=sys.stderr)
        return 1
    data = open(path, "rb").read()
    parts = sections(data)

    places = find_string(data, args.text)
    if not places:
        print("такого рядка в %s немає" % os.path.basename(path))
        return 1

    for offset in places:
        address = to_address(parts, offset)
        print("рядок %r: зсув %#x, адреса %#x" % (args.text, offset, address))
        if args.near:
            at = offset
            for _ in range(args.near):
                found = data.rfind(b"\0", 0, at - 1)
                if found < 0:
                    break
                at = found + 1
            shown = data[at:offset + len(args.text) + 1].split(b"\0")
            print("  поряд: %s" %
                  b" | ".join(x for x in shown if x)[-200:].decode("latin-1"))

        refs = find_refs(data, parts, address)
        if not refs:
            print("  на нього ніхто не посилається прямо")
            continue
        needle = hex(address)[2:]
        for ref in refs:
            print("  посилання @ %#x" % ref)
            lines = disassemble(path, ref - 5 - args.window * 6, ref + args.window * 6)
            mark = next((i for i, l in enumerate(lines) if needle in l), None)
            if mark is None:
                continue
            half = args.window // 2
            for line in lines[max(0, mark - half):mark + half]:
                print("%s%s" % ("  >" if needle in line else "   ", line))
    return 0


if __name__ == "__main__":
    sys.exit(main())
