#!/usr/bin/env python3
"""Drives a connection with an original server to the required stage and
records everything that arrived into a sample file.

    tools/linuxded/capture.py --stage world --out tests/data/world.bin
    tools/linuxded/capture.py --stage player --show
    tools/linuxded/capture.py --wait

The stages:
    connect   the request and the acknowledgement
    challenge + the challenge reply
    player    + the ClientInfo block (the server creates the player)
    world     + "the level is loaded" (the server starts sending the world)

The point is that every experiment is a flag rather than a new script, and that
the captured packets stay forever — tests grow out of them that need neither the
rig nor a network.

The file's format is simple: a u32 length per packet, then the bytes.
"""
import argparse
import os
import socket
import struct
import sys
import time

import probe_connect as p
from handshake import Session, ping_response

STAGES = ["connect", "challenge", "player", "world", "spawn"]

# The block with the level's details (`GameServer::sendClientMapInfo`).
MAP_INFO_BLOCK = 5

# The second event that moves the connection's state (`clientSendDatabaseComplete`).
# The player spawn chain. The numbers come from the table BF2.exe registers itself
# (see docs/functions/network-events.md).
NET_DATABASE_COMPLETE = 4
NET_SELECT_SPAWN_GROUP = 6
NET_SELECT_TEAM = 7
NET_SELECT_KIT = 8

# We deliberately do not send NEDatabaseComplete (4): after it the server falls
# silent — it stops sending pings and our next packets no longer reach it.
# The real client sends it elsewhere in the conversation.


def post_remote(category, event, value=None):
    """A network event. The ones carrying a value expect a 32-bit number."""
    payload = b"" if value is None else struct.pack("<i", value)

    def fill(w):
        w.write(category, 4)
        w.write(event, 32)
        w.write(0, 32)   # the delay, float 0.0
        w.write(len(payload), 8)
        for byte in payload:
            w.write(byte, 8)
    return p._event(p.EVENT_POST_REMOTE, fill)


def wait_for_server(host, port, attempts=60, delay=5):
    """Waits for the server to come up and load the level."""
    for _ in range(attempts):
        answer = p.probe(p.GAME_VERSION, host=host, port=port, timeout=2, punkbuster=0)
        if "ACCEPTED" in answer:
            return True
        time.sleep(delay)
    return False


class Capture:
    def __init__(self, host, port, name, team=1, kit=0, group=1, misc_hash=None,
                 level="dalian_plant", ordinal=0):
        self.ordinal = ordinal
        self.misc_hash = misc_hash
        self.level = level
        self.team = team
        self.kit = kit
        self.group = group
        self.session = Session(host, port, name)
        self.packets = []
        self.map_info = None
        self.answered = False
        self._block = None
        self._block_type = 0
        self._block_size = 0

    def _handle_blocks(self, data):
        """Assembles data blocks; returns True once the level block has arrived."""
        info = p.walk_events(data)
        if not info:
            return False
        got_level = False
        for event in info["events"]:
            if event.get("class") != "DataBlockEvent":
                continue
            if event["header"]:
                self._block_type = event["block type"]
                self._block_size = event["size"]
                self._block = bytearray()
            elif self._block is not None:
                self._block += event["data"]
                if len(self._block) >= self._block_size:
                    if self._block_type == MAP_INFO_BLOCK:
                        self.map_info = bytes(self._block)
                        got_level = True
                    self._block = None
        return got_level

    def _answer_challenge(self, data):
        """Answers the challenge if it is in this packet."""
        if self.answered:
            return
        info = p.walk_events(data)
        if not info:
            return
        for event in info["events"]:
            if event.get("type") == 1:
                self.answered = True
                self.session.send_events([p.challenge_response_event()])
                return

    def _drain(self, seconds):
        s = self.session
        until = time.time() + seconds
        while time.time() < until:
            try:
                data = s.sock.recv(4096)
            except socket.timeout:
                continue
            self.packets.append(data)
            self._answer_challenge(data)
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

        # The challenge and its reply are handled by `_drain` itself: only it writes
        # packets and assembles blocks, so there must be no other receive loop —
        # otherwise the level block goes past.
        self._drain(6)
        if stage == "challenge":
            self._drain(hold)
            return

        info = p.client_info_blob(name=s.name, number=p.name_hash(s.name))
        for event in p.data_block_events(p.CLIENT_INFO_BLOCK, info):
            # A pause between the block's header and a chunk: the server has to
            # manage to create the block before anything is put into it.
            s.send_events([event])
            self._drain(2)
        self._drain(4)
        if stage == "player":
            self._drain(hold)
            return

        # The level block has to be waited for first — that is the order the original
        # works in: the server sends the level, the client loads it and only then
        # says "ready". Otherwise the objects arrive before the level.
        waited = 0.0
        while self.map_info is None and waited < 20.0:
            self._drain(1.0)
            waited += 1.0
        if self.map_info is None:
            print("the level block did not arrive")
        s.send_events([post_remote(p.NETWORK_CATEGORY, p.NET_LOAD_COMPLETE)])
        if stage == "world":
            self._drain(hold)
            return

        # The content check: without it `clientSendDatabaseComplete` takes the
        # refusal branch and the connection's state does not grow far enough.
        # The first hash is the one the server computes itself; the other two come
        # from the fingerprint files in the mod's directory.
        self._drain(3)
        here = os.path.dirname(os.path.abspath(__file__))
        mods = os.path.join(here, "..", "..", "Game Files", "mods", "bf2")
        misc = self.misc_hash or p.misc_hash(mods)
        if misc:
            archives = p.read_fingerprints(os.path.join(mods, "std_archive.md5"))
            level = p.read_fingerprints(
                os.path.join(mods, "levels", self.level, "archive.md5"))
            # The line number in the fingerprint files is the "challenge number". The
            # server picks it when it loads the level and sends it in the level block
            # as the first number.
            # The challenge number arrives in the level block as its first field —
            # a sign plus 31 bits rather than an ordinary u32.
            # The challenge number arrives in the level block; --ordinal overrides it
            # when the options have to be tried.
            ordinal = self.ordinal
            if ordinal < 0:
                ordinal = 0
            s.send_events([p.content_check_event(misc,
                                                 archives[ordinal % len(archives)],
                                                 level[ordinal % len(level)])])
            self._drain(3)
            s.send_events([post_remote(p.NETWORK_CATEGORY, NET_DATABASE_COMPLETE)])
            self._drain(4)

        # Spawning: the team, the kit, the point. In exactly this order and with
        # exactly these events — verified, `ServerGameLogic::spawnPlayer` is called
        # after them.
        for event, value in ((NET_SELECT_TEAM, self.team), (NET_SELECT_KIT, self.kit),
                             (NET_SELECT_SPAWN_GROUP, self.group)):
            s.send_events([post_remote(p.NETWORK_CATEGORY, event, value)])
            self._drain(3)
        self._drain(hold)


