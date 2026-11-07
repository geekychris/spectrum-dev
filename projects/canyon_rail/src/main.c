/*
 * Canyon Rail for the Spectrum Next: start-up and the frame loop.
 * (prototype: the autopilot rides, the frame rate is logged)
 */
#ifndef CR_TRACE
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "palfx.h"
#include "nextcopper.h"
#include "zxlog.h"
#include "cr.h"
#include "render.h"

CRGame game;

/* the banks at 0x0000-0x3FFF (slots 0 and 1) */
void map_rules(void) { ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33); }    /* bank 16: cr_core.c */
void map_render(void) { ZXN_WRITE_MMU0(36); ZXN_WRITE_MMU1(37); }   /* bank 18: render.c */
static volatile uint16_t frames50;
static void on_frame(void) { frames50++; }

/* the original's 32 pens (render.c's names; 12-bit RGB) */
const uint16_t rail_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0xE96, 0x975, 0x864, 0x258, 0xB64, 0x843, 0xA75, 0x754, 0x632,
    0x889, 0x556, 0x6CD, 0x38A, 0x9F4, 0xF44, 0xFA8, 0x99B, 0x667, 0xF5E, 0xFA3, 0x000, 0x000,
    0xF33, 0xFE4, 0x6F6, 0x5EF, 0x000, 0x000
};

static void set_palette(void)
{
    static uint16_t pal[32];
    uint8_t k;
    for (k = 0; k < 32; k++) {
        uint16_t c = rail_palette[k];
        pal[k] = RGB9((((c >> 8) & 15) * 7 + 7) / 15, (((c >> 4) & 15) * 7 + 7) / 15, ((c & 15) * 7 + 7) / 15);
    }
    palfx_load(PAL_L2, 0, 32, pal);
}

/* port: the view is drawn 96 rows high (Layer 2 rows 0..95) and shown 192:
   every other line the copper moves Layer 2's Y scroll (NextReg 0x17) back a
   row, so each row is shown twice (ScAIry Fun's technique) */
static void line_doubling(void)
{
    uint16_t L;
    copper_begin();
    copper_move(0x17, 0);
    for (L = 2; L < VIEW_H; L += 2) {
        copper_wait(L - 1, 50);                 /* the end of the line before */
        copper_move(0x17, (uint8_t)(192 - L / 2));
    }
    copper_end();
}

int main(void)
{
    uint8_t acc = 0, loops = 0;
    uint16_t tstart;
    next_init();
    frame_init();
    frame_hook = on_frame;
    l2_init();
    set_palette();
    line_doubling();
    map_render();
    render_init();
    map_rules();
    cr_new_game(&game, 777, 1);
    zx_log("canyon: ready\n");
    tstart = frames50;
    for (;;) {
        acc += frame_wait();
        l2_flip();
        if (acc > 8) acc = 8;
        map_rules();
        while (acc >= 2) {                      /* the original's 25 ticks a second */
            cr_tick(&game, cr_bot(&game));
            acc -= 2;
            if (game.over || game.won) cr_new_game(&game, game.seed + 1, 1);
        }
        map_render();
        render_scene(&game);
        if (++loops == 50) {
            zx_log("canyon: fps*10=%u polys=%u s=%ld\n", (uint16_t)(5000U * 5 / (uint16_t)(frames50 - tstart)),
                   render_polys, (long)game.cam_s);
            loops = 0;
            tstart = frames50;
        }
    }
}
#endif
