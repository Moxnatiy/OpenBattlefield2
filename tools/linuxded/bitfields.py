#!/usr/bin/env python3
"""Extracts field layouts from the code of BF2's 64-bit Linux server.

    tools/linuxded/bitfields.py CreatePlayerEvent::deSerialize [more names...]
    tools/linuxded/bitfields.py --blocks CreateObjectEvent::serialize

It takes a function apart and writes out every `BitStream::readBits`/`writeBits`
call together with the bit count: in System V AMD64 the third argument (the
count) travels in `%edx`, so a field's size is visible right in the code.

`--blocks` additionally shows the function's shape — which fields lie in one
branch and which behind a condition. A flat list does not show that, and it is
exactly where the fields hide that exist on the wire while the read list seems
to lack them.

Everything is done locally: the binary lies in the game's directory, it need not
be executed, and `objdump` on macOS takes an ELF x86-64 apart without trouble.
This tool used to go over ssh into a container with gdb — now a query costs 0.1 s
instead of a second and a bit.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BINARY = os.environ.get(
    "BF2_BINARY",
    os.path.join(ROOT, "Game Files/OtherFiles/linuxded-full/bin/amd-64/bf2"))
CACHE = os.path.join(HERE, "__pycache__", "symbols.txt")

CALL = re.compile(r"<(?P<name>[^>+]+)")
WIDTH = re.compile(r"mov\w*\s+\$0x([0-9a-f]+),\s*%edx")
INSTR = re.compile(r"^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S+)\s*(.*)$")
JUMP = re.compile(r"^j\w+$")
TARGET = re.compile(r"^0x([0-9a-f]+)")


def symbols():
    """The table "demangled name -> address, size". Read once."""
    if os.path.exists(CACHE) and os.path.getmtime(CACHE) > os.path.getmtime(BINARY):
        text = open(CACHE).read()
    else:
        text = subprocess.run(["nm", "-C", "--print-size", BINARY],
                              capture_output=True, text=True).stdout
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        open(CACHE, "w").write(text)

    table = {}
    for line in text.splitlines():
        parts = line.split(" ", 3)
        if len(parts) < 3:
            continue
        address = parts[0].strip()
        if not re.fullmatch(r"[0-9a-f]+", address):
            continue
        if len(parts) == 4 and re.fullmatch(r"[0-9a-f]+", parts[1]):
            size, name = int(parts[1], 16), parts[3]
        else:
            size, name = 0, parts[-1]
        table.setdefault(name.strip(), (int(address, 16), size))
    return table


def find(table, wanted):
    """Finds a function by the tail of its name, e.g. `CreatePlayerEvent::deSerialize`."""
    for name, (address, size) in table.items():
        head = name.split("(")[0]
        if head.endswith(wanted):
            return address, size, name
    return None


def disassemble(address, size):
    if size <= 0:
        size = 0x400
    out = subprocess.run(
        ["objdump", "-d", "--no-show-raw-insn",
         "--start-address=0x%x" % address, "--stop-address=0x%x" % (address + size),
         BINARY], capture_output=True, text=True).stdout
    lines = []
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$", line)
        if m:
            lines.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
    return lines


def scan(lines):
    """The fields and jumps in the order they appear."""
    steps = []
    width = None
    for at, op, args in lines:
        m = WIDTH.search("%s %s" % (op, args))
        if m:
            width = int(m.group(1), 16)
            continue
        if op.startswith("call"):
            m = CALL.search(args)
            name = m.group("name") if m else ""
            if "readBits" in name or "writeBits" in name:
                steps.append(("field", at, width))
                width = None
            elif "String" in name:
                steps.append(("string", at, None))
        elif JUMP.match(op):
            m = TARGET.search(args)
            steps.append(("branch" if op != "jmp" else "jump", at,
                          int(m.group(1), 16) if m else None))
    return steps


def blocks(lines):
    """Splits the function into blocks at the jump targets."""
    starts = {lines[0][0]} if lines else set()
    for at, op, args in lines:
        if JUMP.match(op):
            m = TARGET.search(args)
            if m:
                starts.add(int(m.group(1), 16))
            nxt = next((a for a, _, _ in lines if a > at), None)
            if nxt:
                starts.add(nxt)
    groups, current = [], None
    for entry in lines:
        if entry[0] in starts:
            current = {"from": entry[0], "lines": []}
            groups.append(current)
        if current is not None:
            current["lines"].append(entry)
    return groups


def report(name, address, lines, show_blocks):
    print("%s (0x%x)" % (name, address))
    if not show_blocks:
        total = 0
        found = False
        for kind, _, value in scan(lines):
            if kind == "field":
                found = True
                print("  %s bits" % (value if value is not None else "?"))
                if value is not None and total is not None:
                    total += value
                else:
                    total = None
            elif kind == "string":
                found = True
                print("  string")
                total = None
        if not found:
            print("  no BitStream calls")
        elif total is not None:
            print("  total: %d bits" % total)
        return

    for group in blocks(lines):
        steps = scan(group["lines"])
        fields = [s for s in steps if s[0] in ("field", "string")]
        parts = [str(v) if k == "field" and v is not None else
                 ("string" if k == "string" else "?") for k, _, v in fields]
        # Where the block's end leads: a forward conditional jump means part of
        # the fields can be skipped.
        tail = ""
        jumps = [s for s in steps if s[0] in ("branch", "jump")]
        if jumps:
            kind, at, target = jumps[-1]
            if target is not None:
                tail = "  -> 0x%x%s" % (target, "" if kind == "jump" else " (conditional)")
        if not parts and not tail:
            continue
        print("  block 0x%-8x %-28s%s" % (group["from"], ", ".join(parts) or "—", tail))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("names", nargs="+")
    parser.add_argument("--blocks", action="store_true",
                        help="show the shape: what is in one branch and what is conditional")
    args = parser.parse_args()

    table = symbols()
    missing = []
    for wanted in args.names:
        hit = find(table, wanted)
        if not hit:
            missing.append(wanted)
            continue
        address, size, full = hit
        report(wanted, address, disassemble(address, size), args.blocks)
    for wanted in missing:
        print("did not find %s" % wanted, file=sys.stderr)
    return 1 if missing and len(missing) == len(args.names) else 0


if __name__ == "__main__":
    sys.exit(main())