def load(path):
    """Reads a sample file: a u32 length per packet, then the bytes."""
    packets = []
    with open(path, "rb") as handle:
        while True:
            head = handle.read(4)
            if len(head) < 4:
                break
            packets.append(handle.read(struct.unpack("<I", head)[0]))
    return packets


def describe(packets):
    """Shows what is in the packets and where exactly the parsing stumbled.

    An event's type is never greater than 69 — anything above that means the
    previous event was read at the wrong length. So next to an unknown type we also
    print the one after which it occurred: that shows at once whose layout has to be
    fixed.
    """
    kinds, unknown, blamed = {}, {}, {}
    for data in packets:
        info = p.walk_events(data)
        if not info:
            continue
        previous = None
        for event in info["events"]:
            if event.get("unknown"):
                kind = event["type"]
                unknown[kind] = unknown.get(kind, 0) + 1
                if kind > 69 and previous:
                    blamed[previous] = blamed.get(previous, 0) + 1
                continue
            kinds[event["class"]] = kinds.get(event["class"], 0) + 1
            previous = event["class"]
    print("packets: %d" % len(packets))
    for name, count in sorted(kinds.items()):
        print("  %-24s %d" % (name, count))
    if unknown:
        print("  not parsed yet: %s" % unknown)
    if blamed:
        print("  went astray after: %s" % blamed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=os.environ.get("BF2_ADDR", "192.168.100.100"))
    parser.add_argument("--port", type=int, default=16567)
    parser.add_argument("--name", default="OpenBF2")
    parser.add_argument("--stage", choices=STAGES, default="world")
    parser.add_argument("--hold", type=float, default=20.0, help="how many seconds to listen")
    parser.add_argument("--out", help="where to write the captured packets")
    parser.add_argument("--show", action="store_true", help="parse and show the contents")
    parser.add_argument("--wait", action="store_true", help="only wait for the server")
    parser.add_argument("--replay", help="parse previously captured packets from a file")
    parser.add_argument("--team", type=int, default=1, help="the team for the spawn stage")
    parser.add_argument("--kit", type=int, default=0, help="the kit for the spawn stage")
    parser.add_argument("--group", type=int, default=1, help="the spawn point for the spawn stage")
    parser.add_argument("--ordinal", type=int, default=-1,
                        help="the line number in the fingerprint files")
    parser.add_argument("--level", default="dalian_plant", help="the level's name for the fingerprint")
    parser.add_argument("--misc-hash", dest="misc_hash",
                        help="the content check's first hash (the server computes it itself)")
    args = parser.parse_args()

    if args.replay:
        describe(load(args.replay))
        return 0

    if args.wait:
        ok = wait_for_server(args.host, args.port)
        print("the server is ready" if ok else "the server does not answer")
        return 0 if ok else 1

    capture = Capture(args.host, args.port, args.name, args.team, args.kit, args.group,
                      args.misc_hash, args.level, args.ordinal)
    capture.run(args.stage, args.hold)

    if args.out:
        with open(args.out, "wb") as handle:
            for packet in capture.packets:
                handle.write(struct.pack("<I", len(packet)))
                handle.write(packet)
        print("wrote %d packets into %s" % (len(capture.packets), args.out))
    if args.show or not args.out:
        describe(capture.packets)
    return 0


if __name__ == "__main__":
    sys.exit(main())
