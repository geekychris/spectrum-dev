/*
 * scrolltest - sdk l2scroll.h (banded scrolling on Layer 2's 320 x 256
 * mode): three bands whose columns are filled with a pattern of the world x
 * they show, moved right, left, by more than a screen, and one column
 * redrawn; the program checks the ring itself and logs, and test.py checks
 * Layer 2's memory, the copper list and the picture.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "nextcopper.h"
#include "nextsplit.h"
#include "l2scroll.h"

static uint16_t calls;
static uint8_t redraw_mark;

/* band b's rows get world x's low byte (+ a mark for redrawn columns) */
static void fill(uint8_t b, uint32_t wx, uint8_t *col)
{
    uint8_t y, y0 = b == 0 ? 0 : b == 1 ? 80 : 176, y1 = b == 0 ? 79 : b == 1 ? 175 : 255, v = (uint8_t)wx;
    if (v == 0xE3) v = 0xE2;            /* not the transparent colour */
    for (y = y0;; y++) {
        col[y] = v;
        if (y == y1) break;
    }
    col[y0] = redraw_mark ? 0x1C : v;
    calls++;
}

/* every column of band b shows world x0 + screen x? (row y of the band) */
static uint16_t check(uint8_t b, uint32_t x0, uint8_t y)
{
    uint16_t sx, bad = 0, off = l2s_offset(b);
    for (sx = 0; sx < L2S_W; sx++) {
        uint16_t col = off + sx;
        uint8_t v = (uint8_t)(x0 + sx);
        if (col >= L2S_W) col -= L2S_W;
        if (v == 0xE3) v = 0xE2;
        if (l2w_column(col)[y] != v) bad++;
    }
    l2w_done();
    return bad;
}

int main(void)
{
    uint8_t a, b, c;
    next_init();
    l2w_slot = 2;
    l2w_restore = 10;
    l2w_init(16);
    frame_init();
    l2s_reset();
    a = l2s_band(0, 79, fill);
    b = l2s_band(80, 175, fill);
    c = l2s_band(176, 255, fill);

    calls = 0;
    l2s_goto(a, 0);
    l2s_goto(b, 1000);
    l2s_goto(c, 70000UL);                /* beyond 16 bits */
    zx_log("scroll: goto calls=%u bad=%u,%u,%u off=%u,%u,%u\n", calls, check(a, 0, 40), check(b, 1000, 100),
           check(c, 70000UL, 200), l2s_offset(a), l2s_offset(b), l2s_offset(c));

    calls = 0;
    l2s_move(b, 1003);                   /* right 3: 3 columns */
    l2s_move(c, 69990UL);                /* left 10: 10 columns */
    zx_log("scroll: move calls=%u bad=%u,%u off=%u,%u\n", calls, check(b, 1003, 100), check(c, 69990UL, 200),
           l2s_offset(b), l2s_offset(c));

    calls = 0;
    l2s_move(b, 1003 + 500);             /* more than a screen: everything */
    zx_log("scroll: jump calls=%u bad=%u off=%u\n", calls, check(b, 1503, 100), l2s_offset(b));

    calls = 0;
    redraw_mark = 1;
    l2s_redraw(c, 69990UL + 7);          /* in view: drawn */
    l2s_redraw(c, 69990UL + 400);        /* out of view: not */
    redraw_mark = 0;
    zx_log("scroll: redraw calls=%u col=%u\n", calls,
           (uint16_t)((l2s_offset(c) + 7) % L2S_W));

    split_begin();
    l2s_copper();
    zx_log("scroll: copper n=%u\n", split_end());
    zx_log("ZX:DONE\n");
    for (;;) frame_wait();
}
