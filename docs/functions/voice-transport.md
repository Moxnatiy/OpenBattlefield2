# Voice chat on the wire: the datagrams and the codec

**Status:** the datagrams and the codec are ported (`net/retail_voice.h`,
`tests/test_voice_transport.cpp`). Nothing sends or plays voice yet: the client
that opens the relay socket, the microphone and a stream per speaker comes with
the session's own voice and mute work. Source: `BF2.exe` 1.50, read by address
with `objdump` (the addresses below).

The voice does not ride the game's own connection. It goes to a separate relay
(the server's voice address and port, from the server's options) as plain UDP
datagrams, each one a frame of narrowband Speex audio with a short trailer.

## The codec

| what | value | where |
|---|---|---|
| frame | 160 samples | `0x68b880` returns `0xa0` |
| sample rate | 8000 Hz | Speex's narrowband mode, which the 160-sample frame belongs to |
| server level → index | 1 → 4, 2 → 3, 3 → 2, 4 → 1, 5 → 0; any other → 3 | `0x736d80`, its jump table at `0x736de8`; the default `ecx = 3` at `0x736d90` |
| index → Speex quality | 0 → 10, 1 → 7, 2 → 4, 3 → 2, 4 → 1 | `0x65f080` |
| frame size | bitrate / 50 / 8, truncated | `0x68b804` asks ctl `0x13` (`SPEEX_GET_BITRATE`); `0x68b809` divides (`0x51eb851f`, `sar 4` is / 50; then / 8 rounding toward zero) and keeps it at `0x9fe9a0`; `0x68b890` reads it back |

So level 1 is the poorest quality and level 5 the best, and a level outside
1..5 gets level 2's codec. With Speex 1.2.1 the frame sizes come out at 9, 14,
20, 37 and 61 bytes for levels 1..5 (the test checks them).

`0x65f0bf` calls `0x68b7b0` with the quality (the codec's set-up), then
`0x68b880` and `0x68b890` for the frame's samples and bytes, and installs
`0x68b8a0` as a callback; those three are not opened further.

The encoder is Speex's floating-point one: it takes the 16-bit samples as floats
of the same scale. The decoder is `speex_decode_int`. Speex 1.2.1 is fetched at
configure time (`cmake/speex.cmake`, BSD 3-clause).

## The datagrams

Little-endian throughout.

### Outgoing (client → relay)

| bytes | what | where |
|---|---|---|
| n × frame | the encoded audio | — |
| 2 | the frame stamp | appended at `0x45155b`, then the datagram goes to the socket object's slot `0x6c` (`0x45157f`) |
| 1 | `1`, only on the alternate set | not established |
| 2 | the voice session | not established |
| 1 | frame type: `1` on the default set, `2` on the alternate | not established |

The keep-alive that tells the relay where the listener is: `session (2), 6`.

**Where the routing bytes are written is not established**: slot `0x6c` is not
opened. Their values come from our earlier relay work and are not confirmed by a
read of the client.

### Incoming (relay → client)

| bytes | what | where |
|---|---|---|
| n × frame | the encoded audio | — |
| 2 | the speaker's frame stamp | read at `0x45168a` |
| 2 | the speaker's player id | read at `0x45165b` |

The client receives into a `0x400`-byte buffer (`0x45148d`), so a longer datagram
cannot be a frame. After reading the speaker, it asks the player manager for that
player (`0xa08f60`, slot `0x20`, at `0x451675`) and drops the frame when the player
is missing or when the player's slot `0x274` answers true (`0x451680`); what slot
`0x274` tests is **not established** (a mute is the obvious guess and is not
checked). Then `0x45169f` hands the audio, the stamp and the player on
(`0x58dd90`, not opened).

## What is not here

- Who opens the relay and when, the push-to-talk keys, the channel rules (who
  may talk on which set), the capture device — the session's voice work.
- The jitter handling on the receiving side (`0x58dd90`).
- The muting event (`VoipPlayerMuteEvent`) and slot `0x274` above.
