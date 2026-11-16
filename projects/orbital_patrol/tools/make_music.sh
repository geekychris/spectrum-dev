#!/bin/sh
# src/music.c from the original's MOD (sdk/tools/mod2ay.py). The MOD's
# channels: 0 arp / saw lead / stab, 1 rolling bass, 2 kick, kick+clap and
# snare, 3 closed and open hats. They go to TurboSound chips 1 and 2,
# leaving chip 0's three channels to the effects (sound.c).
cd "$(dirname "$0")/.."
python3 ../../sdk/tools/mod2ay.py tools/orbital_patrol.mod --name op_song --route 0=1A,1=1B,2=2A,3=2B \
  --inst 1=tone:10 --inst 2=tone:12 --inst 3=kick:13:1:2:220:250 --inst 4=kick:13:1:2:180:220 \
  --inst 5=noise:8:1:3:1 --inst 6=noise:8:1:1:1 --inst 7=tone:11:6:1 --inst 8=noise:12:1:1:10 \
  --inst 9=tone:10:2:1 > src/music.c
