#!/usr/bin/env python3
"""Що ще не вміє наш HUD — за самими даними гри.

    tools/hud_audit.py                    # чого бракує, найчастіше згори
    tools/hud_audit.py setBarNodeSnapDir  # як цю команду викликають насправді

Сенс: не гадати про розкладку аргументів, а подивитися, як команду
викликають у 1145 файлах гри. Кількість аргументів, їхні зразки й файли —
цього майже завжди досить, щоб написати обробник, а бінар лишити на ті
випадки, де з даних не видно сенсу.

Архіви читаються на місці, нічого не розпаковується.
"""
import argparse
import collections
import os
import re
import sys
import zipfile

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "Game Files", "mods", "bf2")
ARCHIVES = ("Menu_client.zip", "Common_client.zip")
HUD_OBJECTS = ("hudbuilder", "hudmanager")
IMPL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                    "src", "hud", "src", "hud.cpp")


def implemented():
    """Команди, які вже має наш Builder: він порівнює імена в нижньому регістрі."""
    text = open(IMPL, encoding="utf-8", errors="replace").read()
    return set(re.findall(r'"([a-z][a-z0-9]+)"', text))


def read_all():
    """Усі .con з архівів гри, ключ — шлях у нижньому регістрі."""
    out = {}
    for name in ARCHIVES:
        path = os.path.join(MOD, name)
        if not os.path.exists(path):
            continue
        with zipfile.ZipFile(path) as archive:
            # Архів монтується під своїм іменем: Menu_client.zip -> Menu/.
            # Саме такі шляхи стоять у `run`, тож ключі робимо такі самі.
            prefix = name.split("_")[0].lower() + "/"
            for entry in archive.namelist():
                if entry.lower().endswith(".con"):
                    key = prefix + entry.lower().replace("\\", "/")
                    out[key] = archive.read(entry).decode("latin-1")
    return out


def follow(files, root):
    """Файли, які рушій справді прочитає, починаючи з кореневого.

    `run` тягне за собою наступний файл — саме так дерево HUD і
    складається. Ходимо тим самим шляхом, щоб бачити рівно те, що
    бачить наш Builder, а не всі 1145 файлів гри.
    """
    order, queue = [], [root.lower().replace("\\", "/")]
    while queue:
        name = queue.pop(0)
        if name in order or name not in files:
            continue
        order.append(name)
        for line in files[name].splitlines():
            line = line.strip()
            if not line.lower().startswith("run "):
                continue
            nxt = line.split(None, 1)[1].split()[0].strip('"')
            nxt = nxt.lower().replace("\\", "/")
            # Шлях у `run` рахується від теки самого файлу; повний шлях
            # трапляється теж, тож пробуємо обидва.
            here = name.rsplit("/", 1)[0]
            queue.append(here + "/" + nxt if here + "/" + nxt in files else nxt)
    return order


def calls(only=None):
    """Усі виклики hudBuilder.*/hudManager.* з архівів гри."""
    out = []
    files = read_all()
    for entry in (only if only is not None else sorted(files)):
            text = files[entry]
            if True:
                for line in text.splitlines():
                    line = line.strip()
                    if not line or line.startswith("rem") or "." not in line:
                        continue
                    head = line.split(None, 1)[0]
                    obj, _, command = head.partition(".")
                    if obj.lower() not in HUD_OBJECTS or not command:
                        continue
                    args = line.split()[1:]
                    out.append((command, args, entry, line))
    return out


def emit(path, everything, counts, known):
    """Таблиця відомих команд HUD для C++.

    Кожна команда, яку гра справді викликає, має бути або обробленою, або
    хоча б записаною — інакше вона губиться мовчки. Тут ми складаємо
    список тих, що записуються як є, разом із кількістю аргументів: рушій
    звіряє її і скаржиться, якщо дані розійшлися з очікуванням.
    """
    rows = []
    for name, count in sorted(counts.items()):
        # create* робить вузол і має свій розбір: тип вузла й прямокутник
        # знати треба, а не просто записати.
        if name.lower() in known or name.lower().startswith("create"):
            continue
        shapes = sorted({len(c[1]) for c in everything if c[0] == name})
        rows.append((name, count, shapes))
    with open(path, "w", encoding="utf-8") as out:
        out.write("// Створено tools/hud_audit.py --emit. Руками не правити.\n")
        out.write("//\n// Команди HUD, які гра викликає, а ми поки лише записуємо:\n")
        out.write("// значення лягають у Node::extra, тож нічого не губиться, і\n")
        out.write("// видно, чого бракує саме рендеру, а не розбору.\n")
        for name, count, shapes in rows:
            out.write('HUD_RECORDED("%s", %d)  // викликів %d\n'
                      % (name.lower(), shapes[0], count))
    print("записано %d команд у %s" % (len(rows), path))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", help="показати виклики саме цієї команди")
    parser.add_argument("--all", action="store_true", help="і ті, що вже вміємо")
    parser.add_argument("--samples", type=int, default=6)
    parser.add_argument("--root", help="рахувати лише те, що тягне цей файл через run")
    parser.add_argument("--emit", help="згенерувати таблицю команд для C++ у цей файл")
    args = parser.parse_args()

    picked_files = None
    if args.root:
        files = read_all()
        picked_files = follow(files, args.root)
        print("файлів у дереві: %d (від %s)" % (len(picked_files), args.root))
    everything = calls(picked_files)
    if not everything:
        print("архівів не знайдено: %s" % MOD, file=sys.stderr)
        return 1
    known = implemented()

    if args.command:
        want = args.command.lower()
        picked = [c for c in everything if c[0].lower() == want]
        if not picked:
            print("такої команди в даних немає")
            return 1
        shapes = collections.Counter(len(c[1]) for c in picked)
        print("%s: викликів %d, у %d файлах%s" % (
            picked[0][0], len(picked), len({c[2] for c in picked}),
            "" if want not in known else " (вже вміємо)"))
        print("аргументів: %s" % dict(sorted(shapes.items())))
        seen = set()
        for _, _, entry, line in picked:
            if line in seen:
                continue
            seen.add(line)
            print("  %-52s %s" % (line, entry))
            if len(seen) >= args.samples:
                break
        return 0

    counts = collections.Counter(c[0] for c in everything)
    if args.emit:
        emit(args.emit, everything, counts, known)
        return 0

    shown = 0
    print("%-34s %6s  %s" % ("команда", "разів", "аргументів"))
    for name, count in counts.most_common():
        if not args.all and name.lower() in known:
            continue
        shapes = sorted({len(c[1]) for c in everything if c[0] == name})
        mark = " *" if name.lower() in known else ""
        print("%-34s %6d  %s%s" % (name, count, shapes, mark))
        shown += 1
    print("\nбракує %d команд із %d; викликів усього %d" %
          (shown, len(counts), len(everything)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
