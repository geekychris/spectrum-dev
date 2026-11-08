#!/bin/sh
# The port's rules against the original's, on the host: gen_levels (the
# original's code) makes the levels and the hashes of its flights; the port
# (src/ld_core.c) flies the same flights from the level records, once with
# fix16's 64-bit host code and once with its Z80 C (-DFIX16_Z80_C).
set -e
cd "$(dirname "$0")"
T=${TMPDIR:-/tmp}/lander_3d_check.$$
mkdir -p "$T/levels"
SDK=../../../sdk
FIX="$SDK/src/fix16.c $SDK/src/fix16_tab.c $SDK/src/fix16_atan.c $SDK/src/fix16_hyp3.c"
printf '#include <stdint.h>\nuint32_t mulu16(uint16_t a, uint16_t b) { return (uint32_t)a * b; }\n' > "$T/mulu16.c"
cc -O2 -Iorig -Iorig/amiga3d -I. gen_levels.c orig/ld_core.c orig/amiga3d/a3d_math.c orig/amiga3d/a3d_fix.c -o "$T/gen"
"$T/gen" "$T/levels" > /dev/null
cc -O2 -I../src -I$SDK/include -I. host_check.c host_ter.c ../src/ld_core.c $FIX -o "$T/port"
cc -O2 -DFIX16_Z80_C -I../src -I$SDK/include -I. host_check.c host_ter.c ../src/ld_core.c $FIX "$T/mulu16.c" -o "$T/port_z80"
ok=1
"$T/port" "$T/levels" || ok=0
"$T/port_z80" "$T/levels" | tail -1 || ok=0
rm -rf "$T"
if [ $ok = 1 ]; then echo "PASS: the port's rules fly the original's flights"; else echo "FAIL"; exit 1; fi
