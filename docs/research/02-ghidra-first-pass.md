# Ghidra: the first pass over BF2.exe

Date: 2026-08-25.

## What was done

`BF2.exe` (6.5 MB, 32-bit PE) was imported and fully analysed headless.
The project is `ghidra_projects/OpenBF2` (151 MB, not in git).

```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21 \
/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless \
  ghidra_projects OpenBF2 -import "Game Files/BF2.exe" -processor "x86:LE:32:default"
```

Result: **27 630 functions**, 11 061 strings. There are no symbols — the
function names have to be recovered by hand.

## The first useful extraction

`tools/ghidra_scripts/DumpConCommands.java` pulls everything that looks
like a `.con` command (`target.method`) out of the string table: **1735
commands**, saved in
[../reference/con-commands-from-exe.txt](../reference/con-commands-from-exe.txt).

```bash
analyzeHeadless ghidra_projects OpenBF2 -process BF2.exe -noanalysis \
  -scriptPath tools/ghidra_scripts -postScript DumpConCommands.java out.txt
```

This is no longer a guess about the language but **a list of what the
engine can actually do** — as opposed to what happens to appear in the
game's files.

## What it shows

The distribution across targets confirms the model we derived from the
data:

| Target | Commands in the binary |
|---|---:|
| `ObjectTemplate` | 784 |
| `hapticSettings` | 214 |
| `detonation` | 54 |
| `armor` | 41 |
| `fire` | 36 |
| `weaponHud` | 34 |
| `vehicleHud` | 32 |

The components we found from the data (`armor`, `fire`, `ammo`,
`deviation`, `recoil`, `zoom`, `target`, `newcar2`, `weaponHud`,
`helpHud`…) sit in the binary as **command prefixes of their own** — so a
sub-object really does have its own namespace rather than following a
naming convention.

Subsystems barely visible in stock BF2's data turned up too: `demo.*`
(demo recording and rendering), `dice.*`, `gameServerSettings.*`, and
`hapticSettings.*` with its 214 commands is support for Novint Falcon
haptic devices (hence `NovintHFX.dll` sitting next to the game).

## The conclusion for planning

At this stage Ghidra gave us **a reference, not logic**: the command list
is useful as a work plan, but each command's behaviour still has to be
worked out separately. Spending decompilation on formats that the data
already describes makes no sense — the level that loaded without any
reversing proved that.

Where it will genuinely be needed:

1. **The network protocol** — the one large piece that exists nowhere in
   the open. Start from the server binaries (`_w32ded`): no graphics, so
   much smaller.
2. **`terraindata.raw`** — the compiled terrain of the game branch. For
   now we go around it through the editor branch, but compatibility with
   the original will need it.
3. **The AI formats** (`.ahm`, `.qtr`, `.clb`, `.vbf`) — compiled as well.
