#!/usr/bin/env python3
"""Консольні об'єкти BF2 та їхні статичні адреси.

    tools/con_objects.py                # усі
    tools/con_objects.py --grep Hud     # лише потрібні
    tools/con_objects.py --emit         # docs/functions/con-objects.md

Навіщо: щоб дивитися в пам'ять живої гри, треба знати, *куди* дивитися.
Кожен об'єкт мови `.con` (`HudBuilder.createTextNode`, `Scoreboard.…`) —
це статичний примірник у образі, який при старті реєструє своє ім'я.
Реєстрація має сталий вигляд, і саме за ним ми його й ловимо:

    push  $0            прапорці
    push  $2
    push  $0x92f2fc     ім'я — "HudBuilder"
    mov   $0xa18898,%ecx    <- сам об'єкт
    call  registerObject

Отже ім'я стоїть поруч з адресою, і одним проходом по `.text` виходить
таблиця «ім'я -> адреса». Образ вантажиться за 0x400000 без релокацій,
тож адреси однакові й у файлі, і в пам'яті.
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe_xref  # noqa: E402

# push imm8, push imm8, push <ім'я>, mov <об'єкт>,%ecx, call
PATTERN = re.compile(rb"\x6a[\x00-\x03]\x6a[\x00-\x03]\x68(....)\xb9(....)\xe8",
                     re.S)


def objects(exe="BF2.exe"):
    data = open(os.path.join(exe_xref.GAME, exe), "rb").read()
    sections = exe_xref.sections(data)

    def text(address):
        for _, start, size, offset in sections:
            if start <= address < start + size:
                at = address - start + offset
                end = data.index(b"\x00", at)
                raw = data[at:end]
                if 0 < len(raw) < 64 and all(32 <= c < 127 for c in raw):
                    return raw.decode("latin-1")
        return None

    _, start, size, offset = sections[0]
    found = []
    for match in PATTERN.finditer(data[offset:offset + size]):
        name = text(struct.unpack("<I", match.group(1))[0])
        if name and name[0].isalpha():
            found.append((name, struct.unpack("<I", match.group(2))[0]))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default="BF2.exe")
    parser.add_argument("--grep", help="лише імена з цим усередині")
    parser.add_argument("--emit", action="store_true", help="записати конспект")
    args = parser.parse_args()

    found = objects(args.exe)
    if args.grep:
        found = [(n, a) for n, a in found if args.grep.lower() in n.lower()]
    for name, address in found:
        print("%-28s %#x" % (name, address))
    print("\nусього %d" % len(found), file=sys.stderr)

    if args.emit:
        here = os.path.dirname(os.path.abspath(__file__))
        path = os.path.join(here, "..", "docs", "functions", "con-objects.md")
        with open(path, "w") as out:
            out.write("# Консольні об'єкти BF2 та їхні адреси\n\n")
            out.write("Знято `tools/con_objects.py` з `%s`. Образ стоїть за\n"
                      "0x400000 без релокацій, тож ці адреси чинні й у живій\n"
                      "грі — за ними можна дивитися в пам'ять.\n\n" % args.exe)
            out.write("| об'єкт | адреса |\n|---|---|\n")
            for name, address in found:
                out.write("| `%s` | `%#x` |\n" % (name, address))
        print("записано %s" % path, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
