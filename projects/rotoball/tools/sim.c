/* The game's logic (src/game.c) built on the host as a shared library, for
   tools/bot.py's development and checks:
     cc -O2 -shared -fPIC -Isrc -I../../sdk/include -o build/host/libroto.so tools/sim.c src/game.c */
#include "roto.h"

Game *sim_game(void) { return &G; }
