# gameswf on arm64: what had to be fixed for it to read BF2's menu

Battlefield 2's menu is run by **gameswf**, a public-domain library
(docs/research/00-prior-art.md). This is the record of the first attempt: will
it build for us and will it read the game's real movies.

**In short: it builds and it reads.** Six fixes were needed, and one of them is
a genuine 64-bit defect in the library itself.

## The measure

`gameswf` reads all four of the menu's movies from the user's installation, and
the numbers match what our own reader `tools/swf_read.py` says.

It agreed:

| movie | version | frames | rate | stage |
|---|---|---|---|---|
| `mainMenu.swf` | 7 | 320 | 30 | 1024 x 768 |
| `menu.swf` | 7 | 10 | 12 | 800 x 600 |
| `endOfRound.swf` | 7 | 70 | 30 | 1024 x 768 |
| `loadGame.swf` | 7 | 1 | 30 | 1024 x 768 |

`tools/swf_read.py mainMenu.swf` counts 320 `ShowFrame` marks, `menu.swf` — 10.
So two independent readers give the same number.

## The second attempt: not just read it but play it

`tools/gameswf_probe.cpp` takes a frame count as its second argument. It then
installs a `render_handler` that **draws nothing but counts**, runs the movie
and prints what was asked of it. That list is exactly what has to be mapped
onto `obf2::gfx` later.

`menu.swf`, three frames:

```
begin_display / end_display   3 / 3
window                        0..16000 x 0..12000   (that is 800 x 600 pixels)
background                    255 255 255 255
set_matrix / set_cxform       696 / 684
mesh strips                   678 (22800 vertices)
line strips                   51 (237 vertices)
fill: colour / bitmap         675 / 3
masks (begin / end)           15 / 15
```

`loadGame.swf`, three frames: 39 mesh strips over 10 830 vertices, window
0..20480 x 0..15360 (1024 x 768).

So **the whole chain works**: parsing -> the display list -> ActionScript ->
tessellation -> draw calls. The window in twips matches the stage size from the
header, and the number of `begin_display` matches the number of frames.

> **But this result is worth less than it seems.** Checking the live game
> (`WINEDEBUG=+file`, docs/functions/menu-bridge.md) showed that the game
> **never opens** `menu.swf` — it is a development leftover — and does not open
> `loadGame.swf` either. The movies actually used are exactly two:
> `mainMenu.swf` and `endOfRound.swf`. Neither plays for us yet. So the correct
> statement is not "everything plays except the main menu" but **"none of the
> ones the game needs plays"**.

## What is still missing: `mainMenu.swf` does not play

The main menu dies at this point — and not because of gameswf. Why is visible:

```
error: can't find [0x0].setRGB
error: can't find m_mcForImage[0x0].loadMovie
loadImg: undefined
can't find target dice.UIs.Forms
```

The movie calls what we did not give it: the bridge objects (`dice.bf2.*`,
docs/functions/menu-bridge.md) and external pictures it pulls through
`loadMovie` from `images/`. Without them its script spins in an error loop
until it eats the stack — and 256 MB of stack does not save it, because the
loop is endless.

`endOfRound.swf` has the same root: it looks for `dice.UIs.*` and does not play
through either.

So the next step is not the renderer but the **bridge**: without it the big
menu will not come alive no matter how much is drawn. The little `menu.swf`
plays precisely because it asks the game for almost nothing.

## The third attempt: a bridge stub

`tools/gameswf_bridge.h` puts all seventeen objects into `_global` — both under
their own names (`Logic`) and under the full ones (`dice.bf2.Logic`), because
the movies use both forms. An object answers **any** name: it hands back a
do-nothing function and prints what was asked of it.

The point is not the implementation but the reconnaissance: this way the
"object -> method" list comes **from the movie itself**, not from a guess about
adjacency of strings in `.rdata`.

`menu.swf` gave four pairs straight away:

```
[bridge] Cursor.setCursor
[bridge] Client.getPlayerName
[bridge] Client.getCommandlineOption
[bridge] Logic.isGameRunning
```

That is exactly what none of the earlier approaches gave: that `getPlayerName`
and `getCommandlineOption` belong to `Client` is in no way visible from
`.rdata`.

### `mainMenu.swf` still does not play

