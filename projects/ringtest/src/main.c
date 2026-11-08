/*
 * ringtest - sdk l2ring.h: a 700 x 520 picture (tools/gen.py) scrolled on
 * Layer 2 320 x 256 in every direction, by jumps and off its edges; after
 * each move it logs where it is and waits three frames, and test.py checks
 * Layer 2 against l2ring.py's model.
 */
#include <arch/zxn.h>
#include <z80.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "palfx.h"
#include "zxlog.h"
#include "l2ring.h"

static const int16_t path[][2] = {
    { 0, 0 }, { 5, 0 }, { 10, 0 }, { 17, 0 },            /* right */
    { 17, 7 }, { 17, 14 }, { 17, 30 },                    /* down */
    { 4, 30 }, { -9, 30 },                                /* left, off the left edge */
    { -9, 21 }, { -9, -12 }, { -9, -40 },                 /* up, off the top */
    { 11, -23 }, { 40, 10 }, { 20, 50 },                  /* diagonals */
    { 330, 210 }, { 360, 240 }, { 390, 270 },             /* a jump, then to the bottom right */
    { 420, 300 }, { 900, 800 }, { 200, 150 }             /* past it all, back */
};

int main(void)
{
    uint8_t i, n;
    uint16_t pal[16];
    next_init();
    ZXN_WRITE_REG(0x12, 9);
    ZXN_WRITE_REG(0x70, 0x10);           /* 320 x 256 */
    ZXN_WRITE_REG(0x1C, 1);
    ZXN_WRITE_REG(0x18, 0);
    ZXN_WRITE_REG(0x18, 159);
    ZXN_WRITE_REG(0x18, 0);
    ZXN_WRITE_REG(0x18, 255);
    z80_outp(0x123B, 0x02);
    lr_open(40);
    n = lr_palette(pal, 16);
    palfx_load(PAL_L2, 0, n, pal);
    frame_init();
    zx_log("ring: open w=%d h=%d ox=%d oy=%d void=%u colours=%u\n", lr_w, lr_h, lr_ox, lr_oy, lr_void, n);
    for (i = 0; i < sizeof(path) / sizeof(path[0]); i++) {
        lr_cols = lr_rows = 0;
        lr_goto(path[i][0], path[i][1]);
        zx_log("ring: at %d %d cols=%u rows=%u\n", lr_x, lr_y, lr_cols, lr_rows);
        frame_wait();                    /* time for the test to look */
        frame_wait();
        frame_wait();
    }
    zx_log("ZX:DONE\n");
    for (;;) frame_wait();
}
