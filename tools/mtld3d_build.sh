#!/bin/sh
# Збирає mtld3d з джерел і ставить у пляшку — debug.
#
#   tools/mtld3d_build.sh            release — для гри
#   MTLD3D_DEBUG=1 tools/mtld3d_build.sh   debug — для розбору
#
# Профіль тут не дрібниця: у debug Rust драйвер помітно повільніший, і
# гра ледве тягне. Для вимірів швидкості потрібен release.
#
# Навіщо: у релізі v0.7.0 текстура не віддає жодного інтерфейсу —
# QueryInterface відмовляє навіть в IUnknown (див. tools/d3d9_qi_test.c).
# BF2 через це падає, щойно доходить до рендеру. У гілці це вже
# виправлено спільним `com_query_interface`, але релізу з ним ще немає.
#
# Хитрість, яка дає зібрати без дерева Wine: крейт `d3d9` **не залежить**
# від `shim`, тож `cargo build -p d3d9` обходить `shim/build.rs`, якому
# потрібен `libwinecrt0.a` з `unix_lib.o`. Unix-частина — звичайний
# dylib, теж без Wine. Міст `mtld3d.dll` лишається з релізу: він тільки
# передає виклики.
#
# Версії d3d9 і unix мають збігатися: між ними міняється домовленість
# про виклики, і на різних збірках гра падає в самому mtld3d.
set -e
if [ -n "$MTLD3D_DEBUG" ]; then PROFILE=dev; OUT=debug; else PROFILE=release; OUT=release; fi
SRC=${MTLD3D_SRC:-$(cd "$(dirname "$0")/.." && pwd)/reference/mtld3d}
BOTTLE=${BF2_BOTTLE:-bf2bottle}
B="$HOME/Library/Application Support/CrossOver/Bottles/$BOTTLE"
export PATH="$HOME/.cargo/bin:$PATH"

[ -d "$SRC" ] || { echo "немає джерел: $SRC"; exit 1; }

echo "==> d3d9.dll (i686, $OUT)"
( cd "$SRC/windows" && cargo build --profile "$PROFILE" -p d3d9 --target i686-pc-windows-msvc )

echo "==> mtld3d.so (x86_64, $OUT)"
( cd "$SRC/unix" && cargo build --profile "$PROFILE" --target x86_64-apple-darwin -p mtld3d-unix )
cp "$SRC/unix/target/x86_64-apple-darwin/$OUT/libmtld3d_unix.dylib" \
   "$SRC/unix/target/x86_64-apple-darwin/$OUT/mtld3d.so"

echo "==> у пляшку і в дерево Wine"
cp "$SRC/windows/target/i686-pc-windows-msvc/$OUT/d3d9.dll" \
   "$B/drive_c/windows/syswow64/d3d9.dll"
cp "$SRC/unix/target/x86_64-apple-darwin/$OUT/mtld3d.so" \
   "$B/mtld3d/x86_64-unix/mtld3d.so"
# Той Wine, під яким ходить sidecar, шукає unix-половину у своєму дереві.
WINE_TREE="$(cd "$(dirname "$0")/.." && pwd)/reference/wine-cx/lib/wine"
[ -d "$WINE_TREE" ] && cp "$SRC/unix/target/x86_64-apple-darwin/$OUT/mtld3d.so" \
   "$WINE_TREE/x86_64-unix/mtld3d.so"
echo "готово ($OUT)"
