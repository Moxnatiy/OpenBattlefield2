#!/usr/bin/env python3
"""Зонд протоколу під'єднання BF2: шле connect request і читає відповідь.

Розкладка взята з рушія (NetServer::_update / handleConnectRequest /
sendConnectAccept / sendConnectDenied у Linux-сервері):

  заголовок: 4 біти тип, 8 бітів номер з'єднання
  тип 1 — запит:  u32 0x1002, u32 версія, 1 біт PunkBuster,
                  u32 токен перепід'єднання, 32 байти пароль,
                  32 байти тека мода
  тип 2 — прийнято: u8 номер, u32 час сервера, 1 біт PunkBuster
  тип 3 — відмова:  u32 причина, 1 біт «є тека», [32 байти теки]

Біти пакуються молодшими вперед — так само, як у нашому BitStream.
"""
import socket
import sys


class Writer:
    def __init__(self):
        self.bits = []

    def write(self, value, count):
        for i in range(count):
            self.bits.append((value >> i) & 1)

    def write_bytes(self, text, size):
        raw = text.encode("latin-1")[:size]
        raw = raw + b"\0" * (size - len(raw))
        for byte in raw:
            self.write(byte, 8)

    def data(self):
        out = bytearray((len(self.bits) + 7) // 8)
        for i, bit in enumerate(self.bits):
            if bit:
                out[i // 8] |= 1 << (i % 8)
        return bytes(out)


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def read(self, count):
        value = 0
        for i in range(count):
            byte = self.at // 8
            if byte >= len(self.data):
                return value
            if self.data[byte] >> (self.at % 8) & 1:
                value |= 1 << i
            self.at += 1
        return value

    def read_bytes(self, size):
        return bytes(self.read(8) for _ in range(size))


DENY = {
    0x02: "сервер повний",
    0x09: "версія не підходить",
    0x11: "невірний пароль",
    0x16: "адресу заблоковано",
    0x17: "клієнт застарий",
    0x18: "клієнт занадто новий",
    0x1E: "немає вільних місць",
    0x1F: "потрібен PunkBuster",
    0x24: "інша тека мода",
}


# Знайдено двійковим пошуком по живому серверу: байти 15 0C 51 00 — це
# 1.5 і збірка 0x0C51 = 3153, тобто рівно bf2-linuxded-1.5.3153.0.
GAME_VERSION = 0x150C5100


def connect_request(version=GAME_VERSION, mod="mods/bf2", password="", token=0, punkbuster=1):
    writer = Writer()
    writer.write(1, 4)          # тип: запит на під'єднання
    writer.write(0, 8)          # номер з'єднання: у клієнта його ще немає
    writer.write(0x1002, 32)    # стала, яку рушій звіряє першою
    writer.write(version, 32)
    writer.write(punkbuster, 1)
    writer.write(token, 32)
    writer.write_bytes(password, 32)
    writer.write_bytes(mod, 32)
    return writer.data()


def probe(version, host="127.0.0.1", port=16567, timeout=2.0, **kwargs):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    sock.sendto(connect_request(version, **kwargs), (host, port))
    try:
        data, _ = sock.recvfrom(2048)
    except socket.timeout:
        return "тиша"
    finally:
        sock.close()

    reader = Reader(data)
    kind = reader.read(4)
    connection = reader.read(8)
    if kind == 2:
        assigned = reader.read(8)
        server_time = reader.read(32)
        pb = reader.read(1)
        return "ПРИЙНЯТО: з'єднання %d, час сервера %d, PunkBuster %d" % (assigned, server_time, pb)
    if kind == 3:
        reason = reader.read(32)
        has_mod = reader.read(1)
        text = DENY.get(reason, "код %d" % reason)
        if has_mod:
            text += " (сервер хоче %r)" % reader.read_bytes(32).split(b"\0")[0].decode("latin-1")
        return "відмова: " + text
    return "тип %d, номер %d, %d байтів" % (kind, connection, len(data))


if __name__ == "__main__":
    versions = [int(v, 0) for v in sys.argv[1:]] or [0]
    for version in versions:
        print("версія %#x -> %s" % (version, probe(version)))


# --- потік подій у пакеті даних (тип 15) ---
#
# Розкладка з рушія: GameEventManager::processReceivedPacket читає
#   1 біт  «є події», 8 бітів кількість, 5 бітів ?, 1 біт ?
# далі readGameEvent бере тип у N бітах, де N — найменше, при якому
# (1<<N)-1 вміщує розмір реєстру подій. На живих пакетах N = 7.
#
# Перед цим у пакеті ще 17 бітів каркасу потоків — їх ми поки не розібрали.
EVENT_STREAM_OFFSET = 17
EVENT_TYPE_BITS = 7


def decode_events(data):
    """Розбирає пакет даних і повертає опис подій."""
    r = Reader(data)
    kind = r.read(4)
    r.read(8)
    if kind != 15:
        return "не пакет даних (тип %d)" % kind

    sequence = r.read(6)
    r.read(6)
    r.read(32)
    r.at += EVENT_STREAM_OFFSET

    if r.read(1) != 1:
        return "seq %d: подій немає" % sequence
    count = r.read(8)
    r.read(5)
    r.read(1)

    out = ["seq %d: подій %d" % (sequence, count)]
    for _ in range(count):
        kind = r.read(EVENT_TYPE_BITS)
        if kind == 1:
            challenge = r.read_bytes(10).split(b"\0")[0].decode("latin-1")
            length = r.read(8)
            mod = r.read_bytes(length).decode("latin-1")
            out.append("  виклик: %r, мод %r" % (challenge, mod))
        else:
            out.append("  подія типу %d (ще не розібрано)" % kind)
            break
    return "\n".join(out)
