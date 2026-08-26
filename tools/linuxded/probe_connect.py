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


# --- блок відомостей про клієнта ---
#
# `ClientInfo::setFromDataBlock` читає з блока:
#   u16 довжина + рядок 1 (ім'я гравця)
#   u32 число
#   1 біт знак + 31 біт значення
#   u16 довжина + рядок 2
#   u16 довжина + рядок 3
#   1 біт прапорець
#
# Сам блок їде подією типу 4 (`DataBlockEvent`): спершу заголовок
# (1 біт = 1, потім u32 тип блока і u32 розмір), далі шматки
# (1 біт = 0, u8 довжина, байти). Тип блока 1 — це саме ClientInfo
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


# --- повне рукостискання ---
#
# Порядок такий самий, як у клієнта: запит → підтвердження → відповідь на
# виклик → блок ClientInfo. Пінги від сервера треба віддзеркалювати, інакше
# він рве з'єднання.
EVENT_CHALLENGE_RESPONSE = 2
EVENT_DATA_BLOCK = 4
EVENT_POST_REMOTE = 11

# `PostRemoteEvent` піднімає подію на тому боці. Категорію 6
# `GameServer::handleEvent` віддає в `handleNetworkEvent`, а номер 2 у
# його таблиці переходів — це `clientLoadComplete`.
NETWORK_CATEGORY = 6
NET_LOAD_COMPLETE = 2


def data_packet(conn, seq, ack, events, ack_bits=0xFFFFFFFF, pad=0, batch=0):
    """Пакет даних: заголовок і три потоки в сталому порядку.

    Заголовок — 72 біти: 4 тип, 8 номер з'єднання, 6 номер пакета,
    6 підтвердження, 32 маска, і 16 бітів — довжина корисної частини в
    байтах. Останнє поле знайдено на живому сервері: у його пакеті на
    26 байтів там стояло 17, а це рівно 26 − 9 байтів заголовка.

    Далі йдуть потоки: дії гравця, події, привиди — саме в такому порядку
    їх додає `ClientConnection::ClientConnection` через `addStreamManager`.

    `batch` — номер пачки подій у 5 бітах. `GameEventManager` складає пачки
    в дерево за цим номером і віддає їх грі лише поспіль, тож рахунок має
    починатися з нуля й рости на одиницю з кожною пачкою.
    """
    body = Writer()
    body.write(0, 1)               # дій гравця немає
    if not events:
        body.write(0, 1)           # подій немає
    else:
        body.write(1, 1)
        body.write(len(events), 8)
        body.write(batch & 0x1F, 5)
        body.write(0, 1)
        for event in events:
            body.bits.extend(event)
    body.write(0, 1)               # привидів не шлемо
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
        w.write(0x423, 31)     # номер продукту BF2
    return _event(EVENT_CHALLENGE_RESPONSE, fill)


def data_block_events(block_type, blob, chunk=200):
    """Заголовок блока плюс шматки — так само, як `DataBlockEvent::serialize`."""
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
    """Той самий хеш, що звіряє `handleClientInfo` на ranked-серверах."""
    value = 0x1505
    for c in name.encode("latin-1"):
        value = (value * 0x21 ^ (c | 0x20 if 65 <= c <= 90 else c)) & 0xFFFFFFFF
    return value


# --- розбір подій, які шле сервер ---
#
# Розкладку кожної події знято з коду сервера через
# `tools/linuxded/bitfields.py <Клас>::deSerialize`: там видно точну
# послідовність викликів readBits із кількістю бітів.
def read_create_player(r):
    """CreatePlayerEvent (тип 5): 3,4,1,8,16,16,1 бітів і 32 байти імені."""
    out = {
        "команда": r.read(3),
        "загін": r.read(4),
        "прапорець1": r.read(1),
        "номер": r.read(8),
        "поле16a": r.read(16),
        "поле16b": r.read(16),
        "прапорець2": r.read(1),
    }
    out["ім'я"] = r.read_bytes(32).split(b"\0")[0].decode("latin-1")
    return out


def read_data_block(r):
    """DataBlockEvent (тип 4): заголовок блока або шматок даних."""
    if r.read(1) == 1:
        return {"заголовок": True, "тип блока": r.read(32), "розмір": r.read(32)}
    length = r.read(8)
    return {"заголовок": False, "байтів": length, "дані": r.read_bytes(length)}


def _float(r):
    return struct.unpack("<f", struct.pack("<I", r.read(32)))[0]


