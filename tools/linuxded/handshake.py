#!/usr/bin/env python3
"""Carries the handshake with an original server through to player registration.

  tools/linuxded/handshake.py [host] [port]

The steps are the same as the client's: a connection request, the acknowledgement,
the challenge reply, then the ClientInfo block. We mirror the pings, otherwise the
server breaks the connection.
"""
import os
import socket
import sys
import time

from probe_connect import (
    GAME_VERSION, Reader, Writer, connect_request, name_hash,
    challenge_response_event, data_block_events, client_info_blob,
    data_packet, _event, CLIENT_INFO_BLOCK, EVENT_TYPE_BITS,
    EVENT_DATA_BLOCK,
    EVENT_STREAM_OFFSET, DENY,
)


def ping_response(conn, seq, ack, when):
    w = Writer()
    w.write(8, 4)
    w.write(conn, 8)
    w.write(seq & 0x3F, 6)
    w.write(ack & 0x3F, 6)
    w.write(0xFFFFFFFF, 32)
    w.write(1, 1)
    w.write(when, 32)
    w.write(when, 32)
    return w.data()


def short(kind, conn):
    w = Writer()
    w.write(kind, 4)
    w.write(conn, 8)
    return w.data()


class Session:
    def __init__(self, host, port, name="OpenBF2"):
        self.addr = (host, port)
        self.name = name
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(2.0)
        self.conn = 0
        self.seq = 0
        self.ack = 0
        self.challenges = 0
        self.batch = 0
        self.data_packets = 0

    def send(self, packet):
        self.sock.sendto(packet, self.addr)

    def send_events(self, events, pad=0):
        self.send(data_packet(self.conn, self.seq, self.ack, events,
                              pad=pad, batch=self.batch))
        self.seq = (self.seq + 1) & 0x3F
        if events:
            self.batch = (self.batch + 1) & 0x1F

    def connect(self, password="", punkbuster=0):
        for attempt in range(4):
            self.send(connect_request(GAME_VERSION, password=password, punkbuster=punkbuster))
            try:
                data = self.sock.recv(2048)
                break
            except socket.timeout:
                continue
        else:
            raise SystemExit("the server is silent")
        r = Reader(data)
        kind, _ = r.read(4), r.read(8)
        if kind == 3:
            reason = r.read(32)
            raise SystemExit("denied: " + DENY.get(reason, "code %d" % reason))
        if kind != 2:
            raise SystemExit("unexpected type %d" % kind)
        self.conn = r.read(8)
        server_time = r.read(32)
        pb = r.read(1)
        print("accepted: connection %d, server time %d, PunkBuster %d" % (self.conn, server_time, pb))
        self.send(short(4, self.conn))     # the acknowledgement

    def pump(self, seconds, on_challenge=None):
        until = time.time() + seconds
        while time.time() < until:
            try:
                data = self.sock.recv(2048)
            except socket.timeout:
                continue
            r = Reader(data)
            kind, _ = r.read(4), r.read(8)
            if kind == 7:
                seq, _ack, _bits = r.read(6), r.read(6), r.read(32)
                self.ack = seq
                r.read(1)
                self.send(ping_response(self.conn, self.seq, seq, r.read(32)))
                self.seq = (self.seq + 1) & 0x3F
            elif kind == 15:
                self.data_packets += 1
                seq = r.read(6)
                self.ack = seq
                r.read(6), r.read(32)
                r.at += EVENT_STREAM_OFFSET
                if r.read(1) != 1:
                    continue
                count, _batch, _rep = r.read(8), r.read(5), r.read(1)
                first = r.read(EVENT_TYPE_BITS)
                print("  data packet: events %d, the first of type %d" % (count, first))
                if first == 1:
                    self.challenges += 1
                    if on_challenge:
                        on_challenge()
            elif kind == 5:
                print("  the server disconnected us")
                return False
        return True


def alive(host, port=16567):
    """Whether the server is alive: it always answers a connection request with something."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.settimeout(2.0)
    try:
        probe.sendto(connect_request(GAME_VERSION, punkbuster=0), (host, port))
        probe.recv(2048)
        return True
    except OSError:
        return False
    finally:
        probe.close()


PAD = int(os.environ.get("PAD", "0"))


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 16567
    s = Session(host, port)
    s.connect()

    answered = []

    def answer():
        if answered:
            return
        answered.append(True)
        s.send_events([challenge_response_event()])
        print("  challenge reply sent")

    s.pump(5, on_challenge=answer)
    if not answered:
        print("there was no challenge")

    blob = client_info_blob(name=s.name, second="", third="",
                            number=name_hash(s.name), value=0)
    events = data_block_events(CLIENT_INFO_BLOCK, blob)
    print("ClientInfo: %d bytes, events %d" % (len(blob), len(events)))
    for event in events:
        s.send_events([event])
        s.pump(2)

    # We keep the connection up: the server has to see the player while we answer.
    s.pump(int(os.environ.get("HOLD", "30")))
    print("data packets: %d, challenges: %d" % (s.data_packets, s.challenges))


if __name__ == "__main__":
    main()
