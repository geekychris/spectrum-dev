/*
 * Tank Duel for the Spectrum Next: memory map, start-up and the frame loop.
 *
 *   0x0000-0x3FFF  bank 16: the rules (td_core.c)          } map_rules()
 *   0x4000-0x7FFF  bank 17: the AI, the fixed point        }
 *   0x0000-0x3FFF  bank 18: the renderer, the models       } map_render()
 *   0x6000-0x7FFF  Layer 2 pages while drawing (l2poly)
 *   0x8000-0xEFFF  resident: the game state, the SDK
 *   0xF000-        the IM 2 table, the stack
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "td.h"
#include "duel.h"

TDGame game;
Duel du;

/* profiling: raster lines (a line is ~1800 T-states at 28 MHz), counted
   from the frame interrupt's counter and the raster position */
static uint16_t raster(void)
{
    uint8_t f;
    uint16_t r;
    do {
        f = frame_ticks;
        r = ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F);
    } while (f != frame_ticks);
    r = r >= 248 ? r - 248 : r + 64;                /* lines since the interrupt (line 248) */
    return (uint16_t)(f & 63) * 312 + r;            /* wraps at 64 frames */
}
uint16_t prof_t, prof[6];
void lap(uint8_t k)
{
    uint16_t t = raster();
    prof[k] += t >= prof_t ? t - prof_t : t + 64 * 312 - prof_t;
    prof_t = t;
}

static void map_rules(void)
{
    ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33);
    ZXN_WRITE_MMU2(34); ZXN_WRITE_MMU3(35);
}

static void map_render(void)
{
    ZXN_WRITE_MMU0(36); ZXN_WRITE_MMU1(37);
    ZXN_WRITE_MMU2(10); ZXN_WRITE_MMU3(11);
}

/* the original's 32 pens (0x0RGB, 4 bits each) -> the Next's 9-bit palette */
static const uint16_t pens444[32] = {
    0x000, 0xFFF, 0x122, 0x99A, 0x58A, 0x353, 0x575, 0x887, 0x554, 0xAAB, 0x667, 0xCA6, 0x864,
    0xB83, 0x751, 0x642, 0x6D4, 0x372, 0xE92, 0x952, 0x98B, 0x546, 0x6EF, 0xFF6, 0xF73, 0xE22,
    0xF33, 0xFE4, 0x6F6, 0x5EF, 0xFA3, 0x021
};

static void palette(void)
{
    uint16_t p[32];
    uint8_t i;
    for (i = 0; i < 32; i++) {
        uint16_t c = pens444[i];
        uint8_t r = (uint8_t)(((c >> 8) & 15) * 7 + 7) / 15, g = (uint8_t)(((c >> 4) & 15) * 7 + 7) / 15,
                b = (uint8_t)((c & 15) * 7 + 7) / 15;
        p[i] = (uint16_t)r << 6 | (uint16_t)g << 3 | b;
    }
    pal_load(1, p, 32);
}

#ifdef TD_TRACE
/* the original's long autopilot fight (tests/test_cases.c): seed 1234 from
   round 1, 9000 ticks; its state hash is 7CFAB335 on the host and the Amiga */
int main(void)
{
    uint16_t t;
    TDEvent e;
    next_init();
    frame_init();
    map_rules();
    td_new_game(&game, 1234, 1);
    for (t = 0; t < 9000 && !game.over && !game.won; t++) {
        td_tick(&game, td_bot(&game));
        while (td_pop_event(&game, &e)) {}
    }
    zx_log("td: trace round=%d score=%ld kills=%d hp=%d frames=%u hash=%08lx\n", game.round, (long)game.score,
           game.kills, game.tk[0].hp, frame_count, (unsigned long)td_state_hash(&game));
    for (;;) frame_wait();
}
#else
int main(void)
{
    uint8_t ticks, acc = 0, frames = 0;
    uint16_t polys = 0, t50 = 0;
    next_init();
    palette();
    l2_init();
    frame_init();
    map_render();
    render_init();
    map_rules();
    td_new_game(&game, 1234, 1);
    du.state = DS_TITLE;
    {
        uint16_t a, b, c2;
        frame_wait();
        a = raster(); frame_wait(); b = raster(); frame_wait(); frame_wait(); c2 = raster();
        zx_log("td: rastercheck %u %u %u\n", a, b, c2);
    }
    zx_log("td: ready\n");
    for (;;) {
        TDEvent e;
        ticks = frame_wait();
        l2_flip();
        t50 += ticks;
        if (ticks > 4) ticks = 4;
        acc += ticks;
        prof_t = raster();
        map_rules();
        while (acc >= 2) {               /* the original's 25 ticks a second */
            acc -= 2;
            if (game.over || game.won) td_new_game(&game, game.seed + 1, 1);
            td_tick(&game, td_bot(&game));
            while (td_pop_event(&game, &e)) {}
        }
        lap(0);
        map_render();
        render_scene();
        polys += render_polys;
        frames++;
        if (t50 >= 100) {
            zx_log("td: fps=%u polys=%u tick=%ld lines: logic %u grid %u collect %u items %u\n", frames / 2, polys / frames, (long)game.tick,
                   prof[0] / frames, prof[1] / frames, prof[2] / frames, prof[3] / frames);
            prof[0] = prof[1] = prof[2] = prof[3] = 0;
            frames = 0;
            polys = 0;
            t50 -= 100;
        }
    }
}
#endif
