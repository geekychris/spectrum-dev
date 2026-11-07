#!/bin/sh
# The simulation against the original: build the original's trace on the
# host (from ../../../amiga_mcp/examples/scairy_fun, or $SCAIRY_ORIG), run
# this port's trace build in the emulator, diff. Prints the frames per step.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
orig=${SCAIRY_ORIG:-$here/../../../amiga_mcp/examples/scairy_fun}
tmp=$(mktemp -d)
cc -O2 -I"$orig" -I"$orig/../amiga3d" -o "$tmp/orig" "$orig/tests/trace_main.c" "$orig/track.c" "$orig/world.c" "$orig/../amiga3d/a3d_math.c"
for t in 50 100; do for r in 1 2 3 4; do "$tmp/orig" $r $t 7500; done; done > "$tmp/orig.txt"
"$here/../../bin/zx-make" -C "$here" TRACE=1 > "$tmp/build.txt" 2>&1 || { tail "$tmp/build.txt"; exit 1; }
"$here/../../zx" log "$here/build-trace/scairy_fun.nex" --frames 60000 > "$tmp/z80.txt" 2>&1
grep -v '^trace' "$tmp/z80.txt" | diff - "$tmp/orig.txt" > "$tmp/diff.txt" && echo "trace: identical to the original" || { head -20 "$tmp/diff.txt"; exit 1; }
grep trace: "$tmp/z80.txt"
