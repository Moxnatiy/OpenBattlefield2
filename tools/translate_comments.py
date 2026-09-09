#!/usr/bin/env python3
"""Move the Ukrainian comments in the code over to English, line by line.

The project was written with Ukrainian comments; for publication they have
to be English (`CLAUDE.md`, rule 10a). Rewriting whole files by hand is
both slow and dangerous — one stray keystroke inside a string literal and
the code changes meaning. So the work is split:

    tools/translate_comments.py --extract src tools > lines.tsv
    …fill in the third column…
    tools/translate_comments.py --apply lines.tsv

`--extract` lists every line that contains Cyrillic, with its file and
line number. `--apply` replaces exactly those lines, and refuses to touch
a file whose line no longer matches what was extracted — so a stale list
fails loudly instead of scrambling the source.

The TSV columns are: path, line number, original line, replacement. A
three-column row (path, line, replacement) is accepted too — handy for
large files, where repeating the original doubles the work; the target
line is then checked for Cyrillic instead of an exact match.

Tabs inside a line are escaped as `\\t`, newlines cannot occur.
"""
import argparse
import os
import re
import sys

CYRILLIC = re.compile("[Ѐ-ӿ]")
SKIP_DIRS = {".git", "build", "reference", "target", "__pycache__", ".venv"}
SUFFIXES = (".cpp", ".h", ".hpp", ".c", ".rs", ".py", ".sh", ".cmake", ".txt", ".yml", ".json")


def walk(roots):
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        for base, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            for name in sorted(files):
                if name.endswith(SUFFIXES) or name == "CMakeLists.txt":
                    yield os.path.join(base, name)


def escape(text):
    return text.replace("\\", "\\\\").replace("\t", "\\t")


def unescape(text):
    out = []
    i = 0
    while i < len(text):
        if text[i] == "\\" and i + 1 < len(text):
            nxt = text[i + 1]
            if nxt == "t":
                out.append("\t")
                i += 2
                continue
            if nxt == "\\":
                out.append("\\")
                i += 2
                continue
        out.append(text[i])
        i += 1
    return "".join(out)


def extract(roots, out):
    total = 0
    for path in walk(roots):
        try:
            lines = open(path, encoding="utf-8").read().split("\n")
        except (UnicodeDecodeError, OSError):
            continue
        for number, line in enumerate(lines, 1):
            if CYRILLIC.search(line):
                out.write("%s\t%d\t%s\t\n" % (path, number, escape(line)))
                total += 1
    print("lines: %d" % total, file=sys.stderr)
    return 0


def apply(table):
    edits = {}
    for row in open(table, encoding="utf-8"):
        row = row.rstrip("\n")
        if not row:
            continue
        parts = row.split("\t")
        if len(parts) == 3:
            # Short form: path, line, replacement. The line is checked for
            # Cyrillic instead of an exact match, so a stale number still
            # fails rather than overwriting translated code.
            path, number, replacement = parts[0], int(parts[1]), parts[2]
            if not replacement:
                continue
            edits.setdefault(path, []).append((number, None, unescape(replacement)))
            continue
        if len(parts) < 4:
            print("bad row: %s" % row[:60], file=sys.stderr)
            return 2
        path, number, original, replacement = parts[0], int(parts[1]), parts[2], parts[3]
        if not replacement:
            continue  # not translated yet
        edits.setdefault(path, []).append((number, unescape(original), unescape(replacement)))

    changed = 0
    for path, items in edits.items():
        lines = open(path, encoding="utf-8").read().split("\n")
        for number, original, replacement in items:
            if number > len(lines):
                print("%s:%d is past the end of the file" % (path, number), file=sys.stderr)
                return 2
            if original is None:
                if not CYRILLIC.search(lines[number - 1]):
                    print("%s:%d has no Cyrillic left, nothing written" % (path, number),
                          file=sys.stderr)
                    return 2
            elif lines[number - 1] != original:
                print("%s:%d no longer matches, nothing written" % (path, number), file=sys.stderr)
                return 2
            lines[number - 1] = replacement
            changed += 1
        open(path, "w", encoding="utf-8").write("\n".join(lines))
    print("replaced: %d lines in %d files" % (changed, len(edits)), file=sys.stderr)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--extract", nargs="+", metavar="PATH")
    parser.add_argument("--apply", metavar="TSV")
    args = parser.parse_args()

    if args.extract:
        return extract(args.extract, sys.stdout)
    if args.apply:
        return apply(args.apply)
    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
