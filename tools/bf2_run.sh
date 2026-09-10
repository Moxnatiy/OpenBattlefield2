#!/bin/sh
# Brings the original BF2.exe up — for dynamic analysis.
#
#   tools/bf2_run.sh                       the menu
#   BF2_LEVEL=dalian_plant tools/bf2_run.sh   straight into a level (own round)
#   BF2_SERVER=192.168.100.100 tools/bf2_run.sh   to our server
#   BF2_RES=1024x768 tools/bf2_run.sh         another resolution
#   BF2_PLAIN=1 tools/bf2_run.sh              through CrossOver, no sidecar
#   BF2_CAPTURE=1 tools/bf2_run.sh            allow a Metal GPU trace (F12)
#
# The flags are taken not from forums but from a table in BF2.exe itself — it
# lies there together with the explanations:
#
#   restart      Used when restarting executable.   (skips the intro movies)
#   playerName   Set the player name
#   loadLevel    Set the level to load
#   gameMode     Sets the game mode.
#   szx / szy    Set resolution width / height
#   fullscreen   Start game in full screen mode
#   noSound      Start game without sound
#   multi        Allow starting multiple BF2 instances
#   modPath      Set the mod path (default mods/bf2)
#   joinServer   Join a server by ip address or hostname
#   help         Displays this help
#
# Why exactly this way:
#
# * games of those years count in x87, and Rosetta 2 translates it very slowly.
#   `x87sidecar` replaces that piece of Rosetta with a JIT of its own, running
#   as a separate arm64 process. It needs a Wine that shakes hands with it —
#   the athei/wine-build build (the same CrossOver 26.3, only patched).
#
#   It comes from https://github.com/athei/x87sidecar and is built from
#   source into `reference/x87sidecar-git`:
#
#       cmake -B build && cmake --build build
#       cp build/bin/x87sidecar ../x87sidecar/x87sidecar
#
#   The **flat** binary, not `x87sidecar_entitled`: the two differ only in
#   the signature, and the flat one is what `ROSETTA_X87_PATH` wants — Wine's
#   loader re-execs each 32-bit process through `x87sidecar --cooperative`,
#   which needs no entitlements and no password (its README, "Building").
#   `x87sidecar --probe` says whether the installed Rosetta is supported;
# * graphics is mtld3d (D3D9 straight into Metal), built from the branch:
#   in release v0.7.0 a texture returns no interface at all
#   (tools/d3d9_qi_test.c) and the game crashes. Building: tools/mtld3d_build.sh;
# * the desktop must be **larger than the game's window**. Exactly the size
#   of the window will not do: the title bar eats 22 pixels and the game gets
#   800x578 instead of 800x600 (or 1024x746 instead of 1024x768). The HUD
#   swims — it is laid out for the full height and drawn into a cropped one.
#   So the script sets the desktop itself, with room: 1024x768 -> 1280x960;
# * the desktop is set in the bottle's registry, not through `explorer
#   /desktop=`: that one swallows the child's stderr and the driver's log
#   disappears. With no desktop the game sees the scaled Mac modes (960x600,
#   1024x640) and does not find its own built-in 800x600;
# * the working directory is the game's: BF2 looks for mods/bf2/... under it.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
BOTTLE=${BF2_BOTTLE:-bf2bottle}
B="$HOME/Library/Application Support/CrossOver/Bottles/$BOTTLE"
GAME='C:\Program Files (x86)\EA GAMES\Battlefield 2'
GAME_UNIX="$B/drive_c/Program Files (x86)/EA GAMES/Battlefield 2"
LOG=${BF2_LOG:-/tmp/bf2run.log}

WINE="$HERE/reference/wine-cx/bin/wine"
SIDECAR="$HERE/reference/x87sidecar/x87sidecar"

pkill -f "BF2.exe" 2>/dev/null || true
pkill -f x87sidecar 2>/dev/null || true

