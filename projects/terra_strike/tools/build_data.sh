#!/bin/sh
# The data made on the host from the original's code (checked in; test.py
# checks they are still what this makes):
#   src/island.asm, src/island.h   the original's terrain_generate(1987)
#   src/models_data.c              the original's models (models_init)
#   src/music_song.c               the original's MOD (tools/make_music.py)
cd "$(dirname "$0")/.." || exit 1
mkdir -p build-trace
cc -O1 -w -Isrc tools/gen_terrain.c src/terrain.c src/ts_math.c -o build-trace/gen_terrain && build-trace/gen_terrain || exit 1
cc -O1 -w -Isrc tools/gen_models.c src/models.c src/ts_math.c -o build-trace/gen_models && build-trace/gen_models || exit 1
python3 tools/make_music.py
