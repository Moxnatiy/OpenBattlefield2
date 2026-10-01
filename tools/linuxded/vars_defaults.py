#!/usr/bin/env python3
"""The engine's tuning variables and their defaults, out of the Linux server.

A module registers its knobs at start-up with `Vars::getInt(name, default)` and
`Vars::getFloat(name, default)` (Linux 0x4ee6b0, 0x4eea40) and keeps the
pointer each returns in a global — `rd_dT`, `rd_iterations` and so on. This
walks a function's disassembly, follows the registers that carry the name
(`%edi`), an integer default (`%esi`) and a float default (`%xmm0`), and prints
one row per call: the name, the default, and the global it is stored in.

    tools/linuxded/vars_defaults.py 0x6baa00 0x6bb000

The range is the registering function (for the ragdoll, the static
initialiser of RagDoll.cpp around 0x6bad3c). A default the walk could not
follow is printed as `?` rather than guessed.
"""
import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from elf_symbol import read_sections  # noqa: E402

DEFAULT_BINARY = Path("Game Files/OtherFiles/linuxded-full/bin/amd-64/bf2")


def read_at(data, sections, address, size):
    for section in sections:
        if section["type"] == 8:  # NOBITS
            continue
        if section["addr"] <= address < section["addr"] + section["size"]:
            offset = section["offset"] + address - section["addr"]
            return data[offset : offset + size]
    return None


def c_string(data, sections, address):
    raw = read_at(data, sections, address, 256)
    if raw is None:
        return None
    return raw.split(b"\0", 1)[0].decode(errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("start", type=lambda v: int(v, 0))
    parser.add_argument("stop", type=lambda v: int(v, 0))
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    args = parser.parse_args()

    data = args.binary.read_bytes()
    sections = read_sections(data)
    listing = subprocess.run(
        ["objdump", "-d", "--no-show-raw-insn", "-C", f"--start-address={args.start:#x}",
         f"--stop-address={args.stop:#x}", str(args.binary)],
        capture_output=True, text=True, check=True).stdout

    regs = {}    # 32-bit register -> an immediate it holds
    stack = {}   # rsp offset -> an immediate stored there
    xmm0 = None  # the float default, as raw bits
    pending = None
    rows = []
    for line in listing.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+(\S+)\s*(.*)", line)
        if not m:
            continue
        address, op, operands = int(m.group(1), 16), m.group(2), m.group(3)
        operands = operands.split("#")[0].strip()

        if pending is not None:
            # The call's result is the variable's pointer, kept in a global.
            store = re.match(r"%rax, 0x[0-9a-f]+\(%rip\)", operands)
            if op == "movq" and store:
                target = re.search(r"<(.*)>", line)
                pending["global"] = target.group(1) if target else "?"
                rows.append(pending)
                pending = None
                continue

        if op == "movl" and (mi := re.match(r"\$(0x[0-9a-f]+|\d+), %(e\w+|r\d+d)$", operands)):
            regs[mi.group(2)] = int(mi.group(1), 0)
        elif op in ("xorl",) and (mz := re.match(r"%(\w+), %(\w+)$", operands)) \
                and mz.group(1) == mz.group(2):
            regs[mz.group(1)] = 0
        elif op == "movl" and (ms := re.match(r"%(\w+), (0x[0-9a-f]+|\d*)\(%rsp\)$", operands)):
            stack[int(ms.group(2) or "0", 0)] = regs.get(ms.group(1))
        elif op == "movss" and (mr := re.match(r"0x[0-9a-f]+\(%rip\), %xmm0$", operands)):
            target = re.search(r"# (0x[0-9a-f]+)", line)
            raw = read_at(data, sections, int(target.group(1), 16), 4) if target else None
            xmm0 = struct.unpack("<I", raw)[0] if raw else None
        elif op == "movss" and (mk := re.match(r"(0x[0-9a-f]+|\d*)\(%rsp\), %xmm0$", operands)):
            xmm0 = stack.get(int(mk.group(1) or "0", 0))
        elif op == "xorps" and operands == "%xmm0, %xmm0":
            xmm0 = 0
        elif op == "callq" and ("Vars::getInt" in line or "Vars::getFloat" in line):
            name_address = regs.get("edi")
            name = c_string(data, sections, name_address) if name_address else None
            if "getInt" in line:
                value = regs.get("esi")
                default = "?" if value is None else str(value)
            else:
                default = "?" if xmm0 is None else repr(struct.unpack("<f", struct.pack("<I", xmm0))[0])
            pending = dict(at=address, name=name or "?", kind="int" if "getInt" in line else "float",
                           default=default)
            # The call clobbers the argument registers.
            for reg in ("eax", "ecx", "edx", "esi", "edi"):
                regs.pop(reg, None)
            xmm0 = None

    for row in rows:
        print(f"{row['at']:#x}  {row['kind']:5}  {row['name']:34} {row['default']:>12}  {row['global']}")


if __name__ == "__main__":
    main()
