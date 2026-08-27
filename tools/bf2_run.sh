#!/bin/sh
# Піднімає оригінальний BF2.exe під CrossOver — для динамічного аналізу.
#
#   tools/bf2_run.sh            меню, вікно 800x600
#   tools/bf2_run.sh --log      те саме, показати журнал наприкінці
#
# Навіщо: статичний розбір HUD упирається в те, чого в даних не видно.
# Живу гру можна спитати напряму.
#
# Що і чому:
#
# * графіка — mtld3d (D3D9 -> Metal). DXVK 3 тут не йде: він хоче Vulkan
#   1.3, а MoltenVK на M1 дає 1.2. d9vk (DXVK 1.10) заводиться, але
#   mtld3d рідніший — і швидший;
# * гра лежить у пляшці за шляхом C:\Program Files (x86)\EA GAMES\...,
#   але це тека **посилань** на оригінал: жоден файл гри не копіюється;
# * єдиний справжній виняток — RendDX9.dll. У ньому пропатчено одинадцять
#   байтів: `int3` і навмисний запис у нуль, якими гра зупиняється на
#   кожному Assert. Це рівно те саме, що тиснути «Continue» у діалозі
#   «BF2 Error», лише без діалогу. Оригінал не змінений — латана копія
#   лежить у пляшці.
set -e
BOTTLE=${BF2_BOTTLE:-bf2bottle}
CX="$HOME/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
GAME='C:\Program Files (x86)\EA GAMES\Battlefield 2'
LOG=${BF2_LOG:-/tmp/bf2run.log}

[ -x "$CX/bin/cxstart" ] || { echo "немає CrossOver: $CX"; exit 1; }

pkill -f "BF2.exe" 2>/dev/null || true
sleep 1
"$CX/bin/cxstart" --bottle "$BOTTLE" --desktop bf2,800x600 \
    --workdir "$GAME" -- "$GAME\\BF2.exe" +menu 1 +fullscreen 0 >"$LOG" 2>&1 &

echo "запущено; журнал: $LOG"
[ "$1" = "--log" ] || exit 0
sleep 60
grep -viE "fixme|^[[:space:]]*$" "$LOG" | tail -20
