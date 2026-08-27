#!/usr/bin/env python3
"""Поля кожного класу `dice::meme::*` — прямо з офіційної бібліотеки.

    tools/meme_types.py                 # усі класи, по порядку полів
    tools/meme_types.py TransformNode   # один клас
    tools/meme_types.py --slots         # які вважаємо типами полів

`MemeDll.dll` і `MemeBf.dll` лежать у теці мода поруч із самим редактором
(`MemeEdit.exe`) і, на відміну від гри, **експортують повні символи C++**.
Кожен клас має `onStream`, і кожне поле там передається окремим методом
потоку — `streamFloat`, `streamInt`, `streamNode`, ... — якому **другим
аргументом іде назва поля рядком**.

Тобто розкладку файлу не треба вгадувати: вона записана в бібліотеці
іменами. Ми лише читаємо порядок викликів.

Методи потоку викликаються віртуально, тож замість імені ми бачимо зсув
у таблиці. Зсув сталий для методу, тож він і є позначкою типу; людські
назви для відомих зсувів — у SLOTS, решта лишається числом.
"""
import argparse
import os
import re
import struct
import subprocess
import sys

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "Game Files", "mods", "bf2")
LIBRARIES = ("MemeDll.dll", "MemeBf.dll")

# Зсув у таблиці методів -> що воно читає. Заповнюється в міру того, як
# зсув вдається звірити з даними; невідомі лишаються числом.
SLOTS = {
    0x34: "float",       # Alpha, Width, Red
    0x38: "bool",        # Border or not, Focus
    0x3c: "int",         # Col, Frames, Index
    0x48: "string",      # Unicode
    0x4c: "picture",     # Picture, Fill picture
    0x50: "font",        # Font handle
    0x54: "sound",       # Select sound
    0x58: "list",        # Action list, Data list
    0x5c: "index",       # Button type, Source blend func — перелічення
    0x64: "event",
    0x68: "action",
    0x6c: "data",        # найчастіше: значення беруться з окремих вузлів-даних
    0x70: "effect",
    0x74: "function",
    0x78: "object",      # Path node, Destination node
    0x7c: "style",
    0x80: "tree",
    0x84: "child",       # Split node, Transformed node
    0x88: "next",        # Next node
}


class Image:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        count = struct.unpack_from("<H", d, pe + 6)[0]
        optional = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.sections = []
        table = pe + 24 + optional
        for i in range(count):
            entry = table + i * 40
            name = d[entry:entry + 8].rstrip(b"\0").decode("latin-1")
            vsize, rva, rsize, raw = struct.unpack_from("<IIII", d, entry + 8)
            self.sections.append((name, rva, max(vsize, rsize), raw))
        self.export_rva = struct.unpack_from("<I", d, pe + 24 + 96)[0]

    def offset(self, rva):
        for _, start, size, raw in self.sections:
            if start <= rva < start + size:
                return raw + (rva - start)
        return None

    def follow(self, address):
        """Експорти тут ведуть у таблицю переходів, а не в саму функцію."""
        at = self.offset(address - self.base)
        if at is not None and self.data[at] == 0xE9:
            return address + 5 + struct.unpack_from("<i", self.data, at + 1)[0]
        return address

    def string(self, rva, limit=128):
        at = self.offset(rva)
        if at is None:
            return None
        end = self.data.find(b"\0", at, at + limit)
        if end < 0:
            return None
        text = self.data[at:end]
        if not text or not all(32 <= c < 127 for c in text):
            return None
        return text.decode("latin-1")

    def exports(self):
        """Ім'я -> адреса в пам'яті."""
        d, at = self.data, self.offset(self.export_rva)
        if at is None:
            return {}
        names_count = struct.unpack_from("<I", d, at + 24)[0]
        functions, names, ordinals = struct.unpack_from("<III", d, at + 28)
        out = {}
        for i in range(names_count):
            name_rva = struct.unpack_from("<I", d, self.offset(names) + i * 4)[0]
            name = self.string(name_rva, 512)
            index = struct.unpack_from("<H", d, self.offset(ordinals) + i * 2)[0]
            code = struct.unpack_from("<I", d, self.offset(functions) + index * 4)[0]
            if name:
                out[name] = self.base + code
        return out


