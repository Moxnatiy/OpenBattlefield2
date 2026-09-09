#!/usr/bin/env python3
"""Which console commands hang on the HUD's buttons.

    tools/hud_commands.py            # by object
    tools/hud_commands.py --full     # every command separately
    tools/hud_commands.py --screen SpawnInterface

A button in the HUD does nothing by itself: it runs **an ordinary console
command** given by `setButtonNodeConCmd`. So for the interface to come alive
no logic has to be invented — exactly this list has to be implemented.

Three commands set the action:

    setButtonNodeConCmd      381   the left button
    setButtonNodeAltConCmd    30   the right one
    setListNodeConCmd         15   a list row (a number comes before the command)
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
            # In a list the column number stands before the command.
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
        print("\n%d distinct, %d calls" % (len(counts), sum(counts.values())))
        return 0

    byObject = collections.defaultdict(collections.Counter)
    for _, text in found:
        head = text.split(" ")[0]
        obj, method = head.split(".", 1)
        byObject[obj][method] += 1
    total = sum(sum(c.values()) for c in byObject.values())
    print("%d objects, %d calls" % (len(byObject), total))
    for obj, methods in sorted(byObject.items(), key=lambda kv: -sum(kv[1].values())):
        print("%-24s %4d  %s" % (obj, sum(methods.values()),
                                 ", ".join("%s(%d)" % kv for kv in methods.most_common())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
