/* the port's rules (src/mt_core.c on fix16.h) against the original
   (tools/orig, on the amiga3d engine): the original's whole autopilot game
   (tests/vectors.h) and games from every level with scripted and autopilot
   input, compared tick by tick through the state hash.
     cc -O2 -I../src -I../../../sdk/include host_check.c ../src/mt_core.c
        ../../../sdk/src/fix16.c ../../../sdk/src/fix16_tab.c -o host_check
   (tools/check_core.sh builds and runs it, and the original's twin) */
#include <stdio.h>
#include "mt.h"
#include "orig/tests/vectors.h"

static MTGame g;

int main(int argc, char **argv)
{
    int k, lv;
    uint32_t s = 1;
    mt_new_game(&g, 1, 1);
    for (k = 0; k < 6000 && !g.over && !g.won; k++) mt_tick(&g, mt_bot(&g));
    printf("trace %s level %d ticks %d score %ld hash %08lX (%s)\n", g.won ? "won" : "stopped", g.level, k,
           (long)g.score, (unsigned long)mt_state_hash(&g), mt_state_hash(&g) == MT_TRACE_HASH ? "matches" : "DIFFERS");
    /* per level: random tilts mixed with the autopilot, hashes printed for diffing */
    for (lv = 1; lv <= MT_LEVELS; lv++) {
        uint32_t h = 0;
        mt_new_game(&g, 1, lv);
        for (k = 0; k < 3000 && !g.over; k++) {
            unsigned in;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            in = (k / 40) & 1 ? mt_bot(&g) : (s >> 7) & 15;
            mt_tick(&g, in);
            h = h * 31 + mt_state_hash(&g);
        }
        printf("level %d: %08lX falls %d bumps %d\n", lv, (unsigned long)h, g.falls, g.bumps);
    }
    (void)argc; (void)argv;
    return 0;
}
