# The entry point: start-up → intro movies → menu

Status: **the first slice is done** — `src/engine`. `openbf2` with no
arguments goes through the start-up chain the same way the game does and
ends up in the menu with the real background from BF2's assets.

## What the game does at start-up

The order was recovered from the data and from `BF2.exe`'s string table:

1. Reads the settings — `Settings/VideoDefault.con`, `Settings/Video.con`,
   `Settings/GeneralOptions.con` (which does
   `run Profiles/<profile>/GeneralOptions.con`), `Settings/Sound.con`,
   `Settings/Controls.con`.
2. Plays the intro movies from `Movies/`: `EA.bik`, `Dice.bik`,
   `Legal.bik`, `Intro.bik`. Playback is switched off with
   `GeneralSettings.setViewIntroMovie 0`.
3. Shows the main menu.

All of it is ordinary `.con` going through **the console**: in Refractor 2
that is the single entry point for any command, whether from a file or
typed by the player (`IO/Console/Console.cpp` in the source layout).

## Two technologies we deliberately do not reproduce

**The intro movies are Bink** (`binkw32.dll` next to the game; `Intro.bik`
alone is 132 MB). **The menu is Macromedia Flash**, which the engine plays
with its own player: the string table shows
`dice.hfe.geom.FSMoviePlayer`, and the assets live in
`menu/external/flashmenu/`.

Reproducing either from scratch makes no sense — it is a lot of work for
2005 technology, and that is not the point of the port. Hence:

> **A caveat, September 2026.** The second half of that statement no
> longer holds. The Flash here is not "some Flash" but **gameswf** — a
> public-domain library, and it is exactly what DICE put into
> `External\gameswf\SwiffPlayer\`. So the "lot of work" is already done
> and sits under a licence that suits us
> (docs/research/00-prior-art.md). In the end we went with Ruffle
> (docs/research/11-ruffle-menu.md); the decision below stands as a
> choice, not as a dead end.

| What | Logic | Technology |
|---|---|---|
| Settings | 1:1, the same command and file names | ours |
| States and transitions | 1:1 (Boot → Intro → MainMenu → …) | ours |
| Intro movies | the order and the fact of playing are kept | Bink is not decoded |
| Menu | the original `mainMenu.swf` | Flash through Ruffle (11-ruffle-menu.md) |

The settings command names are the one case where deviating is **not
allowed at all**: the user writes those files, and they have to stay
compatible with the original.

The menu background is taken from
`menu/external/flashmenu/images/background/background_2.png`: the engine
draws it and the movie is laid over it with a transparent stage — there is
not a single reference to `images/background/` inside `mainMenu.swf`.

Careful with extensions: most "png" files in the game are in fact DDS
(details in 11-ruffle-menu.md), so the PNG decoder (`third_party/stb`) is
needed but on its own does not even cover the menu's assets.

## The port's main metric

The console counts commands with no handler — the most honest measure of
readiness:

```
console: handlers 30, commands executed 11, unknown 314
  no handler: chat.setchatmessagesize, chat.setkillmessagesize, ...
```

314 unknown commands from the start-up files alone is not an error but **a
work list in its purest form**. Together with the
[1735 engine commands](../reference/con-commands-from-exe.txt) pulled out
of the binary it shows exactly where the port is currently empty.

## Checking

```bash
./build/macos-arm64-debug/src/app/openbf2              # start-up → intro → menu
./build/macos-arm64-debug/src/app/openbf2 --level Dalian_plant
```

Space or Enter skips an intro movie, Esc quits.

## Next along this line

- The map list in the menu: `mods/bf2/Settings/maplist.con` and
  `Levels/*/Info/*.desc` are already read by our interpreter.
- Localisation: `ExcelLexicon.cpp` in the source layout, data in
  `Localization/`. Without it the menu has no captions.
- Fonts: `Menu/GameMenu/DifFont.cpp` — BF2's own format, needed for any
  text on screen.
