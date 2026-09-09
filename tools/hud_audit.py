#!/usr/bin/env python3
"""What our HUD still cannot do — from the game's own data.

    tools/hud_audit.py                    # what is missing, the commonest first
    tools/hud_audit.py setBarNodeSnapDir  # how this command is really called

The point: not to guess at the argument layout but to look at how a command is
called across the game's 1145 files. The argument count, the samples and the
files are almost always enough to write a handler, leaving the binary for the
cases where the data does not show the meaning.

The archives are read in place, nothing is unpacked.
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


def strip_comments(text):
    """C++ source with the comments cut out.

    Scanned by hand rather than by a regular expression: a `//` inside a
    string literal is not a comment, and a regular expression cannot tell
    the two apart.
    """
    out = []
    at, end = 0, len(text)
    while at < end:
        char = text[at]
        if char in "\"'":
            out.append(char)
            at += 1
            while at < end:
                if text[at] == "\\":
                    out.append(text[at:at + 2])
                    at += 2
                    continue
                out.append(text[at])
                at += 1
                if text[at - 1] == char:
                    break
            continue
        if text.startswith("//", at):
            newline = text.find("\n", at)
            at = end if newline < 0 else newline
            continue
        if text.startswith("/*", at):
            close = text.find("*/", at + 2)
            at = end if close < 0 else close + 2
            continue
        out.append(char)
        at += 1
    return "".join(out)


def implemented():
    """The commands our Builder already has: it compares names in lower case.

    The comments go first. We look for quoted words, and a word in quotes
    inside a comment would be taken for a command — it would then silently
    drop out of the list of what is missing. There are two such words in
    `hud.cpp` already: "ran" and the game's own typo "tranform".
    """
    text = open(IMPL, encoding="utf-8", errors="replace").read()
    return set(re.findall(r'"([a-z][a-z0-9]+)"', strip_comments(text)))


def read_all():
    """Every .con from the game's archives, keyed by the lower-case path."""
    out = {}
    for name in ARCHIVES:
        path = os.path.join(MOD, name)
        if not os.path.exists(path):
            continue
        with zipfile.ZipFile(path) as archive:
            # An archive is mounted under its own name: Menu_client.zip -> Menu/.
            # Those are exactly the paths in `run`, so we make the keys the same.
            prefix = name.split("_")[0].lower() + "/"
            for entry in archive.namelist():
                if entry.lower().endswith(".con"):
                    key = prefix + entry.lower().replace("\\", "/")
                    out[key] = archive.read(entry).decode("latin-1")
    return out


def follow(files, root):
    """The files the engine will really read, starting from the root one.

    `run` pulls the next file in after it — that is exactly how the HUD tree is
    put together. We walk the same way, so as to see precisely what our Builder
    sees rather than all 1145 files of the game.
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
            # The path in `run` is counted from the file's own directory; a full
            # path occurs too, so we try both.
            here = name.rsplit("/", 1)[0]
            queue.append(here + "/" + nxt if here + "/" + nxt in files else nxt)
    return order


def calls(only=None):
    """Every hudBuilder.*/hudManager.* call from the game's archives."""
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
    """The table of known HUD commands for C++.

    Every command the game really calls must be either handled or at least
    recorded — otherwise it is lost silently. Here we assemble the list of the
    ones recorded as they are, together with the argument count: the engine
    checks it and complains when the data differs from what was expected.
    """
    rows = []
    for name, count in sorted(counts.items()):
        # create* makes a node and has a parse of its own: the node type and the
        # rectangle have to be known, not merely recorded.
        if name.lower() in known or name.lower().startswith("create"):
            continue
        shapes = sorted({len(c[1]) for c in everything if c[0] == name})
        rows.append((name, count, shapes))
    with open(path, "w", encoding="utf-8") as out:
        out.write("// Generated by tools/hud_audit.py --emit. Do not edit by hand.\n")
        out.write("//\n// HUD commands the game calls that we so far only record:\n")
        out.write("// the values land in Node::extra, so nothing is lost, and it is\n")
        out.write("// visible what the renderer is missing rather than the parser.\n")
        for name, count, shapes in rows:
            out.write('HUD_RECORDED("%s", %d)  // calls %d\n'
                      % (name.lower(), shapes[0], count))
    print("written: %d commands into %s" % (len(rows), path))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", help="show the calls of this command")
    parser.add_argument("--all", action="store_true", help="the ones we can do too")
    parser.add_argument("--samples", type=int, default=6)
    parser.add_argument("--root", help="count only what this file pulls in through run")
    parser.add_argument("--emit", help="generate the C++ command table into this file")
    args = parser.parse_args()

    picked_files = None
    if args.root:
        files = read_all()
        picked_files = follow(files, args.root)
        print("files in the tree: %d (from %s)" % (len(picked_files), args.root))
    everything = calls(picked_files)
    if not everything:
        print("no archives found: %s" % MOD, file=sys.stderr)
        return 1
    known = implemented()

    if args.command:
        want = args.command.lower()
        picked = [c for c in everything if c[0].lower() == want]
        if not picked:
            print("there is no such command in the data")
            return 1
        shapes = collections.Counter(len(c[1]) for c in picked)
        print("%s: %d calls, in %d files%s" % (
            picked[0][0], len(picked), len({c[2] for c in picked}),
            "" if want not in known else " (we can do it already)"))
        print("arguments: %s" % dict(sorted(shapes.items())))
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
    print("%-34s %6s  %s" % ("command", "times", "arguments"))
    for name, count in counts.most_common():
        if not args.all and name.lower() in known:
            continue
        shapes = sorted({len(c[1]) for c in everything if c[0] == name})
        mark = " *" if name.lower() in known else ""
        print("%-34s %6d  %s%s" % (name, count, shapes, mark))
        shown += 1
    print("\n%d commands missing out of %d; %d calls in total" %
          (shown, len(counts), len(everything)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
