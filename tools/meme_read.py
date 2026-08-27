#!/usr/bin/env python3
"""Читає файл `MemeFile 2.0` за розкладкою, знятою з офіційної бібліотеки.

    tools/meme_read.py Ingame            # дерево
    tools/meme_read.py --check           # перевірити всі такі файли
    tools/meme_read.py Ingame --find BottomRight

Розкладка не вгадана — вона прочитана з `MemeDll.dll`, де є повні
символи C++ (див. docs/formats/hud-meme.md):

* файл: рядок версії, далі словник рядків, кожен із однобайтовою
  довжиною, до **порожнього** рядка (`IStream::streamStaticString`);
* корінь: `Object::loadNew` читає **лише двобайтовий номер класу** і
  одразу віддає слово самому класові;
* вкладений об'єкт: `Object::load` читає чотирибайтовий **розмір**,
  двобайтове ім'я об'єкта, двобайтове ім'я класу, а тоді поля. Розмір
  міряється від себе, і саме ним рушій пропускає незнайоме;
* у класовому потоці «рядок» — це номер у словнику, нуль означає порожньо
  (`ClassIStream::streamStaticString`);
* поля кожного класу перелічує його `onStream`, і починає він із
  батьківського — тому успадковані поля стоять першими.
"""
import argparse
import os
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import meme_types  # noqa: E402

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "Game Files", "mods", "bf2")
ARCHIVES = ("Menu_client.zip", "Common_client.zip")
MAGIC = b"MemeFile"

# Скільки байтів читає кожен метод потоку. Номери — зсуви в таблиці
# методів `IStream`, знятій з .rdata самої бібліотеки.
FIXED = {
    0x1c: 1,  # Ubyte
    0x20: 1,  # Sbyte
    0x24: 2,  # Ushort
    0x28: 2,  # Sshort
    0x2c: 4,  # Ulong
    0x30: 4,  # Slong
    0x34: 4,  # Float
    0x38: 1,  # Bool -> Ubyte
    0x3c: 4,  # Int
    0x48: 2,  # Wchar
    0x5c: 4,  # Index -> Int
}
# Ім'я ресурсу: однобайтова довжина, далі байти.
NAMED = {0x4c, 0x50, 0x54}
# Вкладений об'єкт.
OBJECT = {0x60, 0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c, 0x80, 0x84, 0x88}
LIST = 0x58


class Tables:
    """Поля кожного класу, разом із успадкованими."""

    def __init__(self):
        self.own = {}
        self.images = []
        for name in meme_types.LIBRARIES:
            path = os.path.join(MOD, name)
            if not os.path.exists(path):
                continue
            image = meme_types.Image(path)
            for klass, span in meme_types.classes(image).items():
                self.own[klass] = meme_types.fields(image, span[0], span[1])
            self.images.append(image)
        self._resolved = {}

    def fields(self, klass):
        if klass in self._resolved:
            return self._resolved[klass]
        out = []
        self._resolved[klass] = out  # захист від кільця
        # У файлі клас зветься повним іменем, а в бібліотеці — коротким.
        table = self.own.get(klass) or self.own.get(klass.split("::")[-1])
        if table is None:
            # Свого onStream клас не має — питаємо його таблицю методів,
            # чий саме дістався йому у спадок.
            short = klass.split("::")[-1]
            for image in self.images:
                parent, address = meme_types.inherited(image, short)
                if parent and parent != short:
                    out.extend(self.fields(parent))
                    break
                if address:
                    # Свій onStream є, просто не експортований — читаємо
                    # його за адресою з таблиці методів.
                    for name, slot in meme_types.fields(image, address, address + 0x200):
                        if name.startswith("@"):
                            out.extend(self.fields(name[1:]))
                        else:
                            out.append((name, slot))
                    break
            self._resolved[klass] = out
            return out
        for name, slot in table:
            if name.startswith("@"):
                out.extend(self.fields(name[1:]))
            else:
                out.append((name, slot))
        self._resolved[klass] = out
        return out


