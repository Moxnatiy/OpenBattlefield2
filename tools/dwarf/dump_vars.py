#!/usr/bin/env python3
"""Pulls the engine's variable values out of the BF2 Linux server's registration code.

The registration has a fixed shape:
    MOV dword ptr [ESP], <the address of the string with the name>
    MOV EAX, <the bit pattern of the float>
    ...
    CALL <register>

The script finds these pairs in .text and prints "name = value".
This is a specification for our implementation, not a copy of the code.
"""
import re, struct, subprocess, sys
from elftools.elf.elffile import ELFFile

binary = sys.argv[1]
pattern = sys.argv[2] if len(sys.argv) > 2 else ""

with open(binary, "rb") as handle:
    elf = ELFFile(handle)
    sections = [(s["sh_addr"], s.data()) for s in elf.iter_sections()
                if s["sh_addr"] and s.data()]

def read_cstring(addr):
    for base, data in sections:
        if base <= addr < base + len(data):
            end = data.index(b"\0", addr - base)
            return data[addr - base:end].decode("latin-1", "replace")
    return None

out = subprocess.run(["objdump", "-d", binary],
                     capture_output=True, text=True).stdout

mov_esp = re.compile(r"movl?\s+\$0x([0-9a-f]+),\s*\(%esp\)")
mov_eax = re.compile(r"movl?\s+\$0x([0-9a-f]+),\s*%eax")

pending = None
for line in out.splitlines():
    m = mov_esp.search(line)
    if m:
        pending = int(m.group(1), 16)
        continue
    m = mov_eax.search(line)
    if m and pending is not None:
        name = read_cstring(pending)
        pending = None
        if not name or not re.match(r"^[a-z][a-z0-9-]{4,}$", name):
            continue
        if pattern and pattern not in name:
            continue
        bits = int(m.group(1), 16)
        value = struct.unpack("<f", struct.pack("<I", bits))[0]
        as_int = struct.unpack("<i", struct.pack("<I", bits))[0]
        if abs(value) < 1e-6 or abs(value) > 1e6:
            print(f"{name:44} {as_int}")
        else:
            print(f"{name:44} {value:g}")
