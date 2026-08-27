#!/usr/bin/env python3
"""Заглянути у файл `MemeFile 2.0` — граф шарів HUD.

    tools/meme_dump.py Ingame            # словник рядків і числа
    tools/meme_dump.py Ingame --walk     # спроба пройти записами
    tools/meme_dump.py --list            # які такі файли є в архівах

Це не .con, а двійковий граф сцени DICE (`dice::meme::*`). Саме тут
лежать кутові шари HUD — BottomLeftStatic, TopLayer і решта: у `.con`
вони згадані як групи, але ніде не позиціонуються, бо якір задає цей
файл, а не опис інтерфейсу.

Що вже певно:

* файл починається зі списку рядків, кожен із однобайтовою довжиною:
  спершу назви класів (`dice::meme::TransformNode`), потім імена шарів і
  змінних (`BottomRight/BottomRight_XPos`);
* далі йде тіло з одного кореневого `NameNode`, і запис має вигляд
  «двобайтовий номер рядка + чотирибайтовий розмір». На порожньому файлі
  (`BottomLeftStatic`, 45 байтів) це сходиться точно, на `Ingame` корінь
  укриває все тіло до байта;
* всередині трапляються 800.0 і 600.0 — той самий віртуальний екран, у
  якому розкладено весь HUD.

Чого ще не знаємо: як усередині запису розділені власні дані вузла і
його діти. Тому --walk доходить до першого TransformNode і чесно
показує, де саме збився, замість того щоб вигадувати розкладку.
"""
import argparse
import os
import struct
import sys
import zipfile

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "Game Files", "mods", "bf2")
ARCHIVES = ("Menu_client.zip", "Common_client.zip")
MAGIC = b"MemeFile"


def archives():
    for name in ARCHIVES:
        path = os.path.join(MOD, name)
        if os.path.exists(path):
            yield name, zipfile.ZipFile(path)


def find(entry):
    for name, archive in archives():
        for candidate in archive.namelist():
            if candidate.lower() == entry.lower() or candidate.lower().endswith(
                    "/" + entry.lower()):
                data = archive.read(candidate)
                if data.startswith(bytes([len(MAGIC) + 4]) + MAGIC):
                    return name, candidate, data
    return None


def strings(data):
    """Список рядків із початку файлу; повертає ще й де він скінчився."""
    out, at = [], 0
    while at < len(data):
        length = data[at]
        if not 1 <= length < 64 or at + 1 + length > len(data):
            break
        text = data[at + 1:at + 1 + length]
        if not all(32 <= c < 127 for c in text):
            break
        out.append(text.decode("latin-1"))
        at += 1 + length
    return out, at


def walk(names, body, at, end, depth, out):
    while at + 6 <= end:
        index, size = struct.unpack_from("<HI", body, at)
        if index >= len(names) or size < 4 or at + 2 + size > end:
            out.append("  " * depth + "збилися на %d: далі %s" % (
                at, " ".join("%02x" % c for c in body[at:min(end, at + 24)])))
            return
        out.append("  " * depth + "%-34s розмір %d" %
                   (names[index].replace("dice::meme::", ""), size))
        walk(names, body, at + 8, at + 2 + size, depth + 1, out)
        at += 2 + size


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("entry", nargs="?", help="ім'я файлу в архіві, напр. Ingame")
    parser.add_argument("--list", action="store_true", help="знайти всі такі файли")
    parser.add_argument("--walk", action="store_true", help="пройти записами")
    args = parser.parse_args()

    if args.list or not args.entry:
        for name, archive in archives():
            for candidate in archive.namelist():
                if archive.getinfo(candidate).file_size < 8:
                    continue
                with archive.open(candidate) as handle:
                    if MAGIC in handle.read(16):
                        print("%-22s %-40s %d б" % (name, candidate,
                                                    archive.getinfo(candidate).file_size))
        return 0

    found = find(args.entry)
    if not found:
        print("не знайдено: %s" % args.entry, file=sys.stderr)
        return 1
    archive, entry, data = found
    names, at = strings(data)
    print("%s / %s: %d байтів, рядків %d, тіло з %d" %
          (archive, entry, len(data), len(names), at))

    classes = [n for n in names if n.startswith("dice::meme::")]
    others = [n for n in names if not n.startswith("dice::meme::")]
    print("\nкласи (%d):" % len(classes))
    for name in classes:
        print("  %s" % name.replace("dice::meme::", ""))
    print("\nімена й змінні (%d):" % len(others))
    for name in others:
        print("  %s" % name)

    body = data[at:]
    numbers = []
    for i in range(0, len(body) - 4):
        value = struct.unpack_from("<f", body, i)[0]
        if 0.01 < abs(value) < 4096 and abs(value - round(value, 3)) < 1e-9:
            numbers.append((i, value))
    if numbers:
        print("\nчисла, схожі на координати:")
        for i, value in numbers[:40]:
            print("  +%-5d %g" % (i, value))

    if args.walk:
        print("\nзаписи:")
        out = []
        walk(names, body, 1, len(body), 0, out)
        print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
