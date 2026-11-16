#!/bin/sh
# tools/reach.c on the original's rules: as it is, then with the port's fixes
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -w -Isrc -o build/host/reach tools/reach.c src/game.c src/levels.c src/port_fixes.c || exit 1
build/host/reach
build/host/reach --fixed
