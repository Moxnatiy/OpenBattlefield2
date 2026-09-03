#!/usr/bin/env python3
"""Розбирає знятий трафік BF2 і показує, чим саме говорить клієнт.

    sudo tcpdump -i any -w /tmp/bf2.pcap "udp port 16567"
    tools/linuxded/pcap_bf2.py /tmp/bf2.pcap
    tools/linuxded/pcap_bf2.py /tmp/bf2.pcap --from-client --events

Сенс простий: коли наш клієнт поводиться не так, як оригінал, найдешевше
покласти поруч байти обох і подивитися, де вони розходяться. Формат
tcpdump на macOS — pcapng з PKTAP-заголовком, тож обидва тут і розбираємо.

Розкладка пакета взята з нашого ж `obf2/net/bf2_protocol.h`, тобто з того,
що вже зреверсовано: 4 біти виду, 8 бітів номера з'єднання, далі —
залежно від виду.
"""
import argparse
import struct
import sys

# Види пакетів (bf2_protocol.h).
KINDS = {
    1: "ConnectRequest", 2: "ConnectAccept", 3: "ConnectDenied",
    4: "ConnectAcceptAck", 5: "Disconnect", 7: "PingRequest",
    8: "PingResponse", 9: "ServerInfoRequest", 15: "Data",
}

# Мережеві події (docs/functions/network-events.md).
NET_EVENTS = {
    1: "NEDataBlockReady", 2: "NELoadComplete", 3: "NEStartSimulation",
    4: "NEDatabaseComplete", 5: "NEReset", 6: "NESelectSpawnGroup",
    7: "NESelectTeam", 8: "NESelectKit", 9: "NEPlayerSpawned",
    10: "NEPlayerDead", 11: "NEStringReceived", 12: "NESuicide",
    13: "NERadioMessageReceived", 17: "NENetworkableDestroyed",
    18: "NERemoteConsoleCommand", 19: "NERemoteConsoleFeedback",
    20: "NEEndOfRoundGuard",
}


class Bits:
    """Читач бітів у тому ж порядку, що й BitStream рушія: молодші перші."""

    def __init__(self, data):
        self.data = data
        self.at = 0

    def read(self, count):
        value = 0
        for i in range(count):
            byte = self.at >> 3
            if byte >= len(self.data):
                return None
            bit = (self.data[byte] >> (self.at & 7)) & 1
            value |= bit << i
            self.at += 1
        return value

    def left(self):
        return len(self.data) * 8 - self.at


def read_pcapng(path):
    """Пакети з pcapng: (час, байти, тип каналу)."""
    data = open(path, "rb").read()
    out = []
    at = 0
    endian = "<"
    link = 1
    while at + 12 <= len(data):
        block_type, = struct.unpack_from(endian + "I", data, at)
        if block_type == 0x0A0D0D0A:  # Section Header
            magic, = struct.unpack_from("<I", data, at + 8)
            endian = "<" if magic == 0x1A2B3C4D else ">"
            block_type, = struct.unpack_from(endian + "I", data, at)
        total, = struct.unpack_from(endian + "I", data, at + 4)
        if total < 12 or at + total > len(data):
            break
        body = data[at + 8:at + total - 4]
        if block_type == 0x00000001:  # Interface Description
            link, = struct.unpack_from(endian + "H", body, 0)
        elif block_type == 0x00000006:  # Enhanced Packet
            _, high, low, captured, _ = struct.unpack_from(endian + "IIIII", body, 0)
            out.append(((high << 32 | low), body[20:20 + captured], link))
        at += total
    return out


def strip_pktap(packet):
    """PKTAP: змінний заголовок, за ним справжній пакет зі своїм DLT."""
    if len(packet) < 4:
        return None, None
    length, = struct.unpack_from("<I", packet, 0)
    if length < 4 or length > len(packet):
        return None, None
    dlt, = struct.unpack_from("<I", packet, 8)
    return packet[length:], dlt


def udp_payload(frame, dlt):
    """(джерело, призначення, дані) або None."""
    if dlt == 1:  # Ethernet
        if len(frame) < 14 or struct.unpack_from(">H", frame, 12)[0] != 0x0800:
            return None
        ip = frame[14:]
    elif dlt in (0, 108):  # NULL / LOOP
        ip = frame[4:]
    elif dlt == 12:  # RAW
        ip = frame
    else:
        return None
    if len(ip) < 20 or (ip[0] >> 4) != 4 or ip[9] != 17:
        return None
    head = (ip[0] & 0xF) * 4
    src = ".".join(str(b) for b in ip[12:16])
    dst = ".".join(str(b) for b in ip[16:20])
    udp = ip[head:]
    if len(udp) < 8:
        return None
    sport, dport = struct.unpack_from(">HH", udp, 0)
    return (f"{src}:{sport}", f"{dst}:{dport}", udp[8:])


def load_event_table():
    """Таблиця подій із того самого `bf2_events.inc`, що й у нашому коді.

    Дублювати її тут було б помилкою: вона згенерована з бінаря, і два
    списки неминуче розійшлися б.
    """
    import os
    import re
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, "..", "..", "src", "net", "src", "bf2_events.inc")
    names, widths, branchy = {}, {}, set()
    for line in open(path, encoding="utf-8"):
        flat = re.match(r'BF2_EVENT\((\d+),\s*"([^"]+)",\s*(.+)\)', line)
        if flat:
            number = int(flat.group(1))
            names[number] = flat.group(2)
            widths[number] = [int(x) for x in flat.group(3).split(",")]
            continue
        other = re.match(r'BF2_EVENT_(BRANCHY|EMPTY)\((\d+),\s*"([^"]+)"\)', line)
        if other:
            number = int(other.group(2))
            names[number] = other.group(3)
            if other.group(1) == "BRANCHY":
                branchy.add(number)
            else:
                widths[number] = []
    return names, widths, branchy


