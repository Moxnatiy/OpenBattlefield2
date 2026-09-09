#!/usr/bin/env python3
"""A probe of BF2's connection protocol: sends a connect request and reads the reply.

The layout comes from the engine (NetServer::_update / handleConnectRequest /
sendConnectAccept / sendConnectDenied in the Linux server):

  the header: 4 bits of type, 8 bits of connection id
  type 1 — the request:  u32 0x1002, u32 version, 1 bit PunkBuster,
                         u32 reconnection token, 32 bytes of password,
                         32 bytes of mod directory
  type 2 — accepted: u8 id, u32 server time, 1 bit PunkBuster
  type 3 — denied:   u32 reason, 1 bit "a directory follows", [32 bytes of directory]

Bits are packed low first — the same as in our BitStream.
"""
import socket
import struct
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
    0x02: "the server is full",
    0x09: "the version does not match",
    0x11: "wrong password",
    0x16: "the address is banned",
    0x17: "the client is too old",
    0x18: "the client is too new",
    0x1E: "no free slots",
    0x1F: "PunkBuster is required",
    0x24: "a different mod directory",
}


# Found by a binary search against a live server: the bytes 15 0C 51 00 are
# 1.5 and build 0x0C51 = 3153, that is exactly bf2-linuxded-1.5.3153.0.
GAME_VERSION = 0x150C5100


def connect_request(version=GAME_VERSION, mod="mods/bf2", password="", token=0, punkbuster=1):
    writer = Writer()
    writer.write(1, 4)          # the type: a connection request
    writer.write(0, 8)          # the connection id: the client has none yet
    writer.write(0x1002, 32)    # the constant the engine checks first
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
        return "silence"
    finally:
        sock.close()

    reader = Reader(data)
    kind = reader.read(4)
    connection = reader.read(8)
    if kind == 2:
        assigned = reader.read(8)
        server_time = reader.read(32)
        pb = reader.read(1)
        return "ACCEPTED: connection %d, server time %d, PunkBuster %d" % (assigned, server_time, pb)
    if kind == 3:
        reason = reader.read(32)
        has_mod = reader.read(1)
        text = DENY.get(reason, "code %d" % reason)
        if has_mod:
            text += " (the server wants %r)" % reader.read_bytes(32).split(b"\0")[0].decode("latin-1")
        return "denied: " + text
    return "type %d, id %d, %d bytes" % (kind, connection, len(data))


if __name__ == "__main__":
    versions = [int(v, 0) for v in sys.argv[1:]] or [0]
    for version in versions:
        print("version %#x -> %s" % (version, probe(version)))


# --- the event stream in a data packet (type 15) ---
#
# The layout from the engine: GameEventManager::processReceivedPacket reads
#   1 bit  "there are events", 8 bits of count, 5 bits ?, 1 bit ?
# then readGameEvent takes the type in N bits, where N is the smallest for which
# (1<<N)-1 covers the event registry's size. On live packets N = 7.
#
# Before that the packet holds another 17 bits of stream framing — not taken apart yet.
EVENT_STREAM_OFFSET = 17
EVENT_TYPE_BITS = 7


def decode_events(data):
    """Parses a data packet and returns a description of its events."""
    r = Reader(data)
    kind = r.read(4)
    r.read(8)
    if kind != 15:
        return "not a data packet (type %d)" % kind

    sequence = r.read(6)
    r.read(6)
    r.read(32)
    r.at += EVENT_STREAM_OFFSET

    if r.read(1) != 1:
        return "seq %d: no events" % sequence
    count = r.read(8)
    r.read(5)
    r.read(1)

    out = ["seq %d: events %d" % (sequence, count)]
    for _ in range(count):
        kind = r.read(EVENT_TYPE_BITS)
        if kind == 1:
            challenge = r.read_bytes(10).split(b"\0")[0].decode("latin-1")
            length = r.read(8)
            mod = r.read_bytes(length).decode("latin-1")
            out.append("  challenge: %r, mod %r" % (challenge, mod))
        else:
            out.append("  an event of type %d (not parsed yet)" % kind)
            break
    return "\n".join(out)


