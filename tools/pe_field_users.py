#!/usr/bin/env python3
"""Who touches a field of the object a global pointer holds, in a 32-bit PE.

    tools/pe_field_users.py <pe> <global> <offset> [--window N]

Ghidra lists the reads of the global itself (`MOV ECX,[0x1023d098]`) but not
what the code does with the pointer after, so "who reads +0x590 of the
renderer's settings" is not a query it answers. This walks every executable
section with capstone, and wherever an instruction loads the global into a
register, follows that register through the next N instructions (default 12)
and prints each one that addresses `[reg + offset]`, until the register is
written again.

Limits: a straight-line follow, not a data-flow analysis. It does not see the
pointer passed to another function, kept on the stack, or copied into a second
register; it also misses code it decodes out of step (data in a code section).
"""
import argparse
import struct
import sys

import capstone
from capstone import x86


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    for i in range(count):
        at = pe + 24 + optional + 40 * i
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, at + 8)
        flags = struct.unpack_from("<I", data, at + 36)[0]
        if flags & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            yield base + va, data[raw:raw + min(rsize, vsize)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("pe")
    parser.add_argument("global_address", type=lambda s: int(s, 0))
    parser.add_argument("offset", type=lambda s: int(s, 0))
    parser.add_argument("--window", type=int, default=12)
    args = parser.parse_args()

    data = open(args.pe, "rb").read()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    found = 0
    for start, code in sections(data):
        listing = list(md.disasm(code, start))
        for i, insn in enumerate(listing):
            if insn.id != x86.X86_INS_MOV or len(insn.operands) != 2:
                continue
            dst, src = insn.operands
            if not (dst.type == x86.X86_OP_REG and src.type == x86.X86_OP_MEM and
                    src.mem.base == 0 and src.mem.index == 0 and
                    (src.mem.disp & 0xFFFFFFFF) == args.global_address):
                continue
            reg = dst.reg
            for follow in listing[i + 1:i + 1 + args.window]:
                if not follow.operands and follow.mnemonic != "ret":
                    continue
                for op in follow.operands:
                    if (op.type == x86.X86_OP_MEM and op.mem.base == reg and
                            (op.mem.disp & 0xFFFFFFFF) == args.offset):
                        print(f"{follow.address:08x}  {follow.mnemonic} {follow.op_str}"
                              f"   (pointer loaded at {insn.address:08x})")
                        found += 1
                _, written = follow.regs_access()
                if reg in written or follow.mnemonic in ("ret", "jmp", "call"):
                    break
    print(f"{found} uses", file=sys.stderr)


if __name__ == "__main__":
    main()
