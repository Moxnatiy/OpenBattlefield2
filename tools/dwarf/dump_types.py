#!/usr/bin/env python3
"""Витягує з DWARF Linux-сервера BF2 опис класів: поля, зміщення, типи.

Це специфікація, а не код: за нею ми пишемо власну реалізацію (clean-room).
Використання:
    python tools/dwarf/dump_types.py <binary> <ім'я класу> [ще класи...]
    python tools/dwarf/dump_types.py <binary> --list <підрядок>
"""
import sys
from elftools.elf.elffile import ELFFile

def type_name(die, depth=0):
    if die is None or depth > 8:
        return "?"
    tag = die.tag
    if tag == "DW_TAG_pointer_type":
        return type_name(ref(die), depth + 1) + "*"
    if tag == "DW_TAG_reference_type":
        return type_name(ref(die), depth + 1) + "&"
    if tag == "DW_TAG_const_type":
        return "const " + type_name(ref(die), depth + 1)
    if tag == "DW_TAG_array_type":
        return type_name(ref(die), depth + 1) + "[]"
    name = die.attributes.get("DW_AT_name")
    if name is not None:
        return name.value.decode("utf-8", "replace")
    return tag.replace("DW_TAG_", "")

def ref(die):
    at = die.attributes.get("DW_AT_type")
    if at is None:
        return None
    try:
        return die.get_DIE_from_attribute("DW_AT_type")
    except Exception:
        return None

def name_of(die):
    at = die.attributes.get("DW_AT_name")
    return at.value.decode("utf-8", "replace") if at else ""

def describe(die, out):
    size = die.attributes.get("DW_AT_byte_size")
    out.append("class %s  (розмір %s)" % (name_of(die), size.value if size else "?"))
    for child in die.iter_children():
        if child.tag == "DW_TAG_inheritance":
            out.append("  : %s" % type_name(ref(child)))
        elif child.tag == "DW_TAG_member":
            off = child.attributes.get("DW_AT_data_member_location")
            offset = "?"
            if off is not None:
                value = off.value
                if isinstance(value, list):
                    offset = value[1] if len(value) > 1 else "?"
                else:
                    offset = value
            out.append("  +%-5s %-40s %s" % (offset, type_name(ref(child)), name_of(child)))
        elif child.tag == "DW_TAG_subprogram":
            params = []
            for p in child.iter_children():
                if p.tag == "DW_TAG_formal_parameter":
                    params.append("%s %s" % (type_name(ref(p)), name_of(p)))
            out.append("  fn  %s %s(%s)" % (type_name(ref(child)), name_of(child),
                                            ", ".join(params)))

def main():
    path, args = sys.argv[1], sys.argv[2:]
    listing = args[0] == "--list" if args else False
    wanted = set(args[1:]) if listing else set(args)
    seen = set()
    out = []
    with open(path, "rb") as handle:
        elf = ELFFile(handle)
        dwarf = elf.get_dwarf_info()
        for unit in dwarf.iter_CUs():
            for die in unit.iter_DIEs():
                if die.tag not in ("DW_TAG_structure_type", "DW_TAG_class_type"):
                    continue
                name = name_of(die)
                if not name:
                    continue
                if listing:
                    if any(w.lower() in name.lower() for w in wanted) and name not in seen:
                        seen.add(name)
                        out.append(name)
                elif name in wanted and name not in seen:
                    seen.add(name)
                    describe(die, out)
                    out.append("")
    print("\n".join(out))

main()
