/* The port's rules (src/td_core.c, src/td_fix.c) on the host: the original's
   long autopilot fight (amiga_mcp/examples/tank_duel/tests: seed 1234 from
   round 1, 9000 ticks) must end in the original's state hash.

     cc -O2 -I../../sdk/include -Isrc -o build/host_check tools/host_check.c src/td_core.c src/td_fix.c
*/
#include <stdio.h>
#include "td.h"

#define TRACE_TICKS 9000
#define TRACE_HASH 0x7CFAB335UL           /* the original's tests/vectors.h */

static TDGame g;

int main(void)
{
    int t;
    TDEvent e;
    td_new_game(&g, 1234, 1);
    for (t = 0; t < TRACE_TICKS && !g.over && !g.won; t++) {
        td_tick(&g, td_bot(&g));
        while (td_pop_event(&g, &e)) {}
    }
    printf("round %d, score %ld, kills %d, shots %d, hits %d, hp %d: hash %08lX (original %08lX)\n",
           g.round, (long)g.score, g.kills, g.shots, g.hits, g.tk[0].hp,
           (unsigned long)td_state_hash(&g), TRACE_HASH);
    for (t = 0; t < g.ntanks; t++)
        printf("tank %d alive=%d hp=%d x=%ld z=%ld yaw=%u v=%ld\n", t, g.tk[t].alive, g.tk[t].hp,
               (long)g.tk[t].x, (long)g.tk[t].z, (unsigned)(uint16_t)g.tk[t].yaw, (long)g.tk[t].v);
    printf("tick=%ld shots=%d hits=%d dmg=%d nobs=%d\n", (long)g.tick, g.shots, g.hits, g.damage_taken, g.nobs);
    return td_state_hash(&g) == TRACE_HASH ? 0 : 1;
}
