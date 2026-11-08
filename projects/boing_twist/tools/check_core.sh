#!/bin/sh
# The rules on the host: src/bt_core.c (the original's, with only the port
# change in tools/orig/port.diff, around a zsdcc miscompile) built with the host compiler, its
# bt_vector_hash() against the original's tests/vectors.h, and the
# original's own host tests (tools/orig/tests/test_core.c) run on it.
# test.py checks the same hash on the Z80N.
set -e
cd "$(dirname "$0")/.."
out=${TMPDIR:-/tmp}/bt_check.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT
diff -u --label tools/orig/bt_core.c --label src/bt_core.c tools/orig/bt_core.c src/bt_core.c | diff -u tools/orig/port.diff -
echo "bt_core.c: the original's but for tools/orig/port.diff"
diff -u tools/orig/bt_core.h src/bt_core.h
cc -O2 -Wall -Isrc -Itools/orig/tests -o "$out/test_core" tools/orig/tests/test_core.c src/bt_core.c
"$out/test_core" | tail -3
