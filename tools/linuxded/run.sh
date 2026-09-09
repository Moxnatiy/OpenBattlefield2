#!/bin/sh
# Brings up an original BF2 dedicated server in a container.
#
#   tools/linuxded/run.sh <the unpacked linuxded directory>
#
# The directory comes from EA's installer (`bf2-linuxded-*.sh --noexec --target ...`).
# The settings are mounted from the repository so the game itself is left alone.
set -e

SERVER_DIR=${1:-"$PWD/Game Files/OtherFiles/linuxded-full"}
HERE=$(cd "$(dirname "$0")" && pwd)

if [ ! -x "$SERVER_DIR/bin/amd-64/bf2" ]; then
    echo "Did not find a server in $SERVER_DIR" >&2
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

echo "The server is up: 16567/udp. The log: docker logs -f bf2ded"
