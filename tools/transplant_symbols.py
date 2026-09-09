#!/usr/bin/env python3
"""Carries the function names over from the Linux server into BF2.exe.

    tools/transplant_symbols.py > docs/reference/bf2exe-symbols.txt

BF2.exe has no symbols — every function is called FUN_xxxxxx. But both
binaries carry the same `Debug` checks, and each one holds **the path to the
source file and a line number**. In the Linux server the function's name is
known next to such a check too, because the symbols there are full. So the
pair "file + line" is a bridge: the same pair in BF2.exe is the same function.

This removes the most expensive part of working with the client: instead of
hunting for a function by strings and guesses, we know what it is called.

Both sides are read locally through objdump: it takes both ELF and PE. In the
Linux server the string's address is loaded as an absolute number into `%esi`
and the number into `%ecx`; in BF2.exe both are pushed onto the stack.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINUX = os.environ.get(
    "BF2_BINARY",
    os.path.join(ROOT, "Game Files/OtherFiles/linuxded-full/bin/amd-64/bf2"))

DEBUG_CTOR = "DebugC1"          # dice::hfe::Debug::Debug(DebugType, string&, int, string&)
# The binary is not PIE, so the string's address is loaded as an absolute number.
STRING_ARG = re.compile(r"movl\s+\$0x([0-9a-f]+),\s*%esi")
LINE_IMM = re.compile(r"movl\s+\$0x([0-9a-f]+),\s*%ecx")
FUNC = re.compile(r"^0*([0-9a-f]+) <(.+)>:")
INSTR = re.compile(r"^\s*([0-9a-f]+):\s+(.*)$")


def source_strings(path):
    """An address -> the source file path, for every such string in the binary."""
    import struct

    data = open(path, "rb").read()
    e_shoff, = struct.unpack_from("<Q", data, 0x28)
    e_shentsize, e_shnum = struct.unpack_from("<HH", data, 0x3a)
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        addr, offset, size = struct.unpack_from("<QQQ", data, off + 0x10)
        if addr:
            sections.append((addr, offset, size))

    out = {}
    for m in re.finditer(rb"[ -~]{4,}\.cpp\x00", data):
        at = m.start()
        for addr, offset, size in sections:
            if offset <= at < offset + size:
                out[addr + (at - offset)] = m.group()[:-1].decode("latin-1")
                break
    return out


def linux_asserts():
    """(file, line) -> the function name, gathered from the Linux server's code."""
    strings = source_strings(LINUX)
    text = subprocess.run(["objdump", "-d", "--no-show-raw-insn", LINUX],
                          capture_output=True, text=True).stdout

    found = {}
    current = None
    last_file = None
    last_line = None
    for raw in text.splitlines():
        m = FUNC.match(raw)
        if m:
            current = m.group(2)
            last_file = last_line = None
            continue
        m = INSTR.match(raw)
        if not m:
            continue
        body = m.group(2)

        hit = STRING_ARG.search(body)
        if hit:
            name = strings.get(int(hit.group(1), 16))
            if name:
                last_file = name
            continue
        hit = LINE_IMM.search(body)
        if hit:
            last_line = int(hit.group(1), 16)
            continue
        if "callq" in body and DEBUG_CTOR in body:
            if last_file and last_line:
                found.setdefault((basename(last_file), last_line), current)
            last_file = last_line = None
    return found


# BF2_r.exe is a checked build: the same thing, but with three times as many
# checks, so there are more matches too. Switched on with BF2_PE.
WINDOWS = os.environ.get("BF2_PE", os.path.join(ROOT, "Game Files/BF2.exe"))
PUSH_IMM = re.compile(r"pushl\s+\$0x([0-9a-f]+)")


def pe_source_strings(path):
    """An address -> the source file path, for a PE image."""
    import struct

    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    sections_count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    first = pe + 24 + opt_size

    sections = []
    for i in range(sections_count):
        off = first + i * 40
        virtual, rva, raw_size, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
        sections.append((rva, raw_ptr, raw_size))

    out = {}
    for m in re.finditer(rb"[ -~]{4,}\.cpp\x00", data):
        at = m.start()
        for rva, raw_ptr, raw_size in sections:
            if raw_ptr <= at < raw_ptr + raw_size:
                out[image_base + rva + (at - raw_ptr)] = m.group()[:-1].decode("latin-1")
                break
    return out


