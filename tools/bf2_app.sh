#!/bin/sh
# Збирає macOS-обгортку навколо оригінального BF2 — щоб гра під Wine була
# **рідним застосунком** із власним bundle id.
#
#   tools/bf2_app.sh            зібрати обгортку
#   open -a "$HOME/Applications/OpenBF2 Original.app"   запустити
#
# Навіщо це взагалі.
#
# Wine, запущений із оболонки, для macOS — процес без bundle id
# (`lsappinfo` показує `bundleID=[ NULL ]`). Через це його не бачить
# нічого, що працює через LaunchServices: ані керування застосунком, ані
# запис екрана «однієї програми», ані Metal-трасування Xcode, яке теж
# питає, який саме застосунок трасувати. Ті обгортки, що робить сам
# CrossOver, тут не годяться: вони запускають ярлик із меню «Пуск», а нам
# потрібні свої прапорці (`+loadLevel`, `+szx`, `+joinServer`).
#
# Обгортка — це найменший можливий `.app`: `Info.plist` із власним
# bundle id і скрипт, який **заміщає себе** (`exec`) процесом Wine. Через
# `exec` pid лишається тим самим, LaunchServices далі вважає його тим
# застосунком, і вікно гри дістає його ім'я та значок.
#
# Прапорці гри беруться з тих самих змінних, що й `tools/bf2_run.sh`:
# BF2_LEVEL, BF2_SERVER, BF2_RES, BF2_NAME. Задавати їх треба **перед
# збиранням** — вони запікаються в скрипт запуску.
#
# `BF2_METAL_CAPTURE=1` вмикає Apple-івське трасування Metal: без
# `MTL_CAPTURE_ENABLED`, поставленої **при старті процесу**,
# `MTLCaptureManager` мовчки відмовляє. Типово вимкнено: у цьому режимі
# Metal помітно повільніший, а трасу знімають рідко.
#
# **Відоме обмеження.** Із `+joinServer` на справжній сервер гра,
# запущена цією обгорткою, каже «You have failed to connect» ще в меню.
# Причина не з'ясована: дозвіл на локальну мережу ні до чого (ping із
# самого застосунку проходить), `MTL_CAPTURE_ENABLED` теж (без нього те
# саме), а наш власний клієнт до того ж сервера в ту саму мить
# під'єднується. Для гри на сервері поки годиться `tools/bf2_run.sh`.
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
  <!-- Сервер зазвичай у локальній мережі (192.168.x.x), а macOS від
       Sonoma питає на це окремий дозвіл — і питає **на застосунок**.
       Запуск із оболонки успадковує дозвіл терміналу, а свіжий bundle
       його не має: гра тоді каже «You have failed to connect» ще в
       меню. Без цього рядка система навіть не показує запиту. -->
  <key>NSLocalNetworkUsageDescription</key>
  <string>Гра з'єднується з сервером Battlefield 2 у локальній мережі.</string>
</dict>
</plist>
PLIST

METAL=${BF2_METAL_CAPTURE:-}
cat > "$APP/Contents/MacOS/run" <<RUN
#!/bin/sh
# Створено tools/bf2_app.sh — руками не правити.
export WINEPREFIX="$B"
export WINEDLLOVERRIDES="d3d9=n"
${METAL:+export MTL_CAPTURE_ENABLED=1}
export ROSETTA_X87_PATH="$SIDECAR"
"$WINE" reg add 'HKCU\\Software\\Wine\\Explorer\\Desktops' \\
    /v Default /d "$DESKTOP" /f >/dev/null 2>&1 || true
cd "$B/drive_c/Program Files (x86)/EA GAMES/Battlefield 2" || exit 1
# exec, а не запуск дитини: pid має лишитися тим самим, інакше
# LaunchServices втратить зв'язок із застосунком.
exec "$WINE" "$GAME\\\\BF2.exe" $ARGS > "$LOG" 2>&1
RUN
chmod +x "$APP/Contents/MacOS/run"

# Підпис хоч і власний, але потрібен: без нього macOS не тримає за
# застосунком дозволів (локальна мережа, керування) — вони прив'язані до
# підпису, а не до шляху.
codesign --force --sign - "$APP" >/dev/null 2>&1 || true

# Без цього LaunchServices може не помітити щойно створений застосунок.
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -f "$APP" 2>/dev/null || true

echo "зібрано: $APP"
echo "  прапорці: $ARGS"
echo "  запуск:   open -a \"$APP\""
