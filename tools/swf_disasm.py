#!/usr/bin/env python3
"""An ActionScript 2 disassembler for the menu movies.

`tools/swf_read.py` shows **what** is in a movie: tags, symbols, a list of
the strings from the byte code. That is enough to find the place and too
little to understand the behaviour: the list has neither order, nor branches,
nor function bounds.

Here is the code proper. We print one instruction per line, the bodies of
`DefineFunction`/`DefineFunction2` indented rather than as "(body 412)", and
next to a jump we write where it leads.

    tools/swf_disasm.py mainMenu.swf --grep setValue
    tools/swf_disasm.py mainMenu.swf --block 27 --tag 59
    tools/swf_disasm.py mainMenu.swf --grep 'setValue' --context 30

We read by the SWF 9 specification (the "ActionScript Byte Code" chapter)
published by Adobe; no Ruffle or gameswf code was carried over here.
"""
import argparse
import importlib.util
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("swf_read", os.path.join(_HERE, "swf_read.py"))
swf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(swf)

# The action codes. Those without operands (< 0x80) and those with a body.
OPS = {
    0x04: "NextFrame", 0x05: "PrevFrame", 0x06: "Play", 0x07: "Stop",
    0x08: "ToggleQuality", 0x09: "StopSounds",
    0x0A: "Add", 0x0B: "Subtract", 0x0C: "Multiply", 0x0D: "Divide",
    0x0E: "Equals", 0x0F: "Less", 0x10: "And", 0x11: "Or", 0x12: "Not",
    0x13: "StringEquals", 0x14: "StringLength", 0x15: "StringExtract",
    0x17: "Pop", 0x18: "ToInteger",
    0x1C: "GetVariable", 0x1D: "SetVariable",
    0x20: "SetTarget2", 0x21: "StringAdd", 0x22: "GetProperty",
    0x23: "SetProperty", 0x24: "CloneSprite", 0x25: "RemoveSprite",
    0x26: "Trace", 0x27: "StartDrag", 0x28: "EndDrag", 0x29: "StringLess",
    0x2A: "Throw", 0x2B: "CastOp", 0x2C: "ImplementsOp",
    0x30: "RandomNumber", 0x31: "MBStringLength", 0x32: "CharToAscii",
    0x33: "AsciiToChar", 0x34: "GetTime", 0x35: "MBStringExtract",
    0x36: "MBCharToAscii", 0x37: "MBAsciiToChar",
    0x3A: "Delete", 0x3B: "Delete2", 0x3C: "DefineLocal",
    0x3D: "CallFunction", 0x3E: "Return", 0x3F: "Modulo",
    0x40: "NewObject", 0x41: "DefineLocal2", 0x42: "InitArray",
    0x43: "InitObject", 0x44: "TypeOf", 0x45: "TargetPath",
    0x46: "Enumerate", 0x47: "Add2", 0x48: "Less2", 0x49: "Equals2",
    0x4A: "ToNumber", 0x4B: "ToString", 0x4C: "PushDuplicate",
    0x4D: "StackSwap", 0x4E: "GetMember", 0x4F: "SetMember",
    0x50: "Increment", 0x51: "Decrement", 0x52: "CallMethod",
    0x53: "NewMethod", 0x54: "InstanceOf", 0x55: "Enumerate2",
    0x60: "BitAnd", 0x61: "BitOr", 0x62: "BitXor", 0x63: "BitLShift",
    0x64: "BitRShift", 0x65: "BitURShift", 0x66: "StrictEquals",
    0x67: "Greater", 0x68: "StringGreater", 0x69: "Extends",
    0x81: "GotoFrame", 0x83: "GetURL", 0x87: "StoreRegister",
    0x88: "ConstantPool", 0x8A: "WaitForFrame", 0x8B: "SetTarget",
    0x8C: "GoToLabel", 0x8D: "WaitForFrame2",
    0x8E: "DefineFunction2", 0x8F: "Try",
    0x94: "With", 0x96: "Push", 0x99: "Jump", 0x9A: "GetURL2",
    0x9B: "DefineFunction", 0x9D: "If", 0x9E: "Call",
    0x9F: "GotoFrame2",
}

