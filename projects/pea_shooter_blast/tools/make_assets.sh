#!/bin/sh
# Remake Pea Shooter Blast's data on the host: the pictures and palettes
# (gen_gfx.py), the effects (gen_sound.py), the music (the original's MOD,
# tools/orig/music.mod, by sdk/tools/mod2ay.py), then the scripted game on
# the original's rules on the host (the hash test.py expects from the Next).
# Needs python3 with numpy and Pillow, and a C compiler.
#
#   sh tools/make_assets.sh        (from projects/pea_shooter_blast)
set -e
cd "$(dirname "$0")/.."
python3 tools/gen_gfx.py
python3 tools/gen_sound.py
# the MOD's channels: lead, bass, drums -> chip 0 A B C; echo -> chip 1 A
python3 ../../sdk/tools/mod2ay.py tools/orig/music.mod --name pea_song --route 0=0A,1=0B,2=0C,3=1A \
    --inst 1=tone:12:8 --inst 2=tone:13:6 --inst 3=kick:14:1:2:220:250 --inst 4=noise:12:1:2:8 \
    --inst 5=noise:8:1:3:1 --inst 6=tone:8:6 > src/music.c
sh tools/host_trace.sh
