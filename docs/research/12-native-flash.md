# Our own Flash player: gameswf r777 grown into the menu's player

The menu is to be played natively, in C++, by our own player — not by Ruffle
(docs/research/11-ruffle-menu.md), which stays only until this one plays the
menu. The decision of 2026-11-09.

**Done, 2026-11-12:** this player runs the menu and the loading screen, and Ruffle
is removed — `src/flash`, `tools/ruffle_*` and the `--ruffle` switch are gone.
The comparisons with Ruffle below were made while it was still there.

## The base: gameswf at tu-testbed revision 777

`SwiffPlayer.dll` is gameswf with DICE's changes. Which gameswf was measured,
not assumed: every `log_error`/`log_msg` text of a revision's `gameswf/*.cpp`
looked up in the DLL —

| revision | its messages | found in SwiffPlayer.dll | found only in this revision and the DLL |
|---|---|---|---|
| r777 (2006-02-27, "checking in so I can make a clean gameswf file release") | 511 | 150 | **40** |
| r1714 (the trunk we had in `reference/gameswf`) | 433 | 112 | 2 |

So the DLL was built from the r777 snapshot (B2BF/OpenBF2 says the same:
"tu-testbed snapshot of 27 February 2006"). It is public domain, so its code is
ours to take (CLAUDE.md, rule 4). It lives in `third_party/gameswf`, exported by

```
svn export https://svn.code.sf.net/p/tu-testbed/code/trunk/tu-testbed/{gameswf,base}@777
```

without the OpenGL/D3D/SDL backends and the stand-alone programs; five files
converted from Latin-1 to UTF-8. Every local change is marked
`OpenBattlefield2:` at its place.

## Making it build on arm64

| file | change |
|---|---|
| `compatibility_include.h` | the system allocator instead of the bundled dlmalloc |
| `base/tu_types.h` | arm64 and x86-64 are little-endian; no SDL for the integer types |
| `base/utility.h` | `fmax`, `fmin`, `log2` dropped: `<cmath>` has them |
| `base/container.h` | **the 64-bit hash defect**: `add` cut the hash to 32 bits and the entry's constructor sign-extended it through an `int`, so the stored hash never matched the lookup's and nothing was found (the same defect docs/research/10 found in r1714) |
| `gameswf_shape.cpp` | `DEBUG_DISPLAY_SHAPE_PATHS` off (it pulls OpenGL in) |
| `gameswf_action.cpp` | two pointer casts to `unsigned` printed with `%p` |
| `gameswf_button.cpp` | `return false` from a function returning a pointer |
| `CMakeLists.txt` | zlib by name (`ZLIB::ZLIB` puts the SDK ahead of libc++'s headers) |

## The measure: `swf_probe`

`tools/swf_probe` plays a movie read through our file system (the archives
mounted as the engine mounts them) with a renderer that counts, and lists the
library's complaints most frequent first.

```
swf_probe Menu/External/FlashMenu/mainMenu.swf 30
```

### First run

It stopped on frame 1: `as_environment::drop(1)` on an empty stack, from the
movie's load event. The game plays this movie, so SwiffPlayer's VM tolerates an
underflow; B2BF lists "tolerant stack underflow" among DICE's changes. Ours now
answers `undefined` from an empty stack and never sizes it below zero
(`gameswf_action.h`). Where SwiffPlayer.dll does it is **not read**.

### Second run — 30 frames played

```
display 30 | matrices 210 cxforms 210 | mesh strips 180 (12480 vertices)
fills: colour 150 bitmap 30 | bitmaps drawn 300 made 68 | masks 0
```

The work list, as the movie itself gives it:

| count | complaint | what it is |
|---|---|---|
| 177 | `call_method can't find method addProperty` | AS2 getter/setter properties — r777 has no `Object.addProperty` |
| 23 | `todo opcode: 69` | `ActionExtends` (0x69), AS2 class inheritance — not in r777 |
| 18 | `method call on unknown c function` | to be traced |
| 8+6+2+1+1 | `find_target("dice.UIs.*") failed` | noise on the way to the global lookup (docs/research/10) |
| 7+5 | `export error: don't know how to export resource 'images/…png'` | named bitmap exports (DefineBitsLossless) — the three-line fix of docs/research/10 |
| 3 | `can't create object with unknown class 'Color'` | `Color` is not in r777 |
| 3+3+1+1 | `… 'getStorageString' / 'registerCallback' / 'getModVersion' on invalid object` | the bridge `dice.bf2.*` (docs/functions/menu-bridge.md) |
| 1 | `gameswf is not linked to jpeglib` | JPEG pictures in the movie |

Underneath the first two lies the object model: r777 keeps an object's
`__proto__` and a function's `prototype` in one field (`as_object::m_prototype`,
read and written under the name `prototype`). AS2 classes —
`Extends`, `super`, `addProperty`, `registerClass` — need the two apart. That is
the area B2BF's map calls "ActionScript objects: properties and prototype
accessors, class registration, inheritance, constructor dispatch, this/super",
and it is the next pass.

