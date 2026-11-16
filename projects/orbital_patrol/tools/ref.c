/*
 * The original's game logic on the host: game.c and flow.c (the original's
 * game.c and its main loop's states) built with the host's C compiler,
 * whose int is 32 bits as on the Amiga. It plays a tape of inputs recorded
 * by the Spectrum game (test.py reads it from the Next's memory: pairs of
 * 16-bit tick, input) and prints the same checkpoint lines the game logs,
 * so the two can be compared tick for tick.
 *
 *   cc -O2 -Isrc -o build/ref tools/ref.c src/game.c src/flow.c
 *   build/ref tape.bin ticks
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "flow.h"

static GameState gs;

int main(int argc, char **argv)
{
    static unsigned char tape[8192];
    FILE *f;
    size_t n = 0, k = 0;
    long ticks, t;
    WORD in = 0;
    if (argc < 3) { fprintf(stderr, "usage: ref tape.bin ticks\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    n = fread(tape, 1, sizeof(tape), f) / 4;
    fclose(f);
    ticks = atol(argv[2]);
    flow_start(&gs);
    for (t = 0; t < ticks; t++) {
        if (k < n && (tape[k * 4] | tape[k * 4 + 1] << 8) == t) {
            in = (WORD)(tape[k * 4 + 2] | tape[k * 4 + 3] << 8);
            k++;
        }
        if (gs.state == STATE_HISCORE_ENTRY) break;
        {   /* the game logs after counting the tick, before the step */
            long tick = t + 1;
            if (tick % 25 == 0)
                printf("op: chk t=%ld st=%d sc=%ld x=%ld y=%ld v=%ld e=%d h=%d\n", tick, gs.state,
                       (long)gs.score, (long)gs.ship.wx, (long)gs.ship.wy, (long)gs.viewport_x,
                       gs.enemies_alive, gs.humans_alive);
        }
        flow_step(&gs, in);
        gs.ev_laser = gs.ev_explode = gs.ev_explode_big = gs.ev_pickup = gs.ev_human_die = 0;
        gs.ev_bomb = gs.ev_hyper = gs.ev_die = gs.ev_level = 0;
    }
    return 0;
}
