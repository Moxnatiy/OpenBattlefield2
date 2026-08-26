#!/usr/bin/env python3
"""Робить із бінаря сервера таблицю ігрових подій для нашого коду.

    tools/linuxded/gen_events.py > src/net/src/bf2_events.inc

Для кожної події бере номер типу з `getType()` і розкладку полів з
`deSerialize`. Події, де поля лежать за умовою, позначає окремо: для них
таблиця розмірів бреше, тож розбір таких пишемо руками, а генератор про
це чесно каже замість того, щоб видати неправильний код.

Сенс таблиці — вміти пропустити будь-яку подію рівно на стільки бітів,
скільки вона займає. Без цього не дійти до кінця пакета, а там на нас
чекає потік привидів.
"""
import re
import subprocess
import sys

import bitfields as b


def event_types(table):
    """Номер типу кожної події з її `getType()`."""
    wanted = {}
    for name, (address, size) in table.items():
        head = name.split("(")[0]
        if head.endswith("::getType"):
            wanted[head] = (address, size or 0x20)

    numbers = {}
    for head, (address, size) in wanted.items():
        cls = head.split("::")[-2]
        text = subprocess.run(
            ["objdump", "-d", "--no-show-raw-insn",
             "--start-address=0x%x" % address,
             "--stop-address=0x%x" % (address + max(size, 0x10)), b.BINARY],
            capture_output=True, text=True).stdout
        m = re.search(r"movl?\s+\$0x([0-9a-f]+),\s*%eax", text)
        if m:
            value = int(m.group(1), 16)
        elif re.search(r"xorl?\s+%eax,\s*%eax", text):
            value = 0
        else:
            continue
        if value < 128:
            numbers.setdefault(cls, value)
    return numbers


def layout(table, cls):
    """(розміри полів, чи є умовні гілки) для `<cls>::deSerialize`."""
    hit = b.find(table, "%s::deSerialize" % cls)
    if not hit:
        return None, False
    address, size, _ = hit
    lines = b.disassemble(address, size)
    steps = b.scan(lines)
    fields = [s for s in steps if s[0] in ("поле", "рядок")]
    if not fields:
        return [], False
    first, last = fields[0][1], fields[-1][1]
    branchy = any(s[0] == "перехід" and first <= s[1] <= last for s in steps)
    widths = [v if k == "поле" else None for k, _, v in fields]
    return widths, branchy


def main():
    table = b.symbols()
    numbers = event_types(table)

    rows = []
    for cls, number in sorted(numbers.items(), key=lambda kv: kv[1]):
        if not cls.endswith("Event"):
            continue
        widths, branchy = layout(table, cls)
        if widths is None:
            continue
        rows.append((number, cls, widths, branchy))

    print("// Згенеровано tools/linuxded/gen_events.py з bin/amd-64/bf2.")
    print("// Не редагувати руками: перезняти можна тією ж командою.")
    print("//")
    print("// BF2_EVENT(номер, назва, бітів...) — поля йдуть поспіль, таку")
    print("// подію можна пропустити за таблицею.")
    print("// BF2_EVENT_BRANCHY(номер, назва) — частина полів за умовою,")
    print("// розмір залежить від вмісту, тож розбір написано руками.")
    print("// BF2_EVENT_EMPTY(номер, назва) — подія без полів, самий тип.")
    print()
    for number, cls, widths, branchy in rows:
        if branchy or any(w is None for w in widths):
            print("BF2_EVENT_BRANCHY(%d, \"%s\")" % (number, cls))
        elif not widths:
            print("BF2_EVENT_EMPTY(%d, \"%s\")" % (number, cls))
        else:
            print("BF2_EVENT(%d, \"%s\"%s)" % (
                number, cls, "".join(", %d" % w for w in widths)))
    print()
    print("// подій разом: %d, з них за умовою: %d"
          % (len(rows), sum(1 for r in rows if r[3])), file=sys.stderr)


if __name__ == "__main__":
    main()
