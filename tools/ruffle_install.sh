#!/bin/sh
# Install our "menu -> game" bridge into the Ruffle tree.
#
# Ruffle lies in `reference/ruffle` and is not in git (a foreign licence,
# MIT/Apache-2.0). For the BF2 menu movie to work, two things have to be
# added there:
#
#   1. `tools/ruffle_bf2_bridge.rs` — the bridge itself, copied into the tree;
#   2. `tools/ruffle_bf2_bridge.patch` — the three lines that wire it up.
#
# The bridge lives **with us**, not in the patch: otherwise the same logic
# lives in two files and drifts apart (see "Rakes" in CLAUDE.md). The script
# can be run as many times as you like.
set -e

here=$(cd "$(dirname "$0")/.." && pwd)
ruffle="$here/reference/ruffle"

if [ ! -f "$ruffle/core/Cargo.toml" ]; then
  echo "no $ruffle — see docs/research/11-ruffle-menu.md" >&2
  exit 1
fi

cp "$here/tools/ruffle_bf2_bridge.rs" "$ruffle/core/src/avm1/globals/bf2_bridge.rs"
echo "bridge: core/src/avm1/globals/bf2_bridge.rs"

# The wiring — only if it is not there yet.
if grep -q "bf2_bridge" "$ruffle/core/src/avm1/globals.rs"; then
  echo "wiring: already in place"
else
  (cd "$ruffle" && git apply --exclude=Cargo.lock "$here/tools/ruffle_bf2_bridge.patch")
  echo "wiring: added"
fi
