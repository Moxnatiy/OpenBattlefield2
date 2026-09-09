# Other people's work: what has been cross-checked

Three repositories sit in `reference/` (not in git, see `.gitignore`). They
are a **source of hints, not of code**.

| repository | licence | what may be taken from it |
|---|---|---|
| `kiwidoggie/breadflowerdos` | **none** | reading only. Without a licence code is unfree by default — nothing may be copied. |
| `matthias-hoste/Refractor-2-BitStream-Emulator` | **GPL-3.0** | incompatible with our MIT. We read it as a description, we do not carry code over. |
| `cetteup/BF2AutoSpectator` | MIT | compatible, but it is a Python bot — only constants are of any use. |

So the rule is simple and the same as for decompilation: we take
**statements** from there and then verify them against the original — on the
binary or on a live server. Below is what has already been verified.

## The level block: the reference did not match

`Refractor-2-BitStream-Emulator` describes the block like this: mode, path,
level name — each with a u16 length, then 1+31 bits of players, 1 bit of
commander, 1+31 bits of the **challenge number**, and two more bits.

Our server (`v1.5.3153`, Linux) sends something else — exactly 28 bytes:

    01 00 00 00 | 0c 00 "dalian_plant" | 06 00 "gpm_cq" | 10 00

That is a u32, then the **level name**, then the mode, then a u16 size. No
path, no 31-bit fields and no challenge number are there at all — they do not
fit. Different builds; for us the right one is what comes off the wire.

## The challenge number: how the server picks it

Verified on BF2_r.exe (0x559b40): the server takes `rand() % 10` and
immediately logs `server chose challenge ordinal N`. That is why
`std_archive.md5` and `levels/<level>/archive.md5` have ten lines each,
numbered 0..9.

The block's first number is not it: our capture has 1 there, while 5 works
(verified with both values back to back on a live server: with 5 come 72 data
packets and ghosts, with 1 — eight packets and a drop).

It is not derived from the challenge string either: two consecutive
connections gave `mpcwgbqgz` and `dtubbwkxr`, while the working number stayed
the same.