# The named registers of `DefineFunction2`: the engine puts them into 1..N
# itself, in the order the flags give (SWF 9, "DefineFunction2").
PRELOAD = [
    (0x01, "this"), (0x04, "arguments"), (0x10, "super"),
    (0x40, "_root"), (0x80, "_parent"), (0x100, "_global"),
]


def _push_values(reader, end, pool):
    """The operands of `ActionPush` -> a list of strings as seen in the code."""
    out = []
    while reader.at < end:
        kind = reader.u8()
        if kind == 0:
            out.append('"%s"' % reader.text())
        elif kind == 1:
            out.append("%g" % reader.f32())
        elif kind == 2:
            out.append("null")
        elif kind == 3:
            out.append("undefined")
        elif kind == 4:
            out.append("reg%d" % reader.u8())
        elif kind == 5:
            out.append("true" if reader.u8() else "false")
        elif kind == 6:
            out.append("%g" % reader.f64())
        elif kind == 7:
            out.append("%d" % reader.u32())
        elif kind in (8, 9):
            index = reader.u8() if kind == 8 else reader.u16()
            out.append('"%s"' % (pool[index] if index < len(pool) else "?%d" % index))
        else:
            out.append("?type%d" % kind)
            break
    return out


def disasm(code, pool=None, depth=0, base=0):
    """Byte code -> a list of (address, depth, action, note).

    `pool` is shared with the nested bodies: in SWF the pool is declared once
    per block, and the functions inside it make use of it.
    """
    pool = [] if pool is None else pool
    reader = swf.Reader(code)
    out = []
    while reader.more():
        at = reader.at
        op = reader.u8()
        if op == 0:
            out.append((base + at, depth, "End", ""))
            break
        name = OPS.get(op, "?%02x" % op)
        if op < 0x80:
            out.append((base + at, depth, name, ""))
            continue
        length = reader.u16()
        end = reader.at + length
        note = ""
        body = None

        if op == 0x88:  # ConstantPool
            count = reader.u16()
            pool.clear()
            while reader.at < end and len(pool) < count:
                pool.append(reader.text())
            note = "%d strings" % len(pool)
        elif op == 0x96:  # Push
            note = ", ".join(_push_values(reader, end, pool))
        elif op in (0x99, 0x9D):  # Jump / If
            offset = reader.u16()
            if offset >= 0x8000:
                offset -= 0x10000
            note = "-> %d" % (base + end + offset)
        elif op == 0x87:  # StoreRegister
            note = "reg%d" % reader.u8()
        elif op == 0x94:  # With
            size = reader.u16()
            body = code[end:end + size]
            note = "(%d bytes)" % size
        elif op == 0x8C:  # GoToLabel
            note = '"%s"' % reader.text()
        elif op == 0x81:  # GotoFrame
            note = "frame %d" % reader.u16()
        elif op == 0x83:  # GetURL
            note = '"%s" -> "%s"' % (reader.text(), reader.text())
        elif op == 0x9B:  # DefineFunction
            fname = reader.text()
            count = reader.u16()
            params = [reader.text() for _ in range(count)]
            size = reader.u16()
            body = code[end:end + size]
            note = "%s(%s)" % (fname or "<anonymous>", ", ".join(params))
        elif op == 0x8E:  # DefineFunction2
            fname = reader.text()
            count = reader.u16()
            reader.u8()  # how many registers — we do not need it
            flags = reader.u16()
            params = []
            for _ in range(count):
                register = reader.u8()
                pname = reader.text()
                params.append("%s=reg%d" % (pname, register) if register else pname)
            size = reader.u16()
            body = code[end:end + size]
            preload = [word for bit, word in PRELOAD if flags & bit]
            note = "%s(%s)" % (fname or "<anonymous>", ", ".join(params))
            if preload:
                note += "  [reg: %s]" % ", ".join(preload)

        out.append((base + at, depth, name, note))
        reader.at = end
        if body is not None:
            out.extend(disasm(body, pool, depth + 1, base + end))
            reader.at = end + len(body)
    return out


def render(rows):
    for at, depth, name, note in rows:
        print("%6d %s%-16s %s" % (at, "  " * depth, name, note))


