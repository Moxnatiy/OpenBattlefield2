# The original BF2 server in a container

Why: to have a **reference** at hand. Our server and client can be checked
against how the engine itself behaves rather than against guesses.

## What is needed

EA's official dedicated server installer — it lies in the user's game
(`bf2-linuxded-1.5.3153.0-installer.sh`). Unpack it without running it:

```bash
cd "Game Files/OtherFiles"
env -u DISPLAY sh ./bf2-linuxded-1.5.3153.0-installer.sh --noexec --target linuxded-full
```

`env -u DISPLAY` is not incidental here: the installer sees X11 and tries to
restart itself in an `xterm`, after which nothing happens in the terminal.

## Running it

```bash
tools/linuxded/run.sh
```

The script builds the image and brings the container up. There are no game
files in the image — the server's directory is mounted from outside, and the
settings are supplied from the repository so the game itself is left alone:

| File | What is changed |
|---|---|
| `serversettings.con` | the server's name, 16 slots, a round starts with one player |
| `maplist.con` | only `dalian_plant gpm_cq 16` — what our client can handle |
| `admin-default.cfg` | the remote console's password |

**PunkBuster is not enabled.** The game's default settings have
`sv.punkBuster 0`, and we do not supply the `pb` directory to the container.

The 2009 binary is built for x86-64, so the image is `linux/amd64` too — on
Apple Silicon Docker runs it through emulation. It works: the server loads the
level and runs a round, taking about 165 MB and a few per cent of the CPU.

## The remote console

```bash
python tools/linuxded/rcon.py "sv.serverName" "sv.maxPlayers" "admin.currentLevel"
```

```
sv.serverName                  -> OpenBF2 reference
sv.maxPlayers                  -> 16
admin.currentLevel             -> 0
gameLogic.getTickets 1         -> Unknown object or method!
```

The last line is the engine itself saying there is no such command. We do
exactly the same when a command arrives with no handler for it.

The protocol details that had to be worked out:

* the greeting and the seed arrive as **two separate** messages;
* the hash is `md5(seed + password)` (visible in `admin/default.py`, which lies
  in the server in plain text);
* the password comes **not** from `ServerSettings.con` but from
  `admin/default.cfg`; without that file the password is empty and logging in
  never succeeds.

## What this rig is for now

`openbf2 --connect <host>` joins this server and holds the connection: the
handshake, `ClientInfo`, the content check and the spawn chain all go through
(docs/research/09-network-protocol.md, docs/formats/network-protocol.md). What
is left is the contents of the data packets — byte-for-byte compatibility is
not promised (see `docs/TODO.md`).

Experiments are run through `capture.py`, not through one-off scripts: it
drives the connection to the required stage and records everything that
arrived, so a capture can be replayed later without the rig.
