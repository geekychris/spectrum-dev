/* the port's rules (src/rr_core.c on fix16.h) against the original
   (tools/orig, on the amiga3d engine): the original's long autopilot race
   (tests/vectors.h), then every track with the autopilot and with random
   input mixed in, the state hash folded in every tick. Built twice from
   this file by tools/check_core.sh (the port, the original) and the outputs
   compared. */
#include <stdio.h>
#include "rr.h"
#include "tests/vectors.h"

static RRGame g;

int main(void)
{
    int t, k;
    uint32_t s = 7;
    rr_new_game(&g, 1234, 3);
    for (t = 0; t < 9000 && !g.finished && !g.over; t++) rr_tick(&g, rr_bot(&g));
    printf("trace: place %ld score %ld ticks %ld hash %08lX (%s)\n", (long)g.place, (long)g.score, (long)g.race_ticks,
           (unsigned long)rr_state_hash(&g), rr_state_hash(&g) == RR_TRACE_HASH ? "matches" : "DIFFERS");
    for (k = 1; k <= RR_TRACKS; k++) {
        uint32_t h = 0;
        rr_new_game(&g, (uint32_t)k * 77u, k);
        for (t = 0; t < 2500 && !g.over && !g.won; t++) {
            unsigned in;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            in = (t / 50) % 3 == 2 ? (unsigned)(s >> 9) & 31 : rr_bot(&g);
            rr_tick(&g, in);
            h = h * 31 + rr_state_hash(&g);
        }
        printf("track %d: %08lX score %ld gates %ld jumps %ld bumps %ld place %ld\n", k, (unsigned long)h,
               (long)g.score, (long)g.gates_hit, (long)g.jumps, (long)g.bumps, (long)g.place);
    }
    return 0;
}