def place_actions(chunk):
    """`PlaceObject2` -> (the instance name, the handlers' byte code).

    The parameters of Flash MX components lie **here**, not in `DoAction`: the
    authoring tool folds them into `onClipEvent(construct)`, that is into the
    ClipActions of the very tag that puts the component on the stage. Without
    this pass neither `listId` nor the column names are visible — and that is
    exactly where the name of the `localProfiles` list got lost.

    The layout is SWF 9, "PlaceObject2" and "CLIPACTIONS".
    """
    reader = swf.Reader(chunk)
    flags = reader.u8()
    reader.u16()  # depth
    if flags & 0x02:  # HasCharacter
        reader.u16()
    if flags & 0x04:  # HasMatrix
        _skip_matrix(reader)
    if flags & 0x08:  # HasColorTransform
        _skip_cxform(reader)
    if flags & 0x10:  # HasRatio
        reader.u16()
    name = reader.text() if flags & 0x20 else ""
    if flags & 0x40:  # HasClipDepth
        reader.u16()
    if not flags & 0x80:  # HasClipActions
        return name, []

    reader.u16()  # reserved
    reader.u32()  # all the events together
    out = []
    while reader.more():
        events = reader.u32()
        if events == 0:
            break
        size = reader.u32()
        if size == 0 or reader.at + size > len(reader.d):
            break
        out.append((events, reader.d[reader.at:reader.at + size]))
        reader.at += size
    return name, out


def _skip_matrix(reader):
    """MATRIX is a bit field, so we read in bits and align back onto a byte."""
    bits = _Bits(reader)
    if bits.take(1):
        width = bits.take(5)
        bits.take(width * 2)
    if bits.take(1):
        width = bits.take(5)
        bits.take(width * 2)
    width = bits.take(5)
    bits.take(width * 2)
    bits.align()


def _skip_cxform(reader):
    bits = _Bits(reader)
    has_add = bits.take(1)
    has_mul = bits.take(1)
    width = bits.take(4)
    if has_mul:
        bits.take(width * 4)
    if has_add:
        bits.take(width * 4)
    bits.align()


class _Bits:
    """Reading in bits on top of the byte reader."""

    def __init__(self, reader):
        self.reader = reader
        self.byte = 0
        self.left = 0

    def take(self, count):
        value = 0
        for _ in range(count):
            if self.left == 0:
                self.byte = self.reader.u8()
                self.left = 8
            value = (value << 1) | ((self.byte >> (self.left - 1)) & 1)
            self.left -= 1
        return value

    def align(self):
        self.left = 0


def blocks(path, tag):
    """The list of byte-code blocks in a movie.

    12 = DoAction, 59 = DoInitAction, 26 = the handlers on an instance
    (`PlaceObject2`), which is where the component parameters lie.
    """
    _sig, _ver, _len, body = swf.unpack(path)
    items = swf.tags(body)
    if tag == 26:
        index = 0
        for chunk in swf.walk(items, 26):
            name, handlers = place_actions(chunk)
            for _events, code in handlers:
                index += 1
                yield "%d %s" % (index, name), code
        return
    for index, chunk in enumerate(swf.walk(items, tag), 1):
        # In DoInitAction the first two bytes are the symbol number.
        yield index, (chunk[2:] if tag == 59 else chunk)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("swf")
    parser.add_argument("--tag", type=int, default=59, help="12 DoAction, 59 DoInitAction")
    parser.add_argument("--block", type=int, help="only this block (numbered from 1)")
    parser.add_argument("--grep", help="print the surroundings of lines with this text")
    parser.add_argument("--context", type=int, default=12, help="how many lines around")
    parser.add_argument("--list", action="store_true", help="the blocks and their sizes")
    args = parser.parse_args()

    for index, code in blocks(args.swf, args.tag):
        if args.block is not None and index != args.block:
            continue
        if args.list:
            print("block %3d: %6d bytes" % (index, len(code)))
            continue
        rows = disasm(code)
        if args.grep is None:
            print("=== block %d (tag %d) ===" % (index, args.tag))
            render(rows)
            continue
        for i, row in enumerate(rows):
            if args.grep in row[3] or args.grep in row[2]:
                print("=== block %d, line %d ===" % (index, i))
                render(rows[max(0, i - args.context):i + args.context])
    return 0


if __name__ == "__main__":
    sys.exit(main())
