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

## The voice client (`app::MatchVoice`)

The round's voice, once a frame from the session.

**When it runs.** The relay is opened when all of these are in place:

* the server's match options turn voice on and name the relay's port;
* the server has given us a session id (`VoipSessionEvent`, 54);
* we hold a player.

When the voice starts it sends `VoipOnOffEvent` (35) with the player in 8 bits
and "on" in 1 (`VoipOnOffEvent::serialize`, Linux 0x435090). It sends "off" when
the voice stops.

**The relay's address.** An address of 1 in the match options is taken as the
game server's own host. That is inferred from live servers, not read in the
binary.

**The keys.** Both come from `mods/bf2/Settings/Controls.con`:

* B, `c_GIVoipPushToTalk` (line 245), talks to the squad;
* V, `c_GIVoipUseLeaderChannel` (line 239), takes a squad leader to the
  commander on the first alternate set.

Which set the commander himself talks on is not established. Ours talks on the
default set.

**Frames.** One per 20 ms, from 160 samples at 8000 Hz. The stamp counts frames
from the voice's start.

**Playback.**

* Each speaker gets his own decoder and SDL stream.
* A muted player's frames are dropped and his stream is closed (voip-mute.md).
* Each frame stamps `RemoteWorld::noteVoice`, the talk mark on the map
  (hud-map.md, kind 1).

**Not measured** (ours):

* the keep-alive's period, 2 s;
* the 5 s after which a silent speaker loses his stream;
* the per-frame budgets of 8 frames captured and 32 datagrams read;
* the ten frames of queued playback past which a stream is cleared.

Test: `test_match_voice`, two clients on a loopback relay.

## What is not here

- The channel rules on the server's side (who hears which set).
- The jitter handling on the receiving side (`0x58dd90`). Ours drops a frame
  older than the last one played.
- Slot `0x274` above, and slot 0x510 of the mute (voip-mute.md).
