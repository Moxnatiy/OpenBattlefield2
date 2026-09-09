# Netcode: the first slice

Status: **BitStream implemented** — `src/net`. The protocol is taken apart
at the level of primitives and headers; the connection logic is still
ahead.

## The main find: the Linux server has symbols

`bf2-linuxded-1.5.3153.0-installer.sh` unpacks without being run (it is a
makeself archive; the payload starts after line 375). Inside:

```
bin/ia-32/bf2    16 MB   ELF 32-bit, with debug_info, not stripped
bin/amd-64/bf2   18 MB   ELF 64-bit, with debug_info, not stripped
```

**68 921 symbols, 16 830 functions, full DWARF** — and the same version
1.5 as `BF2.exe`. That is incomparably better than reversing a Windows
binary without symbols:

```
dice::hfe::io::BitStream::writeUnsigned(unsigned int, unsigned int, unsigned int)
dice::hfe::io::BitStream::writeCompressedVector(Vec3 const&, Vec3 const&, float)
dice::hfe::io::BitStream::writeUnitQuaternion3Comp(Quat const&, int)
dice::hfe::io::PacketBuffer::pushBack(dice::hfe::io::Packet&)
dice::hfe::io::NetServer::getServerAddress() const
```

The binary was imported into Ghidra (`ghidra_projects/OpenBF2`, program
`bf2`); DWARF gives not only names but types and structure layouts.

**Conclusion:** do all further netcode work on the Linux server and keep
`BF2.exe` for checking the client side.

## What BitStream can do in the original

The full method list from the symbols:

| Group | Methods |
|---|---|
| Basics | `writeBits`, `readBits`, `writeUnsigned`, `writeSigned`, `skipBits` |
| Vectors | `write/readCompressedVector`, `...Vector2`, `...HighCompressedVector` |
| Normals | `write/readNormalVector`, `shrinkNormalVector` |
| Quaternions | `write/readUnitQuaternion`, `...3Comp` |
| State | `setCompressionVector`, `resetCompressionVector`, `m_relativeCompressionEnabled` |

Three static tables — `m_compressionVectorBitTable`,
`m_compressionVectorBitTable2`, `m_highCompressionVectorBitTable` — say how
many bits go to a vector component. Their values sit in the data section
and can be read directly, without decompilation.

So positions are sent **relative to a reference vector** at variable
precision, and rotations as three-component quaternions. That is why BF2's
traffic is so dense.

## What is already done

`obf2::net::BitStream` implements the base level: `writeBits`/`readBits`,
strings, service headers. The bit layout is covered by a test and matches
[Refractor-2-BitStream-Emulator](https://github.com/matthias-hoste/Refractor-2-BitStream-Emulator)
(a working handshake with a real server, C#):

**Within each byte the low bits come first.** Writing `0b101` into three
bits gives the byte `0x05`, not `0xA0`. Get that wrong and the protocol
falls apart on the very first packet, which makes it the most important
test in the set.

The service headers:

```
main:      4 bits type + 8 bits subtype
extended:  6 bits type + 6 bits id + 32 bits sequence number
```

Packet types (from the same project, confirmed by working with a live
server):

| Code | Packet |
|---:|---|
| 1 | ConnectionRequest |
| 2 | ConnectionAccept |
| 3 | ConnectionDenied |
| 4 | ConnectionAcknowledge |
| 5 | Disconnect |
| 7 | PingRequest |
| 8 | PingResponse |
| 15 | Data |

Unlike the original, our reader **checks bounds**: a packet comes off the
network and running past the buffer is not acceptable here. The tests
cover that separately.

## Replication partly lives in open data

`Common/Networkables.con` describes how objects are synchronised:

```
NetworkableInfo.createNewInfo HandFireArmsInfo
NetworkableInfo.setPredictionMode PMNone
NetworkableInfo.setForceNetworkableId 1

NetworkableInfo.createNewInfo ProjectileInfo
NetworkableInfo.setPredictionMode PMLinear
NetworkableInfo.setBasePriority c_NIGhostAlways
```

So the prediction mode (`PMNone`, `PMLinear`), the priority and the fixed
network ids are given by **data**, not by code. The same approach as with
levels: part of the protocol can be recovered without any reversing.

## Next

1. Vector and quaternion compression — the bit tables can be read from the
   ELF's data.
2. The handshake: ConnectionRequest → Accept → Acknowledge.
3. `NetworkableInfo` as a replication registry on top of `ObjectTemplate`.
4. Compatibility with original servers — **not decided**, see
   `docs/TODO.md`.
