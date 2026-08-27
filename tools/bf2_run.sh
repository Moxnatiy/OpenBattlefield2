#!/bin/sh
# Піднімає оригінальний BF2.exe — для динамічного аналізу.
#
#   tools/bf2_run.sh                       меню
#   BF2_LEVEL=dalian_plant tools/bf2_run.sh   одразу рівень
#   BF2_RES=1024x768 tools/bf2_run.sh         інша роздільність
#   BF2_PLAIN=1 tools/bf2_run.sh              через CrossOver, без sidecar
#
# Прапорці взяті не з форумів, а з таблиці в самому BF2.exe — вона там
# лежить разом із поясненнями:
#
#   restart      Used when restarting executable.   (пропускає заставки)
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
# Чому саме так:
#
# * ігри тих років рахують у x87, а Rosetta 2 транслює його дуже повільно.
#   `x87sidecar` замінює цей шматок Rosetta власним JIT-ом, що працює
#   окремим arm64-процесом. Йому потрібен Wine з рукостисканням —
#   збірка athei/wine-build (та сама CrossOver 26.3, лише з патчем);
# * графіка — mtld3d (D3D9 прямо в Metal), зібраний із гілки:
#   у релізі v0.7.0 текстура не віддає жодного інтерфейсу
#   (tools/d3d9_qi_test.c), і гра падає. Збірка — tools/mtld3d_build.sh;
# * стіл має бути **більшим за вікно гри**. Рівно такий, як вікно, не
#   годиться: заголовок з'їдає 22 пікселі, і гра дістає 800x578 замість
#   800x600 (або 1024x746 замість 1024x768). HUD від цього пливе — він
#   розкладений під повну висоту, а малюється в обрізану. Тому скрипт
#   ставить стіл сам, із запасом: BF2_RES=1024x768 -> стіл 1280x960;
# * стіл заданий у реєстрі пляшки, а не через `explorer /desktop=`:
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

# Роздільність гра бере зі свого профілю, тож звідти її й читаємо, а не
# вгадуємо. Стіл ставимо на крок більший — інакше заголовок вікна з'їдає
# 22 пікселі висоти й HUD пливе.
# Профіль гра при старті не застосовує — роздільність береться з
# командного рядка (+szx/+szy). Стіл рахуємо від неї.
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

# +restart 1 пропускає заставки; ім'я гравця задаємо самі, щоб гра не
# питала профіль. Рівень — коли попросили.
# `+menu` серед прапорців гри немає — у таблиці BF2.exe такого імені
# нема взагалі, тож ми передавали сміття. Живий набір:
ARGS="+fullscreen 0 +restart 1 +szx $SZX +szy $SZY"
ARGS="$ARGS +playerName ${BF2_NAME:-defaultPlayer}"
# Меню гра пропускає, коли непорожній GSLoadLevel (те, що кладе
# +loadLevel), GSJoinAddress, playNow 1 або GSDedicated — перевірка
# стоїть одним `if` перед запуском Flash-меню. Режим і кількість місць
# рівню потрібні: без них раунд не за чим будувати.
[ -n "$BF2_LEVEL" ] && ARGS="$ARGS +loadLevel $BF2_LEVEL \
    +gameMode ${BF2_MODE:-gpm_cq} +maxPlayers ${BF2_PLAYERS:-16}"

if [ -n "$BF2_PLAIN" ] || [ ! -x "$WINE" ] || [ ! -x "$SIDECAR" ]; then
    CX="$HOME/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
    "$CX/bin/wine" --bottle "$BOTTLE" "$GAME\\BF2.exe" $ARGS >"$LOG" 2>&1 &
    echo "запущено (CrossOver, без sidecar); журнал: $LOG"
else
    # Sidecar не обгортає Wine — навпаки: Wine сам його запускає, коли
    # бачить ROSETTA_X87_PATH. Це видно в його ntdll.so:
    # «ROSETTA_X87_PATH: attaching rosettax87 --cooperative». Обгорнутий
    # вручну sidecar просто стоїть на нулі відсотків і нічого не робить.
    WINEPREFIX="$B" WINEDLLOVERRIDES="d3d9=n" ROSETTA_X87_PATH="$SIDECAR" \
      "$WINE" "$GAME\\BF2.exe" $ARGS >"$LOG" 2>&1 &
    echo "запущено (x87sidecar) $RES у столі $DESKTOP; журнал: $LOG"
fi