def read_create_object(r):
    """CreateObjectEvent (тип 6): дві взаємно виключні гілки.

    Будову знято через `bitfields.py --blocks CreateObjectEvent::deSerialize`,
    а полярність переходу — з самого коду (`jne` після читання прапорця):

        32  шаблон
        16  мережевий номер
         2  поле
         1  прапорець
             якщо 1: 8 бітів, і на цьому все
             якщо 0: 1 біт -> [позиція 3x32], 1 біт -> [поворот 3x32]

    Плаский список читань показував усі поля поспіль, ніби вони завжди
    є, — через це розбір збивався на наступній події в пакеті.
    """
    out = {
        "шаблон": r.read(32),
        "мережевий номер": r.read(16),
        "поле2": r.read(2),
    }
    if r.read(1) == 1:
        out["поле8"] = r.read(8)
        return out
    if r.read(1):
        out["позиція"] = tuple(round(_float(r), 2) for _ in range(3))
    if r.read(1):
        out["поворот"] = tuple(round(_float(r), 2) for _ in range(3))
    return out


def read_create_spawn_group(r):
    """CreateSpawnGroupEvent (тип 57)."""
    return {
        "номер": r.read(8),
        "поле4": r.read(4),
        "прапорець1": r.read(1),
        "прапорець2": r.read(1),
        "прапорець3": r.read(1),
        "поле8a": r.read(8),
        "поле8b": r.read(8),
        "поле16": r.read(16),
    }


def read_begin_round(r):
    """BeginRoundEvent (тип 56): два 32-бітних числа."""
    return {"поле1": r.read(32), "поле2": r.read(32)}


def read_voip_session(r):
    """VoipSessionEvent (тип 54): номер сеансу голосового зв'язку."""
    return {"сеанс": r.read(16)}


def read_unlock(r):
    """UnlockEvent (тип 42): що саме відкрито гравцеві."""
    return {"вид": r.read(2), "гравець": r.read(8), "номер": r.read(4)}


def read_connection_type(r):
    """ConnectionTypeEvent (тип 3)."""
    return {"вид": r.read(3)}


def read_destroy_player(r):
    """DestroyPlayerEvent (тип 8)."""
    return {"гравець": r.read(8)}


def read_string_manager(r):
    """StringManagerEvent (тип 0): службова подія рядкового словника.

    Будова з `--blocks`: 1 біт, 6 бітів, рядок; далі ще прапорець, за
    яким може йти 8-бітне поле, і два біти в кінці.
    """
    out = {"прапорець1": r.read(1), "поле6": r.read(6)}
    if out["прапорець1"] == 0 and out["поле6"] == 0:
        return {"порожня": True}
    out["решта"] = "ще не розібрано"
    return out


def read_voip_on_off(r):
    """VoipOnOffEvent (тип 35): хто говорить і чи ввімкнено."""
    return {"гравець": r.read(8), "увімкнено": r.read(1)}


def read_post_remote(r):
    """PostRemoteEvent (тип 11): «підніми в себе оцю подію»."""
    out = {"категорія": r.read(4), "подія": r.read(32), "затримка": r.read(32)}
    out["дані"] = r.read_bytes(r.read(8))
    return out


def read_invite(r):
    """InviteEvent (тип 25): запрошення до загону."""
    return {"від": r.read(8), "кому": r.read(8), "загін": r.read(8), "прапорець": r.read(1)}


def read_rank(r):
    """RankEvent (тип 26): звання гравця."""
    return {"вид": r.read(2), "звання": r.read(6), "поле32": r.read(32), "гравець": r.read(8)}


def read_commander(r):
    """CommanderEvent (тип 19): 4, 8, 1 біт і 15 бітів в обох гілках."""
    return {"вид": r.read(4), "гравець": r.read(8), "прапорець": r.read(1),
            "поле15": r.read(15)}


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
    """Проходить пакет даних і розбирає стільки подій, скільки вміємо."""
    r = Reader(data)
    if r.read(4) != 15:
        return None
    r.read(8)
    seq, ack, bits = r.read(6), r.read(6), r.read(32)
    size = r.read(16)
    r.read(1)                      # потік дій гравця
    if r.read(1) != 1:
        return {"seq": seq, "події": []}
    count, batch, repeat = r.read(8), r.read(5), r.read(1)

    events = []
    complete = True
    for _ in range(count):
        kind = r.read(EVENT_TYPE_BITS)
        name, reader = EVENT_READERS.get(kind, (None, None))
        if reader is None:
            events.append({"тип": kind, "невідома": True})
            complete = False
            break
        before = r.at
        events.append({"тип": kind, "клас": name, "біт": before, **reader(r)})

    out = {"seq": seq, "розмір": size, "пачка": batch, "події": events}
    if complete:
        out["привиди"] = read_ghosts(r)
    return out


def read_ghosts(r):
    """Потік привидів у хвості пакета (`GhostManager::processReceivedPacket`).

    1 біт «є дані»; далі 32 біти часу (ділиться на 30), 8 бітів кількості
    записів і 1 біт «є стан керованого об'єкта».
    """
    if r.read(1) != 1:
        return None
    return {
        "час": r.read(32),
        "записів": r.read(8),
        "керований об'єкт": r.read(1),
    }