# --- the block with the client's details ---
#
# `ClientInfo::setFromDataBlock` reads from the block:
#   u16 length + string 1 (the player's name)
#   u32 a number
#   1 sign bit + 31 bits of value
#   u16 length + string 2
#   u16 length + string 3
#   1 flag bit
#
# The block itself travels as event type 4 (`DataBlockEvent`): first the header
# (1 bit = 1, then u32 block type and u32 size), then the chunks
# (1 bit = 0, u8 length, bytes). Block type 1 is exactly ClientInfo
# (`GameServer::handleDataBlock`).
CLIENT_INFO_BLOCK = 1


def client_info_blob(name="OpenBF2", second="", third="", number=0, value=0, flag=0):
    w = Writer()
    w.write(len(name), 16)
    for c in name.encode("latin-1"):
        w.write(c, 8)
    w.write(number, 32)
    w.write(1 if value < 0 else 0, 1)
    w.write(abs(value), 31)
    for text in (second, third):
        w.write(len(text), 16)
        for c in text.encode("latin-1"):
            w.write(c, 8)
    w.write(flag, 1)
    return w.data()


# --- the full handshake ---
#
# The order is the same as the client's: request -> acknowledgement -> challenge
# reply -> the ClientInfo block. The server's pings have to be mirrored, otherwise
# it breaks the connection.
EVENT_CHALLENGE_RESPONSE = 2
EVENT_DATA_BLOCK = 4
EVENT_POST_REMOTE = 11

# `PostRemoteEvent` raises an event on the other side. Category 6
# `GameServer::handleEvent` hands to `handleNetworkEvent`, and number 2 in its
# jump table is `clientLoadComplete`.
NETWORK_CATEGORY = 6
NET_LOAD_COMPLETE = 2


def data_packet(conn, seq, ack, events, ack_bits=0xFFFFFFFF, pad=0, batch=0):
    """A data packet: the header and three streams in a fixed order.

    The header is 72 bits: 4 type, 8 connection id, 6 packet number,
    6 acknowledgement, 32 mask, and 16 bits of payload length in bytes.
    The last field was found against a live server: in its 26-byte packet it held
    17, which is exactly 26 − 9 bytes of header.

    Then come the streams: player actions, events, ghosts — in exactly the order
    `ClientConnection::ClientConnection` adds them with `addStreamManager`.

    `batch` is the event batch number in 5 bits. `GameEventManager` puts batches
    into a tree by this number and hands them to the game only in sequence, so the
    count has to start at zero and grow by one with every batch.
    """
    body = Writer()
    body.write(0, 1)               # no player actions
    if not events:
        body.write(0, 1)           # no events
    else:
        body.write(1, 1)
        body.write(len(events), 8)
        body.write(batch & 0x1F, 5)
        body.write(0, 1)
        for event in events:
            body.bits.extend(event)
    body.write(0, 1)               # we send no ghosts
    for _ in range(pad):
        body.write(0, 8)

    size = (len(body.bits) + 7) // 8
    w = Writer()
    w.write(15, 4)
    w.write(conn, 8)
    w.write(seq & 0x3F, 6)
    w.write(ack & 0x3F, 6)
    w.write(ack_bits, 32)
    w.write(size, 16)
    w.bits.extend(body.bits)
    return w.data()


def _event(kind, fill):
    w = Writer()
    w.write(kind, EVENT_TYPE_BITS)
    fill(w)
    return w.bits


def challenge_response_event():
    def fill(w):
        for _ in range(73):
            w.write(0, 8)
        w.write(0, 32)
        w.write(GAME_VERSION, 32)
        w.write(0, 1)
        w.write(0x423, 31)     # BF2's product number
    return _event(EVENT_CHALLENGE_RESPONSE, fill)


