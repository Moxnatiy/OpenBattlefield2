#!/usr/bin/env python3
"""Takes the HUD variable table from the decompilation of the registration functions.

    tools/hud_variables.py <file.json> [more.json ...]      # the table
    tools/hud_variables.py <file.json> --markdown           # for docs/

The input is what the MCP `ghidra` tool returns (`decompile_function`) — JSON
of the form `{"result": [{"name": ..., "code": ...}, ...]}`. That tool puts
large output into a file itself; it is that file which is fed in here.

## What it looks for

The client has **one** HUD variable manager — the global pointer
`DAT_0098734c` in `BF2.exe`. Every HUD object binds its own fields to it by
name in its registration function, and does so through the manager's method
table. The write/read pairs are visible in the wrappers 0x785d00..0x785f10:

| method | what it does |
|---|---|
| +0x10 | register a **bool** |
| +0x18 | get a pointer to a bool by name |
| +0x1c | register an **int** |
| +0x24 | get a pointer to an int |
| +0x28 | register a **float** |
| +0x30 | get a pointer to a float |
| +0x34 | register a **string** |
| +0x3c | find a string (then +0x68 stores the value) |
| +0x40 | register a **wide string** |
| +0x48 | find a wide string (then +0x6c) |
| +0x7c | register a console command handler |

A call has the form `(**(code **)(*DAT_0098734c + 0x28))(name, field)`, and
the name is a `std::string` assembled on the line above. The script walks the
statements top down, keeps the last string for every local variable and prints
"name -> kind, field offset" at every call.

Two functions that create **reference data in the MemeFile graph** are caught
separately (`FloatRefData` is 0x14 bytes, the boolean one 0x10):

    0x789400  float reference: name, path in the graph
    0x7858c0  boolean reference

Through them a HUD object's field becomes **the very same cell** as a variable
of the `Menu/Ingame` graph — see docs/functions/hud-bottom-right.md.

## The limit

The field's offset is printed as the decompiler showed it: almost always that
is `param_1 + 0x…`, an offset within the object itself. Where it is computed
otherwise (through a local variable), the column holds the original expression
— that is not a bug in the script but what is visible in the binary.

The script does **not know** which object this is: the registration function's
name is its label. Who writes a value and when is not visible here either
(rule 7); for that the function itself has to be looked at.
"""
import argparse
import json
import re
import sys
from collections import Counter

# Manager method -> variable kind. Taken from the wrappers 0x785d00..0x785f10
# in `BF2.exe` (see the table row in the docstring above).
KINDS = {
    "0x10": "bool",
    "0x1c": "int",
    "0x28": "float",
    "0x34": "string",
    "0x40": "wide string",
}

# The same methods but for reading — they occur in the same functions.
READERS = {
    "0x18": "read bool",
    "0x24": "read int",
    "0x30": "read float",
    "0x3c": "find string",
    "0x48": "find wide string",
    "0x7c": "console command",
}

# The two functions that attach an object's field to the MemeFile graph.
REFS = {
    "FUN_00789400": "float reference",
    "FUN_007858c0": "bool reference",
}

# The second, larger registry is `HudInformationLayer`. It is not the graph's
# variable manager but a separate layer: the classic HUD variables live in it
# (`MapMinSize`, `CPInterfaceEnabled`, `FriendlyTicketsString`…).
#
# The five functions are five instantiations of one `register<T>` template:
# their code is literally identical, differing only in the line in
# `code\\bf2\\game\\HudInformationLayer.h` (0x6b..0x6f) and in the field the
# list is put into (+0x10, +0x20, +0x30, +0x40, +0x50).
#
# The type is not recorded in the registration itself — two things give it, and
# they agree. First, the order of the instantiations in the header is the same
# as the order of the variable manager's methods (bool, int, float, string,
# wide string). Second, it is visible from the names themselves: layer 1 has
# `MapMinSize` and `CPInterfaceEnabled`, layer 2 `MapZoom` and `PlayerTeam`,
# layer 3 `FriendlyTicketRed`/`Green`/`Blue`/`Alpha`, layer 4
# `FriendlyTicketsString`, layer 5 `DeathMessage` and `WinHeadlineString`
# (localised).
LAYER = {
    "FUN_00466240": "layer bool (h:107)",
    "FUN_00466390": "layer int (h:108)",
    "FUN_004664e0": "layer float (h:109)",
    "FUN_00466630": "layer string (h:110)",
    "FUN_00466780": "layer wide (h:111)",
}

STRING = re.compile(r'basic_string<[^)]*> *\( *(local_\w+) *, *"([^"]*)" *\)$')

# Not every name is a literal string. The map node (0x780180) assembles them
# from its own name: `FUN_004310d0(local_28, "%sDelayedMapAngle", name, ...)`
# gives `MinimapDelayedMapAngle`. The pattern is kept as is, with `%s` — the
# game substitutes it, and that is exactly how it reads in the data.
FORMAT = re.compile(r'^(\w+) = FUN_004310d0\( *(local_\w+) *, *"([^"]*)"')
REF_CALL = re.compile(r"^(FUN_\w+)\((local_\w+), *(local_\w+)\)$")
# `HudInformationLayer`: `FUN_00466240(name, field)`; the field is written
# both as `param_1 + 0x2a` and as `(int)param_1 + 0x1b1`.
LAYER_CALL = re.compile(r"^(FUN_\w+)\((\w+), *(.+?)\)$")

