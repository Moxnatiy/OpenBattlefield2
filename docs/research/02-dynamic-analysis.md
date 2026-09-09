# Dynamic analysis of the original

## The debugger: attaching kills the game

`winedbg attach` to the **32-bit** game in this Wine build (wow64) does
not work — and not "sometimes" but always:

```
WineDbg attached to pid 00d4
Unhandled exception: page fault on read access to 0xffd77574
  in wow64 32-bit code
```

To stop someone else's process Wine injects an interrupt thread into it.
Under WoW64 its entry point sits in the transition page `0xffd7xxxx`,
which the 32-bit side cannot reach, so instead of a stop you get a crash.
The same trace shows in the mtld3d log: `fault outside d3d9.dll,
code=0xc0000005, addr=0xffd77574`. The sidecar has nothing to do with it.

Two small things that can easily cost hours:

* `winedbg` reads the process id in **decimal**. `attach 0x150`, not
  `attach 00000150` — otherwise "error 87";
* `winedbg.exe` has to be the 32-bit one (`C:\windows\syswow64\`). The
  64-bit one will not attach to a 32-bit game at all.

**What does work:** starting the game *under* the debugger. Then no
interrupt thread is needed and stopping at the entry point happens
normally:

```sh
mkfifo cmd.fifo
tail -f /dev/null > cmd.fifo &        # the pipe must not close
wine 'C:\windows\syswow64\winedbg.exe' 'C:\bf2\BF2.exe' +loadLevel … \
     < cmd.fifo > dbg.out 2>&1 &
echo cont > cmd.fifo
```

The path to the game must have no spaces (`C:\bf2` as a symlink), because
winedbg parses its own command line.

After that you have to stop with **breakpoints set in advance**, not by
interrupting: interrupting a running `cont` is impossible. `SIGINT` does
nothing to the debugger, and while `cont` runs it does not read commands
at all — they wait in the pipe until the next stop.

### Which commands actually work

* `x /12x 0xa18898` — exactly like that. winedbg understands neither
  `x /12wx …` nor `x/12x …` ("No symbols found for x");
* `cont N` skips N−1 hits. If fewer are left, the game runs free and never
  stops again — so N has to be exact;
* `break *0xADDRESS` and `info reg` work as usual.

### The breakpoint has to be a rare one

Every hit is a round trip through wineserver, a few milliseconds. On
`PeekMessageA` that is about 5000 hits per second, and 60 000 pass before
the game loads anything; on `Sleep` loading stretches into tens of
minutes. Per-frame breakpoints are useless for this.

**A working breakpoint is the end of HUD construction:**

```
break *0x769c3e      the handler for hudBuilder.setTextNodeOutLineOffset
cont
```

That command occurs exactly five times in the whole HUD chain, all of them
in `HudElementsGameInfo.con`, the very last file. The first hit means the
HUD is assembled; the game runs to it at full speed.

How to find the handler of any other command: the string with its name →
the constructor that puts it into `+0xc` and a method table into `+0x0` →
in that table **slots 23 and 25** differ between commands, and those are
its own functions (slot 33 onwards is the name string itself; the table
has 33 slots).

### What is visible on a live game

Stopping really does let you read memory:

```
EIP:00769c3e  ECX:00a18a98  ESI:00000002
x /8x 0xa18a98
0x00a18a98: 0092b950 00000000 02161ca4 0092b9d4
```

That is the console command's object: `+0x0` the method table, `+0x8` a
pointer to the owning object's name (`"hudBuilder"` on the heap), `+0xc`
the command's own name, `+0x24`/`+0x28` the parsed arguments.

What has **not** been done yet: the HUD node tree in memory. The handler
takes its context from the global `0xa10890`, but behind it lies the
variable store, not the builder. Reaching the nodes needs the class's
layout — that is the next step.

## Where to look in memory

`tools/con_objects.py` pulls all 116 console objects out of the image
together with their static addresses (the image loads at 0x400000 with no
relocations, so the addresses hold in a live game too). The interesting
ones for the interface:

| object | address |
|---|---|
| `HudBuilder` | `0xa18898` |
| `HudManager` | `0xa18be8` |
| `HudItems` | `0xa1b0c4` |
| `SpawnManager` | `0xa15088` |
| `Scoreboard` | `0xa17028` |
| `Minimap` | `0xa14214` |

## The spawn screen is HUD, not Flash

Verified from the archives' contents. There are only five Flash (`swiff`)
files in the game and all of them are the main menu, loading and the end
of a round:
`External/FlashMenu/{mainMenu,menu,loadGame,endOfRound}.swf`.

The spawn screen, by contrast, is assembled by the ordinary `hudBuilder`
from its own root
`HUD/HudSetup/SpawnInterface/HudSetupSpawnInterface.con`, which
`HudSetupMain.con` calls alongside the rest. That is 71 distinct commands
and 2851 calls.