class Reader:
    def __init__(self, data, tables):
        self.data = data
        self.at = 0
        self.tables = tables
        self.words = []
        self.unknown = set()
        self.short = {}
        self.path = []
        # Межа поточного об'єкта: далі за неї поле не читається. Так само
        # робить рушій — розмір тут головніший за таблицю полів.
        self.limit = None

    def ubyte(self):
        value = self.data[self.at]
        self.at += 1
        return value

    def ushort(self):
        value = struct.unpack_from("<H", self.data, self.at)[0]
        self.at += 2
        return value

    def ulong(self):
        value = struct.unpack_from("<I", self.data, self.at)[0]
        self.at += 4
        return value

    def raw_string(self):
        length = self.ubyte()
        text = self.data[self.at:self.at + length]
        self.at += length
        return text.decode("latin-1")

    def word(self):
        """Рядок у класовому потоці — це номер у словнику."""
        index = self.ushort()
        return self.words[index] if 0 < index < len(self.words) else ""

    def header(self):
        version = self.raw_string()
        self.words = [""]
        while True:
            text = self.raw_string()
            if not text:
                break
            self.words.append(text)
        return version

    def value(self, slot):
        if slot in FIXED:
            raw = self.data[self.at:self.at + FIXED[slot]]
            self.at += FIXED[slot]
            if slot == 0x34:
                return round(struct.unpack("<f", raw)[0], 4)
            return int.from_bytes(raw, "little")
        if slot in NAMED:
            return self.raw_string()
        if slot == LIST:
            # Список не має лічильника: об'єкти йдуть підряд, а край дає
            # розмір самого власника списку. Це видно в байтах — одразу
            # після заголовка починається запис об'єкта, а не число.
            out = []
            while self.limit is not None and self.at + 8 <= self.limit:
                out.append(self.object())
            return out
        if slot in OBJECT:
            return self.object()
        raise ValueError("невідомий метод потоку %#x" % slot)

    def object(self):
        """Вкладений об'єкт: розмір, ім'я, клас, поля."""
        start = self.at
        size = self.ulong()
        name = self.word()
        klass = self.word()
        node = {"клас": klass, "ім'я": name, "поля": {}}
        if klass:
            outer, self.limit = self.limit, start + size
            node["поля"] = self.body(klass)
            self.limit = outer
            # Розмір дозволяє пропустити хвіст — і тим ховає неповну
            # таблицю полів. Тому звіряємо: скільки прочитали і скільки
            # мали. Розбіжність означає, що клас розібраний не до кінця.
            left = (start + size) - self.at
            if left:
                self.short[klass] = max(self.short.get(klass, 0), left)
        # Розмір міряється від свого ж поля — так рушій пропускає те,
        # чого не знає. Робимо так само: він тут головний.
        self.at = start + size
        return node if klass else None

    def body(self, klass):
        out = {}
        table = self.tables.fields(klass)
        if not table and klass:
            self.unknown.add(klass)
        for name, slot in table:
            if self.limit is not None and self.at >= self.limit:
                break
            self.path.append("%s.%s" % (klass.split("::")[-1], name))
            out[name] = self.value(slot)
            self.path.pop()
        return out

    def root(self):
        """Корінь читається інакше: лише номер класу, без розміру."""
        klass = self.word()
        return {"клас": klass, "ім'я": "", "поля": self.body(klass)}


def archives():
    for name in ARCHIVES:
        path = os.path.join(MOD, name)
        if os.path.exists(path):
            yield name, zipfile.ZipFile(path)


def find(entry):
    for name, archive in archives():
        for candidate in archive.namelist():
            if candidate.lower() == entry.lower() or candidate.lower().endswith(
                    "/" + entry.lower()):
                data = archive.read(candidate)
                if data[1:1 + len(MAGIC)] == MAGIC:
                    return name, candidate, data
    return None


def every():
    for name, archive in archives():
        for candidate in archive.namelist():
            info = archive.getinfo(candidate)
            if info.file_size < 16 or info.file_size > 1 << 20:
                continue
            data = archive.read(candidate)
            if data[1:1 + len(MAGIC)] == MAGIC:
                yield name, candidate, data


def show(node, depth, out, path=""):
    if node is None:
        return
    label = node["клас"] + (" «%s»" % node["ім'я"] if node["ім'я"] else "")
    plain = {k: v for k, v in node["поля"].items()
             if not isinstance(v, (dict, list)) and v not in ("", 0, 0.0)}
    out.append("  " * depth + label + ("  " + str(plain) if plain else ""))
    for key, value in node["поля"].items():
        if isinstance(value, dict):
            out.append("  " * (depth + 1) + key + ":")
            show(value, depth + 2, out)
        elif isinstance(value, list):
            out.append("  " * (depth + 1) + "%s (%d):" % (key, len(value)))
            for item in value:
                show(item, depth + 2, out)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("entry", nargs="?")
    parser.add_argument("--check", action="store_true", help="перевірити всі файли")
    parser.add_argument("--find", help="показати лише гілки з цим у назві")
    args = parser.parse_args()

    tables = Tables()

    if args.check or not args.entry:
        good = bad = 0
        for archive, entry, data in every():
            reader = Reader(data, tables)
            try:
                reader.header()
                reader.root()
                left = len(data) - reader.at
                mark = "ціло" if left == 0 else "лишилось %d" % left
                good += left == 0
                bad += left != 0
            except Exception as error:  # noqa: BLE001
                mark = "збій: %s" % error
                bad += 1
            print("%-22s %-24s %6d б  %s" % (archive, entry, len(data), mark))
            if reader.unknown:
                print("      класів без таблиці: %s" % ", ".join(sorted(reader.unknown)))
            if reader.short:
                for klass, left in sorted(reader.short.items(), key=lambda kv: -kv[1]):
                    print("      недочитано %-42s %d б" % (klass.split("::")[-1], left))
        print("\nрозібрано повністю: %d, з залишком чи збоєм: %d" % (good, bad))
        return 0 if bad == 0 else 1

    found = find(args.entry)
    if not found:
        print("не знайдено: %s" % args.entry, file=sys.stderr)
        return 1
    archive, entry, data = found
    reader = Reader(data, tables)
    version = reader.header()
    tree = reader.root()
    print("%s / %s: %s, слів %d, прочитано %d з %d" %
          (archive, entry, version, len(reader.words) - 1, reader.at, len(data)))
    lines = []
    show(tree, 0, lines)
    for line in lines:
        if not args.find or args.find.lower() in line.lower():
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
