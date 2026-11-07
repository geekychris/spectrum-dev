#!/bin/sh
# The port's rules against the original's, on the host: the port on fix16.h
# (64-bit C), the port on fix16's Z80 C, and the original on the amiga3d
# engine must print the same results.
set -e
cd "$(dirname "$0")"
T=${TMPDIR:-/tmp}/drone_courier_check.$$
mkdir -p "$T"
SDK=../../../sdk
ORIG=${ORIG:-/Users/chris/code/claude_world/amiga_mcp/examples/drone_courier}
printf '#include <stdint.h>\nuint32_t mulu16(uint16_t a, uint16_t b) { return (uint32_t)a * b; }\n' > "$T/mulu16.c"
P="host_check.c ../src/dc_core.c ../src/test_cases.c $SDK/src/fix16.c $SDK/src/fix16_tab.c $SDK/src/fix16_atan.c"
cc -O2 -w -I../src -I$SDK/include $P -o "$T/port"
cc -O2 -w -DFIX16_Z80_C -I../src -I$SDK/include $P "$T/mulu16.c" -o "$T/port_z80"
cc -O2 -w -I$ORIG -I$ORIG/../amiga3d -I$ORIG/tests host_check.c $ORIG/dc_core.c $ORIG/tests/test_cases.c \
   $ORIG/../amiga3d/a3d_math.c $ORIG/../amiga3d/a3d_fix.c -o "$T/orig"
"$T/port" > "$T/port.txt"; "$T/port_z80" > "$T/port_z80.txt"; "$T/orig" > "$T/orig.txt"
cat "$T/port.txt"
if cmp -s "$T/port.txt" "$T/orig.txt" && cmp -s "$T/port_z80.txt" "$T/orig.txt" && grep -q " 0 failed" "$T/port.txt"; then
    echo "PASS: the port's rules give the original's results"; rm -rf "$T"
else
    echo "FAIL: results differ"; diff "$T/orig.txt" "$T/port_z80.txt" || true; diff "$T/orig.txt" "$T/port.txt" || true; rm -rf "$T"; exit 1
fi
