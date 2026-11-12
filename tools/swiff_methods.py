#!/usr/bin/env python3
"""A bridge object's methods with the address of each, out of `SwiffPlayer.dll`.

    tools/swiff_methods.py "Game Files/SwiffPlayer.dll" getTeamRatio
    tools/swiff_methods.py "Game Files/SwiffPlayer.dll" getTeamRatio --markdown

`tools/swiff_bridge.py` lists the names; this gives the code behind them. Every
bridge object is a class whose constructor registers its methods one by one
(docs/functions/menu-bridge.md, "How the bridge is built inside"):

    push   <name>                     ; the method's name
    mov    dword ptr [ebp - N], <impl> ; the descriptor's native function
    call   0x10007680                 ; assemble the name
    ...
    call   0x1000bb50                 ; add the method

Give any one method name of the object. The script finds the `push` of that
string, walks back to the start of the function it sits in (`push ebp; mov
ebp, esp`), disassembles forward to the function's `ret`, and pairs every
pushed string with the next immediate stored before the name-assembling call.

Limits: a name built another way, or an implementation stored through a
register, is not seen; the order printed is the registration order.
"""
import argparse
import struct
import sys

try:
    import capstone
except ImportError:
    sys.exit("needs the capstone module (pip install capstone)")

ASSEMBLE_NAME = 0x10007680
ADD_METHOD = 0x1000bb50


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    table = pe + 24 + optional
    out = []
    for i in range(count):
        name, vsize, vaddr, rsize, raw = struct.unpack_from("<8sIIII", data, table + i * 40)
        out.append((name.rstrip(b"\0").decode(), base + vaddr, vsize, raw, rsize))
    return base, out


def va_to_off(secs, va):
    for _, start, vsize, raw, rsize in secs:
        if start <= va < start + max(vsize, rsize):
            return raw + va - start
    return None


def off_to_va(secs, off):
    for _, start, _, raw, rsize in secs:
        if raw <= off < raw + rsize:
            return start + off - raw
    return None


def c_string(data, secs, va):
    off = va_to_off(secs, va)
    if off is None:
        return None
    end = data.find(b"\0", off, off + 128)
    if end <= off:
        return None
    raw = data[off:end]
    if not all(32 <= b < 127 for b in raw):
        return None
    return raw.decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dll")
    parser.add_argument("method", help="any one method name of the object")
    parser.add_argument("--markdown", action="store_true", help="a table for docs/")
    args = parser.parse_args()

    data = open(args.dll, "rb").read()
    _, secs = sections(data)
    needle = (args.method + "\0").encode()
    name_off = data.find(b"\0" + needle) + 1
    if name_off <= 0:
        sys.exit(f"no string {args.method!r}")
    name_va = off_to_va(secs, name_off)
    push = data.find(struct.pack("<BI", 0x68, name_va))
    if push < 0:
        sys.exit(f"nothing pushes {args.method!r} (0x{name_va:x})")

    # Back to the function's start: the nearest `push ebp; mov ebp, esp`.
    start = data.rfind(b"\x55\x8b\xec", max(0, push - 0x20000), push)
    if start < 0:
        sys.exit("no function start found")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    start_va = off_to_va(secs, start)

    rows = []
    pending_name = None
    pending_impl = None
    for ins in md.disasm(data[start:start + 0x20000], start_va):
        if ins.mnemonic == "ret":
            break
        if ins.mnemonic == "push" and ins.op_str.startswith("0x"):
            text = c_string(data, secs, int(ins.op_str, 16))
            if text:
                pending_name, pending_impl = text, None
            continue
        if ins.mnemonic == "mov" and ins.op_str.startswith("dword ptr [ebp") and pending_name:
            value = ins.op_str.split(", ")[-1]
            if value.startswith("0x") and int(value, 16) >= 0x10001000:
                pending_impl = int(value, 16)
            continue
        if ins.mnemonic == "call" and ins.op_str == hex(ASSEMBLE_NAME) and pending_name:
            rows.append((pending_name, pending_impl))
            pending_name, pending_impl = None, None

    print(f"function 0x{start_va:x}: {len(rows)} methods")
    if args.markdown:
        print("| method | implementation |")
        print("|---|---|")
    for name, impl in rows:
        where = f"0x{impl:x}" if impl else "?"
        print(f"| `{name}` | {where} |" if args.markdown else f"{where:>12}  {name}")


if __name__ == "__main__":
    main()
