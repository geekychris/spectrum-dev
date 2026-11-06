#!/bin/sh
# src/music.c from the original's MOD (sdk/tools/mod2ay.py): lead and bass on
# TurboSound chip 0 A/B, drums and the stab/bell channel on chip 1 A/B.
cd "$(dirname "$0")/.."
python3 ../../sdk/tools/mod2ay.py tools/stakattack.mod --name stak_song --route 0=0A,1=0B,2=1A,3=1B \
  --inst 1=tone:11 --inst 2=tone:12 --inst 3=kick:13:1:2:220:250 --inst 4=noise:12:1:1:10 \
  --inst 5=noise:10:1:3:1 --inst 6=tone:10:2:1 --inst 7=tone:11:6:1 > src/music.c
