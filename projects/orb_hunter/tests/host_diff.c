/*
 * host_diff.c - the port's rules (../src/game.c) against the original's
 * (amiga_mcp examples/orb_hunter game.c + levels.c, as they are): long
 * runs of random play - running, jumping, shooting, rolling, sometimes with
 * every power-up - fed to both, the whole game state compared after every
 * step, through rooms, deaths and item pick-ups, as the original's main.c
 * drives them.
 *
 *   make -C tests      (ORIG=path to the original)
 */
#include <stdio.h>
#include <string.h>
#include "snap.h"

static unsigned long rng = 4242;
static unsigned rnd(unsigned n) { rng = rng * 1103515245UL + 12345; return (unsigned)((rng >> 16) % n); }

int main(void)
{
    static Snap a, b;
    long step, steps = 400000, rooms = 0, deaths = 0, kills = 0;
    int l = 0, r = 0, u = 0, d = 0, f = 0, hold = 0, games = 0;
    int32_t last_room = -1, last_score = 0;
    port_init();
    orig_init();
    for (step = 0; step < steps; step++) {
        if (--hold <= 0) {                      /* a new stick position for a while */
            int m = rnd(10);
            l = m < 3; r = m >= 3 && m < 7; u = rnd(3) == 0; d = rnd(8) == 0; f = rnd(2);
            hold = 5 + rnd(40);
            if (rnd(400) == 0) { port_give_all(); orig_give_all(); }
        }
        port_update(l, r, u, d, u, f);
        orig_update(l, r, u, d, u, f);
        port_snap(&a);
        orig_snap(&b);
        if (memcmp(&a, &b, sizeof(a))) {
            printf("FAIL at step %ld (game %d): hunter %d,%d / %d,%d room %d,%d / %d,%d state %d / %d score %d / %d\n",
                   step, games, a.hx >> 16, a.hy >> 16, b.hx >> 16, b.hy >> 16, a.room_x, a.room_y, b.room_x, b.room_y,
                   a.state, b.state, a.score, b.score);
            return 1;
        }
        if (a.room_x * 8 + a.room_y != last_room) { rooms++; last_room = a.room_x * 8 + a.room_y; }
        if (a.score > last_score) kills++;
        last_score = a.score;
        /* the original's main.c: fire leaves the item screen; game over and the title start a new game */
        if (a.state == 4 && f) { extern void port_resume(void); port_resume(); }
        if (a.state == 3 || a.state == 0) {
            if (a.state == 3) deaths++;
            port_init();
            orig_init();
            last_score = 0;
            games++;
        }
    }
    printf("PASS: %ld steps, %d games, %ld rooms entered, %ld deaths, %ld scoring steps\n", steps, games, rooms, deaths, kills);
    return 0;
}