So the question is still open: **where the real client learns the number**.
Beside it in the binary there are strings about the stats server ("unable to
recieve challenge from stats server ... content checking will not work"), so
perhaps the number comes from that side and `rand()` is the fallback.

## The challenge event: it matched

Ten random letters and the mod's name — exactly what we already read.

## Dynamic analysis: BF2.exe under CrossOver

Static analysis of the HUD runs into what is not visible in the data, so we
have to be able to bring the original up. `tools/bf2_run.sh` does that.

What turned up along the way:

**Graphics.** The bottle was going through DXVK 3, and that wants Vulkan 1.3 —
MoltenVK on an M1 Pro gives 1.2, and no adapter was ever found. Two routes
work: `Sikarugir-App/d9vk` (DXVK 1.10, via MoltenVK) and `athei/mtld3d` (D3D9
straight into Metal). The second is closer to home: it creates an `MTLDevice`,
attaches an 800x600 layer and has its own `mtld3d-encoder` / `mtld3d-submit`
threads. It installs entirely inside the bottle, `CrossOver.app` is left alone
(it is write-protected anyway).

**Full screen.** The game asks for `800x600@85`; there is no such refresh rate,
and `EnterFullscreenMode` fails. In a window (`+fullscreen 0`) all is well.

**The "BF2 Error" dialog.** It is not fatal, and that is visible in the code
(`BF2.exe` 0x44a0f0):

    call MessageBoxA
    cmp eax, 0xa      ; "Try Again" -> int3, a deliberate crash
    cmp eax, 0xb      ; "Continue"  -> simply returns
    push 0x2a         ; "Cancel"    -> exit(42)

Every launch of ours died at `0x44a0fb` for exactly that reason: the dialog was
being answered "Try Again".

**A patch on RendDX9.** At `renddx9+0x74ee1` sits the same trick: `int3` plus a
deliberate write to zero. Beside it is the function that translates a message's
level (`Debug`, `Info`, `Warning`, `Assert`, `DxAssert`, `Error`, `Log`), so
this is the stop on Assert. Eleven bytes were replaced with `nop`: exactly the
same as always pressing "Continue". The original is not modified — the bottle
holds a directory of links to the game plus one patched copy of that library.

Then: `Unknown DynamicOption value 800x600` — the mode list has the form
`%ix%i@%iHz` while the game looks for the string without a refresh rate. And
after that a crash in `renddx9+0x14a3e`, this one genuine: something in the
renderer failed to be created, and that is exactly what the Assert warned
about.

### Where exactly the game crashes under mtld3d

The chain is visible from the backtrace and three disassembled places.

`renddx9+0x14a3e` is a setter: it takes a pointer as a parameter, puts it into
the object's `0x8` field and immediately reads its method table:

    movl 0x8(%ebp), %eax     ; the parameter
    movl %eax, 0x8(%ebx)
    movl (%eax), %edx        ; ← the crash, eax = 0

Who passed the zero is visible in its caller (`renddx9+0x184c4`):

    pushl &out
    pushl $0x10208590        ; the GUID
    pushl %eax               ; this
    calll *(%edx)            ; slot 0 = QueryInterface
    movl  0x20(%ebp), %eax   ; the result

At address `0x10208590` lies `580CA87E-1D3C-4D54-991D-B7D3E3C298CE` —
**`IID_IDirect3DBaseTexture9`**. The object being asked is alive (there is a
`testl %eax,%eax` before it), so it is `QueryInterface` itself that returns
zero: the driver does not hand that interface out.

That is a hole in mtld3d, not in the game, and it is named precisely — one can
either go upstream with it or try d9vk at the same spot.

### Empty settings lists

Four dialogs in a row said more than one: `800x600`, `Off`, `800x600@60Hz`,
`Off` again. **Not one** value matches, not even `Off` — so every list is
empty, not just the mode list. And every dialog has an empty "Current
confile:" line.

The cause turned up in the data: `mods/bf2/Settings/Video.con` says
`run Profiles/Custom/Video.con`, and there is no `Profiles/Custom` directory in
the installation. It was created (copies from the Default profile), but the
crash stayed the same — so it was necessary but not sufficient.

### Why the original would not start: the refresh rate, not the resolution

Guessing did not help here, but a tiny program did. `tools/d3d9_modes.c` asks
the driver directly: how many adapters, how many modes, which ones. It builds
with mingw and runs in the same bottle.

The answer:

    adapter modes: host 3024x1964@120Hz aspect=1.540, 20 entries
    3024x1964 @120Hz  2880x1800 @120Hz  2560x1600 @120Hz
    1920x1200 @120Hz  1680x1050 @120Hz  1440x900  @120Hz
    1280x800  @120Hz  1024x768  @120Hz  800x600   @120Hz
    640x480   @120Hz

So `800x600` **is** there. What does not match is the refresh rate: mtld3d
gives every mode the display's rate, and on ProMotion that is 120 Hz. The game
looks first for `800x600` and then for `800x600@60Hz` — and finds neither.

That is visible in its own sources (`windows/d3d9/src/direct3d9.rs`): there is
a `RES_BANK` of standard sizes, a filter by aspect (`ASPECT_TOLERANCE = 0.15`,
and 4:3 at 1.540 passes with 0.135 to spare), and a single refresh rate for
everything — `host_hz`.

From here there are two ways out, both simple: switch the display to 60 Hz, or
teach mtld3d to offer every size at 60 Hz as well. The second is better for
upstream but needs an x86_64-Wine tree with development files, which the system
does not have — `/usr/local` is arm64-Wine, and CrossOver does not ship
development files.

At the same time it became visible that `800x600` is the game's **built-in
fallback**, not a profile: the string sits in RendDX9 next to `1024x768` and is
applied before any .con has been read. So fixing the profile was pointless.

### The desktop: `explorer /desktop=`, not `cxstart --desktop`

This is what actually broke the launch, and it is visible only from a
measurement.

The game takes its modes not from D3D9 but from Win32 — in RendDX9 a filter
reads a packed structure and keeps only what has a depth of exactly 32 bits and
a width no smaller than 800:

    cmpb $0x20, 0x6(%esi)   ; depth == 32
    cmpw $0x320, %ax        ; width >= 800

`tools/d3d9_modes.c` showed that D3D9 hands out a full list including
`800x600`. But `EnumDisplaySettings` under `cxstart --desktop` handed out
something else entirely — 396 entries, all of them scaled for Retina:

    960x600   1024x640   1024x665   1147x716 ...
    current: 1512x982

Neither `800x600` nor `1024x768` is there. So `cxstart --desktop` creates no
desktop: the game stays on the real display and sees its modes.

Inside a real Wine desktop (`wine explorer /desktop=bf2,800x600`) the list is
standard — `640x480`, `800x600`, `1024x768`, `1152x864` — and every assert
disappears. Verified with the **original** RendDX9, unpatched: not a single
`int3`.

One crash was left, and it is genuine: `renddx9+0x14a3e`, a zero from
`QueryInterface(IID_IDirect3DBaseTexture9)`.

### A debugger instead of guesses

`winedbg` takes a command line and reads commands from stdin, so it can be
driven hands-free:

    printf 'c\nbt\nframe 1\nx /4x 0x22f354\nq\n' \
      | "$CX/bin/wine" --bottle bf2bottle winedbg "$GAME\\BF2.exe" +menu 1

One caveat: `explorer /desktop=` swallows the child's stderr, and the driver's
log vanishes. So the desktop is set in the registry
(`HKCU\Software\Wine\Explorer` `Desktop`=`Default`,
`...\Explorer\Desktops` `Default`=`800x600`) — it works the same way and the
output stays ours.

### mtld3d has nothing to do with it

Verified on a full trace (`RUST_LOG=mtld3d=trace`): over the whole run there
was **not a single `QueryInterface`** to the driver's objects. So the zero the
game dies on is born inside RendDX9, not in the driver.

Two more suspicions were cleared at the same time. `GetAdapterDisplayMode`
returns format 22 (X8R8G8B8) — correct. And the `CheckDeviceFormat` refusals
with adapter format 21 (A8R8G8B8) are correct too: such a format is not a
screen mode, and Windows answers the same way. The game simply substitutes it
because the profile says "32 bits".

One thing was left: step through from `renddx9+0x18742` and see what object it
passes on. The frame's first argument turned out to be `0x100`, that is not a
pointer — so the object does not come from there.

### The defect found: a texture's QueryInterface matches nothing

The chain has been carried to the end, and every step is measurable.

The debugger gave the object the game asks an interface of: `[ebp-4]` of frame
1. Its method table lies inside `d3d9.dll` (mtld3d), and the string next to it
is `d3d9::texture` / `d3d9/src/texture.rs`. So it is **an mtld3d texture**, and
its `QueryInterface` ought to accept `IID_IDirect3DBaseTexture9`.

`tools/d3d9_qi_test.c` checks that in twenty lines: it creates a device,
creates a texture and asks it for three interfaces. The answer:

    CreateTexture(A8R8G8B8): 0, tex=00E60120
    QI(IDirect3DBaseTexture9): hr=0x80004002 out=00000000
    QI(IUnknown):              hr=0x80004002 out=00000000
    QI(IDirect3DTexture9):     hr=0x80004002 out=00000000

A refusal even of `IUnknown` — mandatory by the specification — and even of its
own `IDirect3DTexture9`. The texture is created but hands nothing out. This is
not specific to us or to BF2: any game that makes such a request will die here.

The defect is in **v0.7.0**, that is in the last published build. In the
current branch it is already gone: a shared `com_query_interface` with a list
of accepted GUIDs appeared after the tag (git shows those lines being added).
So there is nothing to fix — a newer build is what is needed.

And there is nowhere to take one from yet: there are no releases after v0.7.0,
CI holds only conformance logs, and building it ourselves is blocked by one
thing — the PE part needs an **x86_64-Wine tree with development files**
(`i386-windows/libwinecrt0.a` with `unix_lib.o` and `winebuild`). The system
has only arm64-Wine in `/usr/local`, and CrossOver does not ship development
files. Everything else is already in place: rustup, the targets, the linker,
the Windows SDK in `reference/xwin`.

### The command-line flags — from a table inside BF2.exe itself

Forums and wikis are not needed here: the game carries its own list with
explanations.

| flag | what the game itself says |
|---|---|
| `+restart 1` | Used when restarting executable. — skips the intros |
| `+playerName` | Set the player name |
| `+playerPassword` | Set the player password |
| `+loadLevel` | Set the level to load |
| `+gameMode` | Sets the game mode. |
| `+maxPlayers` | Sets max players. |
| `+szx` / `+szy` | Set resolution width / height |
| `+wx` / `+wy` | Position game window on the screen |
| `+fullscreen` | Start game in full screen mode |
| `+noSound` | Start game without sound |
| `+multi` | Allow starting multiple BF2 instances |
| `+modPath` | Set the mod path (default mods/bf2) |
| `+joinServer` / `+port` / `+password` | connecting to a server |
| `+mapList` / `+config` / `+rsconfig` | paths to MapList / ServerSettings / ReservedSlots |
| `+dedicated` | Start in dedicated server mode |
| `+demo` | Sets the con-file with demo options |
| `+skipDXCheck` | Skips DirectX version check. |
| `+lowPriority` | Run the game with slightly lower priority |
| `+hostServer` | use playnow functionality |
| `+dropDynamicSpawns` | Don't re-add dynamic spawn groups as round (re)starts. |
| `+ranked` | Allows gamespy snapshot sending |
| `+help` | Displays this help |

Beside them in the binary sit the internal switches worth remembering:
`GSDumpAllConFiles`, `GSCustomConFile`, `GSFileChangeMonitor`,
`GSDisableShaderCache`, `GSDebugGhostManager`, `GSDebugNetwork`,
`hack-ignore-asserts`, `swiffDebug`, `disable-swiff`. And the game writes a
command history into `Logs/BfCommandHistory.con`.