The bridge removed the earlier endless `loadImg:` loop, and in one of the runs
the movie got as far as its own steps (`setFlaName mainMenu`, `setDefaultLang`,
`initialize`, `start`, `Manager Spawns`). But the ending is the same: the stack
runs out, and 256 MB does not save it.

From the bridge the movie manages to call exactly one name — `Logic.active`.
After that it dies in its own initialisation, and the main suspicion is left in
the log:

```
can't find target dice.UIs.Forms
can't find target dice.UIs.Commons
can't find target dice.UIs.Containers
```

`dice.UIs.*` is the movie's **own component library** (buttons, lists, input
fields), not our bridge. It is registered by ActionScript 2's means
(`#initclip`, classes in packages), and gameswf does not bring it up. So what
we run into is not something we failed to write but something **gameswf cannot
do**: it plays SWF but is not a full Flash Player, and its AS2 class support is
incomplete.

That suspicion had to be **withdrawn**: gameswf does handle `#initclip`
(`do_init_action_loader` in `gameswf_action.cpp`, execution in
`sprite_instance::execute_frame_tags`), and `Object.registerClass` and
`ASSetPropFlags` are in place too.

### What exactly it loops on

Instead of guesses, a depth counter in the ActionScript function call
(`as_s_function::operator()`). It showed it unambiguously: **one and the same
function calls itself**, hundreds of levels deep, with the same buffer and the
same `pc = 3179`.

Then that place was found in the movie itself — by taking the byte code apart
with the same `tools/swf_read.py`. At offset 3179 in a 3856-byte buffer lies
this run:

```
3135  push getDownloadingUrl        DefineFunction  push url
3157  push getDownloadingDate       DefineFunction  push 2005-01-02
3179  push getDownloadingProgress   DefineFunction
3208  push deleteDemoBookmark       DefineFunction
3228  push isDownloadingDemo        DefineFunction
3250  push getNumDemoBookmarks      DefineFunction
```

