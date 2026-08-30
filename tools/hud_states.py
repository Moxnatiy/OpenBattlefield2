#!/usr/bin/env python3
"""Таблиця станів HUD — прочитана з BF2.exe, а не з рук.

    tools/hud_states.py            # таблиця станів у markdown
    tools/hud_states.py --cpp      # той самий перелік як C++

Дві функції рушія перемикають стан HUD через таблиці переходів на 32
позиції: 0x7862ce (таблиця 0x786f88) і 0x786751 (таблиця 0x787008).
Кожен обробник — ланцюжок однакових блоків по 25 байтів:

    8B 1E              mov  ebx, [esi]        ; vtable
    6A vv              push значення (0 або 1)
    83 EC 1C           sub  esp, 0x1c         ; місце під std::string
    8B CC              mov  ecx, esp
    68 <addr>          push адреса імені змінної
    FF 15 6C F4 87 00  call std::string(char*)
    8B CE              mov  ecx, esi
    FF 53 0C           call [ebx+0xc]         ; setVariable(ім'я, значення)

Обробники стоять один за одним і провалюються далі, тож читати треба від
входу стану до першої інструкції, що не є цим блоком.
"""
import argparse
import os
import struct
import sys

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Game Files", "BF2.exe")
TABLE = 0x787008          # таблиця переходів другої функції
SLOTS = 32
END = 0x786f59  # спільний вихід: далі вже не наші блоки



class Image:
    """PE у пам'яті: переведення віртуальної адреси у зсув у файлі."""

    def __init__(self, path):
        self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3c)[0]
        sections = struct.unpack_from("<H", self.data, pe + 6)[0]
        opt = struct.unpack_from("<H", self.data, pe + 20)[0]
        self.base = struct.unpack_from("<I", self.data, pe + 24 + 28)[0]
        self.sections = []
        at = pe + 24 + opt
        for _ in range(sections):
            name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", self.data, at)
            self.sections.append((va, vsize, raw, rsize))
            at += 40

    def offset(self, va):
        rva = va - self.base
        for sva, vsize, raw, rsize in self.sections:
            if sva <= rva < sva + max(vsize, rsize):
                return raw + (rva - sva)
        raise KeyError(hex(va))

    def read(self, va, size):
        at = self.offset(va)
        return self.data[at:at + size]

    def va(self, offset):
        """Зворотне до offset: зсув у файлі -> віртуальна адреса."""
        for sva, vsize, raw, rsize in self.sections:
            if raw <= offset < raw + rsize:
                return self.base + sva + (offset - raw)
        return 0

    def string(self, va):
        at = self.offset(va)
        end = self.data.index(b"\0", at)
        return self.data[at:end].decode("ascii", "replace")


# Крихітний лінійний декодер: обробники — це рівний ланцюжок із семи
# видів інструкцій, і нам треба лише вони. Усе інше означає кінець.
def handler(image, va, limit=8192):
    """Пари (змінна, значення) від входу обробника до спільного виходу."""
    out = []
    value = None
    name = None
    for _ in range(limit):
        if va == END:
            break
        code = image.read(va, 6)
        if code[0:2] in (b"\x8b\x1e", b"\x8b\x3e", b"\x8b\xcc", b"\x8b\xce"):
            va += 2                       # mov ebx/edi,[esi] | mov ecx,esp/esi
        elif code[0] == 0x6A:
            value = code[1]               # push значення
            va += 2
        elif code[0:3] == b"\x83\xec\x1c":
            va += 3                       # sub esp,0x1c — місце під рядок
        elif code[0] == 0x68:
            name = image.string(struct.unpack_from("<I", code, 1)[0])
            va += 5                       # push адреса імені
        elif code[0:2] == b"\xff\x15":
            va += 6                       # call std::string(char*)
        elif code[0:3] in (b"\xff\x53\x0c", b"\xff\x57\x0c"):
            if name is not None and value is not None:
                out.append((name, value))  # setVariable(ім'я, значення)
            name = value = None
            va += 3
        elif code[0] == 0xE9:
            va += 5 + struct.unpack_from("<i", code, 1)[0]  # jmp у спільний хвіст
        elif code[0] == 0xEB:
            va += 2 + struct.unpack_from("<b", code, 1)[0]
        else:
            # Усередині обробника трапляються й інші виклики (звук, запит
            # до гравця). Перестрибуємо їх до наступного блоку — але лише
            # в межах самого обробника: гілки тут не провалюються одна в
            # одну, кожна закінчується стрибком у спільний хвіст.
            block = image.read(va, 160)
            step = -1
            for i in range(1, len(block) - 5):
                if block[i] == 0x6A and block[i + 2:i + 5] == b"\x83\xec\x1c":
                    step = i
                    break
            if step < 0:
                break
            va += step
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp", action="store_true")
    args = parser.parse_args()
    if not os.path.exists(EXE):
        sys.exit("BF2.exe не знайдено: " + EXE)
    image = Image(EXE)
    table = struct.unpack_from("<%dI" % SLOTS, image.read(TABLE, SLOTS * 4))
    empty = max(set(table), key=table.count)  # спільний порожній обробник

    states = []
    for state, entry in enumerate(table):
        states.append((state, entry, handler(image, entry)))

    if args.cpp:
        print("// Створено tools/hud_states.py з BF2.exe, таблиця 0x%x." % TABLE)
        print("static const HudState kHudStates[] = {")
        for state, entry, pairs in states:
            if entry == empty:
                continue
            print("    // стан %d, обробник 0x%x" % (state, entry))
            print("    {%d," % state)
            print("     {%s}}," % ", ".join(
                '{"%s", %d}' % (name, value) for name, value in pairs))
        print("};")
        return

    print("| стан | обробник | вмикає | гасить |")
    print("|---|---|---|---|")
    for state, entry, pairs in states:
        if entry == empty:
            print("| %d | 0x%x | *(порожній)* | |" % (state, entry))
            continue
        on = [n for n, v in pairs if v]
        off = [n for n, v in pairs if not v]
        print("| %d | 0x%x | %s | %d: %s |" %
              (state, entry, ", ".join("`%s`" % n for n in on) or "—",
               len(off), ", ".join("`%s`" % n for n in off) or "—"))


if __name__ == "__main__":
    main()