# Variable families are created in a loop, and the offset is first put into a
# local: `iVar2 = (int)param_1 + iVar1 + 0x202`, and only then `Kit%iShow` is
# registered at `iVar2`. Without this the "field" column would hold `iVar2`.
OFFSET = re.compile(r"^(\w+) = (\(int\))?param_1 \+ (.+)$")
# The second argument exists only in a registration: readers take just a name.
#
# The manager is called in two ways, and both occur: straight through the
# global pointer, or after putting it into a local variable first
# (`iVar10 = *DAT_0098734c`, then `(**(code **)(iVar10 + 0x28))(...)`).
# The second form is produced by, for example, 0x780180 — the largest table
# after 0x789480, which without this reads as empty.
MANAGER = "DAT_0098734c"
CACHE = re.compile(r"^(\w+) = \*" + MANAGER + r"$")
VT_CALL = re.compile(
    r"\(\*\*\(code \*\*\)\((?:\*(?:\(int \*\))?)?(\w+) \+ (0x[0-9a-f]+)\)\)"
    r"\( *(\w+)(?:, *(.+?))? *\)$")


def statements(code):
    """Decompiled text -> individual statements.

    Ghidra breaks long calls across several lines, so the split has to be on
    the semicolon, not on the newline.
    """
    for piece in code.split(";"):
        yield re.sub(r"\s+", " ", piece).strip()


def scan(code):
    """One function -> a list of (method, variable name, what it is bound to)."""
    strings = {}
    offsets = {}
    cached = set()
    found = []
    for line in statements(code):
        match = STRING.search(line)
        if match:
            strings[match.group(1)] = match.group(2)
            continue
        match = CACHE.match(line)
        if match:
            cached.add(match.group(1))
            continue
        match = FORMAT.match(line)
        if match:
            strings[match.group(1)] = match.group(3)
            strings[match.group(2)] = match.group(3)
            continue
        match = OFFSET.match(line)
        if match:
            offsets[match.group(1)] = ("param_1 + " if match.group(2) is None
                                       else "(int)param_1 + ") + match.group(3)
            continue
        match = REF_CALL.match(line)
        if match and match.group(1) in REFS:
            found.append((REFS[match.group(1)], strings.get(match.group(2)),
                          strings.get(match.group(3))))
            continue
        match = LAYER_CALL.match(line)
        if match and match.group(1) in LAYER:
            where = match.group(3)
            found.append((LAYER[match.group(1)], strings.get(match.group(2)),
                          offsets.get(where, where)))
            continue
        match = VT_CALL.search(line)
        if match:
            if match.group(1) != MANAGER and match.group(1) not in cached:
                continue
            slot = match.group(2)
            kind = KINDS.get(slot) or READERS.get(slot) or ("method " + slot)
            found.append((kind, strings.get(match.group(3)), match.group(4)))
    return found


def field(where, elements=False):
    """`param_1 + 0x30` -> `+0x30`; decimal is converted to hex as well.

    `elements` is set when the decompiler declared `param_1` a pointer to a
    four-byte type (`undefined4 *param_1`). Then `param_1 + 0x2a` is the
    **forty-second element**, that is byte 0xa8, not 0x2a. An explicit
    `(int)param_1 + 0x1b1` in the same function is, on the contrary, already
    in bytes. Without this correction half the offsets in 0x468dd0 would be
    four times too small.
    """
    if where is None:
        return "?"
    bytewise = "(int)param_1" in where
    match = re.match(r"^(?:\(int\))?param_1 \+ (\d+|0x[0-9a-f]+)$", where)
    if not match:
        # Families such as `(int)param_1 + iVar1 + 0x202` do not reduce to a
        # single number: that is the start of a row, not a field. Kept as is.
        return where
    number = int(match.group(1), 0)
    if elements and not bytewise:
        number *= 4
    return "+" + hex(number)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--markdown", action="store_true", help="table for docs/")
    parser.add_argument("--only-registered", action="store_true",
                        help="registrations only, no reads")
    args = parser.parse_args()

    rows = []
    # The same decompilation file is easy to pass twice (the tool writes them
    # with timestamped names). Identical rows are not two registrations but two
    # reads of the same thing, so only one of each is kept.
    seen = set()
    for path in args.files:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
        for item in data.get("result", []):
            code = item.get("code") or ""
            if not code:
                continue
            # "FUN_00789480-00789480" -> "0x789480".
            name = item.get("name", "?")
            # Whether the decompiler declared `param_1` a pointer to a word.
            elements = bool(re.search(r"(undefined4|int|uint) \*param_1",
                                      item.get("signature") or ""))
            address = name.split("-")[-1]
            owner = "0x" + address.lstrip("0") if re.fullmatch(r"[0-9a-f]+", address) else name
            for kind, variable, where in scan(code):
                if args.only_registered and kind not in KINDS.values() \
                        and kind not in LAYER.values():
                    continue
                row = (owner, kind, variable or "?", field(where, elements))
                if row in seen:
                    continue
                seen.add(row)
                rows.append(row)

    if args.markdown:
        print("| variable | kind | field | registered by |")
        print("|---|---|---|---|")
        for owner, kind, variable, where in rows:
            print(f"| `{variable}` | {kind} | {where} | {owner} |")
    else:
        for owner, kind, variable, where in rows:
            print(f"{owner}  {kind:22} {variable:34} {where}")
        print(f"\ntotal {len(rows)}", file=sys.stderr)
        print(dict(Counter(kind for _, kind, _, _ in rows)), file=sys.stderr)


if __name__ == "__main__":
    main()
