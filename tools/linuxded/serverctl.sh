#!/bin/sh
# Керує стендом: оригінальний сервер BF2 на x86-машині.
#
#   tools/linuxded/serverctl.sh up          підняти (сам перезапускається)
#
# Сервер тримаємо піднятим постійно. Перезапуск коштує кілька хвилин на
# завантаження рівня, і кожен такий цикл — це ще й змінений номер
# виклику та зайва нестабільність у дослідах. Піднімати заново треба
# лише коли він справді впав.
#   tools/linuxded/serverctl.sh up --gdb    те саме, але під gdb
#   tools/linuxded/serverctl.sh wait        дочекатися, поки рівень завантажиться
#   tools/linuxded/serverctl.sh log         показати журнал без ANSI-сміття
#   tools/linuxded/serverctl.sh down        зупинити
#
# Навіщо x86: під емуляцією на Apple Silicon не працює ptrace, а без
# нього немає ні gdb, ні точок зупину — лишається вгадувати.
#
# Точки зупину для режиму --gdb беруться з tools/linuxded/breakpoints.gdb.
set -e
HOST=${BF2_HOST:-homeserver}
HERE=$(cd "$(dirname "$0")" && pwd)
SSH="ssh -o ControlMaster=auto -o ControlPath=$HOME/.ssh/cm/%r@%h:%p -o ControlPersist=10m"

# Власні перемикачі сервера: він сам розповідає, що робить із мережею
# і привидами. Дешевше за точки зупину — і не гальмує його.
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
    # `--restart unless-stopped`: аварія чи кінець карти більше не з'їдають
    # хвилини на ручний підйом.
    $SSH "$HOST" "docker rm -f bf2 >/dev/null 2>&1 || true
        docker run -d --name bf2 --restart unless-stopped $CAPS \
          -e TERM=xterm -p 16567:16567/udp -p 4711:4711 \
          -v \$HOME/bf2ded:/server \
          -v \$HOME/bf2img/serversettings.con:/server/mods/bf2/settings/serversettings.con:ro \
          -v \$HOME/bf2img/maplist.con:/server/mods/bf2/settings/maplist.con:ro \
          -v \$HOME/bf2img/admin-default.cfg:/server/admin/default.cfg:ro \
          $MOUNT -w /server -e LD_LIBRARY_PATH=/server/bin/amd-64 \
          bf2dbg $CMD +modPath mods/bf2 +dedicated 1 +ignoreAsserts 1 $EXTRA >/dev/null"
    echo "стенд піднято на $HOST"
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
    echo "стенд зупинено"
    ;;
*)
    sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
