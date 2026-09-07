#!/usr/bin/env python3
"""Шукає в знятому трафіку стан `ScoreManager` — той, що несе квитки.

Розкладка знята з BF2.exe 0x5c9650 (docs/functions/score-manager.md):
32-бітова маска, далі на кожен виставлений біт 18-бітове число, у тому
порядку, у якому їх читає сама функція. Два біти несуть квитки команд.
"""
import struct, sys

# Порядок читання і ширини — дослівно з 0x5c9650.
ORDER = [
    (0x00000001, [18]), (0x00000002, [18]), (0x01000000, [18, 18]),
    (0x04000000, [18]), (0x10000000, [18]), (0x00000004, [18]),
    (0x00000008, [18]), (0x00000010, [18, 18]), (0x00000020, [18]),
    (0x00000040, [18]), (0x00000080, [18]), (0x00000100, [18]),
    (0x00000200, [18]),
    (0x00000400, [18, 1, 18]),          # квитки команди 1
    (0x00000800, [18]), (0x00001000, [18]), (0x00002000, [18]),
    (0x02000000, [18, 18]), (0x08000000, [18]), (0x20000000, [18]),
    (0x00004000, [18]), (0x00008000, [18]), (0x00010000, [18, 18]),
    (0x00020000, [18]), (0x00040000, [18]), (0x00080000, [18]),
    (0x00100000, [18]), (0x00200000, [18]),
    (0x00400000, [18, 1, 18]),          # квитки команди 2
    (0x00800000, [18]), (0x40000000, [3]), (0x80000000, [3, 18, 18]),
]


class Bits:
    def __init__(self, data, at=0):
        self.d = data
        self.at = at

    def read(self, n):
        if self.at + n > len(self.d) * 8:
            raise EOFError
        out = 0
        got = 0
        while n:
            byte = self.at >> 3
            off = self.at & 7
            take = min(8 - off, n)
            chunk = (self.d[byte] >> off) & ((1 << take) - 1)
            out |= chunk << got
            got += take
            n -= take
            self.at += take
        return out


def parse(data, start):
    b = Bits(data, start)
    mask = b.read(32)
    if mask == 0:
        return None
    tickets = {}
    for bit, widths in ORDER:
        if not (mask & bit):
            continue
        values = [b.read(w) for w in widths]
        if bit == 0x400:
            tickets[1] = values[0]
        if bit == 0x400000:
            tickets[2] = values[0]
    return mask, tickets, b.at


def packets(path):
    with open(path, "rb") as f:
        while True:
            head = f.read(4)
            if len(head) < 4:
                return
            (length,) = struct.unpack("<I", head)
            if length == 0 or length > 4096:
                return
            body = f.read(length)
            if len(body) < length:
                return
            yield body


def main(path, low=50, high=1000):
    hits = {}
    for index, body in enumerate(packets(path)):
        for start in range(0, len(body) * 8 - 32):
            try:
                found = parse(body, start)
            except EOFError:
                continue
            if not found:
                continue
            mask, tickets, end = found
            if 1 not in tickets or 2 not in tickets:
                continue
            if not (low <= tickets[1] <= high and low <= tickets[2] <= high):
                continue
            key = (tickets[1], tickets[2])
            hits.setdefault(key, []).append((index, start, mask))
    for key in sorted(hits, key=lambda k: -len(hits[k]))[:12]:
        rows = hits[key]
        print("квитки %4d / %4d — %3d збігів, перший: пакет %d біт %d маска %#010x"
              % (key[0], key[1], len(rows), rows[0][0], rows[0][1], rows[0][2]))


if __name__ == "__main__":
    main(sys.argv[1])