def data_block_events(block_type, blob, chunk=200):
    """A block header plus chunks — the same as `DataBlockEvent::serialize`."""
    def header(w):
        w.write(1, 1)
        w.write(block_type, 32)
        w.write(len(blob), 32)
    out = [_event(EVENT_DATA_BLOCK, header)]
    for at in range(0, len(blob), chunk):
        part = blob[at:at + chunk]

        def body(w, part=part):
            w.write(0, 1)
            w.write(len(part), 8)
            for byte in part:
                w.write(byte, 8)
        out.append(_event(EVENT_DATA_BLOCK, body))
    return out


def name_hash(name):
    """The same hash `handleClientInfo` checks on ranked servers."""
    value = 0x1505
    for c in name.encode("latin-1"):
        value = (value * 0x21 ^ (c | 0x20 if 65 <= c <= 90 else c)) & 0xFFFFFFFF
    return value


# --- parsing the events the server sends ---
#
# Every event's layout was taken from the server's code with
# `tools/linuxded/bitfields.py <Class>::deSerialize`: it shows the exact sequence
# of readBits calls together with the bit counts.
def read_create_player(r):
    """CreatePlayerEvent (type 5): 3,4,1,8,16,16,1 bits and 32 bytes of name."""
    out = {
        "team": r.read(3),
        "squad": r.read(4),
        "flag1": r.read(1),
        "id": r.read(8),
        "field16a": r.read(16),
        "field16b": r.read(16),
        "flag2": r.read(1),
    }
    out["name"] = r.read_bytes(32).split(b"\0")[0].decode("latin-1")
    return out


def read_data_block(r):
    """DataBlockEvent (type 4): a block header or a chunk of data."""
    if r.read(1) == 1:
        return {"header": True, "block type": r.read(32), "size": r.read(32)}
    length = r.read(8)
    return {"header": False, "bytes": length, "data": r.read_bytes(length)}


def _float(r):
    return struct.unpack("<f", struct.pack("<I", r.read(32)))[0]


def read_create_object(r):
    """CreateObjectEvent (type 6): two mutually exclusive branches.

    The shape was taken with `bitfields.py --blocks CreateObjectEvent::deSerialize`,
    and the jump's polarity from the code itself (`jne` after the flag is read):

        32  template
        16  network id
         2  a field
         1  a flag
             if 1: 8 bits, and that is all
             if 0: 1 bit -> [position 3x32], 1 bit -> [rotation 3x32]

    A flat list of reads showed every field in a row as though they were always
    there — because of which the parsing went astray on the next event in the packet.
    """
    out = {
        "template": r.read(32),
        "network id": r.read(16),
        "field2": r.read(2),
    }
    if r.read(1) == 1:
        out["field8"] = r.read(8)
        return out
    if r.read(1):
        out["position"] = tuple(round(_float(r), 2) for _ in range(3))
    if r.read(1):
        out["rotation"] = tuple(round(_float(r), 2) for _ in range(3))
    return out


def read_create_spawn_group(r):
    """CreateSpawnGroupEvent (type 57)."""
    return {
        "id": r.read(8),
        "field4": r.read(4),
        "flag1": r.read(1),
        "flag2": r.read(1),
        "flag3": r.read(1),
        "field8a": r.read(8),
        "field8b": r.read(8),
        "field16": r.read(16),
    }


def read_begin_round(r):
    """BeginRoundEvent (type 56): two 32-bit numbers."""
    return {"field1": r.read(32), "field2": r.read(32)}


def read_voip_session(r):
    """VoipSessionEvent (type 54): the voice session's number."""
    return {"session": r.read(16)}


def read_unlock(r):
    """UnlockEvent (type 42): what exactly was unlocked for the player."""
    return {"kind": r.read(2), "player": r.read(8), "id": r.read(4)}


def read_connection_type(r):
    """ConnectionTypeEvent (type 3)."""
    return {"kind": r.read(3)}


def read_destroy_player(r):
    """DestroyPlayerEvent (type 8)."""
    return {"player": r.read(8)}


