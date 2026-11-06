/* The original core (tools/orig) playing the same bot games, for comparison. */
#include <string.h>
#include "a3d.h"
#include "a3d_fix.h"
#include "pb.h"
#include "sim_common.h"
static Table T;
static Game g;

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 10, gi, i;
    pb_build_table(&T, 0);
    for (gi = 0; gi < n; gi++) {
        Bot bot; PbInput in; long t = 0, still = 0;
        memset(&bot, 0, sizeof bot); bot.rng = 1000 + gi;
        pb_game_init(&g, &T, 1 + gi);
        pb_game_start(&g);
        while (g.state != GS_OVER && t < 120L * 900) {
            pb_bot(&g, &in, &bot);
            g.nev = 0;
            pb_game_tick(&g, &in);
            for (i = 0; i < g.nev; i++) ev_count[(int)g.ev[i].type]++;
            sum_sub += g.w.substeps; sub_n++; if (g.w.substeps > max_sub) max_sub = g.w.substeps;
            if (g.w.nballs && g.w.balls[0].state == BS_PLAY && abs(g.w.balls[0].vx) + abs(g.w.balls[0].vy) < 20 * 4096) {
                if (++still == 120 * 5) stuck++;
            } else still = 0;
            t++;
        }
        games++; steps_total += t; balls_total += g.ball_no; score_total += g.score;
    }
    report("original");
    return 0;
}
