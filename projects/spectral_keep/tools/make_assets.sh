#!/bin/sh
# Remake Spectral Keep's data from the original game (tools/orig, from
# amiga_games/spectral_keep) on the host: the pictures (the original's voxel
# renderer and room builder, export.c -> gen_gfx.py), the level data and the
# sound. Needs a C compiler, python3 with numpy and Pillow.
#
#   sh tools/make_assets.sh        (from projects/spectral_keep)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -w -DSCENE_RGB15 -Itools/orig -o build/host/export tools/export.c tools/orig/vox.c \
   tools/orig/textures.c tools/orig/levels.c tools/orig/room.c tools/orig/world.c tools/orig/scene.c
build/host/export build/host
python3 tools/gen_gfx.py build/host
python3 tools/gen_levels.py
python3 tools/gen_sound.py
# the host builds of the logic: the port's (tools/bot.py plays it) and the original's
cc -O2 -Isrc -I../../sdk/include -o build/host/sim tools/sim.c src/game.c src/room.c src/world.c \
   src/levels.c src/probe.c src/isin.c ../../sdk/src/zxrle.c
cc -O2 -w -Itools/orig -o build/host/orig_sim tools/orig_sim.c tools/orig/game.c tools/orig/room.c \
   tools/orig/world.c tools/orig/levels.c
python3 tools/compare.py | head -1
