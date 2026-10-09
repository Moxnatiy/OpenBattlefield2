#!/bin/zsh
# REA (github.com/morluto/rea) over the game's binaries, with a snapshot per binary.
#
#   tools/rea.sh <command> <binary> [args...]
#   tools/rea.sh function RendDX9.dll 0x1002c2e0
#   tools/rea.sh search BF2.exe tweakNearPlane
#   tools/rea.sh resolve-native-call-targets RendDX9.dll 0x1002c2eb
#
# <binary> is a name under `Game Files/` (or any path). Every answer is an
# Evidence record (JSON): the function whole — pseudocode, assembly, callers,
# callees, references, strings, basic blocks, value flow — with its limitations.
#
# Each CLI call imports and analyses the binary again in a temporary Ghidra
# project (RendDX9.dll: 2.5 minutes), so every answer is kept in
# ghidra_projects/rea/<binary>.json and an identical question is answered from
# it in seconds. Inside an agent session the REA MCP server keeps one analysis
# open across calls instead (`open_binary`), which is the cheaper way for more
# than a couple of questions.
#
# Needs Ghidra 12.1.4 exactly (REA 4.1.0 refuses 12.1.3) and a JDK 21.
set -e
if (( $# < 2 )); then
  sed -n '2,20p' "$0"
  exit 2
fi
root=${0:A:h:h}
command=$1
binary=$2
shift 2
[[ -f $binary ]] || binary="$root/Game Files/$binary"
binary=${binary:A}
export GHIDRA_INSTALL_DIR=${GHIDRA_INSTALL_DIR:-/opt/homebrew/opt/ghidra/libexec}
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@21}
mkdir -p "$root/ghidra_projects/rea"
exec npx -y rea-agents@4.1.0 "$command" "$binary" "$@" --provider ghidra \
  --snapshot "$root/ghidra_projects/rea/${binary:t}.json" --json
