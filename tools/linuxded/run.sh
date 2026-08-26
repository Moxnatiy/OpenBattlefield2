#!/bin/sh
# Піднімає оригінальний виділений сервер BF2 у контейнері.
#
#   tools/linuxded/run.sh <тека розпакованого linuxded>
#
# Теку беремо з інсталятора EA (`bf2-linuxded-*.sh --noexec --target ...`).
# Налаштування підмонтовуються з репозиторію, щоб не чіпати саму гру.
set -e

SERVER_DIR=${1:-"$PWD/Game Files/OtherFiles/linuxded-full"}
HERE=$(cd "$(dirname "$0")" && pwd)

if [ ! -x "$SERVER_DIR/bin/amd-64/bf2" ]; then
    echo "Не знайшов сервер у $SERVER_DIR" >&2
    exit 1
fi

docker build --platform linux/amd64 -t openbf2/linuxded "$HERE"

docker rm -f bf2ded 2>/dev/null || true
docker run -d --name bf2ded --platform linux/amd64 \
    -e TERM=xterm \
    -p 16567:16567/udp -p 29900:29900/udp -p 4711:4711 \
    -v "$SERVER_DIR:/server" \
    -v "$HERE/serversettings.con:/server/mods/bf2/settings/serversettings.con:ro" \
    -v "$HERE/maplist.con:/server/mods/bf2/settings/maplist.con:ro" \
    -v "$HERE/admin-default.cfg:/server/admin/default.cfg:ro" \
    openbf2/linuxded

echo "Сервер піднято: 16567/udp. Журнал: docker logs -f bf2ded"
