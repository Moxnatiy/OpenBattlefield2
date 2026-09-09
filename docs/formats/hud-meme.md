# MemeFile 2.0 — the HUD layer graph

Extensionless files at the root of `Menu_client.zip`: `Global`, `Ingame`,
`TopLayer`, `BottomLeftStatic`, `BottomRightAnimate`, `TopLeft`, `Left`,
`Top`, `TopRight`, `PreGlobal`. This is DICE's binary scene graph
(`dice::meme::*`), not `.con`.

The format is **fully taken apart**: all 12 files read to the last byte,
and not one class is left without a field list
(`tools/meme_read.py --check`).

## Where it came from

Nothing was guessed. Next to the editor (`MemeEdit.exe`) in the mod's
directory sit `MemeDll.dll` and `MemeBf.dll`, and unlike the game they
**export full C++ symbols** — 2124 names. The layout was read straight out
of the reader's code:

| what | where it was read |
|---|---|
| header and dictionary | `ClassIStream` (checks `MemeFile 2.0`, then strings up to an empty one) |
| a string in the dictionary | `IStream::streamStaticString` — **a one-byte length** |
| the root | `Object::loadNew` — **only a two-byte class number** |
| a nested object | `Object::load` — size, name, class, fields |
| a "string" in the class stream | `ClassIStream::streamStaticString` — **a two-byte index**, 0 = empty |
| a class's fields | its `onStream`, each field is passed its name as a string |
| field order | `onStream` starts with the parent's — inherited fields come first |

## Layout

    file    := version string, {string}, empty string, root
    root    := u16 class number, fields
    object  := u32 size, u16 name, u16 class, fields

The size is measured **from itself**: `Object::save` remembers the
position, writes a zero, and at the end comes back and puts
`end − position` there. The engine uses it to skip an object it does not
know, and so do we.

A list (`streamList`) has no counter: the objects follow one another and
the owner's size gives the edge.

## Field types

The offsets are slots in `IStream`'s method table, taken from the
library's own `.rdata`, so the mapping is exact:

| offset | method | bytes |
|---|---|---|
| 0x1c / 0x20 | Ubyte / Sbyte | 1 |
| 0x24 / 0x28 | Ushort / Sshort | 2 |
| 0x2c / 0x30 | Ulong / Slong | 4 |
| 0x34 | Float | 4 |
| 0x38 | Bool | 1 |
| 0x3c / 0x5c | Int / Index | 4 |
| 0x48 | Wchar | 2 |
| 0x4c / 0x50 / 0x54 | Picture / Font / Sound | a string with a one-byte length |
| 0x58 | List | objects until the owner ends |
| 0x60..0x88 | Object, Event, Action, Data, Effect, Function, Node, Style, Tree, ChildNode, NextNode | a nested object |

Pairs and rectangles go past the method table, through a direct call:
`Coordinate2::stream` — two floats, `Rectangle::stream` — four,
`Color::stream` — Red, Green, Blue, Alpha.

The full list of classes and fields is in `meme-classes.md`
(`tools/meme_types.py --emit`).

## What this gives the HUD

The corner layers are only mentioned as groups in the `.con` files and are
never positioned there. The anchor is here:

    BottomLeft   TransformNode  X=-1   Y=563  400x64
    BottomRight  TransformNode  X=401  Y=563  400x64
    MiddleLeft   TransformNode  X=0    Y=0    400x600

`TransformNode::iteratePaint` **adds** X and Y to the parent's rectangle
and scales nothing — so this is a plain offset.

On the left that agrees with the interface data: `BottomLeftBar` is
described from −103 to 297, exactly within the layer's four hundred. On
the right it does not yet: the `BottomRightPrimaryAmmo` nodes have y from
5 to 103 — they belong to another layer, 600x100, which sits in the file
next to `BottomRight/BottomRight_XPos`. Where exactly it goes is not yet
proven.

The values themselves read too:

    BottomLeft/BottomLeft_XPos    -295
    BottomRight/BottomRight_XPos   503   (old 201, new 503)
    Kit/ShowIngame                 1

These are the movement variables: the layer slides away on them when the
game hides the interface.
