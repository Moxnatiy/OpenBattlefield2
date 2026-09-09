#!/bin/sh
# Builds mtld3d from source and puts it into the bottle — debug.
#
#   tools/mtld3d_build.sh            release — for playing
#   MTLD3D_DEBUG=1 tools/mtld3d_build.sh   debug — for taking apart
#
# The profile is no small matter here: in a debug Rust build the driver is
# noticeably slower and the game barely runs. Speed measurements need release.
#
# What for: in release v0.7.0 a texture returns no interface at all —
# QueryInterface refuses even IUnknown (see tools/d3d9_qi_test.c). BF2 crashes
# because of that as soon as it reaches rendering. On the branch it is already
# fixed by a shared `com_query_interface`, but there is no release with it yet.
#
# The trick that lets it build without a Wine tree: the `d3d9` crate **does not
# depend** on `shim`, so `cargo build -p d3d9` goes around `shim/build.rs`,
# which needs `libwinecrt0.a` with `unix_lib.o`. The unix part is an ordinary
# dylib, also without Wine. The `mtld3d.dll` bridge stays from the release: it
# only passes the calls on.
#
# The d3d9 and unix versions have to match: the calling convention between them
# changes, and on mismatched builds the game crashes inside mtld3d itself.
set -e
if [ -n "$MTLD3D_DEBUG" ]; then PROFILE=dev; OUT=debug; else PROFILE=release; OUT=release; fi
SRC=${MTLD3D_SRC:-$(cd "$(dirname "$0")/.." && pwd)/reference/mtld3d}
BOTTLE=${BF2_BOTTLE:-bf2bottle}
B="$HOME/Library/Application Support/CrossOver/Bottles/$BOTTLE"
export PATH="$HOME/.cargo/bin:$PATH"

[ -d "$SRC" ] || { echo "no sources: $SRC"; exit 1; }

echo "==> d3d9.dll (i686, $OUT)"
( cd "$SRC/windows" && cargo build --profile "$PROFILE" -p d3d9 --target i686-pc-windows-msvc )

echo "==> mtld3d.so (x86_64, $OUT)"
( cd "$SRC/unix" && cargo build --profile "$PROFILE" --target x86_64-apple-darwin -p mtld3d-unix )
cp "$SRC/unix/target/x86_64-apple-darwin/$OUT/libmtld3d_unix.dylib" \
   "$SRC/unix/target/x86_64-apple-darwin/$OUT/mtld3d.so"

echo "==> into the bottle and into the Wine tree"
cp "$SRC/windows/target/i686-pc-windows-msvc/$OUT/d3d9.dll" \
   "$B/drive_c/windows/syswow64/d3d9.dll"
cp "$SRC/unix/target/x86_64-apple-darwin/$OUT/mtld3d.so" \
   "$B/mtld3d/x86_64-unix/mtld3d.so"
# The Wine the sidecar runs under looks for the unix half in its own tree.
WINE_TREE="$(cd "$(dirname "$0")/.." && pwd)/reference/wine-cx/lib/wine"
[ -d "$WINE_TREE" ] && cp "$SRC/unix/target/x86_64-apple-darwin/$OUT/mtld3d.so" \
   "$WINE_TREE/x86_64-unix/mtld3d.so"
echo "done ($OUT)"
