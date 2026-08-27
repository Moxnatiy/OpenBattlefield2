#!/bin/sh
# Піднімає оригінальний BF2.exe — для динамічного аналізу.
#
#   tools/bf2_run.sh           через athei/wine + x87sidecar (швидко)
#   BF2_PLAIN=1 tools/bf2_run.sh   через Wine із CrossOver, без sidecar
#
# Чому саме так:
#
# * ігри тих років рахують у x87, а Rosetta 2 транслює його дуже повільно.
#   `x87sidecar` замінює цей шматок Rosetta власним JIT-ом, що працює
#   окремим arm64-процесом. Йому потрібен Wine з рукостисканням —
#   збірка athei/wine-build (та сама CrossOver 26.3, лише з патчем);
# * графіка — mtld3d (D3D9 прямо в Metal), зібраний із гілки:
#   у релізі v0.7.0 текстура не віддає жодного інтерфейсу
#   (tools/d3d9_qi_test.c), і гра падає. Збірка — tools/mtld3d_build.sh;
# * стіл 800x600 заданий у реєстрі пляшки, а не через `explorer /desktop=`:
#   той ковтає stderr дитини, і журнал драйвера зникає. Без столу гра
#   бачить масштабовані режими Mac (960x600, 1024x640) і не знаходить
#   свого вбудованого 800x600;
# * робоча тека — тека гри: BF2 шукає mods/bf2/... відносно неї.
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

cd "$GAME_UNIX" || exit 1

if [ -n "$BF2_PLAIN" ] || [ ! -x "$WINE" ] || [ ! -x "$SIDECAR" ]; then
    CX="$HOME/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
    "$CX/bin/wine" --bottle "$BOTTLE" "$GAME\\BF2.exe" +menu 1 +fullscreen 0 >"$LOG" 2>&1 &
    echo "запущено (CrossOver, без sidecar); журнал: $LOG"
else
    WINEPREFIX="$B" WINEDLLOVERRIDES="d3d9=n" \
      "$SIDECAR" --cooperative "$WINE" "$GAME\\BF2.exe" +menu 1 +fullscreen 0 >"$LOG" 2>&1 &
    echo "запущено (x87sidecar); журнал: $LOG"
fi
