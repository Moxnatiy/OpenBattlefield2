#!/usr/bin/env python3
"""What each method of a menu bridge object does, one line per method.

    tools/swiff_accessors.py "Game Files/SwiffPlayer.dll" getHUDTransparency

`tools/swiff_methods.py` gives the method table (name → implementation); this
walks every implementation once and prints a summary of it:

* the engine object it looks up by name (`"GeneralSettings"`, …);
* the virtual slots it calls (`+0x48`) in order;
* float constants its arithmetic loads (`*0.003921569` — 1/255);
* the type of the value it hands the movie (the descriptor's first field:
  1 a String, 2 a Number, 3 a Boolean), or `-` when it hands none;
* the format strings it uses.

It is a skim, not a decompilation: control flow is not followed, so a method
with branches shows every slot of every branch. Read a surprising one whole
with REA (`decompile`).
"""
import argparse
import os
import struct
import subprocess
import sys

try:
    import capstone
except ImportError:
    sys.exit("needs the capstone module (pip install capstone)")

HERE = os.path.dirname(os.path.abspath(__file__))


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    table = pe + 24 + optional
    out = []
    for i in range(count):
        _, vsize, vaddr, rsize, raw = struct.unpack_from("<8sIIII", data, table + i * 40)
        out.append((base + vaddr, max(vsize, rsize), raw, rsize))
    return out


def to_off(secs, va):
    for start, size, raw, rsize in secs:
        if start <= va < start + rsize:
            return raw + va - start
    return None


def c_string(data, secs, va):
    off = to_off(secs, va)
    if off is None:
        return None
    end = data.find(b"\0", off, off + 96)
    raw = data[off:end] if end > off else b""
    return raw.decode() if raw and all(32 <= b < 127 for b in raw) else None


def summarize(data, secs, va, md):
    off = to_off(secs, va)
    slots, consts, texts, objects = [], [], [], []
    result = "-"
    for ins in md.disasm(data[off:off + 0x400], va):
        ops = ins.op_str
        if ins.mnemonic == "call" and "ptr [" in ops and "+" in ops:
            slots.append("+" + ops.split("+")[-1].rstrip("]").strip())
        if ins.mnemonic.startswith("f") and "ptr [0x" in ops:
            addr = int(ops.split("[")[1].rstrip("]"), 16)
            o = to_off(secs, addr)
            if o is not None:
                size = 8 if "qword" in ops else 4
                fmt = "<d" if size == 8 else "<f"
                value = struct.unpack_from(fmt, data, o)[0]
                consts.append(f"{ins.mnemonic} {value:.9g}")
        if ins.mnemonic == "push" and ops.startswith("0x"):
            text = c_string(data, secs, int(ops, 16))
            if text:
                (objects if text[:1].isupper() and " " not in text and "%" not in text else texts).append(text)
        if ins.mnemonic == "mov" and ops.startswith("dword ptr [ebp - ") and ops.endswith((", 1", ", 2", ", 3")):
            result = ops[-1]
        if ins.mnemonic == "ret":
            break
    kind = {"1": "String", "2": "Number", "3": "Boolean", "-": "-"}[result]
    parts = [f"→ {kind}"]
    if objects:
        parts.append("on " + ",".join(dict.fromkeys(objects)))
    if slots:
        parts.append("slots " + " ".join(slots))
    if consts:
        parts.append("; ".join(consts))
    if texts:
        parts.append("strings " + ", ".join(repr(t) for t in texts))
    return "  ".join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dll")
    parser.add_argument("method", help="any one method name of the object")
    parser.add_argument("--only", help="keep the methods whose name contains this")
    args = parser.parse_args()
    table = subprocess.run([sys.executable, os.path.join(HERE, "swiff_methods.py"), args.dll, args.method],
                           capture_output=True, text=True, check=True).stdout.splitlines()
    data = open(args.dll, "rb").read()
    secs = sections(data)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    print(table[0])
    seen = set()
    for line in table[1:]:
        parts = line.split()
        if len(parts) != 2 or parts[0] == "?":
            continue
        va, name = int(parts[0], 16), parts[1]
        if (name, va) in seen or (args.only and args.only.lower() not in name.lower()):
            continue
        seen.add((name, va))
        print(f"{name:32} 0x{va:x}  {summarize(data, secs, va, md)}")


if __name__ == "__main__":
    main()
