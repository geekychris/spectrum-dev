/*
 * sim.c - one build of the game logic on the host, driven by scripted input,
 * printing the state after every tick. Built twice by compare.sh: with
 * -DORIG from the original's game.c / maze.c (tools/orig, 32-bit longs as
 * on the 3DO and the Amiga) and with -DPORT from the port's src/. The two
 * traces must be the same.
 *
 *   sim TICKS SEED   scripted play: start, steer at random, sometimes idle
 *                    long enough for the attract demo, take keys, die ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#ifdef ORIG
#define long int
#include "game.h"
#include "amiga3do.h"
#include "sfx.h"
void sfx_play(int id) { (void)id; }
void sfx_crumb(void) { }
void sfx_ambience(int w) { (void)w; }
#undef long
#define DIR_OF(m) (m).heading
#else
#include "pc.h"
#include "game.h"
void sfx_play(uint8_t id) { (void)id; }
void sfx_crumb(void) { }
void sfx_ambience(uint8_t w) { (void)w; }
void pc_log(const char *fmt, ...) { (void)fmt; }
#endif

static uint32_t r = 1;
static uint32_t rnd(uint32_t n) { r = r * 1664525u + 1013904223u; return (r >> 8) % n; }

static void mover(const char *tag, int from, int to, int t, int hx, int hy, int hz, int moving)
{
    printf(" %s%d>%d@%d/%d,%d,%d/%d", tag, from, to, t, hx, hy, hz, moving);
}

int main(int argc, char **argv)
{
    long n = argc > 1 ? atol(argv[1]) : 100000, i;
    int idle = 0, dir = 0;
    r = argc > 2 ? (uint32_t)atol(argv[2]) : 1;
    game_init();
    for (i = 0; i < n; i++) {
        unsigned held = 0, pressed = 0, k;
        if (g.state == GS_TITLE && g.timer <= 1 && (i == 0 || rnd(2))) idle = 900;   /* let the attract demo play */
        if (g.demo && idle < 2) idle = 2;                    /* and watch it to the end */
        if (idle > 0) idle--;
        else {
            k = rnd(1000);
            if (k < 2) idle = 900;                         /* long enough for the demo */
            else if (k < 40) { dir = rnd(4); pressed = 1u << dir; }
            else if (k < 44) pressed = PAD_A;
            else if (k < 46) pressed = PAD_C;
            else if (k < 47) pressed = PAD_P;
            if (rnd(100) < 70) held = 1u << dir;
            if (rnd(100) < 3) held |= rnd(2) ? PAD_L : PAD_R;
        }
        held |= pressed;
        game_step(held, pressed);
        printf("%ld s%d p%d sc%d li%d lv%d cr%d pw%d cam%d,%d,%d/%d,%d,%d/%d", i, (int)g.state, (int)g.paused,
               (int)g.score, (int)g.lives, (int)g.level, (int)g.crumbs_left, (int)g.power_left,
               (int)g.cam.focus.x, (int)g.cam.focus.y, (int)g.cam.focus.z, (int)g.cam.up.x, (int)g.cam.up.y,
               (int)g.cam.up.z, (int)g.cam.height);
        mover("P", g.player.from, g.player.to, (int)g.player.t, (int)g.player.heading.x, (int)g.player.heading.y,
              (int)g.player.heading.z, g.player.moving);
        for (k = 0; k < 4; k++) {
            printf(" m%df%d", (int)g.ghost[k].mode, (int)g.ghost[k].fright);
            mover("G", g.ghost[k].mv.from, g.ghost[k].mv.to, (int)g.ghost[k].mv.t, (int)g.ghost[k].mv.heading.x,
                  (int)g.ghost[k].mv.heading.y, (int)g.ghost[k].mv.heading.z, g.ghost[k].mv.moving);
        }
        printf(" d%d\n", (int)g.demo);
    }
    return 0;
}
