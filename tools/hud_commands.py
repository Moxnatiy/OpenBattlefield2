#!/usr/bin/env python3
"""Які консольні команди висять на кнопках HUD.

    tools/hud_commands.py            # за об'єктами
    tools/hud_commands.py --full     # кожна команда окремо
    tools/hud_commands.py --screen SpawnInterface

Кнопка в HUD нічого не робить сама: вона виконує **звичайну консольну
команду**, задану `setButtonNodeConCmd`. Тобто щоб інтерфейс ожив, не
треба вигадувати логіку — треба реалізувати рівно цей перелік.

Три команди задають дію:

    setButtonNodeConCmd      381   ліва кнопка
    setButtonNodeAltConCmd    30   права
    setListNodeConCmd         15   рядок списку (перед командою йде номер)
"""
import argparse
import collections
import os
import re
import sys
import zipfile

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Game Files", "mods", "bf2")
CONCMD = re.compile(r"hudBuilder\.set\w*ConCmd\s+(.*)", re.I)


def commands(screen=None):
    archive = zipfile.ZipFile(os.path.join(MOD, "Menu_client.zip"))
    out = []
    for name in archive.namelist():
        if "/HudSetup/" not in name:
            continue
        if screen and screen.lower() not in name.lower():
            continue
        for line in archive.read(name).decode("latin-1", "replace").splitlines():
            found = CONCMD.match(" ".join(line.split()))
            if not found:
                continue
            rest = found.group(1)
            quoted = re.search(r'"([^"]*)"', rest)
            text = (quoted.group(1) if quoted else rest).strip()
            # У списку перед командою стоїть номер стовпця.
            text = re.sub(r"^\d+\s+", "", text)
            if "." not in text.split(" ")[0]:
                continue
            out.append((name.split("/")[-1], text))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--screen")
    args = parser.parse_args()

    found = commands(args.screen)
    if args.full:
        counts = collections.Counter(text for _, text in found)
        for text, count in counts.most_common():
            print("%4d  %s" % (count, text))
        print("\nрізних %d, викликів %d" % (len(counts), sum(counts.values())))
        return 0

    byObject = collections.defaultdict(collections.Counter)
    for _, text in found:
        head = text.split(" ")[0]
        obj, method = head.split(".", 1)
        byObject[obj][method] += 1
    total = sum(sum(c.values()) for c in byObject.values())
    print("об'єктів %d, викликів %d" % (len(byObject), total))
    for obj, methods in sorted(byObject.items(), key=lambda kv: -sum(kv[1].values())):
        print("%-24s %4d  %s" % (obj, sum(methods.values()),
                                 ", ".join("%s(%d)" % kv for kv in methods.most_common())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
