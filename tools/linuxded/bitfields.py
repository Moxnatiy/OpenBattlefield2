#!/usr/bin/env python3
"""Витягує розкладку полів із коду 64-бітного Linux-сервера.

    tools/linuxded/bitfields.py CreatePlayerEvent::deSerialize [ще назви...]

Знаходить адресу функції, розбирає її і виписує всі виклики
`BitStream::readBits`/`writeBits` разом із кількістю бітів. У System V
AMD64 третій аргумент (кількість) їде в `%edx`, тож беремо останнє
присвоєння перед викликом — це дає точну послідовність полів.

Сервер лежить на домашній машині (x86): під емуляцією ptrace не працює,
а без нього немає ні gdb, ні цих замірів. Хост міняється через BF2_HOST.
"""
import os
import re
import subprocess
import sys

BINARY = "/server/bin/amd-64/bf2"


def gdb(host, *commands):
    inner = " ".join('-ex "%s"' % c for c in commands)
    remote = (
        "docker run --rm -v $HOME/bf2ded:/server bf2dbg "
        "gdb -batch %s %s 2>/dev/null" % (inner, BINARY)
    )
    return subprocess.run(["ssh", host, remote], capture_output=True, text=True).stdout


def address_of(host, name):
    listing = gdb(host, "info functions %s" % name)
    for line in listing.splitlines():
        m = re.match(r"^(0x[0-9a-f]+)\s+\S*%s\(" % re.escape(name), line)
        if m:
            return m.group(1)
    return None


def fields(text):
    """Послідовність (дія, кількість бітів) у порядку виклику."""
    out = []
    width = None
    for line in text.splitlines():
        m = re.search(r"mov\s+\$0x([0-9a-f]+),%edx", line)
        if m:
            width = int(m.group(1), 16)
            continue
        m = re.search(r"call.*?(readBits|writeBits|readString|writeString)", line)
        if not m:
            continue
        what = m.group(1)
        if what.endswith("String"):
            out.append((what, None))
        else:
            out.append((what, width))
        width = None
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit("вкажіть назви функцій, напр. CreatePlayerEvent::deSerialize")
    host = os.environ.get("BF2_HOST", "homeserver")
    names = sys.argv[1:]

    # Один сеанс gdb на всі назви: кожен запуск контейнера коштує секунд.
    listing = gdb(host, *["info functions %s" % n for n in names])
    addresses = {}
    for name in names:
        for line in listing.splitlines():
            m = re.match(r"^(0x[0-9a-f]+)\s+\S*%s\(" % re.escape(name), line)
            if m:
                addresses[name] = m.group(1)
                break

    missing = [n for n in names if n not in addresses]
    for name in missing:
        print("не знайшов %s" % name, file=sys.stderr)
    if not addresses:
        raise SystemExit(1)

    order = [n for n in names if n in addresses]
    dumps = gdb(host, *["disassemble %s" % addresses[n] for n in order])
    blocks = dumps.split("Dump of assembler code")[1:]

    for name, block in zip(order, blocks):
        found = fields(block)
        print("%s (%s)" % (name, addresses[name]))
        if not found:
            print("  викликів BitStream немає")
            continue
        total = 0
        for what, bits in found:
            if bits is None:
                print("  рядок")
                total = None
            else:
                print("  %s %d бітів" % ("читає" if what == "readBits" else "пише", bits))
                if total is not None:
                    total += bits
        if total is not None:
            print("  разом: %d бітів" % total)


if __name__ == "__main__":
    main()
