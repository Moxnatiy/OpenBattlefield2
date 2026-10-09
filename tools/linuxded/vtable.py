#!/usr/bin/env python3
"""A class's vtable in the Linux server, slot by slot, with the names.

    tools/linuxded/vtable.py <Class> [--msvc]
    tools/linuxded/vtable.py dice::hfe::world::WeaponHudComp --msvc

The client calls almost everything through vtables — `(*obj + 0x50)()` — and
`BF2.exe` has no names. The server has them: its vtable (`_ZTV...`) lists the
same virtual functions in the same declaration order. This prints each slot's
offset and the function's demangled name.

The server is 64-bit Itanium ABI and the client 32-bit MSVC, so the offsets do
not carry over as they are: `--msvc` prints the offset the slot has in the
client — 4 bytes a slot instead of 8, and the Itanium pair of destructors
(complete and deleting) counted as MSVC's one. That mapping holds for a class
with one base chain; a class with several bases has a vtable per base, and
this prints the primary one only.
"""
import argparse
import subprocess
import sys

from elftools.elf.elffile import ELFFile

SERVER = "Game Files/OtherFiles/linuxded-full/bin/amd-64/bf2"


def demangle(names):
    out = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True)
    return out.stdout.splitlines()


def mangle_class(name):
    return "".join(f"{len(part)}{part}" for part in name.split("::"))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("cls")
    parser.add_argument("--msvc", action="store_true")
    parser.add_argument("--elf", default=SERVER)
    args = parser.parse_args()

    elf = ELFFile(open(args.elf, "rb"))
    symtab = elf.get_section_by_name(".symtab") or elf.get_section_by_name(".dynsym")
    parts = args.cls.split("::")
    wanted = "_ZTV" + ("N" + mangle_class(args.cls) + "E" if len(parts) > 1 else mangle_class(args.cls))
    vtable = None
    by_address = {}
    for sym in symtab.iter_symbols():
        if sym.name == wanted:
            vtable = sym
        if sym["st_info"]["type"] == "STT_FUNC" and sym["st_value"]:
            by_address.setdefault(sym["st_value"], sym.name)
    if vtable is None:
        sys.exit(f"no {wanted} in {args.elf}")

    address, size = vtable["st_value"], vtable["st_size"]
    data = None
    for section in elf.iter_sections():
        start = section["sh_addr"]
        if start <= address < start + section["sh_size"] and section["sh_type"] != "SHT_NOBITS":
            data = section.data()[address - start:address - start + size]
            break
    if data is None:
        sys.exit("vtable lies in no section with bytes")
    entries = [int.from_bytes(data[i:i + 8], "little") for i in range(0, len(data), 8)]
    # Itanium: offset-to-top, typeinfo, then the functions — once for the
    # primary base and once more for every further base (a class with several
    # bases, `GenericFireArm` and its `IWeaponObject`). A section starts where
    # an entry is followed by a typeinfo; its functions are mostly thunks.
    typeinfos = set()
    for sym in symtab.iter_symbols():
        if sym.name.startswith("_ZTI"):
            typeinfos.add(sym["st_value"])
    starts = [i for i in range(len(entries) - 1) if entries[i + 1] in typeinfos]
    starts.append(len(entries))
    typeinfo_names = {sym["st_value"]: sym.name for sym in symtab.iter_symbols()
                      if sym.name.startswith("_ZTI")}
    for section, (start, stop) in enumerate(zip(starts, starts[1:])):
        top = entries[start]
        top = top - (1 << 64) if top >= 1 << 63 else top
        base = demangle([typeinfo_names[entries[start + 1]][4:].join(["_Z", ""])])[0]
        print(f"== vtable {section}: offset-to-top {top}, typeinfo {base}")
        functions = entries[start + 2:stop]
        names = demangle([by_address.get(f, f"0x{f:x}") for f in functions])
        msvc = 0
        seen_destructor = False
        for i, (target, name) in enumerate(zip(functions, names)):
            if target == 0:
                break
            line = f"+0x{i * 8:03x}"
            if args.msvc:
                is_destructor = "::~" in name
                if is_destructor and seen_destructor:
                    print(f"{line}  (msvc —)   {name}")
                    continue
                seen_destructor = seen_destructor or is_destructor
                line += f"  msvc +0x{msvc * 4:03x}"
                msvc += 1
            print(f"{line}  {name}")


if __name__ == "__main__":
    main()
