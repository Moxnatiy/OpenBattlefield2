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
# There are three halves, not two, and they have to match. `d3d9.dll` is what
# the game loads, `mtld3d.so` is the unix side, and `mtld3d.dll` is the shim
# between them. The shim used to be left at whatever the release shipped,
# because `shim/build.rs` wants `libwinecrt0.a` with `unix_lib.o` out of a Wine
# tree while `d3d9` does not depend on `shim` at all and so builds without one.
#
# Leaving it behind is what a mismatch looks like: with the shim at v0.7.0 and
# the other two at v0.8.0 the game rendered, but `mtld3d::d3d9` logged nothing
# at all — so Ctrl+Shift+D did its work and printed not one line, which reads
# exactly like a driver ignoring the key. The Wine tree we already keep has the
# archive, so the shim is built here too.
#
# The d3d9 and unix versions have to match: the calling convention between them
# changes, and on mismatched builds the game crashes inside mtld3d itself.
set -e
if [ -n "$MTLD3D_DEBUG" ]; then PROFILE=dev; OUT=debug; else PROFILE=release; OUT=release; fi
SRC=${MTLD3D_SRC:-$(cd "$(dirname "$0")/.." && pwd)/reference/mtld3d}
BOTTLE=${BF2_BOTTLE:-bf2bottle}
B="$HOME/Library/Application Support/CrossOver/Bottles/$BOTTLE"
WINE_TREE=${WINE_SDK:-$(cd "$(dirname "$0")/.." && pwd)/reference/wine-cx}
export PATH="$HOME/.cargo/bin:$PATH"
export WINE_SDK="$WINE_TREE"

[ -d "$SRC" ] || { echo "no sources: $SRC"; exit 1; }

# `set -e` does not reach inside a pipeline, and this script used to be run
# through `| tail`, which swallowed a compile failure and left the old DLL in
# the bottle looking freshly built. Each step says so itself now.
die() { echo "$1" >&2; exit 1; }

echo "==> d3d9.dll (i686, $OUT)"
( cd "$SRC/windows" && cargo build --profile "$PROFILE" -p d3d9 --target i686-pc-windows-msvc ) \
    || die "d3d9 did not build"

# The shim is a Wine **builtin**, and that is not a figure of speech: its PE
# half can only reach its unix half when the loader takes it as one, and the
# loader decides by a signature `winebuild --builtin` stamps into the file
# (mtld3d's own Makefile does the same after building it). Install one without
# the stamp and Wine refuses to load it, the shim never answers, and the game
# dies dereferencing null the moment it asks for Direct3D. Both arches: the
# 32-bit game loads the 32-bit shim, and its unix call goes through the 64-bit
# one.
WINEBUILD="$WINE_TREE/bin/winebuild"
[ -x "$WINEBUILD" ] || die "no winebuild in $WINE_TREE/bin"

for arch in i686 x86_64; do
    echo "==> mtld3d.dll shim ($arch, $OUT)"
    ( cd "$SRC/windows" && cargo build --profile "$PROFILE" -p mtld3d \
        --target "$arch-pc-windows-msvc" ) \
        || die "the mtld3d shim did not build for $arch (WINE_SDK=$WINE_SDK)"
    "$WINEBUILD" --builtin "$SRC/windows/target/$arch-pc-windows-msvc/$OUT/mtld3d.dll" \
        || die "winebuild --builtin failed for $arch"
done

echo "==> mtld3d.so (x86_64, $OUT)"
( cd "$SRC/unix" && cargo build --profile "$PROFILE" --target x86_64-apple-darwin -p mtld3d-unix ) \
    || die "mtld3d-unix did not build"
cp "$SRC/unix/target/x86_64-apple-darwin/$OUT/libmtld3d_unix.dylib" \
   "$SRC/unix/target/x86_64-apple-darwin/$OUT/mtld3d.so"

echo "==> into the bottle and into the Wine tree"
cp "$SRC/windows/target/i686-pc-windows-msvc/$OUT/d3d9.dll" \
   "$B/drive_c/windows/syswow64/d3d9.dll"
SHIM32="$SRC/windows/target/i686-pc-windows-msvc/$OUT/mtld3d.dll"
SHIM64="$SRC/windows/target/x86_64-pc-windows-msvc/$OUT/mtld3d.dll"
for shim in "$B/drive_c/windows/syswow64/mtld3d.dll" "$B/mtld3d/i386-windows/mtld3d.dll" \
            "$WINE_TREE/lib/wine/i386-windows/mtld3d.dll"; do
    [ -e "$shim" ] && cp "$SHIM32" "$shim"
done
for shim in "$B/drive_c/windows/system32/mtld3d.dll" "$B/mtld3d/x86_64-windows/mtld3d.dll" \
            "$WINE_TREE/lib/wine/x86_64-windows/mtld3d.dll"; do
    [ -e "$shim" ] && cp "$SHIM64" "$shim"
done
cp "$SRC/unix/target/x86_64-apple-darwin/$OUT/mtld3d.so" \
   "$B/mtld3d/x86_64-unix/mtld3d.so"
# The Wine the sidecar runs under looks for the unix half in its own tree.
WINE_TREE="$(cd "$(dirname "$0")/.." && pwd)/reference/wine-cx/lib/wine"
[ -d "$WINE_TREE" ] && cp "$SRC/unix/target/x86_64-apple-darwin/$OUT/mtld3d.so" \
   "$WINE_TREE/x86_64-unix/mtld3d.so"
echo "done ($OUT)"
