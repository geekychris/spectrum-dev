#!/bin/sh
# The port's rules against the original's, on the host (see host_check.c):
# the port with 64-bit fix16, the port with fix16's Z80 code, and the
# original on the amiga3d engine must print the same hashes.
set -e
cd "$(dirname "$0")"
T=${TMPDIR:-/tmp}/orbit_docking_check.$$
mkdir -p "$T/orig"
SDK=../../../sdk
printf '#include <stdint.h>\nuint32_t mulu16(uint16_t a, uint16_t b) { return (uint32_t)a * b; }\n' > "$T/mulu16.c"
# the original's od.h includes a3d_fix.h: build it from tools/orig
cp orig/od.h orig/od_core.c "$T/orig/"
cc -O2 -I../src -I$SDK/include host_check.c ../src/od_core.c $SDK/src/fix16.c $SDK/src/fix16_tab.c -o "$T/port"
cc -O2 -DFIX16_Z80_C -I../src -I$SDK/include host_check.c ../src/od_core.c $SDK/src/fix16.c $SDK/src/fix16_tab.c \
   "$T/mulu16.c" -o "$T/port_z80"
cc -O2 -I"$T/orig" -Iorig/amiga3d host_check.c "$T/orig/od_core.c" orig/amiga3d/a3d_math.c orig/amiga3d/a3d_fix.c -o "$T/orig_bin"
"$T/port" > "$T/port.txt"
"$T/port_z80" > "$T/port_z80.txt"
"$T/orig_bin" > "$T/orig.txt"
cat "$T/port.txt"
if cmp -s "$T/port.txt" "$T/orig.txt" && cmp -s "$T/port_z80.txt" "$T/orig.txt" && grep -q matches "$T/port.txt"; then
    echo "PASS: the port's rules give the original's hashes"; rm -rf "$T"
else
    echo "FAIL: hashes differ"; diff "$T/orig.txt" "$T/port_z80.txt" || true; rm -rf "$T"; exit 1
fi
