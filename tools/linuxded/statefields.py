#!/usr/bin/env python3
"""An object state's layout: which mask bit enables which field.

    tools/linuxded/statefields.py SoldierNetworkable::setNetUpdate

The networked classes read state the same way: the mask first, then the fields,
each under its own bit. `bitfields.py` shows **how many** bits are read and where,
but does not say **under which bit** — and without that the layout cannot be
reproduced: a field skipped through not knowing its bit shifts everything after it.

The script walks the instructions by address and keeps two things:

* the last mask check (`andl $imm, %reg` over the mask variable or
  `testb $imm, offset(%rsp)`) — that is the bit;
* the `BitStream::readBits` calls with the width in `%edx`, and also
  `readCompressedVector` (there is no width there, but the precision is visible).

**WARNING: this output cannot be trusted yet.** The script walks the instructions
by **address** rather than by control flow, and stumbles on that: the blocks in
the binary do not lie in the order they execute, so "the last check seen" is often
from a different block. A check against a known place shows it directly: the script
attributes a simple object's position (0x5d845b) to bit 0x8, while in fact bit 0x2
enables it (`testb $0x2` at 0x5d7a35). A nonsensical width such as 17367456 occurs
too — that is an `%edx` left over from a neighbouring instruction.

To make the output right, one has to walk the **block graph** (which
`bitfields.py --blocks` already builds) and for every block with a read take the
condition that leads to it. Until then the script serves only as a draft: it shows
where to look but does not say what lies under which bit.
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

INSTR = re.compile(r"^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(.*)$")
AND_IMM = re.compile(r"^and[lb]\s+\$(0x[0-9a-f]+|\d+),")
TEST_IMM = re.compile(r"^testb\s+\$(0x[0-9a-f]+|\d+),")
MOV_EDX = re.compile(r"^movl\s+\$(0x[0-9a-f]+|\d+), %edx")
CALL = re.compile(r"^callq?\s+0x[0-9a-f]+ <([^>]+)>")


def disassemble(pattern):
    text = subprocess.run(["objdump", "-d", "--no-show-raw-insn", "-C", BINARY],
                          capture_output=True, text=True).stdout
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if line.endswith(">:") and pattern in line:
            start = i
            break
    if start is None:
        return None, []
    body = []
    for line in lines[start + 1:]:
        if line.endswith(">:"):
            break
        body.append(line)
    return lines[start], body


def main():
    global BINARY
    parser = argparse.ArgumentParser()
    parser.add_argument("name")
    parser.add_argument("--binary", default=BINARY)
    args = parser.parse_args()
    BINARY = args.binary

    header, body = disassemble(args.name)
    if header is None:
        print("function not found:", args.name, file=sys.stderr)
        return 1
    print(header.strip())

    bit = None
    width = None
    order = 0
    for raw in body:
        m = INSTR.match(raw.replace("\t", "\t", 1)) or re.match(
            r"^\s+([0-9a-f]+):\s+(.*)$", raw)
        if not m:
            continue
        at, text = m.group(1), m.group(2).strip()

        hit = AND_IMM.match(text) or TEST_IMM.match(text)
        if hit:
            value = int(hit.group(1), 0)
            # A state mask is bits rather than arbitrary numbers: we take only
            # powers of two, the rest (0x3, 0xf) is not "a field's bit".
            if value and (value & (value - 1)) == 0:
                bit = value
            continue

        hit = MOV_EDX.match(text)
        if hit:
            width = int(hit.group(1), 0)
            continue

        hit = CALL.match(text)
        if not hit:
            continue
        callee = hit.group(1)
        if "readBits" in callee:
            order += 1
            print("  %2d  bit %-8s %3s bits   (0x%s)"
                  % (order, hex(bit) if bit else "?", width if width else "?", at))
            width = None
        elif "readCompressedVector" in callee:
            order += 1
            print("  %2d  bit %-8s compressed vector (0x%s)"
                  % (order, hex(bit) if bit else "?", at))
        elif "readString" in callee or "readSmallString" in callee:
            order += 1
            print("  %2d  bit %-8s string (0x%s)" % (order, hex(bit) if bit else "?", at))
    return 0


if __name__ == "__main__":
    sys.exit(main())