# The game takes the resolution from its own profile, so that is where we read
# it from rather than guessing. The desktop is set one step larger — otherwise
# the window title eats 22 pixels of height and the HUD swims.
# The game does not apply the profile at start-up — the resolution is taken
# from the command line (+szx/+szy). The desktop is computed from it.
RES=${BF2_RES:-800x600}
SZX=${RES%x*}
SZY=${RES#*x}
case $RES in
    640x480)   DESKTOP=800x600   ;;
    800x600)   DESKTOP=1024x768  ;;
    1024x768)  DESKTOP=1280x960  ;;
    1152x864)  DESKTOP=1280x1024 ;;
    1280x960)  DESKTOP=1400x1050 ;;
    *)         DESKTOP=1400x1050 ;;
esac
WINEPREFIX="$B" "$WINE" reg add 'HKCU\Software\Wine\Explorer\Desktops' \
    /v Default /d "$DESKTOP" /f >/dev/null 2>&1 || true

cd "$GAME_UNIX" || exit 1

# +restart 1 skips the intro movies; we set the player name ourselves so the
# game does not ask for a profile. A level — when one was asked for.
# `+menu` is not among the game's flags — that name is not in the BF2.exe
# table at all, so we were passing rubbish. The live set:
ARGS="+fullscreen 0 +restart 1 +szx $SZX +szy $SZY"
ARGS="$ARGS +playerName ${BF2_NAME:-defaultPlayer}"
# The game skips the menu when GSLoadLevel is non-empty (which is what
# +loadLevel sets), or GSJoinAddress, or playNow 1, or GSDedicated — the
# check is a single `if` before the Flash menu starts. The level needs a mode
# and a player count: without them there is nothing to build a round on.
# Joining a real server takes precedence over loading a level: with a
# non-empty GSJoinAddress the game skips the menu just the same, but instead
# of its own round it goes to someone else's. That is what we want — both
# clients, the original and ours, on the same server.
if [ -n "$BF2_SERVER" ]; then
    ARGS="$ARGS +joinServer $BF2_SERVER +port ${BF2_PORT:-16567}"
    [ -n "$BF2_PASSWORD" ] && ARGS="$ARGS +password $BF2_PASSWORD"
elif [ -n "$BF2_LEVEL" ]; then
    ARGS="$ARGS +loadLevel $BF2_LEVEL \
        +gameMode ${BF2_MODE:-gpm_cq} +maxPlayers ${BF2_PLAYERS:-16}"
fi

# A Metal GPU trace can only be armed at start-up: the capture layer inserts
# itself when the process begins, and `MTL_CAPTURE_ENABLED=1` is what asks for
# it. Without the variable F12 still writes the `[dump]` half and the driver
# says "Capture layer is not inserted" about the other. It is off by default —
# the layer costs performance in every frame, not only the captured ones.
if [ -n "$BF2_CAPTURE" ]; then
    MTL_CAPTURE_ENABLED=1
    export MTL_CAPTURE_ENABLED
fi

# The frame dump does not come out here. Since v0.8.0 mtld3d's D3D9 side writes
# to its own file — `mtld3d-logs/<exe stem>-<pid>.log` beside the game's .exe,
# or wherever the `log.dir` setting points — and this log holds only Wine's own
# output plus the shim's one startup line. Looking for `[dump]` in here and
# finding nothing is not the driver ignoring Ctrl+Shift+D.
# RUST_LOG is not needed: mtld3d's default filter is already `info`, and the
# variable only adds to it.

if [ -n "$BF2_PLAIN" ] || [ ! -x "$WINE" ] || [ ! -x "$SIDECAR" ]; then
    CX="$HOME/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
    "$CX/bin/wine" --bottle "$BOTTLE" "$GAME\\BF2.exe" $ARGS >"$LOG" 2>&1 &
    echo "started (CrossOver, no sidecar); log: $LOG"
else
    # The sidecar does not wrap Wine — the other way round: Wine starts it
    # itself when it sees ROSETTA_X87_PATH. It is visible in its ntdll.so:
    # "ROSETTA_X87_PATH: attaching rosettax87 --cooperative". A sidecar
    # wrapped by hand just sits at zero per cent and does nothing.
    WINEPREFIX="$B" WINEDLLOVERRIDES="d3d9=n" ROSETTA_X87_PATH="$SIDECAR" \
      "$WINE" "$GAME\\BF2.exe" $ARGS >"$LOG" 2>&1 &
    echo "started (x87sidecar) $RES on a $DESKTOP desktop; log: $LOG"
fi
