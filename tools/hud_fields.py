#!/usr/bin/env python3
"""HUD variables -> object fields, and who writes those fields.

    tools/hud_fields.py             # the variable name, the field offset
    tools/hud_fields.py --writers   # also the places a constant is written

In the game a HUD variable is not a dictionary entry but **a field of an
object**: registration function 0x789480 calls registerVariable(name, &field)
one after another. The engine writes the field; the HUD reads it by that name.

So "who turns PlayerHealthShow on" is the question "who writes the byte at
offset 0x24b", and the answer is looked for by a byte scan, not by strings.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hud_states import Image  # noqa: E402

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Game Files", "BF2.exe")
# The registration is spread over several functions, so we scan the whole code
# section: the sign is a registerVariable call through [edx+0x10] on a HUD object.
REGISTER = 0x401000
REGISTER_END = 0x87f000
# registerVariable exists in several overloads: through the virtual method table
# and three direct calls (bool, float, string). Found by scanning: what stands
# after the string constructor in 3077 places.
CALL_REGISTER = bytes.fromhex("ff5210")   # call [edx+0x10]
DIRECT = (0x466240, 0x4664e0, 0x466630)


def fields(image):
    """(name, field offset) — every registerVariable call in the binary.

    The sign is `call dword ptr [edx+0x10]`; immediately before it, in a window
    of 48 bytes, lie `push <the name's address>` and `lea <reg>, [<base>+offset]`.
    We read backwards from the call rather than forwards: that way a linear
    disassembly does not lose the thread on data inside the code.
    """
    out = []
    seen = set()
    sites = []
    at = 0
    while True:
        at = image.data.find(CALL_REGISTER, at)
        if at < 0:
            break
        at += 3
        sites.append(at)
    for target in DIRECT:
        at = 0
        while True:
            at = image.data.find(b"\xe8", at)
            if at < 0:
                break
            at += 1
            if at + 4 > len(image.data):
                break
            here = image.va(at - 1)
            if here and here + 5 + struct.unpack_from("<i", image.data, at)[0] == target:
                sites.append(at + 4)
    for at in sorted(sites):
        window = image.data[max(0, at - 51):at]
        name = None
        offset = None
        for i in range(len(window) - 5):
            if window[i] == 0x68:
                try:
                    text = image.string(struct.unpack_from("<I", window, i + 1)[0])
                except (KeyError, ValueError, IndexError):
                    continue
                if text and text.isascii() and text[:1].isalpha() and len(text) > 3:
                    name = text
            # a lea with ebp as the base is a local string on the stack, not a field
            elif window[i] == 0x8D and (window[i + 1] & 7) not in (4, 5):
                mod = window[i + 1] & 0xC0
                if mod == 0x80:
                    offset = struct.unpack_from("<I", window, i + 2)[0]
                elif mod == 0x40:
                    offset = window[i + 2]
        if name is not None and offset is not None and name not in seen:
            seen.add(name)
            out.append((name, offset))
    return out


def writers(image, offset):
    """The places a field is written.

    Two cases: `mov byte/dword ptr [reg+offset], constant` and
    `mov byte ptr [reg+offset], reg8` — the second means the value is
    computed and there is no constant there.
    """
    found = []
    for base in range(8):
        if base == 4:
            continue  # esp — not our case
        for opcode, size in ((0xC6, 1), (0xC7, 4), (0x88, 0)):
            for reg in range(8) if opcode == 0x88 else (0,):
                head = (reg << 3) | base
                if offset < 0x80:
                    pattern = bytes([opcode, 0x40 | head, offset])
                else:
                    pattern = bytes([opcode, 0x80 | head]) + struct.pack("<I", offset)
                at = 0
                while True:
                    at = image.data.find(pattern, at)
                    if at < 0:
                        break
                    if opcode == 0x88:
                        found.append((image.va(at), None))
                    else:
                        value = image.data[at + len(pattern):at + len(pattern) + size]
                        found.append((image.va(at), int.from_bytes(value, "little")))
                    at += 1
    return sorted(found)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--writers", action="store_true")
    parser.add_argument("--only", help="show only the variables containing this string")
    args = parser.parse_args()
    image = Image(EXE)
    for name, offset in fields(image):
        if args.only and args.only.lower() not in name.lower():
            continue
        if not args.writers:
            print("%-40s 0x%x" % (name, offset))
            continue
        places = writers(image, offset)
        print("%-40s 0x%-5x %s" % (
            name, offset,
            ", ".join("0x%x=%s" % (va, "computed" if value is None else value)
                      for va, value in places) or "—"))


if __name__ == "__main__":
    main()
