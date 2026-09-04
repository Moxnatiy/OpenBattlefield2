#!/usr/bin/env python3
"""Розкладка стану об'єкта: який біт маски вмикає яке поле.

    tools/linuxded/statefields.py SoldierNetworkable::setNetUpdate

Мережеві класи читають стан однаково: спершу маска, потім поля, кожне під
своїм бітом. `bitfields.py` показує, **скільки** бітів читається і де, але
не каже, **під яким бітом** — а без цього розкладку не відтворити: поле,
пропущене через незнання біта, зсуває все, що йде далі.

Скрипт іде інструкціями за адресами й тримає дві речі:

* останню перевірку маски (`andl $imm, %reg` над змінною маски або
  `testb $imm, зсув(%rsp)`) — це і є біт;
* виклики `BitStream::readBits` із шириною в `%edx`, а також
  `readCompressedVector` (там ширини немає, зате видно точність).

**УВАГА: цьому виводу поки не можна вірити.** Скрипт іде інструкціями
за **адресами**, а не за потоком керування, і на цьому спотикається:
блоки в бінарі лежать не в тому порядку, в якому виконуються, тож
«остання побачена перевірка» часто з іншого блока. Перевірка на відомому
місці показує це прямо: позицію простого об'єкта (0x5d845b) скрипт
приписує біту 0x8, тоді як насправді її вмикає біт 0x2 (`testb $0x2` за
0x5d7a35). Трапляється й безглузда ширина на кшталт 17367456 — це `%edx`,
що лишився від сусідньої інструкції.

Щоб вивід став правильним, треба йти **графом блоків** (його вже будує
`bitfields.py --blocks`) і для кожного блока з читанням брати ту умову,
яка до нього веде. Доти скрипт годиться лише як чернетка: він показує,
де шукати, але не каже, під яким бітом що лежить.
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

INSTR = re.compile(r"^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(.*)$")
AND_IMM = re.compile(r"^and[lb]\s+\$(0x[0-9a-f]+|\d+),")
TEST_IMM = re.compile(r"^testb\s+\$(0x[0-9a-f]+|\d+),")
MOV_EDX = re.compile(r"^movl\s+\$(0x[0-9a-f]+|\d+), %edx")
CALL = re.compile(r"^callq?\s+0x[0-9a-f]+ <([^>]+)>")


def disassemble(pattern):
    text = subprocess.run(["objdump", "-d", "--no-show-raw-insn", "-C", BINARY],
                          capture_output=True, text=True).stdout
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if line.endswith(">:") and pattern in line:
            start = i
            break
    if start is None:
        return None, []
    body = []
    for line in lines[start + 1:]:
        if line.endswith(">:"):
            break
        body.append(line)
    return lines[start], body


def main():
    global BINARY
    parser = argparse.ArgumentParser()
    parser.add_argument("name")
    parser.add_argument("--binary", default=BINARY)
    args = parser.parse_args()
    BINARY = args.binary

    header, body = disassemble(args.name)
    if header is None:
        print("функції не знайдено:", args.name, file=sys.stderr)
        return 1
    print(header.strip())

    bit = None
    width = None
    order = 0
    for raw in body:
        m = INSTR.match(raw.replace("\t", "\t", 1)) or re.match(
            r"^\s+([0-9a-f]+):\s+(.*)$", raw)
        if not m:
            continue
        at, text = m.group(1), m.group(2).strip()

        hit = AND_IMM.match(text) or TEST_IMM.match(text)
        if hit:
            value = int(hit.group(1), 0)
            # Маска стану — це біти, а не довільні числа: беремо лише
            # степені двійки, решта (0x3, 0xf) це не «біт поля».
            if value and (value & (value - 1)) == 0:
                bit = value
            continue

        hit = MOV_EDX.match(text)
        if hit:
            width = int(hit.group(1), 0)
            continue

        hit = CALL.match(text)
        if not hit:
            continue
        callee = hit.group(1)
        if "readBits" in callee:
            order += 1
            print("  %2d  біт %-8s %3s бітів   (0x%s)"
                  % (order, hex(bit) if bit else "?", width if width else "?", at))
            width = None
        elif "readCompressedVector" in callee:
            order += 1
            print("  %2d  біт %-8s стиснений вектор (0x%s)"
                  % (order, hex(bit) if bit else "?", at))
        elif "readString" in callee or "readSmallString" in callee:
            order += 1
            print("  %2d  біт %-8s рядок (0x%s)" % (order, hex(bit) if bit else "?", at))
    return 0


if __name__ == "__main__":
    sys.exit(main())
