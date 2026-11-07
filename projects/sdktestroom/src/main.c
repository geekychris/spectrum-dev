/*
 * sdktestroom - tests the tile room modules on a 48K: tmap + plat cases
 * (plat_cases.c, also built on the host by test.py: the logs must match),
 * then cellspr48: a room with a sprite composed over its tiles, moved,
 * switched off, cs_refresh, cs_scroll (left and right, with a sprite) and
 * cs_put_tile after cs_wipe_sprites;
 * spr16_shift against spr_preshift's copies. test.py checks the screen bytes.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "zxgame.h"
#include "zxlog.h"
#include "cellspr48.h"
#include "cases.h"

static uint8_t tiles[3 * 8] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF,
    0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55,
};
static const uint8_t attrs[3] = { INK_WHITE, PAPER_RED | INK_YELLOW, INK_GREEN };
static uint8_t map[6 * 4] = {
    1, 1, 1, 1, 1, 1,
    1, 0, 0, 2, 0, 1,
    1, 0, 2, 2, 0, 1,
    1, 1, 1, 1, 1, 1,
};
static const uint8_t cross[8] = { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 };
static uint8_t cross_pre[8 * 2 * 8];
static const uint8_t ref_c[3] = { 3, 2, 3 }, ref_r[3] = { 1, 2, 2 };
/* cs_scroll: the column that comes in on the right (1 a row), then the two on the left */
static const uint8_t fill_r[4] = { 1, 0, 2, 1 }, fill_l[8] = { 1, 1, 2, 0, 0, 2, 1, 1 };
static const uint8_t figure[2 * 5] = { 0x80, 0x01, 0xFF, 0xFF, 0x3C, 0x5A, 0x00, 0x00, 0xA5, 0x0F };
static uint8_t fig_pre[8 * 3 * 5], fig_one[3 * 5];

/* spr16_shift makes each of spr_preshift's 8 copies */
static void spr16_cases(void)
{
    uint8_t s, bad = 0;
    spr_preshift(figure, 2, 5, fig_pre);
    for (s = 0; s < 8; s++) {
        spr16_shift(fig_one, figure, 5, s);
        if (memcmp(fig_one, fig_pre + s * 15, 15)) bad++;
    }
    zx_log("spr16: shifts=8 bad=%u\n", bad);
}

/* test.py reads the screen at each "room:" line (the program waits 3 frames) */
static void step(const char *what)
{
    zx_log("room: %s cells=%u\n", what, cs_last_cells);
    frame_wait();
    frame_wait();
    frame_wait();
}

int main(void)
{
    gfx_init();
    screen_clear(INK_WHITE, INK_BLACK);
    frame_init();
    cases();
    spr16_cases();

    spr_preshift(cross, 1, 8, cross_pre);
    cs_init(map, 6, 4, 4, 4, tiles, attrs);
    cs_nspr = 2;
    cs_spr[0].x = 13; cs_spr[0].y = 10;           /* over cells (1..2, 1..2) */
    cs_spr[0].gfx = cross_pre; cs_spr[0].w = 1; cs_spr[0].h = 8;
    cs_spr[0].ink = INK_CYAN | BRIGHT; cs_spr[0].keep = 0x38; cs_spr[0].on = 1;
    cs_draw_all();
    step("drawn");
    cs_spr[0].x = 24; cs_spr[0].y = 16;           /* aligned: cell (3, 2) only */
    cs_update();
    step("moved");
    cs_update();
    step("still");
    tiles[2 * 8 + 1] = 0x0F;                      /* tile 2's lines 1..3 change */
    tiles[2 * 8 + 2] = 0xF0;
    tiles[2 * 8 + 3] = 0x0F;
    cs_refresh(ref_c, ref_r, 3, 1, 3);            /* (3, 2) is under the sprite: marked */
    zx_log("room: refresh marked=%u\n", cs_list_n);
    frame_wait();
    frame_wait();
    frame_wait();
    cs_update();
    step("refreshed");
    cs_spr[0].on = 0;
    cs_set(1, 1, 2);
    cs_update();
    step("off");
    cs_spr[0].on = 1;                             /* (24, 16): cell (3, 2) */
    cs_update();
    step("on");
    cs_scroll(1, fill_r);                         /* the view moves a cell right */
    cs_spr[0].x = 16;                             /* the sprite stays with the room */
    cs_update();
    step("scrolled");
    cs_scroll(-2, fill_l);                        /* two cells left */
    cs_spr[0].x = 29;
    cs_update();
    step("back");
    cs_wipe_sprites(0);                           /* a view that jumps: new tiles */
    cs_put_tile(0, 1, 2);
    cs_put_tile(5, 2, 0);
    cs_spr[0].x = 8;
    cs_update();
    step("put");
    zx_log("ZX:DONE\n");
    for (;;) frame_wait();
}
