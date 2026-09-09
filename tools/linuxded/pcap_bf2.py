#!/usr/bin/env python3
"""Takes captured BF2 traffic apart and shows what the client actually says.

    sudo tcpdump -i any -w /tmp/bf2.pcap "udp port 16567"
    tools/linuxded/pcap_bf2.py /tmp/bf2.pcap
    tools/linuxded/pcap_bf2.py /tmp/bf2.pcap --from-client --events

The point is simple: when our client behaves differently from the original, the
cheapest thing is to put both sets of bytes side by side and see where they
diverge. tcpdump's format on macOS is pcapng with a PKTAP header, so both are taken apart here.

The packet's layout comes from our own `obf2/net/bf2_protocol.h`, that is from
what has already been reversed: 4 bits of kind, 8 bits of connection id, then
whatever the kind implies.
"""
import argparse
import struct
import sys

# The packet kinds (bf2_protocol.h).
KINDS = {
    1: "ConnectRequest", 2: "ConnectAccept", 3: "ConnectDenied",
    4: "ConnectAcceptAck", 5: "Disconnect", 7: "PingRequest",
    8: "PingResponse", 9: "ServerInfoRequest", 15: "Data",
}

# The network events (docs/functions/network-events.md).
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
    """A bit reader in the same order as the engine's BitStream: low bits first."""

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
    """Packets from a pcapng: (time, bytes, link type)."""
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
    """PKTAP: a variable header, then the real packet with its own DLT."""
    if len(packet) < 4:
        return None, None
    length, = struct.unpack_from("<I", packet, 0)
    if length < 4 or length > len(packet):
        return None, None
    dlt, = struct.unpack_from("<I", packet, 8)
    return packet[length:], dlt


def udp_payload(frame, dlt):
    """(source, destination, data) or None."""
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
    """The event table from the same `bf2_events.inc` as in our code.

    Duplicating it here would be a mistake: it is generated from the binary, and two
    lists would inevitably diverge.
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


def read_actions(bits):
    """The player action stream, if the packet holds one.

    The layout comes from `PlayerActionManager::processReceivedPacket` (0x44d670):

        1 bit                  are there actions
        4 bits                 how many records
        9 bits                 a number
        1 sign bit + 31 bits   a number (shared by every record)
        then, per record:
            6 times: 1 sign bit + 15 bits of value
            32 bits  the button mask
            9 bits   the record's number
            1 bit    a flag

    A record's size in memory is 28 bytes, and the offsets (+4..+0xe words,
    +0x10 u32, +0x14 u32, +0x18 a byte) agree with this list exactly.
    """
    if bits.read(1) != 1:
        return None
    count = bits.read(4)
    number = bits.read(9)
    if count is None or number is None:
        return None
    common = None
    if count > 0:
        sign = bits.read(1)
        value = bits.read(31)
        if value is None:
            return None
        common = -value if sign == 1 else value
    records = []
    for _ in range(count or 0):
        axes = []
        for _ in range(6):
            sign = bits.read(1)
            value = bits.read(15)
            if value is None:
                return {"count": count, "number": number, "common": common, "records": records}
            axes.append(-value if sign == 1 else value)
        buttons = bits.read(32)
        tick = bits.read(9)
        flag = bits.read(1)
        records.append({"axes": axes, "buttons": buttons, "tick": tick, "flag": flag})
    return {"count": count, "number": number, "common": common, "records": records}


def describe_events(payload):
    """The events from a data packet. Returns a list of lines.

    The header's layout is the same as in our `writeDataHeader`:
    4 bits of kind, 8 of connection id, 6 sequence, 6 ack, 32 ackBits,
    16 of payload length. Then the event stream's framing: the action stream's
    bit, the "there are events" bit, 8 bits of count, 5 of batch number, 1 spare.
    """
    bits = Bits(payload)
    if bits.read(4) != 15:
        return []
    bits.read(8)                      # the connection id
    bits.read(6), bits.read(6)        # sequence, ack
    bits.read(32)                     # ackBits
    bits.read(16)                     # the payload's length
    bits.read(1)                      # the player action stream
    if bits.read(1) != 1:
        return []
    count = bits.read(8)
    bits.read(5), bits.read(1)        # the batch number, the spare bit

    out = []
    for _ in range(count or 0):
        kind_id = bits.read(7)
        if kind_id is None:
            break
        name = GAME_EVENTS.get(kind_id, f"event {kind_id}")

        # Events with a flat field list we walk by the table; the ones whose fields
        # sit behind a condition we parse by hand — exactly as our `skipEvent` does.
        if kind_id == 11:             # PostRemoteEvent
            category = bits.read(4)
            number = bits.read(32)
            bits.read(32)             # the delay (float)
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
                else f"category {category} no {number}"
            out.append(label + (f" = {value}" if value is not None else ""))
            continue
        if kind_id == 9:              # EnterVehicleEvent: the player and the object
            player = bits.read(8)
            obj = bits.read(16)
            flag = bits.read(1)
            out.append(f"{name}: player {player} -> object {obj} ({flag})")
            continue
        if kind_id == 10:             # ExitVehicleEvent
            out.append(f"{name}: player {bits.read(8)} ({bits.read(1)})")
            continue
        if kind_id == 7:              # DestroyObjectEvent
            out.append(f"{name}: object {bits.read(16)}")
            continue
        if kind_id == 4:              # DataBlockEvent
            if bits.read(1) == 1:
                out.append(f"{name}: header type {bits.read(32)} size {bits.read(32)}")
            else:
                length = bits.read(8) or 0
                for _ in range(length):
                    bits.read(8)
                out.append(f"{name}: chunk {length}b")
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
                out.append(f"{name}: template {template}, id {network}")
            else:
                if bits.read(1) == 1:
                    bits.read(96)
                if bits.read(1) == 1:
                    bits.read(96)
                out.append(f"{name}: template {template}, id {network}")
            continue
        if kind_id in EVENT_BRANCHY:
            out.append(name + " (parsing still ahead)")
            break
        for width in EVENT_WIDTHS.get(kind_id, []):
            bits.read(width)
        out.append(name)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--from-client", action="store_true",
                        help="only what the client sends (packets to port 16567)")
    parser.add_argument("--events", action="store_true", help="parse the events")
    parser.add_argument("--hex", type=int, default=0, help="how many bytes to show")
    parser.add_argument("--kind", help="this packet kind only")
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
        name = KINDS.get(kind, f"kind {kind}")
        counts[name] = counts.get(name, 0) + 1
        if args.kind and args.kind != name:
            continue
        if first is None:
            first = stamp
        line = f"{(stamp - first) / 1e6:8.2f}s  {'client->server' if to_server else 'server->client'}  {name:16s} {len(payload):4d}b"
        extra = describe_events(payload) if args.events and kind == 15 else []
        if extra:
            line += "  " + "; ".join(extra)
        if args.hex:
            line += "\n          " + " ".join(f"{b:02x}" for b in payload[:args.hex])
        if not args.events or extra or kind != 15:
            print(line)

    print("\ntotal:", ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
