#!/bin/sh
# Starting the original server. The directory with the game is mounted at /server.
set -e

BINARY_DIR=/server/bin/amd-64
if [ ! -x "$BINARY_DIR/bf2" ]; then
    echo "No $BINARY_DIR/bf2 — mount an unpacked linuxded at /server" >&2
    exit 1
fi

# We do not enable PunkBuster: the server's settings have `sv.punkBuster 0`, and
# we deliberately do not supply the pb directory.
export LD_LIBRARY_PATH="$BINARY_DIR"

# +dedicated 1 means no window, +ignoreAsserts 1 means do not die on trifles.
exec "$BINARY_DIR/bf2" \
    +modPath mods/bf2 \
    +dedicated 1 \
    +ignoreAsserts 1 \
    "$@"
