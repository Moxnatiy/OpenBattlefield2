#!/usr/bin/env python3
"""The engine's control constants (`c_PIYaw`, `c_PIAction`, …) and their numbers.

    tools/control_constants.py ["Game Files/BF2.exe"]

`BF2.exe` registers every `c_PI*` name with its number from 0x69045d on: a
`push <name>` builds the string, the number goes into EDX (`xor edx, edx`,
`inc edx`, `push n; pop edx` or `mov edx, n`), then `call 0x68f099`. The
number is the axis's index in a `PlayerInput` and the bit its mask carries
(axis n: bit n of the 64-bit mask, the second word from 32 on). This walks
those calls and prints the list.
"""
import importlib.util
import pathlib
import struct
import sys

import capstone

REGISTER = 0x68F099


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "Game Files/BF2.exe"
    here = pathlib.Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location("pe", here / "pe_field_users.py")
    pe_tools = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pe_tools)
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    sections = []
    for i in range(count):
        at = pe + 24 + optional + 40 * i
        _, va, raw_size, raw = struct.unpack_from("<IIII", data, at + 8)
        sections.append((va, raw_size, raw))

    def text_at(address):
        rva = address - base
        for va, size, raw in sections:
            if va <= rva < va + size:
                start = raw + rva - va
                return data[start:start + 64].split(b"\0")[0].decode("latin1")
        return None

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    for start, code in pe_tools.sections(data):
        name, edx = None, None
        for insn in md.disasm(code, start):
            if insn.address < 0x690400 or insn.address > 0x6A0000:
                continue
            ops = insn.op_str
            if insn.mnemonic == "push" and (ops.startswith("0x") or ops.isdigit()):
                value = int(ops, 0)
                text = text_at(value) if value > 0x800000 else None
                if text and text.startswith("c_"):
                    name = text
                else:
                    edx = value  # `push n; pop edx`
            elif insn.mnemonic == "xor" and ops == "edx, edx":
                edx = 0
            elif insn.mnemonic == "inc" and ops == "edx" and edx is not None:
                edx += 1
            elif insn.mnemonic == "mov" and ops.startswith("edx, 0x"):
                edx = int(ops.split(", ")[1], 16)
            elif insn.mnemonic == "mov" and ops.startswith("edx, ") and ops[5:].isdigit():
                edx = int(ops[5:])
            elif insn.mnemonic == "call" and ops == hex(REGISTER):
                if name is not None and edx is not None:
                    print(f"{edx:3d}  {name}")
                name, edx = None, None
            elif insn.mnemonic == "ret":
                break


if __name__ == "__main__":
    main()