CALL = re.compile(r"calll\s+\*(0x[0-9a-f]+)\(%\w+\)")
PUSH = re.compile(r"pushl\s+\$(0x[0-9a-f]+)")


def fields(image, address, end=None):
    """Поля в порядку появи: (назва, зсув методу потоку).

    Межу функції беремо з наступного експорту, а не «до першого retl»:
    onStream часто спершу віддає роботу батьківському класу і повертається
    з середини, а поля йдуть далі.
    """
    if end is None or end <= address:
        end = address + 0x600
    text = subprocess.run(
        ["objdump", "-d", "--no-show-raw-insn",
         "--start-address=%#x" % address, "--stop-address=%#x" % end,
         image.path], capture_output=True, text=True).stdout

    out, pending = [], []
    for line in text.splitlines():
        push = PUSH.search(line)
        if push:
            name = image.string(int(push.group(1), 16) - image.base)
            # Назви полів — короткі слова з великої літери; решта сталих
            # (розміри, прапорці) нас тут не цікавить.
            if name and re.fullmatch(r"[A-Za-z][A-Za-z0-9 _]{0,30}", name):
                pending.append(name)
            continue
        call = CALL.search(line)
        if call and pending:
            out.append((pending[-1], int(call.group(1), 16)))
            pending = []
    return out


def classes(image):
    """Клас -> (початок onStream, початок наступної функції)."""
    starts = sorted({image.follow(a) for a in image.exports().values()})
    out = {}
    for name, address in image.exports().items():
        match = re.match(r"\?onStream@([A-Za-z0-9]+)@meme@dice@@", name)
        if not match:
            continue
        start = image.follow(address)
        after = [s for s in starts if s > start]
        out[match.group(1)] = (start, after[0] if after else start + 0x600)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("klass", nargs="?", help="показати один клас")
    parser.add_argument("--slots", action="store_true", help="які зсуви трапляються")
    parser.add_argument("--emit", help="записати повний перелік у файл")
    args = parser.parse_args()

    images = []
    for name in LIBRARIES:
        path = os.path.join(MOD, name)
        if os.path.exists(path):
            images.append(Image(path))
    if not images:
        print("немає %s у %s" % (" / ".join(LIBRARIES), MOD), file=sys.stderr)
        return 1

    found = {}
    for image in images:
        for name, span in classes(image).items():
            found[name] = (image, span)

    if args.slots:
        counts = {}
        for name, (image, span) in sorted(found.items()):
            for _, slot in fields(image, span[0], span[1]):
                counts[slot] = counts.get(slot, 0) + 1
        for slot, count in sorted(counts.items()):
            print("  %#-6x %-16s %d полів" % (slot, SLOTS.get(slot, "?"), count))
        return 0

    if args.emit:
        with open(args.emit, "w", encoding="utf-8") as out:
            out.write("# Класи `dice::meme::*` та їхні поля\n\n")
            out.write("Створено `tools/meme_types.py --emit`. Руками не правити.\n\n")
            out.write("Порядок полів — це порядок у файлі: `onStream` викликає\n")
            out.write("методи потоку один за одним, і кожному передає назву поля\n")
            out.write("рядком. Тип — за тим, який саме метод викликано.\n\n")
            for name in sorted(found):
                image, span = found[name]
                rows = fields(image, span[0], span[1])
                out.write("## %s (%s)\n\n" % (name, os.path.basename(image.path)))
                if not rows:
                    out.write("Власних полів немає.\n\n")
                    continue
                for field, slot in rows:
                    out.write("* `%s` — %s\n" % (field, SLOTS.get(slot, "?%#x" % slot)))
                out.write("\n")
        print("записано %d класів у %s" % (len(found), args.emit))
        return 0

    wanted = sorted(found) if not args.klass else [
        n for n in found if n.lower() == args.klass.lower()]
    if not wanted:
        print("такого класу немає; є %d" % len(found), file=sys.stderr)
        return 1

    for name in wanted:
        image, span = found[name]
        rows = fields(image, span[0], span[1])
        print("%s (%s, %#x): полів %d" %
              (name, os.path.basename(image.path), span[0], len(rows)))
        for field, slot in rows:
            print("    %-28s %s" % (field, SLOTS.get(slot, "зсув %#x" % slot)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
