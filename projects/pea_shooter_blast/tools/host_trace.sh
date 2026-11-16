#!/bin/sh
# The scripted game (src/state_hash.h) on the original's rules compiled for
# the host; prints the state hash the Next must reproduce.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -w -Isrc -o build/host/trace tools/host_trace.c src/game.c src/levels.c src/state_hash.c src/port_fixes.c
build/host/trace
