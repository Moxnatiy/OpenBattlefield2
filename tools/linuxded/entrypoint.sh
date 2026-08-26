#!/bin/sh
# Запуск оригінального сервера. Теку з грою монтуємо в /server.
set -e

BINARY_DIR=/server/bin/amd-64
if [ ! -x "$BINARY_DIR/bf2" ]; then
    echo "Немає $BINARY_DIR/bf2 — змонтуйте розпакований linuxded у /server" >&2
    exit 1
fi

# PunkBuster не вмикаємо: у налаштуваннях сервера `sv.punkBuster 0`, і
# теку pb ми навмисно не підкладаємо.
export LD_LIBRARY_PATH="$BINARY_DIR"

# +dedicated 1 — без вікна, +ignoreAsserts 1 — не падати на дрібницях.
exec "$BINARY_DIR/bf2" \
    +modPath mods/bf2 \
    +dedicated 1 \
    +ignoreAsserts 1 \
    "$@"
