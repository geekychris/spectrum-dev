/*
 * Orbit Docking for the Spectrum Next: start-up and the loop.
 */
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "od.h"
#include "view.h"

const uint16_t view_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0x002, 0xCCD, 0x99A, 0x667, 0xBBC, 0x111, 0x3F5, 0xF33,
    0x889, 0x556, 0xAAB, 0x778, 0x0A8, 0x986, 0x35A, 0x238, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x000, 0x000
};

/* memory: bank 20 (the rules, od_core.c) or bank 21 (the view, view.c)
   at 0x0000-0x3FFF; the rest from 0x8000 */
#define LOGIC() do { ZXN_WRITE_MMU0(40); ZXN_WRITE_MMU1(41); } while (0)
#define VIEW()  do { ZXN_WRITE_MMU0(42); ZXN_WRITE_MMU1(43); } while (0)

static ODGame game;

static uint16_t rgb9(uint16_t c)
{
    return (uint16_t)(((c >> 9) & 7) << 6 | ((c >> 5) & 7) << 3 | ((c >> 1) & 7));
}

int main(void)
{
    uint8_t k;
    uint16_t frames = 0, draws = 0, ticks = 0;
    uint8_t acc = 0;
    next_init();
    l2_init();
    for (k = 0; k < 32; k++) pal_set(1, k, rgb9(view_palette[k]));
    VIEW();
    view_init();
    frame_init();
    LOGIC();
    od_new_game(&game, 1234, 1);
    zx_log("od: ready\n");
    for (;;) {
        uint8_t f = frame_wait(), steps;
        l2_flip();
        acc += f;
        if (acc > 8) acc = 8;
        steps = acc >> 1;
        acc &= 1;
        LOGIC();
        while (steps--) {
            if (game.over || game.won) od_new_game(&game, game.seed + 1, 1);
            od_tick(&game, od_bot(&game));
            ticks++;
        }
        for (k = 0; k < game.ndebris; k++) view_debris_x[k] = (int16_t)(od_debris_x(&game, k) >> 16);
        VIEW();
        view_draw(&game);
        draws++;
        frames += f;
        if (frames >= 100) {
            zx_log("od: perf fps10=%u ticks=%u polys=%u z=%ld level=%d docks=%d\n", (uint16_t)(draws * 500u / frames), ticks,
                   view_polys, game.z >> 16, game.level, game.docks);
            frames = draws = ticks = 0;
        }
    }
    return 0;
}
