#!/bin/sh
# The rules against the original: tools/trace_main.c built on the host with
# the original's sources (../../../amiga_mcp/examples/canyon_rail, or
# $CANYON_ORIG) and on the Z80 (make TRACE=1), run, diffed.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
orig=${CANYON_ORIG:-$here/../../../amiga_mcp/examples/canyon_rail}
tmp=$(mktemp -d)
cc -O2 -I"$orig" -I"$orig/../amiga3d" -o "$tmp/orig" "$here/tools/trace_main.c" "$orig/cr_core.c" \
   "$orig/../amiga3d/a3d_math.c" "$orig/../amiga3d/a3d_fix.c"
"$tmp/orig" > "$tmp/orig.txt"
"$here/../../bin/zx-make" -C "$here" TRACE=1 > "$tmp/build.txt" 2>&1 || { tail "$tmp/build.txt"; exit 1; }
"$here/../../zx" log "$here/build-trace/canyon_rail.nex" --frames ${FRAMES:-60000} > "$tmp/z80.txt" 2>&1
grep -v '^trace' "$tmp/z80.txt" | diff - "$tmp/orig.txt" > "$tmp/diff.txt" && echo "trace: identical to the original" || { head -20 "$tmp/diff.txt"; exit 1; }
grep trace: "$tmp/z80.txt"
