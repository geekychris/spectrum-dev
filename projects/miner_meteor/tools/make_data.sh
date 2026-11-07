#!/bin/sh
# Regenerates src/levels_data.c/.h from src/levels.txt (checked in, so a build
# needs no Python).
cd "$(dirname "$0")/.." && python3 ../../sdk/tools/levelpack.py src/levels.txt --prefix lv \
    -o src/levels_data.c --header src/levels_data.h
