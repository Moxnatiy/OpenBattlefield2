#!/bin/sh
# Drives the rig: an original BF2 server on an x86 machine.
#
#   tools/linuxded/serverctl.sh up          bring it up (it restarts itself)
#
# We keep the server up all the time. A restart costs several minutes of level
# loading, and every such cycle also means a changed challenge number and extra
# instability in the experiments. It has to be brought up anew only when it really
# fell over.
#   tools/linuxded/serverctl.sh up --gdb    the same, but under gdb
#   tools/linuxded/serverctl.sh wait        wait until the level has loaded
#   tools/linuxded/serverctl.sh log         show the log without ANSI noise
#   tools/linuxded/serverctl.sh down        stop it
#
# Why x86: under emulation on Apple Silicon ptrace does not work, and without it
# there is neither gdb nor breakpoints — only guessing is left.
#
# The breakpoints for --gdb mode come from tools/linuxded/breakpoints.gdb.
set -e
HOST=${BF2_HOST:-homeserver}
HERE=$(cd "$(dirname "$0")" && pwd)
SSH="ssh -o ControlMaster=auto -o ControlPath=$HOME/.ssh/cm/%r@%h:%p -o ControlPersist=10m"

# The server's own switches: it tells us itself what it does with the network and
# the ghosts. Cheaper than breakpoints — and it does not slow it down.
EXTRA=${BF2_EXTRA:-}

case "$1" in
up)
    mkdir -p "$HOME/.ssh/cm"
    $SSH "$HOST" 'mkdir -p ~/bf2img'
    tar cf - -C "$HERE" serversettings.con maplist.con admin-default.cfg \
        | $SSH "$HOST" 'tar xf - -C ~/bf2img'
    if [ "$2" = "--gdb" ]; then
        scp -q -o ControlPath="$HOME/.ssh/cm/%r@%h:%p" "$HERE/breakpoints.gdb" "$HOST:~/bf2img/cmds.gdb"
        CMD='gdb -batch -x /cmds.gdb --args /server/bin/amd-64/bf2'
        MOUNT='-v $HOME/bf2img/cmds.gdb:/cmds.gdb:ro'
        CAPS='--cap-add=SYS_PTRACE --security-opt seccomp=unconfined'
    else
        CMD='/server/bin/amd-64/bf2'
        MOUNT=''
        CAPS=''
    fi
    # `--restart unless-stopped`: a crash or the end of a map no longer eats
    # minutes on bringing it up by hand.
    $SSH "$HOST" "docker rm -f bf2 >/dev/null 2>&1 || true
        docker run -d --name bf2 --restart unless-stopped $CAPS \
          -e TERM=xterm -p 16567:16567/udp -p 4711:4711 \
          -v \$HOME/bf2ded:/server \
          -v \$HOME/bf2img/serversettings.con:/server/mods/bf2/settings/serversettings.con:ro \
          -v \$HOME/bf2img/maplist.con:/server/mods/bf2/settings/maplist.con:ro \
          -v \$HOME/bf2img/admin-default.cfg:/server/admin/default.cfg:ro \
          $MOUNT -w /server -e LD_LIBRARY_PATH=/server/bin/amd-64 \
          bf2dbg $CMD +modPath mods/bf2 +dedicated 1 +ignoreAsserts 1 $EXTRA >/dev/null"
    echo "the rig is up on $HOST"
    ;;
wait)
    python3 "$HERE/capture.py" --wait
    ;;
log)
    $SSH "$HOST" 'docker logs bf2 2>&1' | tr -d '\r' | tr '\033' '\n' \
        | grep -av '^\[' | grep -av '^\s*$'
    ;;
down)
    $SSH "$HOST" 'docker rm -f bf2 >/dev/null 2>&1 || true'
    echo "the rig is stopped"
    ;;
*)
    sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
