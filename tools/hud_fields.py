#!/usr/bin/env python3
"""Змінні HUD -> поля об'єкта, і хто ці поля пише.

    tools/hud_fields.py             # ім'я змінної, зсув поля
    tools/hud_fields.py --writers   # ще й місця, де поле записують сталою

У грі змінна HUD — це не запис у словнику, а **поле об'єкта**: функція
реєстрації 0x789480 підряд викликає registerVariable(ім'я, &this->поле).
Далі рушій пише в саме поле, а HUD читає його через ту саму назву.

Тому «хто вмикає PlayerHealthShow» — це питання «хто пише в байт за
зсувом 0x24b», і відповідь шукається байтовим сканом, а не по рядках.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hud_states import Image  # noqa: E402

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Game Files", "BF2.exe")
# Реєстрація розкидана по кількох функціях, тож скануємо всю секцію коду:
# ознака — виклик registerVariable через [edx+0x10] у HUD-об'єкті.
REGISTER = 0x401000
REGISTER_END = 0x87f000
# registerVariable існує в кількох перевантаженнях: через таблицю
# віртуальних методів і три прямі виклики (bool, float, рядок). Знайдені
# скануванням: що стоїть після конструктора рядка у 3077 місцях.
CALL_REGISTER = bytes.fromhex("ff5210")   # call [edx+0x10]
DIRECT = (0x466240, 0x4664e0, 0x466630)


def fields(image):
    """(ім'я, зсув поля) — усі виклики registerVariable у бінарі.

    Ознака — `call dword ptr [edx+0x10]`; безпосередньо перед ним у вікні
    в 48 байтів лежать `push <адреса імені>` і `lea <reg>, [<база>+зсув]`.
    Читаємо назад від виклику, а не вперед: так лінійний розбір не
    збивається на даних усередині коду.
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
            # lea з базою ebp — це місцевий рядок на стеку, не поле
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
    """Місця, де в поле пишуть.

    Два випадки: `mov byte/dword ptr [reg+зсув], стала` і
    `mov byte ptr [reg+зсув], reg8` — друге означає, що значення
    обчислюють, і сталої там немає.
    """
    found = []
    for base in range(8):
        if base == 4:
            continue  # esp — не наш випадок
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
    parser.add_argument("--only", help="показати лише змінні, що містять цей рядок")
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
            ", ".join("0x%x=%s" % (va, "обчислене" if value is None else value)
                      for va, value in places) or "—"))


if __name__ == "__main__":
    main()