## The plan

1. The AS2 object model: `__proto__` apart from `prototype`, `Extends` (0x69),
   `addProperty`, `super`, `Object.registerClass`; `Color`. Measure: no
   `addProperty`/`0x69`/`Color` complaints, and `setColor` gets a clip
   (docs/research/10's dead end).
2. Named bitmap exports; JPEG through our own `stb_image`.
3. The bridge `dice.bf2.*` in C++, moved over from `tools/ruffle_bf2_bridge.rs`,
   with the engine's own state (the profile, the levels) instead of a second
   copy.
4. `loadMovie` as SwiffPlayer does it: a picture into `_____image`, `?name` a
   Bink video (`RendDX9.dll` 0x101ba466, docs/functions/startup.md).
5. The renderer on `obf2::gfx`: mesh strips, line strips, colour and bitmap
   fills, cxforms, masks.
6. Input, then replace Ruffle in `app/main_menu`.

Every DICE change is checked against `SwiffPlayer.dll` before it is written;
B2BF's `[DICE-MOD SwiffPlayer: …]` markers (GPL, hints only) say where to look.

## Is r777 really the base? Every revision measured

The first measure compared only r777 and r1714. Upstream added clip methods in
2007 (`duplicateMovieClip`, `removeMovieClip`, `createEmptyMovieClip` — r852..r854),
and BF2 1.50 is from 2009, so the revisions between were measured too, the same
way (the library's log texts found in SwiffPlayer.dll):

| revision | messages | in the DLL |
|---|---|---|
| r777, r800 | 511 | **150** |
| r850 | 519 | 145 |
| r870 | 501 | 144 |
| r900 | 504 | 144 |
| r950 | 423 | 137 |
| r1000 | 438 | 132 |
| r1100 | 431 | 129 |
| r1714 | 525 | 118 |

Every later revision fits worse. And the DLL's own texts for these features are
not upstream's: "Object::registerClass need two arguments", "Object::addProperty
needs three arguments" appear in no revision. **So the clip methods, the classes
and the rest are DICE's own code, written on r777**, and are reproduced from the
DLL, not taken from a later gameswf.

## What the original player has: its own name tables

The clip methods SwiffPlayer registers (`0x1007e2d0`, each entry `push name;
mov [ebp-0x18], function`), with their implementations:

| method | at | in r777 |
|---|---|---|
| play | 0x1006f980 | yes |
| stop | 0x1006f9a0 | yes |
| gotoAndStop | 0x10075860 | yes |
| gotoAndPlay | 0x10075780 | yes |
| nextFrame | 0x100719f0 | yes |
| loadMovie | 0x1007bbf0 | — |
| startDrag | 0x10071a30 | yes |
| stopDrag | 0x10071b90 | yes |
| duplicateMovieClip | 0x10075950 | — |
| removeMovieClip | 0x1006f9c0 | — |
| attachMovie | 0x10075a40 | — |
| setMask | 0x10071c00 | — |
| getNextHighestDepth | 0x1006f9f0 | — |
| setInterval | 0x1007e030 | — |
| clearInterval | 0x1007e210 | — |
| createEmptyMovieClip | 0x1007bfd0 | — |
| lineTo | 0x1007ca20 | — |
| moveTo | 0x1007cb60 | — |
| beginFill | 0x1007c2c0 | — |
| beginGradientFill | 0x1007c3b0 | — |
| endFill | 0x1007c1d0 | — |
| lineStyle | 0x1007c930 | — |

r777's `prevFrame`, `getBytesLoaded`, `getBytesTotal`, `getURL` and `swapDepths`
are **not** in the DLL at all — DICE's table replaced them. Names the movie might
ask for and the original player does not have either (`hitTest`, `getBounds`,
`localToGlobal`, `globalToLocal`, `createTextField`, `getDepth`, `Stage`,
`TextField`, `LoadVars`, `hasOwnProperty`, `watch`) are absent from the DLL: a
one-to-one player leaves them out.

Globals SwiffPlayer installs through 0x1008e6e0 (name in ecx, function in edx):
`ASSetPropFlags`, `MovieClip` (0x1008a150 — a constructor that does nothing),
then `XML`, `XMLNode`, `Array`, `String`, `Boolean`, `Number`, `Object`, …
(0x100911c0..0x100913d4).

## The AS2 object model — done

| what | how | source |
|---|---|---|
| `__proto__` apart from `prototype` | `as_object::m_prototype` is `__proto__`; `prototype` is a plain member | the SWF 7 specification |
| `ActionExtends` (0x69) | a new object with `__proto__` = superclass.prototype and `__constructor__` = superclass becomes subclass.prototype | the SWF 7 specification |
| `addProperty` | three arguments or "Object::addProperty needs three arguments"; the getter/setter pair in its own table, apart from the members | SwiffPlayer.dll 0x1009a0f0 (the table at the object's +0x28) |
| accessor lookup | along the `__proto__` chain, an accessor before a member at each level, the original object as `this` | AS2 semantics; SwiffPlayer's lookup order **not read** |
| `Object.registerClass` | two arguments or "Object::registerClass need two arguments"; the root movie's export by name; the class stored on it | 0x100997b0 (the export lookup at 0x10099813, the store at 0x10099830) |
| a clip of a registered class | its `__proto__` is the class's prototype, and the constructor runs with the clip as `this` — at creation; SwiffPlayer's moment **not read** | 0x100997b0 for the binding; B2BF lists "construct/load ordering" as a DICE change |
| native functions as objects | each native function has a properties object with a `prototype`, so `extends MovieClip` works; `Object.registerClass` is a member of `Object` | 0x1008a150 is `ret 0xc`, yet the movie's classes extend it |

`swf_probe` after it, 30 frames of `mainMenu.swf`:

| complaint | before | after |
|---|---|---|
| `call_method can't find method addProperty` | 177 | 0 |
| `todo opcode: 69` | 23 | 0 |
| `method call on unknown c function: registerClass` | 18 | 0 |
| `extends: the superclass is not an object` | 2 | 0 |

What remains, in order: `attachMovie` and the other DICE clip methods (the
movie's components are attached, which is why `loadImg` is still not found on
them), `Color`, the named bitmap exports, JPEG, and the bridge (`getStorageString`,
`registerCallback`, `getModVersion`).

## The DICE clip methods the menu calls — read, not yet written

Which ones the movies call (names counted in `mainMenu.swf` / `endOfRound.swf`):
`attachMovie` 17/16, `getNextHighestDepth` 19/17, `createEmptyMovieClip` 18/15,
`removeMovieClip` 6/5, `loadMovie` 11/12, `setMask` 1/1, `setInterval` 2/2,
`clearInterval` 1/1, `lineStyle`/`beginFill`/`moveTo`/`lineTo`/`endFill` ~16
each, `setRGB` 5/2. Never called: `duplicateMovieClip`, `startDrag`, `stopDrag`,
`beginGradientFill`, `getRGB`, `gotoAndPlay`, `nextFrame`.

| method | what SwiffPlayer.dll does |
|---|---|
| `attachMovie` 0x10075a40 | ≥3 args, else "error: attach_movie needs atleast three args" (0x100dfb94). Depth = arg 2 truncated (0x10075a9a..ab4). The symbol from the root movie's exports (+0x74, +0x38); found → `add_display_object` (vslot +0x8c) with its character id, the name, no events, the depth, identity cxform (0x100ece84) and matrix (0x100ece6c), ratio 1.0, clip depth 0; the result is the new clip, else undefined. The 4th argument (initObject) is not read. DICE's add has 8 arguments — no "replace if occupied" flag |
| `getNextHighestDepth` 0x1006f9f0 | returns the clip's counter at +0xdc and increments it (post-increment) — **not** the display list's highest depth. The counter starts at **1000** (0x3e8, clip constructor 0x1007f310, store 0x1007f3b0); `loadMovie`'s `_____image` takes its depth from it too (0x1007bd0f), and 0x1006f960 is the plain "take next" |
| `removeMovieClip` 0x1006f9c0 | the parent (+0x64) removes the clip by its depth (+0x78), vslot +0xa0 |
| `createEmptyMovieClip` 0x1007bfd0 | exactly 2 args (name, depth truncated), else "error: create_empty_movieclip needs two args" (0x100e0384); a new one-frame sprite definition (0x1007b5a0, 0x4c bytes) placed with `add_display_object` by definition (0x1007ba80), identity cxform/matrix, ratio 1, clip 0; returns the clip |
| drawing | the clip's child `_____defaultShape` (0x1007c0d0), created on first use as a runtime shape (0x60 bytes, 0x100991e0) at **depth 0**. Pixels × **20.0** (0x100e0430) into twips, line width too; alpha × **2.55** (0x100e03c8). `lineTo` (0x1007ca20, exactly 2 args, "error: line_to needs two args") stores a straight edge as a quadratic with its control at the midpoint (× 0.5, 0x100df338); `moveTo` (0x1007cb60, "error: move_to needs two args"); `endFill` (0x1007c1d0) closes back to the path's start the same way; `beginFill` (0x1007c2c0) ends any open fill first (calls 0x1007c1d0) |
| `setInterval` 0x1007e030 | ≥2 args ("error: set_interval needs atleast two args"). Only an argument of type OBJECT (0x1008a2c0 tests type 4) counts as the object: `(object, "method", ms, args…)` needs ≥3 ("…atleast three args"); otherwise the entry has no object. Entries in a global table (0x100f8cc0); the id is a global counter (0x100f8e48, starts at 0, post-increment) returned as a number |
| interval tick 0x10077910 | an entry fires when `now − last ≥ interval × 0.001` (0x100dfc4c) **or** `last == 0` (so the first time at the next tick); then `last = now` (the root's time, +0x24). **An entry without an object never fires** (0x100779c3) — so `setInterval(function, ms)` does nothing in the original. Firing: the object's member named by the method; if a function, called with the object as `this` and the stored arguments |
| `clearInterval` 0x1007e210 | exactly one arg, else "error: set_interval needs one arg" (0x100e0770) |
| `setMask` 0x10071c00 | exactly one arg, else "error: set_mask needs one arg" (0x100df710) |

Next: write these into `third_party/gameswf/gameswf/gameswf_impl.cpp` (the clip
gets `m_next_depth = 1000`; the shape needs a way to drop its cached meshes),
replace r777's builtins table with DICE's, then `Color`, the named bitmap
exports, JPEG through stb_image, the bridge in C++, the renderer, and the swap
from Ruffle in `app/main_menu`.

## The menu on our own player — the first screens

`openbf2` now plays the menu with `obf2::swf::Player` (src/swf: our gameswf,
the bridge moved from Rust to C++, a CPU renderer into the `#flash` texture);
`--ruffle` keeps the old path for comparison. The LOGIN page comes up with its
captions translated (LOGIN, QUIT, MANAGE ACCOUNTS). What it took, each a
separate defect of r777 against what the movie and SwiffPlayer need:

| what | source |
|---|---|
| **Init actions run before the frame's other tags.** r777 only queued them; a clip placed below ran its own actions first (`bg.flush()` before `registerClass`) | Flash's order; B2BF lists action-queue draining among DICE's changes |
| **Dotted names through plain objects** — `dice.UIs.Containers.ImageContainer` resolved member by member when `find_target` (clips only) fails, for reads and writes | the classes live in packages on `_global`; the game resolves them |
| **`this`** is the object a function was called on; r777 gave the target clip | AS2 |
| **`super`**: bound to `this` and to the prototype level the method was found on; `super.m()` looks above it, `super(…)` runs that level's `__constructor__` | SwiffPlayer builds `super` from an environment field (0x10080618 → 0x10080e70); who fills it is not read |
| a nameless `CallMethod` calls the object itself (`super(…)`) | SWF 7 |
| **`ActionNewMethod` (0x53)**, `ActionEnumerate2` (0x55); `Enumerate` (0x46) no longer casts a clip to an object | SWF 7 |
| **Array** — elements, `length`, numeric names; `join concat reverse toString push shift slice sort splice unshift`, no `pop`; `sort` by `strcmp` of the strings, its arguments not read | SwiffPlayer.dll: data +0x2c, count +0x30; member read 0x1009c1f0; table 0x1009cb20; sort 0x1009c1d0 / compare 0x1009c1a0; join's "," 0x100e2a18 |
| **`Color`** — `setRGB` (one argument, the RGB multipliers to 0, the additions to the colour, alpha kept), `getRGB` the last value | 0x100a95a0, 0x100a9570 |
| `new Color(mc)` inside `setColor(mcObj, color)` found the number `color`: r777 looks names up without case; a non-function under the name falls back to `_global` | ours; SwiffPlayer's resolution not read |
| **named bitmap exports** (DefineBitsLossless) | B2BF's map; the menu attaches its pictures by name |
| **JPEG** through our stb_image (JPEG2; DefineBits with the movie's JPEGTables); stb_image now builds its JPEG decoder | — |
| `gotoAndStop("login")` — a label, not a number | r777 took every argument as a number |
| a registered class: prototype → `onClipEvent(construct)` → `initialize` → frame 1's children → constructor → frame 1's actions → `onLoad`, applied when the clip is placed (a constructor run at creation dropped the only reference) | Flash's order; SwiffPlayer's not read |
| **`onClipEvent(construct)`** (flag bit 18) — where the components carry their parameters (`localizationId`, `columnTexts`, …); r777 had no slot for it | SWF 7 |
| the clip methods DICE added (table above) | their addresses above |

Still wrong on the LOGIN page: the buttons' plates are grey and their captions
black and wrapped (MANAGE ACCOUNT / S); the profile list is empty; `loadMovie`
(a picture or `?menu.bik`) is not implemented; four "Invalid local register 0"
(function-1 code using the global registers). The renderer is slow in a debug
build (14 frames a second).

### Second round on the LOGIN page

| what | source |
|---|---|
| **ASSetPropFlags copied the prototype's members into the object.** AS2 classes end with `ASSetPropFlags(Class.prototype, null, 1)`, so every component's own methods were overwritten by its base's — every `draw` became dice.UI's empty one, and no component built its children (the grey placeholders). Now: the object's own members only; the names as an Array or a comma-separated string; a bad call is ignored, not asserted | found by tracing `draw`: four classes' prototypes all held one function object |
| a registered clip's children of frame 1 are placed **before** its constructor (a v2 component's `init()` reaches `mcBoundingBox`), its frame-1 actions run after | Flash's order |
| String: `concat`, `lastIndexOf`, `slice`, `split` added; a String method on an object that stands for a string runs on its string value; no `substr` | SwiffPlayer's String names lie together in the DLL |
| a text field answers `_parent` — captions are written inside `with(labelTextField)` through `_parent._parent.m_label` | the version text read `undefined` |
| `loadMovie(url)`: the clip's children cleared, a `_____image` clip at the next counter depth holding a rectangle filled with the picture, through the host's texture loader (`?name` a Bink video in `<mod>/Movies/`, laid out 256 × 256; else `Menu/External/FlashMenu/<name>`) | SwiffPlayer.dll 0x1007bbf0; RendDX9.dll 0x101ba466, 0x101ba5d7 |
| an unknown clip event (`construct` was one) has no handler name instead of an assert | — |

On screen now: the LOGIN panel with its frame and captions, QUIT, the top-left
picture container, the version text. Still wrong: captions wrap inside words
and are black, not yellow; the device font (`Helvetica`) has no space glyph;
the background video does not show; the profile list is empty.

### Third round — the captions right

| what | source |
|---|---|
| a text field keeps its own box and colour: `_width`/`_height` resize the box and lay the text out again, `textColor` replaces the colour — r777 ignored the size writes and multiplied the definition's (black) colour | Flash's TextField; the captions were black and wrapped mid-word |

On screen now: LOGIN, QUIT and MANAGE ACCOUNTS yellow and on one line, the
version text, the panel. Left on this page:

* **the profile list stays empty.** The ScrollTainer `profilesList` attaches
  `list_profiles` into `m_mcContainer.contents` and calls `contents.update()`;
  the attach runs (its code executes) but `update` is not found on what
  `contents` names afterwards, and `dice.General.getListEntries` is never
  called. Next: whether `removeMovieClip` of the empty `contents` and the
  attach by the same name leave the right clip under that name;
* the background video does not show (`loadMovie("?menu.bik")` reaches the
  loader; where its fill lands is not checked);
* the device font `Helvetica` (the version text) has no space glyph — the
  embedded font of the same name has it;
* the buttons' plates (`images/UIcomponents/button-bkg-N.png`) are not drawn;
* four `Invalid local register 0` (register 0 falls back to a global register,
  harmless); intervals fire twice on their first two ticks, as SwiffPlayer's
  `last == 0` test does.

### Fourth round — the list, the background, logging in

Each row below was found with the display-tree dump (`OBF2_SWF_DUMP=<step>`
with `OBF2_SWF_TRACE=1`) or the VM trace (`OBF2_SWF_VERBOSE=1`), then checked
on a screenshot against Ruffle playing the same movie.

| what was wrong | the fix | source |
|---|---|---|
| `_width`/`_height` of a clip: r777 took a sprite's widest child, ignoring where the children stood, and a write on an empty clip turned the pixels straight into a scale (25604 on the list's mask) | bounds the way Flash measures them: the content's rectangle in the clip's own space through its matrix into the parent's; a write on a clip with no content leaves it alone | Flash's MovieClip; `character::get_bound_local` / `get_bound_parent` |
| a function called inside `with (obj)` ran on the movie's target — the list's `with (m_mcList) { createEmptyMovieClip(...) }` made every row on `_root` | a function found on a `with` object is called on that object | ActionCallFunction's scope chain |
| `menu.bik` came out green | the video is grey: flag 0x20000 of the Bink video flags (+0x24 of the file) — no chroma coded; FFmpeg leaves the planes at zero. Converted from the luma plane | `mods/bf2/Movies/menu.bik` header `00 00 02 00` |
| the list's text drew empty boxes | the fields name glyph-less fonts (`Helvetica` 43, `Helvetica Condensed` 50, `TradeGothic LT BdCondTwenty` 59, `tools/swf_read.py --fonts`); a same-named full font from the movie is used. **Where the original finds their glyphs is not established**: SwiffPlayer's formatText (0x10086c00) has only r777's fontlib walk, and nothing calls that registry's add (0x100a6af0, registry 0x100f9bd8) | see the left |
| `new Number(...)` unknown | SwiffPlayer's `Number` (registered 0x10091364, constructor 0x1008c520): an object with a double at +0x30 | `SwiffPlayer.dll` |
| `Invalid local register 0` | DefineFunction2 registers are 0..count−1; r777 numbered them from 1 | the SWF specification |
| a selected row did nothing | two causes: r777's `String(x)` object had no `length`, so the list's callback name counted as empty; and `var subPage = …` in a frame's script was kept a local the frame's own functions could not see — in Flash it is a member of the timeline | the VM trace; ActionDefineLocal outside a function |
| a crash a frame after leaving a page | the bridge's delayed captions wrote a field whose parent was gone; a field only the queue still holds is dropped | backtrace in `flushDelayedCaptions` |

On screen now: the profile list with its row, the grey video under the
movie's own 30 % `0x464646` veil (`bg flush: 4605510 a: 30` — Ruffle's blue is
the engine's still picture, it cannot play Bink), PREFIX and SELECT ACCOUNT
after a row is chosen, and SELECT ACCOUNT logs in: the main menu with BFHQ,
MULTIPLAYER, SINGLEPLAYER, COMMUNITY, OPTIONS, QUIT, the active account and
PLAY NOW. Not measured against the original: whether its menu shows the still
picture under the video.

Left: SINGLEPLAYER crashes (exit 139, not yet looked at); `ServerSettings` and
many other bridge methods are stubs; `call_method can't find method update` ×4.

### Speed

The menu ran at 3.4 frames a second in the debug build (`frames drawn:`
line). `sample` put nearly all of it in the CPU rasteriser and in the menu's
per-frame upload. Now 59 (the display's rate) for both the menu and the intro:

* the rasteriser walks each triangle row by row over the span the three edges
  allow, with u and v stepped, integer bilinear sampling and integer "over";
  fills that can only be transparent are skipped;
* `gameswf` and `obf2_swf` are compiled with `-O2` in the debug build too;
* the `#flash` and `#movie` quads are made once and only their textures'
  pixels go to the card each frame (`MeshRenderer::updateTexture`, one reused
  staging buffer) — before, each frame made a mesh and a texture and copied
  the 3 MB picture twice;
* the RGBA → BGRA copy goes through raw pointers.

The optimised build also exposed a bug of ours: the bridge's object table held
`std::initializer_list`s whose arrays died with the expression that made them.

### Fifth round — SINGLEPLAYER to a running level

Measure: `--click-at 130:234:184 --click-at 180:532:414 --click-at 300:332:14
--click-at 470:48:483 --click-at 560:730:584` (a profile, SELECT ACCOUNT,
SINGLEPLAYER, Strike At Karkand, START SINGLEPLAYER) ends on the level's
spawn screen; the page itself matches Ruffle's frame of the same movie
(INSTANT BATTLE, all seventeen maps, START SINGLEPLAYER).

| what was wrong | the fix | source |
|---|---|---|
| SINGLEPLAYER crashed (stack overflow) | a forward goto placed — and loaded — every object of every frame it passed: the jump to `createGame` built the BFHQ and server pages on the way, and their checkboxes' callbacks recursed (`setInfantryOnly` → `flush` → `setValue` → …). The frames in between now only change a record per depth (`sprite_instance::m_staging`), and the display list gets the end result | Flash's goto; the trace |
| the footer said START SERVER, MULTIPLAYER lit | one symbol placed per frame at the same depth and name, with other clip actions: r777 kept the old instance. A placement is the same instance only with the same clip actions (`character::m_placed_by`) | the display-tree dump |
| `ServerSettings.get*` undefined | SwiffPlayer's `ServerSettings` (0x10066760, 98 methods, `tools/swiff_methods.py`) calls the engine's `GameServerSettings` (setTeamRatio 0x10063b50 → +0x17c, getTeamRatio 0x10064eb0 → +0x178); the profile's `ServerSettings.con` writes the same object. `Engine::serverSetting` keeps both | `SwiffPlayer.dll`, `Profiles/<id>/ServerSettings.con` |
| the map list stayed empty | r777's `as_value ==` called any two objects equal; the movie's update manager finds a registration by `m_object == obj`, so every list after the first was taken for the first and never updated. Objects and functions are now equal only to themselves | ECMA-262 abstract equality; the movie's `getIndex` |

`tools/swiff_methods.py <dll> <any method>` prints a bridge object's whole
method table with addresses — the same walk `Logic`'s table was taken by hand.

The VM trace (`OBF2_SWF_VERBOSE=1`) now names objects: `get_member`/`set_member`
print the object's address, and every placement and `goto_frame` is logged —
how the two map-list bugs above were told apart.

### Sixth round — every main page opens; the bridge's settings are real

Measure: from a logged-in profile, BFHQ (→ NOT ONLINE, NO), MULTIPLAYER,
COMMUNITY, OPTIONS and OPTIONS → CREDITS each open without a crash, and the run
from login to a started level asks the bridge for nothing it does not have
(no `menu bridge: … is not made yet` line).

| what | the fix | source |
|---|---|---|
| OPTIONS recursed, then BFHQ → NO → `none` did | a goto now records the target frame's own placements too, and a backward goto rebuilds the timeline from frame 0 into the record (the timeline's objects start as gone, script-made clips are left alone — `character::m_scripted`). r777 undid frames one by one and re-made the objects of the frames it passed, the audio page among them | Flash's goto; the place/goto log of the VM trace |
| `ServerSettings`/`GeneralSettings` | both are the engine's console objects (`GameServerSettings`, `GeneralSettings`); `Console::observe` keeps every `set<Name>` they get from the profile's .con or the menu, a menu setter is a console line, a getter reads it back with its own type and arithmetic (transparency `1 − v/255`, colours packed `a<<24|r<<16|g<<8|b`, `setCoopBotDiff` also setting bot skill × 0.01) | `tools/swiff_accessors.py`; 0x10033840, 0x100334a0, 0x100323f0, 0x10063aa0 |
| bitwise operators | ECMA ToInt32/ToUint32 (modulo 2^32); r777 cast past 2^31 to int — the packed colour came apart | ECMA-262 |
| Logic, Sound, Multiplay, VideoSettings | `isGameRunning`, `get/setActivateReason`, `get/setFrameLabel`, `isCapsLockPressed`, `setAsync`/`getAsync`, `enableScroller`, `setDecimals` (`"%.<n>f"`), `cacheCredits`; `playSound` (−1: no sound output), `voipSetEnable`/`voipStopTest` (VoIP never initialised); `get/setBrowser`, `stopBrowser`, `isBrowsing`, `isDownloadingDemo`, `getNumDemoBookmarks`, `get/setToggleFilter` (bit n of `GeneralSettings.setToggleFilters` — the bit order is inferred), `getScrollFadeSpeed`; `isRunningWithUnsupportedDrivers` | each body read, addresses beside the code |
| names the DLL does not register | `Logic.getCommandline` (the DLL's is `getCommandLine`), `Locale.setFlaName`, `Locale.setDefaultLang` answer undefined, as a missing method does | `tools/swiff_methods.py`, `strings` |
| the credits | `credits.con` (`swiffHost.setScrollFadeSpeed 200`, `setScrollSpeed 1200`, `setScrollerRect 0 118 1024 668`, 1747 `addCredit` lines) is run by `Logic.cacheCredits` and kept by the engine (`Engine::credits`); BF2.exe names the path | BF2.exe string at file offset 0x49fe8c |
| FALCON tab showed `OPTIONS_6` | the client reads every file of `localization/<language>/` (initLocalization 0x4086e0, path built at 0x40879f); the rest of the folder now fills the keys the three main files lack — 3777 strings became 4658. Which file wins a key two files define is not read | BF2.exe 0x4086e0..0x408899 |

New tools: `tools/swiff_methods.py` (a bridge object's method table),
`tools/swiff_accessors.py` (one line per method: the engine object, the slots,
the float constants, the type handed back).

Not done, and known:

* the credits' scroller itself — the host draws the lines in its rectangle at
  its speed, with fonts and sizes not read;
* `MapIconTransparency` (the menu's, slot +0x98) against the profile's
  `setMapIconAlphaTransparency 255`: whether they are one field is not read,
  so ICONS shows 100 %;
* bots: the settings reach the engine's store, but our server has no bots;
* menu sounds (`playSound`), VoIP, the server browser, demos and BFHQ's
  online pages are not there; the menu is told so the way the original's
  refusal paths tell it.

### Seventh round — the pictures

* **Most of the menu's `.png` files are DDS** (`scrollArrow_up.png`,
  `button-bkg-N.png`, every level's `Info/*_menuMap.png` start with
  `DDS `). The original's renderer loads by content (D3DX); ours now decodes
  them with `texture::decodeRgba8`. Every picture the menu loaded had failed
  before — the scroll arrows, the panel corners, the difficulty radio buttons,
  the map preview.
* `$` in a picture's path is the mod's own folder (RendDX9.dll 0x101ba3e2;
  anything else is under `Menu/External/FlashMenu/`, 0x101ba1dc). `Levels/
  <level>/Info/` lies loose on disk, so the host reads the mod's folder after
  the archives.
* `Multiplay.getGameModeFromId` (0x10053710): "Singleplayer" for sp1..sp3,
  "Co-op"/"Conquest" otherwise — plain strings.
* A missing glyph is skipped, no box and no advance — SwiffPlayer's formatText
  (0x10086c00) only logs it. `MorganSnCnOffice` has no glyphs anywhere in the
  movie and `MorganSnCnCaps` fifteen, so their fields stay short in the original
  too unless its fonts come from somewhere not read.

### Eighth round — the maintainer's look at the login page

* **The panels' edges were dashed.** A line a pixel wide was rasterised as a
  quad centred on a pixel boundary, so its row of pixel centres sat exactly on
  the quad's edges and rounding dropped every other one. Thin lines now have
  their ends on pixel centres (raster.cpp). How SwiffRender places one is not
  measured.
* **`MODAL_disclaimer` showed as text under CONNECTING TO ACCOUNT SERVER.**
  English.utxt has that key with an empty text; the host's localize returned
  the key for any empty text. Now only an absent key comes back as itself
  (whether the original does that for an absent one is not read).

### The loading screen — what is known, what is not

* `RendDX9.dll` has `dice.hfe.geom.LoadingScreen` (class name at 0x1020a26c):
  its init (0x100a2470) takes the effect `Shaders/LoadingScreen` (TRANSFORM,
  TEXTURE), the texture `Menu/Startup/loadingscreen.dds` and a four-vertex
  quad (−1,−1)…(1,1), UV 0..1 with v flipped — the whole screen. The picture is
  the olive "BATTLEFIELD 2" card.
* BF2.exe registers menu events 3..8 — `ECMenuMenuEvent_InitLoadingScreen`,
  `UpdateLoadingScreen`, `RemoveLoadingScreen`, `SetLoadingScreenDebug`,
  `InitGameLoadingScreen`, `RemoveGameLoadingScreen` (names at 0x880304 on) —
  and a HUD data layer `ShowLoadingScreen`, `LoadingBarValue`,
  `LoadingPicturePathString` (default `Load/default_loadscreen.tga`),
  `LoadingScreenMapName`, `LoadingScreenDebug`, `LoadingScreenBuildNr`
  (strings at 0x934d00 on). No HUD script in any archive uses those variables,
  and `mainMenu.swf` has no loading page; `loadGame.swf` is a component library
  with no frame actions.
* **Not established:** what the original draws while a *level* loads — which
  of these, with what text and bar, and who shows it. Needs a look at the
  running original (rule 13).

**Found (B2BF's GameLogic::beginLoadStats, to be checked at BF2.exe
0x4EC051..0x4EC08C):** the level's loading screen is **`endOfRound.swf`**.
Loading a level sets SwiffHost's loading flag (+0xD0, what `Logic.getIsLoading`
reads) and posts menu event 10/3 (`InitLoadingScreen`) with the level path;
progress goes out as 10/4 every 1/30 s from LoadStat's callback. The movie
itself (endOfRound.swf, frame code) shows `$/Levels/<map>/Info/loadMap.png`,
the map's name and briefing, a bar from `Client.getLoadProgress`, and
`finishedloading` at the end. The bridge it calls, with SwiffPlayer.dll's
implementations (`tools/swiff_accessors.py`): Client getLoadProgress
0x1000d810 (+0x94), getEORType 0x1000d870 (+0x98), getCurrentLoadFile
0x1000d9d0 (+0xa0); EndOfRound (table 0x10012590) getMapPath, getMapName
('Unknown Map' when not found), getMapSize, getMapGameMode, …; Logic
getIsLoading 0x1004d980, isFirstTimeLoading 0x1004d910, isAbortLoadLevel
0x1004d3e0, readyToPlay 0x1004de90, getNumRoundsLeft 0x1004d2e0,
isLoadingDemo 0x1004d270. None of it is wired in our app yet.

### Ninth round — the loading screen is endOfRound.swf, and it runs

Checked in the binary: the load-start function at BF2.exe 0x4ec030 looks the
`SwiffHost` object up by name (0x4ec05e) and calls its +0xd0 with 1
(0x4ec08c) — the slot `Logic.setIsLoading` (SwiffPlayer.dll 0x1004d090) also
reaches: the loading flag the movie reads.

Ours now (`app/loading_screen`, `engine/load_stat`):

* a level chosen in the menu starts a session that makes the window and boots
  the engine *before* loading, opens `endOfRound.swf` with the level, mode and
  size the menu's `createServer` gave, and draws a frame of it between our
  load's phases (Level, Terrain, Objects, Server, Placement, Upload);
* the percent is LoadStat's formula (each part a window of 0.15 × weight, the
  share of its steps inside); our phases weigh 1 each — **not the engine's
  parts**, so the bar's pace is ours;
* the bridge answers the movie from the level's `.desc` and the dictionary:
  `EndOfRound.hasMap/getMapPath/getMapName/getMapSize/getMapGameMode/
  getMapGameModeName/getMapDescription/getMapGameModeDescription`, the
  previous round's questions empty (no round before the first),
  `Client.getLoadProgress/getEORType/getCurrentLoadFile`,
  `Logic.getIsLoading/isFirstTimeLoading/readyToPlay/isAbortLoadLevel/
  isLoadingDemo/getNumRoundsLeft`, `Multiplay.get/setHasNewBookmark`,
  `VideoSettings.isOptimzingShaders`;
* when the load is over the flag goes off; the movie shows "MAP LOADED,
  SELECT THE JOIN GAME BUTTON OR PRESS ESCAPE TO ENTER THE GAME" and the screen
  waits for `readyToPlay` or Escape (a `--frames` run leaves after 3 s).
  `OBF2_LOADING_SCREENSHOT=<file.bmp>` saves its frames.

On screen: MAP BRIEFING (the Karkand briefing), GAME MODE (the Conquest:
Assault text), SERVER INFO, SERVER MESSAGE, the map picture, MAP SETTINGS
(Conquest, 16), the four tabs.

Not yet: the JOIN GAME button is not seen on the frame (whether the movie
hides it until something, or ours misses it, is not looked at); the mode the
menu hands on is `gpm_cq` for the singleplayer page, where the original may
hand `sp1` (getGameModeFromId would then say Singleplayer); the engine's own
load parts and their weights; our load is one thread, so the screen moves
only between phases.

**JOIN GAME.** The button is `goFurther`; its `flush` goes to its
`interactive` frame only while `m_interactive` and `Logic.getNumRoundsLeft() >
0`. Ours answered 0, so the button never came. It now answers the server's
RoundsPerMap (1 when the profile gives none) — that the host's +0x358 holds
that count is inferred. On screen now: JOIN GAME and AUTO READY ON in the
bottom bar. Whether a click on JOIN GAME reaches `readyToPlay` is not yet
tried by hand (the loading screen takes no scripted clicks).

**One window.** The menu's session and the game's after it share one
`RenderContext` (main.cpp: `shared`, made by the first session that needs
it); each session releases what it uploaded before it ends. The window is no
longer closed and opened again between the menu and the round. What a session
leaves on the card beyond its meshes (light-map pages, the sky) is not freed
until the end of the run.
