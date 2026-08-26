#!/usr/bin/env python3
"""Витягує розкладку полів із коду 64-бітного Linux-сервера BF2.

    tools/linuxded/bitfields.py CreatePlayerEvent::deSerialize [ще назви...]
    tools/linuxded/bitfields.py --blocks CreateObjectEvent::serialize

Розбирає функцію і виписує всі виклики `BitStream::readBits`/`writeBits`
разом із кількістю бітів: у System V AMD64 третій аргумент (кількість)
їде в `%edx`, тож розмір поля видно прямо в коді.

`--blocks` додатково показує будову функції — які поля лежать в одній
гілці, а які за умовою. Плаский список цього не показує, і саме там
ховаються поля, які на дроті є, а в списку читань їх наче немає.

Усе робиться локально: бінар лежить у теці гри, виконувати його не
треба, а `objdump` на macOS розбирає ELF x86-64 без проблем. Раніше цей
інструмент ходив по ssh у контейнер із gdb — тепер запит коштує 0.1 с
замість секунди з гаком.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BINARY = os.environ.get(
    "BF2_BINARY",
    os.path.join(ROOT, "Game Files/OtherFiles/linuxded-full/bin/amd-64/bf2"))
CACHE = os.path.join(HERE, "__pycache__", "symbols.txt")

CALL = re.compile(r"<(?P<name>[^>+]+)")
WIDTH = re.compile(r"mov\w*\s+\$0x([0-9a-f]+),\s*%edx")
INSTR = re.compile(r"^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S+)\s*(.*)$")
JUMP = re.compile(r"^j\w+$")
TARGET = re.compile(r"^0x([0-9a-f]+)")


def symbols():
    """Таблиця «демангловане ім'я -> адреса, розмір». Читається раз."""
    if os.path.exists(CACHE) and os.path.getmtime(CACHE) > os.path.getmtime(BINARY):
        text = open(CACHE).read()
    else:
        text = subprocess.run(["nm", "-C", "--print-size", BINARY],
                              capture_output=True, text=True).stdout
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        open(CACHE, "w").write(text)

    table = {}
    for line in text.splitlines():
        parts = line.split(" ", 3)
        if len(parts) < 3:
            continue
        address = parts[0].strip()
        if not re.fullmatch(r"[0-9a-f]+", address):
            continue
        if len(parts) == 4 and re.fullmatch(r"[0-9a-f]+", parts[1]):
            size, name = int(parts[1], 16), parts[3]
        else:
            size, name = 0, parts[-1]
        table.setdefault(name.strip(), (int(address, 16), size))
    return table


def find(table, wanted):
    """Шукає функцію за хвостом імені, напр. `CreatePlayerEvent::deSerialize`."""
    for name, (address, size) in table.items():
        head = name.split("(")[0]
        if head.endswith(wanted):
            return address, size, name
    return None


def disassemble(address, size):
    if size <= 0:
        size = 0x400
    out = subprocess.run(
        ["objdump", "-d", "--no-show-raw-insn",
         "--start-address=0x%x" % address, "--stop-address=0x%x" % (address + size),
         BINARY], capture_output=True, text=True).stdout
    lines = []
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$", line)
        if m:
            lines.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
    return lines


def scan(lines):
    """Поля й переходи в порядку появи."""
    steps = []
    width = None
    for at, op, args in lines:
        m = WIDTH.search("%s %s" % (op, args))
        if m:
            width = int(m.group(1), 16)
            continue
        if op.startswith("call"):
            m = CALL.search(args)
            name = m.group("name") if m else ""
            if "readBits" in name or "writeBits" in name:
                steps.append(("поле", at, width))
                width = None
            elif "String" in name:
                steps.append(("рядок", at, None))
        elif JUMP.match(op):
            m = TARGET.search(args)
            steps.append(("перехід" if op != "jmp" else "стрибок", at,
                          int(m.group(1), 16) if m else None))
    return steps


def blocks(lines):
    """Ділить функцію на блоки за цілями переходів."""
    starts = {lines[0][0]} if lines else set()
    for at, op, args in lines:
        if JUMP.match(op):
            m = TARGET.search(args)
            if m:
                starts.add(int(m.group(1), 16))
            nxt = next((a for a, _, _ in lines if a > at), None)
            if nxt:
                starts.add(nxt)
    groups, current = [], None
    for entry in lines:
        if entry[0] in starts:
            current = {"з": entry[0], "рядки": []}
            groups.append(current)
        if current is not None:
            current["рядки"].append(entry)
    return groups


def report(name, address, lines, show_blocks):
    print("%s (0x%x)" % (name, address))
    if not show_blocks:
        total = 0
        found = False
        for kind, _, value in scan(lines):
            if kind == "поле":
                found = True
                print("  %s бітів" % (value if value is not None else "?"))
                if value is not None and total is not None:
                    total += value
                else:
                    total = None
            elif kind == "рядок":
                found = True
                print("  рядок")
                total = None
        if not found:
            print("  викликів BitStream немає")
        elif total is not None:
            print("  разом: %d бітів" % total)
        return

    for group in blocks(lines):
        steps = scan(group["рядки"])
        fields = [s for s in steps if s[0] in ("поле", "рядок")]
        parts = [str(v) if k == "поле" and v is not None else
                 ("рядок" if k == "рядок" else "?") for k, _, v in fields]
        # Куди веде кінець блока: умовний перехід уперед означає, що
        # частину полів можна пропустити.
        tail = ""
        jumps = [s for s in steps if s[0] in ("перехід", "стрибок")]
        if jumps:
            kind, at, target = jumps[-1]
            if target is not None:
                tail = "  -> 0x%x%s" % (target, "" if kind == "стрибок" else " (за умовою)")
        if not parts and not tail:
            continue
        print("  блок 0x%-8x %-28s%s" % (group["з"], ", ".join(parts) or "—", tail))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("names", nargs="+")
    parser.add_argument("--blocks", action="store_true",
                        help="показати будову: що в одній гілці, а що за умовою")
    args = parser.parse_args()

    table = symbols()
    missing = []
    for wanted in args.names:
        hit = find(table, wanted)
        if not hit:
            missing.append(wanted)
            continue
        address, size, full = hit
        report(wanted, address, disassemble(address, size), args.blocks)
    for wanted in missing:
        print("не знайшов %s" % wanted, file=sys.stderr)
    return 1 if missing and len(missing) == len(args.names) else 0


if __name__ == "__main__":
    sys.exit(main())
