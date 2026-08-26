#!/usr/bin/env python3
"""Доводить з'єднання з оригінальним сервером до потрібного етапу і
записує все, що прийшло, у файл-зразок.

    tools/linuxded/capture.py --stage world --out tests/data/world.bin
    tools/linuxded/capture.py --stage player --show
    tools/linuxded/capture.py --wait

Етапи:
    connect   запит і підтвердження
    challenge + відповідь на виклик
    player    + блок ClientInfo (сервер заводить гравця)
    world     + «рівень завантажено» (сервер починає слати світ)

Сенс у тому, щоб кожен дослід був прапорцем, а не новим скриптом, і щоб
спіймані пакети лишалися назавжди — з них ростуть тести, які не
потребують ані стенда, ані мережі.

Формат файлу простий: для кожного пакета u32 довжина, далі байти.
"""
import argparse
import os
import socket
import struct
import sys
import time

import probe_connect as p
from handshake import Session, ping_response

STAGES = ["connect", "challenge", "player", "world"]

# Блок з відомостями про рівень (`GameServer::sendClientMapInfo`).
MAP_INFO_BLOCK = 5

# Друга подія, що рухає стан з'єднання (`clientSendDatabaseComplete`).
NET_DATABASE_COMPLETE = 4


def post_remote(category, event):
    def fill(w):
        w.write(category, 4)
        w.write(event, 32)
        w.write(0, 32)   # затримка, float 0.0
        w.write(0, 8)    # даних немає
    return p._event(p.EVENT_POST_REMOTE, fill)


def wait_for_server(host, port, attempts=60, delay=5):
    """Чекає, поки сервер підніметься й завантажить рівень."""
    for _ in range(attempts):
        answer = p.probe(p.GAME_VERSION, host=host, port=port, timeout=2, punkbuster=0)
        if "ПРИЙНЯТО" in answer:
            return True
        time.sleep(delay)
    return False


class Capture:
    def __init__(self, host, port, name):
        self.session = Session(host, port, name)
        self.packets = []
        self.map_info = None
        self._block = None
        self._block_type = 0
        self._block_size = 0

    def _handle_blocks(self, data):
        """Складає блоки даних; повертає True, коли прийшов блок із рівнем."""
        info = p.walk_events(data)
        if not info:
            return False
        got_level = False
        for event in info["події"]:
            if event.get("клас") != "DataBlockEvent":
                continue
            if event["заголовок"]:
                self._block_type = event["тип блока"]
                self._block_size = event["розмір"]
                self._block = bytearray()
            elif self._block is not None:
                self._block += event["дані"]
                if len(self._block) >= self._block_size:
                    if self._block_type == MAP_INFO_BLOCK:
                        self.map_info = bytes(self._block)
                        got_level = True
                    self._block = None
        return got_level

    def _drain(self, seconds):
        s = self.session
        until = time.time() + seconds
        while time.time() < until:
            try:
                data = s.sock.recv(4096)
            except socket.timeout:
                continue
            self.packets.append(data)
            self._handle_blocks(data)
            r = p.Reader(data)
            kind = r.read(4)
            r.read(8)
            if kind == 7:
                seq = r.read(6)
                r.read(6), r.read(32), r.read(1)
                s.sock.sendto(ping_response(s.conn, s.seq, seq, r.read(32)), s.addr)
                s.seq = (s.seq + 1) & 0x3F
            elif kind == 15:
                s.ack = r.read(6)

    def run(self, stage, hold):
        s = self.session
        s.connect()
        if stage == "connect":
            self._drain(hold)
            return

        answered = []

        def answer():
            if not answered:
                answered.append(True)
                s.send_events([p.challenge_response_event()])

        s.pump(6, on_challenge=answer)
        if stage == "challenge":
            self._drain(hold)
            return

        info = p.client_info_blob(name=s.name, number=p.name_hash(s.name))
        for event in p.data_block_events(p.CLIENT_INFO_BLOCK, info):
            s.send_events([event])
        self._drain(3)
        if stage == "player":
            self._drain(hold)
            return

        # Спершу дочекатися блока з рівнем — саме в такому порядку працює
        # оригінал: сервер шле рівень, клієнт його завантажує і аж тоді
        # каже «готово». Інакше об'єкти приходять раніше за рівень.
        waited = 0.0
        while self.map_info is None and waited < 10.0:
            self._drain(1.0)
            waited += 1.0
        if self.map_info is None:
            print("блок із рівнем не прийшов")
        s.send_events([post_remote(p.NETWORK_CATEGORY, p.NET_LOAD_COMPLETE)])
        self._drain(2)
        # І «базу гравців отримано»: стан з'єднання рухають обидві події,
        # а `GameServer::isClientReady` вимагає, щоб він перевалив за три.
        s.send_events([post_remote(p.NETWORK_CATEGORY, NET_DATABASE_COMPLETE)])
        self._drain(hold)


def load(path):
    """Читає файл-зразок: для кожного пакета u32 довжина, далі байти."""
    packets = []
    with open(path, "rb") as handle:
        while True:
            head = handle.read(4)
            if len(head) < 4:
                break
            packets.append(handle.read(struct.unpack("<I", head)[0]))
    return packets


def describe(packets):
    """Показує, що в пакетах, і де саме розбір спіткнувся.

    Тип події не буває більшим за 69 — усе понад це означає, що
    попередня подія прочитана неправильної довжини. Тому поруч з
    невідомим типом друкуємо й ту, після якої він трапився: так одразу
    видно, чию розкладку треба виправляти.
    """
    kinds, unknown, blamed = {}, {}, {}
    for data in packets:
        info = p.walk_events(data)
        if not info:
            continue
        previous = None
        for event in info["події"]:
            if event.get("невідома"):
                kind = event["тип"]
                unknown[kind] = unknown.get(kind, 0) + 1
                if kind > 69 and previous:
                    blamed[previous] = blamed.get(previous, 0) + 1
                continue
            kinds[event["клас"]] = kinds.get(event["клас"], 0) + 1
            previous = event["клас"]
    print("пакетів: %d" % len(packets))
    for name, count in sorted(kinds.items()):
        print("  %-24s %d" % (name, count))
    if unknown:
        print("  ще не розбираємо: %s" % unknown)
    if blamed:
        print("  збилося після: %s" % blamed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=os.environ.get("BF2_ADDR", "192.168.100.100"))
    parser.add_argument("--port", type=int, default=16567)
    parser.add_argument("--name", default="OpenBF2")
    parser.add_argument("--stage", choices=STAGES, default="world")
    parser.add_argument("--hold", type=float, default=20.0, help="скільки секунд слухати")
    parser.add_argument("--out", help="куди записати спіймані пакети")
    parser.add_argument("--show", action="store_true", help="розібрати й показати вміст")
    parser.add_argument("--wait", action="store_true", help="лише дочекатися сервера")
    parser.add_argument("--replay", help="розібрати раніше спіймані пакети з файлу")
    args = parser.parse_args()

    if args.replay:
        describe(load(args.replay))
        return 0

    if args.wait:
        ok = wait_for_server(args.host, args.port)
        print("сервер готовий" if ok else "сервер не відповідає")
        return 0 if ok else 1

    capture = Capture(args.host, args.port, args.name)
    capture.run(args.stage, args.hold)

    if args.out:
        with open(args.out, "wb") as handle:
            for packet in capture.packets:
                handle.write(struct.pack("<I", len(packet)))
                handle.write(packet)
        print("записано %d пакетів у %s" % (len(capture.packets), args.out))
    if args.show or not args.out:
        describe(capture.packets)
    return 0


if __name__ == "__main__":
    sys.exit(main())
