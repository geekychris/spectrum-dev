/* Ace Pilot (Next) - bring-up: the rules alone, to measure a tick */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "game.h"
#include "ace_input.h"

void sfx_gunfire(void) {}
void sfx_explosion(void) {}
void sfx_hit(void) {}
void sfx_die(void) {}

static GameWorld world;
WORD angle_to(LONG dx, LONG dz);
WORD angle_to_scan(LONG dx, LONG dz);

int main(void)
{
    uint16_t t = 0;
    next_init();
    frame_init();
    {   /* bring-up check: the asm angle_to against the original's scan */
        uint16_t i, bad = 0;
        rnd_seed(7);
        for (i = 0; i < 400; i++) {
            int16_t x = (int16_t)(rnd16() % 8001) - 4000, z = (int16_t)(rnd16() % 8001) - 4000;
            WORD f, sc;
            if (i & 1) x >>= 3;                    /* shorter vectors, pitch-like */
            f = angle_to(x, z);
            sc = angle_to_scan(x, z);
            if (f != sc) { bad++; if (bad < 5) zx_log("ace: angle %d,%d fast=%d scan=%d\n", x, z, f, sc); }
        }
        zx_log("ace: angle check bad=%u\n", bad);
    }
    game_init(&world);
    zx_log("ace: ready\n");
    for (;;) {
        frame_wait();
        game_update(&world, (t & 64) ? (INPUT_LEFT | INPUT_FIRE) : (INPUT_UP | INPUT_FIRE), 0);
        if ((++t & 255) == 0) zx_log("ace: t=%u wave=%d alive=%d\n", t, world.wave, world.enemies_alive);
    }
}