So **the movie has its own stand-in bridge methods** — exactly the ones that
ought to come from the game (these are `Multiplay`'s methods, demo downloads).
The design is evidently that the player's native method wins, while the stand-in
exists for clicking around without a game (the `Clickdummy` directory sits right
next to it).

### What did not work here

Two assumptions were checked and **both proved wrong**:

* forbidding the movie to overwrite the bridge objects' methods, so ours would
  win — the loop stayed;
* returning `false` from the stub instead of `undefined` (in AS2 `undefined` is
  treacherous in comparisons) — it stayed too.

Both changes were removed from the code, except `false`: it is closer to what a
real bridge would return and does no harm.

### The call chain, not just the depth

Next the depth counter was supplemented with a ring of **method names** (written
where the byte code executes "call method", opcode 0x52). The loop became
visible verbatim:

```
createChildren -> getNextHighestDepth -> attachMovie -> getNextHighestDepth
-> attachMovie -> arrange -> setColor -> setRGB -> resetImageContainers
-> loadImg -> __get__soundId -> ... -> authorModeUpdate -> createChildren -> ...
```

That is the life cycle of the movie's own component: `createChildren` attaches
a child, the child goes into `authorModeUpdate`, and that goes back into
`createChildren`.

Two suspicions were checked and **cleared**: `getNextHighestDepth` in gameswf
counts correctly (`display_list::get_highest_depth` already returns "highest +
1"), and `Color.setRGB` is in place. But `as_global_color_ctor` returns nothing
when the target could not be found — hence `can't find [0x0].setRGB`. So the
culprit is not the colour but the fact that `attachMovie` did not find what to
attach.

### A genuine defect found: gameswf cannot export bitmaps

`export_loader` (tag 56) asks for only three things: a font, a
`character_def`, a sound. Bitmaps
(`DefineBitsLossless`/`DefineBitsLossless2`), meanwhile, are stored separately
— `add_bitmap_character` — and fall into none of those three. Because of that
every picture produced

```
export error: don't know how to export resource 'images/components/playNow.png'
```

in the log, and `attachMovie("images/…png")` found nothing.

That this is exactly the case was verified: taking the movie apart shows that
all twelve "unexportable" names are defined by tags 20 and 36, that is
`DefineBitsLossless` and `DefineBitsLossless2`.

The fix is three lines: ask `m->get_bitmap_character(id)` as well.
`bitmap_character_def` derives from `character_def` anyway, so
`export_resource` takes it unchanged. After that **all twelve export errors
disappeared**.

### What that fix gave

`attachMovie` stopped missing: after it there is **not one** failed attach in
the log (checked with a temporary log on both branches of
`sprite_instance::attach_movie`). Twelve export errors disappeared.

### The state, and what was checked and rejected

`mainMenu.swf` still does not play through — the loop stayed, and the failure
happens **already on the first frame**, that is where the initialisation actions
run.

Checked and **cleared** of suspicion:

| suspicion | how it was cleared |
|---|---|
| the classes are declared in later frames and we only play three | all **70** `DoInitAction` lie in frame 0 |
| `getNextHighestDepth` returns the same depth | `display_list::get_highest_depth` already returns "highest + 1" |
| there is no `Color.setRGB` | there is; the `Color` object itself comes out empty because the target was not found |
| our `_global.dice` shadows the movie's own package | the errors are the same without our `dice` (but we take the existing one — that is more correct) |
| the movie overwrites the bridge's methods with its own stand-ins | forbidding the write changed nothing |
| `undefined` from the stub confuses AS2 | `false` changed nothing (kept — it is closer to the truth) |
| `attach_movie` force-advances the child (`advance(1)`) inside the parent | removing the line changed nothing, it was put back |

### What taking the initialisation blocks apart showed

Blocks 20–40 **create** the classes, 53–69 register them
(`Object.registerClass "dice.UIs.Forms.CheckBox"`). The prologue in each is the
ordinary ActionScript 2 one:

```
push "_global"; GetVariable; push "dice"; GetMember; Not; Not; If ->past
push "_global"; GetVariable; push "dice", 0, "Object"; NewObject; SetMember
```

`_global` resolves in gameswf (`M_GLOBAL` in `get_variable_raw`), and
`NewObject` and `Object` are in place too.

**And it works.** Verified by measurement, not by reasoning: right after the
frame-0 actions run (there are exactly 70 of them) `_global.dice` exists, and so
does `dice.UIs`.

### `can't find target dice.UIs.*` turned out to be noise

That is the most important correction of this round.
`as_environment::get_variable` first tries `find_target(path)` on the **current
target**, and `find_target` logs the failure itself. If that did not work, the
next line is `get_global()->find_target(path)` — and that is the one that
succeeds. So the eight complaints about `dice.UIs.Forms` mean nothing; they are
printed on the way to success.

So the suspicion that "gameswf cannot do AS2 classes" is withdrawn entirely.

### Where the emptiness actually is

`Color()` was left. A log was added to `as_global_color_ctor` — and it says it
unambiguously: **sixteen times `Color` receives `undefined`**, not a clip.

Then the `setColor` function itself in block 19 was taken apart. It is a
`DefineFunction2` with two parameters:

```
parameter 0 -> register 2, name 'mcObj'
parameter 1 -> register 3, name 'color'
flags 0x002a: suppress_this, suppress_arguments, suppress_super
```

and the body does `new Color(reg2)`. So it is the **first argument** that
arrives empty: whoever calls `setColor` does not have the clip it passes.

### What is healthy at the same time

Two checks were done by measurement, both clean:

* **`attachMovie` never misses** — a log printing only the failures of both
  `attach_movie` branches stays silent;
* **the attached child is found**, both in the display list and as a member of
  the parent by name; each of the 32 attachments already has its own content
  inside.

So the chain "export -> attachMovie -> child by name" is intact. The emptiness
appears deeper: whoever calls `setColor` does not have the clip it needs. That
is the next link to be reached.

Incidentally it turned out that disabling zlib and jpeg was unnecessary: both
are in the system (`-lz`, `-ljpeg` from Homebrew), and with them the movie's
pictures really do decompress.

This is **only parsing and call accounting**. Nothing is drawn yet.

## How to build it

```
clang++ -std=gnu++14 -w -O1 \
  -DTU_CONFIG_LINK_TO_THREAD=0 -DTU_CONFIG_LINK_TO_JPEGLIB=0 \
  -DTU_CONFIG_LINK_TO_LIBPNG=0 -DTU_CONFIG_LINK_TO_ZLIB=0 \
  -I. -Ibase <probe>.cpp <sources> -o swf_probe
```

The sources are everything from `base/`, `gameswf/` and
`gameswf/gameswf_as_classes/`, **except**:

* `gameswf_render_handler_{d3d,ogl,ogles}.cpp`, `gameswf_sound_handler_sdl.cpp`,
  `gameswf_test_ogl.cpp`, `base/ogl.cpp` — these are backends, and we have our
  own (`obf2::gfx`);
* `gameswf_processor.cpp`, `gameswf_parser.cpp` — separate programs with their
  own `main`;
* `base/{GCBench,Stackwalker,demo,triangulate_float,triangulate_sint32}.cpp` —
  demonstrations and things gameswf never calls.

That is **96 files** in total. The `TU_CONFIG_LINK_TO_*` flags disable threads
(not needed for parsing) and png. But **zlib and jpeg are worth enabling**:

```
-DTU_CONFIG_LINK_TO_ZLIB=1 -DTU_CONFIG_LINK_TO_JPEGLIB=1
-I/opt/homebrew/include -L/opt/homebrew/lib -ljpeg -lz
```

Without them the movie's pictures do not decompress. Our own miniz and stb can
be substituted here later — they give the same interface.

## The fixes

The patch is `tools/gameswf_macos.patch` — 53 changed lines in seven files.

### 1. `base/container.h` — the hash table did not work at all

This is not "it would not build", it **silently computed the wrong thing**:
`add` put an entry in, `get` did not find it. On our probe of 64 keys **zero**
were found.

Two mistakes in a row, both invisible on 32 bits:

```
// add():
unsigned int hash_value = (unsigned int) compute_hash(key);   // truncated to 32 bits
...
new (natural_entry) entry(key, value, -1, hash_value);

// entry:
entry(const T& key, const U& value, int next_in_chain, int hash_value)
                                                       ^^^ int again
    : m_hash_value(hash_value)   // while the field is a size_t
```

So a 32-bit hash with the high bit set (0xB77A0008, say) went into a `size_t`
**signed** and became `0xFFFFFFFFB77A0008`. The lookup computed the hash in full
and compared against that — it never matched.

It was visible in the library's own assertion:

```
assert(e->is_tombstone() || !(e->first == key));  // keys are equal, but hash differs!
```

Both places were fixed: `add` no longer truncates, and the entry's constructor
takes a `size_t`.

On 32 bits, which tu-testbed was written for, `size_t` and `unsigned int` are
the same thing, so the defect was not there.

### 2. `base/utility.h` — `fmax`, `fmin`, `log2`

Declared as free functions for `float`; libc++ already has the same ones, and
clang says "declaration conflicts with target of using declaration". Commented
out — the standard library picks the calls up.

### 3. `base/vert_types.h` — `compiler_assert(0)`

The macro expands to `switch(0){case 0: case 0:;}`, and modern clang sees a
duplicate `case` in it. It is a "this template must be specialised" stub;
removed.

### 4. `gameswf/gameswf_environment.{h,cpp}` — `array` from a private base

`struct vm_stack : private array<as_value>`, and in the derived classes the name
`array` finds the **injected name of the private base**, not the global
template. Ten places were rewritten as `::array<…>`.

### 5. `gameswf/gameswf_impl.cpp` — exporting bitmaps

Three lines in `export_loader`: ask `get_bitmap_character(id)` as well. The
analysis is in the section on `mainMenu.swf` above. This is not platform
compatibility but **a defect in the library itself**: without it no movie can
get its picture by name.

### 6. `gameswf/gameswf_player.cpp` — string concatenation

`"LINUX "__DATE__" "__TIME__` — since C++11 that is a user-defined suffix.
Spaces were added.

## What follows from this

Parsing works completely: labels, sprites, fonts, text fields, ActionScript
actions. What comes next are two separate jobs, and neither has been started:

1. **The bridge** — and it now comes first, not second. The probe showed that
   without `dice.bf2.*` the main menu does not play at all. It is not part of
   gameswf, it is what DICE wrote on top in `SwiffPlayer.cpp`
   (docs/functions/menu-bridge.md).
2. **A loader for external files.** The movie pulls pictures through
   `loadMovie` from `images/`; gameswf has to be given a way to open them with
   our `obf2::FileSystem` and decode them with our miniz and stb.
3. **A renderer.** gameswf draws through a `render_handler`, and only D3D and
   OpenGL ship with it. Ours has to map onto `obf2::gfx`. What exactly it has to
   be able to do is now visible from the counters above: mesh strips, line
   strips, colour and bitmap fills, colour transforms, masks.

The scale of the picture: `mainMenu.swf` — 2 MB, 320 frames, 1485 bridge names.
