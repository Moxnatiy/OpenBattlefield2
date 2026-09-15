#!/usr/bin/env python3
"""The order object templates are created in, straight out of the game's archives.

    tools/template_order.py                      # every template, numbered
    tools/template_order.py --grep lav25         # only matching names
    tools/template_order.py --check 5007:jep_vodnik 5035:RUTNK_T90

What for: `CreateObjectEvent` carries a template **number**, and the number is
the order of creation (`ObjectTemplateManager::createTemplate` does
`setId(counter++)`, bf2_events.h). A hypothesis to test against the pairs measured
on a live server: the numbers follow the archives' own file order — the zip's
central directory, not a sorted list — `.con` by `.con`, each counting its
`ObjectTemplate.create` lines and those of the files it `include`s or `run`s.

`--check` prints, for pairs of (number, name) measured on the server, the
difference in numbers against the difference in our order: where they agree the
order is right between those two.
"""
import argparse
import posixpath
import re
import sys
import zipfile
from pathlib import Path

MOD = Path("Game Files/mods/bf2")
# ServerArchives.con: Objects_server.zip -> Objects, Menu_server.zip -> Menu,
# Common_server.zip -> Common, Booster_server.zip -> Objects.
ARCHIVES = [("Objects_server.zip", "objects"), ("Menu_server.zip", "menu"),
            ("Common_server.zip", "common"), ("Booster_server.zip", "objects")]

CREATE = re.compile(r"^\s*ObjectTemplate\.create\s+(\S+)\s+(\S+)", re.IGNORECASE)
INCLUDE = re.compile(r"^\s*(include|run)\s+(\S+)", re.IGNORECASE)


def load(walk):
    files = {}   # mount path (lower) -> bytes loader
    order = []   # .con in the chosen order
    for archive, mount in ARCHIVES:
        z = zipfile.ZipFile(MOD / archive)
        cons = []
        for info in z.infolist():
            path = f"{mount}/{info.filename}".lower()
            files[path] = (z, info.filename)
            if path.endswith(".con"):
                cons.append(path)
        if walk == "zip":
            order.extend(cons)
        else:
            order.extend(tree_order(cons, files_first=(walk == "lower-files-first")))
    return files, order


def tree_order(paths, files_first):
    # A directory walk that sorts each directory's entries by their lower-case
    # names, where '_' (0x5f) comes before the letters — the zip's own order puts
    # it after them.
    tree = {}
    for path in paths:
        node = tree
        parts = path.split("/")
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = None
    out = []

    def walk(node, prefix):
        names = sorted(node, key=lambda n: n.lower())
        if files_first:
            names = [n for n in names if node[n] is None] + [n for n in names if node[n] is not None]
        for name in names:
            if node[name] is None:
                out.append(prefix + name)
            else:
                walk(node[name], prefix + name + "/")

    walk(tree, "")
    return out


def templates_of(path, files, seen):
    if path in seen or path not in files:
        return []
    seen.add(path)
    z, name = files[path]
    out = []
    for line in z.read(name).decode("latin-1").splitlines():
        m = CREATE.match(line)
        if m:
            out.append((m.group(2), m.group(1), path))
            continue
        m = INCLUDE.match(line)
        if m:
            target = posixpath.normpath(posixpath.join(posixpath.dirname(path), m.group(2).replace("\\", "/"))).lower()
            out.extend(templates_of(target, files, seen))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--grep")
    parser.add_argument("--check", nargs="*")
    parser.add_argument("--walk", default="zip", choices=["zip", "lower", "lower-files-first"])
    args = parser.parse_args()

    files, order = load(args.walk)
    seen = set()
    numbered = []
    created = set()
    for path in order:
        for entry in templates_of(path, files, seen):
            # A name created again does not take a new number: the 25 repeats
            # before the soldiers were exactly the offset against the server.
            if entry[0].lower() in created:
                continue
            created.add(entry[0].lower())
            numbered.append(entry)
    index = {}
    for i, (name, kind, path) in enumerate(numbered):
        index.setdefault(name.lower(), i)

    if args.check:
        pairs = []
        for item in args.check:
            number, name = item.split(":", 1)
            pairs.append((int(number), name, index.get(name.lower())))
        pairs.sort()
        for (n1, a, i1), (n2, b, i2) in zip(pairs, pairs[1:]):
            ours = (i2 - i1) if i1 is not None and i2 is not None else None
            mark = "==" if ours == n2 - n1 else "!="
            print(f"{a} {n1} -> {b} {n2}: server {n2 - n1}, ours {ours} {mark}")
        for n, name, i in pairs:
            print(f"  {name}: server {n}, ours {i}, offset {None if i is None else n - i}")
        return 0

    for i, (name, kind, path) in enumerate(numbered):
        if args.grep and args.grep.lower() not in name.lower():
            continue
        print(f"{i:6d}  {kind:24s} {name:40s} {path}")
    print(f"templates: {len(numbered)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
