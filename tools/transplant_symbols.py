#!/usr/bin/env python3
"""Переносить імена функцій із Linux-сервера в BF2.exe.

    tools/transplant_symbols.py > docs/reference/bf2exe-symbols.txt

У BF2.exe немає символів — усі функції звуться FUN_xxxxxx. Але в обох
бінарях є ті самі перевірки `Debug`, і кожна несе **шлях до вихідного
файлу й номер рядка**. У Linux-сервері поруч із такою перевіркою відомо
й ім'я функції, бо там символи повні. Отже пара «файл + рядок» — це
місток: та сама пара в BF2.exe вказує на ту саму функцію.

Це прибирає найдорожче в роботі з клієнтом: замість шукати функцію по
рядках і здогадах, ми одразу знаємо, як вона зветься в коді.

Обидва боки читаються локально через objdump: він бере і ELF, і PE.
У Linux-сервері адреса рядка вантажиться абсолютним числом у `%esi`, а
номер — у `%ecx`; у BF2.exe і те, і те кладеться на стек через `pushl`.
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
# Бінар не PIE, тож адреса рядка вантажиться абсолютним числом.
STRING_ARG = re.compile(r"movl\s+\$0x([0-9a-f]+),\s*%esi")
LINE_IMM = re.compile(r"movl\s+\$0x([0-9a-f]+),\s*%ecx")
FUNC = re.compile(r"^0*([0-9a-f]+) <(.+)>:")
INSTR = re.compile(r"^\s*([0-9a-f]+):\s+(.*)$")


def source_strings(path):
    """Адреса -> шлях до вихідного файлу для всіх таких рядків у бінарі."""
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
    """(файл, рядок) -> ім'я функції, зібране з коду Linux-сервера."""
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


# BF2_r.exe — складання з налагодженням: те саме, але перевірок утричі
# більше, тож і збігів виходить більше. Перемикається через BF2_PE.
WINDOWS = os.environ.get("BF2_PE", os.path.join(ROOT, "Game Files/BF2.exe"))
PUSH_IMM = re.compile(r"pushl\s+\$0x([0-9a-f]+)")


def pe_source_strings(path):
    """Адреса -> шлях до вихідного файлу для PE-образу."""
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
    """(файл, рядок) -> адреса перевірки в BF2.exe."""
    strings = pe_source_strings(WINDOWS)
    text = subprocess.run(["objdump", "-d", "--no-show-raw-insn", WINDOWS],
                          capture_output=True, text=True).stdout

    out = {}
    pending = None       # (файл, скільки інструкцій тому побачили)
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
        # Номер рядка: невелике число невдовзі після адреси файлу.
        if pending and 0 < value < 100000:
            out.setdefault((pending[0], value), at)
            pending = None
    return out


def basename(path):
    """Останній складник шляху. Windows-шляхи йдуть із зворотними скісними,
    і `os.path.basename` на macOS їх не розрізає."""
    return re.split(r"[\\/]", path)[-1]


def as_identifier(name):
    """Ім'я, придатне для Ghidra: без дужок, двокрапок і пробілів."""
    head = name.split("(")[0]
    head = head.replace("dice::hfe::", "").replace("::", "_")
    return re.sub(r"[^A-Za-z0-9_]", "_", head)


def main():
    table = linux_asserts()
    print("# Перевірки Debug у Linux-сервері: файл, рядок, функція.")
    print("# Пара «файл + рядок» однакова в BF2.exe, тож за нею можна")
    print("# називати тамтешні FUN_xxxxxx. Знято tools/transplant_symbols.py.")
    windows = windows_asserts()
    matched = {key: (windows[key], table[key]) for key in table if key in windows}

    print("# Перевірок у Linux-сервері: %d, у BF2.exe: %d, збіглося: %d"
          % (len(table), len(windows), len(matched)))
    print()
    seen = {}
    for (name, line), (address, func) in sorted(matched.items(), key=lambda kv: kv[1][0]):
        seen.setdefault(func, address)
    # Демангл через c++filt: читати `_ZN4dice3hfe...` руками немає потреби.
    names = list(seen)
    pretty = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True,
                            text=True).stdout.splitlines()
    readable = dict(zip(names, pretty)) if len(pretty) == len(names) else {}

    if "--script" in sys.argv:
        # Скрипт для Ghidra: перейменувати все за раз, а не сотнями викликів.
        print("# Згенеровано tools/transplant_symbols.py --script")
        print("# Запуск: у Ghidra через Script Manager, або headless:")
        print("#   analyzeHeadless <проєкт> OpenBF2 -process BF2.exe \\")
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
        print("print('перейменовано %d із %d' % (done, len(names)))")
        return

    print("# адреса перевірки в BF2.exe -> функція з Linux-сервера")
    print("# (адреса вказує всередину функції, не на її початок)")
    for func, address in sorted(seen.items(), key=lambda kv: kv[1]):
        print("0x%08x  %s" % (address, readable.get(func, func)))


if __name__ == "__main__":
    main()