def read_string_manager(r):
    """StringManagerEvent (type 0): a dictionary string the server shares.

    The layout is visible in `deSerialize`: after a 6-bit length comes the string
    itself, and after it a flag with an optional byte and two more bits.
    We used to read only the first seven bits — and everything further in the packet
    slid, the ghost stream included.

         1  a flag
         6  the string's length in bytes
         N  the string itself
         1  is there another byte -> [8]
         1
         1
    """
    out = {"flag": r.read(1)}
    length = r.read(6)
    out["string"] = r.read_bytes(length).decode("latin-1", "replace")
    if r.read(1) == 1:
        out["field8"] = r.read(8)
    out["bit1"] = r.read(1)
    out["bit2"] = r.read(1)
    return out


def read_voip_on_off(r):
    """VoipOnOffEvent (type 35): who is speaking and whether it is on."""
    return {"player": r.read(8), "on": r.read(1)}


def read_post_remote(r):
    """PostRemoteEvent (type 11): \"raise this event on your side\"."""
    out = {"category": r.read(4), "event": r.read(32), "delay": r.read(32)}
    out["data"] = r.read_bytes(r.read(8))
    return out


def read_invite(r):
    """InviteEvent (type 25): an invitation to a squad."""
    return {"from": r.read(8), "to": r.read(8), "squad": r.read(8), "flag": r.read(1)}


def read_rank(r):
    """RankEvent (type 26): the player's rank."""
    return {"kind": r.read(2), "rank": r.read(6), "field32": r.read(32), "player": r.read(8)}


def read_commander(r):
    """CommanderEvent (type 19): 4, 8, 1 bit and 15 bits in both branches."""
    return {"kind": r.read(4), "player": r.read(8), "flag": r.read(1),
            "field15": r.read(15)}


EVENT_READERS = {
    19: ("CommanderEvent", read_commander),
    0: ("StringManagerEvent", read_string_manager),
    11: ("PostRemoteEvent", read_post_remote),
    25: ("InviteEvent", read_invite),
    26: ("RankEvent", read_rank),
    35: ("VoipOnOffEvent", read_voip_on_off),
    3: ("ConnectionTypeEvent", read_connection_type),
    4: ("DataBlockEvent", read_data_block),
    8: ("DestroyPlayerEvent", read_destroy_player),
    42: ("UnlockEvent", read_unlock),
    5: ("CreatePlayerEvent", read_create_player),
    6: ("CreateObjectEvent", read_create_object),
    54: ("VoipSessionEvent", read_voip_session),
    56: ("BeginRoundEvent", read_begin_round),
    57: ("CreateSpawnGroupEvent", read_create_spawn_group),
}


def walk_events(data):
    """Walks a data packet and parses as many events as we can."""
    r = Reader(data)
    if r.read(4) != 15:
        return None
    r.read(8)
    seq, ack, bits = r.read(6), r.read(6), r.read(32)
    size = r.read(16)
    r.read(1)                      # the player action stream
    if r.read(1) != 1:
        # There are no events — but there are ghosts after them, and it is in packets
        # like these that the server mostly sends them. We used to return here at once.
        return {"seq": seq, "size": size, "events": [], "ghosts": read_ghosts(r)}
    count, batch, repeat = r.read(8), r.read(5), r.read(1)

    events = []
    complete = True
    for _ in range(count):
        kind = r.read(EVENT_TYPE_BITS)
        name, reader = EVENT_READERS.get(kind, (None, None))
        if reader is None:
            events.append({"type": kind, "unknown": True})
            complete = False
            break
        before = r.at
        events.append({"type": kind, "class": name, "bit": before, **reader(r)})

    out = {"seq": seq, "size": size, "batch": batch, "events": events}
    if complete:
        out["ghosts"] = read_ghosts(r)
    return out


# The width of the length field in a ghost record. In the engine it is computed on
# the fly (`GhostManager` keeps it in field 0x4298), and on the wire it turned out
# to be exactly eleven bits: with it all 132 packets of the sample parse to the
# last byte, with any other not one does.
GHOST_LENGTH_BITS = 11


