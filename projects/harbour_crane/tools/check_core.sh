#!/bin/sh
# The port's rules (src/hc_core.c on fix16) run the original's tests on the
# host twice: with fix16's 64-bit C, and with its Z80 model (the steps of
# fix16.asm in C, -DFIX16_Z80_C). Both must pass every test, including the
# 15,000-tick autopilot game's hash from the original (tools/orig/vectors.h).
#   sh tools/check_core.sh        (from projects/harbour_crane)
set -e
cd "$(dirname "$0")/.."
SDK=../../sdk
mkdir -p build/host
cc -O2 -w -Isrc -Itools/orig -I$SDK/include -o build/host/check tools/host_check.c tools/orig/test_cases.c \
   src/hc_core.c $SDK/src/fix16.c $SDK/src/fix16_tab.c
build/host/check
cc -O2 -w -DFIX16_Z80_C -Isrc -Itools/orig -I$SDK/include -o build/host/check_z80 tools/host_check.c \
   tools/orig/test_cases.c src/hc_core.c $SDK/src/fix16.c $SDK/src/fix16_tab.c tools/mulu16.c
build/host/check_z80
