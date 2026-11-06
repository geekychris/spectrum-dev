/*
 * The original's game logic (tools/orig: Q12, 32-bit) on the host with the
 * same stdin protocol as sim.c, printing "X state room px py pz" (Q12) after
 * each step, for tools/compare.py.
 *
 *   cc -O2 -Itools/orig -o build/host/orig_sim tools/orig_sim.c tools/orig/game.c \
 *      tools/orig/room.c tools/orig/world.c tools/orig/levels.c
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "keep.h"

void keep_log(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("L %s", buf);
    if (!strchr(buf, '\n')) printf("\n");
}
int  snd_init(void) { return 1; }
void snd_play(int s) { (void)s; }
void snd_music(int t) { (void)t; }
void snd_duck(long t) { (void)t; }
void snd_music_enable(int on) { (void)on; }

int main(void)
{
    char line[64];
    unsigned held, pressed;
    game_init();
    for (;;) {
        const Body *b = R.player >= 0 ? R.act[R.player].body : 0;
        printf("X %d %d %ld %ld %ld\n", G.state, R.ridx, b ? b->px : 0L, b ? b->py : 0L, b ? b->pz : 0L);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin) || line[0] == 'q') break;
        if (sscanf(line, "%u %u", &held, &pressed) != 2) continue;
        game_step(held, pressed);
    }
    return 0;
}
