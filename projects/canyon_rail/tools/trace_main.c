/*
 * trace_main.c - whole autopilot games with every event and the state hash
 * every 250 ticks. Built on the host against the original's sources and on
 * the Z80 against the port (src/trace.c, make TRACE=1); tools/check_trace.sh
 * diffs the two. The first game is the original's tests/vectors.h game.
 */
#include <stdint.h>
#include "cr.h"

#ifdef __SDCC
#include "zxlog.h"
#define OUT zx_log
#else
#include <stdio.h>
#define OUT printf
#endif

static CRGame tg;

static void play(uint32_t seed, int stage, int32_t ticks, unsigned mask)
{
    CRGame *g = &tg;
    CREvent e;
    int32_t t;
    cr_new_game(g, seed, stage);
    OUT("game seed %lu stage %d\n", (unsigned long)seed, stage);
    for (t = 0; t < ticks && !g->over && !g->won; t++) {
        cr_tick(g, cr_bot(g) & mask);
        while (cr_pop_event(g, &e))
            if (e.type == CRE_OVER || e.type == CRE_WIN)
                OUT("t%ld %s score %ld\n", (long)t, cr_event_names[e.type], (long)g->score);
            else if (e.type != CRE_SHOT && e.type != CRE_FIRE)
                OUT("t%ld %s %d %d\n", (long)t, cr_event_names[e.type], (int)e.a, (int)e.b);
        if (t % 250 == 0)
            OUT("t%ld h %08lx sight %d %d\n", (long)t, (unsigned long)cr_state_hash(g), (int)g->sx, (int)g->sy);
    }
    OUT("end t%ld h %08lx score %ld stage %d shield %d hits %d/%d\n", (long)t, (unsigned long)cr_state_hash(g),
        (long)g->score, (int)g->stage, (int)g->shield, (int)g->hits, (int)g->shots);
}

void trace_games(void)
{
    play(1234, 1, 3043, 0xFF);          /* tests/vectors.h: 0x137A07CD */
    play(7, 2, 3000, 0xFF);
    play(99, 3, 2500, 0xFF);
    play(5, 1, 3000, 0xFF & ~CR_IN_FIRE);   /* never firing: hurts, game over */
}

#ifndef __SDCC
int main(void)
{
    trace_games();
    return 0;
}
#endif
