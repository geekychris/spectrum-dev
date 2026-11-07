/*
 * sdktest48gfx - exercises the 48K drawing modules (bitline, dots48, hmap48,
 * gauge48, attrtint, spr32_shift) and fixmath's sin14 / muls / rot2d, and
 * logs results; test.py checks them against Python and the screen.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "zxgame.h"
#include "zxlog.h"
#include "fixmath.h"
#include "bitline.h"
#include "dots48.h"
#include "hmap48.h"
#include "gauge48.h"
#include "attrtint.h"

static uint8_t buf[4 * 8], *buf_rows[8];
static uint8_t h[256];
static const uint8_t solid[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const int8_t tri[6] = {0, 0, 10, 0, 0, 10};
static const int8_t pts[6] = {10, 0, 0, -8, 3, 4};
static int8_t rot[6];
static uint8_t dots[6] = {40, 40, 41, 41, 42, 42};
static const uint8_t shape[8] = {0xF0, 0x0F, 0xAA, 0x01, 0x81, 0x00, 0xFF, 0x00};
static uint8_t shifted[8];

int main(void)
{
    uint8_t i;
    gauge g;
    tint_area t = {0, 0, 0, 0};

    gfx_init();
    screen_clear(PAPER_WHITE | INK_BLACK, INK_BLUE);
    frame_init();

    /* fixmath: Q14 sine, the small multiply, rotation */
    zx_log("gfx: sin14 %d %d %d %d\n", fix_sin14(0), fix_sin14(64), fix_cos14(128), fix_sin14(32));
    zx_log("gfx: muls %d %d %d %d\n", muls(7, 205), muls(-12, 192), muls(5, -300), muls(0, 999));
    rot2d_set(64, 256, 256);            /* a quarter turn: (x, y) -> (-y, x) */
    rot2d(pts, rot, 3);
    zx_log("gfx: rot64 %d,%d %d,%d %d,%d\n", rot[0], rot[1], rot[2], rot[3], rot[4], rot[5]);
    rot2d_set(32, 205, 192);
    rot2d(pts, rot, 3);
    zx_log("gfx: rot32 %d,%d %d,%d %d,%d\n", rot[0], rot[1], rot[2], rot[3], rot[4], rot[5]);

    /* bitline on the screen: a box's diagonals at (8..40, 8..30) in OR */
    bl_mode = BL_OR;
    bl_draw(8, 8, 40, 30);
    bl_draw(40, 8, 8, 30);
    bl_draw(8, 40, 8, 40);            /* one pixel */
    bl_draw_open(50, 8, 60, 8);       /* (60, 8) left out */
    bl_draw(-100, 60, 400, 60);       /* clipped to the screen */
    bl_draw(70, -20, 70, 300);
    bl_cx0 = 100; bl_cx1 = 140; bl_cy0 = 100; bl_cy1 = 120;
    bl_draw(90, 110, 150, 110);       /* clipped to 100..140 */
    bl_screen();
    /* XOR: twice = nothing; a closed polygon keeps its vertices */
    bl_mode = BL_XOR;
    bl_draw(150, 10, 200, 30);
    bl_draw(150, 10, 200, 30);
    bl_poly(tri, 3, 160, 40, 1);
    /* into a buffer through its own row table */
    for (i = 0; i < 8; i++) buf_rows[i] = buf + i * 4;
    bl_rows = buf_rows;
    bl_cx0 = 0; bl_cx1 = 31; bl_cy0 = 0; bl_cy1 = 7;
    bl_mode = BL_OR;
    bl_draw(0, 0, 31, 7);
    bl_draw(-5, 3, 40, 3);
    bl_screen();
    bl_mode = BL_XOR;
    zx_log("gfx: buf");
    for (i = 0; i < 32; i++) zx_log(" %u", buf[i]);
    zx_log("\n");

    /* dots48: three XORed once, then the first two again (= off) */
    dots_xor(dots, 3);
    dots_xor(dots, 2);

    /* hmap48: steps 140 + x / 16 from x = 0, solid fill 3 below the surface to 170 */
    for (i = 0;; i++) {
        h[i] = 140 + (i >> 4);
        if (i == 255) break;
    }
    h[100] = 130;                       /* a spike: the line joins it */
    hmap_line(h, 0);
    hmap_fill(h, 3, 170, solid);
    hmap_colour(h, 17, 21, PAPER_WHITE | INK_BLUE, PAPER_WHITE | INK_RED, PAPER_WHITE | INK_GREEN,
                (uint8_t *)0x5800);

    /* gauge48 on row 23: 10 cells, length 50 then 30 */
    gauge_init(&g, 2, 23, 10, PAPER_WHITE | INK_BLACK);
    gauge_set(&g, 50, PAPER_WHITE | INK_GREEN);
    gauge_set(&g, 30, PAPER_WHITE | INK_RED);
    zx_log("gfx: gauge len=%u max=%u half=%u big=%u\n", g.len, GAUGE_MAX(&g), gauge_len(&g, 50, 100),
           gauge_len(&g, 40000, 60000));

    /* attrtint on rows 0..1, cols 24..31: plain = white paper, black ink */
    tint_plain = PAPER_WHITE | INK_BLACK;
    attr_set(26, 0, PAPER_YELLOW);      /* scenery: never tinted */
    tint_capture();
    tint_place(&t, 24, 0, 4, 2, PAPER_CYAN);
    zx_log("gfx: tint %u %u %u plain=%u,%u\n", *(uint8_t *)(0x5800 + 24), *(uint8_t *)(0x5800 + 26),
           *(uint8_t *)(0x5800 + 32 + 27), tint_is_plain(24), tint_is_plain(26));
    tint_restore(&t);
    tint_mark(30, 1, 2, 0);
    zx_log("gfx: untint %u %u %u marked=%u\n", *(uint8_t *)(0x5800 + 24), *(uint8_t *)(0x5800 + 26),
           *(uint8_t *)(0x5800 + 32 + 27), tint_is_plain(32 + 30));

    /* spr32_shift: 2 lines shifted right by 3 and by 0 */
    spr32_shift(shifted, shape, 2, 3);
    zx_log("gfx: shift3 %u %u %u %u %u %u %u %u\n", shifted[0], shifted[1], shifted[2], shifted[3],
           shifted[4], shifted[5], shifted[6], shifted[7]);
    spr32_shift(shifted, shape, 2, 0);
    zx_log("gfx: shift0 %u %u %u %u\n", shifted[0], shifted[1], shifted[2], shifted[3]);

    zx_log("ZX:DONE\n");
    for (;;) frame_wait();
}
