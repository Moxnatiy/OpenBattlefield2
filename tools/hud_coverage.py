#!/usr/bin/env python3
"""Покриття HUD: що з оригіналу ми вже малюємо, а що ні.

    # знімок кадру оригіналу (Ctrl+Shift+D у грі під mtld3d)
    tools/hud_coverage.py /tmp/bf2run.log ours.txt

    # наші прямокутники
    build/.../openbf2 --level Dalian_plant --hud-screen SpawnMenu \
        --hud-rects --frames 2 > ours.txt

Обидва боки дають прямокутники в тих самих екранних координатах 800x600,
тож звірка зводиться до пошуку пари. Оригінал рахує від центра екрана —
переводимо: `(400 + x, 300 + y)`.

Що це дає: не «схоже — не схоже», а число. Видно, скільки викликів
оригіналу ми відтворюємо, які лишилися без пари (їх ще треба зробити) і
які малюємо зайвими.

Дві застороги, щоб не читати цифри неправильно:

* оригінал подекуди зводить багато значків в один виклик, і тоді від
  них лишається спільний габарит — така партія в парі не знайдеться;
* підпис у нас — це прямокутник вузла, а в оригіналі — обведення самого
  рядка. Тому текст звіряється лише за положенням, з більшим допуском.
"""
import argparse
import re
import sys

DUMP = re.compile(r"\[dump\] draw (\d+):.*geom=\[(-?[\d.]+),(-?[\d.]+) ([\d.]+)x([\d.]+)")
OURS = re.compile(r"^RECT\s+(\S+)\s+(\S+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)")


def original(path, width, height):
    """Двовимірні виклики останнього знятого кадру."""
    frames = []
    current = []
    for line in open(path, "rb").read().decode("latin-1").splitlines():
        line = re.sub(r"\x1b\[[0-9;]*m", "", line)
        if "[dump] frame start" in line:
            if current:
                frames.append(current)
            current = []
        found = DUMP.search(line)
        if not found:
            continue
        x, y, w, h = (float(v) for v in found.groups()[1:])
        x += width / 2
        y += height / 2
        # Тривимірне відсіюємо: інтерфейс лежить у межах екрана.
        if w < 2 or h < 2 or w > width + 10 or h > height + 10:
            continue
        if x < -width or y < -height or x > width * 1.5 or y > height * 1.5:
            continue
        current.append((int(found.group(1)), x, y, w, h))
    if current:
        frames.append(current)
    return frames[-1] if frames else []


def ours(path, roots=None):
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        found = OURS.match(line)
        if not found:
            continue
        if roots and found.group(1) not in roots:
            continue
        out.append((found.group(2),) + tuple(float(v) for v in found.groups()[2:]))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", help="журнал гри зі знімком кадру")
    parser.add_argument("rects", help="вивід --hud-rects")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--tolerance", type=float, default=3.0)
    parser.add_argument("--root", action="append",
                        help="брати лише ці корені з нашого боку (можна кілька)")
    args = parser.parse_args()

    theirs = original(args.dump, args.width, args.height)
    mine = ours(args.rects, args.root)
    if not theirs:
        print("у журналі немає знятого кадру", file=sys.stderr)
        return 1

    used = [False] * len(mine)
    matched = []
    missing = []
    for seq, x, y, w, h in theirs:
        best = -1
        bestCost = args.tolerance
        for i, (name, mx, my, mw, mh) in enumerate(mine):
            if used[i]:
                continue
            cost = max(abs(mx - x), abs(my - y), abs(mw - w), abs(mh - h))
            if cost < bestCost:
                bestCost, best = cost, i
        if best >= 0:
            used[best] = True
            matched.append((seq, mine[best][0]))
        else:
            missing.append((seq, x, y, w, h))

    extra = [mine[i] for i in range(len(mine)) if not used[i]]
    total = len(theirs)
    print("оригінал: %d викликів, у нас: %d прямокутників" % (total, len(mine)))
    print("збіглося: %d (%.0f%%)" % (len(matched), 100.0 * len(matched) / max(total, 1)))
    print("\nне відтворено (%d):" % len(missing))
    for seq, x, y, w, h in missing[:40]:
        print("   виклик %-5d %7.1f %7.1f  %6.1f x %-6.1f" % (seq, x, y, w, h))
    if len(missing) > 40:
        print("   ... ще %d" % (len(missing) - 40))
    print("\nмалюємо зайве (%d):" % len(extra))
    for name, x, y, w, h in extra[:20]:
        print("   %-30s %7.1f %7.1f  %6.1f x %-6.1f" % (name, x, y, w, h))
    if len(extra) > 20:
        print("   ... ще %d" % (len(extra) - 20))
    return 0


if __name__ == "__main__":
    sys.exit(main())
