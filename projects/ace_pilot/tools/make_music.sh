#!/bin/sh
# The original's music (art/blue_danube.mod, the original's gen_mod.py: the
# Blue Danube waltz on a sine "melody" sample and a "bass") for the SDK's
# modplay: its three channels on TurboSound chips 1 and 2 (chip 0 is the
# effects'). Writes src/music_song.c (committed; rerun after changing this).
cd "$(dirname "$0")/.." || exit 1
python3 ../../sdk/tools/mod2ay.py art/blue_danube.mod --name ace_song \
    --route 0=1A,1=1B,2=1C \
    --inst 1=tone:11:8 \
    --inst 2=tone:10:6 > src/music_song.c
