# The menu on Ruffle: visible for the first time

Battlefield 2's menu is Flash (proven by running the original, see
docs/functions/menu-bridge.md). The attempt to play it through **gameswf** —
the same library the original uses — ran into an endless loop
(docs/research/10-gameswf-on-arm64.md). This is the record of the second
approach, and it worked.

## What was taken

[Ruffle](https://github.com/ruffle-rs/ruffle) — a Flash player in Rust,
licensed **MIT or Apache-2.0**, compatible with ours. By the project's own
table — 99% of the ActionScript 2 language and 82% of its library.

The key thing for us: it has an **`exporter`** package that renders a movie's
frames **to PNG without a window**. So "is the menu visible" is checked with a
picture rather than by reasoning (rule 9).

Taken from source in `reference/ruffle` (not in git). No prebuilt binaries were
downloaded.

## Building

```
PATH="/opt/homebrew/opt/openjdk@21/bin:$PATH" \
  cargo build --release --package exporter
```

**Java is required**: Ruffle compiles its AS3 library with a Java compiler, and
without it the build fails with "Unable to locate a Java Runtime".
`openjdk@21` is already in the system, just not on `PATH`.

## Our changes in Ruffle

The patch is `tools/ruffle_bf2_bridge.patch`. Two things.

### 1. Visible `trace()` from the movie

Ruffle sends them to `tracing`, and the exporter had no subscriber — so the
movie was printing things that went nowhere. `tracing_subscriber` was added to
`exporter/src/main.rs`. It became visible at once how the menu starts up:

```
setFlaName mainMenu -> setDefaultLang -> initialize -> start
-> assignDelayedInstances -> Manager Spawns
-> UM: Register callback for: _level0.messageHandler
-> bg flush: NaN a: NaN
```

### 2. The menu -> game bridge

`core/src/avm1/globals/bf2_bridge.rs` (the source also lives here:
`tools/ruffle_bf2_bridge.rs`). It puts the bridge's seventeen objects into
`_global` — both under their own names (`Logic`) and under the full ones
(`dice.bf2.Logic`), because the movies use both forms.

The trick that made this tiny: **`__resolve`**. It is ActionScript 2's built-in
"property not found" hook, and Ruffle supports it. So there is no need to list
all 1485 names — the object answers any of them and prints along the way what
was asked of it. The "object -> method" list comes **from the movie itself**
again.

The stub returns `false`, not `undefined`: in AS2 `undefined` is treacherous in
comparisons, while `false` is an honest "no": there are no servers, no profile,
no battle in progress.

## The result

```
exporter mainMenu.swf output.png --frames 60
```

Without the bridge all 60 frames are identical: the movie stands on the first
one waiting for the game. With the bridge the **menu builds**: the welcome
dialog, the headings, `BUTTON_singleplayer`, `BUTTON_online`, the button in the
top right.

Over the first 60 frames the movie called the bridge 63 times, twelve distinct
names:

```
Locale.getString          Logic.getFrameLabel      Profile.getLoginStatus
Logic.active              Logic.getModVersion      Profile.getNumLocalProfiles
Logic.getActivateReason   Logic.getStorageString   Profile.logout
Logic.getCommandline      Logic.isGameRunning      Logic.setStorageString
```

That is no longer a guess about who belongs to whom but an observation.

## Real data instead of stubs

The game keeps its profiles not next to itself but in
**`Documents/Battlefield 2`** (on Windows, "My Documents"). There:

```
Profiles/Global.con          GlobalSettings.setDefaultUser "0001"
Profiles/0001/Profile.con    LocalProfile.setNick "defaultPlayer"
                             LocalProfile.setNumTimesLoggedIn 47
Profiles/0001/*.con          Audio, Controls, Video, ServerSettings, mapList
```

The bridge now reads them itself (the path can be overridden with the variable
`BF2_DOCS_DIR`, the game's with `BF2_GAME_DIR`):

| method | where it takes it from |
|---|---|
| `Client.getPlayerName`, `Profile.getStatPlayerName`, `Profile.getActivePlayer` | `Profile.con`, `setNick` |
| `Profile.getNumLocalProfiles` | how many numeric directories are in `Profiles/` |
| `Profile.getNamePrefix` | the default profile's number from `Global.con` |
| `Locale.getString`, `Locale.loadString` | `Localization/English/*.utxt` — 4658 strings from seven files |

**And that had a visible consequence at once.** As soon as
`getNumLocalProfiles` started returning a real one instead of "no", the menu
switched from the welcome screen to the **login screen**
(`LOGIN_loginusing`, `LOGIN_manageaccount`). So the movie already behaves
according to our data rather than to a stub.

## The labels: why they are still keys

The dictionary reads correctly — `MAINBUTTON_6` gives "QUIT",
`WELCOME_welcome` gives "Welcome, Recruit!". The movie **asks** for exactly
those keys, and we **return** the right strings. But the key stays on screen.

Verified directly: the stub started returning text in square brackets
(`[QUIT]`) — nothing on screen changed. So our answer does not reach that label
at all.

The cause is visible from the list of what the movie calls on `Locale`:

```
addDelayedInstance  checkXMLStatus  getLanguage
initialize  loadString  setDefaultLang  setFlaName
```

So the menu has its **own deferred translator**: a component sets a key as its
own label, marks itself `isLocalized` and registers through
`addDelayedInstance`, and the text is replaced later, when the manager decides
the translation is ready. Our stub for `addDelayedInstance` remembers nothing,
so no replacement happens.

That is the next link, and it is taken apart the same way as everything else:
look in the byte code at what `addDelayedInstance` does and under what
condition the manager walks the registered instances.

## What is still not in the picture

* **The labels** — see the section above: the dictionary is connected, but the
  movie takes the text through its own deferred translator.
* **The background** — the menu pulls pictures from `images/` through
  `loadMovie`; those are external files, and we have not given it a loader for
  them yet (`bg flush: NaN` in the log).
* **The data** — servers, the profile, the maps: all of that is a real bridge,
  not a stub.

## Integration into the client

Done. `openbf2 --flash <file.swf>` shows the movie through **our own** drawing
path: the screenshot above was taken with

```
openbf2 --flash "…/mainMenu.swf" --width 1024 --height 768 \
        --frames 90 --screenshot menu.png
```

How it is put together:

| part | where |
|---|---|
| the library with a C interface | `src/flash/src/lib.rs` (Rust, `cdylib`) |
| the C++ wrapper | `src/flash/include/obf2/flash/movie.h`, `src/movie.cpp` |
| the build | `src/flash/CMakeLists.txt` — `cargo` as a CMake target of its own |

The interface is deliberately narrow: open, size, step, draw, close. The frame
arrives as ordinary RGBA, and we put it on screen ourselves — all graphics in
the port go through `obf2::gfx`, and Flash is no exception. In `main.cpp` the
frame is handed to the texture resolver under the name **`#flash`**, just like
colour fills (`#20344a`): it is not a file but an image we made ourselves. Then
comes an ordinary full-screen quad (`buildScreenQuad`) in the same interface
pass as our own menu.

The RGBA from Ruffle is converted to BGRA: our texture loader expects the
channel order of a DDS.

**The module is optional.** With no `cargo` or no `reference/ruffle` CMake
simply skips it, `OBF2_HAVE_FLASH` is not defined, and the client builds as
before. The rest of the port does not depend on it; 32 tests pass, and a run
without `--flash` is unchanged.

## Input: the menu became clickable

Without a mouse it was a picture. Now `src/flash` takes four more things —
mouse movement, the button, a character and the control keys — and `main.cpp`
takes them from the ordinary `Device::readInput()` and converts the coordinates
from the window into the movie's stage (`Movie::toStage`).

One detail turned out to be mandatory: **a click has to be sent as two
events**, down and up on different frames. If both are sent at once, Flash does
not separate them and the button does not fire.

Verified with nobody at the keyboard, the same way as the other screens —
`--mouse X Y --click`:

```
openbf2 --flash mainMenu.swf --mouse 372 530 --click --frames 90 --screenshot click.png
```

Clicking `LOGIN_manageaccount` **moved the menu to the account management
screen** — with the tabs `LOGIN_manageaccount` / `LOGIN_retrieveaccount` and
the buttons `BUTTON_back`, `BUTTON_forgotpassword`, `CREATE_createprofile`. So
in our client the menu is not merely drawn, it walks its pages.

## A loader for external files

The movie pulls pictures (`loadMovie("images/…png")`) and, as it turned out, the
translation. Ruffle already had what was needed:
`NullNavigatorBackend::with_base_path` reads local files relative to a given
directory. The directory is the one the `.swf` itself lies in.

Plus a log switch: `OBF2_FLASH_LOG=info` turns on `trace()` from the movie and
the player's complaints. It is off in an ordinary run so as not to get in the
way.

## The labels: what the log showed

The keys remain, and the cause is now named more precisely. In the byte code it
is visible that the label is set by the **standard Flash component**
`mx.lang.Locale` (init block 17, sprite 179), not by the menu's own code:

```
push reg2, "text", reg1, 1, "dice"
GetVariable ; push "bf2" ; GetMember ; push "Locale" ; GetMember
push "getString" ; CallMethod ; SetMember
```

That is `<field>.text = dice.bf2.Locale.getString(<key>)` — and that is our
bridge, which returns the right string. But it is done in
`assignDelayedInstances`, which in the original is called **after the XML
loads** (`addXMLPath` -> `onXMLLoad` -> `checkXMLStatus`). We have no XML, so
the pass happens once and too early — when the components do not exist yet.

The log added two more things, both worth a pass of their own:

* `Unknown device font "Helvetica"`, `"TradeGothic LT BdCondTwenty"`,
  `"MorganSnCnCaps"` — the menu asks for system fonts Ruffle does not have;
* `Avm1::pop: Stack underflow`, many times — somewhere the movie's byte code
  pops more than it pushed. Whether that is a consequence of our stubs or a
  limit of Ruffle is not established yet.

## `mx.lang.Locale` taken apart: BF2 replaced the standard component

The menu pulls its text not with its own code but with the standard Flash
component `mx.lang.Locale` — and DICE **replaced its insides**. Init block 17
(sprite 179) has been taken apart in full:

```
addDelayedInstance(instance, stringID)   ; the parameters are in registers 2 and 1
    if stringID.length > 0:
        instance.text = dice.bf2.Locale.getString(stringID)

assignDelayedInstances()   ; the same in a loop over instanceObjects
initialize() -> start() -> assignDelayedInstances()
checkXMLStatus()  ->  always false      ; hard-coded
addXMLPath()      ->  nothing
instanceObjects, instanceIds — empty arrays, nobody fills them
```

So there is no "deferral" here: the text is assigned **immediately**, and its
source is our bridge.

**And it really is called correctly.** The log shows
`getString("LOGIN_loginusing")`, `("MAINBUTTON_6")` — exactly the keys on
screen, and the dictionary gives "LOGIN" and "QUIT" for them.

But **the value does not land**: a trial where `getString` returned `"ZZZZ"`
did not change a single pixel on screen. So `instance.text` is being
overwritten with the key by someone. In the components nearby one can see
`getLocID`, `isLocalizationID`, `__set__isLocalized` — so they have their own
second path to the label. That has not been taken apart yet.

## Author mode: `Logic.active`

The first thing the menu asks the engine is `Logic.active`. While the answer is
"no", the components stay in author mode (`m_inAuthorMode`, `authorModeUpdate`,
`showAuthColLook` in their code).

As soon as the bridge began returning `true` for it, **the colour scheme
changed from the green "author" one to the real one** — dark. That is not
cosmetic: it shows the flag really does govern how the menu draws itself.

The labels and the background did not change from it, so they have a separate
cause.

## Two different label paths — and why the keys remain

Taking it apart proved there are **two** paths in the menu, and they must not
be confused.

**The first — text fields on the stage.** This is the standard code Flash
itself generates (the Strings panel):

```
if (mx.lang.Locale.checkXMLStatus())
    field.text = mx.lang.Locale.loadString("IDS_…");
else
    mx.lang.Locale.addDelayedInstance(field, "IDS_…");
```

`checkXMLStatus` in this movie hard-codes `false`, so the second branch is
always taken — and DICE replaced it with an immediate
`field.text = dice.bf2.Locale.getString(id)`. **That path goes through our
bridge and works.**

**The second — the components' labels.** Here is where I first drew a wrong
conclusion, and it is worth writing down together with the correction.

A component's constructor (`TextContainer`, symbol 117, 0x945) does this:

```
init(); draw(); createChildren(); arrange(); setEvents();
```

`createChildren()` calls `setLocID(m_localizationId)` on its last line, and
that calls `mx.lang.Locale.addDelayedInstance(field, key)`. The very next
`arrange()` writes **the key itself** into that same field:

```
arrange():  with (m_mcLabel) with (labelTextField)
                if (_parent._parent.m_localizationId.length > 0)
                    text = _parent._parent.m_localizationId;   ; the key
                else
                    text = _parent._parent.m_label;
```

From this I concluded that the translation must arrive ready-made from outside.
**That was wrong.** The key word is *delayed*: in a real `mx.lang.Locale`
`addDelayedInstance` only **remembers** the field, and the text is handed out
later by `assignDelayedInstances`. The stock components are built for that:
`arrange()` calmly writes the key, because the translation will land on top
**after** the construction.

It broke because we were using the **fallback** implementation from the movie
itself, and that one sets the text immediately — and `arrange()` wiped it. The
fallback is enabled only when the class is absent:

```
symbol 179's DoInitAction block:  if (_global.mx.lang.Locale) { do nothing }
```

So in the game `mx.lang.Locale` is supplied by the player itself — and
`SwiffPlayer.dll` really does have an object with exactly the stock set
(`addDelayedInstance`, `checkXMLStatus`, `getLanguage`, `initialize`,
`loadString`, `setDefaultLang`, `setFlaName`, 0xd7af4).

Now we do the same: `tools/ruffle_bf2_bridge.rs` puts its own
`_global.mx.lang.Locale` with a **real queue** in place, and it is flushed
after a frame — from `Player::run_frame`, right after `Avm1::run_frame` (two
lines in `tools/ruffle_bf2_bridge.patch`). The labels became translated:
`LOGIN`, `MANAGE ACCOUNTS`, `QUIT`.

Incidentally it turned out that `loadString` in the rest of the movie is **not
a localisation method** but the menu's own helper that assembles picture paths
(`loadString("images/awards/front/AWARD_NAME_" + … + ".png")`). Our mapping of
`Locale.loadString` onto the dictionary was unnecessary.

## The background is drawn by the engine, not by the movie

`mainMenu.swf` contains **not a single** reference to `images/background/`,
even though it loads the rest of the pictures itself. The menu's background is
put there by the engine, and Flash goes on top — so we turn the stage
transparent (`Player::set_window_mode("transparent")`) and put under the movie
in `--flash` the same picture our own menu used.

The movie does ask for its own background, but differently: `loadImg: ?menu.bik`.
The question mark is a request to the player, and `menu.bik` is a Bink video. We
do not decode Bink (docs/research/03-startup-and-menu.md), so a still picture
stays in that place. That is **deliberate debt**, not a bug.

## Time, not frames

At first we ran the movie with a direct `Player::run_frame()`. The menu stood
still: it rests on `setInterval` (the update manager registers its calls in it —
"UM: Register callback for … freq: 1"), and timers are counted from time. Now we
call `Player::tick(one movie frame)`, and along with it
`PlayerBuilder::with_autoplay(true)` — without autoplay `tick` does nothing at
all.

## The string store: why everything was `false`

The most expensive stub turned out to be the cheapest to fix.
`Logic.getStorageString`/`setStorageString` is the memory between the menu's
pages, and the movie leans on it constantly. While we answered `false`, the log
held `LOGIN GOING TO: false` and `loadImg: false`, and the frame held
`loadMovie("false")`.

The layout was taken from the binary (`SwiffPlayer.dll`, 0x1004d0e0,
0x1004db60, 0x1004d170): `set(key, value)`, `get(key, default)`, `erase(key)`.
None of the keys used (`lastFrameLabel`, `currentFrameName`, `BGmovie`,
`noobieSP`) is in `BF2.exe`, in the game's files or in the profile — so the
store lives only in the session's memory, and that is how we made it.

## Lists: `dice.General`, and why they were invisible

The profile list on the login screen stayed empty for a long time, and the cause
turned out to be twofold.

First, a list's name lies **not in the byte code** but in the instance's
parameters — that is, in the `ClipActions` of the `PlaceObject2` tag, where our
disassembler did not look. Because of that `listId "localProfiles"` was not
found by a string search. Now `tools/swf_disasm.py --tag 26` reads them too:

```
listId              localProfiles
columnTexts         Online, Playername
columnWidths        120, 200
columnToUseTextFrom 1
callBackFunction    selectProfile
```

Second, the movie calls `dice.General`, **not** `dice.bf2.General` — while we
put the object only into `dice.bf2`. The API itself is taken apart in
docs/functions/menu-bridge.md.

The reply format of `getListEntries(id, start, count)` was taken from
`ColumnList.fillList` (symbol 42, 0x3a24): it is an array in which **`[0]` is
the header** (`totalCount` hangs on it, and `[0][0]` goes into every row as the
"list object"), and the rows start at index **1**. Every row is itself an array:
`[0]` is the record's object (`id`, `disabled`), then the columns' values.

Now the login screen shows the user's real profile from
`Documents/Battlefield 2/Profiles/0001/Profile.con`: `Offline` and
`defaultPlayer`. Clicking the row opens `PREFIX` and `SELECT ACCOUNT` — so the
menu is not a picture, it works.

At the same time the screen corrected a mistake of ours: the `PREFIX` field held
"0001", because we had bound `Profile.getNamePrefix` to the profile's number. In
fact it is a separate line in `Profiles/Global.con` —
`GlobalSettings.setNamePrefix ""`.

## Name case: we reproduce SwiffPlayer, not Flash Player

After logging in, the bottom read "ACTIVE ACCOUNT **Label**" — the label stayed
at its default. The cause is in how the movie sets it:

```
textString = Profile.getNamePrefix() + " " + Profile.getActivePlayer();
this.textAnim.profiletext.setvalue(textString);
```

`setvalue` is lower-case, while the component's method is called `setValue`.
`mainMenu.swf` is version 7, and by Adobe's rule property lookup in version
seven is **case-sensitive**, so Ruffle silently failed to find that call.

But the menu is played not by Flash Player but by SwiffPlayer, built on gameswf
— and there properties live in a `stringi_hash`, that is
**case-insensitively** (`reference/gameswf`, `gameswf_object.h:44`). That is
why the original works.

We reproduce the original player, so we take its behaviour:
`Avm1::is_case_sensitive` in our patch is always `false`. The label became the
profile's name at once, and the menu's tinting was fixed along with it — it
turned out there is more than one such wrong-case call in the movie.

That is a **deliberate departure from the Flash standard** and the only place
where we change Ruffle's behaviour rather than add to it.

## Clicking in a window: the input was read twice per frame

In a window none of the movie's buttons worked, even though the synthetic
`--click-at` walked the menu to the end. The cause is not in Flash:
`Device::readInput()` returns `clicked` as a **transition** from released to
pressed and immediately remembers the new state. In menu mode we called it twice
per frame — first for our own menu, then for the movie — and the second call
always saw "not pressed". The synthetic path worked because it set the flag
itself.

Now the movie takes the same `menuInput` as our own menu. The second mistake
nearby: the release went to the current cursor, and by the next frame it is
somewhere else — Flash counted that as "released outside the button". Now we
release at the same place we pressed.

## The font: we take the embedded one, as gameswf does

The menu was drawn with a wide font instead of a narrow one. The movie carries
its own fonts (`Helvetica`, `Helvetica Condensed`, `TradeGothic LT
BdCondTwenty` — 241–242 glyphs each), but some fields are marked as **device
fonts**: under the same name the movie holds an empty placeholder with zero
glyphs, which is Flash's way of saying "this field is drawn by the system".

In the game they are drawn by gameswf, and its order is: first a system font
**by name** (`m_fontname`), with the embedded glyphs as the fallback
(`reference/gameswf`, `gameswf_font.cpp:306`). We have no system
`TradeGothic LT BdCondTwenty`, so the fallback ought to fire — but Ruffle does
not consider embedded fonts in that place and takes its own Noto Sans.

Added to the patch: before giving up, look for a font from the movie itself **by
name**, ignoring bold/italic and skipping the empty placeholders. The last part
matters: an exact match found precisely the placeholder, and then the labels
vanished altogether.

## The diagonal stroke on the buttons: a bug in Ruffle, and it is ours

The user supplied a screenshot of the original — there is no stroke there. So it
is our defect, and it was narrowed down by experiments rather than guesses.

**Step 1: it is a stroke.** We temporarily disabled the drawing of all `Stroke`s
— the line disappeared together with the buttons' frames.

**Step 2: what the strokes actually draw.** We disabled the fills and left the
strokes alone: a **rectangle with a diagonal from corner to corner** is visible.

**Step 3: there is no diagonal in the movie.** The shape is drawn not from the
file but by ActionScript calls (`shape id=0` — that is how Ruffle marks a
drawing made through the API). The code in `StandardButton` (symbol 344) is a
bevel:

```
lineStyle(0, m_sliderBevelTopLeftColor, 100)
moveTo(100, 0);  lineTo(0, 0);  lineTo(0, 100)
lineStyle(0, m_sliderBevelBottomRightColor, 100)
lineTo(0, 100);  lineTo(100, 100);  …
```

Not a single diagonal `lineTo`. The drawing starts at `(100,0)` and ends there
too.

**Step 4: the cause.** In Ruffle a stroke **without a fill** is marked closed
like this (`core/src/drawing.rs`):

```rust
let is_closed = self.cursor == self.fill_start;
```

`fill_start` is the last `moveTo` of the **whole** drawing, not the start of
this run. Changing `lineStyle` in the middle of a figure starts a new run from
the current point `(0,100)`, but the flag is computed against `(100,0)` — and it
matches. Ruffle closes the run, and `close()` draws a line from `(100,0)` to
`(0,100)`. That is the diagonal.

Fixed in the patch: the flag is computed against the run's **own** start.
Together with the buttons the radio buttons were fixed too — they were being
struck through as well.

## What the reference settled, and what it did not

The screenshot of the original closed three questions:

* **the stroke** — our defect, fixed (above);
* **the radio buttons** — fixed by the same change;
* **the version at the bottom** — the original shows `1.5.3153-802.0`, we had
  `BF2 1.5` from `mod.desc`. It turned out I had bound `Logic.getModVersion` to
  the wrong thing: despite the name it does not touch `mod.desc` but asks the
  **player's** version (`SwiffPlayer.dll`, 0x1004d520, engine method +0x1a0 —
  the same one `General.getHostVersion` uses). Now we return our own.
  `Mod.getModVersion` (0x10050260) asks the mod manager — that one was left on
  `mod.desc`.

**Not closed: the chevrons on `PLAY NOW`.** In the original that is
`images/components/playNow.png`, and this chain puts it there (symbol 344):
`LCD == 8` -> `m_useSpecial = true` and
`m_specialUrl = "images/components/playNow.png"` -> `createChildren` makes
`m_mcButtonSpecial` -> `draw` calls `loadMovie` on it.

For us the picture is **never requested at all** — so the `LCD == 8` branch does
not run at construction time. The `LCD` parameter lies in the instance's
`Construct` event (there are three such instances), and Ruffle runs `Construct`
events before the class's constructor — so the order is right. Where it breaks
has not been found. The guess "an empty clip gets scale 0" was **checked and
rejected**: with the scale zeroing disabled there are still no chevrons.

## The diagonal stroke: the first guess, which was not confirmed

A thin diagonal from corner to corner is visible on the buttons and the input
fields. The very first explanation suggested itself: every component has a
placeholder rectangle `mcBoundingBox` (18 instances — exactly one per component
class, `StandardButton` and `InputField` included), and each component's
`init()` hides it:

```
if (this.mcBoundingBox) { _visible = false; _width = 0; _height = 0; }
```

We checked directly: hid all 23 such clips at creation — **the stroke stayed**.
So it is not the placeholder but the button's own graphics.

Whether it is there in the original we cannot say: we have no reference
screenshot of the real game's main menu (the same gap as for the battle HUD).
That is the next step: take the original's menu through `tools/bf2_run.sh` and
compare.

## One menu, not two

While the movie played, our own menu stayed underneath it with its invisible
buttons — and a click "past" the movie ran its console command, which is why a
map would suddenly load out of the background. Now, when the movie is open, our
menu takes no input at all: `flashMenu` in `main.cpp`.

## The input was read twice per frame

`Device::readInput()` returns `clicked` as a **transition** from released to
pressed and immediately remembers the new state. In the frame loop it was called
four times: for our own menu, for the movie, for the game and for the spawn
screen. The second and later calls always saw "not pressed".

Because of that the movie's buttons in a window did not work **and** neither did
the `DONE` button on the spawn screen while our own server was running. The
synthetic `--click` paths worked because they set the flag themselves — which is
why it went unnoticed.

Now the input is read once per frame (`frameInput`) and shared with everyone.

Right after that the other side of the same trouble surfaced — **an offset**. We
were converting coordinates from the window into the movie's stage by the size we
had *asked for*, not the one we were given:

```
GPU backend: metal | window 1221x916 (asked for 1600x1200)
```

That is, we divided by 1600 instead of 1221, and the cursor for the movie ended
up a third of the screen to the left and above the real one. Now the size comes
from `SDL_GetWindowSize`: SDL's mouse arrives in window points and the movie is
stretched over the whole window, so the conversion is a simple proportion.

## The game's pictures are DDS under a `.png` name

The map preview would not appear, and the cause turned out not to be in Flash:
`Levels/<map>/Info/sp1_16_menuMap.png` is actually DDS/DXT1. That is how most
"png" in the game is stored at all: icons, flags, awards.

The original player decodes them because it receives a texture from the engine,
not a file. We did the same: `src/flash/src/images.cpp` hands the library our
decoder (`obf2::texture::decodeRgba8`, with BC1/BC2/BC3 decompression added
together with a test), and `GamePaths::fetch` substitutes a PNG for the reply's
body. No second decoder appeared in Rust.

At the same time the navigator learned the game's paths: a leading `$` means the
mod's root (`$/Levels/…`), as in `fileManager`.

After that the "EASY / VETERAN / EXPERT" switch icons appeared on the
single-player screen. **The map preview is still absent**, and honestly: the
cause is not proven. The suspicion is the order: `loadImg` calls `loadMovie`,
and **immediately after it** `setImageSizes()` sets the container's
`_width`/`_height`. In the original the load is synchronous (the engine returns
a texture at once), in Ruffle it is not, so the size is set on an empty clip.
That is the next step.

## What this means for the port

Ruffle does what gameswf could not: it **plays the same movie the real game
opens**. The price is Rust in the build and a C ABI to our C++. The decision has
since been taken in favour of Ruffle — see `docs/TODO.md`.
