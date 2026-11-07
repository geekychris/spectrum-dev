#!/bin/sh
# Remakes Planet Chomp's tables, pictures and sound from the original's
# sources (tools/orig) and checks the port's rules against the original.
# The outputs (src/*_tab.c, src/rtab*.c, gfx/, sound/, src/gfx_data.*,
# src/spr_data.*, src/hud_tiles.h, src/sound_data.*) are checked in.
#   sh tools/make_assets.sh
set -e
cd "$(dirname "$0")/.."
PY=../../.venv/bin/python
mkdir -p build
cc -O2 -w -Itools/orig tools/gen_data.c -o build/gen_data && build/gen_data
cc -O2 -w -Itools/stub -Itools/orig tools/gen_sprites.c -o build/gen_sprites && build/gen_sprites
$PY tools/gen_gfx.py
$PY tools/gen_sound.py
sh tools/compare.sh 300000 7