def windows_asserts():
    """(file, line) -> the address of the check in BF2.exe."""
    strings = pe_source_strings(WINDOWS)
    text = subprocess.run(["objdump", "-d", "--no-show-raw-insn", WINDOWS],
                          capture_output=True, text=True).stdout

    out = {}
    pending = None       # (file, how many instructions ago it was seen)
    for raw in text.splitlines():
        m = INSTR.match(raw)
        if not m:
            continue
        at, body = int(m.group(1), 16), m.group(2)
        hit = PUSH_IMM.search(body)
        if not hit:
            if pending:
                pending = (pending[0], pending[1] + 1)
                if pending[1] > 12:
                    pending = None
            continue
        value = int(hit.group(1), 16)
        name = strings.get(value)
        if name:
            pending = (basename(name), 0)
            continue
        # The line number: a small number shortly after the file's address.
        if pending and 0 < value < 100000:
            out.setdefault((pending[0], value), at)
            pending = None
    return out


def basename(path):
    """The last part of a path. Windows paths come with backslashes, and
    `os.path.basename` does not cut them on macOS."""
    return re.split(r"[\\/]", path)[-1]


def as_identifier(name):
    """A name fit for Ghidra: no brackets, colons or spaces."""
    head = name.split("(")[0]
    head = head.replace("dice::hfe::", "").replace("::", "_")
    return re.sub(r"[^A-Za-z0-9_]", "_", head)


def print_source_map():
    """The address of a check -> the source file and line, with no Linux server.

    It makes sense for `BF2_r.exe` in particular: that is a build with the
    debug checks in it, and there are 3111 of them there against 335 in the
    ordinary client, out of 448 distinct source files. It does not give the
    function's name, but it does give which file the code is from — and that
    is often enough to find the function wanted: "reading a simple object's
    state" lies in `SimpleObjectNetworkable.cpp` and nowhere else.
    """
    windows = windows_asserts()
    print("# Debug checks in %s: address -> source file and line." % WINDOWS)
    print("# Taken by tools/transplant_symbols.py --source-map.")
    print("# Checks in total: %d, distinct files: %d"
          % (len(windows), len({name for name, _ in windows})))
    print()
    for (name, line), address in sorted(windows.items(), key=lambda kv: kv[1]):
        print("0x%08x  %s:%d" % (address, name, line))


def main():
    if "--source-map" in sys.argv:
        print_source_map()
        return
    table = linux_asserts()
    print("# Debug checks in the Linux server: file, line, function.")
    print("# The pair \"file + line\" is the same in BF2.exe, so the FUN_xxxxxx")
    print("# there can be named by it. Taken by tools/transplant_symbols.py.")
    windows = windows_asserts()
    matched = {key: (windows[key], table[key]) for key in table if key in windows}

    print("# Checks in the Linux server: %d, in BF2.exe: %d, matched: %d"
          % (len(table), len(windows), len(matched)))
    print()
    seen = {}
    for (name, line), (address, func) in sorted(matched.items(), key=lambda kv: kv[1][0]):
        seen.setdefault(func, address)
    # Demangling through c++filt: no need to read `_ZN4dice3hfe...` by hand.
    names = list(seen)
    pretty = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True,
                            text=True).stdout.splitlines()
    readable = dict(zip(names, pretty)) if len(pretty) == len(names) else {}

    if "--script" in sys.argv:
        # A script for Ghidra: rename everything at once, not in hundreds of calls.
        print("# Generated by tools/transplant_symbols.py --script")
        print("# To run: in Ghidra through the Script Manager, or headless:")
        print("#   analyzeHeadless <project> OpenBF2 -process BF2.exe \\")
        print("#     -postScript apply_symbols.py")
        print("from ghidra.program.model.symbol import SourceType")
        print("fm = currentProgram.getFunctionManager()")
        print("names = [")
        for func, address in sorted(seen.items(), key=lambda kv: kv[1]):
            print("    (0x%08x, %r)," % (address, as_identifier(readable.get(func, func))))
        print("]")
        print("done = 0")
        print("for address, name in names:")
        print("    at = currentProgram.getAddressFactory().getAddress(hex(address))")
        print("    fn = fm.getFunctionContaining(at)")
        print("    if fn is not None:")
        print("        fn.setName(name, SourceType.USER_DEFINED)")
        print("        done += 1")
        print("print('renamed %d of %d' % (done, len(names)))")
        return

    print("# the address of a check in BF2.exe -> the function from the Linux server")
    print("# (the address points inside the function, not at its start)")
    for func, address in sorted(seen.items(), key=lambda kv: kv[1]):
        print("0x%08x  %s" % (address, readable.get(func, func)))


if __name__ == "__main__":
    main()
