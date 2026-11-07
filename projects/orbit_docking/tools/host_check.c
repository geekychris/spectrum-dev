/* the port's rules (src/od_core.c on fix16.h) against the original
   (tools/orig, on the amiga3d engine): the original's long autopilot game
   (orig/tests/vectors.h) and games from every station with scripted and
   autopilot input mixed, hashed tick by tick. tools/check_core.sh builds
   the three (the port, the port with fix16's Z80 C, the original) and
   compares what they print. */
#include <stdio.h>
#include "od.h"
#include "orig/tests/vectors.h"

#define OD_TRACE_TICKS 6000             /* as the original's test_cases.c */
static ODGame g;

int main(void)
{
    int k, lv;
    uint32_t s = 1;
    od_new_game(&g, 1234, 1);
    for (k = 0; k < OD_TRACE_TICKS && !g.over && !g.won; k++) od_tick(&g, od_bot(&g));
    printf("trace: %d ticks level %d docks %d crashes %d score %ld hash %08lX (%s)\n", k, g.level, g.docks, g.crashes,
           (long)g.score, (unsigned long)od_state_hash(&g), od_state_hash(&g) == OD_TRACE_HASH ? "matches" : "DIFFERS");
    for (lv = 1; lv <= OD_LEVELS; lv++) {
        uint32_t h = 0;
        od_new_game(&g, 7 + lv, lv);
        for (k = 0; k < 4000 && !g.over && !g.won; k++) {
            unsigned in;
            ODEvent e;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            in = (k / 25) % 8 ? od_bot(&g) : (s >> 9) & 255;
            od_tick(&g, in);
            while (od_pop_event(&g, &e)) h = h * 31 + (uint32_t)e.type * 7 + (uint32_t)e.a * 3 + (uint32_t)e.b;
            h = h * 31 + od_state_hash(&g);
        }
        printf("station %d: %08lX ticks %d docks %d crashes %d score %ld\n", lv, (unsigned long)h, k, g.docks, g.crashes,
               (long)g.score);
    }
    return 0;
}