def read_ghost_record(r):
    """One ghost stream record (`GhostManager::readData`).

        2  kind
       16  network id
    kind 1: 1 bit, 11 bits of content length, the content itself
    kind 0: nothing more
    kind 3: the object disappears (the engine looks it up in NetworkManager)
    kind 2: the engine treats this as a stream error
    """
    kind = r.read(2)
    out = {"kind": kind, "id": r.read(16)}
    if kind == 1:
        out["flag"] = r.read(1)
        length = r.read(GHOST_LENGTH_BITS)
        out["content bits"] = length
        out["content"] = r.at
        r.at += length
    return out


def read_ghosts(r):
    """The ghost stream at the packet's tail (`GhostManager::processReceivedPacket`).

    1 bit "has data"; then 32 bits of time (divided by 30), 8 bits of record count
    and 1 bit "there is a controlled-object state".
    """
    if r.read(1) != 1:
        return None
    out = {
        "time": r.read(32),
        "record count": r.read(8),
        "control object": r.read(1),
    }
    # The controlled-object state comes before the records and is not parsed yet,
    # so in such packets we do not read the records.
    if not out["control object"]:
        out["records"] = [read_ghost_record(r) for _ in range(out["record count"])]
    return out


# --- the content check ---
#
# `ContentCheckEvent` (type 46) carries three 128-bit hashes. The server compares
# them in `GameServer::onContentCheckEvent` against the tables it read from the
# files std_archive.md5, std_archive_mod.md5, bst_archive.md5 and
# bst_archive_mod.md5, taking the line by the number from
# `MapInfo::getChallengeOrdinal()`.
#
# The files are simple "number + md5" pairs, and the client has the same ones, so
# we simply read the line we need.
EVENT_CONTENT_CHECK = 46

# The content check's first hash both sides compute themselves:
# `ChecksumContext::runMiscChecksum` takes an MD5 over four of the mod's files, in
# exactly this order. The names were found in the function itself.
MISC_CON_FILES = ("ClientArchives.con", "ServerArchives.con",
                  "Init.con", "GameLogicInit.con")


def misc_hash(mod_dir):
    """MD5 over the mod's .con files — the content check's first hash."""
    import hashlib
    import os

    digest = hashlib.md5()
    for name in MISC_CON_FILES:
        path = os.path.join(mod_dir, name)
        if not os.path.exists(path):
            return None
        with open(path, "rb") as handle:
            digest.update(handle.read())
    return digest.hexdigest()


def read_fingerprints(path):
    """A number -> the md5 from a fingerprint file.

    The archive files have the form "number + md5", while the level's file also has
    a name in front: "dalian_plant 0 <md5>". We read both the same way.
    """
    out = {}
    with open(path) as handle:
        for line in handle:
            parts = line.split()
            if len(parts) == 2 and parts[0].isdigit():
                out[int(parts[0])] = parts[1]
            elif len(parts) == 3 and parts[1].isdigit():
                out[int(parts[1])] = parts[2]
    return out


def content_check_event(first, second, third):
    """Three hashes, 16 bytes each."""
    def fill(w):
        for value in (first, second, third):
            raw = bytes.fromhex(value) if isinstance(value, str) else value
            assert len(raw) == 16, "a hash has to be 16 bytes"
            for byte in raw:
                w.write(byte, 8)
    return _event(EVENT_CONTENT_CHECK, fill)


def parse_map_info(block):
    """Parses the level block.

    The first field is not an ordinary u32 but "1 sign bit + 31 bits of value"
    (`MapInfo::setFromDataBlock` reads it exactly that way). Because of that the
    bytes 01 00 00 00 mean zero rather than one — and that is the challenge number
    the lines in the fingerprint files are taken by.
    """
    r = Reader(block)
    sign = r.read(1)
    ordinal = r.read(31)
    if sign:
        ordinal = -ordinal

    def text():
        length = r.read(16)
        return r.read_bytes(length).decode("latin-1", "replace")

    level = text()
    mode = text()
    size = r.read(16)
    return {"challenge number": ordinal, "level": level, "mode": mode, "size": size}
