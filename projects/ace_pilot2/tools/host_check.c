/* The port's rules on the host: the original's long autopilot campaign
   (amiga_mcp/examples/ace_pilot2/tests: seed 1234 from mission 1, 20000
   ticks) must end in the original's state hash.

     cc -O2 -I../../sdk/include -Isrc -o build/host_check tools/host_check.c src/ap_core.c src/ap_a3d.c ../../sdk/src/fix16*.c
*/
#include <stdio.h>
#include "ap.h"

#define TRACE_TICKS 20000
#define TRACE_HASH 0x2B867448UL           /* the original's tests/vectors.h */

static APGame g;

int main(void)
{
    int t;
    ap_new_game(&g, 1234, 1);
    for (t = 0; t < TRACE_TICKS && !g.over && !g.won; t++) {
        ap_tick(&g, ap_bot(&g));
        g.nev = 0;
        if (g.phase >= PH_DONE && g.end_t == 0) ap_next(&g);
    }
    printf("mission %d, score %ld, kills %d, ground %d, shots %d, hits %d, lives %d: hash %08lX (original %08lX)\n",
           g.mission, (long)g.score, g.kills, g.gkills, g.shots, g.hits, g.lives,
           (unsigned long)ap_state_hash(&g), TRACE_HASH);
    return ap_state_hash(&g) == TRACE_HASH ? 0 : 1;
}
