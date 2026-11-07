/* A trace of the rules tick by tick with pseudo-random input, built twice:
   with the original's game.c (amiga_mcp/examples/ace_pilot, -DORIG) and with
   the port's src/game.c. tools/trace.sh diffs them. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "game.h"
#include "input.h"

void sfx_gunfire(void) { printf(" gun"); }
void sfx_explosion(void) { printf(" boom"); }
void sfx_hit(void) { printf(" hit"); }
void sfx_die(void) { printf(" die"); }

#ifdef ORIG
void engine3d_init(void);
#endif

static GameWorld w;

/* an autopilot: turn toward the nearest enemy, climb or dive to its height,
   fire when it is roughly ahead (both builds read the same state) */
#include <math.h>
static UWORD bot(int pi)
{
    PlayerState *p = &w.players[pi];
    int i, best = -1;
    double bd = 1e18, want, dy, diff;
    UWORD in = 0;
    if (!p->alive) return 0;
    for (i = 0; i < MAX_ENEMIES; i++) {
        double dx, dz, d;
        if (!w.enemies[i].active) continue;
        dx = w.enemies[i].pos.x - p->cam.pos.x;
        dz = w.enemies[i].pos.z - p->cam.pos.z;
        d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return INPUT_FIRE;
    {
        double dx = w.enemies[best].pos.x - p->cam.pos.x, dz = w.enemies[best].pos.z - p->cam.pos.z;
        double horiz = sqrt(dx * dx + dz * dz), elev, pnow, pd;
        want = atan2(dx, dz) * 128 / M_PI;
        diff = fmod(want - p->cam.yaw + 512 + 128, 256) - 128;
        if (diff > 1) in |= INPUT_RIGHT;
        if (diff < -1) in |= INPUT_LEFT;
        dy = w.enemies[best].pos.y - p->cam.pos.y;
        elev = -atan2(dy, horiz) * 128 / M_PI;           /* pitch that points at it (+ = down) */
        if (elev > 32) elev = 32;
        if (elev < -32) elev = -32;
        pnow = p->cam.pitch > 128 ? p->cam.pitch - 256 : p->cam.pitch;
        pd = elev - pnow;
        if (pd > 1) in |= INPUT_DOWN;
        if (pd < -1) in |= INPUT_UP;
        if (diff > -2 && diff < 2 && pd > -3 && pd < 3 && horiz < 600) in |= INPUT_FIRE;
    }
    return in;
}

int main(int argc, char **argv)
{
    unsigned long seed = argc > 1 ? strtoul(argv[1], 0, 10) : 1;
    long ticks = argc > 2 ? atol(argv[2]) : 20000, t;
    UWORD in = 0;
    int i, use_bot = argc > 4 ? atoi(argv[4]) : 0;
#ifdef ORIG
    engine3d_init();
#endif
    g_tune.num_players = argc > 3 ? atol(argv[3]) : 1;
    game_init(&w);
    for (t = 0; t < ticks; t++) {
        if (use_bot) in = bot(0);
        else if ((t & 15) == 0) {                     /* new stick input every 16 ticks */
            seed = seed * 1103515245UL + 12345UL;
            in = (UWORD)((seed >> 16) & (INPUT_LEFT | INPUT_RIGHT | INPUT_UP | INPUT_DOWN | INPUT_FIRE
                                         | INPUT_THROT_UP | INPUT_THROT_DN));
        }
        printf("t%ld", t);
        game_update(&w, in, use_bot ? bot(1) : (UWORD)(in ^ 3));
        printf(" st=%d w=%d ea=%d", w.state, w.wave, w.enemies_alive);
        for (i = 0; i < 2; i++) {
            PlayerState *p = &w.players[i];
            printf(" p%d=%ld,%ld,%ld,%d,%d s%ld l%d a%d", i, (long)p->cam.pos.x, (long)p->cam.pos.y,
                   (long)p->cam.pos.z, p->cam.yaw, p->cam.pitch, (long)p->score, p->lives, p->alive);
        }
        for (i = 0; i < MAX_ENEMIES; i++)
            if (w.enemies[i].active)
                printf(" e%d=%ld,%ld,%ld,%d,%d,%d", i, (long)w.enemies[i].pos.x, (long)w.enemies[i].pos.y,
                       (long)w.enemies[i].pos.z, w.enemies[i].yaw, w.enemies[i].pitch, w.enemies[i].ai_state);
        for (i = 0; i < MAX_BULLETS; i++)
            if (w.bullets[i].active) printf(" b%d=%ld,%ld,%ld", i, (long)w.bullets[i].pos.x, (long)w.bullets[i].pos.y, (long)w.bullets[i].pos.z);
        for (i = 0; i < MAX_GROUND; i++) printf("%c", w.ground[i].active ? 'G' : '.');
        printf("\n");
        if (w.state == STATE_GAMEOVER) break;
    }
    return 0;
}
