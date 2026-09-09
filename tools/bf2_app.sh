#!/bin/sh
# Builds a macOS wrapper around the original BF2 — so that the game under Wine
# is a **native application** with a bundle id of its own.
#
#   tools/bf2_app.sh            build the wrapper
#   open -a "$HOME/Applications/OpenBF2 Original.app"   run it
#
# What this is for at all.
#
# Wine started from a shell is, to macOS, a process with no bundle id
# (`lsappinfo` shows `bundleID=[ NULL ]`). Because of that nothing working
# through LaunchServices can see it: neither application control, nor
# single-application screen recording, nor Xcode's Metal capture, which also
# asks which application to trace. The wrappers CrossOver makes itself will
# not do here: they start a shortcut from the Start menu, and we need flags of
# our own (`+loadLevel`, `+szx`, `+joinServer`).
#
# The wrapper is the smallest possible `.app`: an `Info.plist` with a bundle id
# of its own and a script that **replaces itself** (`exec`) with the Wine
# process. Thanks to `exec` the pid stays the same, LaunchServices still takes
# it for that application, and the game window gets its name and icon.
#
# The game's flags come from the same variables as `tools/bf2_run.sh`:
# BF2_LEVEL, BF2_SERVER, BF2_RES, BF2_NAME. They have to be set **before
# building** — they are baked into the launch script.
#
# `BF2_METAL_CAPTURE=1` turns Apple's Metal capture on: without
# `MTL_CAPTURE_ENABLED` set **at process start**, `MTLCaptureManager` refuses
# silently. Off by default: in that mode Metal is noticeably slower, and a
# capture is taken rarely.
#
# **A known limitation.** With `+joinServer` to a real server, the game
# started by this wrapper says "You have failed to connect" while still in the
# menu. The cause is not established: the local network permission has nothing
# to do with it (a ping from the application itself goes through), nor does
# `MTL_CAPTURE_ENABLED` (the same without it), and our own client connects to
# the same server at the same moment. For playing on a server, use bf2_run.sh.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
NAME=${BF2_APP_NAME:-OpenBF2 Original}
APP="$HOME/Applications/$NAME.app"
BOTTLE=${BF2_BOTTLE:-bf2bottle}
B="$HOME/Library/Application Support/CrossOver/Bottles/$BOTTLE"
GAME='C:\Program Files (x86)\EA GAMES\Battlefield 2'
LOG=${BF2_LOG:-/tmp/bf2run.log}

WINE="$HERE/reference/wine-cx/bin/wine"
SIDECAR="$HERE/reference/x87sidecar/x87sidecar"

RES=${BF2_RES:-800x600}
SZX=${RES%x*}
SZY=${RES#*x}
case $RES in
    640x480)   DESKTOP=800x600   ;;
    800x600)   DESKTOP=1024x768  ;;
    1024x768)  DESKTOP=1280x960  ;;
    *)         DESKTOP=1400x1050 ;;
esac

ARGS="+fullscreen 0 +restart 1 +szx $SZX +szy $SZY +playerName ${BF2_NAME:-defaultPlayer}"
if [ -n "$BF2_SERVER" ]; then
    ARGS="$ARGS +joinServer $BF2_SERVER +port ${BF2_PORT:-16567}"
elif [ -n "$BF2_LEVEL" ]; then
    ARGS="$ARGS +loadLevel $BF2_LEVEL +gameMode ${BF2_MODE:-gpm_cq} +maxPlayers ${BF2_PLAYERS:-16}"
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>org.openbf2.original</string>
  <key>CFBundleExecutable</key><string>run</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <!-- The server is usually on the local network (192.168.x.x), and macOS
       since Sonoma asks a separate permission for that — and asks it **per
       application**. A start from a shell inherits the terminal's permission,
       a fresh bundle does not: the game then says "You have failed to
       connect" while still in the menu. Without this key nothing is asked. -->
  <key>NSLocalNetworkUsageDescription</key>
  <string>The game connects to a Battlefield 2 server on the local network.</string>
</dict>
</plist>
PLIST

METAL=${BF2_METAL_CAPTURE:-}
cat > "$APP/Contents/MacOS/run" <<RUN
#!/bin/sh
# Created by tools/bf2_app.sh — do not edit by hand.
export WINEPREFIX="$B"
export WINEDLLOVERRIDES="d3d9=n"
${METAL:+export MTL_CAPTURE_ENABLED=1}
export ROSETTA_X87_PATH="$SIDECAR"
"$WINE" reg add 'HKCU\\Software\\Wine\\Explorer\\Desktops' \\
    /v Default /d "$DESKTOP" /f >/dev/null 2>&1 || true
cd "$B/drive_c/Program Files (x86)/EA GAMES/Battlefield 2" || exit 1
# exec, not starting a child: the pid has to stay the same, otherwise
# LaunchServices loses the link with the application.
exec "$WINE" "$GAME\\\\BF2.exe" $ARGS > "$LOG" 2>&1
RUN
chmod +x "$APP/Contents/MacOS/run"

# The signature is our own, but it is needed: without it macOS does not keep
# permissions (local network, control) attached to the application — they are
# tied to the signature, not to the path.
codesign --force --sign - "$APP" >/dev/null 2>&1 || true

# Without this LaunchServices may not notice the application just created.
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f "$APP" 2>/dev/null || true

echo "built: $APP"
echo "  flags: $ARGS"
echo "  run:   open -a \"$APP\""