GAME_EVENTS, EVENT_WIDTHS, EVENT_BRANCHY = load_event_table()


def describe_events(payload):
    """Події з пакета даних. Повертає перелік рядків.

    Розкладка заголовка — та сама, що в нашому `writeDataHeader`:
    4 біти виду, 8 номера з'єднання, 6 sequence, 6 ack, 32 ackBits,
    16 довжини корисної частини. Далі каркас потоку подій: біт потоку
    дій, біт «події є», 8 бітів кількості, 5 номера пачки, 1 запасний.
    """
    bits = Bits(payload)
    if bits.read(4) != 15:
        return []
    bits.read(8)                      # номер з'єднання
    bits.read(6), bits.read(6)        # sequence, ack
    bits.read(32)                     # ackBits
    bits.read(16)                     # довжина корисної частини
    bits.read(1)                      # потік дій гравця
    if bits.read(1) != 1:
        return []
    count = bits.read(8)
    bits.read(5), bits.read(1)        # номер пачки, запасний біт

    out = []
    for _ in range(count or 0):
        kind_id = bits.read(7)
        if kind_id is None:
            break
        name = GAME_EVENTS.get(kind_id, f"подія {kind_id}")

        # Події з рівним переліком полів проходимо за таблицею; ті, у
        # яких поля лежать за умовою, розбираємо руками — рівно так само,
        # як це робить наш `skipEvent`.
        if kind_id == 11:             # PostRemoteEvent
            category = bits.read(4)
            number = bits.read(32)
            bits.read(32)             # затримка (float)
            length = bits.read(8)
            value = None
            if length == 4:
                value = 0
                for i in range(4):
                    byte = bits.read(8)
                    if byte is None:
                        break
                    value |= byte << (i * 8)
            elif length:
                for _ in range(length):
                    bits.read(8)
            label = NET_EVENTS.get(number, str(number)) if category == 6 \
                else f"кат {category} № {number}"
            out.append(label + (f" = {value}" if value is not None else ""))
            continue
        if kind_id == 9:              # EnterVehicleEvent: гравець і об'єкт
            player = bits.read(8)
            obj = bits.read(16)
            flag = bits.read(1)
            out.append(f"{name}: гравець {player} -> об'єкт {obj} ({flag})")
            continue
        if kind_id == 10:             # ExitVehicleEvent
            out.append(f"{name}: гравець {bits.read(8)} ({bits.read(1)})")
            continue
        if kind_id == 7:              # DestroyObjectEvent
            out.append(f"{name}: об'єкт {bits.read(16)}")
            continue
        if kind_id == 4:              # DataBlockEvent
            if bits.read(1) == 1:
                out.append(f"{name}: заголовок тип {bits.read(32)} розмір {bits.read(32)}")
            else:
                length = bits.read(8) or 0
                for _ in range(length):
                    bits.read(8)
                out.append(f"{name}: шматок {length}б")
            continue
        if kind_id == 0:              # StringManagerEvent
            if bits.read(1) != 0:
                bits.read(6)
            out.append(name)
            continue
        if kind_id == 6:              # CreateObjectEvent
            template = bits.read(32)
            network = bits.read(16)
            bits.read(2)
            branch = bits.read(1)
            if branch == 1:
                bits.read(8)
                out.append(f"{name}: шаблон {template}, номер {network}")
            else:
                if bits.read(1) == 1:
                    bits.read(96)
                if bits.read(1) == 1:
                    bits.read(96)
                out.append(f"{name}: шаблон {template}, номер {network}")
            continue
        if kind_id in EVENT_BRANCHY:
            out.append(name + " (розбір попереду)")
            break
        for width in EVENT_WIDTHS.get(kind_id, []):
            bits.read(width)
        out.append(name)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--from-client", action="store_true",
                        help="лише те, що шле клієнт (пакети на порт 16567)")
    parser.add_argument("--events", action="store_true", help="розбирати події")
    parser.add_argument("--hex", type=int, default=0, help="скільки байтів показувати")
    parser.add_argument("--kind", help="лише цей вид пакета")
    args = parser.parse_args()

    first = None
    counts = {}
    for stamp, packet, link in read_pcapng(args.path):
        frame, dlt = (strip_pktap(packet) if link == 258 else (packet, link))
        if frame is None:
            continue
        parsed = udp_payload(frame, dlt)
        if parsed is None:
            continue
        src, dst, payload = parsed
        to_server = dst.endswith(":16567")
        if args.from_client and not to_server:
            continue
        if not payload:
            continue
        kind = payload[0] & 0xF
        name = KINDS.get(kind, f"вид {kind}")
        counts[name] = counts.get(name, 0) + 1
        if args.kind and args.kind != name:
            continue
        if first is None:
            first = stamp
        line = f"{(stamp - first) / 1e6:8.2f}с  {'клієнт->сервер' if to_server else 'сервер->клієнт'}  {name:16s} {len(payload):4d}б"
        extra = describe_events(payload) if args.events and kind == 15 else []
        if extra:
            line += "  " + "; ".join(extra)
        if args.hex:
            line += "\n          " + " ".join(f"{b:02x}" for b in payload[:args.hex])
        if not args.events or extra or kind != 15:
            print(line)

    print("\nусього:", ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
