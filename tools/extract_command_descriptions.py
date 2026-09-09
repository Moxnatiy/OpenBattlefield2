#!/usr/bin/env python3
"""Pulls the official .con command descriptions out of the BF2 editor.

`bf2editor/Help/CommandDescriptions.dat` is UTF-16LE, the strings separated by
NUL and coming in "command, description" pairs. This is documentation from DICE
itself, so it is worth more than any guesswork: it shows what a command does,
not merely that it exists.

    python3 tools/extract_command_descriptions.py \
        "Game Files/OtherFiles/bf2editor_and_tools/bf2editor/Help/CommandDescriptions.dat" \
        docs/reference/con-command-descriptions.txt
"""
import sys


def extract(path: str):
    with open(path, "rb") as handle:
        raw = handle.read()

    text = raw.decode("utf-16-le", errors="replace")
    parts = [part.strip() for part in text.split("\0")]
    parts = [part for part in parts if part]

    # The first two strings are a service header ("LANGUAGE", "Description").
    if len(parts) >= 2 and parts[0].upper() == "LANGUAGE":
        parts = parts[2:]

    # We do not step strictly every other one: empty descriptions occur in the
    # file, and they shift the whole stream that follows for good. Instead we
    # look for what looks like a command ("target.method" with no spaces) and
    # take the next string as its description — so the parse resynchronises.
    def looks_like_command(value: str) -> bool:
        return "." in value and " " not in value and not value.endswith(".")

    pairs = []
    index = 0
    while index < len(parts) - 1:
        if looks_like_command(parts[index]) and not looks_like_command(parts[index + 1]):
            pairs.append((parts[index], parts[index + 1]))
            index += 2
        else:
            index += 1
    return pairs


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    pairs = extract(sys.argv[1])
    pairs.sort(key=lambda item: item[0].lower())

    with open(sys.argv[2], "w", encoding="utf-8") as out:
        out.write("# The official descriptions of the .con commands, from the BF2 editor\n")
        out.write("# Source: bf2editor/Help/CommandDescriptions.dat\n")
        out.write(f"# Commands: {len(pairs)}\n")
        for command, description in pairs:
            out.write(f"{command}\t{description}\n")

    print(f"commands: {len(pairs)} -> {sys.argv[2]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
