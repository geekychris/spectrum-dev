/* The Z80 port's simulation on the host: bot-played games, statistics
   (compare with tools/orig_sim.c). cc -I src tools/host_sim.c src/pb_*.c ... */
#include <string.h>
#include "pbz.h"
#include "sim_common.h"
extern Table table_0;
static Game g;

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 10, gi, i;
    for (gi = 0; gi < n; gi++) {
        Bot bot; PbInput in; long t = 0, still = 0;
        memset(&bot, 0, sizeof bot); bot.rng = 1000 + gi;
        pb_game_init(&g, &table_0, 1 + gi);
        pb_game_start(&g);
        while (g.state != GS_OVER && t < 120L * 900) {
            pb_bot(&g, &in, &bot);
            g.nev = 0;
            pb_game_tick(&g, &in);
            for (i = 0; i < g.nev; i++) ev_count[(int)g.ev[i].type]++;
            sum_sub += g.w.substeps; sub_n++; if (g.w.substeps > max_sub) max_sub = g.w.substeps;
            for (i = 0; i < g.w.nballs; i++) {
                Ball *b = &g.w.balls[i];
                if (b->state == BS_PLAY && (b->x < -200 || b->x > 8600 || b->y > 17000)) escapes++;
            }
            if (g.w.nballs && g.w.balls[0].state == BS_PLAY && abs(g.w.balls[0].vx) + abs(g.w.balls[0].vy) < 20) {
                if (++still == 120 * 5) stuck++;
            } else still = 0;
            t++;
        }
        games++; steps_total += t; balls_total += g.ball_no; score_total += g.score;
        if (argc > 2) printf("game %d: score %ld balls %d steps %ld\n", gi, (long)g.score, g.ball_no, t);
    }
    report("z80 port");
    return 0;
}
