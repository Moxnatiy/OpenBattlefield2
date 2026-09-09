# Fonts and localisation

Status: **implemented** — `src/font`, `src/loc`. Both formats turned out
to be textual; no reversing was needed.

## Font: `.dif` + `.dds`

A pair of files in `Fonts_client.zip`, with directories per resolution
(`800/`) and per language (`Chinese/800/`). The `.dif` holds metrics, the
`.dds` next to it is the glyph atlas.

```
header
2                          version
Helvetica LT CondensedBold name
128                        atlas width
128                        atlas height
13.000000                  size
glyphs
328
65<TAB>0.066667<TAB>4.600000<TAB>1.266667<TAB>0<TAB>35<TAB>7<TAB>40<TAB>14
...
kerning
88
65<TAB>84<TAB>-0.466667
```

The glyph fields are: **code, left bearing, width, right bearing, vertical
offset, left, top, right, bottom**.

That the last four are a rectangle in the atlas is visible straight from
the data: `!` gives 1×7, `"` 2×3, `A` 5×7, `™` 7×4. And that the fifth
field is a vertical offset is shown by punctuation: the comma and the full
stop have 5 (they sit low), `$` has −1 (it sticks up), letters have 0.

Kerning is triples of `character, character, adjustment`, with real Unicode
code points (8217 is the typographic apostrophe).

## Two traps

**The text is UTF-8 and must not be read byte by byte.** Localisation
strings contain non-ASCII characters, and read bytewise `People’s` turns
into `Peopleâs`. The glyph sits under the real code point (8217), not
under the first byte.

**16-bit DDS had to be expanded to 8-bit.** Font atlases are stored as
`A4R4G4B4`. In DDS the channel order is given by masks, while the names of
packed 16-bit formats in graphics APIs mean their own order — the glyphs
came out yellow and full of holes. Expanding to `B8G8R8A8` on load settles
the question; these are 345 small interface textures, so the cost is nil.

## Localisation: `.utxt`

`Localization/<language>/*.utxt`, **UTF-16LE with a BOM**:

```
KEY<spaces>\x1B\x1B value \x1B\x1B CRLF
```

Two ESC characters frame the value on both sides. The keys are the same
ones that turn up in `.con`: `HUD_INGAME_QUIT`, `WEAPON_NAME_ammobag`,
`LOADINGSCREEN_MAPDESCRIPTION_dalianplant`.

There are several files per language (base, patch, add-on) — later ones
override earlier ones. The English set has **3777 lines**.

A missing key is not invented: the key itself is returned — exactly how it
looks in the original game.

## Where this already works

- **The loading screen**: `Info/loadmap.png`, the map name and **the map
  description** by the key from the `<briefing locid="...">` attribute —
  the same text the game shows, word-wrapped.
- **The menu**: the game's own `mainMenu.swf` gets its strings from the
  same lexicon through the bridge (docs/research/11-ruffle-menu.md).

```bash
./build/macos-arm64-debug/src/app/openbf2 --screen loading
```

The interface is drawn by a **separate pipeline**: no depth, alpha
blending, no lighting. Without it the glyphs would be opaque rectangles
and the text would not pass the depth test against the background at all.
